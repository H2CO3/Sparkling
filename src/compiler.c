/*
 * compiler.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * AST to bytecode compiler
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "compiler.h"
#include "vm.h"
#include "array.h"
#include "private.h"


typedef struct TBytecode {
	spn_uword	*insns;
	size_t		 len;
	size_t		 allocsz;
} TBytecode;

/* bidirectional hash table: maps indices to values and values to indices */
typedef struct RoundTripStore {
	SpnArray *fwd;	/* forward mapping		*/
	SpnArray *inv;	/* inverse mapping		*/
	int maxsize;	/* maximal size during lifetime	*/
} RoundTripStore;

/* if you ever add a member to this structure, consider adding it to the
 * scope context info as well if necessary (and extend the `save_scope()` and
 * `restore_scope()` functions accordingly).
 */
struct SpnCompiler {
	TBytecode	 bc;
	char		*errmsg;
	int		 tmpidx;	/* (I)		*/
	int		 nregs;		/* (II)		*/
	RoundTripStore	*symtab;	/* (III)	*/
	RoundTripStore	*varstack;	/* (IV)		*/
};

/* Remarks:
 * 
 * (I): the index of the top of the temporary "stack". only registers with
 * an index greater than this number may be used as temporaries. (this value
 * may change within a single expression (as we walk different levels of an
 * expression AST) as well as across statements (when a new variable is
 * declared between two statements).
 * 
 * (II): number of registers maximally needed. This counter is only set if
 * an expression that needs temporaries is ever compiled. (this is why we
 * check if the number of local variables is greater than this number --
 * see the remark in the `compile_funcdef()` function.)
 * 
 * (III) - (IV): array of local symbols and stack of global and local variable
 * names and the corresponding register indices
 */

/* information describing the state of the global scope or a function scope.
 * this has to be preserved (saved and restored) across the compilation of
 * function bodies.
 */
typedef struct ScopeInfo {
	int		 tmpidx;
	int		 nregs;
	RoundTripStore	*varstack;
} ScopeInfo;

static void save_scope(SpnCompiler *cmp, ScopeInfo *sci)
{
	sci->tmpidx = cmp->tmpidx;
	sci->nregs = cmp->nregs;
	sci->varstack = cmp->varstack;
}

static void restore_scope(SpnCompiler *cmp, ScopeInfo *sci)
{
	cmp->tmpidx = sci->tmpidx;
	cmp->nregs = sci->nregs;
	cmp->varstack = sci->varstack;
}

/* compile_*() functions return nonzero on success, 0 on error */
static int compile(SpnCompiler *cmp, SpnAST *ast);

static int compile_program(SpnCompiler *cmp, SpnAST *ast);
static int compile_compound(SpnCompiler *cmp, SpnAST *ast);
static int compile_block(SpnCompiler *cmp, SpnAST *ast);
static int compile_funcdef(SpnCompiler *cmp, SpnAST *ast, int *symidx);
static int compile_while(SpnCompiler *cmp, SpnAST *ast);
static int compile_do(SpnCompiler *cmp, SpnAST *ast);
static int compile_for(SpnCompiler *cmp, SpnAST *ast);
static int compile_foreach(SpnCompiler *cmp, SpnAST *ast);
static int compile_if(SpnCompiler *cmp, SpnAST *ast);

static int compile_break(SpnCompiler *cmp, SpnAST *ast);
static int compile_continue(SpnCompiler *cmp, SpnAST *ast);
static int compile_vardecl(SpnCompiler *cmp, SpnAST *ast);
static int compile_return(SpnCompiler *cmp, SpnAST *ast);

/* compile and load string literal */
static void compile_string_literal(SpnCompiler *cmp, SpnValue *str, int *dst);

/* `dst` is a pointer to `int` that will be filled with the index of the
 * destination register (i. e. the one holding the result of the expression)
 * pass `NULL` if you don't need this information (e. g. when an expression
 * is merely evaluated for its side effects)
 */
static int compile_expr_toplevel(SpnCompiler *cmp, SpnAST *ast, int *dst);

/* dst is the preferred destination register index. Pass a pointer to
 * a non-negative `int` to force the function to emit an instruction
 * of which the destination register is `*dst`. If the integer pointed
 * to by `dst` is initially negative, then the function decides which
 * register to use as the destination, then sets `*dst` accordingly.
 */
static int compile_expr(SpnCompiler *cmp, SpnAST *ast, int *dst);

/* takes a printf-like format string */
static void compiler_error(SpnCompiler *cmp, unsigned long lineno, const char *fmt, const void *args[]);

/* quick and dirty integer maximum function */
static int max(int x, int y)
{
	return x > y ? x : y;
}

/* these functions execute "push" and "pop" operations on the operand stack
 * formed by taking the temporary registers (those within the range [0...argc),
 * where argc is the number of declared arguments -- always zero at global
 * program scope)
 * These functions also keep track of the maximal number of necessary temporary
 * regsisters.
 */
static int tmp_push(SpnCompiler *cmp)
{
	int idx = cmp->tmpidx++;

	if (cmp->tmpidx > cmp->nregs) {
		cmp->nregs = cmp->tmpidx;
	}

	return idx;
}

static void tmp_pop(SpnCompiler *cmp)
{
	cmp->tmpidx--;
}

/* raw bytecode manipulation */
static void bytecode_init(TBytecode *bc);
static void bytecode_append(TBytecode *bc, spn_uword *words, size_t n);
static void append_cstring(TBytecode *bc, const char *str, size_t len);

/* managing round-trip stores */
static void rts_init(RoundTripStore *rts);
static int rts_add(RoundTripStore *rts, SpnValue *val);
static SpnValue *rts_getval(RoundTripStore *rts, int idx); /* returns nil if not found */
static int rts_getidx(RoundTripStore *rts, SpnValue *val); /* returns < 0 if not found */
static int rts_count(RoundTripStore *rts);
static void rts_delete_top(RoundTripStore *rts, int newsize);
static void rts_free(RoundTripStore *rts);

SpnCompiler *spn_compiler_new()
{
	SpnCompiler *cmp = malloc(sizeof(*cmp));
	if (cmp == NULL) {
		abort();
	}

	cmp->errmsg = NULL;

	return cmp;
}

void spn_compiler_free(SpnCompiler *cmp)
{
	free(cmp->errmsg);
	free(cmp);
}

spn_uword *spn_compiler_compile(SpnCompiler *cmp, SpnAST *ast, size_t *sz)
{
	bytecode_init(&cmp->bc);

	/* compile program */
	if (compile_program(cmp, ast)) { /* success */
		if (sz != NULL) {
			*sz = cmp->bc.len;
		}

		return cmp->bc.insns;
	}

	/* error */
	free(cmp->bc.insns);
	return NULL;
}

const char *spn_compiler_errmsg(SpnCompiler *cmp)
{
	return cmp->errmsg;
}

/* bytecode manipulation */
static void bytecode_init(TBytecode *bc)
{
	bc->insns = NULL;
	bc->len = 0;
	bc->allocsz = 0;
}

static void bytecode_append(TBytecode *bc, spn_uword *words, size_t n)
{
	if (bc->allocsz < bc->len + n) {
		if (bc->allocsz == 0) {
			bc->allocsz = 0x40;
		}

		while (bc->allocsz < bc->len + n) {
			bc->allocsz <<= 1;
		}

		bc->insns = realloc(bc->insns, bc->allocsz * sizeof(bc->insns[0]));
		if (bc->insns == NULL) {
			abort();
		}
	}

	memcpy(bc->insns + bc->len, words, n * sizeof(bc->insns[0]));
	bc->len += n;
}

/* this function appends a 0-terminated array of characters to the bytecode
 * in the compiler. The number of characters (including the NUL terminator) is
 * of course padded with zeroes to the nearest multiple of sizeof(spn_uword).
 */
static void append_cstring(TBytecode *bc, const char *str, size_t len)
{
	size_t nwords = ROUNDUP(len + 1, sizeof(spn_uword));
	size_t padded_len = nwords * sizeof(spn_uword);

	spn_uword *buf = malloc(padded_len);
	if (buf == NULL) {
		abort();
	}

	/* this relies on the fact that `strncpy()` pads with NUL characters
	 * when strlen(src) < sizeof(dest)
	 */
	strncpy((char *)(buf), str, padded_len);

	bytecode_append(bc, buf, nwords);
	free(buf);
}


/* round-trip stores */
static void rts_init(RoundTripStore *rts)
{
	rts->fwd = spn_array_new();
	rts->inv = spn_array_new();
	rts->maxsize = 0;
}

static int rts_add(RoundTripStore *rts, SpnValue *val)
{
	int idx = rts_count(rts);
	int newsize;

	/* insert element into last place */
	SpnValue idxval;
	idxval.t = SPN_TYPE_NUMBER;
	idxval.f = 0;
	idxval.v.intv = idx;

	spn_array_set(rts->fwd, &idxval, val);
	spn_array_set(rts->inv, val, &idxval);

	/* keep track of maximal size of the RTS */
	newsize = rts_count(rts);
	if (newsize > rts->maxsize) {
		rts->maxsize = newsize;
	}

	return idx;
}

static SpnValue *rts_getval(RoundTripStore *rts, int idx)
{
	SpnValue idxval;
	idxval.t = SPN_TYPE_NUMBER;
	idxval.f = 0;
	idxval.v.intv = idx;

	return spn_array_get(rts->fwd, &idxval);
}

static int rts_getidx(RoundTripStore *rts, SpnValue *val)
{
	SpnValue *res = spn_array_get(rts->inv, val);
	return res->t == SPN_TYPE_NUMBER ? res->v.intv : -1;
}

static int rts_count(RoundTripStore *rts)
{
	unsigned long n = spn_array_count(rts->fwd);

	/* petit sanity check */
	assert(n == spn_array_count(rts->inv));

	return n;
}

/* removes the last elements so that only the first `newsize` ones remain */
static void rts_delete_top(RoundTripStore *rts, int newsize)
{
	int oldsize = rts_count(rts);
	int i;

	/* because nothing else would make sense */
	assert(newsize <= oldsize);

	for (i = newsize; i < oldsize; i++) {
		SpnValue val, *valp;

		SpnValue idx;
		idx.t = SPN_TYPE_NUMBER;
		idx.f = 0;
		idx.v.intv = i;

		valp = spn_array_get(rts->fwd, &idx);
		assert(valp->t != SPN_TYPE_NIL);

		/* if I used a pointer, then `spn_array_remove(rts->fwd, &idx)`
		 * would set its type to nil (I know, screw me), so we need to
		 * make a copy of the value struct (reference counts stay
		 * correct, though)
		 */
		val = *valp;

		spn_array_remove(rts->fwd, &idx);		
		spn_array_remove(rts->inv, &val);
	}

	assert(rts_count(rts) == newsize);
}

static void rts_free(RoundTripStore *rts)
{
	spn_object_release(rts->fwd);
	spn_object_release(rts->inv);
}


static void compiler_error(SpnCompiler *cmp, unsigned long lineno, const char *fmt, const void *args[])
{
	char *prefix, *msg;
	size_t prefix_len, msg_len;
	const void *prefix_args[1];
	prefix_args[0] = &lineno;

	prefix = spn_string_format(
		"Sparkling: semantic error near line %u: ",
		&prefix_len,
		prefix_args,
		0
	);

	msg = spn_string_format(fmt, &msg_len, args, 0);

	free(cmp->errmsg);
	cmp->errmsg = malloc(prefix_len + msg_len + 1);

	if (cmp->errmsg == NULL) {
		abort();
	}

	strcpy(cmp->errmsg, prefix);
	strcpy(cmp->errmsg + prefix_len, msg);

	free(prefix);
	free(msg);
}

/* this assumes an expression statement if the node is an expression,
 * so it doesn't return the destination register index. DO NOT use this
 * if the resut of an expression shall be known.
 */
static int compile(SpnCompiler *cmp, SpnAST *ast)
{
	/* compiling an empty tree always succeeds silently */
	if (ast == NULL) {
		return 1;
	}

	switch (ast->node) {
	case SPN_NODE_COMPOUND:	return compile_compound(cmp, ast);
	case SPN_NODE_BLOCK:	return compile_block(cmp, ast);
	case SPN_NODE_FUNCSTMT:	return compile_funcdef(cmp, ast, NULL);
	case SPN_NODE_WHILE:	return compile_while(cmp, ast);
	case SPN_NODE_DO:	return compile_do(cmp, ast);
	case SPN_NODE_FOR:	return compile_for(cmp, ast);
	case SPN_NODE_FOREACH:	return compile_foreach(cmp, ast);
	case SPN_NODE_IF:	return compile_if(cmp, ast);
	case SPN_NODE_BREAK:	return compile_break(cmp, ast);
	case SPN_NODE_CONTINUE:	return compile_continue(cmp, ast);
	case SPN_NODE_RETURN:	return compile_return(cmp, ast);
	case SPN_NODE_EMPTY:	return 1; /* no compile_empty() for you ;-) */
	case SPN_NODE_VARDECL:	return compile_vardecl(cmp, ast);
	default:		return compile_expr_toplevel(cmp, ast, NULL);
	}
}

/* returns zero on success, nonzero on error */
static int write_symtab(SpnCompiler *cmp)
{
	int i, nsyms = rts_count(cmp->symtab);

	for (i = 0; i < nsyms; i++) {
		SpnValue *sym = rts_getval(cmp->symtab, i);

		switch (sym->t) {
		case SPN_TYPE_STRING: {
			/* string literal */

			SpnString *str = sym->v.ptrv;

			/* append symbol type and length description */
			spn_uword ins = SPN_MKINS_LONG(SPN_LOCSYM_STRCONST, str->len);
			bytecode_append(&cmp->bc, &ins, 1);

			/* append actual 0-terminated string */
			append_cstring(&cmp->bc, str->cstr, str->len);
			break;
		}
		case SPN_TYPE_FUNC: {
			/* unresolved function stub */

			size_t namelen = strlen(sym->v.fnv.name);

			/* append symbol type */
			spn_uword ins = SPN_MKINS_LONG(SPN_LOCSYM_FUNCSTUB, namelen);
			bytecode_append(&cmp->bc, &ins, 1);

			/* append function name */
			append_cstring(&cmp->bc, sym->v.fnv.name, namelen);
			break;
		}
		case SPN_TYPE_NUMBER: {
			/* Yes, I do realize this is odd - storing a function
			 * as a number... But a lambda function is represented
			 * only by the offset of its header (counting from the
			 * beginning of the whole bytecode). However, the
			 * `SpnValue.fnv.r` union does not have a member which
			 * could store that integer. That is intentional: at
			 * runtime, the VM is only concerned about pointers -
			 * I could add the aforementioned field, but I thought
			 * it would clutter the function representation -
			 * conceptually, SpnValue is a runtime thing, it must
			 * not contain private compiler data.
			 * So, `sym->v.intv` stores this offset as a `long`.
			 */
			spn_uword ins = SPN_MKINS_LONG(SPN_LOCSYM_LAMBDA, sym->v.intv);
			bytecode_append(&cmp->bc, &ins, 1);
			break;
		}
		default:
			{
				/* got something that's not supposed to be there */
				int st = sym->t;
				const void *args[1];
				args[0] = &st;
				compiler_error(cmp, 0, "wrong symbol type %i in write_symtab()", args);
				return -1;
			}
		}
	}

	return 0;
}

static void append_return_nil(SpnCompiler *cmp)
{
	/* load nil into register 0 and return it */
	spn_uword ins[2] = {
		SPN_MKINS_AB(SPN_INS_LDCONST, 0, SPN_CONST_NIL),
		SPN_MKINS_A(SPN_INS_RET, 0)
	};
	bytecode_append(&cmp->bc, ins, COUNT(ins));	

	/* set register count to 1 if it was 0, since, although a function or
	 * a program might not need any registers at all in itself,
	 * for returning nil, we still need at least register #0.
	 */
	if (cmp->nregs < 1) {
		cmp->nregs = 1;
	}
}

static int compile_program(SpnCompiler *cmp, SpnAST *ast)
{
	int regcnt;
	RoundTripStore symtab, glbvars;

	/* append stub program header (just make room for it) */
	spn_uword header[SPN_PRGHDR_LEN] = { 0 };
	bytecode_append(&cmp->bc, header, SPN_PRGHDR_LEN);

	/* set up variable table and local symbol table */
	rts_init(&symtab);
	rts_init(&glbvars);
	cmp->symtab = &symtab;
	cmp->varstack = &glbvars;

	/* set up the maximal number of registers needed at global scope */
	cmp->nregs = 0;

	/* compile children */
	if (compile(cmp, ast->left) == 0 || compile(cmp, ast->right) == 0) {
		/* on error, clean up and return error */
		rts_free(&symtab);
		rts_free(&glbvars);
		return 0;
	}

	/* unconditionally append `return nil;`, just in case */
	append_return_nil(cmp);

	/* since `cmp->nregs` is only set if temporary variables are used at
	 * least once during compilation (i. e. if there's an expression that
	 * needs temporary registers), it may contain zero even if more than
	 * zero registers are necessary (for storing local variables).
	 * So, we pick the maximum of the number of global variables and the
	 * number of registers stored in `cmp->nregs`.
	 */
	regcnt = max(cmp->nregs, cmp->varstack->maxsize);

	assert(regcnt >= 0);
	assert(rts_count(cmp->symtab) >= 0);

	/* now sufficient header info is available - fill in bytecode */
	cmp->bc.insns[SPN_HDRIDX_MAGIC]	    = SPN_MAGIC;
	cmp->bc.insns[SPN_HDRIDX_SYMTABOFF] = cmp->bc.len;
	cmp->bc.insns[SPN_HDRIDX_SYMTABLEN] = rts_count(cmp->symtab);
	cmp->bc.insns[SPN_HDRIDX_FRMSIZE]   = regcnt;

	/* write local symbol table, check for errors */
	if (write_symtab(cmp) != 0) {
		rts_free(&symtab);
		rts_free(&glbvars);
		return 0;
	}

	/* clean up */
	rts_free(&symtab);
	rts_free(&glbvars);
	return 1;
}

static int compile_compound(SpnCompiler *cmp, SpnAST *ast)
{
	return compile(cmp, ast->left) && compile(cmp, ast->right);
}

/* TODO: check here that there are at most 256 entries (variables) */
static int compile_block(SpnCompiler *cmp, SpnAST *ast)
{
	/* block -> new lexical scope, "push" a new set of variable names on
	 * the stack. This is done by keeping track of the current length of
	 * the variable stack, then removing the last names when compilation
	 * of the block is complete, until only the originally present names
	 * remain in the stack.
	 */
	int old_stack_size = rts_count(cmp->varstack);

	/* compile children */
	int success = compile(cmp, ast->left) && compile(cmp, ast->right);

	/* and now remove the variables declared in the scope of this block */
	rts_delete_top(cmp->varstack, old_stack_size);

	return success;
}

/* TODO: should check for there being at most 256 function arguments here */
static int compile_funcdef(SpnCompiler *cmp, SpnAST *ast, int *symidx)
{
	int regcount, argc;
	spn_uword ins, fnhdr[SPN_FUNCHDR_LEN] = { 0 };
	size_t namelen, hdroff, bodylen;
	const char *name; /* must always be non-NULL: append_cstring() dereferences it */
	SpnAST *arg;
	RoundTripStore vs_this;
	ScopeInfo sci;

	/* save scope context data: local variables, temporary register stack
	 * pointer, maximal number of registers, innermost loop offset
	 */
	save_scope(cmp, &sci);

	/* init new local variable stack and register counter */
	rts_init(&vs_this);
	cmp->varstack = &vs_this;
	cmp->nregs = 0;

	/* write VM instruction and function name length to bytecode */
	if (ast->name != NULL) {
		name = ast->name->cstr;
		namelen = ast->name->len;
	} else {
		name = SPN_LAMBDA_NAME;
		namelen = strlen(name);
	}

	ins = SPN_MKINS_LONG(SPN_INS_GLBSYM, namelen);
	bytecode_append(&cmp->bc, &ins, 1);
	append_cstring(&cmp->bc, name, namelen);

	/* save the offset of the function header */
	hdroff = cmp->bc.len;

	/* write stub function header */
	bytecode_append(&cmp->bc, fnhdr, SPN_FUNCHDR_LEN);

	/* if the function is a lambda, create a local symtab entry for it. */
	if (ast->node == SPN_NODE_FUNCEXPR) {
		/* the next index to be used in the local symtab is the length
		 * of the symtab itself (we insert at position 0 when it's
		 * empty, at position 1 if it contains 1 item, etc.)
		 */
		SpnValue offval;
		offval.t = SPN_TYPE_NUMBER;
		offval.f = 0;
		offval.v.intv = hdroff;

		*symidx = rts_add(cmp->symtab, &offval);		
	}

	/* bring function arguments in scope (they are put in the first
	 * `argc' registers), and count them as well
	 */
	for (arg = ast->left; arg != NULL; arg = arg->left) {
		SpnValue argname;
		argname.t = SPN_TYPE_STRING;
		argname.f = SPN_TFLG_OBJECT;
		argname.v.ptrv = arg->name;

		/* check for double declaration of an argument */
		if (rts_getidx(cmp->varstack, &argname) < 0) {
			rts_add(cmp->varstack, &argname);
		} else {
			/* on error, free local var stack and restore scope context */
			const void *args[1];
			args[0] = arg->name->cstr;
			compiler_error(cmp, arg->lineno, "argument `%s' already declared", args);
			rts_free(&vs_this);
			restore_scope(cmp, &sci);
			return 0;
		}
	}

	argc = rts_count(cmp->varstack);

	/* compile body */
	if (compile(cmp, ast->right) == 0) {
		rts_free(&vs_this);
		restore_scope(cmp, &sci);
		return 0;
	}

	/* unconditionally append `return nil;` at the end */
	append_return_nil(cmp);

	/* `max()` is called for the same reason it is called in the
	 * `compile_program()` function (see the explanation there)
	 */
	regcount = max(cmp->nregs, cmp->varstack->maxsize);

	bodylen = cmp->bc.len - (hdroff + SPN_FUNCHDR_LEN);

	/* fill in now-available function header information */
	cmp->bc.insns[hdroff + SPN_FUNCHDR_IDX_BODYLEN]	= bodylen;
	cmp->bc.insns[hdroff + SPN_FUNCHDR_IDX_ARGC]	= argc;
	cmp->bc.insns[hdroff + SPN_FUNCHDR_IDX_NREGS]	= regcount;

	/* free local var stack, restore scope context data */
	rts_free(&vs_this);
	restore_scope(cmp, &sci);

	return 1;
}

static int compile_while(SpnCompiler *cmp, SpnAST *ast)
{
	int cndidx = -1;
	spn_uword ins[2] = { 0 }; /* stub */
	spn_sword off_cond, off_cndjmp, off_body, off_jmpback, off_end;

	off_cond = cmp->bc.len;

	/* compile condition */
	if (compile_expr_toplevel(cmp, ast->left, &cndidx) == 0) {
		return 0;
	}

	off_cndjmp = cmp->bc.len;

	/* append jump over the loop body if condition is false (stub) */
	bytecode_append(&cmp->bc, ins, COUNT(ins));

	off_body = cmp->bc.len;

	/* compile loop body */
	if (compile(cmp, ast->right) == 0) {
		return 0;
	}

	off_jmpback = cmp->bc.len;

	/* compile jump back to the condition */
	bytecode_append(&cmp->bc, ins, COUNT(ins));

	off_end = cmp->bc.len;

	cmp->bc.insns[off_cndjmp + 0] = SPN_MKINS_A(SPN_INS_JZE, cndidx);
	cmp->bc.insns[off_cndjmp + 1] = off_end - off_body;

	cmp->bc.insns[off_jmpback + 0] = SPN_MKINS_VOID(SPN_INS_JMP);
	cmp->bc.insns[off_jmpback + 1] = off_cond - off_end;

	return 1;
}

static int compile_do(SpnCompiler *cmp, SpnAST *ast)
{
	spn_sword off_body = cmp->bc.len;
	spn_sword off_jmp, diff;
	spn_uword ins[2];
	int reg = -1;

	/* compile body */
	if (compile(cmp, ast->right) == 0) {
		return 0;
	}

	/* compile condition */
	if (compile_expr_toplevel(cmp, ast->left, &reg) == 0) {
		return 0;
	}

	off_jmp = cmp->bc.len + COUNT(ins);
	diff = off_body - off_jmp;

	/* jump back to body if condition is true */
	ins[0] = SPN_MKINS_A(SPN_INS_JNZ, reg);
	ins[1] = diff;
	bytecode_append(&cmp->bc, ins, COUNT(ins));

	return 1;
}

static int compile_for(SpnCompiler *cmp, SpnAST *ast)
{
	int regidx = -1;
	spn_sword off_cond, off_body_begin, off_body_end, off_cond_jmp, off_uncd_jmp;
	spn_uword jmpins[2] = { 0 }; /* dummy */
	SpnAST *header, *init, *cond, *icmt;

	/* hand-unrolled loops FTW! */
	header = ast->left;
	init = header->left;
	header = header->right;
	cond = header->left;
	header = header->right;
	icmt = header->left;

	/* compile initialization ouside the loop */
	if (compile_expr_toplevel(cmp, init, NULL) == 0) {
		return 0;
	}

	/* compile condition */
	off_cond = cmp->bc.len;
	if (compile_expr_toplevel(cmp, cond, &regidx) == 0) {
		return 0;
	}

	/* compile "skip body if condition is false" jump */
	off_cond_jmp = cmp->bc.len;
	bytecode_append(&cmp->bc, jmpins, COUNT(jmpins));

	/* compile body and incrementing expression */
	off_body_begin = cmp->bc.len;
	if (compile(cmp, ast->right) == 0) {
		return 0;
	}

	if (compile_expr_toplevel(cmp, icmt, NULL) == 0) {
		return 0;
	}

	/* compile unconditional jump back to the condition */
	off_uncd_jmp = cmp->bc.len;
	bytecode_append(&cmp->bc, jmpins, COUNT(jmpins));

	off_body_end = cmp->bc.len;

	/* fill in stub jump instructions
	 * 1. jump over body if condition not met
	 */
	cmp->bc.insns[off_cond_jmp + 0] = SPN_MKINS_A(SPN_INS_JZE, regidx);
	cmp->bc.insns[off_cond_jmp + 1] = off_body_end - off_body_begin;

	/* 2. always jump back to beginning and check condition */
	cmp->bc.insns[off_uncd_jmp + 0] = SPN_MKINS_VOID(SPN_INS_JMP);
	cmp->bc.insns[off_uncd_jmp + 1] = off_cond - off_body_end;

	return 1;
}

/* Why do I have the feeling this will only be implemented in alpha 2 only? */
static int compile_foreach(SpnCompiler *cmp, SpnAST *ast)
{
	compiler_error(cmp, ast->lineno, "compiling `foreach' is currently unimplemented", NULL);
	return 0;
}

static int compile_if(SpnCompiler *cmp, SpnAST *ast)
{
	spn_sword off_then, off_else, off_jze_b4_then, off_jmp_b4_else;
	spn_sword len_then, len_else;
	spn_uword ins[2] = { 0 };
	int condidx = -1;

	SpnAST *cond = ast->left;
	SpnAST *branches = ast->right;
	SpnAST *br_then = branches->left;
	SpnAST *br_else = branches->right;

	/* sanity check */
	assert(branches->node == SPN_NODE_BRANCHES);

	/* compile condition */
	if (compile_expr_toplevel(cmp, cond, &condidx) == 0) {
		return 0;
	}

	off_jze_b4_then = cmp->bc.len;

	/* append stub "jump if zero" instruction */
	bytecode_append(&cmp->bc, ins, COUNT(ins));

	off_then = cmp->bc.len;

	/* compile "then" branch */
	if (compile(cmp, br_then) == 0) {
		return 0;
	}

	off_jmp_b4_else = cmp->bc.len;

	/* append stub unconditional jump */
	bytecode_append(&cmp->bc, ins, COUNT(ins));

	off_else = cmp->bc.len;

	/* compile "else" branch */
	if (compile(cmp, br_else) == 0) {
		return 0;
	}

	/* complete stub jumps */
	len_then = off_else - off_then;
	len_else = cmp->bc.len - off_else;

	cmp->bc.insns[off_jze_b4_then + 0] = SPN_MKINS_A(SPN_INS_JZE, condidx);
	cmp->bc.insns[off_jze_b4_then + 1] = len_then;

	cmp->bc.insns[off_jmp_b4_else + 0] = SPN_MKINS_VOID(SPN_INS_JMP);
	cmp->bc.insns[off_jmp_b4_else + 1] = len_else;

	return 1;
}

static int compile_break(SpnCompiler *cmp, SpnAST *ast)
{
	compiler_error(cmp, ast->lineno, "compiling `break' is currently unimplemented", NULL);
	return 0;
}

static int compile_continue(SpnCompiler *cmp, SpnAST *ast)
{
	compiler_error(cmp, ast->lineno, "compiling `continue' is currently unimplemented", NULL);
	return 0;
}

/* left child: initializer expression, if any (or NULL)
 * right child: next variable declaration (link list)
 */
static int compile_vardecl(SpnCompiler *cmp, SpnAST *ast)
{
	SpnAST *head = ast;

	while (head != NULL) {
		int idx;

		SpnValue name;
		name.t = SPN_TYPE_STRING;
		name.f = SPN_TFLG_OBJECT;
		name.v.ptrv = head->name;

		/* check for erroneous re-declaration - the name must not yet be
		 * in scope (i. e. in the variable stack)
		 */
		if (rts_getidx(cmp->varstack, &name) >= 0) {
			const void *args[1];
			args[0] = head->name->cstr;
			compiler_error(cmp, head->lineno, "variable `%s' already declared", args);
			return 0;
		}

		/* add identifier to variable stack */
		idx = rts_add(cmp->varstack, &name);

		/* compile initializer expression, if any; if there is no
		 * initializer expression, fill variable with nil
		 */
		if (head->left != NULL) {
			if (compile_expr_toplevel(cmp, head->left, &idx) == 0) {
				return 0;
			}
		} else {
			spn_uword ins = SPN_MKINS_AB(SPN_INS_LDCONST, idx, SPN_CONST_NIL);
			bytecode_append(&cmp->bc, &ins, 1);
		}

		head = head->right;
	}

	return 1;
}

static int compile_return(SpnCompiler *cmp, SpnAST *ast)
{
	/* compile expression (left child) if any; else return nil */
	if (ast->left != NULL) {
		spn_uword ins;

		int dst = -1;
		if (compile_expr_toplevel(cmp, ast->left, &dst) == 0) {
			return 0;
		}

		ins = SPN_MKINS_A(SPN_INS_RET, dst);
		bytecode_append(&cmp->bc, &ins, 1);
	} else {
		append_return_nil(cmp);	
	}

	return 1;
}

/* a "top-level" expression is basically what the C standard calls a
 * "full expression". That is, an expression which is part of an expression
 * statement, or an expression in the condition of an if, while or do-while
 * statement, or an expression which is part of the the header of a for
 * statement or one that is an array-valued expression in a foreach statement.
 */
static int compile_expr_toplevel(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	int reg = dst != NULL ? *dst : -1;

	/* set the lowest index that can be used as a temporary register
	 * to the number of variables (local or global, depending on scope)
	 */
	cmp->tmpidx = rts_count(cmp->varstack);

	/* actually compile expression */
	if (compile_expr(cmp, ast, &reg)) {
		if (dst != NULL) {
			*dst = reg;
		}

		return 1;
	}

	return 0;
}

/* helper function for loading a string literal */
static void compile_string_literal(SpnCompiler *cmp, SpnValue *str, int *dst)
{
	spn_uword ins;
	int idx;

	assert(str->t == SPN_TYPE_STRING);

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* if the string is not in the symtab yet, add it */
	idx = rts_getidx(cmp->symtab, str);
	if (idx < 0) {
		idx = rts_add(cmp->symtab, str);
	}

	/* emit load instruction */
	ins = SPN_MKINS_MID(SPN_INS_LDSYM, *dst, idx);
	bytecode_append(&cmp->bc, &ins, 1);
}

/* simple (non short-circuiting) binary operators: arithmetic, bitwise ops,
 * comparison and equality tests, string concatenation
 */
static int compile_simple_binop(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	spn_uword ins;
	int dst_left, dst_right, nvars;

	/* to silence "may be used uninitialized" warning in release build */
	enum spn_vm_ins opcode = -1;

	switch (ast->node) {
	case SPN_NODE_ADD:	opcode = SPN_INS_ADD;	 break;
	case SPN_NODE_SUB:	opcode = SPN_INS_SUB;	 break;
	case SPN_NODE_MUL:	opcode = SPN_INS_MUL;	 break;
	case SPN_NODE_DIV:	opcode = SPN_INS_DIV;	 break;
	case SPN_NODE_MOD:	opcode = SPN_INS_MOD;	 break;
	case SPN_NODE_BITAND:	opcode = SPN_INS_AND;	 break;
	case SPN_NODE_BITOR:	opcode = SPN_INS_OR;	 break;
	case SPN_NODE_BITXOR:	opcode = SPN_INS_XOR;	 break;
	case SPN_NODE_SHL:	opcode = SPN_INS_SHL;	 break;
	case SPN_NODE_SHR:	opcode = SPN_INS_SHR;	 break;
	case SPN_NODE_EQUAL:	opcode = SPN_INS_EQ;	 break;
	case SPN_NODE_NOTEQ:	opcode = SPN_INS_NE;	 break;
	case SPN_NODE_LESS:	opcode = SPN_INS_LT;	 break;
	case SPN_NODE_LEQ:	opcode = SPN_INS_LE;	 break;
	case SPN_NODE_GREATER:	opcode = SPN_INS_GT;	 break;
	case SPN_NODE_GEQ:	opcode = SPN_INS_GE;	 break;
	case SPN_NODE_CONCAT:	opcode = SPN_INS_CONCAT; break;
	default:		SHANT_BE_REACHED();	 break;
	}

	dst_left  = -1;
	dst_right = -1;
	if (compile_expr(cmp, ast->left,  &dst_left)  == 0
	 || compile_expr(cmp, ast->right, &dst_right) == 0) {
		/* an error occurred */
		return 0;
	}

	nvars = rts_count(cmp->varstack);

	/* if result of LHS went into a temporary, then "pop" */
	if (dst_left >= nvars) {
		tmp_pop(cmp);
	}

	/* if result of RHS went into a temporary, then "pop" */
	if (dst_right >= nvars) {
		tmp_pop(cmp);
	}

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	ins = SPN_MKINS_ABC(opcode, *dst, dst_left, dst_right);
	bytecode_append(&cmp->bc, &ins, 1);
	return 1;
}

static int compile_assignment_var(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	int idx;

	/* get register index of variable using its name */
	SpnValue ident;
	ident.t = SPN_TYPE_STRING;
	ident.f = SPN_TFLG_OBJECT;
	ident.v.ptrv = ast->left->name;

	idx = rts_getidx(cmp->varstack, &ident);
	if (idx < 0) {
		const void *args[1];
		args[0] = ast->left->name->cstr;
		compiler_error(
			cmp,
			ast->left->lineno,
			"variable `%s' is undeclared",
			args
		);
		return 0;
	}

	/* store RHS to the variable register */
	if (compile_expr(cmp, ast->right, &idx) == 0) {
		return 0;
	}

	/* if we are free to choose the destination register, then we
	 * just return the left-hand side, the variable that is being
	 * assigned to. If, however, the destination is determinate,
	 * then we emit a 'move' instruction.
	 */
	if (*dst < 0) {
		*dst = idx;
	} else if (*dst != idx) {
		/* tiny optimization: don't move a register into itself */
		spn_uword ins = SPN_MKINS_AB(SPN_INS_MOV, *dst, idx);
		bytecode_append(&cmp->bc, &ins, 1);
	}

	return 1;
}

static int compile_assignment_array(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	spn_uword ins;
	SpnAST *lhs = ast->left;
	SpnAST *rhs = ast->right;
	int nvars;

	/* array and subscript indices: just like in `compile_arrsub()` */
	int arridx = -1, subidx = -1;

	/* compile right-hand side directly into the destination register,
	 * since the assignment operation needs to yield it anyway
	 * (formally, the operation yields the LHS, but they are the same
	 * in the case of a simple assignment.)
	 */
	if (compile_expr(cmp, rhs, dst) == 0) {
		return 0;
	}

	/* compile array expression */
	if (compile_expr(cmp, lhs->left, &arridx) == 0) {
		return 0;
	}

	/* compile subscript */
	if (lhs->node == SPN_NODE_ARRSUB) {
		/* indexing with brackets */
		if (compile_expr(cmp, lhs->right, &subidx) == 0) {
			return 0;
		}
	} else {
		/* memberof */
		SpnValue nameval;
		nameval.t = SPN_TYPE_STRING;
		nameval.f = SPN_TFLG_OBJECT;
		nameval.v.ptrv = lhs->right->name;
		compile_string_literal(cmp, &nameval, &subidx);
	}

	/* emit "store to array" instruction */
	ins = SPN_MKINS_ABC(SPN_INS_ARRSET, arridx, subidx, *dst);
	bytecode_append(&cmp->bc, &ins, 1);

	/* XXX: is this correct? since we need neither the value of the
	 * array nor the value of the subscripting expression, we can
	 * get rid of them by popping them off the temporary stack.
	 */
	nvars = rts_count(cmp->varstack);

	if (arridx >= nvars) {
		tmp_pop(cmp);
	}

	if (subidx >= nvars) {
		tmp_pop(cmp);
	}

	return 1;
}

/* left child: LHS, right child: RHS */
static int compile_assignment(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	switch (ast->left->node) {
	case SPN_NODE_IDENT:	return compile_assignment_var(cmp, ast, dst);

	case SPN_NODE_ARRSUB:
	case SPN_NODE_MEMBEROF:	return compile_assignment_array(cmp, ast, dst);

	default:
		compiler_error(
			cmp,
			ast->left->lineno,
			"left-hand side of assignment must be\na variable or an array member",
			NULL
		);
		return 0;
	}
}

static int compile_cmpd_assgmt_var(SpnCompiler *cmp, SpnAST *ast, int *dst, enum spn_vm_ins opcode)
{
	int idx, nvars, rhs = -1;
	spn_uword ins;

	/* get register index of variable using its name */
	SpnValue ident;
	ident.t = SPN_TYPE_STRING;
	ident.f = SPN_TFLG_OBJECT;
	ident.v.ptrv = ast->left->name;

	idx = rts_getidx(cmp->varstack, &ident);
	if (idx < 0) {
		const void *args[1];
		args[0] = ast->left->name->cstr;
		compiler_error(
			cmp,
			ast->left->lineno,
			"variable `%s' is undeclared",
			args
		);
		return 0;
	}

	/* evaluate RHS */
	if (compile_expr(cmp, ast->right, &rhs) == 0) {
		return 0;
	}

	/* if RHS went into a temporary register, then pop() */
	nvars = rts_count(cmp->varstack);
	if (rhs >= nvars) {
		tmp_pop(cmp);
	}

	/* emit instruction to operate on LHS and RHS */
	ins = SPN_MKINS_ABC(opcode, idx, idx, rhs);
	bytecode_append(&cmp->bc, &ins, 1);

	/* finally, yield the LHS. (this just means that we set the destination
	 * register to the index of the variable if we can, and we emit a move
	 * instruction if we can't.)
	 */
	if (*dst < 0) {
		*dst = idx;
	} else if (*dst != idx) {
		ins = SPN_MKINS_AB(SPN_INS_MOV, *dst, idx);
		bytecode_append(&cmp->bc, &ins, 1);
	}

	return 1;
}

static int compile_cmpd_assgmt_arr(SpnCompiler *cmp, SpnAST *ast, int *dst, enum spn_vm_ins opcode)
{
	spn_uword ins[3];
	SpnAST *lhs = ast->left;
	SpnAST *rhs = ast->right;

	int nvars, arridx = -1, subidx = -1, rhsidx = -1;

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* compile RHS */
	if (compile_expr(cmp, rhs, &rhsidx) == 0) {
		return 0;
	}

	/* compile array expression */
	if (compile_expr(cmp, lhs->left, &arridx) == 0) {
		return 0;
	}

	/* compile subscript */
	if (lhs->node == SPN_NODE_ARRSUB) {
		/* indexing with brackets */
		if (compile_expr(cmp, lhs->right, &subidx) == 0) {
			return 0;
		}
	} else {
		/* memberof */
		SpnValue nameval;
		nameval.t = SPN_TYPE_STRING;
		nameval.f = SPN_TFLG_OBJECT;
		nameval.v.ptrv = lhs->right->name;
		compile_string_literal(cmp, &nameval, &subidx);
	}

	/* load LHS into destination register */
	ins[0] = SPN_MKINS_ABC(SPN_INS_ARRGET, *dst, arridx, subidx);

	/* evaluate "LHS = LHS <op> RHS" */
	ins[1] = SPN_MKINS_ABC(opcode, *dst, *dst, rhsidx);

	/* store value of updated destination register into array */
	ins[2] = SPN_MKINS_ABC(SPN_INS_ARRSET, arridx, subidx, *dst);

	bytecode_append(&cmp->bc, ins, COUNT(ins));

	/* pop as many times as we used a temporary register (XXX: correct?) */
	nvars = rts_count(cmp->varstack);

	if (rhsidx >= nvars) {
		tmp_pop(cmp);
	}

	if (arridx >= nvars) {
		tmp_pop(cmp);
	}

	if (subidx >= nvars) {
		tmp_pop(cmp);
	}

	return 1;
}

static int compile_compound_assignment(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	enum spn_vm_ins opcode;

	switch (ast->node) {
	case SPN_NODE_ASSIGN_ADD:	opcode = SPN_INS_ADD;	 break;
	case SPN_NODE_ASSIGN_SUB:	opcode = SPN_INS_SUB;	 break;
	case SPN_NODE_ASSIGN_MUL:	opcode = SPN_INS_MUL;	 break;
	case SPN_NODE_ASSIGN_DIV:	opcode = SPN_INS_DIV;	 break;
	case SPN_NODE_ASSIGN_MOD:	opcode = SPN_INS_MOD;	 break;
	case SPN_NODE_ASSIGN_AND:	opcode = SPN_INS_AND;	 break;
	case SPN_NODE_ASSIGN_OR:	opcode = SPN_INS_OR;	 break;
	case SPN_NODE_ASSIGN_XOR:	opcode = SPN_INS_XOR;	 break;
	case SPN_NODE_ASSIGN_SHL:	opcode = SPN_INS_SHL;	 break;
	case SPN_NODE_ASSIGN_SHR:	opcode = SPN_INS_SHR;	 break;
	case SPN_NODE_ASSIGN_CONCAT:	opcode = SPN_INS_CONCAT; break;
	default:			SHANT_BE_REACHED();	 break;
	}

	switch (ast->left->node) {
	case SPN_NODE_IDENT:	return compile_cmpd_assgmt_var(cmp, ast, dst, opcode);

	case SPN_NODE_ARRSUB:
	case SPN_NODE_MEMBEROF:	return compile_cmpd_assgmt_arr(cmp, ast, dst, opcode);

	default:
		compiler_error(
			cmp,
			ast->left->lineno,
			"left-hand side of assignment must be\na variable or an array member",
			NULL
		);
		return 0;
	}
}

static int compile_logical(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	spn_sword off_rhs, off_jump, end_rhs;
	spn_uword movins, jumpins[2] = { 0 }; /* dummy */

	enum spn_vm_ins opcode = ast->node == SPN_NODE_LOGAND ? SPN_INS_JZE : SPN_INS_JNZ;

	/* we can't compile the result directly into the destination register,
	 * because if the destination is a variable which will be examined in
	 * the righ-hand side expression too, we will be in trouble.
	 */
	int idx = tmp_push(cmp);

	/* compile left-hand side */
	if (compile_expr(cmp, ast->left, &idx) == 0) {
		return 0;
	}

	/* if it evaluates to false (AND) or true (OR),
	 * then we short-circuit and yield the LHS
	 */
	off_jump = cmp->bc.len;
	bytecode_append(&cmp->bc, jumpins, COUNT(jumpins));

	off_rhs = cmp->bc.len;

	/* compile right-hand side */
	if (compile_expr(cmp, ast->right, &idx) == 0) {
		return 0;
	}

	end_rhs = cmp->bc.len;

	/* fill in stub jump instruction */
	cmp->bc.insns[off_jump + 0] = SPN_MKINS_A(opcode, idx);
	cmp->bc.insns[off_jump + 1] = end_rhs - off_rhs;

	/* move result into destination, then get rid of temporary */
	movins = SPN_MKINS_AB(SPN_INS_MOV, *dst, idx);
	bytecode_append(&cmp->bc, &movins, 1);
	tmp_pop(cmp);

	return 1;
}

/* ternary conditional
 * left child: condition expression
 * right child: common parent for branches
 *	left child of right child: `then` value
 *	right         - " -      : `else` value
 */
static int compile_condexpr(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	spn_sword off_then, off_else, off_jze_b4_then, off_jmp_b4_else;
	spn_sword len_then, len_else;
	spn_uword ins[2] = { 0 }; /* stub */
	int condidx = -1;

	SpnAST *cond = ast->left;
	SpnAST *branches = ast->right;
	SpnAST *val_then = branches->left;
	SpnAST *val_else = branches->right;

	/* sanity check */
	assert(branches->node == SPN_NODE_BRANCHES);

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* compile condition */
	if (compile_expr(cmp, cond, &condidx) == 0) {
		return 0;
	}

	/* save offset of jump before "then" branch */
	off_jze_b4_then = cmp->bc.len;

	/* append stub JZE instruction */
	bytecode_append(&cmp->bc, ins, COUNT(ins));

	/* save offset of "then" branch */
	off_then = cmp->bc.len;

	/* compile "then" branch */
	if (compile_expr(cmp, val_then, dst) == 0) {
		return 0;
	}

	/* save offset of jump before "else" branch */
	off_jmp_b4_else = cmp->bc.len;

	/* append stub JMP instruction */
	bytecode_append(&cmp->bc, ins, COUNT(ins));

	/* save offset of "else" branch */
	off_else = cmp->bc.len;

	/* compile "else" branch */
	if (compile_expr(cmp, val_else, dst) == 0) {
		return 0;
	}

	/* fill in jump instructions */
	len_then = off_else - off_then;
	len_else = cmp->bc.len - off_else;

	cmp->bc.insns[off_jze_b4_then + 0] = SPN_MKINS_A(SPN_INS_JZE, condidx);
	cmp->bc.insns[off_jze_b4_then + 1] = len_then;

	cmp->bc.insns[off_jmp_b4_else + 0] = SPN_MKINS_VOID(SPN_INS_JMP);
	cmp->bc.insns[off_jmp_b4_else + 1] = len_else;

	return 1;
}

static int compile_ident(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	int idx;
	SpnValue varname;

	varname.t = SPN_TYPE_STRING;
	varname.f = SPN_TFLG_OBJECT;
	varname.v.ptrv = ast->name;

	idx = rts_getidx(cmp->varstack, &varname);

	/* if `rts_getidx()` returns -1, then the variable is undeclared --
	 * assume a global function and search the symtab. If not found,
	 * create a symtab entry.
	 */
	if (idx < 0) {
		spn_uword ins;
		int sym;

		SpnValue stub;
		stub.t = SPN_TYPE_FUNC;
		stub.f = SPN_TFLG_PENDING;
		stub.v.fnv.name = ast->name->cstr;

		sym = rts_getidx(cmp->symtab, &stub);
		if (sym < 0) {
			/* not found, append to symtab */
			sym = rts_add(cmp->symtab, &stub);
		}

		/* compile "load symbol" instruction */
		if (*dst < 0) {
			*dst = tmp_push(cmp);
		}

		ins = SPN_MKINS_MID(SPN_INS_LDSYM, *dst, sym);
		bytecode_append(&cmp->bc, &ins, 1);

		return 1;
	}

	/* if the caller leaves to us where to put the value, then we just
	 * leave it where it currently is and return the index of the variable
	 * register itself. However, if we are requested to put the value
	 * in a specific register, then we have to copy it over.
	 */
	if (*dst < 0) {
		*dst = idx;
	} else if (*dst != idx) {
		spn_uword ins = SPN_MKINS_AB(SPN_INS_MOV, *dst, idx);
		bytecode_append(&cmp->bc, &ins, 1);
	}

	return 1;
}

static int compile_literal(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	switch (ast->value.t) {
	case SPN_TYPE_NIL: {
		spn_uword ins = SPN_MKINS_AB(SPN_INS_LDCONST, *dst, SPN_CONST_NIL);
		bytecode_append(&cmp->bc, &ins, 1);
		break;
	}
	case SPN_TYPE_BOOL: {
		enum spn_const_kind b = ast->value.v.boolv != 0
				      ? SPN_CONST_TRUE
				      : SPN_CONST_FALSE;

		spn_uword ins = SPN_MKINS_AB(SPN_INS_LDCONST, *dst, b);
		bytecode_append(&cmp->bc, &ins, 1);
		break;
	}
	case SPN_TYPE_NUMBER: {
		if (ast->value.f & SPN_TFLG_FLOAT) {
			spn_uword ins[1 + ROUNDUP(sizeof(ast->value.v.fltv), sizeof(spn_uword))] = { 0 };

			ins[0] = SPN_MKINS_AB(SPN_INS_LDCONST, *dst, SPN_CONST_FLOAT);
			memcpy(&ins[1], &ast->value.v.fltv, sizeof(ast->value.v.fltv));

			bytecode_append(&cmp->bc, ins, COUNT(ins));
		} else {
			spn_uword ins[1 + ROUNDUP(sizeof(ast->value.v.intv), sizeof(spn_uword))] = { 0 };

			ins[0] = SPN_MKINS_AB(SPN_INS_LDCONST, *dst, SPN_CONST_INT);
			memcpy(&ins[1], &ast->value.v.intv, sizeof(ast->value.v.intv));

			bytecode_append(&cmp->bc, ins, COUNT(ins));
		}

		break;
	}
	case SPN_TYPE_STRING:
		compile_string_literal(cmp, &ast->value, dst);
		break;
	default:
		SHANT_BE_REACHED();
		return 0;
	}

	return 1;
}

static int compile_funcexpr(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	int symidx;
	spn_uword ins;

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* compile definition of lambda */
	if (compile_funcdef(cmp, ast, &symidx) == 0) {
		return 0;
	}

	/* emit load instruction */
	ins = SPN_MKINS_MID(SPN_INS_LDSYM, *dst, symidx);
	bytecode_append(&cmp->bc, &ins, 1);

	return 1;
}

static int compile_arrsub(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	spn_uword ins;
	int nvars;

	/* array index: register index of the array expression
	 * subscript index: register index of the subscripting expression
	 */
	int arridx = -1, subidx = -1;

	/* compile array expression */
	if (compile_expr(cmp, ast->left, &arridx) == 0) {
		return 0;
	}

	/* compile subscripting expression */
	if (ast->node == SPN_NODE_ARRSUB) {
		/* normal subscripting with brackets */
		if (compile_expr(cmp, ast->right, &subidx) == 0) {
			return 0;
		}
	} else {
		/* memberof, dot/arrow notation */
		SpnValue nameval;
		nameval.t = SPN_TYPE_STRING;
		nameval.f = SPN_TFLG_OBJECT;
		nameval.v.ptrv = ast->right->name;
		compile_string_literal(cmp, &nameval, &subidx);
	}

	/* the usual "pop as many times as we pushed" optimization */
	nvars = rts_count(cmp->varstack);

	if (arridx >= nvars) {
		tmp_pop(cmp);
	}

	if (subidx >= nvars) {
		tmp_pop(cmp);
	}

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* emit "load from array" instruction */
	ins = SPN_MKINS_ABC(SPN_INS_ARRGET, *dst, arridx, subidx);
	bytecode_append(&cmp->bc, &ins, 1);

	return 1;
}

/* XXX: when passed to this function, `*n` should be initialized to zero!
 * On return, it will contain the number of call arguments. `*idc` will
 * point to an array of register indices where the call arguments are stored.
 */
static int compile_callargs(SpnCompiler *cmp, SpnAST *ast, spn_uword **idc, int *argc)
{
	if (ast == NULL) {
		/* last argument in the list (first in the code)
		 * calloc is necessary since we use `|=` to set the indices,
		 * and that doesn't work well with uninitialized integers.
		 */
		*idc = calloc(ROUNDUP(*argc, SPN_WORD_OCTETS), sizeof(**idc));
		if (*argc > 0 && *idc == NULL) {
			/* calloc(0) may return NULL, hence the extra check */
			abort();
		}

		/* reset counter to 0 (innermost argument is the first one) */
		*argc = 0;
	} else {
		int dst = -1;
		int wordidx, shift;

		assert(ast->node == SPN_NODE_CALLARGS);

		/* signal that there's one more argument */
		++*argc;

		if (compile_callargs(cmp, ast->left, idc, argc) == 0) {
			return 0;
		}

		if (compile_expr(cmp, ast->right, &dst) == 0) {
			free(*idc);
			return 0;
		}

		/* TODO: this should be a proper error check instead */
		assert(dst < 256);

		/* fill in the appropriate octet in the bytecode */
		wordidx = *argc / SPN_WORD_OCTETS;
		shift = 8 * (*argc % SPN_WORD_OCTETS);
		(*idc)[wordidx] |= dst << shift;

		/* step over to next register index */
		++*argc;
	}

	return 1;
}

/* left child: function-valued expression
 * right child: link list of call argument expressions
 */
static int compile_call(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	spn_uword *idc = NULL;
	/* 0-initializing `argc` is needed by `compile_callargs()` */
	int argc = 0, fnreg = -1;
	spn_uword ins;

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* load function */
	if (compile_expr(cmp, ast->left, &fnreg) == 0) {
		return 0;
	}

	/* call arguments are in reverse order in the AST, so we can use
	 * recursion to count and evaluate them	
	 */
	if (compile_callargs(cmp, ast->right, &idc, &argc) == 0) {
		return 0;
	}

	/* actually emit call instruction */
	ins = SPN_MKINS_ABC(SPN_INS_CALL, *dst, fnreg, argc);
	bytecode_append(&cmp->bc, &ins, 1);
	bytecode_append(&cmp->bc, idc, ROUNDUP(argc, SPN_WORD_OCTETS));

	/* idc has been assigned to `calloc()`ed memory, so free it */
	free(idc);

	return 1;
}

static int compile_unary(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	spn_uword ins;
	int idx = -1, nvars;

	/* to silence "may be used uninitialized" warning in release mode */
	enum spn_vm_ins opcode = -1;

	switch (ast->node) {
	case SPN_NODE_SIZEOF:	opcode = SPN_INS_SIZEOF; break;
	case SPN_NODE_TYPEOF:	opcode = SPN_INS_TYPEOF; break;
	case SPN_NODE_LOGNOT:	opcode = SPN_INS_LOGNOT; break;
	case SPN_NODE_BITNOT:	opcode = SPN_INS_BITNOT; break;
	case SPN_NODE_NTHARG:	opcode = SPN_INS_NTHARG; break;
	case SPN_NODE_UNMINUS:	opcode = SPN_INS_NEG;	 break;
	default:		SHANT_BE_REACHED();
	}

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	if (compile_expr(cmp, ast->left, &idx) == 0) {
		return 0;
	}

	ins = SPN_MKINS_AB(opcode, *dst, idx);
	bytecode_append(&cmp->bc, &ins, 1);

	nvars = rts_count(cmp->varstack);
	if (idx >= nvars) {
		tmp_pop(cmp);
	}

	return 1;
}

static int compile_unminus(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	/* if the operand is a literal, check if it's actually a number,
	 * and if it is, compile its negated value.
	 */
	SpnAST *op = ast->left;
	if (op->node == SPN_NODE_LITERAL) {
		SpnAST *negated;
		int success;

		if (op->value.t != SPN_TYPE_NUMBER) {
			compiler_error(
				cmp,
				ast->lineno,
				"unary minus applied to non-number literal",
				NULL
			);
			return 0;
		}

		negated = spn_ast_new(SPN_NODE_LITERAL, ast->lineno);

		if (op->value.f & SPN_TFLG_FLOAT) {
			negated->value.t = SPN_TYPE_NUMBER;
			negated->value.f = SPN_TFLG_FLOAT;
			negated->value.v.fltv = -1.0 * op->value.v.fltv;
		} else {
			negated->value.t = SPN_TYPE_NUMBER;
			negated->value.f = 0;
			negated->value.v.intv = -1 * op->value.v.intv;
		}

		success = compile_literal(cmp, negated, dst);
		spn_ast_free(negated);
		return success;
	}

	/* Else fall back to treating it just like all
	 * other prefix unary operators are treated.
	 */
	return compile_unary(cmp, ast, dst);
}

static int compile_incdec(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	int idx;
	SpnValue varname;

	if (ast->left->node != SPN_NODE_IDENT) {
		compiler_error(
			cmp,
			ast->left->lineno,
			"argument of ++ and -- operators must be a variable",
			NULL
		);
		return 0;
	}

	varname.t = SPN_TYPE_STRING;
	varname.f = SPN_TFLG_OBJECT;
	varname.v.ptrv = ast->left->name;
	idx = rts_getidx(cmp->varstack, &varname);
	if (idx < 0) {
		const void *args[1];
		args[0] = ast->left->name->cstr;
		compiler_error(
			cmp,
			ast->left->lineno,
			"variable `%s' is undeclared",
			args
		);
		return 0;
	}

	/* here, case fallthru could avoid a tiny bit of code duplication,
	 * but I hate falling through because it hurts when I land.
	 */
	switch (ast->node) {
	case SPN_NODE_PREINCRMT:
	case SPN_NODE_PREDECRMT: {
		spn_uword ins;
		enum spn_vm_ins opcode = ast->node == SPN_NODE_PREINCRMT
				       ? SPN_INS_INC
				       : SPN_INS_DEC;
		
		/* increment or decrement first */
		ins = SPN_MKINS_A(opcode, idx);
		bytecode_append(&cmp->bc, &ins, 1);

		/* then yield already changed value */
		if (*dst < 0) {
			*dst = idx;
		} else if (*dst != idx) {
			ins = SPN_MKINS_AB(SPN_INS_MOV, *dst, idx);
			bytecode_append(&cmp->bc, &ins, 1);
		}

		break;
	}
	case SPN_NODE_POSTINCRMT:
	case SPN_NODE_POSTDECRMT: {
		spn_uword ins;
		enum spn_vm_ins opcode = ast->node == SPN_NODE_POSTINCRMT
				       ? SPN_INS_INC
				       : SPN_INS_DEC;

		/* first, yield unchanged value */
		if (*dst < 0) {
			*dst = tmp_push(cmp);
			ins = SPN_MKINS_AB(SPN_INS_MOV, *dst, idx);
			bytecode_append(&cmp->bc, &ins, 1);			
		} else if (*dst != idx) {
			ins = SPN_MKINS_AB(SPN_INS_MOV, *dst, idx);
			bytecode_append(&cmp->bc, &ins, 1);
		}

		/* increment/decrement only then */
		ins = SPN_MKINS_A(opcode, idx);
		bytecode_append(&cmp->bc, &ins, 1);

		break;
	}
	default:
		SHANT_BE_REACHED();
	}

	return 1;
}

static int compile_expr(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	switch (ast->node) {
	case SPN_NODE_CONCAT:		/* concat.	*/
	case SPN_NODE_ADD:		/* arithmetic	*/
	case SPN_NODE_SUB:
	case SPN_NODE_MUL:
	case SPN_NODE_DIV:
	case SPN_NODE_MOD:
	case SPN_NODE_BITAND:		/* bitwise ops	*/
	case SPN_NODE_BITOR:
	case SPN_NODE_BITXOR:
	case SPN_NODE_SHL:
	case SPN_NODE_SHR:
	case SPN_NODE_EQUAL:		/* comparisons	*/
	case SPN_NODE_NOTEQ:
	case SPN_NODE_LESS:
	case SPN_NODE_LEQ:
	case SPN_NODE_GREATER:
	case SPN_NODE_GEQ:		return compile_simple_binop(cmp, ast, dst);

	case SPN_NODE_ASSIGN:		return compile_assignment(cmp, ast, dst);

	case SPN_NODE_ASSIGN_ADD:
	case SPN_NODE_ASSIGN_SUB:
	case SPN_NODE_ASSIGN_MUL:
	case SPN_NODE_ASSIGN_DIV:
	case SPN_NODE_ASSIGN_MOD:
	case SPN_NODE_ASSIGN_AND:
	case SPN_NODE_ASSIGN_OR:
	case SPN_NODE_ASSIGN_XOR:
	case SPN_NODE_ASSIGN_SHL:
	case SPN_NODE_ASSIGN_SHR:
	case SPN_NODE_ASSIGN_CONCAT:	return compile_compound_assignment(cmp, ast, dst);

	/* short-circuiting logical operators */
	case SPN_NODE_LOGAND:
	case SPN_NODE_LOGOR:		return compile_logical(cmp, ast, dst);

	/* conditional ternary */
	case SPN_NODE_CONDEXPR:		return compile_condexpr(cmp, ast, dst);

	/* variables, functions */
	case SPN_NODE_IDENT:		return compile_ident(cmp, ast, dst);

	/* immediate values */
	case SPN_NODE_LITERAL:		return compile_literal(cmp, ast, dst);

	/* function expression, lambda */	
	case SPN_NODE_FUNCEXPR:		return compile_funcexpr(cmp, ast, dst);

	/* array indexing */
	case SPN_NODE_ARRSUB:
	case SPN_NODE_MEMBEROF:		return compile_arrsub(cmp, ast, dst);

	/* function calls */
	case SPN_NODE_FUNCCALL:		return compile_call(cmp, ast, dst);

	/* unary plus just returns its argument verbatim */
	case SPN_NODE_UNPLUS:		return compile_expr(cmp, ast->left, dst);

	/* non-altering prefix unary operators */
	case SPN_NODE_SIZEOF:
	case SPN_NODE_TYPEOF:
	case SPN_NODE_LOGNOT:
	case SPN_NODE_BITNOT:
	case SPN_NODE_NTHARG:		return compile_unary(cmp, ast, dst);

	/* unary minus is special and it's handled separately,
	 * because it is optimized when used with literals.
	 */
	case SPN_NODE_UNMINUS:		return compile_unminus(cmp, ast, dst);

	case SPN_NODE_PREINCRMT:
	case SPN_NODE_PREDECRMT:
	case SPN_NODE_POSTINCRMT:
	case SPN_NODE_POSTDECRMT:	return compile_incdec(cmp, ast, dst);

	default: /* my apologies, again */
		{
			int node = ast->node;
			const void *args[1];
			args[0] = &node;
			compiler_error(cmp, ast->lineno, "unrecognized AST node `%i'", args);
			return 0;
		}
	}
}

