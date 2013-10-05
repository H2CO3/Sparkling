/*
 * ast.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 03/05/2013
 * Licensed under the 2-clause BSD License
 *
 * AST: a right-leaning abstract syntax tree
 */

#ifndef SPN_AST_H
#define SPN_AST_H

#include "spn.h"
#include "str.h"

/* AST nodes
 * 
 * brief description:
 * ------------------
 * 
 * PROGRAM represents the entire translation unit. Its children are the
 * statements and function definitions in the same order they are present
 * in the source text.
 * 
 * BLOCK is a compound statement that opens a new lexical scope.
 * 
 * loops: while and do have their condition store in the left child,
 * for and foreach maintain a link list of FORHEADER nodes, describing the
 * three parts of the for(each) loop header (initialization, condition,
 * increment; or key, value, table)
 * 
 * SPN_NODE_IF's left child is the condition, the right child is a
 * BRANCHES node (left child: then-branch, right child: else-branch)
 * 
 * VARDECL has its `name` member set to the identifier of the variable, the
 * optional left child is the initializer expression, or NULL if none
 * 
 * expressions are laid out intuitively: the left child is the LHS, the right
 * child is the RHS. the exception is the conditional ternary operator:
 * its left child is the condition, the right child is a BRANCHES node.
 * unary operators have their left child set only, except array subscripting,
 * the `memberof` operator and function calls, which have their argument(s)
 * packaged into the right child (this is an expression in the case of array
 * subscripts and memberof, and a link list of CALLARGS nodes in the case
 * of function calls)
 * 
 * the `name` member of an IDENT node is set to the actual identifier string
 * likewise, the `value` member of a LITERAL token is an SpnValue
 * (integer, floating-point number, string, etc.) describing the token
 * 
 * LAMBDA and FUNCDEF are almost identical, except that the former can only
 * appear at global (file) scope. (the name of a FUNCTION is *still* an
 * expression, though, just like a lambda). The arguments are DECLARGS nodes,
 * with their `name` member set to the name of the formal parameter.
 * 
 * the COMPOUND node is needed when a list of statements is to be represented
 * by a single node. It's used in a right-leaning manner: one statement is the
 * left child, the right child is either another statement or another COMPOUND
 * node.
 */

enum spn_ast_node {
	SPN_NODE_PROGRAM,	/* top-level program, translation unit */
	SPN_NODE_BLOCK,		/* block, compound statement */
	SPN_NODE_FUNCDEF,	/* function definition */

	/* statements */
	SPN_NODE_WHILE,
	SPN_NODE_DO,
	SPN_NODE_FOR,
	SPN_NODE_FOREACH,								/* done */
	SPN_NODE_IF,

	SPN_NODE_BREAK,									/* TODO: implement */
	SPN_NODE_CONTINUE,								/* TODO: implement */
	SPN_NODE_RETURN,
	SPN_NODE_EMPTY,
	SPN_NODE_VARDECL,

	/* assignments */
	SPN_NODE_ASSIGN,
	SPN_NODE_ASSIGN_ADD,
	SPN_NODE_ASSIGN_SUB,
	SPN_NODE_ASSIGN_MUL,
	SPN_NODE_ASSIGN_DIV,
	SPN_NODE_ASSIGN_MOD,
	SPN_NODE_ASSIGN_AND,
	SPN_NODE_ASSIGN_OR,
	SPN_NODE_ASSIGN_XOR,
	SPN_NODE_ASSIGN_SHL,
	SPN_NODE_ASSIGN_SHR,
	SPN_NODE_ASSIGN_CONCAT,

	/* string concatenation and ternary conditional */
	SPN_NODE_CONCAT,
	SPN_NODE_CONDEXPR,

	/* binary arithmetic (and bitwise) operations */
	SPN_NODE_ADD,
	SPN_NODE_SUB,
	SPN_NODE_MUL,
	SPN_NODE_DIV,
	SPN_NODE_MOD,

	SPN_NODE_BITAND,
	SPN_NODE_BITOR,
	SPN_NODE_BITXOR,
	SPN_NODE_SHL,
	SPN_NODE_SHR,

	/* logical operators */
	SPN_NODE_LOGAND,
	SPN_NODE_LOGOR,
	
	/* comparison operations */
	SPN_NODE_EQUAL,
	SPN_NODE_NOTEQ,
	SPN_NODE_LESS,
	SPN_NODE_LEQ,
	SPN_NODE_GREATER,
	SPN_NODE_GEQ,

	/* prefix unary */
	SPN_NODE_UNPLUS,
	SPN_NODE_UNMINUS,
	SPN_NODE_PREINCRMT,
	SPN_NODE_PREDECRMT,
	SPN_NODE_SIZEOF,
	SPN_NODE_TYPEOF,
	SPN_NODE_LOGNOT,
	SPN_NODE_BITNOT,
	SPN_NODE_NTHARG,	/* n-th unnamed function argument */

	/* postfix unary */
	SPN_NODE_POSTINCRMT,								/* XXX not done for arrays */
	SPN_NODE_POSTDECRMT,								/* XXX not done for arrays */
	SPN_NODE_ARRSUB,
	SPN_NODE_MEMBEROF,	/* syntactic sugar for simulating OO */
	SPN_NODE_FUNCCALL,								/* XXX: handle `this' properly  */

	/* terms */
	SPN_NODE_IDENT,
	SPN_NODE_LITERAL,
	SPN_NODE_LAMBDA,

	/* miscellaneous */
	SPN_NODE_DECLARGS,	/* argument names in a function definition	*/	/* done (implicit) */
	SPN_NODE_CALLARGS,	/* function call argument list			*/	/* done (implicit) */
	SPN_NODE_BRANCHES,	/* then and else branch of `if` and `?:`	*/	/* done (implicit) */
	SPN_NODE_FORHEADER,	/* link list: "init; cond; incrmt" of `for` and
				 * "key as value in array" for `foreach` loop
				 */
	SPN_NODE_COMPOUND	/* generic "compound node" (not a block, this		/+ done +/
				 * is to be used when multiple statements
				 * follow each other, and the AST needs more
				 * than two children)
				 */
};

/* the Abstract Syntax Tree */
typedef struct SpnAST {
	enum spn_ast_node node;		/* public: the node type (see the enum)	*/
	SpnValue	  value;	/* public: the value of the node if any	*/
	SpnString	 *name;		/* public: the name of the node, if any	*/
	unsigned long	  lineno;	/* public: the line where the node is	*/
	struct SpnAST	 *left;		/* public: left child or NULL		*/
	struct SpnAST	 *right;	/* public: right child or NULL		*/
} SpnAST;

/* lineno is the line number where the parser is currently */
SPN_API SpnAST	*spn_ast_new(int node, unsigned long lineno);
SPN_API void	 spn_ast_free(SpnAST *ast);

/* dumps a textual representation of the AST to stdout */
SPN_API void	 spn_ast_dump(SpnAST *ast);

#endif /* SPN_AST_H */

