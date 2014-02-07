/*
 * vm.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 03/05/2013
 * Licensed under the 2-clause BSD License
 *
 * The Sparkling virtual machine
 */

#include <limits.h>
#include <stdarg.h>
#include <assert.h>
#include <stddef.h>

#include "vm.h"
#include "str.h"
#include "func.h"
#include "private.h"

/* stack management macros 
 * 0th slot: header
 * register ordinal numbers grow _downwards_
 *
 * |                          | <- SP
 * +--------------------------+
 * | activation record header | <- SP - 1
 * +--------------------------+
 * | register #0              | <- SP - 2
 * +--------------------------+
 * | register #1              | <- SP - 3
 * +--------------------------+
 * |                          |
 * 
 * 
 * register layout within a stack frame:
 * 
 * [0...argc)			- declared arguments
 * [argc...nregs)		- other local variables and temporary registers
 * [nregs...nregs + extra_argc)	- unnamed (variadic) arguments
 */

#define EXTRA_SLOTS	1
#define IDX_FRMHDR	(-1)
#define REG_OFFSET	(-2)

/* the debug versions of the following macros are defined in such a horrible
 * way because once I've shot myself in the foot trying to store the result of
 * reg[1] + reg[2] in reg[3], whereas there were only 3 registers in the frame
 * (obviously I meant reg[0] as destination and not reg[3]). This caused
 * a quite nasty bug: it was overwriting *half* of the global symbol table,
 * and I was wondering why the otherwise correct SpnArray implementation
 * crashed upon deallocation. It turned out that the array and hash count of
 * the object have been changed to bogus values, but the `isa` field and the
 * allocsize members stayed intact. Welp.
 *
 * So, unless I either stop generating bytecode by hand or I'm entirely sure
 * that the compiler is perfect, I'd better stick to asserting that the
 * register indices are within the bounds of the stack frame...
 * 
 * arguments: `s` - stack pointer; `r`: register index
 */
#ifndef NDEBUG
#define VALPTR(s, r) (assert((r) < (s)[IDX_FRMHDR].h.size - EXTRA_SLOTS), &(s)[(-(int)(r) + REG_OFFSET)].v)
#define SLOTPTR(s, r) (assert((r) < (s)[IDX_FRMHDR].h.size - EXTRA_SLOTS), &(s)[(-(int)(r) + REG_OFFSET)])
#else
#define VALPTR(s, r) (&(s)[(-(int)(r) + REG_OFFSET)].v)
#define SLOTPTR(s, r) (&(s)[(-(int)(r) + REG_OFFSET)])
#endif


/* a local symbol table.
 * `bc' is a pointer to the bytecode out of which the symbol table was read.
 * This is necessary because local symbol tables need to be uniqued;
 * even if a certain file is run multiple times, its symtab should
 * only be created/added once. (duplicated symtabs do no real harm, but their
 * memory usage can add up if a certain file is run thousands of times...)
 */
typedef struct TSymtab {
	SpnValue *vals;
	size_t size;
	spn_uword *bc;
} TSymtab;

/* An index into the array of local symbol tables is used instead of a
 * pointer to the symtab struct itself because that array may be
 * `realloc()`ated, and then we have an invalid pointer once again
 */
typedef struct TFrame {
	size_t		 size;		/* no. of slots, including EXTRA_SLOTS	*/
	int		 decl_argc;	/* declaration argument count		*/
	int		 extra_argc;	/* number of extra args, if any (or 0)	*/
	int		 real_argc;	/* number of call args			*/
	spn_uword	*retaddr;	/* return address (points to bytecode)	*/
	ptrdiff_t	 retidx;	/* pointer into the caller's frame	*/
	spn_uword	*env;		/* bytecode of the TU of the function 	*/
	const char	*fnname;	/* name of the function being called	*/
} TFrame;

/* see http://stackoverflow.com/q/18310789/ */
typedef union TSlot {
	TFrame h;
	SpnValue v;
} TSlot;

struct SpnVMachine {
	TSlot		*stack;		/* base of the stack		*/
	TSlot		*sp;		/* stack pointer		*/
	size_t		 stackallsz;	/* stack alloc size in frames	*/

	SpnArray	*glbsymtab;	/* global symbol table		*/

	TSymtab		*lsymtabs;	/* local symbol table		*/
	size_t		 lscount;	/* number of the local symtabs	*/

	char		*errmsg;	/* last (runtime) error message	*/
	int		 haserror;	/* an error occurred		*/
	void		*ctx;		/* context info, use at will	*/
};

/* this is the structure used by `push_and_copy_args()' */
struct args_copy_descriptor {
	int caller_is_native;
	union {
		struct {
			spn_uword *ip;
			spn_uword *retaddr;
			ptrdiff_t calleroff;
			ptrdiff_t retidx;
		} script_env;
		struct {
			SpnValue *argv;
		} native_env;
	} env;
};

static void push_and_copy_args(
	SpnVMachine *vm,
	const SpnValue *fn,
	const struct args_copy_descriptor *desc,
	int argc
);


static int dispatch_loop(SpnVMachine *vm, spn_uword *ip, SpnValue *ret);

/* this only releases the values stored in the stack frames */
static void free_frames(SpnVMachine *vm);

/* stack manipulation */
static size_t stacksize(SpnVMachine *vm);
static void push_first_frame(SpnVMachine *vm, spn_uword *env);
static void expand_stack(SpnVMachine *vm, size_t nregs);
static void push_frame(
	SpnVMachine *vm,
	int nregs,
	int decl_argc,
	int extra_argc,
	int real_argc,
	spn_uword *retaddr,
	ptrdiff_t retidx,
	spn_uword *env,
	const char *fnname
);
static void pop_frame(SpnVMachine *vm);

/* this function helps including native functions' names in the stack trace */
static void push_native_pseudoframe(SpnVMachine *vm, const char *fnname);

/* bytecode validation - returns 0 on success, nonzero on error */
static int validate_magic(SpnVMachine *vm, spn_uword *bc);

/* returns the index of the symtab of `bc`, creates local symtab if
 * necessary. Returns -1 on error.
 */
static void read_local_symtab(SpnVMachine *vm, spn_uword *bc);

/* accessing function arguments */
static SpnValue *nth_call_arg(TSlot *sp, spn_uword *ip, int idx);
static SpnValue *nth_vararg(TSlot *sp, int idx);

/* emulating the ALU... */
static int numeric_compare(const SpnValue *lhs, const SpnValue *rhs, int op);
static int cmp2bool(int res, int op);
static SpnValue arith_op(const SpnValue *lhs, const SpnValue *rhs, int op);
static long bitwise_op(const SpnValue *lhs, const SpnValue *rhs, int op);

/* ...and a weak dynamic linker */
static int resolve_symbol(SpnVMachine *vm, spn_uword *ip, SpnValue *symp);

/* type information, reflection */
static SpnValue sizeof_value(SpnValue *val);
static SpnValue typeof_value(SpnValue *val);

/* generating a runtime error (message) */
static void runtime_error(SpnVMachine *vm, spn_uword *ip, const char *fmt, const void *args[]);

/* releases the entries of a local symbol table */
static void free_local_symtab(TSymtab *symtab)
{
	size_t i;
	for (i = 0; i < symtab->size; i++) {
		spn_value_release(&symtab->vals[i]);
	}

	free(symtab->vals);
}

SpnVMachine *spn_vm_new(void)
{
	SpnVMachine *vm = spn_malloc(sizeof(*vm));

	/* initialize stack */
	vm->stackallsz = 0;
	vm->stack = NULL;
	vm->sp = NULL;

	/* initialize the global and local symbol tables */
	vm->glbsymtab = spn_array_new();
	vm->lsymtabs = NULL;
	vm->lscount = 0;

	/* set up error reporting and context info */
	vm->errmsg = NULL;
	vm->haserror = 0;
	vm->ctx = NULL;

	return vm;
}

void spn_vm_free(SpnVMachine *vm)
{
	size_t i;

	/* free the stack */
	free_frames(vm);
	free(vm->stack);

	/* free the global symbol table */
	spn_object_release(vm->glbsymtab);

	/* free each local symbol table... */
	for (i = 0; i < vm->lscount; i++) {
		free_local_symtab(&vm->lsymtabs[i]);
	}

	/* ...then free the array that contains them */
	free(vm->lsymtabs);

	/* free the error message buffer */
	free(vm->errmsg);

	free(vm);
}

const char **spn_vm_stacktrace(SpnVMachine *vm, size_t *size)
{
	size_t i = 0;
	const char **buf;

	TSlot *sp = vm->sp;

	/* handle uninitialized stack */
	if (sp == NULL) {
		*size = 0;
		return NULL;
	}

	/* count frames */
	while (sp > vm->stack) {
		TFrame *frmhdr = &sp[IDX_FRMHDR].h;
		i++;
		sp -= frmhdr->size;
	}

	/* allocate buffer */
	buf = spn_malloc(i * sizeof(*buf));

	*size = i;

	i = 0;
	sp = vm->sp;
	while (sp > vm->stack) {
		TFrame *frmhdr = &sp[IDX_FRMHDR].h;
		buf[i++] = frmhdr->fnname ? frmhdr->fnname : SPN_LAMBDA_NAME;
		sp -= frmhdr->size;
	}

	return buf;
}

SpnArray *spn_vm_getglobals(SpnVMachine *vm)
{
	return vm->glbsymtab;
}

static void free_frames(SpnVMachine *vm)
{
	if (vm->stack != NULL) {
		while (vm->sp > vm->stack) {
			pop_frame(vm);
		}
	}
}

static void clean_vm_if_needed(SpnVMachine *vm)
{
	if (vm->haserror) {
		/* releases values in stack frames from the previous execution.
		 * This is not done immediately after the dispatch loop,
		 * because if a runtime error occurs, we want the backtrace
		 * functions to be able to unwind the stack.
		 */
		free_frames(vm);

		/* clear the "there was an error" flag */
		vm->haserror = 0;
	}
}

int spn_vm_exec(SpnVMachine *vm, spn_uword *bc, SpnValue *retval)
{
	spn_uword *ip;

	/* clean up after a previous program having thrown a runtime error */
	clean_vm_if_needed(vm);

	/* check bytecode for magic bytes */
	if (validate_magic(vm, bc) != 0) {
		return -1;
	}

	/* read the local symbol table, if necessary */
	read_local_symtab(vm, bc);

	/* initialize global stack (read 1st frame size from bytecode) */
	push_first_frame(vm, bc);

	/* initialize instruction pointer to beginning of TU */
	ip = bc + SPN_PRGHDR_LEN;

	/* actually run the program */
	return dispatch_loop(vm, ip, retval);
}

int spn_vm_callfunc(
	SpnVMachine *vm,
	const SpnValue *fn,
	SpnValue *retval,
	int argc,
	SpnValue *argv
)
{
	struct args_copy_descriptor desc;
	spn_uword *fnhdr;
	spn_uword *entry;

	/* if this is the first call after the execution of a program
	 * in which an error occurred, unwind the stack automagically
	 */
	clean_vm_if_needed(vm);

	/* ensure that the callee is indeed a function */
	if (!isfunc(fn)) {
		spn_vm_seterrmsg(vm, "attempt to call non-function value", NULL);
		return -1;
	}

	/* native functions are easy to deal with */
	if (funcvalue(fn)->native) {
		int err;

		/* push pseudo-frame to include function name in stack trace */
		push_native_pseudoframe(vm, funcvalue(fn)->name);

		/* "return nothing" should mean "implicitly return nil" */
		*retval = makenil();

		err = funcvalue(fn)->repr.fn(retval, argc, argv, vm->ctx);
		if (err != 0) {
			const void *args[2];
			args[0] = funcvalue(fn)->name;
			args[1] = &err;
			spn_vm_seterrmsg(vm, "error in function `%s' (code: %i)", args);
		} else {
			pop_frame(vm);
		}

		return err;
	}

	/* if we got here, the callee is a valid Sparkling function. */
	fnhdr = funcvalue(fn)->repr.bc;
	entry = fnhdr + SPN_FUNCHDR_LEN;

	desc.caller_is_native = 1; /* because we are the calling function */
	desc.env.native_env.argv = argv;

	/* push frame and bind arguments */
	push_and_copy_args(vm, fn, &desc, argc);

	/* recurse, because it's convenient */
	return dispatch_loop(vm, entry, retval);
}

void spn_vm_addlib_cfuncs(SpnVMachine *vm, const char *libname, const SpnExtFunc fns[], size_t n)
{
	SpnArray *storage;
	size_t i;

	/* a NULL libname means that the functions will be global */
	if (libname != NULL) {
		SpnValue libval;

		storage = spn_array_new();

		libval.type = SPN_TYPE_ARRAY;
		libval.v.o = storage;

		spn_array_set_strkey(vm->glbsymtab, libname, &libval);
		spn_object_release(storage); /* still alive, was retained */
	} else {
		storage = vm->glbsymtab;
	}

	for (i = 0; i < n; i++) {
		SpnValue val = makenativefunc(fns[i].name, fns[i].fn);
		spn_array_set_strkey(storage, fns[i].name, &val);
		spn_value_release(&val);
	}
}

void spn_vm_addlib_values(SpnVMachine *vm, const char *libname, const SpnExtValue vals[], size_t n)
{
	SpnArray *storage;
	size_t i;

	/* a NULL libname means that the functions will be global */
	if (libname != NULL) {
		SpnValue libval;

		storage = spn_array_new();

		libval.type = SPN_TYPE_ARRAY;
		libval.v.o = storage;

		spn_array_set_strkey(vm->glbsymtab, libname, &libval);
		spn_object_release(storage); /* still alive, was retained */
	} else {
		storage = vm->glbsymtab;
	}

	for (i = 0; i < n; i++) {
		spn_array_set_strkey(storage, vals[i].name, &vals[i].value);
	}
}

/* ip == NULL indicates an error in native code
 * Apart from creating an error message, this function also sets
 * the `haserror` flag, which is essential for the stack trace
 * and function calling mechanisms to work.
 */
static void runtime_error(SpnVMachine *vm, spn_uword *ip, const char *fmt, const void *args[])
{
	char *prefix, *msg;
	size_t prefix_len, msg_len;

	/* self-protection: don't overwrite previous error message */
	if (vm->haserror) {
		return;
	}

	if (ip != NULL) {
		unsigned long addr = ip - vm->sp[IDX_FRMHDR].h.env;
		const void *prefix_args[1];
		prefix_args[0] = &addr;
		prefix = spn_string_format_cstr(
			"Sparkling: at address 0x%08x: runtime error: ",
			&prefix_len,
			prefix_args
		);
	} else {
		prefix = spn_string_format_cstr( /* glorified strdup() */
			"Sparkling: in native code: runtime error: ",
			&prefix_len,
			NULL
		);
	}

	msg = spn_string_format_cstr(fmt, &msg_len, args);

	free(vm->errmsg);
	vm->errmsg = spn_malloc(prefix_len + msg_len + 1);

	strcpy(vm->errmsg, prefix);
	strcpy(vm->errmsg + prefix_len, msg);

	free(prefix);
	free(msg);

	/* self-protection */
	vm->haserror = 1;
}

const char *spn_vm_geterrmsg(SpnVMachine *vm)
{
	return vm->errmsg;
}

void spn_vm_seterrmsg(SpnVMachine *vm, const char *fmt, const void *args[])
{
	runtime_error(vm, NULL, fmt, args);
}

void *spn_vm_getcontext(SpnVMachine *vm)
{
	return vm->ctx;
}

void spn_vm_setcontext(SpnVMachine *vm, void *ctx)
{
	vm->ctx = ctx;
}

/* because `NULL - NULL` is UB */
static size_t stacksize(SpnVMachine *vm)
{
	return vm->stackallsz != 0 ? vm->sp - vm->stack : 0;
}

static void expand_stack(SpnVMachine *vm, size_t nregs)
{
	size_t oldsize = stacksize(vm);
	size_t newsize = oldsize + nregs;

	if (vm->stackallsz == 0) {
		vm->stackallsz = 8;
		/* or however many; something greater than 0 so the << works */
	}

	while (vm->stackallsz < newsize) {
		vm->stackallsz <<= 1;
	}

	vm->stack = spn_realloc(vm->stack, vm->stackallsz * sizeof(vm->stack[0]));

	/* re-initialize stack pointer because we realloc()'d the stack */
	vm->sp = vm->stack + oldsize;
}

/* nregs is the logical size (without the activation record header)
 * of the new stack frame, in slots
 */
static void push_frame(
	SpnVMachine *vm,
	int nregs,
	int decl_argc,
	int extra_argc,
	int real_argc,
	spn_uword *retaddr,
	ptrdiff_t retidx,
	spn_uword *env,
	const char *fnname
)
{
	int i;

	/* real frame allocation size, including extra call-time arguments,
	 * frame header and implicit self
	 */
	int real_nregs = nregs + extra_argc + EXTRA_SLOTS;

	/* just a bit of sanity check in case someone misinterprets how
	 * `extra_argc` is computed...
	 */
	assert(extra_argc >= 0);

	/* reallocate stack if necessary */
	if (vm->stackallsz == 0					/* empty stack	*/
	 || vm->stackallsz < stacksize(vm) + real_nregs) {	/* too small	*/
		expand_stack(vm, real_nregs);
	}

	/* adjust stack pointer */
	vm->sp += real_nregs;

	/* initialize registers to nil */
	for (i = -real_nregs; i < -EXTRA_SLOTS; i++) {
		vm->sp[i].v = makenil();
	}

	/* initialize activation record header */
	vm->sp[IDX_FRMHDR].h.size = real_nregs;
	vm->sp[IDX_FRMHDR].h.decl_argc = decl_argc;
	vm->sp[IDX_FRMHDR].h.extra_argc = extra_argc;
	vm->sp[IDX_FRMHDR].h.real_argc = real_argc;
	vm->sp[IDX_FRMHDR].h.retaddr = retaddr; /* if NULL, return to C-land */
	vm->sp[IDX_FRMHDR].h.retidx = retidx; /* if negative, return to C-land */
	vm->sp[IDX_FRMHDR].h.env = env;
	vm->sp[IDX_FRMHDR].h.fnname = fnname;
}

static void push_first_frame(SpnVMachine *vm, spn_uword *env)
{
	/* the next word in the bytecode is the number of global registers */
	size_t nregs = env[SPN_HDRIDX_FRMSIZE];

	/* push a large enough frame - return address: NULL (nowhere to
	 * return from top-level program scope). A return value index < 0
	 * (-1 in this case) indicates that the program or function returns to
	 * C-land, and instead of indexing the stack, the return value should
	 * be copied directly into the return value pointer.
	 */
	push_frame(vm, nregs, 0, 0, 0, NULL, -1, env, "<main program>");
}

static void push_native_pseudoframe(SpnVMachine *vm, const char *fnname)
{
	push_frame(vm, 0, 0, 0, 0, NULL, -1, NULL, fnname);
}

static void pop_frame(SpnVMachine *vm)
{
	/* we need to release all values when popping a frame, since
	 * computations/instructions are designed so that each step
	 * (e. g. concatenation, function calls, etc.) cleans up its
	 * destination register, but not the source(s) (if any).
	 */
	TFrame *hdr = &vm->sp[IDX_FRMHDR].h;
	int nregs = hdr->size;

	/* release registers */
	int i;
	for (i = -nregs; i < -EXTRA_SLOTS; i++) {
		spn_value_release(&vm->sp[i].v);
	}

	/* adjust stack pointer */
	vm->sp -= nregs;
}

/* retrieve a pointer to the register denoted by the `idx`th octet
 * of an array of `spn_uword`s (which is the instruction pointer)
 */
static SpnValue *nth_call_arg(TSlot *sp, spn_uword *ip, int idx)
{
	int regidx = nth_arg_idx(ip, idx);
	return VALPTR(sp, regidx);
}

/* get the `idx`th unnamed argument from a stack frame
 * XXX: this does **NOT** check for an out-of-bounds error in release
 * mode, that's why `argidx < hdr->extra_argc` is checked in SPN_INS_NTHARG.
 */
static SpnValue *nth_vararg(TSlot *sp, int idx)
{
	TFrame *hdr = &sp[IDX_FRMHDR].h;
	int vararg_off = hdr->size - EXTRA_SLOTS - hdr->extra_argc;

	assert(idx >= 0 && idx < hdr->extra_argc);

	return VALPTR(sp, vararg_off + idx);
}


/* helper for calling Sparkling functions (pushes frame, copies arguments) */
static void push_and_copy_args(
	SpnVMachine *vm,
	const SpnValue *fn,
	const struct args_copy_descriptor *desc,
	int argc
)
{
	int i;
	spn_uword *fnhdr;
	spn_uword *env;
	int decl_argc;
	int extra_argc;
	int nregs;
	const char *fnname;

	/* this whole copying thingy only applies if we're calling
	 * a Sparkling function. Native callees are handled separately.
	 */
	assert(isfunc(fn) && funcvalue(fn)->native == 0);

	/* see Remark (VI) in vm.h in order to get an
	 * understanding of the layout of the
	 * bytecode representing a function
	 */
	fnhdr = funcvalue(fn)->repr.bc;
	env = funcvalue(fn)->env;
	decl_argc = fnhdr[SPN_FUNCHDR_IDX_ARGC];
	nregs = fnhdr[SPN_FUNCHDR_IDX_NREGS];
	fnname = funcvalue(fn)->name;

	/* if there are less call arguments than formal
	 * parameters, we set extra_argc to 0 (and all
	 * the unspecified arguments are implicitly set
	 * to `nil` by push_frame)
	 */
	extra_argc = argc > decl_argc ? argc - decl_argc : 0;

	/* sanity check: decl_argc should be <= nregs,
	 * else arguments wouldn't fit in the registers
	 */
	assert(decl_argc <= nregs);

	/* push a new stack frame - after that,
	 * `&vm->sp[IDX_FRMHDR].h` is a pointer to
	 * the stack frame of the *called* function.
	 */
	push_frame(
		vm,
		nregs,
		decl_argc,
		extra_argc,
		argc,
		desc->caller_is_native ? NULL : desc->env.script_env.retaddr,
		desc->caller_is_native ?   -1 : desc->env.script_env.retidx,
		env,
		fnname
	);

	/* first, fill in arguments that fit into the
	 * first `decl_argc` registers (i. e. those
	 * that are declared as formal parameters). The
	 * number of call arguments may be less than
	 * the number of formal parameters; handle that
	 * case too (`&& i < argc`).
	 *
	 * The values in the source registers
	 * should be retained, since pop_frame()
	 * releases all registers. Also, an
	 * argument may be assigned to from
	 * within the called function, which
	 * releases its previous value.
	 */
	for (i = 0; i < decl_argc && i < argc; i++) {
		SpnValue *dst = VALPTR(vm->sp, i);
		SpnValue *src;

		if (desc->caller_is_native) {
			SpnValue *argv = desc->env.native_env.argv;
			src = &argv[i];
		} else {
			TSlot *caller = vm->stack + desc->env.script_env.calleroff;
			spn_uword *ip = desc->env.script_env.ip;
			src = nth_call_arg(caller, ip, i);
		}

		spn_value_retain(src);
		*dst = *src;
	}

	/* then, copy over the extra (unnamed) args */
	for (i = decl_argc; i < argc; i++) {
		int dstidx = i - decl_argc;
		SpnValue *dst = nth_vararg(vm->sp, dstidx);
		SpnValue *src;

		if (desc->caller_is_native) {
			SpnValue *argv = desc->env.native_env.argv;
			src = &argv[i];
		} else {
			TSlot *caller = vm->stack + desc->env.script_env.calleroff;
			spn_uword *ip = desc->env.script_env.ip;
			src = nth_call_arg(caller, ip, i);
		}

		spn_value_retain(src);
		*dst = *src;
	}
}


/* This function uses switch dispatch instead of token-threaded dispatch
 * for the sake of conformance to standard C. (in GNU C, I could have used
 * the `goto labels_array[*ip++];` extension, though.)
 */

static int dispatch_loop(SpnVMachine *vm, spn_uword *ip, SpnValue *retvalptr)
{
	while (1) {
		spn_uword ins = *ip++;
		enum spn_vm_ins opcode = OPCODE(ins);

		switch (opcode) {
		case SPN_INS_CALL: {
			/* XXX: the return value of a call to a Sparkling
			 * function is stored in stack[header->retidx] and has
			 * a reference count of one. Here, it MUST NOT be
			 * retained, only its contents should be copied to the
			 * destination register.
			 * 
			 * offsets insetad of pointers are used here because
			 * a function call (in particular, push_frame()) may
			 * `realloc()` the stack, thus rendering saved pointers
			 * invalid.
			 */
			TSlot *retslot = SLOTPTR(vm->sp, OPA(ins));
			ptrdiff_t retidx = retslot - vm->stack;

			TSlot *funcslot = SLOTPTR(vm->sp, OPB(ins));
			SpnValue func = funcslot->v; /* copy the value struct */

			int argc = OPC(ins);

			/* this is the minimal number of `spn_uword`s needed
			 * to store the register numbers representing
			 * call-time arguments
			 */
			int narggroups = ROUNDUP(argc, SPN_WORD_OCTETS);

			/* check if value is really a function */
			if (!isfunc(&func)) {
				runtime_error(
					vm,
					ip - 1,
					"attempt to call non-function value",
					NULL
				);
				return -1;
			}

			/* loading a symbol should have already resolved it
			 * if it's a function -- assert that condition here
			 */
			assert((func.type & SPN_FLAG_PENDING) == 0);

			if (funcvalue(&func)->native) {	/* native function */
				int i, err;
				SpnValue tmpret = makenil();
				SpnValue *argv;

				#define MAX_AUTO_ARGC 16
				SpnValue auto_argv[MAX_AUTO_ARGC];

				/* allocate a big enough array for the arguments */
				if (argc > MAX_AUTO_ARGC) {
					argv = spn_malloc(argc * sizeof(argv[0]));
				} else {
					argv = auto_argv;
				}

				/* copy the arguments into the argument array */
				for (i = 0; i < argc; i++) {
					SpnValue *val = nth_call_arg(vm->sp, ip, i);
					argv[i] = *val;
				}

				/* push pseudo-frame for stack trace's sake
				 * this should be done *after* having copied
				 * the arguments, since those arguments are
				 * taken from the topmost stack frame.
				 */
				push_native_pseudoframe(vm, funcvalue(&func)->name);

				/* then call the native function. its return
				 * value must have a reference count of one.
				 */
				err = funcvalue(&func)->repr.fn(&tmpret, argc, argv, vm->ctx);

				if (argc > MAX_AUTO_ARGC) {
					free(argv);
				}

				#undef MAX_AUTO_ARGC

				/* clear and set return value register
				 * (it's released only now because it may be
				 * the same as one of the arguments:
				 * foo = bar(foo) won't do any good if foo is
				 * released before it's passed to the function
				 */
				spn_value_release(&vm->stack[retidx].v);
				vm->stack[retidx].v = tmpret;

				/* check if the native function returned
				 * an error. If so, abort execution.
				 * Also check if the callee generated a
				 * custom error message; if so, use it.
				 */
				if (err != 0) {
					const void *args[2];
					args[0] = funcvalue(&func)->name;
					args[1] = &err;
					spn_vm_seterrmsg(vm, "error in function `%s' (code: %i)", args);
					return err;
				}

				/* should not return success after an error */
				assert(vm->haserror == 0);

				/* pop pseudo-frame */
				pop_frame(vm);

				/* advance IP past the argument indices (round
				 * up to nearest number of `spn_uword`s that
				 * can accomodate `argc` octets)
				 */
				ip += narggroups;
			} else {
				/* Sparkling function
				 * The return address is the address of the
				 * instruction immediately following the
				 * current CALL instruction.
				 */
				spn_uword *retaddr = ip + narggroups;
				spn_uword *fnhdr = funcvalue(&func)->repr.bc;
				spn_uword *entry = fnhdr + SPN_FUNCHDR_LEN;
				ptrdiff_t calleroff = vm->sp - vm->stack;

				/* set up environment for push_and_copy_args */
				struct args_copy_descriptor desc;
				desc.caller_is_native = 0; /* we, the caller, are a Sparkling function */
				desc.env.script_env.ip = ip;
				desc.env.script_env.retaddr = retaddr;
				desc.env.script_env.calleroff = calleroff;
				desc.env.script_env.retidx = retidx;

				/* push the frame of the callee, and
				 * copy over its arguments
				 * (I <3 descriptive function names!)
				 */
				push_and_copy_args(vm, &func, &desc, argc);

				/* set instruction pointer to entry point
				 * in order to kick off the function call
				 */
				ip = entry;
			}

			break;
		}
		case SPN_INS_RET: {
			TFrame *callee = &vm->sp[IDX_FRMHDR].h;

			/* storing the return value is done in two steps
			 * because we need to ensure that if the return
			 * value goes into a register that also held a function
			 * argument, then first _the arguments_ need to be
			 * released - if we assigned the return value right
			 * away, then pop_frame() would release it, and that's
			 * not what we want.
			 */

			/* transfer return value to caller */
			SpnValue *res = VALPTR(vm->sp, OPA(ins));

			if (callee->retidx < 0) {
				/* return to C-land */
				if (retvalptr != NULL) {
					spn_value_retain(res);
					*retvalptr = *res;
				}
			} else {
				/* return to Sparkling-land */
				SpnValue *retptr = &vm->stack[callee->retidx].v;
				spn_value_retain(res);
				spn_value_release(retptr);
				*retptr = *res;
			}

			/* pop the callee's frame (the current one) */
			pop_frame(vm);

			/* check the return address. If it's NULL, then
			 * we need to return to C-land;
			 * else we just adjust the instruction pointer.
			 * In addition, of course, the top stack frame
			 * needs to be popped.
			 */
			if (callee->retaddr == NULL) {
				return 0;
			} else {
				ip = callee->retaddr;
			}

			break;
		}
		case SPN_INS_JMP: {
			/* ip has already passed by the opcode, it now
			 * points to the beginning of the jump offset, so
			 * store the offset and skip it
			 *
			 * storing the offset in a separate variable is done
			 * because 1. ip += *ip++ is UB,
			 * and 2. this way it's obvious what happens
			 */
			spn_sword offset = *ip++;
			ip += offset;
			break;
		}
		case SPN_INS_JZE:
		case SPN_INS_JNZ: {
			SpnValue *reg = VALPTR(vm->sp, OPA(ins));

			/* XXX: if offset is supposed to be negative, the
			 * implicit unsigned -> signed conversion is
			 * implementation-defined. Should `memcpy()` be used
			 * here instead of an assignment?
			 */
			spn_sword offset = *ip++;

			if (!isbool(reg)) {
				runtime_error(
					vm,
					ip - 2,
					"register does not contain Boolean value in conditional jump "
					"(are you trying to use non-Booleans with logical operators "
					"or in the condition of an `if`, `while` or `for` statement?)",
					NULL
				);
				return -1;
			}

			/* see the TODO concerning `&& within ||' in TODO.txt */
			if (opcode == SPN_INS_JZE && boolvalue(reg) == 0    /* JZE jumps only if zero */
			 || opcode == SPN_INS_JNZ && boolvalue(reg) != 0) { /* JNZ jumps only if nonzero */
				ip += offset;
			}

			break;

		}
		case SPN_INS_EQ:
		case SPN_INS_NE: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			SpnValue *c = VALPTR(vm->sp, OPC(ins));

			/* warning: computing the result first and storing
			 * it only later is necessary since the destination
			 * register may be the same as one of the operands
			 * (see Remark (III) in vm.h for an explanation)
			 */
			int res = opcode == SPN_INS_EQ
				? spn_value_equal(b, c)
				: spn_value_noteq(b, c);

			/* clean and update destination register */
			spn_value_release(a);
			*a = makebool(res);

			break;
		}
		case SPN_INS_LT:
		case SPN_INS_LE:
		case SPN_INS_GT:
		case SPN_INS_GE: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			SpnValue *c = VALPTR(vm->sp, OPC(ins));

			/* values must be checked for being orderable */
			if (isnumber(b) && isnumber(c)) {
				int res = numeric_compare(b, c, opcode);
				spn_value_release(a);
				*a = makebool(res);
			} else if (isobject(b) && isobject(c)) {
				SpnObject *lhs = objvalue(b);
				SpnObject *rhs = objvalue(c);
				int res;

				if (lhs->isa != rhs->isa) {
					runtime_error(
						vm,
						ip - 1,
						"ordered comparison of objects of different classes",
						NULL
					);
					return -1;
				}

				if (lhs->isa->compare == NULL) {
					runtime_error(
						vm,
						ip - 1,
						"ordered comparison of uncomparable objects",
						NULL
					);
					return -1;
				}

				/* compute answer */
				res = spn_object_cmp(lhs, rhs);

				/* clean and update destination register */
				spn_value_release(a);
				*a = makebool(cmp2bool(res, opcode));
			} else {
				runtime_error(
					vm,
					ip - 1,
					"ordered comparison of uncomparable values",
					NULL
				);
				return -1;
			}

			break;
		}
		case SPN_INS_ADD:
		case SPN_INS_SUB:
		case SPN_INS_MUL:
		case SPN_INS_DIV: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			SpnValue *c = VALPTR(vm->sp, OPC(ins));
			SpnValue res;

			if (!isnumber(b) || !isnumber(c)) {
				runtime_error(vm, ip - 1, "arithmetic on non-numbers", NULL);
				return -1;
			}

			/* compute result */
			res = arith_op(b, c, opcode);

			/* clean and update destination register */
			spn_value_release(a);
			*a = res;

			break;
		}
		case SPN_INS_MOD: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			SpnValue *c = VALPTR(vm->sp, OPC(ins));
			long res;

			if (!isint(b) || !isint(c)) {
				runtime_error(vm, ip - 1, "modulo division on non-integers", NULL);
				return -1;
			}

			res = intvalue(b) % intvalue(c);

			spn_value_release(a);
			*a = makeint(res);

			break;
		}
		case SPN_INS_NEG: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));

			if (!isnumber(b)) {
				runtime_error(vm, ip - 1, "negation of non-number", NULL);
				return -1;
			}

			if (isfloat(b)) {
				double res = -floatvalue(b);
				spn_value_release(a);
				*a = makefloat(res);
			} else {
				long res = -intvalue(b);
				spn_value_release(a);
				*a = makeint(res);
			}

			break;
		}
		case SPN_INS_INC:
		case SPN_INS_DEC: {
			SpnValue *val = VALPTR(vm->sp, OPA(ins));

			if (!isnumber(val)) {
				runtime_error(vm, ip - 1, "incrementing or decrementing non-number", NULL);
				return -1;
			}

			if (isfloat(val)) {
				if (opcode == SPN_INS_INC) {
					val->v.f++;
				} else {
					val->v.f--;
				}
			} else {
				if (opcode == SPN_INS_INC) {
					val->v.i++;
				} else {
					val->v.i--;
				}
			}

			break;
		}
		case SPN_INS_AND:
		case SPN_INS_OR:
		case SPN_INS_XOR:
		case SPN_INS_SHL:
		case SPN_INS_SHR: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			SpnValue *c = VALPTR(vm->sp, OPC(ins));
			long res;

			if (!isint(b) || !isint(c)) {
				runtime_error(vm, ip - 1, "bitwise operation on non-integers", NULL);
				return -1;
			}

			res = bitwise_op(b, c, opcode);
			spn_value_release(a);
			*a = makeint(res);

			break;
		}
		case SPN_INS_BITNOT: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			long res;

			if (!isint(b)) {
				runtime_error(vm, ip - 1, "bitwise NOT on non-integer", NULL);
				return -1;
			}

			res = ~intvalue(b);
			spn_value_release(a);
			*a = makeint(res);

			break;
		}
		case SPN_INS_LOGNOT: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			int res;

			if (!isbool(b)) {
				runtime_error(vm, ip - 1, "logical negation of non-Boolean value", NULL);
				return -1;
			}

			res = !boolvalue(b);
			spn_value_release(a);
			*a = makebool(res);

			break;
		}
		case SPN_INS_SIZEOF:
		case SPN_INS_TYPEOF: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));

			SpnValue res;
			if (opcode == SPN_INS_SIZEOF) {
				res = sizeof_value(b);
			} else {
				res = typeof_value(b);
			}

			spn_value_release(a);
			*a = res;

			break;
		}
		case SPN_INS_CONCAT: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			SpnValue *c = VALPTR(vm->sp, OPC(ins));
			SpnString *res;

			if (!isstring(b) || !isstring(c)) {
				runtime_error(vm, ip - 1, "concatenation of non-string values", NULL);
				return -1;
			}

			res = spn_string_concat(stringvalue(b), stringvalue(c));

			spn_value_release(a);
			a->type = SPN_TYPE_STRING;
			a->v.o = res;

			break;
		}
		case SPN_INS_LDCONST: {
			/* the first argument is the destination register */
			SpnValue *dst = VALPTR(vm->sp, OPA(ins));

			/* the second argument of the instruction is the
			 * kind of the constant to be loaded
			 */
			enum spn_const_kind type = OPB(ins);

			/* clear previous value */
			spn_value_release(dst);

			switch (type) {
			case SPN_CONST_NIL:
				*dst = makenil();
				break;
			case SPN_CONST_TRUE:
				*dst = makebool(1);
				break;
			case SPN_CONST_FALSE:
				*dst = makebool(0);
				break;
			case SPN_CONST_INT: {
				long num;
				memcpy(&num, ip, sizeof num);
				ip += ROUNDUP(sizeof num, sizeof(spn_uword));
				*dst = makeint(num);
				break;
			}
			case SPN_CONST_FLOAT: {
				double num;
				memcpy(&num, ip, sizeof num);
				ip += ROUNDUP(sizeof num, sizeof(spn_uword));
				*dst = makefloat(num);
				break;
			}
			default:
				SHANT_BE_REACHED();
			}

			break;
		}
		case SPN_INS_LDSYM: {
			/* operand A is the destination; operand B (16 bits)
			 * is the index of the symbol in the local symbol table
			 */
			SpnValue *dst = VALPTR(vm->sp, OPA(ins));
			unsigned symidx = OPMID(ins); /* just if it's 16 bits */
			SpnValue *symp;

			TFrame *frmhdr = &vm->sp[IDX_FRMHDR].h;
			TSymtab *symtab = NULL;

			/* search for symbol table of current function */
			size_t i;
			for (i = 0; i < vm->lscount; i++) {
				if (vm->lsymtabs[i].bc == frmhdr->env) {
					symtab = &vm->lsymtabs[i];
					break;
				}
			}

			/* in case someone calls a function unknown to the VM */
			assert(symtab != NULL);

			symp = &symtab->vals[symidx];


			/* if the symbol is an unresolved reference
			 * to a global, then attempt to resolve it
			 * (it need not be of type function.)
			 */
			if (symp->type & SPN_FLAG_PENDING) {
				if (resolve_symbol(vm, ip - 1, symp) != 0) {
					return -1;
				}
			}

			/* set the new - now surely resolved - value */
			spn_value_retain(symp);
			spn_value_release(dst);
			*dst = *symp;

			break;
		}
		case SPN_INS_MOV: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));

			/* ...and again. Here, however, we don't need to
			 * copy the result (i. e. operand B). If we move a
			 * value in a register onto itself, that's fine
			 * (because no computations depend on their type
			 * or value), but MOV won't be emitted anyway to
			 * move a value onto itself, because that's silly.
			 */
			spn_value_retain(b);
			spn_value_release(a);
			*a = *b;

			break;
		}
		case SPN_INS_LDARGC: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			spn_value_release(a);
			*a = makeint(vm->sp[IDX_FRMHDR].h.real_argc);
			break;
		}
		case SPN_INS_NEWARR: {
			SpnValue *dst = VALPTR(vm->sp, OPA(ins));
			spn_value_release(dst);
			*dst = makearray();
			break;
		}
		case SPN_INS_ARRGET: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			SpnValue *c = VALPTR(vm->sp, OPC(ins));

			if (isarray(b)) {
				SpnValue val;
				spn_array_get(arrayvalue(b), c, &val);
				spn_value_retain(&val);
				spn_value_release(a);
				*a = val;
			} else if (isstring(b)) {
				SpnString *str = stringvalue(b);
				long len = str->len;
				long idx;
				unsigned char ch;

				if (!isint(c)) {
					runtime_error(vm, ip - 1, "indexing string with non-integer value", NULL);
					return -1;
				}

				idx = intvalue(c);

				/* negative indices count from the end of the string */
				if (idx < 0) {
					idx = len + idx;
				}

				if (idx < 0 || idx >= len) {
					const void *args[2];
					args[0] = &idx;
					args[1] = &len;
					runtime_error(
						vm,
						ip - 1,
						"character at normalized index %d is "
						"out of bounds for string of length %d",
						args
					);
					return -1;
				}

				/* copy character before string is potentially deallocated */
				ch = str->cstr[idx];

				spn_value_release(a);
				*a = makeint(ch);
			} else {
				runtime_error(vm, ip - 1, "first operand of [] operator must be an array or a string", NULL);
				return -1;
			}

			break;
		}
		case SPN_INS_ARRSET: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			SpnValue *c = VALPTR(vm->sp, OPC(ins));

			if (!isarray(a)) {
				runtime_error(vm, ip - 1, "assignment to member of non-array value", NULL);
				return -1;
			}

			spn_array_set(arrayvalue(a), b, c);
			break;
		}
		case SPN_INS_NTHARG: {
			/* this accesses unnamed arguments only, so regardless
			 * of the number of formal parameters, #0 always yields
			 * the first **unnamed** argument, which is *not*
			 * necessarily the first argument!
			 */
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			TFrame *hdr = &vm->sp[IDX_FRMHDR].h;
			long argidx;


			if (!isint(b)) {
				runtime_error(vm, ip - 1, "non-integer argument to `#' operator", NULL);
				return -1;
			}

			argidx = intvalue(b);

			if (argidx < 0) {
				runtime_error(vm, ip - 1, "negative argument to `#' operator", NULL);
				return -1;
			}

			if (argidx < hdr->extra_argc) {
				/* if there are enough args, get one */
				SpnValue *argp = nth_vararg(vm->sp, argidx);
				spn_value_retain(argp);

				spn_value_release(a);
				*a = *argp;
			} else {
				/* if index is out of bounds, throw */
				const void *args[1];
				args[0] = &argidx;
				runtime_error(vm, ip - 1, "argument `%d' of `#' operator is out-of bounds", args);
				return -1;
			}

			break;
		}
		case SPN_INS_FUNCDEF: {
			SpnValue funcval, auxval;

			size_t bodylen;

			/* pointer to the symbol header, see the SPN_FUNCHDR_*
			 * macros in vm.h
			 */
			spn_uword *hdr;

			const char *symname = (const char *)(ip);
			size_t namelen = OPLONG(ins); /* expected length */

			size_t nwords = ROUNDUP(namelen + 1, sizeof(spn_uword));

#ifndef NDEBUG
			/* this is the sanity check described in Remark (VI) */
			size_t reallen = strlen(symname);
			assert(namelen == reallen);
#endif

			/* skip symbol name */
			ip += nwords;

			/* save header position, fill in properties */
			hdr = ip;
			bodylen = hdr[SPN_FUNCHDR_IDX_BODYLEN];

			/* sanity check: argc <= nregs is a must (else how
			 * could arguments fit in the first `argc` registers?)
			 */
			assert(hdr[SPN_FUNCHDR_IDX_ARGC] <= hdr[SPN_FUNCHDR_IDX_NREGS]);

			/* skip the function header and function body */
			ip += SPN_FUNCHDR_LEN + bodylen;

			/* if the function is a lambda, do not add it
			 * (XXX: this should be right here after modifying the
			 * instruction pointer, because we *still* DO want to
			 * skip the code of the function body.)
			 */
			if (strcmp(symname, SPN_LAMBDA_NAME) == 0) {
				break;
			}

			/* create function value, insert it in global symtab */
			funcval = makescriptfunc(symname, hdr, vm->sp[IDX_FRMHDR].h.env);
			/* (the environment of a global function is always the
			 * environment of the compilation unit itself)
			 */

			/* check for a global symbol with the same name -- if
			 * one exists, it's an error, there should be no
			 * global symbols with identical names.
			 */
			spn_array_get_strkey(vm->glbsymtab, symname, &auxval);
			if (!isnil(&auxval)) {
				const void *args[1];
				args[0] = symname;
				runtime_error(
					vm,
					ip - bodylen - SPN_FUNCHDR_LEN - nwords - 1,
					"re-definition of global `%s'",
					args
				);

				return -1;
			}

			/* if everything was OK, add the function to the global
			 * symbol table
			 */
			spn_array_set_strkey(vm->glbsymtab, symname, &funcval);
			spn_value_release(&funcval);

			break;
		}
		case SPN_INS_GLBVAL: {
			/* instruction format is "mid": 8 bit operand A for the
			 * register number from which to read the expression,
			 * 16-bit operand B to store the length of the name
			 * stored in the global symbol table. The symbol name
			 * follows the instruction word in the bytecode.
			 */
			SpnValue *src = VALPTR(vm->sp, OPA(ins));
			size_t namelen = OPMID(ins);
			size_t nwords = ROUNDUP(namelen + 1, sizeof(spn_uword));
			const char *symname = (const char *)(ip);
			SpnValue auxval;

#ifndef NDEBUG
			size_t reallen = strlen(symname);
			assert(namelen == reallen);
#endif

			/* skip symbol name */
			ip += nwords;

			/* attempt adding value to global symtab */
			spn_array_get_strkey(vm->glbsymtab, symname, &auxval);
			if (!isnil(&auxval)) {
				const void *args[1];
				args[0] = symname;
				runtime_error(
					vm,
					ip - nwords - 1,
					"re-definition of global `%s'",
					args
				);

				return -1;
			}

			spn_array_set_strkey(vm->glbsymtab, symname, src);
			break;
		}
		default: /* I am sorry for the indentation here. */
			{
				unsigned long lopcode = opcode;
				const void *args[1];
				args[0] = &lopcode;
				runtime_error(vm, ip - 1, "illegal instruction 0x%02x", args);
				return -1;
			}
		}
	}
}

static int validate_magic(SpnVMachine *vm, spn_uword *bc)
{
	if (bc[SPN_HDRIDX_MAGIC] != SPN_MAGIC) {
		spn_vm_seterrmsg(vm, "invalid magic number", NULL);
		return -1;
	}

	return 0;
}

static void read_local_symtab(SpnVMachine *vm, spn_uword *bc)
{
	TSymtab *cursymtab;

	/* read the offset and size from bytecode */
	ptrdiff_t offset = bc[SPN_HDRIDX_SYMTABOFF];
	size_t symcount = bc[SPN_HDRIDX_SYMTABLEN];

	spn_uword *stp = bc + offset;
	size_t lsidx;

	/* we only read the symbol table if it hasn't been read yet before */
	size_t i;
	for (i = 0; i < vm->lscount; i++) {
		if (vm->lsymtabs[i].bc == bc) {
			/* already known bytecode chunk, don't read symtab */
			return;
		}
	}

	/* if we got here, the bytecode file is being run for the first time
	 * on this VM instance, so we proceed to reading its symbol table
	 */
	lsidx = vm->lscount++;

	vm->lsymtabs = spn_realloc(vm->lsymtabs, vm->lscount * sizeof(vm->lsymtabs[0]));

	/* initialize new local symbol table */
	cursymtab = &vm->lsymtabs[lsidx];
	cursymtab->bc = bc;
	cursymtab->size = symcount;
	cursymtab->vals = spn_malloc(symcount * sizeof(cursymtab->vals[0]));

	/* then actually read the symbols */
	for (i = 0; i < symcount; i++) {
		spn_uword ins = *stp++;
		enum spn_local_symbol sym = OPCODE(ins);

		switch (sym) {
		case SPN_LOCSYM_STRCONST: {
			const char *cstr = (const char *)(stp);

			size_t len = OPLONG(ins);
			size_t nwords = ROUNDUP(len + 1, sizeof(spn_uword));

#ifndef NDEBUG
			/* sanity check: the string length recorded in the
			 * bytecode and the actual length
			 * reported by `strlen()` shall match.
			 */

			size_t reallen = strlen(cstr);
			assert(len == reallen);
#endif

			cursymtab->vals[i] = makestring_nocopy_len(cstr, len, 0);

			stp += nwords;
			break;
		}
		case SPN_LOCSYM_SYMSTUB: {
			const char *symname = (const char *)(stp);
			size_t len = OPLONG(ins);
			size_t nwords = ROUNDUP(len + 1, sizeof(spn_uword));

#ifndef NDEBUG
			/* sanity check: the symbol name length recorded in
			 * the bytecode and the actual length
			 * reported by `strlen()` shall match.
			 */

			size_t reallen = strlen(symname);
			assert(len == reallen);
#endif

			/* we don't know the type of the symbol yet */
			cursymtab->vals[i] = makescriptfunc(symname, NULL, NULL);
			cursymtab->vals[i].type |= SPN_FLAG_PENDING; /* this is a hack */

			/* note that `cursymtab->vals[i].v.fnv.env`
			 * _must not_ be set here: a symbol stub is merely
			 * an unresolved reference to a global symbol,
			 * we don't know yet in which TU the actual definition
			 * will be (in fact, we don't even know if it's really
			 * going to be a function).
			 */

			stp += nwords;
			break;	
		}
		case SPN_LOCSYM_LAMBDA: {
			size_t hdroff = OPLONG(ins);
			spn_uword *entry = bc + hdroff;

			cursymtab->vals[i] = makescriptfunc(NULL, entry, bc);

			/* unlike global functions, lambda functions can only
			 * be implemented in the same translation unit int
			 * which their local symtab entry is defined.
			 * So we *can* (and should) fill in the `env'
			 * member of the SpnValue while reading the symtab.
			 */

			break;
		}
		default:
			SHANT_BE_REACHED();
			return;
		}
	}
}

/* ordered comparisons */
static int cmp2bool(int res, int op)
{
	switch (op) {
	case SPN_INS_LT: return res <  0;
	case SPN_INS_LE: return res <= 0;
	case SPN_INS_GT: return res >  0;
	case SPN_INS_GE: return res >= 0;
	default:	 SHANT_BE_REACHED();
	}

	return -1;
}

static int numeric_compare(const SpnValue *lhs, const SpnValue *rhs, int op)
{
	int res;

	assert(isnumber(lhs) && isnumber(rhs));

	if (isfloat(lhs)) {
		if (isfloat(rhs)) {
			res =	floatvalue(lhs) < floatvalue(rhs) ? -1
			      : floatvalue(lhs) > floatvalue(rhs) ? +1
			      :					     0;
		} else {
			res =	floatvalue(lhs) < intvalue(rhs) ? -1
			      : floatvalue(lhs) > intvalue(rhs) ? +1
			      :					   0;
		}
	} else {
		if (isfloat(rhs)) {
			res =	intvalue(lhs) < floatvalue(rhs) ? -1
			      : intvalue(lhs) > floatvalue(rhs) ? +1
			      :					   0;
		} else {
			res =	intvalue(lhs) < intvalue(rhs) ? -1
			      : intvalue(lhs) > intvalue(rhs) ? +1
			      :					 0;
		}
	}

	return cmp2bool(res, op);
}

static SpnValue arith_op(const SpnValue *lhs, const SpnValue *rhs, int op)
{
	assert(isnumber(lhs) && isnumber(rhs));

	if (isfloat(lhs) || isfloat(rhs)) {
		double a = isfloat(lhs) ? floatvalue(lhs) : intvalue(lhs);
		double b = isfloat(rhs) ? floatvalue(rhs) : intvalue(rhs);
		double y;

		switch (op) {
		case SPN_INS_ADD: y = a + b; break;
		case SPN_INS_SUB: y = a - b; break;
		case SPN_INS_MUL: y = a * b; break;
		case SPN_INS_DIV: y = a / b; break;
		default: y = 0; SHANT_BE_REACHED();
		}

		return makefloat(y);
	} else {
		long a = intvalue(lhs);
		long b = intvalue(rhs);
		long y;

		switch (op) {
		case SPN_INS_ADD: y = a + b; break;
		case SPN_INS_SUB: y = a - b; break;
		case SPN_INS_MUL: y = a * b; break;
		case SPN_INS_DIV: y = a / b; break;
		default: y = 0; SHANT_BE_REACHED();
		}

		return makeint(y);
	}
}

static long bitwise_op(const SpnValue *lhs, const SpnValue *rhs, int op)
{
	long a, b;

	assert(isint(lhs) && isint(rhs));

	a = intvalue(lhs);
	b = intvalue(rhs);

	switch (op) {
	case SPN_INS_AND: return a & b;
	case SPN_INS_OR:  return a | b;
	case SPN_INS_XOR: return a ^ b;
	case SPN_INS_SHL: return a << b;
	case SPN_INS_SHR: return a >> b;
	default:	  SHANT_BE_REACHED();
	}

	return -1;
}

static int resolve_symbol(SpnVMachine *vm, spn_uword *ip, SpnValue *symp)
{
	SpnValue res;
	const char *symname = funcvalue(symp)->name;
	spn_array_get_strkey(vm->glbsymtab, symname, &res);

	if (isnil(&res)) {
		const void *args[1];
		args[0] = symname;
		runtime_error(
			vm,
			ip,
			"global `%s' does not exist or it is nil",
			args
		);
		return -1;
	}

	/* if the resolution succeeded, then we
	 * cache the symbol into the appropriate
	 * local symbol table so that we don't
	 * need to resolve it anymore.
	 */
	spn_value_release(symp);
	*symp = res;

	/* local symbol tables are supposed to hold
	 * owning (strong) pointers to their values
	 */
	spn_value_retain(symp);

	return 0;
}

static SpnValue sizeof_value(SpnValue *val)
{
	switch (valtype(val)) {
	case SPN_TTAG_NIL:    return makeint(0);
	case SPN_TTAG_STRING: return makeint(stringvalue(val)->len);
	case SPN_TTAG_ARRAY:  return makeint(spn_array_count(arrayvalue(val)));
	default:	      return makeint(1);
	}
}

static SpnValue typeof_value(SpnValue *val)
{
	const char *type = spn_type_name(val->type);
	return makestring_nocopy(type);
}

