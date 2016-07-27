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
#include "parser.h"
#include "vm.h"
#include "str.h"
#include "array.h"
#include "hashmap.h"
#include "private.h"
#include "func.h"
#include "debug.h"


typedef struct Bytecode {
	spn_uword *insns;
	size_t len;
	size_t allocsz;
} Bytecode;

/* bidirectional hash table: maps indices to values and values to indices */
typedef struct RoundTripStore {
	SpnArray *fwd;    /* forward (index -> value) mapping */
	SpnHashMap *inv;  /* inverse (value -> index) mapping */
	int maxsize;      /* maximal size during lifetime     */
} RoundTripStore;

/* managing round-trip stores */
static void rts_init(RoundTripStore *rts);
static int rts_add(RoundTripStore *rts, SpnValue val);
static SpnValue rts_getval(RoundTripStore *rts, int idx); /* returns nil if not found */
static int rts_getidx(RoundTripStore *rts, SpnValue val); /* returns < 0 if not found */
static int rts_count(RoundTripStore *rts);
static void rts_delete_top(RoundTripStore *rts, int newsize);
static void rts_free(RoundTripStore *rts);

/* linked list for storing offsets and types
 * of 'break' and 'continue' statements
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
	SpnHashMap *name_to_index;
	/* weak pointer: maps upvalue indices to upvalue descriptors */
	SpnArray *index_to_desc;
	/* weak pointer: points to the enclosing function's variable stack */
	RoundTripStore *enclosing_varstack;
	/* strong pointer: link list, points to scope of enclosing function */
	struct UpvalChain *next;
} UpvalChain;

/* if you ever add a member to this structure, consider adding it to the
 * scope context info as well if necessary (and extend the 'save_function_scope()'
 * and 'restore_function_scope()' functions accordingly).
 */
struct SpnCompiler {
	Bytecode               bc;
	char                  *errmsg;
	int                    tmpidx;               /* (I)    */
	int                    nregs;                /* (II)   */
	RoundTripStore        *symtab;               /* (III)  */
	RoundTripStore        *varstack;             /* (IV)   */
	int                    parent_loop_varcount; /* (V)    */
	int                    parent_init_varcount; /* (VI)   */
	struct jump_stmt_list *jumplist;             /* (VII)  */
	int                    is_in_loop;           /* (VIII) */
	UpvalChain            *upval_chain;          /* (IX)   */
	SpnSourceLocation      error_loc;            /* (X)    */
	SpnHashMap            *debug_info;           /* (XI)   */
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
 * see the remark in the 'compile_funcdef()' function.)
 *
 * (III) - (IV): array of local symbols and stack of file-scope and local
 * (block-scope) variable names and the corresponding register indices
 *
 * (V): number of entries in 'varstack' in the enclosing (parent) loop
 * (if exists) of the scope currently being compiled. Necessary for
 * generating RAII-style cleanup code before a "break" or "continue"
 * statement for variables in the scope of the innermost loop.
 *
 * (VI): number of entries in 'varstack' in the enclosing (parent) loop
 * (if exists) of the current scope _after_ the initialization (in the
 * case of for and - later - while loops) but _before_ the loop body.
 * This is necessary as "break" statements should clean up all the
 * variables declared in the loop body _and_ in the initializer, whereas
 * "continue" statements should not touch the variables declared in the
 * initializer, they should only deallocate those defined in the body proper.
 *
 * (VII): link list for storing the offsets and types of unconditional
 * control flow statements 'break' and 'continue'
 *
 * (VIII): Boolean flag which is nonzero inside a loop and zero outside a
 * loop. It is used to limit the use of 'break' and 'continue' to loop bodies
 * (since it doesn't make sense to 'break' or 'continue' outside a loop).
 *
 * (IX): the UpvalChain link list forms a stack. Each node contains two
 * arrays: they contain the same 'spn_uword's (the "upvalue descriptors"),
 * that describe which variables in the scope of the containing function are
 * referred to as upvalues in the function corresponding to a given node.
 * The 'names_to_desc' array maps variable names to upvalue descriptors,
 * and is unordered; meanwhile, 'index_to_desc' is sorted. The former array
 * is used for looking up the external local variables _during_ compilation,
 * the latter is used for generating the SPN_INS_CLOSURE instruction _after_
 * the compilation of the function body.
 *
 * (X): error_loc is the location information for the AST node for which
 * a compiler error occurred.
 *
 * (XI): the debug information maps bytecode addresses to line and character
 * numbers, and register numbers to variable names.
 * This member may be NULL, in which case no debug information is emitted.
 */

/* information describing the state of the global scope or a function scope.
 * this has to be preserved (saved and restored) across the compilation of
 * function bodies.
 */
typedef struct FunctionScopeInfo {
	int                    tmpidx;
	int                    nregs;
	RoundTripStore        *varstack;
	int                    parent_loop_varcount;
	int                    parent_init_varcount;
	struct jump_stmt_list *jumplist;
	int                    is_in_loop;
} FunctionScopeInfo;

static void save_function_scope(SpnCompiler *cmp, FunctionScopeInfo *sci)
{
	sci->tmpidx               = cmp->tmpidx;
	sci->nregs                = cmp->nregs;
	sci->varstack             = cmp->varstack;
	sci->parent_loop_varcount = cmp->parent_loop_varcount;
	sci->parent_init_varcount = cmp->parent_init_varcount;
	sci->jumplist             = cmp->jumplist;
	sci->is_in_loop           = cmp->is_in_loop;
}

static void restore_function_scope(SpnCompiler *cmp, FunctionScopeInfo *sci)
{
	cmp->tmpidx               = sci->tmpidx;
	cmp->nregs                = sci->nregs;
	cmp->varstack             = sci->varstack;
	cmp->parent_loop_varcount = sci->parent_loop_varcount;
	cmp->parent_init_varcount = sci->parent_init_varcount;
	cmp->jumplist             = sci->jumplist;
	cmp->is_in_loop           = sci->is_in_loop;
}

/* Structure representing whether the compilation is inside a loop body,
 * and other loop-related information such as the number of locals in
 * the enclosing loop (if any) of the current scope, and the list of
 * "break" and "continue" jumps in the form of a jumplist to be fixed up.
 */
typedef struct LoopState {
	int                    parent_loop_varcount;
	int                    parent_init_varcount;
	struct jump_stmt_list *jumplist;
	int                    is_in_loop;
} LoopState;

static void save_loop_state(SpnCompiler *cmp, LoopState *ls)
{
	ls->parent_loop_varcount = cmp->parent_loop_varcount;
	ls->parent_init_varcount = cmp->parent_init_varcount;
	ls->jumplist             = cmp->jumplist;
	ls->is_in_loop           = cmp->is_in_loop;
}

static void restore_loop_state(SpnCompiler *cmp, LoopState *ls)
{
	cmp->parent_loop_varcount = ls->parent_loop_varcount;
	cmp->parent_init_varcount = ls->parent_init_varcount;
	cmp->jumplist             = ls->jumplist;
	cmp->is_in_loop           = ls->is_in_loop;
}

static void init_loop_state_no_parent(SpnCompiler *cmp)
{
	cmp->parent_loop_varcount = -1; /* < 0 means 'no loops in any parent scope */
	cmp->parent_init_varcount = -1;
	cmp->jumplist = NULL;
	cmp->is_in_loop = 0;
}

static void init_loop_state_in_parent(SpnCompiler *cmp)
{
	cmp->parent_loop_varcount = rts_count(cmp->varstack);
	cmp->parent_init_varcount = rts_count(cmp->varstack);
	cmp->jumplist = NULL;
	cmp->is_in_loop = 1;
}

/*****************************
 * Symbol table entry class. *
 *****************************/

/* This class is used for fixing the ugly hack that was previously used in
 * the code of the compiler. Namely, symbol table entries were represented
 * in a quite, khm, "particular" way inside the SpnCompiler struct:
 * - String literals were stored as SpnStrings (that's fine);
 * - Stubs that reference global symbols were stored as SpnValues of
 *	function type, with their name set to the symbol name. This is
 * 	conceptually wrong because global symbols can be of any type, not
 * 	just functions.
 * - Symbols corresponding to unnamed lambdas in the current translation unit
 * 	were stored as integers (SpnValue of type SPN_TYPE_INT), since an
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
	SPN_CLASS_UID_SYMTABENTRY,
	"SymtabEntry",
	symtabentry_equal,
	NULL,
	symtabentry_hash,
	NULL, /* TODO: implement description */
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

/* A structure representing the range of registers [first, plast).
 * Used for describing the target of of RAII-style cleanup operations.
 */
typedef struct RegisterRange {
	int first; /* first register to clean up */
	int plast; /* past-the-end, exclusive    */
} RegisterRange;

/****************************************/

/* compile_*() functions return nonzero on success, 0 on error */
static int compile(SpnCompiler *cmp, SpnHashMap *ast);

static int compile_program(SpnCompiler *cmp, SpnHashMap *ast);
static int compile_block(SpnCompiler *cmp, SpnHashMap *ast);
static int compile_funcdef(SpnCompiler *cmp, SpnHashMap *ast, int *symidx, SpnArray *upvalues);
static int compile_while(SpnCompiler *cmp, SpnHashMap *ast);
static int compile_do(SpnCompiler *cmp, SpnHashMap *ast);
static int compile_for(SpnCompiler *cmp, SpnHashMap *ast);
static int compile_if(SpnCompiler *cmp, SpnHashMap *ast);

static int compile_loop_jump(SpnCompiler *cmp, SpnHashMap *ast); /* break & continue */
static int compile_vardecl(SpnCompiler *cmp, SpnHashMap *ast);
static int compile_const(SpnCompiler *cmp, SpnHashMap *ast);
static int compile_return(SpnCompiler *cmp, SpnHashMap *ast);
static int compile_empty(SpnCompiler *cmp, SpnHashMap *ast);

/* compile and load string literal */
static void compile_string_literal(SpnCompiler *cmp, SpnValue str, int *dst);

/* 'dst' is a pointer to 'int' that will be filled with the index of the
 * destination register (i. e. the one holding the result of the expression)
 * pass 'NULL' if you don't need this information (e. g. when an expression
 * is merely evaluated for its side effects).
 * 'cleanup_range' is a pointer that will contain, upon successful return,
 * the range of temporaries that were used for compiling 'ast'.
 * Upon failure, the pointed object is not touched. Pass NULL if you do not
 * need this information.
 */
static int compile_expr_toplevel(
	SpnCompiler *cmp,
	SpnHashMap *ast,
	int *dst,
	RegisterRange *temp_range
);

/* dst is the preferred destination register index. Pass a pointer to
 * a non-negative 'int' to force the function to emit an instruction
 * of which the destination register is '*dst'. If the integer pointed
 * to by 'dst' is initially negative, then the function decides which
 * register to use as the destination, then sets '*dst' accordingly.
 */
static int compile_expr(SpnCompiler *cmp, SpnHashMap *ast, int *dst);

/* takes a printf-like format string */
static void compiler_error(SpnCompiler *cmp, SpnHashMap *ast, const char *fmt, const void *args[]);

/* quick and dirty integer maximum function */
static int max(int x, int y)
{
	return x > y ? x : y;
}

/* Helper functions for walking the AST and
 * obtaining various properties thereof along the way
 */
static SpnString *ast_get_string(SpnHashMap *ast, const char *key)
{
	SpnValue value = spn_hashmap_get_strkey(ast, key);
	assert(isstring(&value));
	return stringvalue(&value);
}

static SpnString *ast_get_string_optional(SpnHashMap *ast, const char *key)
{
	SpnValue value = spn_hashmap_get_strkey(ast, key);
	assert(isstring(&value) || isnil(&value));
	return isstring(&value) ? stringvalue(&value) : NULL;
}

static SpnArray *ast_get_array(SpnHashMap *ast, const char *key)
{
	SpnValue value = spn_hashmap_get_strkey(ast, key);
	assert(isarray(&value));
	return arrayvalue(&value);
}

/* used for getting the type of an AST node.
 * Returns a (non-owning) pointer to the type string inside the AST node.
 */
static const char *ast_get_type(SpnHashMap *ast)
{
	SpnString *typestr = ast_get_string(ast, "type");
	return typestr->cstr;
}

/* returns nonzero if the two node type strings are equal, and zero otherwise */
static int type_equal(const char *p, const char *q)
{
	return strcmp(p, q) == 0;
}

static SpnArray *ast_get_children(SpnHashMap *ast)
{
	return ast_get_array(ast, "children");
}

static SpnHashMap *ast_get_nth_child(SpnArray *children, size_t index)
{
	SpnValue child_val = spn_array_get(children, index);
	assert(ishashmap(&child_val));
	return hashmapvalue(&child_val);
}

static SpnHashMap *ast_get_child_byname(SpnHashMap *ast, const char *key)
{
	SpnValue child = spn_hashmap_get_strkey(ast, key);
	assert(ishashmap(&child));
	return hashmapvalue(&child);
}

static SpnHashMap *ast_get_child_byname_optional(SpnHashMap *ast, const char *key)
{
	SpnValue child = spn_hashmap_get_strkey(ast, key);
	assert(ishashmap(&child) || isnil(&child));
	return ishashmap(&child) ? hashmapvalue(&child) : NULL;
}

static SpnHashMap *ast_shallow_copy(SpnHashMap *ast)
{
	size_t cursor = 0;
	SpnValue key, val;
	SpnHashMap *dup = spn_hashmap_new();

	while ((cursor = spn_hashmap_next(ast, cursor, &key, &val)) != 0) {
		spn_hashmap_set(dup, &key, &val);
	}

	return dup;
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
static void bytecode_init(Bytecode *bc);
static void bytecode_append(Bytecode *bc, spn_uword *words, size_t n);
static void append_cstring(Bytecode *bc, const char *str, size_t len);

SpnCompiler *spn_compiler_new(void)
{
	SpnCompiler *cmp = spn_malloc(sizeof(*cmp));

	init_loop_state_no_parent(cmp);

	cmp->errmsg = NULL;
	cmp->upval_chain = NULL;
	cmp->error_loc.line = 0;
	cmp->error_loc.column = 0;

	return cmp;
}

void spn_compiler_free(SpnCompiler *cmp)
{
	free(cmp->errmsg);
	free(cmp);
}

SpnFunction *spn_compiler_compile(SpnCompiler *cmp, SpnHashMap *ast, int debug)
{
	bytecode_init(&cmp->bc);
	cmp->debug_info = debug ? spn_dbg_new() : NULL;

	if (compile_program(cmp, ast)) {
		/* should be at global scope when compilation is done */
		assert(cmp->upval_chain == NULL);
		assert(cmp->parent_loop_varcount < 0);
		assert(cmp->parent_init_varcount < 0);
		assert(cmp->jumplist == NULL);
		assert(cmp->is_in_loop == 0);

		/* success. transfer ownership of cmp->bc.insns
		 * and that of cmp->debug_info to the result.
		 */
		return spn_func_new_topprg(SPN_TOPFN, cmp->bc.insns, cmp->bc.len, cmp->debug_info);
	}

	/* error, clean up */
	free(cmp->bc.insns);

	if (cmp->debug_info) {
		spn_object_release(cmp->debug_info);
	}

	return NULL;
}

const char *spn_compiler_errmsg(SpnCompiler *cmp)
{
	return cmp->errmsg;
}

SpnSourceLocation spn_compiler_errloc(SpnCompiler *cmp)
{
	return cmp->error_loc;
}

/* Map AST node types to virtual machine instruction opcodes */
typedef struct NodeAndOpcode {
	const char *type;
	enum spn_vm_ins opcode;
} NodeAndOpcode;

static enum spn_vm_ins node_to_opcode(const NodeAndOpcode opcode_map[], size_t size, const char *type)
{
	size_t i;
	for (i = 0; i < size; i++) {
		if (type_equal(opcode_map[i].type, type)) {
			return opcode_map[i].opcode;
		}
	}

	SHANT_BE_REACHED();
	return -1;
}

/* Functions for working with upvalues
 * the 'upvalues' argument of upval_chain_push() is an already existing,
 * empty array, which will be filled by 'compile_funcdef()'.
 */
static void upval_chain_push(SpnCompiler *cmp, SpnArray *upvalues)
{
	UpvalChain *node = spn_malloc(sizeof(*node));

	node->name_to_index = spn_hashmap_new();
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
static int search_upvalues(UpvalChain *node, SpnValue name)
{
	int enclosing_varidx, enclosing_upvalidx, flat_upvalidx;
	spn_uword flat_upvaldesc;
	SpnValue upval_idx_val, flat_upvalidx_val, flat_upvaldesc_val;

	if (node == NULL) {
		return -1;
	}

	assert(isstring(&name));

	/* first, search in the closure of the current function */
	upval_idx_val = spn_hashmap_get(node->name_to_index, &name);

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
		int upval_idx = spn_hashmap_count(node->name_to_index);
		SpnValue new_idx_val = makeint(upval_idx);

		/* the two upvalue arrays should contain the same
		 * entries, therefore their sizes must be equal.
		 */
		assert(spn_array_count(node->index_to_desc) == upval_idx);

		spn_hashmap_set(node->name_to_index, &name, &new_idx_val);
		spn_array_push(node->index_to_desc, &upval_desc_val);

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
	flat_upvalidx = spn_hashmap_count(node->name_to_index);
	flat_upvalidx_val = makeint(flat_upvalidx);

	assert(spn_array_count(node->index_to_desc) == flat_upvalidx);

	flat_upvaldesc = SPN_MKINS_A(SPN_UPVAL_OUTER, enclosing_upvalidx);
	flat_upvaldesc_val = makeint(flat_upvaldesc);

	spn_hashmap_set(node->name_to_index, &name, &flat_upvalidx_val);
	spn_array_push(node->index_to_desc, &flat_upvaldesc_val);

	return flat_upvalidx;
}

/* bytecode manipulation */
static void bytecode_init(Bytecode *bc)
{
	bc->insns = NULL;
	bc->len = 0;
	bc->allocsz = 0;
}

static void bytecode_append(Bytecode *bc, spn_uword *words, size_t n)
{
	if (bc->allocsz < bc->len + n) {
		if (bc->allocsz == 0) {
			bc->allocsz = 0x40;
		}

		while (bc->allocsz < bc->len + n) {
			bc->allocsz *= 2;
		}

		bc->insns = spn_realloc(bc->insns, bc->allocsz * sizeof bc->insns[0]);
	}

	memcpy(bc->insns + bc->len, words, n * sizeof bc->insns[0]);
	bc->len += n;
}

/* this function appends a 0-terminated array of characters to the bytecode
 * in the compiler. The number of characters (including the NUL terminator) is
 * of course padded with zeroes to the nearest multiple of sizeof(spn_uword).
 */
static void append_cstring(Bytecode *bc, const char *str, size_t len)
{
	size_t nwords = ROUNDUP(len + 1, sizeof(spn_uword));
	size_t padded_len = nwords * sizeof(spn_uword);

	spn_uword *buf = spn_malloc(padded_len);

	/* this relies on the fact that 'strncpy()' pads with NUL characters
	 * when strlen(src) < sizeof(dest)
	 */
	strncpy((char *)(buf), str, padded_len);

	bytecode_append(bc, buf, nwords);
	free(buf);
}

/* Helpers for emitting single-word instructions */
static void emit_ins_void(SpnCompiler *cmp, enum spn_vm_ins opcode)
{
	spn_uword ins = SPN_MKINS_VOID(opcode);
	bytecode_append(&cmp->bc, &ins, 1);
}

static void emit_ins_A(SpnCompiler *cmp, enum spn_vm_ins opcode,
	unsigned char a)
{
	spn_uword ins = SPN_MKINS_A(opcode, a);
	bytecode_append(&cmp->bc, &ins, 1);
}

static void emit_ins_AB(SpnCompiler *cmp, enum spn_vm_ins opcode,
	unsigned char a, unsigned char b)
{
	spn_uword ins = SPN_MKINS_AB(opcode, a, b);
	bytecode_append(&cmp->bc, &ins, 1);
}

static void emit_ins_ABC(SpnCompiler *cmp, enum spn_vm_ins opcode,
	unsigned char a, unsigned char b, unsigned char c)
{
	spn_uword ins = SPN_MKINS_ABC(opcode, a, b, c);
	bytecode_append(&cmp->bc, &ins, 1);
}

static void emit_ins_mid(SpnCompiler *cmp, enum spn_vm_ins opcode,
	unsigned char a, unsigned short b)
{
	spn_uword ins = SPN_MKINS_MID(opcode, a, b);
	bytecode_append(&cmp->bc, &ins, 1);
}

static void emit_ins_long(SpnCompiler *cmp, enum spn_vm_ins opcode,
	spn_uword a)
{
	spn_uword ins = SPN_MKINS_LONG(opcode, a);
	bytecode_append(&cmp->bc, &ins, 1);
}

/* These are overloads with opcode of type 'enum spn_local_symbol',
 * so that the compiler leaves us alone.
 * Also, it is clearer that we are messing around with the local symbol table
 * using these functions (which is data rather than executable code).
 */
static void emit_symtab_entry_void(SpnCompiler *cmp, enum spn_local_symbol opcode)
{
	spn_uword ins = SPN_MKINS_VOID(opcode);
	bytecode_append(&cmp->bc, &ins, 1);
}

static void emit_symtab_entry_long(SpnCompiler *cmp, enum spn_local_symbol opcode,
	spn_uword a)
{
	spn_uword ins = SPN_MKINS_LONG(opcode, a);
	bytecode_append(&cmp->bc, &ins, 1);
}

/* cleans up the range of temporary registers and/or registers of
 * locals fallen out of scope by storing nil into them.
 */
static void cleanup_registers(SpnCompiler *cmp, RegisterRange range)
{
	/* do not emit redundant / no-op cleanup instruction */
	if (range.first < range.plast) {
		emit_ins_AB(cmp, SPN_INS_CLEAN, range.first, range.plast);
	}
}


/* round-trip stores */
static void rts_init(RoundTripStore *rts)
{
	rts->fwd = spn_array_new();
	rts->inv = spn_hashmap_new();
	rts->maxsize = 0;
}

static int rts_add(RoundTripStore *rts, SpnValue val)
{
	int idx = rts_count(rts);
	int newsize;

	/* insert element into last place */
	SpnValue idxval = makeint(idx);
	spn_array_push(rts->fwd, &val);
	spn_hashmap_set(rts->inv, &val, &idxval);

	/* keep track of maximal size of the RTS */
	newsize = rts_count(rts);
	if (newsize > rts->maxsize) {
		rts->maxsize = newsize;
	}

	return idx;
}

static SpnValue rts_getval(RoundTripStore *rts, int idx)
{
	return spn_array_get(rts->fwd, idx);
}

static int rts_getidx(RoundTripStore *rts, SpnValue val)
{
	SpnValue res = spn_hashmap_get(rts->inv, &val);
	return isnum(&res) ? intvalue(&res) : -1;
}

static int rts_count(RoundTripStore *rts)
{
	size_t n = spn_array_count(rts->fwd);

	/* petit sanity check */
	assert(n == spn_hashmap_count(rts->inv));

	return n;
}

/* removes the last elements so that only the first 'newsize' ones remain */
static void rts_delete_top(RoundTripStore *rts, int newsize)
{
	int oldsize = rts_count(rts);
	int i;

	/* because nothing else would make sense */
	assert(newsize <= oldsize);

	/* we have to go backwards so that spn_array_get() doesn't
	 * get confused by the gradually shrinking array...
	 */
	for (i = oldsize; i > newsize; i--) {
		SpnValue val = spn_array_get(rts->fwd, i - 1);
		assert(notnil(&val));

		spn_array_pop(rts->fwd);
		spn_hashmap_delete(rts->inv, &val);
	}

	assert(rts_count(rts) == newsize);
}

static void rts_free(RoundTripStore *rts)
{
	spn_object_release(rts->fwd);
	spn_object_release(rts->inv);
}


static void compiler_error(SpnCompiler *cmp, SpnHashMap *ast, const char *fmt, const void *args[])
{
	SpnValue line = spn_hashmap_get_strkey(ast, "line");
	SpnValue column = spn_hashmap_get_strkey(ast, "column");

	assert(isint(&line));
	assert(isint(&column));

	cmp->error_loc.line = intvalue(&line);
	cmp->error_loc.column = intvalue(&column);

	free(cmp->errmsg);
	cmp->errmsg = spn_string_format_cstr(fmt, NULL, args);
}

/* this assumes an expression statement if the node is an expression,
 * so it doesn't return the destination register index. DO NOT use this
 * if the resut of an expression shall be known.
 */
static int compile(SpnCompiler *cmp, SpnHashMap *ast)
{
	size_t i;
	const char *nodetype = ast_get_type(ast);
	RegisterRange temp_range;

	static const struct {
		const char *node;
		int (*fn)(SpnCompiler *, SpnHashMap *);
	} compilers[] = {
		{ "block",     compile_block     },
		{ "if",        compile_if        },
		{ "for",       compile_for       },
		{ "while",     compile_while     },
		{ "do",        compile_do        },
		{ "return",    compile_return    },
		{ "vardecl",   compile_vardecl   },
		{ "constdecl", compile_const     },
		{ "break",     compile_loop_jump },
		{ "continue",  compile_loop_jump },
		{ "empty",     compile_empty     }
	};

	assert(ast != NULL);

	/* My benchmarking result: for a dozen elements, linear search
	 * and 'strcmp()' is ~14% faster than looking up in an SpnHashMap
	 */
	for (i = 0; i < COUNT(compilers); i++) {
		if (type_equal(compilers[i].node, nodetype)) {
			return compilers[i].fn(cmp, ast);
		}
	}

	/* if the node was neither of the known statements, assume an expression */
	if (compile_expr_toplevel(cmp, ast, NULL, &temp_range)) {
		cleanup_registers(cmp, temp_range);
		return 1;
	}

	return 0;
}

/* returns zero on success, nonzero on error */
static int write_symtab(SpnCompiler *cmp)
{
	int i, nsyms = rts_count(cmp->symtab);

	for (i = 0; i < nsyms; i++) {
		SpnValue sym = rts_getval(cmp->symtab, i);

		assert(isobject(&sym));
		switch (classuid(&sym)) {
		case SPN_CLASS_UID_STRING: {
			/* string literal */

			SpnString *str = stringvalue(&sym);

			/* append symbol type and length description */
			emit_symtab_entry_long(cmp, SPN_LOCSYM_STRCONST, str->len);

			/* append actual 0-terminated string */
			append_cstring(&cmp->bc, str->cstr, str->len);
			break;
		}
		case SPN_CLASS_UID_SYMTABENTRY: {
			SymtabEntry *entry = objvalue(&sym);

			switch (entry->type) {
			case SYMTABENTRY_GLOBAL: {
				SpnString *name = entry->name;

				/* append symbol type and name length */
				emit_symtab_entry_long(cmp, SPN_LOCSYM_SYMSTUB, name->len);

				/* append symbol name */
				append_cstring(&cmp->bc, name->cstr, name->len);

				break;
			}
			case SYMTABENTRY_FUNCTION: {
				SpnString *nobj = entry->name;
				spn_uword offset = entry->offset;
				spn_uword namelen = nobj ? nobj->len : strlen(SPN_LAMBDA_NAME);
				const char *name = nobj ? nobj->cstr : SPN_LAMBDA_NAME;

				/* append symbol type */
				emit_symtab_entry_void(cmp, SPN_LOCSYM_FUNCDEF);

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
				args[0] = spn_value_type_name(&sym);
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

static int compile_children(SpnCompiler *cmp, SpnHashMap *root)
{
	SpnArray *children = ast_get_children(root);
	size_t n_children = spn_array_count(children);

	size_t i;
	for (i = 0; i < n_children; i++) {
		SpnHashMap *child = ast_get_nth_child(children, i);

		if (compile(cmp, child) == 0) {
			return 0;
		}
	}

	return 1;
}

static int compile_program(SpnCompiler *cmp, SpnHashMap *ast)
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

	/* no parent loop (nothing encloses the top-level program) */
	init_loop_state_no_parent(cmp);

	/* compile children; on error, clean up and return error */
	if (compile_children(cmp, ast) == 0) {
		rts_free(&symtab);
		rts_free(&glbvars);
		return 0;
	}

	/* We don't need to explicitly clean up registers corresponding
	 * to variables declared at the top-level scope: when this scope
	 * ends, the program returns and everything is released automatically.
	 *
	 * cleanup_registers(cmp, 0, rts_count(cmp->varstack));
	 */

	/* unconditionally append 'return nil;', just in case */
	append_return_nil(cmp);

	/* since 'cmp->nregs' is only set if temporary variables are used at
	 * least once during compilation (i. e. if there's an expression that
	 * needs temporary registers), it may contain zero even if more than
	 * zero registers are necessary (for storing local variables).
	 * So, we pick the maximum of the number of global variables and the
	 * number of registers stored in 'cmp->nregs'.
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
			ast,
			"too many registers in top-level program",
			NULL
		);
		return 0;
	}

	return 1;
}

static int compile_block(SpnCompiler *cmp, SpnHashMap *ast)
{
	/* block -> new lexical scope, "push" a new set of variable names on
	 * the stack. This is done by keeping track of the current length of
	 * the variable stack and removing the topmost names when compilation
	 * of the block is complete, until only the originally present names
	 * remain in the stack.
	 */
	int old_stack_size = rts_count(cmp->varstack);
	int new_stack_size;

	/* compile children */
	int success = compile_children(cmp, ast);

	/* now release the local variables declared in this block,
	 * and remove them from the variable stack too.
	 */
	RegisterRange locals_range;

	new_stack_size = rts_count(cmp->varstack);
	assert(old_stack_size <= new_stack_size);

	locals_range.first = old_stack_size;
	locals_range.plast = new_stack_size;

	cleanup_registers(cmp, locals_range);
	rts_delete_top(cmp->varstack, old_stack_size);

	return success;
}

static int compile_funcdef(SpnCompiler *cmp, SpnHashMap *ast, int *symidx, SpnArray *upvalues)
{
	int regcount;
	spn_uword fnhdr[SPN_FUNCHDR_LEN] = { 0 };
	size_t hdroff, bodylen, argc, i;

	SymtabEntry *entry;
	RoundTripStore vs_this;
	FunctionScopeInfo sci;
	SpnValue offval;

	/* obtain argument list, (optional) function name and body */
	SpnArray *declargs = ast_get_array(ast, "declargs");
	SpnString *funcname = ast_get_string_optional(ast, "name");
	SpnHashMap *body = ast_get_child_byname(ast, "body");

	/* self-examination (transitions are hard) */
	assert(type_equal(ast_get_type(ast), "function"));

	/* The 'upval_chain_push()' function must be called BEFORE we replace
	 * 'cmp->varstack' with our own, new variable stack, because the head
	 * of the upvalue chain needs to refer to the scope of the enclosing
	 * function; otherwise, 'search_upvalues()' function would not really
	 * search the upvalues of the current function, but rather its locals.
	 */
	upval_chain_push(cmp, upvalues);

	/* save scope context data: local variables, temporary register stack
	 * pointer, maximal number of registers, innermost loop offset
	 * and number of variables in the enclosing loop (if any).
	 */
	save_function_scope(cmp, &sci);

	/* init new local variable stack and register counter */
	rts_init(&vs_this);
	cmp->varstack = &vs_this;
	cmp->nregs = 0;

	/* loop control flow statements across function boundaries don't make sense */
	init_loop_state_no_parent(cmp);

	/* emit 'SPN_INS_FUNCTION' to bytecode */
	emit_ins_void(cmp, SPN_INS_FUNCTION);

	/* save the offset of the function header */
	hdroff = cmp->bc.len;

	/* write stub function header */
	bytecode_append(&cmp->bc, fnhdr, SPN_FUNCHDR_LEN);

	/* create a local symtab entry for the function */
	entry = symtabentry_new_function(hdroff, funcname);
	offval = makeobject(entry);
	*symidx = rts_add(cmp->symtab, offval);
	spn_object_release(entry);

	/* bring each declared argument in scope */
	argc = spn_array_count(declargs);

	for (i = 0; i < argc; i++) {
		SpnValue argname = spn_array_get(declargs, i);
		assert(isstring(&argname));

		/* check for double declaration of an argument */
		if (rts_getidx(cmp->varstack, argname) < 0) {
			rts_add(cmp->varstack, argname);
		} else {
			/* on error, free local var stack and restore scope context */
			const void *args[1];
			args[0] = stringvalue(&argname)->cstr;
			compiler_error(cmp, ast, "argument '%s' already declared", args);

			rts_free(&vs_this);
			restore_function_scope(cmp, &sci);
			upval_chain_pop(cmp);

			return 0;
		}
	}

	/* compile body */
	if (compile(cmp, body) == 0) {
		rts_free(&vs_this);
		restore_function_scope(cmp, &sci);
		upval_chain_pop(cmp);

		return 0;
	}

	/* unconditionally append 'return nil;' at the end */
	append_return_nil(cmp);

	/* 'max()' is called for the same reason it is called in the
	 * 'compile_program()' function (see the explanation there)
	 */
	regcount = max(cmp->nregs, cmp->varstack->maxsize);

	bodylen = cmp->bc.len - (hdroff + SPN_FUNCHDR_LEN);

	/* fill in now-available function header information */
	cmp->bc.insns[hdroff + SPN_FUNCHDR_IDX_BODYLEN] = bodylen;
	cmp->bc.insns[hdroff + SPN_FUNCHDR_IDX_ARGC]    = argc;
	cmp->bc.insns[hdroff + SPN_FUNCHDR_IDX_NREGS]   = regcount;
	cmp->bc.insns[hdroff + SPN_FUNCHDR_IDX_SYMCNT]  = 0; /* unused anyway */

	/* free local var stack, restore scope context data */
	rts_free(&vs_this);
	restore_function_scope(cmp, &sci);
	upval_chain_pop(cmp);

	if (regcount > MAX_REG_FRAME) {
		compiler_error(
			cmp,
			ast,
			"too many registers in function",
			NULL
		);
		return 0;
	}

	return 1;
}

/* helper function for filling in jump list (list of 'break' and 'continue'
 * statements) in a while, do-while or for loop.
 *
 * 'off_end' is the offset after the whole loop, where control flow should be
 * transferred by 'break'. 'off_cond' is the offset of the condition (or
 * that of the incrementing expression in the case of 'for' loops) where a
 * 'continue' statement should transfer the control flow.
 *
 * This function also frees the link list on the fly.
 */
static void fix_and_free_jump_list(SpnCompiler *cmp, spn_sword off_end, spn_sword off_cond)
{
	struct jump_stmt_list *hdr = cmp->jumplist;
	while (hdr != NULL) {
		struct jump_stmt_list *tmp = hdr->next;

		/* 'break' jumps to right after the end of the body,
		 * whereas 'continue' jumps back to the condition
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

static int compile_while(SpnCompiler *cmp, SpnHashMap *ast)
{
	int cndidx = -1;
	spn_uword ins[2] = { 0 }; /* stub */
	spn_sword off_cond, off_cndjmp, off_body, off_jmpback, off_end;

	SpnHashMap *condition = ast_get_child_byname(ast, "cond");
	SpnHashMap *body = ast_get_child_byname(ast, "body");

	RegisterRange temp_range;

	/* save old loop state */
	LoopState loop_state;
	save_loop_state(cmp, &loop_state);

	/* set up new loop state */
	init_loop_state_in_parent(cmp);

	/* save offset of condition */
	off_cond = cmp->bc.len;

	/* compile condition
	 * on error, clean up, restore loop state
	 * no need to free jump list -- it's empty so far
	 */
	if (compile_expr_toplevel(cmp, condition, &cndidx, &temp_range) == 0) {
		restore_loop_state(cmp, &loop_state);
		return 0;
	}

	off_cndjmp = cmp->bc.len;

	/* Once variable declarations in the condition of 'while' loops
	 * are permitted, here we should insert code that sets the
	 * cmp->parent_init_varcount variable to rts_count(cmp->varstack),
	 * so that "continue" statements won't erroneously nil out
	 * variables declared in the loop header.
	 */

	/* append jump over the loop body if condition is false (stub) */
	bytecode_append(&cmp->bc, ins, COUNT(ins));

	off_body = cmp->bc.len;

	/* compile loop body */
	if (compile(cmp, body) == 0) {
		/* clean up and restore loop state */
		free_jumplist(cmp->jumplist);
		restore_loop_state(cmp, &loop_state);
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

	/* clean up temporary registers used for evaluating the condition */
	cleanup_registers(cmp, temp_range);

	/* restore original loop state */
	restore_loop_state(cmp, &loop_state);

	return 1;
}

static int compile_do(SpnCompiler *cmp, SpnHashMap *ast)
{
	spn_sword off_body = cmp->bc.len;
	spn_sword off_jmp, off_cond, off_end, diff;
	spn_uword ins[2];
	int reg = -1;

	SpnHashMap *condition = ast_get_child_byname(ast, "cond");
	SpnHashMap *body = ast_get_child_byname(ast, "body");

	RegisterRange temp_range;

	/* save old loop state */
	LoopState loop_state;
	save_loop_state(cmp, &loop_state);

	/* set up new loop state */
	init_loop_state_in_parent(cmp);

	/* compile body; clean up jump list on error */
	if (compile(cmp, body) == 0) {
		free_jumplist(cmp->jumplist);
		restore_loop_state(cmp, &loop_state);
		return 0;
	}

	off_cond = cmp->bc.len;

	/* compile condition, clean up jump list on error */
	if (compile_expr_toplevel(cmp, condition, &reg, &temp_range) == 0) {
		free_jumplist(cmp->jumplist);
		restore_loop_state(cmp, &loop_state);
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

	/* clean up temporary registers used for evaluating the condition */
	cleanup_registers(cmp, temp_range);

	/* restore original loop state */
	restore_loop_state(cmp, &loop_state);

	return 1;
}

static int compile_for(SpnCompiler *cmp, SpnHashMap *ast)
{
	int regidx = -1;
	spn_sword off_cond, off_incmt, off_body_begin, off_body_end, off_cond_jmp, off_uncd_jmp;
	spn_uword jmpins[2] = { 0 }; /* dummy */

	SpnHashMap *init = ast_get_child_byname(ast, "init");
	SpnHashMap *cond = ast_get_child_byname(ast, "cond");
	SpnHashMap *icmt = ast_get_child_byname(ast, "increment");
	SpnHashMap *body = ast_get_child_byname(ast, "body");

	RegisterRange init_var_range, cond_temp_range, icmt_temp_range;

	/* save old loop state */
	LoopState loop_state;
	save_loop_state(cmp, &loop_state);

	/* Set up new loop state. Doing this _before_ compiling the initializer
	 * makes 'break' magically (and correctly) free the variables declared in
	 * the initializer when a 'break' statement is encountered.
	 */
	init_loop_state_in_parent(cmp);

	/* we want that the scope of variables declared in the initialization
	 * be limited to the loop body, so here we save the variable stack size
	 */
	init_var_range.first = rts_count(cmp->varstack);

	/* compile initialization ouside the loop;
	 * restore jump list on error (no need to free, here it's empty)
	 * 'compile()' is used instead of 'compile_expr_toplevel()'
	 * because 'init' may be an expression or a variable declaration
	 */
	if (compile(cmp, init) == 0) {
		restore_loop_state(cmp, &loop_state);
		return 0;
	}

	/* store number of local variables between initialization and the
	 * loop body, so that "continue" statements and cleanup work properly.
	 */
	init_var_range.plast = rts_count(cmp->varstack);

	cmp->parent_init_varcount = init_var_range.plast;
	assert(cmp->parent_init_varcount >= cmp->parent_loop_varcount);

	/* compile condition, clean up on error likewise */
	off_cond = cmp->bc.len;
	if (compile_expr_toplevel(cmp, cond, &regidx, &cond_temp_range) == 0) {
		restore_loop_state(cmp, &loop_state);
		return 0;
	}

	/* compile "skip body if condition is false" jump */
	off_cond_jmp = cmp->bc.len;
	bytecode_append(&cmp->bc, jmpins, COUNT(jmpins));

	/* compile body */
	off_body_begin = cmp->bc.len;
	if (compile(cmp, body) == 0) {
		free_jumplist(cmp->jumplist);
		restore_loop_state(cmp, &loop_state);
		return 0;
	}

	/* compile incrementing expression */
	off_incmt = cmp->bc.len;
	if (compile_expr_toplevel(cmp, icmt, NULL, &icmt_temp_range) == 0) {
		free_jumplist(cmp->jumplist);
		restore_loop_state(cmp, &loop_state);
		return 0;
	}

	/* clean up temporaries used by incrementing expression */
	cleanup_registers(cmp, icmt_temp_range);

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

	/* clen up temporary registers used for evaluating the condition */
	cleanup_registers(cmp, cond_temp_range);

	/* get rid of variables declared in the initialization of the loop */
	cleanup_registers(cmp, init_var_range);
	rts_delete_top(cmp->varstack, init_var_range.first);

	/* restore loop state */
	restore_loop_state(cmp, &loop_state);

	return 1;
}

static int compile_if(SpnCompiler *cmp, SpnHashMap *ast)
{
	spn_sword off_then, off_else, off_jze_b4_then, off_jmp_b4_else;
	spn_sword len_then, len_else;
	spn_uword ins[2] = { 0 };
	int condidx = -1;

	/* the else-branch might not exist, hence 'ast_get_child_byname_optional' */
	SpnHashMap *cond = ast_get_child_byname(ast, "cond");
	SpnHashMap *br_then = ast_get_child_byname(ast, "then");
	SpnHashMap *br_else = ast_get_child_byname_optional(ast, "else");

	RegisterRange temp_range;

	/* compile condition */
	if (compile_expr_toplevel(cmp, cond, &condidx, &temp_range) == 0) {
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
	if (br_else != NULL && compile(cmp, br_else) == 0) {
		return 0;
	}

	/* complete stub jumps */
	len_then = off_else - off_then;
	len_else = cmp->bc.len - off_else;

	cmp->bc.insns[off_jze_b4_then + 0] = SPN_MKINS_A(SPN_INS_JZE, condidx);
	cmp->bc.insns[off_jze_b4_then + 1] = len_then;

	cmp->bc.insns[off_jmp_b4_else + 0] = SPN_MKINS_VOID(SPN_INS_JMP);
	cmp->bc.insns[off_jmp_b4_else + 1] = len_else;

	/* clean up temporary registers used for evaluating the condition */
	cleanup_registers(cmp, temp_range);

	return 1;
}

/* helper function for 'compile_loop_jump()' */
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

static int compile_loop_jump(SpnCompiler *cmp, SpnHashMap *ast)
{
	/* dummy jump instruction */
	spn_uword ins[2] = { 0 };

	/* "break" or "continue" */
	const char *type = ast_get_type(ast);

	/* 1 for "break", 0 for "continue" */
	int is_break = type_equal(type, "break");

	/* "break" releases variables declared in the loop initializer; "continue" doesn't. */
	int first_regidx = is_break ? cmp->parent_loop_varcount : cmp->parent_init_varcount;

	RegisterRange cleanup_range;

	/* it doesn't make sense to 'break' or 'continue' outside a loop */
	if (cmp->is_in_loop == 0) {
		const void *args[1];
		args[0] = type;
		compiler_error(cmp, ast, "'%s' is only meaningful inside a loop", args);
		return 0;
	}

	/* Release all variables declared in the loop body.
	 * In the case of a "break" statement, also release the variables
	 * declared in the loop header/initializer.
	 * In the case of a "continue" statement, the variables declared
	 * in the loop header must _not_ be released.
	 */
	cleanup_range.first = first_regidx;
	cleanup_range.plast = rts_count(cmp->varstack);
	cleanup_registers(cmp, cleanup_range);

	/* add link list node with current offset */
	prepend_jumplist_node(cmp, cmp->bc.len, is_break);

	/* then append dummy jump instruction */
	bytecode_append(&cmp->bc, ins, COUNT(ins));

	return 1;
}

static int compile_vardecl(SpnCompiler *cmp, SpnHashMap *ast)
{
	SpnArray *children = ast_get_children(ast);
	size_t n = spn_array_count(children);
	size_t i;

	RegisterRange temp_range;

	/* bring all variables in scope (each child represents a variable) */
	for (i = 0; i < n; i++) {
		int idx;

		/* the child representing a variable declaration */
		SpnHashMap *child = ast_get_nth_child(children, i);

		/* the name of the variable and the initializer expression */
		SpnHashMap *init = ast_get_child_byname_optional(child, "init");
		SpnValue name = spn_hashmap_get_strkey(child, "name");
		assert(isstring(&name));

		/* check for erroneous re-declaration - the name must not yet be
		 * in scope (i. e. in the variable stack)
		 */
		if (rts_getidx(cmp->varstack, name) >= 0) {
			const void *args[1];
			args[0] = stringvalue(&name)->cstr;
			compiler_error(cmp, child, "variable '%s' is already declared", args);
			return 0;
		}

		/* add identifier to variable stack */
		idx = rts_add(cmp->varstack, name);

		/* always load nil into register before compiling initializer
		 * expression, in order to avoid garbage when one initializes
		 * a variable with an expression that refers to itself
		 */
		emit_ins_AB(cmp, SPN_INS_LDCONST, idx, SPN_CONST_NIL);

		/* only compile initializer expression if exists */
		if (init != NULL) {
			if (compile_expr_toplevel(cmp, init, &idx, &temp_range) == 0) {
				return 0;
			}

			/* Clean up temporary registers used for evaluating the initializer.
			 * This is guaranteed not to prematurely release the value stored
			 * in the now-initialized variable since we have already added it
			 * to the variable stack, hance its register is not a temporary.
			 */
			assert(idx < temp_range.first && "variable register index must not be in temporary range");
			cleanup_registers(cmp, temp_range);
		}
	}

	return 1;
}

static int compile_const(SpnCompiler *cmp, SpnHashMap *ast)
{
	SpnArray *children = ast_get_children(ast);
	size_t n = spn_array_count(children);
	size_t i;

	for (i = 0; i < n; i++) {
		int regidx = -1;

		/* constant declaration descriptor */
		SpnHashMap *child = ast_get_nth_child(children, i);

		/* name and initializer expression of constant */
		SpnString *name = ast_get_string(child, "name");
		SpnHashMap *init = ast_get_child_byname(child, "init");

		RegisterRange temp_range;

		if (compile_expr_toplevel(cmp, init, &regidx, &temp_range) == 0) {
			return 0;
		}

		/* write "set global symbol" instruction */
		emit_ins_mid(cmp, SPN_INS_GLBVAL, regidx, name->len);

		/* append 0-terminated name of the symbol */
		append_cstring(&cmp->bc, name->cstr, name->len);

		/* clean up temporary registers used for evaluating the initializer */
		cleanup_registers(cmp, temp_range);
	}

	return 1;
}

static int compile_return(SpnCompiler *cmp, SpnHashMap *ast)
{
	/* compile expression (left child) if any; else return nil */
	SpnHashMap *expression = ast_get_child_byname_optional(ast, "expr");
	if (expression != NULL) {
		int dst = -1;

		/* no need to clean up temporary registers: returning from a function
		 * makes the VM release all registers in the stack frame of the function.
		 */
		if (compile_expr_toplevel(cmp, expression, &dst, NULL) == 0) {
			return 0;
		}

		emit_ins_A(cmp, SPN_INS_RET, dst);
	} else {
		append_return_nil(cmp);
	}

	return 1;
}

static int compile_empty(SpnCompiler *cmp, SpnHashMap *ast)
{
	return 1;
}

/* a "top-level" expression is basically what the C standard calls a
 * "full expression". That is, an expression which is part of an expression
 * statement, or an expression in the condition of an if, while or do-while
 * statement, or an expression which is part of the the header of a for
 * statement.
 */
static int compile_expr_toplevel(
	SpnCompiler *cmp,
	SpnHashMap *ast,
	int *dst,
	RegisterRange *temp_range
)
{
	int reg = dst != NULL ? *dst : -1;

	/* set the lowest index that can be used as a temporary register
	 * to the number of variables (local or global, depending on scope).
	 * We also need this tmpidx for later use in the cleanup code,
	 * which is necessary because cmp->tmpidx may be modified by the
	 * various compiler functions, in particular unused expression
	 * results may go into temporary registers, which would result in
	 * them not being cleaned up at the end of the top-level expression.
	 */
	int tmpidx = rts_count(cmp->varstack);
	cmp->tmpidx = tmpidx;

	/* actually compile expression */
	if (compile_expr(cmp, ast, &reg)) {
		if (dst != NULL) {
			*dst = reg;
		}

		/* clean up temporaries in order for RAII to behave correctly.
		 * For the sake of simplicity of the compiler, we don't store
		 * _exactly_ how many temporary registers the expression
		 * required. Instead, we nil out _every_ register between
		 * cmp->tmpidx and cmp->nregs, which potentially sets some
		 * already-dead / unused registers to nil, but we don't care
		 * about this redundancy. It doesn't do any harm to local
		 * variables, since tmpidx >= the register index of a local
		 * variable implies that the variable already went out of scope.
		 *
		 * NOTA BENE: if we ever allow arbitrary statements, in particular
		 * blocks and loops, if-then-else and match statements within
		 * expressions, we need to re-think this, as we will then need to:
		 *
		 *   1. Clean up everything in the block, including local
		 *      variables declared therein; and
		 *   2. Clean up the block scope after a break or continue statement;
		 *   3. Pay special attention to variables declared in the condition
		 *      of if, for, while and match statements.
		 *
		 */
		if (temp_range) {
			temp_range->first = tmpidx;
			temp_range->plast = cmp->nregs;
		}

		return 1;
	}

	return 0;
}

/* helper function for loading a string literal */
static void compile_string_literal(SpnCompiler *cmp, SpnValue str, int *dst)
{
	int idx;

	assert(isstring(&str));

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* if the string is not in the symtab yet, add it */
	idx = rts_getidx(cmp->symtab, str);
	if (idx < 0) {
		idx = rts_add(cmp->symtab, str);
	}

	/* emit load instruction */
	emit_ins_mid(cmp, SPN_INS_LDSYM, *dst, idx);
}

/* simple (non short-circuiting) binary operators: arithmetic, bitwise ops,
 * comparison and equality tests, string concatenation
 */
static int compile_simple_binop(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	int dst_left  = -1;
	int dst_right = -1;
	int nvars;

	/* "side effect-less" binary operators, in ascending order of precedence */
	static const NodeAndOpcode opcode_map[] = {
		{ "concat",  SPN_INS_CONCAT },
		{ "==",      SPN_INS_EQ     },
		{ "!=",      SPN_INS_NE     },
		{ "<",       SPN_INS_LT     },
		{ "<=",      SPN_INS_LE     },
		{ ">",       SPN_INS_GT     },
		{ ">=",      SPN_INS_GE     },
		{ "bit_or",  SPN_INS_OR     },
		{ "bit_xor", SPN_INS_XOR    },
		{ "bit_and", SPN_INS_AND    },
		{ "<<",      SPN_INS_SHL    },
		{ ">>",      SPN_INS_SHR    },
		{ "+",       SPN_INS_ADD    },
		{ "-",       SPN_INS_SUB    },
		{ "*",       SPN_INS_MUL    },
		{ "/",       SPN_INS_DIV    },
		{ "mod",     SPN_INS_MOD    }
	};

	SpnHashMap *left = ast_get_child_byname(ast, "left");
	SpnHashMap *right = ast_get_child_byname(ast, "right");

	const char *type = ast_get_type(ast);
	enum spn_vm_ins opcode = node_to_opcode(opcode_map, COUNT(opcode_map), type);

	/* compile children */
	if (compile_expr(cmp, left,  &dst_left)  == 0
	 || compile_expr(cmp, right, &dst_right) == 0) {
		return 0;
	}

	nvars = rts_count(cmp->varstack);

	/* if result of RHS went into a temporary, then "pop" */
	if (dst_right >= nvars) {
		tmp_pop(cmp);
	}

	/* if result of LHS went into a temporary, then "pop" */
	if (dst_left >= nvars) {
		tmp_pop(cmp);
	}

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	emit_ins_ABC(cmp, opcode, *dst, dst_left, dst_right);
	return 1;
}

static int compile_assignment_var(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	SpnHashMap *left  = ast_get_child_byname(ast, "left");
	SpnHashMap *right = ast_get_child_byname(ast, "right");

	/* get register index of variable using its name */
	SpnValue varname = spn_hashmap_get_strkey(left, "name");
	int idx = rts_getidx(cmp->varstack, varname);

	if (idx < 0) {
		const void *args[1];
		args[0] = stringvalue(&varname)->cstr;
		compiler_error(
			cmp,
			left,
			"variable '%s' is undeclared",
			args
		);
		return 0;
	}

	/* store RHS to the variable register */
	if (compile_expr(cmp, right, &idx) == 0) {
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
		/* tiny optimization: don't move a register onto itself */
		emit_ins_AB(cmp, SPN_INS_MOV, *dst, idx);
	}

	return 1;
}

static int compile_assignment_array(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	int nvars;

	/* array and subscript indices: just like in 'compile_subscript()' */
	int arridx = -1, subidx = -1;

	SpnHashMap *lhs = ast_get_child_byname(ast, "left");
	SpnHashMap *rhs = ast_get_child_byname(ast, "right");
	SpnHashMap *object = ast_get_child_byname(lhs, "object");

	const char *nodetype = ast_get_type(lhs);
	int is_subscript = type_equal(nodetype, "subscript");
	enum spn_vm_ins opcode = is_subscript ? SPN_INS_IDX_SET : SPN_INS_PROPSET;

	/* compile right-hand side directly into the destination register,
	 * since the assignment operation needs to yield it anyway
	 * (formally, the operation yields the LHS, but they are the same
	 * in the case of a simple assignment.)
	 */
	if (compile_expr(cmp, rhs, dst) == 0) {
		return 0;
	}

	/* compile array expression */
	if (compile_expr(cmp, object, &arridx) == 0) {
		return 0;
	}

	/* compile subscript */
	if (is_subscript) {
		/* indexing with brackets */
		SpnHashMap *index = ast_get_child_byname(lhs, "index");
		if (compile_expr(cmp, index, &subidx) == 0) {
			return 0;
		}
	} else {
		/* memberof */
		SpnValue nameval = spn_hashmap_get_strkey(lhs, "name");
		assert(type_equal(nodetype, "memberof"));
		compile_string_literal(cmp, nameval, &subidx);
	}

	/* emit "indexed setter" or "property setter" instruction */
	emit_ins_ABC(cmp, opcode, arridx, subidx, *dst);

	/* XXX: is this correct? since we need neither the value of the
	 * array nor the value of the subscripting expression, we can
	 * get rid of them by popping them off the temporary stack.
	 */
	nvars = rts_count(cmp->varstack);

	if (subidx >= nvars) {
		tmp_pop(cmp);
	}

	if (arridx >= nvars) {
		tmp_pop(cmp);
	}

	return 1;
}

static int compile_assignment(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	SpnHashMap *lhs = ast_get_child_byname(ast, "left");
	const char *nodetype = ast_get_type(lhs);

	/* assignment to a variable */
	if (type_equal(nodetype, "ident")) {
		return compile_assignment_var(cmp, ast, dst);
	}

	/* assignment to subscripted expression or object property */
	if (type_equal(nodetype, "subscript") || type_equal(nodetype, "memberof")) {
		return compile_assignment_array(cmp, ast, dst);
	}

	/* nothing else can be assigned to */
	compiler_error(
		cmp,
		ast,
		"LHS of assignment must be a variable, a subscripted expression or a property",
		NULL
	);

	return 0;
}

static int compile_cmpd_assgmt_var(SpnCompiler *cmp, SpnHashMap *ast, int *dst, enum spn_vm_ins opcode)
{
	int nvars;
	int rhs = -1;

	SpnHashMap *left  = ast_get_child_byname(ast, "left");
	SpnHashMap *right = ast_get_child_byname(ast, "right");

	/* get register index of variable using its name */
	SpnValue varname = spn_hashmap_get_strkey(left, "name");
	int idx = rts_getidx(cmp->varstack, varname);

	if (idx < 0) {
		const void *args[1];
		args[0] = stringvalue(&varname)->cstr;
		compiler_error(
			cmp,
			left,
			"variable '%s' is undeclared",
			args
		);
		return 0;
	}

	/* evaluate RHS */
	if (compile_expr(cmp, right, &rhs) == 0) {
		return 0;
	}

	/* if RHS went into a temporary register, then pop() */
	nvars = rts_count(cmp->varstack);
	if (rhs >= nvars) {
		tmp_pop(cmp);
	}

	/* emit instruction to operate on LHS and RHS */
	emit_ins_ABC(cmp, opcode, idx, idx, rhs);

	/* finally, yield the LHS. (this just means that we set the destination
	 * register to the index of the variable if we can, and we emit a move
	 * instruction if we can't.)
	 */
	if (*dst < 0) {
		*dst = idx;
	} else if (*dst != idx) {
		emit_ins_AB(cmp, SPN_INS_MOV, *dst, idx);
	}

	return 1;
}

static int compile_cmpd_assgmt_arr(SpnCompiler *cmp, SpnHashMap *ast, int *dst, enum spn_vm_ins opcode)
{
	spn_uword ins[3];
	int nvars;

	/* VM register indices of:
	 * 1. the object which is being subscripted
	 * 2. the subscripting expression
	 * 3. the RHS of the assignment
	 */
	int arridx = -1, subidx = -1, rhsidx = -1;

	SpnHashMap *lhs = ast_get_child_byname(ast, "left");
	SpnHashMap *rhs = ast_get_child_byname(ast, "right");
	SpnHashMap *object = ast_get_child_byname(lhs, "object");

	const char *nodetype = ast_get_type(lhs);
	int is_subscript = type_equal(nodetype, "subscript");

	/* select appropriate array/property getter and setter opcodes */
	enum spn_vm_ins getter_opcode = is_subscript ? SPN_INS_IDX_GET : SPN_INS_PROPGET;
	enum spn_vm_ins setter_opcode = is_subscript ? SPN_INS_IDX_SET : SPN_INS_PROPSET;

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* compile RHS */
	if (compile_expr(cmp, rhs, &rhsidx) == 0) {
		return 0;
	}

	/* compile array/object expression */
	if (compile_expr(cmp, object, &arridx) == 0) {
		return 0;
	}

	/* compile subscript */
	if (is_subscript) {
		/* indexing with brackets */
		SpnHashMap *index = ast_get_child_byname(lhs, "index");
		if (compile_expr(cmp, index, &subidx) == 0) {
			return 0;
		}
	} else {
		/* memberof */
		SpnValue nameval = spn_hashmap_get_strkey(lhs, "name");
		assert(type_equal(nodetype, "memberof"));
		compile_string_literal(cmp, nameval, &subidx);
	}

	/* load LHS into destination register */
	ins[0] = SPN_MKINS_ABC(getter_opcode, *dst, arridx, subidx);

	/* evaluate "LHS = LHS <op> RHS" */
	ins[1] = SPN_MKINS_ABC(opcode, *dst, *dst, rhsidx);

	/* store value of updated destination register into array */
	ins[2] = SPN_MKINS_ABC(setter_opcode, arridx, subidx, *dst);

	bytecode_append(&cmp->bc, ins, COUNT(ins));

	/* pop as many times as we used a temporary register (XXX: correct?) */
	nvars = rts_count(cmp->varstack);

	if (subidx >= nvars) {
		tmp_pop(cmp);
	}

	if (arridx >= nvars) {
		tmp_pop(cmp);
	}

	if (rhsidx >= nvars) {
		tmp_pop(cmp);
	}

	return 1;
}

static int compile_compound_assignment(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	const char *type = ast_get_type(ast);
	SpnHashMap *lhs = ast_get_child_byname(ast, "left");
	const char *lhs_type = ast_get_type(lhs);

	static const NodeAndOpcode opcode_map[] = {
		{ "+=",  SPN_INS_ADD    },
		{ "-=",  SPN_INS_SUB    },
		{ "*=",  SPN_INS_MUL    },
		{ "/=",  SPN_INS_DIV    },
		{ "%=",  SPN_INS_MOD    },
		{ "&=",  SPN_INS_AND    },
		{ "|=",  SPN_INS_OR     },
		{ "^=",  SPN_INS_XOR    },
		{ "<<=", SPN_INS_SHL    },
		{ ">>=", SPN_INS_SHR    },
		{ "..=", SPN_INS_CONCAT }
	};

	enum spn_vm_ins opcode = node_to_opcode(opcode_map, COUNT(opcode_map), type);

	if (type_equal(lhs_type, "ident")) {
		return compile_cmpd_assgmt_var(cmp, ast, dst, opcode);
	}

	if (type_equal(lhs_type, "subscript") || type_equal(lhs_type, "memberof")) {
		return compile_cmpd_assgmt_arr(cmp, ast, dst, opcode);
	}

	compiler_error(
		cmp,
		ast,
		"LHS of assignment must be a variable, subscripted expression or a property",
		NULL
	);

	return 0;
}

static int compile_logical(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	spn_sword off_rhs, off_jump, end_rhs;
	spn_uword jumpins[2] = { 0 }; /* dummy */

	const char *nodetype = ast_get_type(ast);
	int is_and = type_equal(nodetype, "and");
	enum spn_vm_ins opcode = is_and ? SPN_INS_JZE : SPN_INS_JNZ;

	SpnHashMap *lhs = ast_get_child_byname(ast, "left");
	SpnHashMap *rhs = ast_get_child_byname(ast, "right");

	/* we can't compile the result directly into the destination register,
	 * because if the destination is a variable which will be examined in
	 * the right-hand side expression too, we will be in trouble.
	 * So, the 'idx' variable holds the desintation index of the temporary
	 * register in which the value of the two sides will be stored.
	 */
	int idx;

	/* this needs to be done before 'idx = tmp_push()', because the
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
	if (compile_expr(cmp, lhs, &idx) == 0) {
		return 0;
	}

	/* if it evaluates to false (AND) or true (OR),
	 * then we short-circuit and yield the LHS
	 */
	off_jump = cmp->bc.len;
	bytecode_append(&cmp->bc, jumpins, COUNT(jumpins));

	off_rhs = cmp->bc.len;

	/* compile right-hand side */
	if (compile_expr(cmp, rhs, &idx) == 0) {
		return 0;
	}

	end_rhs = cmp->bc.len;

	/* fill in stub jump instruction */
	cmp->bc.insns[off_jump + 0] = SPN_MKINS_A(opcode, idx);
	cmp->bc.insns[off_jump + 1] = end_rhs - off_rhs;

	/* move result into destination, then get rid of temporary */
	emit_ins_AB(cmp, SPN_INS_MOV, *dst, idx);
	tmp_pop(cmp);

	return 1;
}

/* ternary conditional expression */
static int compile_condexpr(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	spn_sword off_then, off_else, off_jze_b4_then, off_jmp_b4_else;
	spn_sword len_then, len_else;
	spn_uword ins[2] = { 0 }; /* stub */
	int condidx = -1;

	SpnHashMap *cond = ast_get_child_byname(ast, "cond");
	SpnHashMap *val_then = ast_get_child_byname(ast, "true");
	SpnHashMap *val_else = ast_get_child_byname(ast, "false");

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

static int compile_ident(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	SpnValue varname = spn_hashmap_get_strkey(ast, "name");
	int idx = rts_getidx(cmp->varstack, varname);

	/* if 'rts_getidx()' returns -1, then the variable is not in the
	 * current scope, so we search for its name in the enclosing scopes.
	 * If it is found in one of the enclosing scopes, then we load it as
	 * the appropriate upvalue. Else we assume that it referes to a global
	 * symbol and we search the symbol table. If it's not yet in the symbol
	 * table, we create a symtab entry referencing the unresolved global.
	 */
	if (idx < 0) {
		int upval_idx = search_upvalues(cmp->upval_chain, varname);

		/* if it's not found in the closure either, assume a global */
		if (upval_idx < 0) {
			int sym;

			SpnString *varname_str = stringvalue(&varname);
			SymtabEntry *entry = symtabentry_new_global(varname_str);
			SpnValue stub = makeobject(entry);

			sym = rts_getidx(cmp->symtab, stub);
			if (sym < 0) {
				/* not found, append to symtab */
				sym = rts_add(cmp->symtab, stub);
			}

			spn_object_release(entry);

			/* compile "load symbol" instruction */
			if (*dst < 0) {
				*dst = tmp_push(cmp);
			}

			emit_ins_mid(cmp, SPN_INS_LDSYM, *dst, sym);
		} else {
			/* if this is reached, the variable (upvalue) was
			 * found in the enclosing scope
			 *
			 * XXX: TODO: check that upval_idx <= 255!
			 */
			if (*dst < 0) {
				*dst = tmp_push(cmp);
			}

			emit_ins_AB(cmp, SPN_INS_LDUPVAL, *dst, upval_idx);
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
		emit_ins_AB(cmp, SPN_INS_MOV, *dst, idx);
	}

	return 1;
}

static int compile_literal(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	SpnValue value = spn_hashmap_get_strkey(ast, "value");

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	switch (valtype(&value)) {
	case SPN_TTAG_NIL: {
		emit_ins_AB(cmp, SPN_INS_LDCONST, *dst, SPN_CONST_NIL);
		break;
	}
	case SPN_TTAG_BOOL: {
		enum spn_const_kind b = boolvalue(&value) ? SPN_CONST_TRUE : SPN_CONST_FALSE;
		emit_ins_AB(cmp, SPN_INS_LDCONST, *dst, b);
		break;
	}
	case SPN_TTAG_NUMBER: {
		if (isfloat(&value)) {
			double num = floatvalue(&value);
			spn_uword ins[1 + ROUNDUP(sizeof num, sizeof(spn_uword))] = { 0 };

			ins[0] = SPN_MKINS_AB(SPN_INS_LDCONST, *dst, SPN_CONST_FLOAT);
			memcpy(&ins[1], &num, sizeof num);

			bytecode_append(&cmp->bc, ins, COUNT(ins));
		} else {
			long num = intvalue(&value);
			spn_uword ins[1 + ROUNDUP(sizeof num, sizeof(spn_uword))] = { 0 };

			ins[0] = SPN_MKINS_AB(SPN_INS_LDCONST, *dst, SPN_CONST_INT);
			memcpy(&ins[1], &num, sizeof num);

			bytecode_append(&cmp->bc, ins, COUNT(ins));
		}

		break;
	}
	case SPN_TTAG_OBJECT: /* only string literals are stored as objects */
		assert(isstring(&value));
		compile_string_literal(cmp, value, dst);
		break;
	default:
		compiler_error(cmp, ast, "invalid type for scalar literal", NULL);
		return 0;
	}

	return 1;
}

static int compile_argv(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* emit "get argument vector" instruction */
	emit_ins_A(cmp, SPN_INS_ARGV, *dst);

	return 1;
}

static int compile_funcexpr(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
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
	emit_ins_mid(cmp, SPN_INS_LDSYM, *dst, symidx);

	/* By now, the 'upvalues' array should be filled with upvalue
	 * descriptors, in the order they will be written to the bytecode.
	 * As an optimization, we create a closure object _only_ if the
	 * function uses at least one upvalue.
	 */
	upval_count = spn_array_count(upvalues);

	if (upval_count > 0) {
		int i;

		/* append SPN_INS_CLOSURE instruction */
		emit_ins_AB(cmp, SPN_INS_CLOSURE, *dst, upval_count);

		/* add upvalue descriptors */
		for (i = 0; i < upval_count; i++) {
			SpnValue upval_desc = spn_array_get(upvalues, i);
			assert(isint(&upval_desc));

			ins = intvalue(&upval_desc);
			bytecode_append(&cmp->bc, &ins, 1);
		}
	}

	spn_object_release(upvalues);

	return 1;
}

static int compile_array_literal(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	SpnArray *children = ast_get_children(ast);
	size_t n = spn_array_count(children);
	size_t i;

	int validx;

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* obtain temporary index for values */
	validx = tmp_push(cmp);

	/* create array instance */
	emit_ins_A(cmp, SPN_INS_NEWARR, *dst);

	for (i = 0; i < n; i++) {
		SpnHashMap *expr = ast_get_nth_child(children, i);

		if (compile_expr(cmp, expr, &validx) == 0) {
			return 0;
		}

		/* emit 'push' instruction */
		emit_ins_AB(cmp, SPN_INS_ARR_PUSH, *dst, validx);
	}

	/* clean up temporary value register */
	tmp_pop(cmp);

	return 1;
}

static int compile_hashmap_literal(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	SpnArray *children = ast_get_children(ast);
	size_t n = spn_array_count(children);
	size_t i;

	int keyidx, validx;

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* obtain two temporary registers to store the key and the corresponding value */
	keyidx = tmp_push(cmp);
	validx = tmp_push(cmp);

	/* first, create the hashmap */
	emit_ins_A(cmp, SPN_INS_NEWHASH, *dst);

	/* then, compile keys and values */
	for (i = 0; i < n; i++) {
		SpnHashMap *kvpair = ast_get_nth_child(children, i);
		SpnHashMap *key = ast_get_child_byname(kvpair, "key");
		SpnHashMap *value = ast_get_child_byname(kvpair, "value");

		/* compile the key */
		if (compile_expr(cmp, key, &keyidx) == 0) {
			return 0;
		}

		/* compile the value */
		if (compile_expr(cmp, value, &validx) == 0) {
			return 0;
		}

		/* emit instruction for indexing setter */
		emit_ins_ABC(cmp, SPN_INS_IDX_SET, *dst, keyidx, validx);
	}

	/* clean up temporary registers of key and value */
	tmp_pop(cmp);
	tmp_pop(cmp);

	return 1;
}

static int compile_subscript_ex(SpnCompiler *cmp, SpnHashMap *ast, int *dst,
	int is_method_call, int *reg_array, int *reg_subsc)
{
	enum spn_vm_ins opcode;

	/* array index: register index of the array expression
	 * subscript index: register index of the subscripting expression
	 */
	int arridx = -1, subidx = -1;

	const char *nodetype = ast_get_type(ast);
	int is_subscript = type_equal(nodetype, "subscript");
	int is_memberof = type_equal(nodetype, "memberof");

	SpnHashMap *object = ast_get_child_byname(ast, "object");

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* compile array/hashmap expression */
	if (compile_expr(cmp, object, &arridx) == 0) {
		return 0;
	}

	/* compile subscripting expression */
	if (is_subscript) {
		/* normal subscripting with brackets */
		SpnHashMap *index = ast_get_child_byname(ast, "index");
		if (compile_expr(cmp, index, &subidx) == 0) {
			return 0;
		}
	} else {
		/* memberof, dot/arrow notation */
		SpnValue nameval = spn_hashmap_get_strkey(ast, "name");
		compile_string_literal(cmp, nameval, &subidx);
	}

	if (is_method_call) {
		/* In order to call a method, we need to keep the 'self' or
		 * 'this' argument, and potentially the method name as well.
		 */
		assert(reg_array != NULL);
		assert(reg_subsc != NULL);

		*reg_array = arridx;
		*reg_subsc = subidx;
	} else {
		/* the usual "pop as many times as we pushed" optimization */
		int nvars = rts_count(cmp->varstack);

		assert(reg_array == NULL);
		assert(reg_subsc == NULL);

		if (arridx >= nvars) {
			tmp_pop(cmp);
		}

		if (subidx >= nvars) {
			tmp_pop(cmp);
		}
	}

	if (is_method_call) {
		/* when compiling a method call, the function is not actually
		 * in the object itself; rather, it's a member of its class.
		 */
		assert(is_memberof);
		opcode = SPN_INS_METHOD;
	} else if (is_memberof) {
		/* not a method call but still a property access */
		opcode = SPN_INS_PROPGET;
	} else {
		/* plain array subscripting, just emit a normal "load from array" */
		assert(is_subscript);
		opcode = SPN_INS_IDX_GET;
	}

	emit_ins_ABC(cmp, opcode, *dst, arridx, subidx);

	return 1;
}

static int compile_subscript(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	/* Compile array subscript expression.
	 * Keep the result only, throw away array
	 * expression and subscripting expression.
	 */
	return compile_subscript_ex(cmp, ast, dst, 0, NULL, NULL);
}

static int compile_subscript_method_call(SpnCompiler *cmp, SpnHashMap *ast,
	int *dst, int *reg_array, int *reg_subsc)
{
	/* Compile memberof expression. Keep the register index of
	 * the array (which will become 'self'/'this') and that of the
	 * method name as well, along with the result of the indexing.
	 */
	return compile_subscript_ex(cmp, ast, dst, 1, reg_array, reg_subsc);
}

/* returns an array of register indices where the call arguments are stored. */
static spn_uword *compile_callargs(SpnCompiler *cmp, SpnArray *arguments, int is_method_call, int self_reg)
{
	size_t explicit_argc = spn_array_count(arguments);
	size_t total_argc = is_method_call ? explicit_argc + 1 : explicit_argc;
	size_t nelem = ROUNDUP(total_argc, SPN_WORD_OCTETS);
	size_t i;

	spn_uword *indices = spn_calloc(nelem, sizeof indices[0]);

	if (is_method_call) {
		/* 'self' is always argument #0 if present. So we can safely
		 * omit the computation of the offsets for bit operations here.
		 *
		 * int zero_wordidx = 0 / SPN_WORD_OCTETS;     // 0
		 * int zero_shift = 8 * (0 % SPN_WORD_OCTETS); // also 0
		 * indices[zero_wordidx] |= (spn_uword)(self_reg) << zero_shift;
		 */
		assert(self_reg < 256);
		indices[0] |= (spn_uword)(self_reg);
	}

	for (i = 0; i < explicit_argc; i++) {
		/* 'j' is the "physical" index of the argument, which indicates
		 * where it is located in the bytecode. This index is the same
		 * as its logical index in the explicit argument list if the
		 * call is a free function call (*not* a method), and one bigger
		 * then the logical index if the call is a method call.
		 */
		size_t j = is_method_call ? i + 1 : i;
		size_t wordidx = j / SPN_WORD_OCTETS;
		size_t shift = 8 * (j % SPN_WORD_OCTETS);

		SpnHashMap *argexpr = ast_get_nth_child(arguments, i);
		int dst = -1;

		if (compile_expr(cmp, argexpr, &dst) == 0) {
			free(indices);
			return NULL;
		}

		/* fill in the appropriate octet in the bytecode */
		indices[wordidx] |= (spn_uword)(dst) << shift;
	}

	return indices;
}

static int compile_call(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	int fnreg = -1, self_reg = -1, method_name_reg = -1;
	spn_uword *arg_register_indices;

	SpnHashMap *funcexpr = ast_get_child_byname(ast, "func");
	int is_method_call = type_equal(ast_get_type(funcexpr), "memberof");

	SpnArray *children = ast_get_children(ast);
	size_t argc = spn_array_count(children);

	/* if the call is a method call (as opposed to a free function call),
	 * then there's one extra call-time argument, 'self'.
	 */
	if (is_method_call) {
		argc++;
	}

	/* make room for return value */
	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* compile function expression, check if it is a method */
	if (is_method_call) {
		if (compile_subscript_method_call(cmp, funcexpr,
		   &fnreg, &self_reg, &method_name_reg) == 0) {
			return 0;
		}
	} else {
		if (compile_expr(cmp, funcexpr, &fnreg) == 0) {
			return 0;
		}
	}

	/* the 'children' array of the function call AST node contains
	 * the call arguments (actual parameters).
	 * We need to check whether argc > 0, because if argc == 0,
	 * then 'malloc()' may return NULL even if it succeeded.
	 * Fortunately, this won't ever result in false negatives
	 * (i. e. the omission of error reporting), since if there are
	 * no arguments to compile, then nothing could possibly fail.
	 */
	arg_register_indices = compile_callargs(cmp, children, is_method_call, self_reg);
	if (argc > 0 && arg_register_indices == NULL) {
		return 0;
	}

	/* actually emit call instruction */
	emit_ins_ABC(cmp, SPN_INS_CALL, *dst, fnreg, argc);
	bytecode_append(&cmp->bc, arg_register_indices, ROUNDUP(argc, SPN_WORD_OCTETS));

	/* 'arg_register_indices' has been 'malloc()'ed, so free it */
	free(arg_register_indices);

	return 1;
}

/* compiles unary prefix operators that have no side effects */
static int compile_unary(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	int idx = -1, nvars;

	static const NodeAndOpcode opcode_map[] = {
		{ "not",      SPN_INS_LOGNOT },
		{ "bit_not",  SPN_INS_BITNOT },
		{ "un_minus", SPN_INS_NEG    },
	};

	const char *type = ast_get_type(ast);
	enum spn_vm_ins opcode = node_to_opcode(opcode_map, COUNT(opcode_map), type);

	SpnHashMap *child = ast_get_child_byname(ast, "right");

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	if (compile_expr(cmp, child, &idx) == 0) {
		return 0;
	}

	emit_ins_AB(cmp, opcode, *dst, idx);

	nvars = rts_count(cmp->varstack);
	if (idx >= nvars) {
		tmp_pop(cmp);
	}

	return 1;
}

static int compile_unplus(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	SpnHashMap *child = ast_get_child_byname(ast, "right");
	return compile_expr(cmp, child, dst);
}

static int compile_unminus(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	/* if the operand is a literal, check if it's actually a number,
	 * and if it is, directly emit its negated value.
	 */
	SpnHashMap *op = ast_get_child_byname(ast, "right");
	const char *op_type = ast_get_type(op);

	if (type_equal(op_type, "literal")) {
		SpnValue value = spn_hashmap_get_strkey(op, "value");
		SpnValue negated_value;
		SpnHashMap *negated;
		int success;

		if (!isnum(&value)) {
			compiler_error(
				cmp,
				op,
				"unary minus applied to non-number literal",
				NULL
			);
			return 0;
		}

		if (isfloat(&value)) {
			negated_value = makefloat(-1.0 * floatvalue(&value));
		} else {
			negated_value = makeint(-1 * intvalue(&value));
		}

		/* clone the entire operand as-is, except negate its value */
		negated = ast_shallow_copy(op);
		spn_hashmap_set_strkey(negated, "value", &negated_value);

		success = compile_literal(cmp, negated, dst);
		spn_object_release(negated);
		return success;
	}

	/* Else fall back to treating it just like all
	 * other prefix unary operators are treated.
	 */
	return compile_unary(cmp, ast, dst);
}

static int compile_incdec_var(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	const char *nodetype = ast_get_type(ast);

	int is_prefix = type_equal(nodetype, "pre_inc") || type_equal(nodetype, "pre_dec");
	int is_incrmt = type_equal(nodetype, "pre_inc") || type_equal(nodetype, "post_inc");

	SpnHashMap *op = ast_get_child_byname(ast, is_prefix ? "right" : "left");
	enum spn_vm_ins opcode = is_incrmt ? SPN_INS_INC : SPN_INS_DEC;

	SpnValue varname = spn_hashmap_get_strkey(op, "name");
	int idx = rts_getidx(cmp->varstack, varname);

	if (idx < 0) {
		const void *args[1];
		args[0] = stringvalue(&varname)->cstr;
		compiler_error(
			cmp,
			op,
			"variable '%s' is undeclared",
			args
		);
		return 0;
	}

	if (is_prefix) {
		/* increment or decrement first */
		emit_ins_A(cmp, opcode, idx);

		/* then yield already changed value */
		if (*dst < 0) {
			*dst = idx;
		} else if (*dst != idx) {
			emit_ins_AB(cmp, SPN_INS_MOV, *dst, idx);
		}
	} else {
		/* first, yield unchanged value */
		if (*dst < 0) {
			*dst = tmp_push(cmp);
			emit_ins_AB(cmp, SPN_INS_MOV, *dst, idx);
		} else if (*dst != idx) {
			emit_ins_AB(cmp, SPN_INS_MOV, *dst, idx);
		}

		/* increment/decrement only then */
		emit_ins_A(cmp, opcode, idx);
	}

	return 1;
}

static int compile_incdec_arr(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	const char *nodetype = ast_get_type(ast);

	int is_prefix = type_equal(nodetype, "pre_inc") || type_equal(nodetype, "pre_dec");
	int is_incrmt = type_equal(nodetype, "pre_inc") || type_equal(nodetype, "post_inc");

	SpnHashMap *op = ast_get_child_byname(ast, is_prefix ? "right" : "left");
	const char *op_type = ast_get_type(op);
	int is_subscript = type_equal(op_type, "subscript");

	/* select appropriate opcodes:
	 * increment vs. decrement; array subscript vs. property accessor
	 */
	enum spn_vm_ins arith_opcode = is_incrmt ? SPN_INS_INC : SPN_INS_DEC;
	enum spn_vm_ins getter_opcode = is_subscript ? SPN_INS_IDX_GET : SPN_INS_PROPGET;
	enum spn_vm_ins setter_opcode = is_subscript ? SPN_INS_IDX_SET : SPN_INS_PROPSET;

	/* the subscripted object */
	SpnHashMap *object = ast_get_child_byname(op, "object");

	/* register index of subscripted object ("array")
	 * and indexing expression, respectively
	 */
	int arridx = -1, subidx = -1;

	int nvars;

	if (*dst < 0) {
		*dst = tmp_push(cmp);
	}

	/* compile subscripted object */
	if (compile_expr(cmp, object, &arridx) == 0) {
		return 0;
	}

	/* compile indexing expression */
	if (is_subscript) { /* operator [] */
		SpnHashMap *index = ast_get_child_byname(op, "index");
		if (compile_expr(cmp, index, &subidx) == 0) {
			return 0;
		}
	} else { /* member-of, '.' */
		SpnValue nameval = spn_hashmap_get_strkey(op, "name");
		compile_string_literal(cmp, nameval, &subidx);
	}

	if (is_prefix) { /* these yield the already incremented/decremented value */
		spn_uword insns[3];
		insns[0] = SPN_MKINS_ABC(getter_opcode, *dst, arridx, subidx);
		insns[1] = SPN_MKINS_A(arith_opcode, *dst);
		insns[2] = SPN_MKINS_ABC(setter_opcode, arridx, subidx, *dst);

		bytecode_append(&cmp->bc, insns, COUNT(insns));
	} else {
		/* on the other hand, these operators yield the original
		 * (yet unmodified) value. For this, we need a temporary
		 * register to store the incremented/decremented value in.
		 */
		int tmpidx = tmp_push(cmp);

		spn_uword insns[4];
		insns[0] = SPN_MKINS_ABC(getter_opcode, *dst, arridx, subidx);
		insns[1] = SPN_MKINS_AB(SPN_INS_MOV, tmpidx, *dst);
		insns[2] = SPN_MKINS_A(arith_opcode, tmpidx);
		insns[3] = SPN_MKINS_ABC(setter_opcode, arridx, subidx, tmpidx);

		bytecode_append(&cmp->bc, insns, COUNT(insns));
		tmp_pop(cmp);
	}

	/* once again, pop expired temporary values */
	nvars = rts_count(cmp->varstack);

	if (subidx >= nvars) {
		tmp_pop(cmp);
	}

	if (arridx >= nvars) {
		tmp_pop(cmp);
	}

	return 1;
}

static int compile_incdec(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	const char *type = ast_get_type(ast);
	int is_prefix = type_equal(type, "pre_inc") || type_equal(type, "pre_dec");

	SpnHashMap *op = ast_get_child_byname(ast, is_prefix ? "right" : "left");
	const char *op_type = ast_get_type(op);

	if (type_equal(op_type, "ident")) {
		return compile_incdec_var(cmp, ast, dst);
	}

	if (type_equal(op_type, "subscript") || type_equal(op_type, "memberof")) {
		return compile_incdec_arr(cmp, ast, dst);
	}

	/* anything else is not an lvalue and cannot be [in | de]cremented */
	compiler_error(
		cmp,
		ast,
		"argument of ++ and -- must be a variable or array member",
		NULL
	);
	return 0;
}

static int compile_expr(SpnCompiler *cmp, SpnHashMap *ast, int *dst)
{
	const void *args[1]; /* for error reporting */

	/* Linear search with a twist: frequently-used node types are ordered
	 * before less common ones so that the constant factor stays smaller.
	 */
	static const struct {
		const char *node;
		int (*fn)(SpnCompiler *, SpnHashMap *, int *);
	} compilers[] = {
		/* terms and most postfix operators */
		{ "literal",   compile_literal             },
		{ "ident",     compile_ident               },
		{ "function",  compile_funcexpr            },
		{ "hashmap",   compile_hashmap_literal     },
		{ "array",     compile_array_literal       },
		{ "argv",      compile_argv                },
		{ "call",      compile_call                },
		{ "subscript", compile_subscript           },
		{ "memberof",  compile_subscript           },

		/* comparisons */
		{ "==",        compile_simple_binop        },
		{ "!=",        compile_simple_binop        },
		{ "<",         compile_simple_binop        },
		{ "<=",        compile_simple_binop        },
		{ ">",         compile_simple_binop        },
		{ ">=",        compile_simple_binop        },

		/* simple assignment, logical ops and concatenation */
		{ "assign",    compile_assignment          },
		{ "or",        compile_logical             },
		{ "and",       compile_logical             },
		{ "concat",    compile_simple_binop        },

		/* the rest of the binary operators (mostly arithmetic) */
		{ "+",         compile_simple_binop        },
		{ "-",         compile_simple_binop        },
		{ "*",         compile_simple_binop        },
		{ "/",         compile_simple_binop        },
		{ "mod",       compile_simple_binop        },
		{ "bit_or",    compile_simple_binop        },
		{ "bit_xor",   compile_simple_binop        },
		{ "bit_and",   compile_simple_binop        },
		{ "<<",        compile_simple_binop        },
		{ ">>",        compile_simple_binop        },

		/* compound assignments */
		{ "+=",        compile_compound_assignment },
		{ "-=",        compile_compound_assignment },
		{ "*=",        compile_compound_assignment },
		{ "/=",        compile_compound_assignment },
		{ "%=",        compile_compound_assignment },
		{ "|=",        compile_compound_assignment },
		{ "^=",        compile_compound_assignment },
		{ "&=",        compile_compound_assignment },
		{ "<<=",       compile_compound_assignment },
		{ ">>=",       compile_compound_assignment },
		{ "..=",       compile_compound_assignment },

		/* and the rest: increment/decrement... */
		{ "pre_inc",   compile_incdec              },
		{ "pre_dec",   compile_incdec              },
		{ "post_inc",  compile_incdec              },
		{ "post_dec",  compile_incdec              },

		/* ...prefix unary ops... */
		{ "un_plus",   compile_unplus              },
		{ "un_minus",  compile_unminus             },
		{ "not",       compile_unary               },
		{ "bit_not",   compile_unary               },

		/* ... and the conditional expression */
		{ "condexpr",  compile_condexpr            }
	};

	const char *type = ast_get_type(ast);

	size_t i;
	for (i = 0; i < COUNT(compilers); i++) {
		if (type_equal(compilers[i].node, type)) {
			size_t begin = cmp->bc.len;
			int status = compilers[i].fn(cmp, ast, dst);
			size_t end = cmp->bc.len;

			/* add debug info mapping bytecode addresses to source
			 * lines, columns and registers.
			 */
			spn_dbg_emit_source_location(cmp->debug_info, begin, end, ast, *dst);

			return status;
		}
	}

	/* if neither one of the node types above matched,
	 * then we don't know how to compile this node
	 */
	args[0] = type;
	compiler_error(cmp, ast, "unknown AST node: \"%s\"", args);
	return 0;
}
