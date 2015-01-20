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
 * the object have been changed to bogus values, but the 'isa' field and the
 * allocsize members stayed intact. Welp.
 *
 * So, unless I either stop generating bytecode by hand or I'm entirely sure
 * that the compiler is perfect, I'd better stick to asserting that the
 * register indices are within the bounds of the stack frame...
 *
 * arguments: 's' - stack pointer; 'r': register index
 */
#ifndef NDEBUG
#define VALPTR(s, r) (assert((r) < (s)[IDX_FRMHDR].h.size - EXTRA_SLOTS), &(s)[(-(int)(r) + REG_OFFSET)].v)
#define SLOTPTR(s, r) (assert((r) < (s)[IDX_FRMHDR].h.size - EXTRA_SLOTS), &(s)[(-(int)(r) + REG_OFFSET)])
#else
#define VALPTR(s, r) (&(s)[(-(int)(r) + REG_OFFSET)].v)
#define SLOTPTR(s, r) (&(s)[(-(int)(r) + REG_OFFSET)])
#endif


/* An index into the array of local symbol tables is used instead of a
 * pointer to the symtab struct itself because that array may be
 * 'realloc()'ated, and then we have an invalid pointer once again
 */
typedef struct TFrame {
	size_t       size;       /* no. of slots, including EXTRA_SLOTS */
	int          decl_argc;  /* declaration argument count          */
	int          extra_argc; /* number of extra args, if any (or 0) */
	int          real_argc;  /* number of call args                 */
	spn_uword   *retaddr;    /* return address (points to bytecode) */
	ptrdiff_t    retidx;     /* pointer into the caller's frame     */
	SpnFunction *callee;     /* the called function itself          */
	SpnArray    *argv;       /* lazily loaded argument vector       */
} TFrame;

/* see http://stackoverflow.com/q/18310789/ */
typedef union TSlot {
	TFrame h;
	SpnValue v;
} TSlot;

struct SpnVMachine {
	TSlot      *stack;      /* base of the stack            */
	TSlot      *sp;         /* stack pointer                */
	size_t      stackallsz; /* stack alloc size in frames   */

	SpnHashMap *glbsymtab;  /* global symbol table          */
	SpnHashMap *classes;    /* class descriptors            */

	SpnValue    supername;  /* the string "super"           */
	SpnValue    getname;    /* the string "get"             */
	SpnValue    setname;    /* the string "set"             */

	char       *errmsg;     /* last (runtime) error message */
	int         haserror;   /* whether an error occurred    */
	void       *ctx;        /* context info, use at will    */
};

/* this is the structure used by 'push_and_copy_args()' */
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
	SpnFunction *fn,
	const struct args_copy_descriptor *desc,
	int argc
);


static int dispatch_loop(SpnVMachine *vm, spn_uword *ip, SpnValue *ret);

/* this only releases the values stored in the stack frames */
static void free_frames(SpnVMachine *vm);

/* stack manipulation */
static size_t stacksize(SpnVMachine *vm);
static void expand_stack(SpnVMachine *vm, size_t nregs);

static void push_frame(
	SpnVMachine *vm,
	int nregs,
	int decl_argc,
	int extra_argc,
	int real_argc,
	spn_uword *retaddr,
	ptrdiff_t retidx,
	SpnFunction *callee
);
static void pop_frame(SpnVMachine *vm);

/* this function helps including native functions' names in the stack trace */
static void push_native_pseudoframe(SpnVMachine *vm, SpnFunction *callee);

/* reads/creates the local symbol table of 'program' if necessary,
 * then stores it back into the function object.
 */
static void read_local_symtab(SpnFunction *program);

/* accessing function arguments */
static SpnValue *nth_call_arg(TSlot *sp, spn_uword *ip, int idx);
static SpnValue *nth_vararg(TSlot *sp, int idx);

/* emulating the ALU... */
static int cmp2bool(int res, int op);
static SpnValue arith_op(const SpnValue *lhs, const SpnValue *rhs, int op);
static long bitwise_op(const SpnValue *lhs, const SpnValue *rhs, int op);

/* ...and a weak dynamic linker */
static int resolve_symbol(SpnVMachine *vm, spn_uword *ip, SpnValue *symp);

/* array, hashmap, string indexing validation */
static int indexing_array_check(SpnVMachine *vm, spn_uword *ip, SpnValue *varr, SpnValue *vidx);
static int indexing_string_check(SpnVMachine *vm, spn_uword *ip, SpnValue *vstr, SpnValue *vidx);
static int indexing_hashmap_check(SpnVMachine *vm, spn_uword *ip, SpnValue *vidx);

/* return value:
 * non-zero if the property is a special built-in and it was processed successfully,
 * 0 otherwise.
 */
static int get_builtin_property(SpnValue *dstreg, SpnValue *pself, SpnValue *nameval);

static int lookup_member(SpnVMachine *vm, SpnValue *result, SpnValue *pself, SpnValue *name);

/* type information, reflection */
static SpnValue typeof_value(SpnValue *val);

/* generating a runtime error (message) */
static void runtime_error(SpnVMachine *vm, spn_uword *ip, const char *fmt, const void *args[]);

SpnVMachine *spn_vm_new(void)
{
	SpnVMachine *vm = spn_malloc(sizeof(*vm));

	/* initialize stack */
	vm->stackallsz = 0;
	vm->stack = NULL;
	vm->sp = NULL;

	/* initialize the global symbol table and class descriptors */
	vm->glbsymtab = spn_hashmap_new();
	vm->classes   = spn_hashmap_new();

	vm->supername = makestring_nocopy("super");
	vm->getname   = makestring_nocopy("get");
	vm->setname   = makestring_nocopy("set");

	/* set up error reporting and context info */
	vm->errmsg = NULL;
	vm->haserror = 0;
	vm->ctx = NULL;

	return vm;
}

void spn_vm_free(SpnVMachine *vm)
{
	/* free the stack */
	free_frames(vm);
	free(vm->stack);

	/* free the global symbol table and all class descirptors */
	spn_object_release(vm->glbsymtab);
	spn_object_release(vm->classes);

	/* free special string indices */
	spn_value_release(&vm->supername);
	spn_value_release(&vm->getname);
	spn_value_release(&vm->setname);

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
	buf = spn_malloc(i * sizeof buf[0]);

	*size = i;

	i = 0;
	sp = vm->sp;
	while (sp > vm->stack) {
		TFrame *frmhdr = &sp[IDX_FRMHDR].h;
		buf[i++] = frmhdr->callee->name;
		sp -= frmhdr->size;
	}

	return buf;
}

SpnHashMap *spn_vm_getglobals(SpnVMachine *vm)
{
	return vm->glbsymtab;
}

/* get the class descriptor table of the VM */
SpnHashMap *spn_vm_getclasses(SpnVMachine *vm)
{
	return vm->classes;
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

int spn_vm_callfunc(
	SpnVMachine *vm,
	SpnFunction *fn,
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

	/* native functions are easy to deal with */
	if (fn->native) {
		int err;

		/* "return nothing" should mean "implicitly return nil" */
		SpnValue tmpret = spn_nilval;

		/* push pseudo-frame to include function name in stack trace */
		push_native_pseudoframe(vm, fn);

		err = fn->repr.fn(&tmpret, argc, argv, vm->ctx);
		if (err != 0) {
			const void *args[2];
			args[0] = fn->name;
			args[1] = &err;
			spn_vm_seterrmsg(vm, "error in function '%s' (code: %i)", args);
		} else {
			if (retval != NULL) {
				*retval = tmpret;
			} else {
				spn_value_release(&tmpret);
			}

			pop_frame(vm);
		}

		return err;
	}

	/* if we got here, the callee is a valid Sparkling function.
	 * First, check if the function is a top-level program.
	 * If so, read the local symbol table (if necessary).
	 */
	if (fn->topprg) {
		read_local_symtab(fn);
	}

	/* compute entry point */
	fnhdr = fn->repr.bc;
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
	SpnHashMap *storage;
	size_t i;

	/* a NULL libname means that the functions will be global */
	if (libname != NULL) {
		SpnValue libval = spn_hashmap_get_strkey(vm->glbsymtab, libname);

		if (notnil(&libval) && !ishashmap(&libval)) {
			spn_die(
				"global '%s' already exists but is not a hashmap (%s)",
				libname,
				spn_type_name(valtype(&libval))
			);
		}

		/* if library does not exist it must be created */
		if (isnil(&libval)) {
			libval = makehashmap();
			spn_hashmap_set_strkey(vm->glbsymtab, libname, &libval);
			spn_value_release(&libval); /* still alive, was retained */
		}

		storage = hashmapvalue(&libval);
	} else {
		storage = vm->glbsymtab;
	}

	for (i = 0; i < n; i++) {
		SpnValue val = makenativefunc(fns[i].name, fns[i].fn);
		spn_hashmap_set_strkey(storage, fns[i].name, &val);
		spn_value_release(&val);
	}
}

void spn_vm_addlib_values(SpnVMachine *vm, const char *libname, const SpnExtValue vals[], size_t n)
{
	SpnHashMap *storage;
	size_t i;

	/* a NULL libname means that the functions will be global */
	if (libname != NULL) {
		SpnValue libval = spn_hashmap_get_strkey(vm->glbsymtab, libname);

		if (notnil(&libval) && !ishashmap(&libval)) {
			spn_die(
				"global '%s' already exists but is not a hashmap (%s)",
				libname,
				spn_type_name(valtype(&libval))
			);
		}

		/* if library does not exist it must be created */
		if (isnil(&libval)) {
			libval = makehashmap();
			spn_hashmap_set_strkey(vm->glbsymtab, libname, &libval);
			spn_value_release(&libval); /* still alive, was retained */
		}

		storage = hashmapvalue(&libval);
	} else {
		storage = vm->glbsymtab;
	}

	for (i = 0; i < n; i++) {
		spn_hashmap_set_strkey(storage, vals[i].name, &vals[i].value);
	}
}

/* ip == NULL indicates an error in native code
 * Apart from creating an error message, this function also sets
 * the 'haserror' flag, which is essential for the stack trace
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
		spn_uword *prog_bc = vm->sp[IDX_FRMHDR].h.callee->env->repr.bc;
		unsigned long addr = ip - prog_bc;
		const void *prefix_args[1];
		prefix_args[0] = &addr;
		prefix = spn_string_format_cstr(
			"runtime error at address 0x%08x: ",
			&prefix_len,
			prefix_args
		);
	} else {
		prefix = spn_string_format_cstr( /* glorified strdup() */
			"runtime error in native code: ",
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

/* because 'NULL - NULL' is UB */
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
		vm->stackallsz *= 2;
	}

	vm->stack = spn_realloc(vm->stack, vm->stackallsz * sizeof vm->stack[0]);

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
	SpnFunction *callee
)
{
	int i;

	/* real frame allocation size, including extra call-time arguments,
	 * frame header and implicit self
	 */
	int real_nregs = nregs + extra_argc + EXTRA_SLOTS;

	/* just a bit of sanity check in case someone misinterprets how
	 * 'extra_argc' is computed...
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
		vm->sp[i].v = spn_nilval;
	}

	/* initialize activation record header */
	vm->sp[IDX_FRMHDR].h.size = real_nregs;
	vm->sp[IDX_FRMHDR].h.decl_argc = decl_argc;
	vm->sp[IDX_FRMHDR].h.extra_argc = extra_argc;
	vm->sp[IDX_FRMHDR].h.real_argc = real_argc;
	vm->sp[IDX_FRMHDR].h.retaddr = retaddr; /* if NULL, return to C-land */
	vm->sp[IDX_FRMHDR].h.retidx = retidx; /* if negative, return to C-land */
	vm->sp[IDX_FRMHDR].h.callee = callee;
	vm->sp[IDX_FRMHDR].h.argv = NULL;
}

static void push_native_pseudoframe(SpnVMachine *vm, SpnFunction *callee)
{
	push_frame(vm, 0, 0, 0, 0, NULL, -1, callee);
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

	/* release argv, if any */
	if (hdr->argv) {
		spn_object_release(hdr->argv);
	}

	/* adjust stack pointer */
	vm->sp -= nregs;
}

/* retrieve a pointer to the register denoted by the 'idx'th octet
 * of an array of 'spn_uword's (which is the instruction pointer)
 */
static SpnValue *nth_call_arg(TSlot *sp, spn_uword *ip, int idx)
{
	int regidx = nth_arg_idx(ip, idx);
	return VALPTR(sp, regidx);
}

/* get the 'idx'th unnamed argument from a stack frame
 * XXX: this does **NOT** check for an out-of-bounds error in release
 * mode, that's why 'argidx < hdr->extra_argc' is checked in SPN_INS_ARGV.
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
	SpnFunction *fn,
	const struct args_copy_descriptor *desc,
	int argc
)
{
	int i;
	spn_uword *fnhdr;
	int decl_argc;
	int extra_argc;
	int nregs;

	/* this whole copying thingy only applies if we're calling
	 * a Sparkling function. Native callees are handled separately.
	 */
	assert(fn->native == 0);

	/* see Remark (VI) in vm.h in order to get an
	 * understanding of the layout of the
	 * bytecode representing a function
	 */
	fnhdr = fn->repr.bc;
	decl_argc = fnhdr[SPN_FUNCHDR_IDX_ARGC];
	nregs = fnhdr[SPN_FUNCHDR_IDX_NREGS];

	/* if there are less call arguments than formal
	 * parameters, we set extra_argc to 0 (and all
	 * the unspecified arguments are implicitly set
	 * to 'nil' by push_frame)
	 */
	extra_argc = argc > decl_argc ? argc - decl_argc : 0;

	/* sanity check: decl_argc should be <= nregs,
	 * else arguments wouldn't fit in the registers
	 */
	assert(decl_argc <= nregs);

	/* push a new stack frame - after that,
	 * '&vm->sp[IDX_FRMHDR].h' is a pointer to
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
		fn
	);

	/* first, fill in arguments that fit into the
	 * first 'decl_argc' registers (i. e. those
	 * that are declared as formal parameters). The
	 * number of call arguments may be less than
	 * the number of formal parameters; handle that
	 * case too ('&& i < argc').
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
			 * 'realloc()' the stack, thus rendering saved pointers
			 * invalid.
			 */
			TSlot *retslot = SLOTPTR(vm->sp, OPA(ins));
			ptrdiff_t retidx = retslot - vm->stack;

			TSlot *funcslot = SLOTPTR(vm->sp, OPB(ins));
			SpnValue func = funcslot->v; /* copy the value struct */
			SpnFunction *fnobj;

			int argc = OPC(ins);

			/* this is the minimal number of 'spn_uword's needed
			 * to store the register numbers representing
			 * call-time arguments
			 */
			int narggroups = ROUNDUP(argc, SPN_WORD_OCTETS);

			/* check if value is really a function */
			if (!isfunc(&func)) {
				const void *args[1];
				args[0] = spn_type_name(func.type);
				runtime_error(
					vm,
					ip - 1,
					"attempt to call value of type %s",
					args
				);
				return -1;
			}

			fnobj = funcvalue(&func);

			if (fnobj->native) {	/* native function */
				int i, err;
				SpnValue tmpret = spn_nilval;
				SpnValue *argv;

				#define MAX_AUTO_ARGC 16
				SpnValue auto_argv[MAX_AUTO_ARGC];

				/* allocate a big enough array for the arguments */
				if (argc > MAX_AUTO_ARGC) {
					argv = spn_malloc(argc * sizeof argv[0]);
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
				push_native_pseudoframe(vm, fnobj);

				/* then call the native function. its return
				 * value must have a reference count of one.
				 */
				err = fnobj->repr.fn(&tmpret, argc, argv, vm->ctx);

				if (argc > MAX_AUTO_ARGC) {
					free(argv);
				}

				#undef MAX_AUTO_ARGC

				/* check if the native function returned
				 * an error. If so, abort execution.
				 * Also check if the callee generated a
				 * custom error message; if so, use it.
				 */
				if (err != 0) {
					const void *args[2];
					args[0] = fnobj->name;
					args[1] = &err;
					spn_vm_seterrmsg(vm, "error in function '%s' (code: %i)", args);
					return err;
				}

				/* should not return success after an error */
				assert(vm->haserror == 0);

				/* clear and set return value register
				 * (it's released only now because it may be
				 * the same as one of the arguments:
				 * foo = bar(foo) won't do any good if foo is
				 * released before it's passed to the function.)
				 *
				 * In addition, this is only done _after_ error checking,
				 * since an error in a native function may mean that it has
				 * clobbered the return value by filling it with an object
				 * then deallocating that object. In this case, we don't
				 * want to copy the deallocated value back to the Sparkling
				 * stack, since a 'pop()' (most probably called from
				 * 'clean_vm_if_needed()' before the next function call)
				 * would then attempt to double-free it.
				 */
				spn_value_release(&vm->stack[retidx].v);
				vm->stack[retidx].v = tmpret;

				/* pop pseudo-frame */
				pop_frame(vm);

				/* advance IP past the argument indices (round
				 * up to nearest number of 'spn_uword's that
				 * can accomodate 'argc' octets)
				 */
				ip += narggroups;
			} else {
				/* Sparkling function
				 * The return address is the address of the
				 * instruction immediately following the
				 * current CALL instruction.
				 */
				spn_uword *retaddr = ip + narggroups;
				spn_uword *fnhdr = fnobj->repr.bc;
				spn_uword *entry = fnhdr + SPN_FUNCHDR_LEN;
				ptrdiff_t calleroff = vm->sp - vm->stack;
				struct args_copy_descriptor desc;

				/* if function designates top-level program,
				 * then parse its local symbol table
				 */
				if (fnobj->topprg) {
					read_local_symtab(fnobj);
				}

				/* set up environment for push_and_copy_args */
				desc.caller_is_native = 0; /* we, the caller, are a Sparkling function */
				desc.env.script_env.ip = ip;
				desc.env.script_env.retaddr = retaddr;
				desc.env.script_env.calleroff = calleroff;
				desc.env.script_env.retidx = retidx;

				/* push the frame of the callee, and
				 * copy over its arguments
				 * (I <3 descriptive function names!)
				 */
				push_and_copy_args(vm, fnobj, &desc, argc);

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
			 * implementation-defined. Should 'memcpy()' be used
			 * here instead of an assignment?
			 */
			spn_sword offset = *ip++;

			if (!isbool(reg)) {
				runtime_error(
					vm,
					ip - 2,
					"non-Boolean value used as condition or logical expression",
					NULL
				);
				return -1;
			}

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

			int cmpres;

			if (!spn_values_comparable(b, c)) {
				const void *args[2];
				args[0] = spn_type_name(b->type);
				args[1] = spn_type_name(c->type);

				runtime_error(
					vm,
					ip - 1,
					"ordered comparison of uncomparable values"
					" of type %s and %s",
					args
				);

				return -1;
			}

			cmpres = spn_value_compare(b, c);
			spn_value_release(a);
			*a = makebool(cmp2bool(cmpres, opcode));

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

			if (!isnum(b) || !isnum(c)) {
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

			if (!isnum(b)) {
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

			if (!isnum(val)) {
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
		case SPN_INS_TYPEOF: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));

			SpnValue res = typeof_value(b);
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
				*dst = spn_nilval;
				break;
			case SPN_CONST_TRUE:
				*dst = spn_trueval;
				break;
			case SPN_CONST_FALSE:
				*dst = spn_falseval;
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

			TFrame *frmhdr = &vm->sp[IDX_FRMHDR].h;
			SpnArray *symtab = frmhdr->callee->symtab;
			SpnValue sym = spn_array_get(symtab, symidx);
			assert(notnil(&sym)); /* must not be nil */

			/* if the symbol is an unresolved reference
			 * to a global, then attempt to resolve it
			 * (it need not be of type function.)
			 * Then, cache it in the local symbol table.
			 */
			if (is_symstub(&sym)) {
				if (resolve_symbol(vm, ip - 1, &sym) != 0) {
					return -1;
				}

				spn_array_set(symtab, symidx, &sym);
			}

			/* set the new - now surely resolved - value */
			spn_value_retain(&sym);
			spn_value_release(dst);
			*dst = sym;

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
		case SPN_INS_ARGV: {
			TFrame *hdr = &vm->sp[IDX_FRMHDR].h;
			SpnValue *a = VALPTR(vm->sp, OPA(ins));

			/* construct argument vector lazily, on-demand */
			if (hdr->argv == NULL) {
				int i;
				hdr->argv = spn_array_new();

				/* copy declared arguments:
				 * they always reside in the first decl_argc registers
				 */
				for (i = 0; i < hdr->decl_argc && i < hdr->real_argc; i++) {
					SpnValue *argptr = VALPTR(vm->sp, i);
					spn_array_push(hdr->argv, argptr);
				}

				/* copy variadic arguments, if any */
				for (i = 0; i < hdr->extra_argc; i++) {
					SpnValue *argptr = nth_vararg(vm->sp, i);
					spn_array_push(hdr->argv, argptr);
				}
			}

			/* it's safe to release the register before retaining argv,
			 * because the SpnArray backing argv is always referred to
			 * by the strong 'hdr->argv' pointer.
			 */
			spn_value_release(a);
			a->type = SPN_TYPE_ARRAY;
			a->v.o = hdr->argv;
			spn_value_retain(a);

			break;
		}
		case SPN_INS_NEWARR: {
			SpnValue *dst = VALPTR(vm->sp, OPA(ins));
			spn_value_release(dst);
			*dst = makearray();
			break;
		}
		case SPN_INS_NEWHASH: {
			SpnValue *dst = VALPTR(vm->sp, OPA(ins));
			spn_value_release(dst);
			*dst = makehashmap();
			break;
		}
		case SPN_INS_IDX_GET: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			SpnValue *c = VALPTR(vm->sp, OPC(ins));

			if (ishashmap(b)) {
				SpnValue val = spn_hashmap_get(hashmapvalue(b), c);
				spn_value_retain(&val);
				spn_value_release(a);
				*a = val;
			} else if (isarray(b)) {
				SpnValue val;

				if (indexing_array_check(vm, ip - 1, b, c) != 0) {
					return -1;
				}

				val = spn_array_get(arrayvalue(b), intvalue(c));
				spn_value_retain(&val);
				spn_value_release(a);
				*a = val;
			} else if (isstring(b)) {
				unsigned char ch;
				long index;
				SpnString *str;

				if (indexing_string_check(vm, ip - 1, b, c) != 0) {
					return -1;
				}

				str = stringvalue(b);
				index = intvalue(c);
				ch = str->cstr[index];

				spn_value_release(a);
				*a = makeint(ch);
			} else {
				const void *args[1];
				args[0] = spn_type_name(b->type);
				runtime_error(vm, ip - 1, "cannot subscript value of type %s", args);
				return -1;
			}

			break;
		}
		case SPN_INS_IDX_SET: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));
			SpnValue *c = VALPTR(vm->sp, OPC(ins));

			if (ishashmap(a)) {
				if (indexing_hashmap_check(vm, ip - 1, b) != 0) {
					return -1;
				}

				spn_hashmap_set(hashmapvalue(a), b, c);
			} else if (isarray(a)) {
				if (indexing_array_check(vm, ip - 1, a, b) != 0) {
					return -1;
				}

				spn_array_set(arrayvalue(a), intvalue(b), c);
			} else {
				const void *args[1];
				args[0] = spn_type_name(a->type);
				runtime_error(vm, ip - 1, "cannot index value of type %s", args);
				return -1;
			}

			break;
		}
		case SPN_INS_ARR_PUSH: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins));
			SpnValue *b = VALPTR(vm->sp, OPB(ins));

			assert(isarray(a));

			spn_array_push(arrayvalue(a), b);

			break;
		}
		case SPN_INS_FUNCTION: {
			/* pointer to the symbol header, see the SPN_FUNCHDR_*
			 * macros in vm.h.
			 * save header position, fill in properties
			 */
			spn_uword *hdr = ip;
			size_t bodylen = hdr[SPN_FUNCHDR_IDX_BODYLEN];

			/* sanity check: argc <= nregs is a must (else how
			 * could arguments fit in the first 'argc' registers?)
			 */
			assert(hdr[SPN_FUNCHDR_IDX_ARGC] <= hdr[SPN_FUNCHDR_IDX_NREGS]);

			/* skip the function header and function body */
			ip += SPN_FUNCHDR_LEN + bodylen;

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
			SpnValue auxval = spn_hashmap_get_strkey(vm->glbsymtab, symname);

#ifndef NDEBUG
			size_t reallen = strlen(symname);
			assert(namelen == reallen);
#endif

			/* skip symbol name */
			ip += nwords;

			/* attempt adding value to global symtab.
			 * Raise a runtime error if the name is taken.
			 */
			if (notnil(&auxval)) {
				const void *args[1];
				args[0] = symname;
				runtime_error(
					vm,
					ip - nwords - 1,
					"re-definition of global '%s'",
					args
				);

				return -1;
			}

			spn_hashmap_set_strkey(vm->glbsymtab, symname, src);
			break;
		}
		case SPN_INS_CLOSURE: {
			int reg_index = OPA(ins);
			int n_upvals = OPB(ins);
			int i;

			SpnFunction *prototype;
			SpnFunction *closure;

			/* We need a reference to the environment of the closure
			 * to be created. It is the currently executing function.
			 */
			SpnFunction *enclosing_fn = vm->sp[IDX_FRMHDR].h.callee;

			/* create a closure function object by appropriately
			 * "copying" the prototype in the reg_index-th register
			 */
			SpnValue *prototype_val = VALPTR(vm->sp, reg_index);
			assert(isfunc(prototype_val));
			prototype = funcvalue(prototype_val);

			closure = spn_func_new_closure(prototype);

			/* replace the prototype in the register with the newly
			 * created closure object early, so that if the closure
			 * is self-capturing, then it still correctly captures
			 * itself (i. e. the closure object) as an upvalue,
			 * as opposed to its prototype (which is not a closure).
			 *
			 * We can re-use the pointer since this instruction
			 * does not push to the stack, so the stack is not
			 * realloc()'ed, and consequently, pointers into the
			 * stack frame are not invalidated.
			 */
			prototype_val->type = SPN_TYPE_FUNC; /* redundant */
			prototype_val->v.o = closure;

			for (i = 0; i < n_upvals; i++) {
				spn_uword upval_desc = *ip++;
				enum spn_upval_type upval_type = OPCODE(upval_desc);
				int upval_index = OPA(upval_desc);

				switch (upval_type) {
				case SPN_UPVAL_LOCAL: {
					/* upvalue is local variable of enclosing function */
					SpnValue *upval = VALPTR(vm->sp, upval_index);
					spn_array_push(closure->upvalues, upval);
					break;
				}
				case SPN_UPVAL_OUTER: {
					/* upvalue is in the closure of enclosing function */
					SpnValue upval;
					assert(enclosing_fn->upvalues);
					upval = spn_array_get(enclosing_fn->upvalues, upval_index);
					spn_array_push(closure->upvalues, &upval);
					break;
				}
				default:
					SHANT_BE_REACHED();
				}
			}

			/* relinquish ownership of the prototype function of
			 * the closure object as it is not needed anymore
			 */
			spn_object_release(prototype);

			break;
		}
		case SPN_INS_LDUPVAL: {
			int reg_index   = OPA(ins);
			int upval_index = OPB(ins);

			/* grab a reference to the currently executing function */
			SpnFunction *current_fn = vm->sp[IDX_FRMHDR].h.callee;

			/* release previous content of 'reg_index'-th register */
			SpnValue *reg = VALPTR(vm->sp, reg_index);
			spn_value_release(reg);

			/* store upvalue into register and retain it */
			*reg = spn_array_get(current_fn->upvalues, upval_index);
			spn_value_retain(reg);

			break;
		}
		case SPN_INS_METHOD: {
			SpnValue *a = VALPTR(vm->sp, OPA(ins)); /* result         */
			SpnValue *b = VALPTR(vm->sp, OPB(ins)); /* object, 'self' */
			SpnValue *c = VALPTR(vm->sp, OPC(ins)); /* method name    */
			SpnValue tmp;
			const void *args[1]; /* for error reporting */

			/* lookup_member returns true if 'pself' is a hashtable or
			 * if it has a class. In this case, 'result' will contain
			 * the value for 'name'. (it may not be a function, in
			 * which case, INS_CALL will throw an error anyway.
			 */
			if (lookup_member(vm, &tmp, b, c)) {
				spn_value_retain(&tmp);
				spn_value_release(a);
				*a = tmp;
				break;
			}

			args[0] = spn_type_name(b->type);
			runtime_error(vm, ip - 1, "object of type %s has no class", args);
			return -1;
		}
		case SPN_INS_PROPGET: {
			SpnValue *result = VALPTR(vm->sp, OPA(ins));
			SpnValue *pself  = VALPTR(vm->sp, OPB(ins));
			SpnValue *prname = VALPTR(vm->sp, OPC(ins));

			SpnValue accval, gval, tmp; /* accessors; getter; and temporary */
			const void *args[2]; /* for error reporting */

			assert(isstring(prname));

			/* if this is a special property, treat it as such */
			if (get_builtin_property(result, pself, prname)) {
				break;
			}

			if (lookup_member(vm, &accval, pself, prname)) {
				if (ishashmap(&accval)) {
					SpnHashMap *accessors = hashmapvalue(&accval);
					gval = spn_hashmap_get(accessors, &vm->getname);

					if (isfunc(&gval)) {
						/* copy the arguments because calling the getter may
						 * reallocate the stack, invalidating all pointers
						 */
						SpnFunction *getter = funcvalue(&gval);
						SpnValue gargv[2], grv;
						gargv[0] = *pself;
						gargv[1] = *prname;

						if (spn_vm_callfunc(vm, getter, &grv, COUNT(gargv), gargv) != 0) {
							return -1;
						}

						result = VALPTR(vm->sp, OPA(ins));
						spn_value_release(result);
						*result = grv;

						break; /* break out if getter was called! */
					}
				}
			}

			/* else if self is a hashmap, fall back to raw indexing getter.
			 * We can use the original pointers here, since if control
			 * flow reached this point, that means that no getter has
			 * been found, consequently no function could be called.
			 */
			if (ishashmap(pself) && lookup_member(vm, &tmp, pself, prname)) {
				spn_value_retain(&tmp);
				spn_value_release(result);
				*result = tmp;
				break;
			}

			/* at this point, the value had neither a class nor an
			 * appropriate getter function, and it's not a hashmap
			 */
			args[0] = spn_type_name(pself->type);
			args[1] = stringvalue(prname)->cstr;
			runtime_error(vm, ip - 1, "value of type %s has no getter for property '%s'", args);
			return -1;
		}
		case SPN_INS_PROPSET: {
			SpnValue *pself  = VALPTR(vm->sp, OPA(ins)); /* object, 'self' */
			SpnValue *prname = VALPTR(vm->sp, OPB(ins)); /* property name  */
			SpnValue *newval = VALPTR(vm->sp, OPC(ins)); /* new value      */

			SpnValue accval, sval; /* hashmap of accessors; and setter */
			const void *args[2]; /* for error reporting */

			assert(isstring(prname));

			if (lookup_member(vm, &accval, pself, prname)) {
				if (ishashmap(&accval)) {
					SpnHashMap *accessors = hashmapvalue(&accval);
					sval = spn_hashmap_get(accessors, &vm->setname);

					if (isfunc(&sval)) {
						SpnFunction *setter = funcvalue(&sval);
						SpnValue sargv[3];
						/* again, copy arguments */
						sargv[0] = *pself;
						sargv[1] = *newval; /* reverse order! value first, */
						sargv[2] = *prname; /* and only then comes the key */

						/* call setter, ignore its return value */
						if (spn_vm_callfunc(vm, setter, NULL, COUNT(sargv), sargv) != 0) {
							return -1;
						}

						break; /* nothing to do if setter was called successfully */
					}
				}
			}

			/* if there's no setter, try raw hashmap indexing */
			if (ishashmap(pself)) {
				/* we don't need to use indexing_hashmap_check,
				 * since the key is always a string (so not NaN or 'nil')
				 */
				spn_hashmap_set(hashmapvalue(pself), prname, newval);
				break;
			}

			/* if 'self' is not a hashmap, though, there's no more hope */
			args[0] = spn_type_name(pself->type);
			args[1] = stringvalue(prname)->cstr;
			runtime_error(vm, ip - 1, "value of type %s has no setter for property '%s'", args);
			return -1;
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

static void read_local_symtab(SpnFunction *program)
{
	spn_uword *bc = program->repr.bc;

	/* read the offset and number of symbols from bytecode */
	ptrdiff_t offset = bc[SPN_FUNCHDR_IDX_BODYLEN];
	size_t symcount = bc[SPN_FUNCHDR_IDX_SYMCNT];
	size_t i;
	spn_uword *stp = bc + SPN_FUNCHDR_LEN + offset;

	/* can only read the symtab of a top-level program */
	assert(program->topprg);

	/* we only read the symbol table if it hasn't been read yet before */
	if (program->readsymtab) {
		return;
	}

	/* indicate that it has been read and proceed parsing */
	program->readsymtab = 1;


	/* if we got here, the bytecode file is being run for the
	 * first time, so we proceed with reading its symbol table.
	 */

	/* then actually read the symbols */
	for (i = 0; i < symcount; i++) {
		spn_uword ins = *stp++;
		enum spn_local_symbol sym = OPCODE(ins);

		switch (sym) {
		case SPN_LOCSYM_STRCONST: {
			const char *cstr = (const char *)(stp);
			SpnValue strval;

			size_t len = OPLONG(ins);
			size_t nwords = ROUNDUP(len + 1, sizeof(spn_uword));

#ifndef NDEBUG
			/* sanity check: the string length recorded in the
			 * bytecode and the actual length
			 * reported by 'strlen()' shall match.
			 */

			size_t reallen = strlen(cstr);
			assert(len == reallen);
#endif

			strval = makestring_nocopy_len(cstr, len, 0);
			spn_array_push(program->symtab, &strval);
			spn_value_release(&strval);

			stp += nwords;
			break;
		}
		case SPN_LOCSYM_SYMSTUB: {
			const char *symname = (const char *)(stp);

			/* lenght of symbol name */
			size_t len = OPLONG(ins);

			/* +1: terminating NUL character */
			size_t nwords = ROUNDUP(len + 1, sizeof(spn_uword));

			SpnValue symval;

#ifndef NDEBUG
			/* sanity check: the symbol name length recorded in
			 * the bytecode and the actual length
			 * reported by 'strlen()' shall match.
			 */

			size_t reallen = strlen(symname);
			assert(len == reallen);
#endif

			/* we don't know the type of the symbol yet */
			symval = make_symstub(symname);
			spn_array_push(program->symtab, &symval);
			spn_value_release(&symval);

			stp += nwords;
			break;
		}
		case SPN_LOCSYM_FUNCDEF: {
			spn_uword hdroff = *stp++;
			spn_uword *entry = bc + hdroff;

			size_t namelen = *stp++;
			const char *name = (const char *)(stp);
			size_t nwords = ROUNDUP(namelen + 1, sizeof(spn_uword));

			SpnValue fnval;

#ifndef NDEBUG
			/* some more sanity check */
			size_t reallen = strlen(name);
			assert(namelen == reallen);
#endif

			fnval = makescriptfunc(name, entry, program);

			/* functions are implemented in the same translation
			 * unit in which their local symtab entry is defined.
			 * So we *can* (and should) fill in the 'env'
			 * member of the SpnValue while reading the symtab.
			 */

			spn_array_push(program->symtab, &fnval);
			spn_value_release(&fnval);

			/* skip function name */
			stp += nwords;

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
	default: SHANT_BE_REACHED();
	}

	return -1;
}

static SpnValue arith_op(const SpnValue *lhs, const SpnValue *rhs, int op)
{
	assert(isnum(lhs) && isnum(rhs));

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
	default: SHANT_BE_REACHED();
	}

	return -1;
}

static int resolve_symbol(SpnVMachine *vm, spn_uword *ip, SpnValue *symp)
{
	SpnValue res;
	SymbolStub *symstub;

	assert(is_symstub(symp));

	symstub = symstubvalue(symp);
	res = spn_hashmap_get_strkey(vm->glbsymtab, symstub->name);

	if (isnil(&res)) {
		const void *args[1];
		args[0] = symstub->name;
		runtime_error(
			vm,
			ip,
			"global '%s' does not exist or it is nil",
			args
		);
		return -1;
	}

	/* if the resolution succeeded, then we
	 * cache the symbol into the appropriate
	 * local symbol table so that we don't
	 * need to resolve it anymore.
	 */
	*symp = res;

	return 0;
}

/* indexing getters and setters */
static int indexing_array_check(SpnVMachine *vm, spn_uword *ip, SpnValue *varr, SpnValue *vidx)
{
	SpnArray *arr;
	long index, length;

	assert(isarray(varr));

	if (!isint(vidx)) {
		const void *args[1];
		args[0] = spn_type_name(vidx->type);
		runtime_error(vm, ip, "indexing array with non-integer value of type %s", args);
		return -1;
	}

	arr = arrayvalue(varr);
	index = intvalue(vidx);
	length = spn_array_count(arr);

	if (index < 0 || index >= length) {
		const void *args[2];
		args[0] = &index;
		args[1] = &length;
		runtime_error(vm, ip, "index %d is out of bounds for array of size %d", args);
		return -1;
	}

	return 0;
}

static int indexing_string_check(SpnVMachine *vm, spn_uword *ip, SpnValue *vstr, SpnValue *vidx)
{
	SpnString *str;
	long index, length;

	assert(isstring(vstr));

	if (!isint(vidx)) {
		const void *args[1];
		args[0] = spn_type_name(vidx->type);
		runtime_error(vm, ip, "indexing string with non-integer value of type %s", args);
		return -1;
	}

	str = stringvalue(vstr);
	index = intvalue(vidx);
	length = str->len;

	if (index < 0 || index >= length) {
		const void *args[2];
		args[0] = &index;
		args[1] = &length;
		runtime_error(vm, ip, "index %d is out of bounds for string of size %d", args);
		return -1;
	}

	return 0;
}

static int indexing_hashmap_check(SpnVMachine *vm, spn_uword *ip, SpnValue *vidx)
{
	/* NaN != NaN, so it can't be used as a key in a hashmap */
	if (isfloat(vidx) && floatvalue(vidx) != floatvalue(vidx)) {
		runtime_error(vm, ip, "hashmap index cannot be NaN", NULL);
		return -1;
	}

	/* a nil key cannot be found by e. g. "keys()",
	 * so it could cause inconsistency -> disallowed
	 */
	if (isnil(vidx)) {
		runtime_error(vm, ip, "hashmap index cannot be nil", NULL);
		return -1;
	}

	return 0;
}

/* XXX: this function *MUST NOT* use 'spn_vm_callfunc()' or
 * reallocate the call stack in any way, since it is passed
 * pointers pointing straight within the active stack frame!
 *
 * XXX: So, if we ever implement destructors, the calls to the
 * 'spn_value_release()' function will potentially call the
 * destructor of whatever originally was in 'dstreg', and
 * that can invalidate the stack, so this will no longer work.
 */
static int get_builtin_property(SpnValue *dstreg, SpnValue *pself, SpnValue *nameval)
{
	const char *name = stringvalue(nameval)->cstr;

	switch (valtype(pself)) {
	case SPN_TTAG_STRING: {
		if (strcmp(name, "length") == 0) {
			size_t length = stringvalue(pself)->len;
			spn_value_release(dstreg);
			*dstreg = makeint(length);
			return 1;
		}

		break;
	}
	case SPN_TTAG_ARRAY: {
		if (strcmp(name, "length") == 0) {
			SpnArray *arr = arrayvalue(pself);
			size_t length = spn_array_count(arr);
			spn_value_release(dstreg);
			*dstreg = makeint(length);
			return 1;
		}

		break;
	}
	case SPN_TTAG_HASHMAP: {
		if (strcmp(name, "length") == 0) {
			SpnHashMap *hm = hashmapvalue(pself);
			size_t length = spn_hashmap_count(hm);
			spn_value_release(dstreg);
			*dstreg = makeint(length);
			return 1;
		}

		break;
	}
	default:
		break;
	}

	return 0;
}

/* this returns 1 if it was able to find the method.
 * 'root' is passed by-value so that it can safely
 * be overwritten while searching the object chain.
 */
static int lookup_member_chained(SpnVMachine *vm, SpnValue *result, SpnValue root, SpnValue *name)
{
	assert(ishashmap(&root));

	do {
		SpnHashMap *roothm = hashmapvalue(&root);
		SpnValue tmp = spn_hashmap_get(roothm, name);

		/* found something non-nil? return it! */
		if (notnil(&tmp)) {
			*result = tmp;
			return 1;
		}

		/* else, try super object/class */
		root = spn_hashmap_get(roothm, &vm->supername);
	} while (ishashmap(&root));

	/* not found anywhere in the chain */
	return 0;
}

static int lookup_member(SpnVMachine *vm, SpnValue *result, SpnValue *pself, SpnValue *name)
{
	SpnValue root; /* member lookup starts here */
	int typetag = valtype(pself);
	SpnValue tagval = makeint(typetag);

	assert(isstring(name));

	switch (typetag) {
	case SPN_TTAG_HASHMAP:
		/* hashmap methods are looked up in the hashmap object itself */
		root = *pself;
		break;
	case SPN_TTAG_USERINFO:
		/* user info values have a per-instance class lookup mechanism */
		root = spn_hashmap_get(vm->classes, pself);
		break;
	default:
		/* and other values share a per-type class descriptor */
		root = spn_hashmap_get(vm->classes, &tagval);
		break;
	}

	/* user info instance or primitive type has no class */
	if (!ishashmap(&root)) {
		return 0;
	}

	/* search chain of objects/classes for the method */
	if (lookup_member_chained(vm, result, root, name)) {
		return 1;
	}

	/* method wasn't found in whole object/class chain; as a last resort,
	 * and only if it is a hashmap, try looking it up in the hashmap class
	 */
	if (ishashmap(pself)) {
		root = spn_hashmap_get(vm->classes, &tagval);

		/* if hashmaps have a class (this is normally the case), _and_ the
		 * class or any of the superclasses thereof contains the method,
		 * then return it. Else just fall through and return nil.
		 */
		if (ishashmap(&root) && lookup_member_chained(vm, result, root, name)) {
			return 1;
		}
	}

	/* method was not found at all, return nil */
	*result = spn_nilval;
	return 1;
}

static SpnValue typeof_value(SpnValue *val)
{
	const char *type = spn_type_name(val->type);
	return makestring_nocopy(type);
}
