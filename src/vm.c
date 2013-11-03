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
#include "array.h"
#include "private.h"

/* stack management macros 
 * 0th slot: header, 1st slot: implicit self
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
	spn_uword	*retaddr;	/* return address (points to bytecode)	*/
	ptrdiff_t	 retidx;	/* pointer into the caller's frame	*/
	int		 symtabidx;	/* index of the local symtab in use	*/
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
	void		*ctx;		/* user data			*/
};

static int dispatch_loop(SpnVMachine *vm, spn_uword *ip, SpnValue *ret);

/* this only releases the values stored in the stack frames */
static void free_frames(SpnVMachine *vm);

/* stack manipulation */
static size_t stacksize(SpnVMachine *vm);
static void push_first_frame(SpnVMachine *vm, int symtabidx);
static void expand_stack(SpnVMachine *vm, size_t nregs);
static void push_frame(
	SpnVMachine *vm,
	int nregs,
	int decl_argc,
	int extra_argc,
	spn_uword *retaddr,
	ptrdiff_t retidx,
	int symtabidx,
	const char *fnname
);
static void pop_frame(SpnVMachine *vm);

/* bytecode validation - returns 0 on success, nonzero on error */
static int validate_magic(SpnVMachine *vm, spn_uword *bc);

/* returns the index of the symtab of `bc`, creates local symtab if
 * necessary. Returns -1 on error.
 */
static int read_local_symtab(SpnVMachine *vm, spn_uword *bc);

/* accessing function arguments */
static SpnValue *nth_call_arg(TSlot *sp, spn_uword *ip, int idx);
static SpnValue *nth_vararg(TSlot *sp, int idx);

/* emulating the ALU... */
static int numeric_compare(const SpnValue *lhs, const SpnValue *rhs, int op);
static int cmp2bool(int res, int op);
static SpnValue arith_op(const SpnValue *lhs, const SpnValue *rhs, int op);
static long bitwise_op(const SpnValue *lhs, const SpnValue *rhs, int op);

/* ...and a weak dynamic linker */
static SpnValue *resolve_symbol(SpnVMachine *vm, const char *name);

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

SpnVMachine *spn_vm_new()
{
	SpnVMachine *vm = malloc(sizeof(*vm));
	if (vm == NULL) {
		abort();
	}

	/* initialize stack */
	vm->stackallsz = 0;
	vm->stack = NULL;
	vm->sp = NULL;

	/* initialize the global and local symbol tables */
	vm->glbsymtab = spn_array_new();
	vm->lsymtabs = NULL;
	vm->lscount = 0;

	/* set up error reporting and user data */
	vm->errmsg = NULL;
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

	/* handle empty stack */
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
	buf = malloc(i * sizeof(*buf));
	if (buf == NULL) {
		abort();
	}

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

static void free_frames(SpnVMachine *vm)
{
	if (vm->stack != NULL) {
		while (vm->sp > vm->stack) {
			pop_frame(vm);
		}
	}
}

static spn_uword *current_bytecode(SpnVMachine *vm)
{
	TFrame *stkhdr = &vm->sp[IDX_FRMHDR].h;
	int symtabidx = stkhdr->symtabidx;
	assert(symtabidx >= 0 && symtabidx < vm->lscount);
	return vm->lsymtabs[symtabidx].bc;
}

int spn_vm_exec(SpnVMachine *vm, spn_uword *bc, SpnValue *retval)
{
	int symtabidx;
	spn_uword *ip;

	/* releases values in stack frames from the previous execution.
	 * This is not done immediately after the dispatch loop because if
	 * a runtime error occurred, we want the backtrace functions to
	 * be able to unwind the stack.
	 */
	free_frames(vm);

	/* check bytecode for magic bytes */
	if (validate_magic(vm, bc) != 0) {
		return -1;
	}

	/* read the local symbol table, if necessary */
	symtabidx = read_local_symtab(vm, bc);
	if (symtabidx < 0) {
		return -1;
	}

	/* initialize global stack (read 1st frame size from bytecode) */
	push_first_frame(vm, symtabidx);

	/* initialize instruction pointer to beginning of TU */
	ip = current_bytecode(vm) + SPN_PRGHDR_LEN;

	/* actually run the program */
	return dispatch_loop(vm, ip, retval);
}

int spn_vm_callfunc(
	SpnVMachine *vm,
	SpnValue *fn,
	SpnValue *retval,
	int argc,
	SpnValue *argv
)
{
	int i;
	int symtabidx;
	int decl_argc;
	int extra_argc;
	int nregs;
	spn_uword *entry;
	spn_uword *fnhdr;
	const char *fnname;

	/* check that the callee is indeed a function */
	if (fn->t != SPN_TYPE_FUNC) {
		runtime_error(vm, NULL, "attempt to call non-function value", NULL);
		return -1;
	}

	/* native functions are easy to deal with */
	if (fn->f & SPN_TFLG_NATIVE) {
		return fn->v.fnv.r.fn(retval, argc, argv, vm->ctx);
	}

	/* if we got here, the callee is a valid Sparkling function.
	 * The following code does roughly the same as the implementation
	 * of the SPN_INS_CALL virtual machine instruction.
	 */
	fnhdr = fn->v.fnv.r.bc;
	symtabidx = fn->v.fnv.symtabidx;
	decl_argc = fnhdr[SPN_FUNCHDR_IDX_ARGC];
	nregs = fnhdr[SPN_FUNCHDR_IDX_NREGS];
	entry = fnhdr + SPN_FUNCHDR_LEN;
	fnname = fn->v.fnv.name;

	/* if there are less call arguments than formal
	 * parameters, we set extra_argc to 0 (and all
	 * the unspecified arguments are implicitly set
	 * to `nil` by push_frame)
	 */
	extra_argc = argc > decl_argc ? argc - decl_argc : 0;

	/* store the offset of the stack frame of the
	 * caller so we can read the call arguments
	 * later, when the active frame is that of the
	 * called function. An offset is stored instead
	 * of the stack pointer itself because the
	 * push_frame() function reallocates the stack.
	 */

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
		NULL,
		-1,
		symtabidx,
		fnname
	);

	/* copy named (declared) arguments */
	for (i = 0; i < decl_argc && i < argc; i++) {
		SpnValue *dst = VALPTR(vm->sp, i);
		spn_value_retain(&argv[i]);
		*dst = argv[i];
	}

	/* copy over the extra (unnamed) args */
	for (i = decl_argc; i < argc; i++) {
		int dstidx = i - decl_argc;
		SpnValue *dst = nth_vararg(vm->sp, dstidx);
		spn_value_retain(&argv[i]);
		*dst = argv[i];
	}

	/* recurse, because it's convenient */
	return dispatch_loop(vm, entry, retval);
}

void spn_vm_addlib(SpnVMachine *vm, const SpnExtFunc fns[], size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		SpnValue key, val;

		key.t = SPN_TYPE_STRING;
		key.f = SPN_TFLG_OBJECT;
		key.v.ptrv = spn_string_new_nocopy(fns[i].name, 0);

		val.t = SPN_TYPE_FUNC;
		val.f = SPN_TFLG_NATIVE;
		val.v.fnv.name = fns[i].name;
		val.v.fnv.r.fn = fns[i].fn;

		spn_array_set(vm->glbsymtab, &key, &val);
		spn_object_release(key.v.ptrv);
	}
}

static void runtime_error(SpnVMachine *vm, spn_uword *ip, const char *fmt, const void *args[])
{
	char *prefix, *msg;
	size_t prefix_len, msg_len;
	unsigned long addr = ip ? ip - current_bytecode(vm) : 0;
	const void *prefix_args[1];
	prefix_args[0] = &addr;

	prefix = spn_string_format_cstr(
		"Sparkling: at address 0x%08x: runtime error: ",
		&prefix_len,
		prefix_args
	);

	msg = spn_string_format_cstr(fmt, &msg_len, args);

	free(vm->errmsg);
	vm->errmsg = malloc(prefix_len + msg_len + 1);

	if (vm->errmsg == NULL) {
		abort();
	}

	strcpy(vm->errmsg, prefix);
	strcpy(vm->errmsg + prefix_len, msg);

	free(prefix);
	free(msg);
}

const char *spn_vm_errmsg(SpnVMachine *vm)
{
	return vm->errmsg;
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

	vm->stack = realloc(vm->stack, vm->stackallsz * sizeof(vm->stack[0]));
	if (vm->stack == NULL) {
		abort();
	}

	/* re-initialize stack pointer because we realloc()'d the stack */
	vm->sp = vm->stack + oldsize;
}

/* nregs is the logical size (without the activation record header and the
 * implicit self) of the new stack frame, in slots
 */
static void push_frame(
	SpnVMachine *vm,
	int nregs,
	int decl_argc,
	int extra_argc,
	spn_uword *retaddr,
	ptrdiff_t retidx,
	int symtabidx,
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
		vm->sp[i].v.t = SPN_TYPE_NIL;
		vm->sp[i].v.f = 0;
	}

	/* initialize activation record header */
	vm->sp[IDX_FRMHDR].h.size = real_nregs;
	vm->sp[IDX_FRMHDR].h.decl_argc = decl_argc;
	vm->sp[IDX_FRMHDR].h.extra_argc = extra_argc;
	vm->sp[IDX_FRMHDR].h.retaddr = retaddr; /* if NULL, return to C-land */
	vm->sp[IDX_FRMHDR].h.retidx = retidx; /* if negative, return to C-land */
	vm->sp[IDX_FRMHDR].h.symtabidx = symtabidx;
	vm->sp[IDX_FRMHDR].h.fnname = fnname;
}

static void push_first_frame(SpnVMachine *vm, int symtabidx)
{
	/* the next word in the bytecode is the number of global registers */
	TSymtab *symtab = &vm->lsymtabs[symtabidx];
	size_t nregs = symtab->bc[SPN_HDRIDX_FRMSIZE];

	/* push a large enough frame - return address: NULL (nowhere to
	 * return from top-level program scope). A return value index < 0
	 * (-1 in this case) indicates that the program or function returns to
	 * C-land, and instead of indexing the stack, the return value should
	 * be copied directly into *vm->retptr.
	 */
	push_frame(vm, nregs, 0, 0, NULL, -1, symtabidx, "<main program>");
}

static void pop_frame(SpnVMachine *vm)
{
	/* we need to release all values when popping a frame, since
	 * computations/instructions are designed so that each step
	 * (e. g. concatenation, function calls, etc.) cleans up its
	 * destination register, but not the source(s) (if any).
	 * the implicit self argument needs to be released as well.
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
			 * 
			 * TODO: the implementation of this instruction needs
			 * a fair amound of refactoring.
			 */
			TSlot *retslot = SLOTPTR(vm->sp, OPA(ins));
			SpnValue *func = VALPTR(vm->sp, OPB(ins));
			int argc = OPC(ins);

			SpnValue *retval = &retslot->v;
			ptrdiff_t retidx = retslot - vm->stack;

			/* this is the minimal number of `spn_uword`s needed
			 * to store the register numbers representing
			 * call-time arguments
			 */
			int narggroups = ROUNDUP(argc, SPN_WORD_OCTETS);

			if (func->t != SPN_TYPE_FUNC) {
				runtime_error(vm, ip - 1, "calling non-function value", NULL);
				return -1;
			}

			/* loading a symbol should have already resolved it
			 * if it's a function -- assert that condition here
			 */
			assert((func->f & SPN_TFLG_PENDING) == 0);

			if (func->f & SPN_TFLG_NATIVE) {
				/* call native function */
				int i, err;
				SpnValue tmpret;
				SpnValue *argv;

				/* allocate a big enough array for the arguments */
				argv = malloc(argc * sizeof(argv[0]));
				if (argv == NULL) {
					abort();
				}

				/* copy the arguments into the argument array */
				for (i = 0; i < argc; i++) {
					SpnValue *val = nth_call_arg(vm->sp, ip, i);
					argv[i] = *val;
				}

				/* return nil unless otherwise specified */
				tmpret.t = SPN_TYPE_NIL;
				tmpret.f = 0;

				/* then call the native function. its return
				 * value must have a reference count of one.
				 */
				err = func->v.fnv.r.fn(&tmpret, argc, argv, vm->ctx);
				free(argv);

				/* clear and set return value register
				 * (it's released only now because it may be
				 * the same as one of the arguments:
				 * foo = bar(foo) won't do any good if foo is
				 * released before it's passed to the function
				 */
				spn_value_release(retval);
				*retval = tmpret;

				/* check if the native function returned
				 * an error. If so, abort execution.
				 */
				if (err != 0) {
					const void *args[2];
					args[0] = func->v.fnv.name;
					args[1] = &err;
					runtime_error(
						vm,
						ip - 1,
						"error in function %s() (code %i)",
						args
					);
					return err;
				}

				/* advance IP past the argument indices (round
				 * up to nearest number of `spn_uword`s that
				 * can accomodate `argc` octets)
				 */
				ip += narggroups;
			} else {
				/* call Sparkling function */
				int i;

				/* see Remark (VI) in vm.h in order to get an
				 * understanding of the layout of the
				 * bytecode representing a function
				 */
				spn_uword *fnhdr = func->v.fnv.r.bc;
				int symtabidx = func->v.fnv.symtabidx;
				int decl_argc = fnhdr[SPN_FUNCHDR_IDX_ARGC];
				int nregs = fnhdr[SPN_FUNCHDR_IDX_NREGS];
				spn_uword *entry = fnhdr + SPN_FUNCHDR_LEN;
				const char *fnname = func->v.fnv.name;

				/* if there are less call arguments than formal
				 * parameters, we set extra_argc to 0 (and all
				 * the unspecified arguments are implicitly set
				 * to `nil` by push_frame)
				 */
				int extra_argc = argc > decl_argc ? argc - decl_argc : 0;

				spn_uword *retaddr = ip + narggroups;

				/* store the offset of the stack frame of the
				 * caller so we can read the call arguments
				 * later, when the active frame is that of the
				 * called function. An offset is stored instead
				 * of the stack pointer itself because
				 * `push_frame()' may reallocate the stack.
				 */
				ptrdiff_t calleroff = vm->sp - vm->stack;
				TSlot *caller;

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
					retaddr,
					retidx,
					symtabidx,
					fnname
				);

				/* and grab the (now potentially changed)
				 * frame pointer of the caller function
				 */
				caller = vm->stack + calleroff;

				/* first, fill in arguments that fit into the
				 * first `decl_argc` registers (i. e. those
				 * that are declared as formal parameters). The
				 * number of call arguments may be less than
				 * the number of formal parameters; handle that
				 * case too (`&& i < argc`)
				 */
				for (i = 0; i < decl_argc && i < argc; i++) {
					/* copy over register value.
					 * Now the value in the source register
					 * should be retained, since pop_frame()
					 * releases all registers. Also, an
					 * argument may be assigned to from
					 * within the called function, which
					 * releases its previous value.
					 */
					SpnValue *src = nth_call_arg(caller, ip, i);

					/* the first `decl_argc` registers hold
					 * the named arguments
					 */
					SpnValue *dst = VALPTR(vm->sp, i);
					spn_value_retain(src);
					*dst = *src;
				}

				/* next, copy over the extra (unnamed) args */
				for (i = decl_argc; i < argc; i++) {
					SpnValue *src = nth_call_arg(caller, ip, i);
					int dstidx = i - decl_argc;
					SpnValue *dst = nth_vararg(vm->sp, dstidx);

					spn_value_retain(src);
					*dst = *src;
				}

				/* set instruction pointer to entry point
				 * to kick off the function call
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

			/* this relies on the fact that pop_frame()
			 * does NOT reallocate the stack
			 */
			SpnValue *res = VALPTR(vm->sp, OPA(ins));

			/* get and retain the return value */
			spn_value_retain(res);

			/* pop the callee's frame (the current one) */
			pop_frame(vm);

			/* transfer return value to caller */
			if (callee->retidx < 0) {	/* return to C-land */
				if (retvalptr != NULL) {
					*retvalptr = *res;
				}
			} else {			/* return to Sparkling-land */
				SpnValue *retptr = &vm->stack[callee->retidx].v;
				spn_value_release(retptr);
				*retptr = *res;
			}

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

			if (reg->t != SPN_TYPE_BOOL) {
				runtime_error(
					vm,
					ip - 2,
					"register does not contain Boolean value in conditional jump\n"
					"(are you trying to use non-Booleans with logical operators\n"
					"or in the condition of an `if`, `while` or `for` statement?)",
					NULL
				);
				return -1;
			}

			/* see the TODO concerning `&& within ||' in TODO.txt */
			if (opcode == SPN_INS_JZE && reg->v.boolv == 0    /* JZE jumps only if zero */
			 || opcode == SPN_INS_JNZ && reg->v.boolv != 0) { /* JNZ jumps only if nonzero */
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
			a->t = SPN_TYPE_BOOL;
			a->f = 0;
			a->v.boolv = res;

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
			if (b->t == SPN_TYPE_NUMBER
			 && c->t == SPN_TYPE_NUMBER) {

				int res = numeric_compare(b, c, opcode);

				spn_value_release(a);
				a->t = SPN_TYPE_BOOL;
				a->f = 0;
				a->v.boolv = res;
				
			} else if (b->f & SPN_TFLG_OBJECT
				&& c->f & SPN_TFLG_OBJECT) {

				SpnObject *lhs = b->v.ptrv;
				SpnObject *rhs = c->v.ptrv;
				int res;

				if (lhs->isa != rhs->isa) {
					runtime_error(
						vm,
						ip - 1,
						"ordered comparison of objects of different types",
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
				a->t = SPN_TYPE_BOOL;
				a->f = 0;
				a->v.boolv = cmp2bool(res, opcode);
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

			if (b->t != SPN_TYPE_NUMBER
			 || c->t != SPN_TYPE_NUMBER) {
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

			if (b->t != SPN_TYPE_NUMBER
			 || c->t != SPN_TYPE_NUMBER) {
				runtime_error(vm, ip - 1, "modulo division on non-numbers", NULL);
				return -1;
			}

			if (b->f & SPN_TFLG_FLOAT || c->f & SPN_TFLG_FLOAT) {
				runtime_error(vm, ip - 1, "modulo division on non-integers", NULL);
				return -1;
			}

			res = b->v.intv % c->v.intv;

			spn_value_release(a);
			a->t = SPN_TYPE_NUMBER;
			a->f = 0;
			a->v.intv = res;

			break;
		}
		case SPN_INS_NEG: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));

			if (b->t != SPN_TYPE_NUMBER) {
				runtime_error(vm, ip - 1, "negation of non-number", NULL);
				return -1;
			}

			if (b->f & SPN_TFLG_FLOAT) {
				double res = -b->v.fltv;

				spn_value_release(a);
				a->t = SPN_TYPE_NUMBER;
				a->f = SPN_TFLG_FLOAT;
				a->v.fltv = res;
			} else {
				long res = -b->v.intv;

				spn_value_release(a);
				a->t = SPN_TYPE_NUMBER;
				a->f = 0;
				a->v.intv = res;
			}

			break;
		}
		case SPN_INS_INC:
		case SPN_INS_DEC: {
			SpnValue *val = VALPTR(vm->sp, OPA(ins));

			if (val->t != SPN_TYPE_NUMBER) {
				runtime_error(vm, ip - 1, "incrementing or decrementing non-number", NULL);
				return -1;
			}

			if (val->f & SPN_TFLG_FLOAT) {
				if (opcode == SPN_INS_INC) {
					val->v.fltv++;
				} else {
					val->v.fltv--;
				}
			} else {
				if (opcode == SPN_INS_INC) {
					val->v.intv++;
				} else {
					val->v.intv--;
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

			if (b->t != SPN_TYPE_NUMBER
			 || c->t != SPN_TYPE_NUMBER) {
				runtime_error(vm, ip - 1, "bitwise operation on non-numbers", NULL);
				return -1;
			}

			if (b->f & SPN_TFLG_FLOAT
			 || c->f & SPN_TFLG_FLOAT) {
				runtime_error(vm, ip - 1, "bitwise operation on non-integers", NULL);
				return -1;
			}

			res = bitwise_op(b, c, opcode);

			spn_value_release(a);
			a->t = SPN_TYPE_NUMBER;
			a->f = 0;
			a->v.intv = res;

			break;
		}
		case SPN_INS_BITNOT: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			long res;

			if (b->t != SPN_TYPE_NUMBER) {
				runtime_error(vm, ip - 1, "bitwise NOT on non-number", NULL);
				return -1;
			}

			if (b->f & SPN_TFLG_FLOAT) {
				runtime_error(vm, ip - 1, "bitwise NOT on non-integer", NULL);
				return -1;
			}

			res = ~b->v.intv;

			spn_value_release(a);
			a->t = SPN_TYPE_NUMBER;
			a->f = 0;
			a->v.intv = res;

			break;
		}
		case SPN_INS_LOGNOT: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			int res;

			if (b->t != SPN_TYPE_BOOL) {
				runtime_error(vm, ip - 1, "logical negation of non-Boolean value", NULL);
				return -1;
			}

			res = !b->v.boolv;

			spn_value_release(a);
			a->t = SPN_TYPE_BOOL;
			a->f = 0;
			a->v.boolv = res;

			break;
		}
		case SPN_INS_SIZEOF:
		case SPN_INS_TYPEOF: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			SpnValue res = opcode == SPN_INS_SIZEOF
					       ? sizeof_value(b)
					       : typeof_value(b);

			spn_value_release(a);
			*a = res;

			break;
		}
		case SPN_INS_CONCAT: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			SpnValue *c = VALPTR(vm->sp, OPC(ins));
			SpnString *res;

			if (b->t != SPN_TYPE_STRING
			 || c->t != SPN_TYPE_STRING) {
				runtime_error(vm, ip - 1, "concatenation of non-string values", NULL);
				return -1;
			}

			res = spn_string_concat(b->v.ptrv, c->v.ptrv);

			spn_value_release(a);
			a->t = SPN_TYPE_STRING;
			a->f = SPN_TFLG_OBJECT;
			a->v.ptrv = res;

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
				dst->t = SPN_TYPE_NIL;
				dst->f = 0;
				break;
			case SPN_CONST_TRUE:
				dst->t = SPN_TYPE_BOOL;
				dst->f = 0;
				dst->v.boolv = 1;
				break;
			case SPN_CONST_FALSE:
				dst->t = SPN_TYPE_BOOL;
				dst->f = 0;
				dst->v.boolv = 0;
				break;
			case SPN_CONST_INT: {
				long num;
				memcpy(&num, ip, sizeof(num));
				ip += ROUNDUP(sizeof(num), sizeof(spn_uword));

				dst->t = SPN_TYPE_NUMBER;
				dst->f = 0;
				dst->v.intv = num;
				break;
			}
			case SPN_CONST_FLOAT: {
				double num;
				memcpy(&num, ip, sizeof(num));
				ip += ROUNDUP(sizeof(num), sizeof(spn_uword));

				dst->t = SPN_TYPE_NUMBER;
				dst->f = SPN_TFLG_FLOAT;
				dst->v.fltv = num;
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
			TFrame *frmhdr = &vm->sp[IDX_FRMHDR].h;
			int symtabidx = frmhdr->symtabidx;
			TSymtab *symtab = &vm->lsymtabs[symtabidx];

			unsigned symidx = OPMID(ins); /* just if it's 16 bits */
			SpnValue *symp = &symtab->vals[symidx];

			SpnValue *dst = VALPTR(vm->sp, OPA(ins));

			/* if the symbol is an unresolved reference
			 * to a global function, then attempt to resolve it
			 */
			if (symp->t == SPN_TYPE_FUNC
			 && symp->f & SPN_TFLG_PENDING) {
			 	const char *fnname = symp->v.fnv.name;
				SpnValue *res = resolve_symbol(vm, fnname);
			 	if (res->t == SPN_TYPE_NIL) {
			 		const void *args[1];
			 		args[0] = symp->v.fnv.name;
			 		runtime_error(
			 			vm,
			 			ip - 1,
			 			"global `%s' was not found",
			 			args
			 		);
			 		return -1;
			 	}

			 	/* if the resolution succeeded, then we
			 	 * cache the symbol into the appropriate
			 	 * local symbol table so that we don't
			 	 * need to resolve it anymore
			 	 */
			 	*symp = *res;
			}

			/* suspicion: for symbols, we don't need the RBR idiom
			 * -- there's always at least one reference to the
			 * object, in the local and/or global symbol table.
			 * TODO: prove and test this.
			 */

			/* clean up previous value */
			spn_value_retain(symp);

			/* and set the new - now surely resolved - one */
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
		case SPN_INS_NEWARR: {
			SpnValue *dst = VALPTR(vm->sp, OPA(ins));

			spn_value_release(dst);
			dst->t = SPN_TYPE_ARRAY;
			dst->f = SPN_TFLG_OBJECT;
			dst->v.ptrv = spn_array_new();

			break;
		}
		case SPN_INS_ARRGET: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			SpnValue *c = VALPTR(vm->sp, OPC(ins));

			if (b->t == SPN_TYPE_ARRAY) {
				SpnValue *val = spn_array_get(b->v.ptrv, c);
				spn_value_retain(val);

				spn_value_release(a);
				*a = *val;
			} else if (b->t == SPN_TYPE_STRING) {
				SpnString *str = b->v.ptrv;
				long len = str->len;
				long idx;

				if (c->t != SPN_TYPE_NUMBER) {
					runtime_error(vm, ip - 1, "indexing string with non-number value", NULL);
					return -1;
				}

				if (c->f != 0) {
					runtime_error(vm, ip - 1, "indexing string with non-integer value", NULL);
					return -1;
				}

				idx = c->v.intv;

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
						"character at normalized index %d is\n"
						"out of bounds for string of length %d",
						args
					);
					return -1;
				}

				spn_value_release(a);
				a->t = SPN_TYPE_NUMBER;
				a->f = 0;
				a->v.intv = (unsigned char)(str->cstr[idx]);
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

			if (a->t != SPN_TYPE_ARRAY) {
				runtime_error(vm, ip - 1, "assignment to member of non-array value", NULL);
				return -1;
			}

			spn_array_set(a->v.ptrv, b, c);
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

			if (b->t != SPN_TYPE_NUMBER) {
				runtime_error(vm, ip - 1, "non-number argument to `#' operator", NULL);
				return -1;
			}

			if (b->f & SPN_TFLG_FLOAT) {
				runtime_error(vm, ip - 1, "non-integer argument to `#' operator", NULL);
				return -1;
			}

			argidx = b->v.intv;

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
				/* if index is out of bounds, yield nil */
				spn_value_release(a);
				a->t = SPN_TYPE_NIL;
				a->f = 0;
			}

			break;
		}
		case SPN_INS_GLBSYM: {
			SpnValue funcval;
			SpnValue funckey;

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
			 * arguments could fit in the first `argc` registers?)
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
			funcval.t = SPN_TYPE_FUNC;
			funcval.f = 0;
			funcval.v.fnv.name = symname;
			funcval.v.fnv.r.bc = hdr;
			funcval.v.fnv.symtabidx = vm->sp[IDX_FRMHDR].h.symtabidx;
			/* (the environment of a global function is always the
			 * environment of the compilation unit itself)
			 */

			funckey.t = SPN_TYPE_STRING;
			funckey.f = SPN_TFLG_OBJECT;
			funckey.v.ptrv = spn_string_new_nocopy_len(symname, namelen, 0);

			/* check for a function with the same name -- if one
			 * exists, it's an error, there should be no functions
			 * with identical names (except lambdas, they all have
			 * the same name, but it's unused anyway).
			 */
			if (spn_array_get(vm->glbsymtab, &funckey)->t != SPN_TYPE_NIL) {
				const void *args[1];
				args[0] = symname;
				runtime_error(
					vm,
					ip,
					"re-definition of global `%s'",
					args
				);
				spn_object_release(funckey.v.ptrv);
				return -1;
			}

			/* if everything was OK, add the function to the global
			 * symbol table
			 */
			spn_array_set(vm->glbsymtab, &funckey, &funcval);
			spn_object_release(funckey.v.ptrv);

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
		runtime_error(vm, NULL, "bytecode magic number is invalid", NULL);
		return -1;
	}

	return 0;
}

static int read_local_symtab(SpnVMachine *vm, spn_uword *bc)
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
			return i;
		}
	}

	/* if we got here, the bytecode file is being run for the first time
	 * on this VM instance, so we proceed to reading its symbol table
	 */
	lsidx = vm->lscount++;

	vm->lsymtabs = realloc(vm->lsymtabs, vm->lscount * sizeof(vm->lsymtabs[0]));
	if (vm->lsymtabs == NULL) {
		abort();
	}

	/* initialize new local symbol table */
	cursymtab = &vm->lsymtabs[lsidx];
	cursymtab->bc = bc;
	cursymtab->size = symcount;
	cursymtab->vals = malloc(symcount * sizeof(cursymtab->vals[0]));

	if (cursymtab->vals == NULL) {
		abort();
	}

	/* then actually read the symbols */
	for (i = 0; i < symcount; i++) {
		spn_uword ins = *stp++;
		enum spn_local_symbol sym = OPCODE(ins);

		switch (sym) {
		case SPN_LOCSYM_STRCONST: {
			SpnString *str;
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

			str = spn_string_new_nocopy_len(cstr, len, 0);

			cursymtab->vals[i].t = SPN_TYPE_STRING;
			cursymtab->vals[i].f = SPN_TFLG_OBJECT;
			cursymtab->vals[i].v.ptrv = str;

			stp += nwords;
			break;
		}
		case SPN_LOCSYM_FUNCSTUB: {
			const char *fnname = (const char *)(stp);
			size_t len = OPLONG(ins);
			size_t nwords = ROUNDUP(len + 1, sizeof(spn_uword));

#ifndef NDEBUG
			/* sanity check: the funcion name length recorded in
			 * the bytecode and the actual length
			 * reported by `strlen()` shall match.
			 */

			size_t reallen = strlen(fnname);
			assert(len == reallen);
#endif

			cursymtab->vals[i].t = SPN_TYPE_FUNC;
			cursymtab->vals[i].f = SPN_TFLG_PENDING;
			cursymtab->vals[i].v.fnv.name = fnname;

			/* note that `cursymtab->vals[i].v.fnv.symtabidx`
			 * _must not_ be set here: a function stub is merely
			 * an unresolved reference to a global function,
			 * we don't know yet in which translation unit
			 * the actual definition will be.
			 */

			stp += nwords;
			break;	
		}
		case SPN_LOCSYM_LAMBDA: {
			size_t hdroff = OPLONG(ins);
			spn_uword *entry = bc + hdroff;

			cursymtab->vals[i].t = SPN_TYPE_FUNC;
			cursymtab->vals[i].f = 0;
			cursymtab->vals[i].v.fnv.name = NULL; /* lambda -> unnamed */
			cursymtab->vals[i].v.fnv.r.bc = entry;
			cursymtab->vals[i].v.fnv.symtabidx = lsidx;

			/* unlike global functions, lambda functions can only
			 * be implemented in the same translation unit int
			 * which their local symtab entry is defined.
			 * So we *can* (and should) fill in the `symtabidx'
			 * member of the SpnValue while reading the symtab.
			 */

			break;
		}
		default:
			{
				int int_sym = sym;
				const void *args[1];
				args[0] = &int_sym;
				runtime_error(
					vm,
					stp - 1,
					"incorrect local symbol kind `%i' in read_local_symtab()\n",
					args
				);

				/* so that free_local_symtab() won't attempt to release
				 * uninitialized values (it's necessary to release the
				 * already initialized ones, though, else this leaks)
				 */
				cursymtab->size = i;

				return -1;
			}
		}
	}

	return lsidx;
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

	assert(lhs->t == SPN_TYPE_NUMBER && rhs->t == SPN_TYPE_NUMBER);

	if (lhs->f & SPN_TFLG_FLOAT) {
		if (rhs->f & SPN_TFLG_FLOAT) {
			res =	lhs->v.fltv < rhs->v.fltv ? -1
			      : lhs->v.fltv > rhs->v.fltv ? +1
			      :				     0;
		} else {
			res =	lhs->v.fltv < rhs->v.intv ? -1
			      : lhs->v.fltv > rhs->v.intv ? +1
			      :				     0;
		}
	} else {
		if (rhs->f & SPN_TFLG_FLOAT) {
			res =	lhs->v.intv < rhs->v.fltv ? -1
			      : lhs->v.intv > rhs->v.fltv ? +1
			      :				     0;
		} else {
			res =	lhs->v.intv < rhs->v.intv ? -1
			      : lhs->v.intv > rhs->v.intv ? +1
			      :				     0;
		}
	}

	return cmp2bool(res, op);
}

static SpnValue arith_op(const SpnValue *lhs, const SpnValue *rhs, int op)
{
	SpnValue res;

	assert(lhs->t == SPN_TYPE_NUMBER && rhs->t == SPN_TYPE_NUMBER);

	if (lhs->f & SPN_TFLG_FLOAT || rhs->f & SPN_TFLG_FLOAT) {
		double a = lhs->f & SPN_TFLG_FLOAT ? lhs->v.fltv : lhs->v.intv;
		double b = rhs->f & SPN_TFLG_FLOAT ? rhs->v.fltv : rhs->v.intv;
		double y;

		switch (op) {
		case SPN_INS_ADD: y = a + b; break;
		case SPN_INS_SUB: y = a - b; break;
		case SPN_INS_MUL: y = a * b; break;
		case SPN_INS_DIV: y = a / b; break;
		default: y = 0; SHANT_BE_REACHED();
		}

		res.t = SPN_TYPE_NUMBER;
		res.f = SPN_TFLG_FLOAT;
		res.v.fltv = y;
	} else {
		long a = lhs->v.intv;
		long b = rhs->v.intv;
		long y;

		switch (op) {
		case SPN_INS_ADD: y = a + b; break;
		case SPN_INS_SUB: y = a - b; break;
		case SPN_INS_MUL: y = a * b; break;
		case SPN_INS_DIV: y = a / b; break;
		default: y = 0; SHANT_BE_REACHED();
		}

		res.t = SPN_TYPE_NUMBER;
		res.f = 0;
		res.v.intv = y;
	}

	return res;
}

static long bitwise_op(const SpnValue *lhs, const SpnValue *rhs, int op)
{
	long a, b;

	assert(lhs->t == SPN_TYPE_NUMBER && rhs->t == SPN_TYPE_NUMBER);
	assert(!(lhs->f & SPN_TFLG_FLOAT) && !(rhs->f & SPN_TFLG_FLOAT));

	a = lhs->v.intv;
	b = rhs->v.intv;

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

static SpnValue *resolve_symbol(SpnVMachine *vm, const char *name)
{
	SpnValue *res;

	SpnValue nameval;
	nameval.t = SPN_TYPE_STRING;
	nameval.f = SPN_TFLG_OBJECT;
	nameval.v.ptrv = spn_string_new_nocopy(name, 0);

	res = spn_array_get(vm->glbsymtab, &nameval);
	spn_object_release(nameval.v.ptrv);

	return res;
}

static SpnValue sizeof_value(SpnValue *val)
{
	SpnValue res;

	res.t = SPN_TYPE_NUMBER;
	res.f = 0;

	switch (val->t) {
	case SPN_TYPE_NIL: {
		res.v.intv = 0;
		break;
	}
	case SPN_TYPE_STRING: {
		SpnString *str = val->v.ptrv;
		res.v.intv = str->len;
		break;
	}
	case SPN_TYPE_ARRAY: {
		SpnArray *arr = val->v.ptrv;
		res.v.intv = spn_array_count(arr);
		break;
	}
	case SPN_TYPE_USRDAT: {
		/* TODO: implement sizeof() for custom object types */
		res.v.intv = 1;
		break;
	}
	default:
		/* all other types represent one value, conceptually */
		res.v.intv = 1;
		break;
	}

	return res;
}

static SpnValue typeof_value(SpnValue *val)
{
	static SpnString *niltype	= NULL,
			 *booltype	= NULL,
			 *numtype	= NULL,
			 *fntype	= NULL,
			 *strtype	= NULL,
			 *arrtype	= NULL,
			 *usrtype	= NULL;

	int retainflag = 1;

	SpnValue res;
	res.t = SPN_TYPE_STRING;
	res.f = SPN_TFLG_OBJECT;

	switch (val->t) {
	case SPN_TYPE_NIL:
		if (niltype == NULL) {
			niltype = spn_string_new_nocopy("nil", 0);
		}

		res.v.ptrv = niltype;
		break;
	case SPN_TYPE_BOOL:
		if (booltype == NULL) {
			booltype = spn_string_new_nocopy("bool", 0);
		}

		res.v.ptrv = booltype;
		break;
	case SPN_TYPE_NUMBER:
		if (numtype == NULL) {
			numtype = spn_string_new_nocopy("number", 0);
		}

		res.v.ptrv = numtype;
		break;
	case SPN_TYPE_FUNC:
		if (fntype == NULL) {
			fntype = spn_string_new_nocopy("function", 0);
		}

		res.v.ptrv = fntype;
		break;
	case SPN_TYPE_STRING:
		if (strtype == NULL) {
			strtype = spn_string_new_nocopy("string", 0);
		}

		res.v.ptrv = strtype;
		break;
	case SPN_TYPE_ARRAY:
		if (arrtype == NULL) {
			arrtype = spn_string_new_nocopy("array", 0);
		}

		res.v.ptrv = arrtype;
		break;
	case SPN_TYPE_USRDAT:
		if (val->f & SPN_TFLG_OBJECT) {
			/* custom object */
			const char *classname = spn_object_type(val->v.ptrv);
			res.v.ptrv = spn_string_new_nocopy(classname, 0);
			retainflag = 0;
		} else {
			/* custom non-object */
			if (usrtype == NULL) {
				usrtype = spn_string_new_nocopy("userdata", 0);
			}

			res.v.ptrv = usrtype;
		}

		break;
	default:
		SHANT_BE_REACHED();
	}

	if (retainflag) {
		spn_value_retain(&res);
	}

	return res;
}

