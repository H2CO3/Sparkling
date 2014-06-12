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
#include "func.h"


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

/* linked list for storing offsets and types
 * of `break` and `continue` statements
 */
struct jump_stmt_list {
	int is_break; /* boolean flag: 1 -> break, 0 -> continue */
	spn_sword offset; /* offset (relative to beginning of bytecode) */
	struct jump_stmt_list *next;
};

static void prepend_jumplist_node(SpnCompiler *cmp, spn_sword offset, int is_break);
static void free_jumplist(struct jump_stmt_list *hdr);

/* used for looking up upvalues (bound variables) of closures */
typedef struct UpvalChain {
	/* strong pointer: maps variable names to upvalue indices */
	SpnArray *name_to_index;
	/* weak pointer: maps upvalue indices to upvalue descriptors */
	SpnArray *index_to_desc;
	/* weak pointer: points to the enclosing function's variable stack */
	RoundTripStore *enclosing_varstack;
	/* strong pointer: link list, points to scope of enclosing function */
	struct UpvalChain *next;
} UpvalChain;

/* if you ever add a member to this structure, consider adding it to the
 * scope context info as well if necessary (and extend the `save_scope()`
 * and `restore_scope()` functions accordingly).
 */
struct SpnCompiler {
	TBytecode		 bc;
	char			*errmsg;
	int			 tmpidx;	/* (I)		*/
	int			 nregs;		/* (II)		*/
	RoundTripStore		*symtab;	/* (III)	*/
	RoundTripStore		*varstack;	/* (IV)		*/
	struct jump_stmt_list	*jumplist;	/* (V)		*/
	int			 is_in_loop;	/* (VI)		*/
	UpvalChain		*upval_chain;	/* (VII)	*/
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
 * (III) - (IV): array of local symbols and stack of file-scope and local
 * (block-scope) variable names and the corresponding register indices
 *
 * (V): link list for storing the offsets and types of unconditional
 * control flow statements `break` and `continue`
 *
 * (VI): Boolean flag which is nonzero inside a loop and zero outside a
 * loop. It is used to limit the use of `break` and `continue` to loop bodies
 * (since it doesn't make sense to `break` or `continue` outside a loop).
 *
 * (VII): the UpvalChain link list forms a stack. Each node contains two
 * arrays: they contain the same 'spn_uword's (the "upvalue descriptors"),
 * that describe which variables in the scope of the containing function are
 * referred to as upvalues in the function corresponding to a given node.
 * The `names_to_desc` array maps variable names to upvalue descriptors,
 * and is unordered; meanwhile, `index_to_desc` is sorted. The former array
 * is used for looking up the external local variables _during_ compilation,
 * the latter is used for generating the SPN_INS_CLOSURE instruction _after_
 * the compilation of the function body.
 */

/* information describing the state of the global scope or a function scope.
 * this has to be preserved (saved and restored) across the compilation of
 * function bodies.
 */
typedef struct ScopeInfo {
	int			 tmpidx;
	int			 nregs;
	RoundTripStore		*varstack;
	struct jump_stmt_list	*jumplist;
	int			 is_in_loop;
} ScopeInfo;

static void save_scope(SpnCompiler *cmp, ScopeInfo *sci)
{
	sci->tmpidx = cmp->tmpidx;
	sci->nregs = cmp->nregs;
	sci->varstack = cmp->varstack;
	sci->jumplist = cmp->jumplist;
	sci->is_in_loop = cmp->is_in_loop;
}

static void restore_scope(SpnCompiler *cmp, ScopeInfo *sci)
{
	cmp->tmpidx = sci->tmpidx;
	cmp->nregs = sci->nregs;
	cmp->varstack = sci->varstack;
	cmp->jumplist = sci->jumplist;
	cmp->is_in_loop = sci->is_in_loop;
}

/*****************************
 * Symbol table entry class. *
 *****************************/

/* This class is used for fixing the ugly hack that was previously used in
 * the code of the compiler. Namely, symbol table entries were represented
 * in a quite, khm, "particular" way inside the SpnCompiler struct:
 * - String literals were stored as SpnStrings (that's fine);
 * - Stubs that reference global symbols were stored as SpnValues of type
 *	SPN_TYPE_FUNCTION, with their name set to the symbol name. This is
 * 	conceptually wrong because global symbols can be of any type, not
 * 	just functions.
 * - Symbols corresponding to unnamed lambdas in the current translation unit
 * 	were stored as integers (SpnValue of type SPN_TYPE_NUMBER), since an
 * 	integer offset unambiguously identifies such an unnamed function, but
 * 	an SpnValue of function type cannot store integers. This is also wrong
 * 	conceptually.
 * What's worse, after a recent attempt to clean up the equality testing and
 * hashing methods of functions, this lead to various errors: functions should
 * be compared and hashed using their addresses only (be they C function
 * pointers or pointers to inside a chunk of bytecode); however, the hack in
 * the code of the compiler assumed that named function values are compared
 * based on their name (which was indeed the case up until now). Of course,
 * this failed miserably when used with the new comparison and hashing code
 * that only considered pointers but not names.
 */

enum symtabentry_type {
	SYMTABENTRY_GLOBAL,
	SYMTABENTRY_FUNCTION
};

typedef struct SymtabEntry {
	SpnObject base;
	enum symtabentry_type type;
	SpnString *name;  /* name of the function or global symbol stub	*/
	ptrdiff_t offset; /* offset of the function in the bytecode	*/
} SymtabEntry;

static int symtabentry_equal(void *lhs, void *rhs)
{
	const SymtabEntry *lo = lhs, *ro = rhs;

	if (lo->type != ro->type) {
		return 0;
	}

	switch (lo->type) {
	case SYMTABENTRY_GLOBAL:
		return spn_object_equal(lo->name, ro->name);
	case SYMTABENTRY_FUNCTION:
		return lo->offset == ro->offset;
	default:
		SHANT_BE_REACHED();
		return 0;
	}
}

static unsigned long symtabentry_hash(void *obj)
{
	SymtabEntry *entry = obj;

	switch (entry->type) {
	case SYMTABENTRY_GLOBAL: {
		SpnObject *base = &entry->name->base;
		return base->isa->hashfn(base);
	}
	case SYMTABENTRY_FUNCTION:
		return entry->offset;
	default:
		SHANT_BE_REACHED();
		return 0;
	}
}

static void symtabentry_free(void *obj)
{
	SymtabEntry *entry = obj;

	if (entry->name != NULL) {
		spn_object_release(entry->name);
	}
}

static const SpnClass SymtabEntry_class = {
	sizeof(SymtabEntry),
	symtabentry_equal,
	NULL,
	symtabentry_hash,
	symtabentry_free
};

static SymtabEntry *symtabentry_new_global(SpnString *name)
{
	SymtabEntry *entry = spn_object_new(&SymtabEntry_class);
	entry->type = SYMTABENTRY_GLOBAL;
	spn_object_retain(name);
	entry->name = name;
	return entry;
}

static SymtabEntry *symtabentry_new_function(ptrdiff_t offset, SpnString *name)
{
	SymtabEntry *entry = spn_object_new(&SymtabEntry_class);
	entry->type = SYMTABENTRY_FUNCTION;
	entry->offset = offset;

	/* name is optional */
	if (name != NULL) {
		spn_object_retain(name);
		entry->name = name;
	} else {
		entry->name = NULL;
	}

	return entry;
}

/****************************************/

/* compile_*() functions return nonzero on success, 0 on error */
static int compile(SpnCompiler *cmp, SpnAST *ast);

static int compile_program(SpnCompiler *cmp, SpnAST *ast);
static int compile_compound(SpnCompiler *cmp, SpnAST *ast);
static int compile_block(SpnCompiler *cmp, SpnAST *ast);
static int compile_funcdef(SpnCompiler *cmp, SpnAST *ast, int *symidx, SpnArray *upvalues);
static int compile_while(SpnCompiler *cmp, SpnAST *ast);
static int compile_do(SpnCompiler *cmp, SpnAST *ast);
static int compile_for(SpnCompiler *cmp, SpnAST *ast);
static int compile_if(SpnCompiler *cmp, SpnAST *ast);

static int compile_break(SpnCompiler *cmp, SpnAST *ast);
static int compile_continue(SpnCompiler *cmp, SpnAST *ast);
static int compile_vardecl(SpnCompiler *cmp, SpnAST *ast);
static int compile_const(SpnCompiler *cmp, SpnAST *ast);
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
static void compiler_error(SpnCompiler *cmp, int lineno, const char *fmt, const void *args[]);

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
static void rts_getval(RoundTripStore *rts, int idx, SpnValue *val); /* sets val to nil if not found */
static int rts_getidx(RoundTripStore *rts, SpnValue *val); /* returns < 0 if not found */
static int rts_count(RoundTripStore *rts);
static void rts_delete_top(RoundTripStore *rts, int newsize);
static void rts_free(RoundTripStore *rts);

SpnCompiler *spn_compiler_new(void)
{
	SpnCompiler *cmp = spn_malloc(sizeof(*cmp));

	cmp->errmsg = NULL;
	cmp->jumplist = NULL;
	cmp->is_in_loop = 0;
	cmp->upval_chain = NULL;

	return cmp;
}

void spn_compiler_free(SpnCompiler *cmp)
{
	free(cmp->errmsg);
	free(cmp);
}

int spn_compiler_compile(SpnCompiler *cmp, SpnAST *ast, SpnValue *result)
{
	bytecode_init(&cmp->bc);

	if (compile_program(cmp, ast)) {
		/* success. transfer ownership of cmp->bc.insns to `result' */
		*result = maketopprgfunc(SPN_TOPFN, cmp->bc.insns, cmp->bc.len);

		/* should be at global scope when compilation is done */
		assert(cmp->upval_chain == NULL);

		return 0;
	}

	/* error */
	free(cmp->bc.insns);
	return -1;
}

const char *spn_compiler_errmsg(SpnCompiler *cmp)
{
	return cmp->errmsg;
}

/* Functions for working with upvalues
 * the 'upvalues' argument of upval_chain_push() is an already existing,
 * empty array, which will be filled by 'compile_funcdef()'.
 */
static void upval_chain_push(SpnCompiler *cmp, SpnArray *upvalues)
{
	UpvalChain *node = spn_malloc(sizeof(*node));

	node->name_to_index = spn_array_new();
	node->index_to_desc = upvalues;
	node->enclosing_varstack = cmp->varstack;
	node->next = cmp->upval_chain;

	cmp->upval_chain = node;
}

static void upval_chain_pop(SpnCompiler *cmp)
{
	UpvalChain *head = cmp->upval_chain;
	UpvalChain *next = head->next;

	spn_object_release(head->name_to_index);

	/* nothing to do with head->index_to_desc and head->enclosing_varstack,
	 * since they are weak (non-owning) pointers
	 */

	free(head);
	cmp->upval_chain = next;
}

/* returns the index of the upvalue (in the closure of the current,
 * innermost function) if it is in scope.
 *
 * XXX: If there's no variable in scope with the given name, returns -1.
 */
static int search_upvalues(UpvalChain *node, SpnValue *name)
{
	int enclosing_varidx, enclosing_upvalidx, flat_upvalidx;
	spn_uword flat_upvaldesc;
	SpnValue upval_idx_val, flat_upvalidx_val, flat_upvaldesc_val;

	if (node == NULL) {
		return -1;
	}

	assert(isstring(name));

	/* first, search in the closure of the current function */
	spn_array_get(node->name_to_index, name, &upval_idx_val);

	/* if it's found, return its index */
	if (isint(&upval_idx_val)) {
		return intvalue(&upval_idx_val);
	}

	assert(isnil(&upval_idx_val));

	/* if it's not there, search the locals of the enclosing function */
	enclosing_varidx = rts_getidx(node->enclosing_varstack, name);

	/* if found, add to the closure of current function, and return it */
	if (enclosing_varidx >= 0) {
		/* make an upvalue descriptor */
		spn_uword upval_desc = SPN_MKINS_A(SPN_UPVAL_LOCAL, enclosing_varidx);
		SpnValue upval_desc_val = makeint(upval_desc);

		/* add to current closure */
		int upval_idx = spn_array_count(node->name_to_index);
		SpnValue new_idx_val = makeint(upval_idx);

		/* the two upvalue arrays should contain the same
		 * entries, therefore their sizes must be equal.
		 */
		assert(spn_array_count(node->index_to_desc) == upval_idx);

		spn_array_set(node->name_to_index, name, &new_idx_val);
		spn_array_set_intkey(node->index_to_desc, upval_idx, &upval_desc_val);

		return upval_idx;
	}

	/* else continue searching in enclosing function recursively */
	enclosing_upvalidx = search_upvalues(node->next, name);

	/* if not found even there, return -1 to indicate "not found" */
	if (enclosing_upvalidx < 0) {
		return -1;
	}

	/* But if is found, add it to the current closure and return it.
	 * (This is the step which makes the closures "flat".)
	 */
	flat_upvalidx = spn_array_count(node->name_to_index);
	flat_upvalidx_val = makeint(flat_upvalidx);

	assert(spn_array_count(node->index_to_desc) == flat_upvalidx);

	flat_upvaldesc = SPN_MKINS_A(SPN_UPVAL_OUTER, enclosing_upvalidx);
	flat_upvaldesc_val = makeint(flat_upvaldesc);

	spn_array_set(node->name_to_index, name, &flat_upvalidx_val);
	spn_array_set_intkey(node->index_to_desc, flat_upvalidx, &flat_upvaldesc_val);

	return flat_upvalidx;
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
			bc->allocsz *= 2;
		}

		bc->insns = spn_realloc(bc->insns, bc->allocsz * sizeof(bc->insns[0]));
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

	spn_uword *buf = spn_malloc(padded_len);

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
	SpnValue idxval = makeint(idx);
	spn_array_set(rts->fwd, &idxval, val);
	spn_array_set(rts->inv, val, &idxval);

	/* keep track of maximal size of the RTS */
	newsize = rts_count(rts);
	if (newsize > rts->maxsize) {
		rts->maxsize = newsize;
	}

	return idx;
}

static void rts_getval(RoundTripStore *rts, int idx, SpnValue *val)
{
	spn_array_get_intkey(rts->fwd, idx, val);
}

static int rts_getidx(RoundTripStore *rts, SpnValue *val)
{
	SpnValue res;
	spn_array_get(rts->inv, val, &res);
	return isnumber(&res) ? intvalue(&res) : -1;
}

static int rts_count(RoundTripStore *rts)
{
	size_t n = spn_array_count(rts->fwd);

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
		SpnValue val, idx = makeint(i);

		spn_array_get(rts->fwd, &idx, &val);
		assert(!isnil(&val));

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


static void compiler_error(SpnCompiler *cmp, int lineno, const char *fmt, const void *args[])
{
	char *prefix, *msg;
	size_t prefix_len, msg_len;
	const void *prefix_args[1];
	prefix_args[0] = &lineno;

	prefix = spn_string_format_cstr(
		"semantic error near line %i: ",
		&prefix_len,
		prefix_args
	);

	msg = spn_string_format_cstr(fmt, &msg_len, args);

	free(cmp->errmsg);
	cmp->errmsg = spn_malloc(prefix_len + msg_len + 1);

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
	case SPN_NODE_WHILE:	return compile_while(cmp, ast);
	case SPN_NODE_DO:	return compile_do(cmp, ast);
	case SPN_NODE_FOR:	return compile_for(cmp, ast);
	case SPN_NODE_IF:	return compile_if(cmp, ast);
	case SPN_NODE_BREAK:	return compile_break(cmp, ast);
	case SPN_NODE_CONTINUE:	return compile_continue(cmp, ast);
	case SPN_NODE_RETURN:	return compile_return(cmp, ast);
	case SPN_NODE_EMPTY:	return 1; /* no compile_empty() for you ;-) */
	case SPN_NODE_VARDECL:	return compile_vardecl(cmp, ast);
	case SPN_NODE_CONST:	return compile_const(cmp, ast);
	default:		return compile_expr_toplevel(cmp, ast, NULL);
	}
}

/* returns zero on success, nonzero on error */
static int write_symtab(SpnCompiler *cmp)
{
	int i, nsyms = rts_count(cmp->symtab);

	for (i = 0; i < nsyms; i++) {
		SpnValue sym;
		rts_getval(cmp->symtab, i, &sym);

		switch (valtype(&sym)) {
		case SPN_TTAG_STRING: {
			/* string literal */

			SpnString *str = stringvalue(&sym);

			/* append symbol type and length description */
			spn_uword ins = SPN_MKINS_LONG(SPN_LOCSYM_STRCONST, str->len);
			bytecode_append(&cmp->bc, &ins, 1);

			/* append actual 0-terminated string */
			append_cstring(&cmp->bc, str->cstr, str->len);
			break;
		}
		case SPN_TTAG_USERINFO: {
			SymtabEntry *entry = objvalue(&sym);

			switch (entry->type) {
			case SYMTABENTRY_GLOBAL: {
				SpnString *name = entry->name;

				/* append symbol type and name length */
				spn_uword ins = SPN_MKINS_LONG(SPN_LOCSYM_SYMSTUB, name->len);
				bytecode_append(&cmp->bc, &ins, 1);

				/* append symbol name */
				append_cstring(&cmp->bc, name->cstr, name->len);

				break;
			}
			case SYMTABENTRY_FUNCTION: {
				spn_uword symtype = SPN_MKINS_VOID(SPN_LOCSYM_FUNCDEF);
				spn_uword offset = entry->offset;

				SpnString *nobj = entry->name;
				spn_uword namelen = nobj ? nobj->len : strlen(SPN_LAMBDA_NAME);
				const char *name = nobj ? nobj->cstr : SPN_LAMBDA_NAME;

				/* append symbol type */
				bytecode_append(&cmp->bc, &symtype, 1);

				/* append function header offset */
				bytecode_append(&cmp->bc, &offset, 1);

				/* append name length and name */
				bytecode_append(&cmp->bc, &namelen, 1);
				append_cstring(&cmp->bc, name, namelen);

				break;
			}
			default:
				SHANT_BE_REACHED();
			}

			break;
		}
		default:
			{
				/* got something that's not supposed to be there */
				const void *args[1];
				args[0] = spn_type_name(valtype(&sym));
				compiler_error(cmp, 0, "wrong symbol type %s in write_symtab()", args);
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
	spn_uword header[SPN_FUNCHDR_LEN] = { 0 };
	bytecode_append(&cmp->bc, header, SPN_FUNCHDR_LEN);

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
	cmp->bc.insns[SPN_FUNCHDR_IDX_BODYLEN] = cmp->bc.len - SPN_FUNCHDR_LEN;
	cmp->bc.insns[SPN_FUNCHDR_IDX_ARGC]    = 0; /* no formal params for top-level program */
	cmp->bc.insns[SPN_FUNCHDR_IDX_NREGS]   = regcnt;
	cmp->bc.insns[SPN_FUNCHDR_IDX_SYMCNT]  = rts_count(cmp->symtab);

	/* write local symbol table, check for errors */
	if (write_symtab(cmp) != 0) {
		rts_free(&symtab);
		rts_free(&glbvars);
		return 0;
	}

	/* clean up */
	rts_free(&symtab);
	rts_free(&glbvars);

	if (regcnt > MAX_REG_FRAME) {
		compiler_error(
			cmp,
			ast->lineno,
			"too many registers in top-level program",
			NULL
		);
		return 0;
	}

	return 1;
}

static int compile_compound(SpnCompiler *cmp, SpnAST *ast)
{
	return compile(cmp, ast->left) && compile(cmp, ast->right);
}

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

static int compile_funcdef(SpnCompiler *cmp, SpnAST *ast, int *symidx, SpnArray *upvalues)
{
	int regcount, argc;
	spn_uword ins, fnhdr[SPN_FUNCHDR_LEN] = { 0 };
	size_t hdroff, bodylen;
	SpnAST *arg;
	SymtabEntry *entry;
	RoundTripStore vs_this;
	ScopeInfo sci;
	SpnValue offval;

	/* self-examination (transitions are hard) */
	assert(ast->node == SPN_NODE_FUNCEXPR);

	/* The `upval_chain_push()` function must be called BEFORE we replace
	 * `cmp->varstack` with our own, new variable stack, because the head
	 * of the upvalue chain needs to refer to the scope of the enclosing
	 * function; otherwise, `search_upvalues()` function would not really
	 * search the upvalues of the current function, but rather its locals.
	 */
	upval_chain_push(cmp, upvalues);

	/* save scope context data: local variables, temporary register stack
	 * pointer, maximal number of registers, innermost loop offset
	 */
	save_scope(cmp, &sci);

	/* init new local variable stack and register counter */
	rts_init(&vs_this);
	cmp->varstack = &vs_this;
	cmp->nregs = 0;

	/* write VM instruction `SPN_INS_FUNCTION' to bytecode */
	ins = SPN_MKINS_VOID(SPN_INS_FUNCTION);
	bytecode_append(&cmp->bc, &ins, 1);

	/* save the offset of the function header */
	hdroff = cmp->bc.len;

	/* write stub function header */
	bytecode_append(&cmp->bc, fnhdr, SPN_FUNCHDR_LEN);

	/* create a local symtab entry for the function */
	entry = symtabentry_new_function(hdroff, ast->name);
	offval = makestrguserinfo(entry);
	*symidx = rts_add(cmp->symtab, &offval);
	spn_object_release(entry);

	/* bring function arguments in scope (they are put in the first
	 * `argc' registers), and count them as well
	 */
	for (arg = ast->left; arg != NULL; arg = arg->left) {
		SpnValue argname;
		argname.type = SPN_TYPE_STRING;
		argname.v.o = arg->name;

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
			upval_chain_pop(cmp);

			return 0;
		}
	}

	argc = rts_count(cmp->varstack);

	/* compile body */
	if (compile(cmp, ast->right) == 0) {
		rts_free(&vs_this);
		restore_scope(cmp, &sci);
		upval_chain_pop(cmp);

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
	cmp->bc.insns[hdroff + SPN_FUNCHDR_IDX_SYMCNT]  = 0; /* unused anyway */

	/* free local var stack, restore scope context data */
	rts_free(&vs_this);
	restore_scope(cmp, &sci);
	upval_chain_pop(cmp);

	if (regcount > MAX_REG_FRAME) {
		compiler_error(
			cmp,
			ast->lineno,
			"too many registers in function",
			NULL
		);
		return 0;
	}

	return 1;
}

/* helper function for filling in jump list (list of `break` and `continue`
 * statements) in a while, do-while or for loop.
 *
 * `off_end` is the offset after the whole loop, where control flow should be
 * transferred by `break`. `off_cond` is the offset of the condition (or
 * that of the incrementing expression in the case of `for` loops) where a
 * `continue` statement should transfer the control flow.
 *
 * This function also frees the link list on the fly.
 */
static void fix_and_free_jump_list(SpnCompiler *cmp, spn_sword off_end, spn_sword off_cond)
{
	struct jump_stmt_list *hdr = cmp->jumplist;
	while (hdr != NULL) {
		struct jump_stmt_list *tmp = hdr->next;

		/* `break` jumps to right after the end of the body,
		 * whereas `continue` jumps back to the condition
		 * +2: a jump instruction is 2 words long
		 */
		spn_sword target_off = hdr->is_break ? off_end : off_cond;
		spn_sword rel_off = target_off - (hdr->offset + 2);

		cmp->bc.insns[hdr->offset + 0] = SPN_MKINS_VOID(SPN_INS_JMP);
		cmp->bc.insns[hdr->offset + 1] = rel_off;

		free(hdr);
		hdr = tmp;
	}
}

static int compile_while(SpnCompiler *cmp, SpnAST *ast)
{
	int cndidx = -1;
	spn_uword ins[2] = { 0 }; /* stub */
	spn_sword off_cond, off_cndjmp, off_body, off_jmpback, off_end;

	/* save old loop state */
	int is_in_loop = cmp->is_in_loop;
	struct jump_stmt_list *orig_jumplist = cmp->jumplist;

	/* set up new loop state */
	cmp->is_in_loop = 1;
	cmp->jumplist = NULL;

	/* save offset of condition */
	off_cond = cmp->bc.len;

	/* compile condition
	 * on error, clean up, restore jumplist
	 * no need to free it -- it's empty so far
	 */
	if (compile_expr_toplevel(cmp, ast->left, &cndidx) == 0) {
		cmp->jumplist = orig_jumplist;
		cmp->is_in_loop = is_in_loop;
		return 0;
	}

	off_cndjmp = cmp->bc.len;

	/* append jump over the loop body if condition is false (stub) */
	bytecode_append(&cmp->bc, ins, COUNT(ins));

	off_body = cmp->bc.len;

	/* compile loop body */
	if (compile(cmp, ast->right) == 0) {
		/* clean up and restore jumplist */
		free_jumplist(cmp->jumplist);
		cmp->jumplist = orig_jumplist;
		cmp->is_in_loop = is_in_loop;
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

	/* fix up dummy jumps, free jump list on the fly */
	fix_and_free_jump_list(cmp, off_end, off_cond);

	/* restore original jump list */
	cmp->jumplist = orig_jumplist;
	cmp->is_in_loop = is_in_loop;

	return 1;
}

static int compile_do(SpnCompiler *cmp, SpnAST *ast)
{
	spn_sword off_body = cmp->bc.len;
	spn_sword off_jmp, off_cond, off_end, diff;
	spn_uword ins[2];
	int reg = -1;

	/* save old loop state */
	int is_in_loop = cmp->is_in_loop;
	struct jump_stmt_list *orig_jumplist = cmp->jumplist;

	/* set up new loop state */
	cmp->is_in_loop = 1;
	cmp->jumplist = NULL;

	/* compile body; clean up jump list on error */
	if (compile(cmp, ast->right) == 0) {
		free_jumplist(cmp->jumplist);
		cmp->jumplist = orig_jumplist;
		cmp->is_in_loop = is_in_loop;
		return 0;
	}

	off_cond = cmp->bc.len;

	/* compile condition, clean up jump list on error */
	if (compile_expr_toplevel(cmp, ast->left, &reg) == 0) {
		free_jumplist(cmp->jumplist);
		cmp->jumplist = orig_jumplist;
		cmp->is_in_loop = is_in_loop;
		return 0;
	}

	off_jmp = cmp->bc.len + COUNT(ins);
	diff = off_body - off_jmp;

	/* jump back to body if condition is true */
	ins[0] = SPN_MKINS_A(SPN_INS_JNZ, reg);
	ins[1] = diff;
	bytecode_append(&cmp->bc, ins, COUNT(ins));

	off_end = cmp->bc.len;

	/* fix up continue and break statements, free jump list on the fly */
	fix_and_free_jump_list(cmp, off_end, off_cond);

	/* restore original jump list */
	cmp->jumplist = orig_jumplist;
	cmp->is_in_loop = is_in_loop;

	return 1;
}

static int compile_for(SpnCompiler *cmp, SpnAST *ast)
{
	int regidx = -1;
	int old_stack_size;
	spn_sword off_cond, off_incmt, off_body_begin, off_body_end, off_cond_jmp, off_uncd_jmp;
	spn_uword jmpins[2] = { 0 }; /* dummy */
	SpnAST *header, *init, *cond, *icmt;

	/* save old loop state */
	int is_in_loop = cmp->is_in_loop;
	struct jump_stmt_list *orig_jumplist = cmp->jumplist;

	/* set up new loop state */
	cmp->is_in_loop = 1;
	cmp->jumplist = NULL;

	/* hand-unrolled loops FTW! */
	header = ast->left;
	init = header->left;
	header = header->right;
	cond = header->left;
	header = header->right;
	icmt = header->left;

	/* we want that the scope of variables declared in the initialization
	 * be limited to the loop body, so here we save the variable stack size
	 */
	old_stack_size = rts_count(cmp->varstack);

	/* compile initialization ouside the loop;
	 * restore jump list on error (no need to free, here it's empty)
	 * `compile()' is used instead of `compile_expr_toplevel()'
	 * because `init' may be an expression or a variable declaration
	 */
	if (compile(cmp, init) == 0) {
		cmp->jumplist = orig_jumplist;
		cmp->is_in_loop = is_in_loop;
		return 0;
	}

	/* compile condition, clean up on error likewise */
	off_cond = cmp->bc.len;
	if (compile_expr_toplevel(cmp, cond, &regidx) == 0) {
		cmp->jumplist = orig_jumplist;
		cmp->is_in_loop = is_in_loop;
		return 0;
	}

	/* compile "skip body if condition is false" jump */
	off_cond_jmp = cmp->bc.len;
	bytecode_append(&cmp->bc, jmpins, COUNT(jmpins));

	/* compile body */
	off_body_begin = cmp->bc.len;
	if (compile(cmp, ast->right) == 0) {
		free_jumplist(cmp->jumplist);
		cmp->jumplist = orig_jumplist;
		cmp->is_in_loop = is_in_loop;
		return 0;
	}

	/* compile incrementing expression */
	off_incmt = cmp->bc.len;
	if (compile_expr_toplevel(cmp, icmt, NULL) == 0) {
		free_jumplist(cmp->jumplist);
		cmp->jumplist = orig_jumplist;
		cmp->is_in_loop = is_in_loop;
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

	/* 3. patch break and continue instructions */
	fix_and_free_jump_list(cmp, off_body_end, off_incmt);

	/* get rid of variables declared in the initialization of the loop */
	rts_delete_top(cmp->varstack, old_stack_size);

	/* restore jump state */
	cmp->jumplist = orig_jumplist;
	cmp->is_in_loop = is_in_loop;

	return 1;
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

/* helper function for `compile_break()` and `compile_continue()` */
static void prepend_jumplist_node(SpnCompiler *cmp, spn_sword offset, int is_break)
{
	struct jump_stmt_list *jump = spn_malloc(sizeof(*jump));

	jump->offset = offset;
	jump->is_break = is_break;
	jump->next = cmp->jumplist;
	cmp->jumplist = jump;
}

static void free_jumplist(struct jump_stmt_list *hdr)
{
	while (hdr != NULL) {
		struct jump_stmt_list *tmp = hdr->next;
		free(hdr);
		hdr = tmp;
	}
}

static int compile_break(SpnCompiler *cmp, SpnAST *ast)
{
	/* dummy jump instruction */
	spn_uword ins[2] = { 0 };

	/* it doesn't make sense to `break` outside a loop */
	if (cmp->is_in_loop == 0) {
		compiler_error(cmp, ast->lineno, "`break' is only meaningful inside a loop", NULL);
		return 0;
	}

	/* add link list node with current offset */
	prepend_jumplist_node(cmp, cmp->bc.len, 1);

	/* then append dummy jump instruction */
	bytecode_append(&cmp->bc, ins, COUNT(ins));

	return 1;
}

static int compile_continue(SpnCompiler *cmp, SpnAST *ast)
{
	spn_uword ins[2] = { 0 };

	/* it doesn't make sense to `continue` outside a loop either */
	if (cmp->is_in_loop == 0) {
		compiler_error(cmp, ast->lineno, "`continue' is only meaningful inside a loop", NULL);
		return 0;
	}

	/* add link list node with current offset */
	prepend_jumplist_node(cmp, cmp->bc.len, 0);

	/* then append dummy jump instruction */
	bytecode_append(&cmp->bc, ins, COUNT(ins));

	return 1;
}

/* left child: initializer expression, if any (or NULL)
 * right child: next variable declaration (link list)
 */
static int compile_vardecl(SpnCompiler *cmp, SpnAST *ast)
{
	SpnAST *head = ast;

	while (head != NULL) {
		int idx;
		spn_uword ins;

		SpnValue name;
		name.type = SPN_TYPE_STRING;
		name.v.o = head->name;

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

		/* always load nil into register before compiling initializer
		 * expression, in order to avoid garbage when one initializes
		 * a variable with an expression that refers to itself
		 */
		ins = SPN_MKINS_AB(SPN_INS_LDCONST, idx, SPN_CONST_NIL);
		bytecode_append(&cmp->bc, &ins, 1);

		if (head->left != NULL) {
			if (compile_expr_toplevel(cmp, head->left, &idx) == 0) {
				return 0;
			}
		}

		head = head->right;
	}

	return 1;
}

static int compile_const(SpnCompiler *cmp, SpnAST *ast)
{
	while (ast != NULL) {
		spn_uword ins;

		int regidx = -1;
		if (compile_expr_toplevel(cmp, ast->left, &regidx) == 0) {
			return 0;
		}

		/* write "set global symbol" instruction */
		ins = SPN_MKINS_MID(SPN_INS_GLBVAL, regidx, ast->name->len);
		bytecode_append(&cmp->bc, &ins, 1);

		/* append 0-terminated name of the symbol */
		append_cstring(&cmp->bc, ast->name->cstr, ast->name->len);

		/* update head of link list */
		ast = ast->right;
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
 * statement.
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

	assert(isstring(str));

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
	ident.type = SPN_TYPE_STRING;
	ident.v.o = ast->left->name;

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
		nameval.type = SPN_TYPE_STRING;
		nameval.v.o = lhs->right->name;
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
			"left-hand side of assignment must be a variable or an array member",
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
	ident.type = SPN_TYPE_STRING;
	ident.v.o = ast->left->name;

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
		nameval.type = SPN_TYPE_STRING;
		nameval.v.o = lhs->right->name;
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
			"left-hand side of assignment must be a variable or an array member",
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
	 * the right-hand side expression too, we will be in trouble.
	 * So, the `idx` variable holds the desintation index of the temporary
	 * register in which the value of the two sides will be stored.
	 */
	int idx;

	/* this needs to be done before `idx = tmp_push()`, because the
	 * temporary register will be gone when the logical expression has
	 * finished evaluating, whereas the result shall be preserved and
	 * accessible. If I did this in the wrong order, then the result
	 * register could be (ab)used as a temporary in a higher-level
	 * expression and it could be overwritten.
	 */
	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	idx = tmp_push(cmp);

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

	varname.type = SPN_TYPE_STRING;
	varname.v.o = ast->name;

	idx = rts_getidx(cmp->varstack, &varname);

	/* if `rts_getidx()` returns -1, then the variable is not in the
	 * current scope, so we search for its name in the enclosing scopes.
	 * If it is found in one of the enclosing scopes, then we load it as
	 * the appropriate upvalue. Else we assume that it referes to a global
	 * symbol and we search the symbol table. If it's not yet in the symbol
	 * table, we create a symtab entry referencing the unresolved global.
	 */
	if (idx < 0) {
		int upval_idx = search_upvalues(cmp->upval_chain, &varname);

		/* if it's not found in the closure either, assume a global */
		if (upval_idx < 0) {
			spn_uword ins;
			int sym;

			SymtabEntry *entry = symtabentry_new_global(ast->name);
			SpnValue stub = makestrguserinfo(entry);

			sym = rts_getidx(cmp->symtab, &stub);
			if (sym < 0) {
				/* not found, append to symtab */
				sym = rts_add(cmp->symtab, &stub);
			}

			spn_object_release(entry);

			/* compile "load symbol" instruction */
			if (*dst < 0) {
				*dst = tmp_push(cmp);
			}

			ins = SPN_MKINS_MID(SPN_INS_LDSYM, *dst, sym);
			bytecode_append(&cmp->bc, &ins, 1);
		} else {
			/* if this is reached, the variable (upvalue) was
			 * found in the enclosing scope
			 */
			spn_uword ins;

			/* XXX: TODO: check that upval_idx <= 255! */

			if (*dst < 0) {
				*dst = tmp_push(cmp);
			}

			ins = SPN_MKINS_AB(SPN_INS_LDUPVAL, *dst, upval_idx);
			bytecode_append(&cmp->bc, &ins, 1);
		}

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

	switch (valtype(&ast->value)) {
	case SPN_TTAG_NIL: {
		spn_uword ins = SPN_MKINS_AB(SPN_INS_LDCONST, *dst, SPN_CONST_NIL);
		bytecode_append(&cmp->bc, &ins, 1);
		break;
	}
	case SPN_TTAG_BOOL: {
		enum spn_const_kind b = boolvalue(&ast->value)
				      ? SPN_CONST_TRUE
				      : SPN_CONST_FALSE;

		spn_uword ins = SPN_MKINS_AB(SPN_INS_LDCONST, *dst, b);
		bytecode_append(&cmp->bc, &ins, 1);
		break;
	}
	case SPN_TTAG_NUMBER: {
		if (isfloat(&ast->value)) {
			double num = floatvalue(&ast->value);
			spn_uword ins[1 + ROUNDUP(sizeof num, sizeof(spn_uword))] = { 0 };

			ins[0] = SPN_MKINS_AB(SPN_INS_LDCONST, *dst, SPN_CONST_FLOAT);
			memcpy(&ins[1], &num, sizeof num);

			bytecode_append(&cmp->bc, ins, COUNT(ins));
		} else {
			long num = intvalue(&ast->value);
			spn_uword ins[1 + ROUNDUP(sizeof num, sizeof(spn_uword))] = { 0 };

			ins[0] = SPN_MKINS_AB(SPN_INS_LDCONST, *dst, SPN_CONST_INT);
			memcpy(&ins[1], &num, sizeof num);

			bytecode_append(&cmp->bc, ins, COUNT(ins));
		}

		break;
	}
	case SPN_TTAG_STRING:
		compile_string_literal(cmp, &ast->value, dst);
		break;
	default:
		SHANT_BE_REACHED();
		return 0;
	}

	return 1;
}

static int compile_argc(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	spn_uword ins;

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	assert(ast->node == SPN_NODE_ARGC);

	/* emit "get argument count" instruction */
	ins = SPN_MKINS_A(SPN_INS_LDARGC, *dst);
	bytecode_append(&cmp->bc, &ins, 1);

	return 1;
}

static int compile_funcexpr(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	int symidx;
	int upval_count;
	spn_uword ins;
	SpnArray *upvalues = spn_array_new();

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* compile definition of function */
	if (compile_funcdef(cmp, ast, &symidx, upvalues) == 0) {
		spn_object_release(upvalues);
		return 0;
	}

	/* emit load instruction */
	ins = SPN_MKINS_MID(SPN_INS_LDSYM, *dst, symidx);
	bytecode_append(&cmp->bc, &ins, 1);

	/* By now, the `upvalues' array should be filled with upvalue
	 * descriptors, in the order they will be written to the bytecode.
	 * As an optimization, we create a closure object _only_ if the
	 * function uses at least one upvalue.
	 */
	upval_count = spn_array_count(upvalues);

	if (upval_count > 0) {
		int i;

		/* append SPN_INS_CLOSURE instruction */
		ins = SPN_MKINS_AB(SPN_INS_CLOSURE, *dst, upval_count);
		bytecode_append(&cmp->bc, &ins, 1);

		/* add upvalue descriptors */
		for (i = 0; i < upval_count; i++) {
			SpnValue upval_desc;

			spn_array_get_intkey(upvalues, i, &upval_desc);
			assert(isint(&upval_desc));

			ins = intvalue(&upval_desc);
			bytecode_append(&cmp->bc, &ins, 1);
		}
	}

	spn_object_release(upvalues);

	return 1;
}

static int compile_array_literal(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	spn_uword ins;
	int keyidx, validx;
	long array_idx = 0; /* this should be a `long`, since it's
			     * copied into the bytecode verbatim
			     */

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* obtain two temporary registers to store the key and the corresponding value */
	keyidx = tmp_push(cmp);
	validx = tmp_push(cmp);

	/* first, create the array */
	ins = SPN_MKINS_A(SPN_INS_NEWARR, *dst);
	bytecode_append(&cmp->bc, &ins, 1);

	/* ast->right is the next pointer;
	 * ast->left is the key-value pair;
	 * ast->left->left is the key or NULL,
	 * ast->left->right is the value (or NULL in case of an emtpy array)
	 */
	while (ast && ast->left && ast->left->right) {
		SpnAST *key = ast->left->left;
		SpnAST *val = ast->left->right;

		/* Compile the key. If it is NULL, then it is
		 * asssumed to be an incrementing integer index.
		 */
		if (key != NULL) {
			if (compile_expr(cmp, key, &keyidx) == 0) {
				return 0;
			}
		} else {
			spn_uword index_data[ROUNDUP(sizeof(array_idx), sizeof(spn_uword))] = { 0 };
			memcpy(index_data, &array_idx, sizeof(array_idx));
			ins = SPN_MKINS_AB(SPN_INS_LDCONST, keyidx, SPN_CONST_INT);

			bytecode_append(&cmp->bc, &ins, 1);
			bytecode_append(&cmp->bc, index_data, COUNT(index_data));
		}

		/* compile the value */
		if (compile_expr(cmp, val, &validx) == 0) {
			return 0;
		}

		/* emit instruction for array setter */
		ins = SPN_MKINS_ABC(SPN_INS_ARRSET, *dst, keyidx, validx);
		bytecode_append(&cmp->bc, &ins, 1);

		array_idx++;
		ast = ast->right;
	}

	/* clean up temporary registers */
	tmp_pop(cmp);
	tmp_pop(cmp);

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
		nameval.type = SPN_TYPE_STRING;
		nameval.v.o = ast->right->name;
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

/* XXX: when passed to this function, `*argc` should be initialized to zero!
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

		/* fill in the appropriate octet in the bytecode */
		wordidx = *argc / SPN_WORD_OCTETS;
		shift = 8 * (*argc % SPN_WORD_OCTETS);
		(*idc)[wordidx] |= (spn_uword)(dst) << shift;

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

		if (!isnumber(&op->value)) {
			compiler_error(
				cmp,
				ast->lineno,
				"unary minus applied to non-number literal",
				NULL
			);
			return 0;
		}

		negated = spn_ast_new(SPN_NODE_LITERAL, ast->lineno);

		if (isfloat(&op->value)) {
			negated->value = makefloat(-1.0 * floatvalue(&op->value));
		} else {
			negated->value = makeint(-1 * intvalue(&op->value));
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

static int compile_incdec_var(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	int idx;
	SpnValue varname;

	assert(ast->left->node == SPN_NODE_IDENT);

	varname.type = SPN_TYPE_STRING;
	varname.v.o = ast->left->name;
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

static int compile_incdec_arr(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	int arridx = -1, subidx = -1;
	enum spn_vm_ins opcode =
		ast->node == SPN_NODE_PREINCRMT
	     || ast->node == SPN_NODE_POSTINCRMT
			? SPN_INS_INC
			: SPN_INS_DEC;

	int nvars;

	SpnAST *lhs = ast->left;
	assert(lhs->node == SPN_NODE_ARRSUB || lhs->node == SPN_NODE_MEMBEROF);

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* compile array expression */
	if (compile_expr(cmp, lhs->left, &arridx) == 0) {
		return 0;
	}

	/* compile subscript expression */
	if (lhs->node == SPN_NODE_ARRSUB) { /* operator[] */
		if (compile_expr(cmp, lhs->right, &subidx) == 0) {
			return 0;
		}
	} else { /* member-of, "." or "->" */
		SpnValue nameval;
		nameval.type = SPN_TYPE_STRING;
		nameval.v.o = lhs->right->name;
		compile_string_literal(cmp, &nameval, &subidx);
	}

	switch (ast->node) {
	case SPN_NODE_PREINCRMT:
	case SPN_NODE_PREDECRMT: {
		/* these yield the already incremented/decremented value */
		spn_uword insns[3];
		insns[0] = SPN_MKINS_ABC(SPN_INS_ARRGET, *dst, arridx, subidx);
		insns[1] = SPN_MKINS_A(opcode, *dst);
		insns[2] = SPN_MKINS_ABC(SPN_INS_ARRSET, arridx, subidx, *dst);

		bytecode_append(&cmp->bc, insns, COUNT(insns));
		break;
	}
	case SPN_NODE_POSTINCRMT:
	case SPN_NODE_POSTDECRMT: {
		/* on the other hand, these operators yield the original
		 * (yet unmodified) value. For this, we need a temporary
		 * register to store the incremented/decremented value in.
		 */
		int tmpidx = tmp_push(cmp);

		spn_uword insns[4];
		insns[0] = SPN_MKINS_ABC(SPN_INS_ARRGET, *dst, arridx, subidx);
		insns[1] = SPN_MKINS_AB(SPN_INS_MOV, tmpidx, *dst);
		insns[2] = SPN_MKINS_A(opcode, tmpidx);
		insns[3] = SPN_MKINS_ABC(SPN_INS_ARRSET, arridx, subidx, tmpidx);

		bytecode_append(&cmp->bc, insns, COUNT(insns));
		tmp_pop(cmp);
		break;
	}
	default:
		SHANT_BE_REACHED();
	}

	/* once again, pop expired temporary values */
	nvars = rts_count(cmp->varstack);

	if (arridx >= nvars) {
		tmp_pop(cmp);
	}

	if (subidx >= nvars) {
		tmp_pop(cmp);
	}

	return 1;
}

static int compile_incdec(SpnCompiler *cmp, SpnAST *ast, int *dst)
{
	switch (ast->left->node) {
	case SPN_NODE_IDENT:
		return compile_incdec_var(cmp, ast, dst);

	case SPN_NODE_ARRSUB:
	case SPN_NODE_MEMBEROF:
		return compile_incdec_arr(cmp, ast, dst);

	default:
		compiler_error(
			cmp,
			ast->left->lineno,
			"argument of ++ and -- must be a variable or array member",
			NULL
		);
		return 0;
	}
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

	/* call-time argument count */
	case SPN_NODE_ARGC:		return compile_argc(cmp, ast, dst);

	/* function expression, lambda */
	case SPN_NODE_FUNCEXPR:		return compile_funcexpr(cmp, ast, dst);

	/* array literal */
	case SPN_NODE_ARRAY_LITERAL:	return compile_array_literal(cmp, ast, dst);

	/* array indexing */
	case SPN_NODE_ARRSUB:
	case SPN_NODE_MEMBEROF:		return compile_arrsub(cmp, ast, dst);

	/* function calls */
	case SPN_NODE_FUNCCALL:		return compile_call(cmp, ast, dst);

	/* unary plus just returns its argument verbatim */
	case SPN_NODE_UNPLUS:		return compile_expr(cmp, ast->left, dst);

	/* prefix unary operators without side effects */
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

