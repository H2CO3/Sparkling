/*
 * parser.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Simple recursive descent parser
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "parser.h"
#include "lex.h"
#include "private.h"


static SpnAST *parse_program(SpnParser *p);
static SpnAST *parse_program_nonempty(SpnParser *p);
static SpnAST *parse_stmt(SpnParser *p, int is_global);
static SpnAST *parse_stmt_list(SpnParser *p);
static SpnAST *parse_function(SpnParser *p, int is_stmt);
static SpnAST *parse_expr(SpnParser *p);

static SpnAST *parse_assignment(SpnParser *p);
static SpnAST *parse_concat(SpnParser *p);
static SpnAST *parse_condexpr(SpnParser *p);

static SpnAST *parse_logical_or(SpnParser *p);
static SpnAST *parse_logical_and(SpnParser *p);
static SpnAST *parse_bitwise_or(SpnParser *p);
static SpnAST *parse_bitwise_xor(SpnParser *p);
static SpnAST *parse_bitwise_and(SpnParser *p);

static SpnAST *parse_comparison(SpnParser *p);
static SpnAST *parse_shift(SpnParser *p);
static SpnAST *parse_additive(SpnParser *p);
static SpnAST *parse_multiplicative(SpnParser *p);

static SpnAST *parse_prefix(SpnParser *p);
static SpnAST *parse_postfix(SpnParser *p);
static SpnAST *parse_term(SpnParser *p);

static SpnAST *parse_array_literal(SpnParser *p);
static SpnAST *parse_decl_args(SpnParser *p);
static SpnAST *parse_call_args(SpnParser *p);

static SpnAST *parse_if(SpnParser *p);
static SpnAST *parse_while(SpnParser *p);
static SpnAST *parse_do(SpnParser *p);
static SpnAST *parse_for(SpnParser *p);
static SpnAST *parse_break(SpnParser *p);
static SpnAST *parse_continue(SpnParser *p);
static SpnAST *parse_return(SpnParser *p);
static SpnAST *parse_vardecl(SpnParser *p);
static SpnAST *parse_const(SpnParser *p);
static SpnAST *parse_expr_stmt(SpnParser *p);
static SpnAST *parse_empty(SpnParser *p);
static SpnAST *parse_block(SpnParser *p);


static SpnAST *parse_binexpr_rightassoc(
	SpnParser *p,
	const enum spn_lex_token toks[],
	const enum spn_ast_node nodes[],
	size_t n,
	SpnAST *(*subexpr)(SpnParser *)
);

static SpnAST *parse_binexpr_leftassoc(
	SpnParser *p,
	const enum spn_lex_token toks[],
	const enum spn_ast_node nodes[],
	size_t n,
	SpnAST *(*subexpr)(SpnParser *)
);


SpnParser *spn_parser_new(void)
{
	SpnParser *p = spn_malloc(sizeof(*p));

	p->pos = NULL;
	p->eof = 0;
	p->error = 0;
	p->lineno = 1;
	p->errmsg = NULL;

	return p;
}

void spn_parser_free(SpnParser *p)
{
	free(p->errmsg);
	free(p);
}

void spn_parser_error(SpnParser *p, const char *fmt, const void *args[])
{
	char *prefix, *msg;
	size_t prefix_len, msg_len;
	const void *prefix_args[1];
	prefix_args[0] = &p->lineno;

	if (p->error) {
		return; /* only report first syntax error */
	}

	prefix = spn_string_format_cstr(
		"syntax error near line %i: ",
		&prefix_len,
		prefix_args
	);

	msg = spn_string_format_cstr(fmt, &msg_len, args);

	free(p->errmsg);
	p->errmsg = spn_malloc(prefix_len + msg_len + 1);

	strcpy(p->errmsg, prefix);
	strcpy(p->errmsg + prefix_len, msg);

	free(prefix);
	free(msg);

	p->error = 1;
}

static void init_parser(SpnParser *p, const char *src)
{
	p->pos = src;
	p->eof = 0;
	p->error = 0;
	p->lineno = 1;
}

/* re-initialize the parser object (so that it can be reused for parsing
 * multiple translation units), then kick off the actual recursive descent
 * parser to process the source text
 */
SpnAST *spn_parser_parse(SpnParser *p, const char *src)
{
	init_parser(p, src);
	return parse_program(p);
}

SpnAST *spn_parser_parse_expression(SpnParser *p, const char *src)
{
	SpnAST *expr;

	init_parser(p, src);
	spn_lex(p);
	expr = parse_expr(p);

	if (expr == NULL) {
		return NULL;
	}

	if (p->eof) {
		SpnAST *return_stmt, *program;

		return_stmt = spn_ast_new(SPN_NODE_RETURN, 1);
		return_stmt->left = expr;

		program = spn_ast_new(SPN_NODE_PROGRAM, 1);
		program->left = return_stmt;

		return program;
	}

	/* it is an error if we are not at the end of the source */
	spn_ast_free(expr);
	spn_parser_error(p, "garbage after input", NULL);
	return NULL;
}

static SpnAST *parse_program(SpnParser *p)
{
	SpnAST *tree = NULL;

	if (spn_lex(p))	{	/* there are tokens */
		tree = parse_program_nonempty(p);
	} else {
		return p->error ? NULL : spn_ast_new(SPN_NODE_PROGRAM, p->lineno);
	}

	if (p->eof) {		/* if EOF after parsing, then all went fine */
		return tree;
	}

	/* if not, then there's garbage after the source */
	spn_ast_free(tree);
	spn_parser_error(p, "garbage after input", NULL);
	return NULL;
}

static SpnAST *parse_program_nonempty(SpnParser *p)
{
	SpnAST *ast;

	/* parse global statements */
	SpnAST *sub = parse_stmt(p, 1);
	if (sub == NULL) {
		return NULL;
	}

	while (!p->eof) {
		SpnAST *tmp;
		SpnAST *right = parse_stmt(p, 1);
		if (right == NULL) {
			spn_ast_free(sub);
			return NULL;
		}

		tmp = spn_ast_new(SPN_NODE_COMPOUND, p->lineno);
		tmp->left = sub;    /* this node    */
		tmp->right = right; /* next node    */
		sub = tmp;          /* update head  */
	}

	/* here the same hack is performed that is used in parse_block()
	 * (refer there for an explanation)
	 */

	if (sub->node == SPN_NODE_COMPOUND) {
		sub->node = SPN_NODE_PROGRAM;
		return sub;
	}

	ast = spn_ast_new(SPN_NODE_PROGRAM, p->lineno);
	ast->left = sub;
	return ast;
}

/* statement lists appear in block statements, so loop until `}' is found */
static SpnAST *parse_stmt_list(SpnParser *p)
{
	/* parse local statement */
	SpnAST *ast = parse_stmt(p, 0);
	if (ast == NULL) {
		return NULL;
	}

	while (p->curtok.tok != SPN_TOK_RBRACE) {
		SpnAST *tmp;
		SpnAST *right = parse_stmt(p, 0);
		if (right == NULL) {
			spn_ast_free(ast);
			return NULL;
		}

		tmp = spn_ast_new(SPN_NODE_COMPOUND, p->lineno);
		tmp->left = ast;    /* this node    */
		tmp->right = right; /* next node    */
		ast = tmp;          /* update head  */
	}

	return ast;
}

static SpnAST *parse_stmt(SpnParser *p, int is_global)
{
	switch (p->curtok.tok) {
	case SPN_TOK_IF:        return parse_if(p);
	case SPN_TOK_WHILE:     return parse_while(p);
	case SPN_TOK_DO:        return parse_do(p);
	case SPN_TOK_FOR:       return parse_for(p);
	case SPN_TOK_BREAK:     return parse_break(p);
	case SPN_TOK_CONTINUE:  return parse_continue(p);
	case SPN_TOK_RETURN:    return parse_return(p);
	case SPN_TOK_SEMICOLON: return parse_empty(p);
	case SPN_TOK_LBRACE:    return parse_block(p);
	case SPN_TOK_VAR:       return parse_vardecl(p);
	case SPN_TOK_FUNCTION:
		if (is_global) {
			/* assume function statement at file scope */
			return parse_function(p, 1);
		} else {
			/* and a function expression at local scope */
			return parse_expr_stmt(p);
		}
	case SPN_TOK_CONST:
		if (is_global) {
			return parse_const(p);
		} else {
			spn_parser_error(p, "`const' declarations are only allowed at file scope", NULL);
			return NULL;
		}
	default:
		return parse_expr_stmt(p);
	}
}

static SpnAST *parse_function(SpnParser *p, int is_stmt)
{
	SpnString *name;
	SpnAST *ast, *body;

	/* skip `function' keyword */
	if (!spn_accept(p, SPN_TOK_FUNCTION)) {
		spn_parser_error(p, "internal error, expected `function'", NULL);
		spn_value_release(&p->curtok.val);
		return NULL;
	}

	if (is_stmt) {
		/* named global function statement: a global constant,
		 * initialized with a named function expression
		 */

		if (p->curtok.tok != SPN_TOK_IDENT) {
			spn_parser_error(p, "expected function name in function statement", NULL);
			spn_value_release(&p->curtok.val);
			return NULL;
		}

		name = stringvalue(&p->curtok.val);

		/* skip identifier */
		spn_lex(p);
	} else {
		/* optionally named function expression, lambda */
		if (p->curtok.tok == SPN_TOK_IDENT) {
			name = stringvalue(&p->curtok.val);
			spn_lex(p);
		} else {
			name = NULL;
		}
	}

	if (!spn_accept(p, SPN_TOK_LPAREN)) {
		spn_parser_error(p, "expected `(' in function header", NULL);
		spn_value_release(&p->curtok.val);

		if (name != NULL) {
			spn_object_release(name);
		}

		return NULL;
	}

	ast = spn_ast_new(SPN_NODE_FUNCEXPR, p->lineno);
	ast->name = name; /* ownership transfer */

	if (!spn_accept(p, SPN_TOK_RPAREN)) {
		SpnAST *arglist = parse_decl_args(p);
		if (arglist == NULL) {
			spn_ast_free(ast);
			return NULL;
		}

		ast->left = arglist;

		if (!spn_accept(p, SPN_TOK_RPAREN)) { /* error */
			spn_parser_error(p, "expected `)' after function argument list", NULL);
			spn_value_release(&p->curtok.val);
			spn_ast_free(ast); /* frees `arglist' and `name' too */
			return NULL;
		}
	}

	body = parse_block(p);
	if (body == NULL) {
		spn_ast_free(ast);
		return NULL;
	}

	ast->right = body;

	/* if we are parsing a function statement, then we need to
	 * return a node for a global constant, initialized with a
	 * function _expression_.
	 * Else we can just return the function expression itself.
	 */

	if (is_stmt) {
		SpnAST *global = spn_ast_new(SPN_NODE_CONST, ast->lineno);

		/* The name of the global is the same as the function name.
		 * (we have to retain the name string because the AST assumes
		 * that it's a strong pointer, so if we add it to another
		 * node, we don't want it to be released twice if it doesn't
		 * have a reference count of two.)
		 */
		assert(name != NULL);

		spn_object_retain(name);
		global->left = ast;
		global->name = name;
		return global;
	} else {
		return ast;
	}
}

static SpnAST *parse_block(SpnParser *p)
{
	SpnAST *list, *ast;

	if (!spn_accept(p, SPN_TOK_LBRACE)) {
		spn_parser_error(p, "expected `{' in block statement", NULL);
		spn_value_release(&p->curtok.val);
		return NULL;
	}

	if (spn_accept(p, SPN_TOK_RBRACE)) {	/* empty block */
		return spn_ast_new(SPN_NODE_EMPTY, p->lineno);
	}

	list = parse_stmt_list(p);

	if (list == NULL) {
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_RBRACE)) {
		spn_parser_error(p, "expected `}' at end of block statement", NULL);
		spn_value_release(&p->curtok.val);
		spn_ast_free(list);
		return NULL;
	}

	/* hack: we look at the subtree that parse_stmt_list() returned.
	 * if it is a compound, we change its node from the generic
	 * SPN_NODE_COMPOUND to the correct SPN_NODE_BLOCK. If it isn't (i. e.
	 * it's a single statement), then we create a wrapper SPN_NODE_BLOCK
	 * node, add the original subtree as its (left) child, and then we
	 * return the new (block) node.
	 *
	 * The reason why SPN_NODE_BLOCK isn't used immediately
	 * in parse_stmt_list() is that it may return multiple levels of nested
	 * compounds, but only the top-level node should be marked as a block.
	 */

	if (list->node == SPN_NODE_COMPOUND) {
		list->node = SPN_NODE_BLOCK;
		return list;
	}

	ast = spn_ast_new(SPN_NODE_BLOCK, p->lineno);
	ast->left = list;
	return ast;
}

static SpnAST *parse_expr(SpnParser *p)
{
	return parse_assignment(p);
}

static SpnAST *parse_assignment(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_ASSIGN,
		SPN_TOK_PLUSEQ,
		SPN_TOK_MINUSEQ,
		SPN_TOK_MULEQ,
		SPN_TOK_DIVEQ,
		SPN_TOK_MODEQ,
		SPN_TOK_ANDEQ,
		SPN_TOK_OREQ,
		SPN_TOK_XOREQ,
		SPN_TOK_SHLEQ,
		SPN_TOK_SHREQ,
		SPN_TOK_DOTDOTEQ,
	};

	static const enum spn_ast_node nodes[] = {
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
	};

	return parse_binexpr_rightassoc(p, toks, nodes, COUNT(toks), parse_concat);
}

static SpnAST *parse_concat(SpnParser *p)
{
	static const enum spn_lex_token toks[] = { SPN_TOK_DOTDOT };
	static const enum spn_ast_node nodes[] = { SPN_NODE_CONCAT };
	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_condexpr);
}

static SpnAST *parse_condexpr(SpnParser *p)
{
	SpnAST *ast, *br_true, *br_false, *branches, *tmp;

	ast = parse_logical_or(p);
	if (ast == NULL) {
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_QMARK)) {
		return ast;
	}

	br_true = parse_expr(p);
	if (br_true == NULL) {
		spn_ast_free(ast);
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_COLON)) {
		/* error, expected ':' */
		spn_parser_error(p, "expected `:' in conditional expression", NULL);
		spn_value_release(&p->curtok.val);
		spn_ast_free(ast);
		spn_ast_free(br_true);
		return NULL;
	}

	br_false = parse_condexpr(p);
	if (br_false == NULL) {
		spn_ast_free(ast);
		spn_ast_free(br_true);
		return NULL;
	}

	branches = spn_ast_new(SPN_NODE_BRANCHES, p->lineno);
	branches->left  = br_true;
	branches->right = br_false;

	tmp = spn_ast_new(SPN_NODE_CONDEXPR, p->lineno);
	tmp->left = ast; /* condition */
	tmp->right = branches; /* true and false values */

	return tmp;
}

/* Functions to parse binary mathematical expressions
 * in ascending precedence order.
 */
static SpnAST *parse_logical_or(SpnParser *p)
{
	static const enum spn_lex_token toks[] = { SPN_TOK_LOGOR };
	static const enum spn_ast_node nodes[] = { SPN_NODE_LOGOR };
	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_logical_and);
}

static SpnAST *parse_logical_and(SpnParser *p)
{
	static const enum spn_lex_token toks[] = { SPN_TOK_LOGAND };
	static const enum spn_ast_node nodes[] = { SPN_NODE_LOGAND };
	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_comparison);
}

static SpnAST *parse_comparison(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_EQUAL,
		SPN_TOK_NOTEQ,
		SPN_TOK_LESS,
		SPN_TOK_GREATER,
		SPN_TOK_LEQ,
		SPN_TOK_GEQ
	};

	static const enum spn_ast_node nodes[] = {
		SPN_NODE_EQUAL,
		SPN_NODE_NOTEQ,
		SPN_NODE_LESS,
		SPN_NODE_GREATER,
		SPN_NODE_LEQ,
		SPN_NODE_GEQ
	};

	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_bitwise_or);
}

static SpnAST *parse_bitwise_or(SpnParser *p)
{
	static const enum spn_lex_token toks[] = { SPN_TOK_BITOR };
	static const enum spn_ast_node nodes[] = { SPN_NODE_BITOR };
	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_bitwise_xor);
}

static SpnAST *parse_bitwise_xor(SpnParser *p)
{
	static const enum spn_lex_token toks[] = { SPN_TOK_XOR };
	static const enum spn_ast_node nodes[] = { SPN_NODE_BITXOR };
	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_bitwise_and);
}

static SpnAST *parse_bitwise_and(SpnParser *p)
{
	static const enum spn_lex_token toks[] = { SPN_TOK_BITAND };
	static const enum spn_ast_node nodes[] = { SPN_NODE_BITAND };
	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_shift);
}

static SpnAST *parse_shift(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_SHL,
		SPN_TOK_SHR
	};

	static const enum spn_ast_node nodes[] = {
		SPN_NODE_SHL,
		SPN_NODE_SHR
	};

	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_additive);
}

static SpnAST *parse_additive(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_PLUS,
		SPN_TOK_MINUS
	};

	static const enum spn_ast_node nodes[] = {
		SPN_NODE_ADD,
		SPN_NODE_SUB
	};

	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_multiplicative);
}

static SpnAST *parse_multiplicative(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_MUL,
		SPN_TOK_DIV,
		SPN_TOK_MOD
	};

	static const enum spn_ast_node nodes[] = {
		SPN_NODE_MUL,
		SPN_NODE_DIV,
		SPN_NODE_MOD
	};

	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_prefix);
}

static SpnAST *parse_prefix(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_INCR,
		SPN_TOK_DECR,
		SPN_TOK_PLUS,
		SPN_TOK_MINUS,
		SPN_TOK_LOGNOT,
		SPN_TOK_BITNOT,
		SPN_TOK_SIZEOF,
		SPN_TOK_TYPEOF,
		SPN_TOK_HASH
	};

	static const enum spn_ast_node nodes[] = {
		SPN_NODE_PREINCRMT,
		SPN_NODE_PREDECRMT,
		SPN_NODE_UNPLUS,
		SPN_NODE_UNMINUS,
		SPN_NODE_LOGNOT,
		SPN_NODE_BITNOT,
		SPN_NODE_SIZEOF,
		SPN_NODE_TYPEOF,
		SPN_NODE_NTHARG
	};

	SpnAST *operand, *ast;
	int idx = spn_accept_multi(p, toks, COUNT(toks));
	if (idx < 0) {
		return parse_postfix(p);
	}

	/* right recursion for right-associative operators */
	operand = parse_prefix(p);
	if (operand == NULL) {	/* error */
		return NULL;
	}

	ast = spn_ast_new(nodes[idx], p->lineno);
	ast->left = operand;

	return ast;
}

static SpnAST *parse_postfix(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_INCR,
		SPN_TOK_DECR,
		SPN_TOK_LBRACKET,
		SPN_TOK_LPAREN,
		SPN_TOK_DOT,
		SPN_TOK_ARROW
	};

	static const enum spn_ast_node nodes[] = {
		SPN_NODE_POSTINCRMT,
		SPN_NODE_POSTDECRMT,
		SPN_NODE_ARRSUB,
		SPN_NODE_FUNCCALL,
		SPN_NODE_MEMBEROF,
		SPN_NODE_MEMBEROF
	};

	SpnAST *tmp, *expr, *ast;
	int idx;

	ast = parse_term(p);
	if (ast == NULL) {	/* error */
		return NULL;
	}

	/* iteration instead of left recursion - we want to terminate */
	while ((idx = spn_accept_multi(p, toks, COUNT(toks))) >= 0) {
		SpnAST *ident;
		tmp = spn_ast_new(nodes[idx], p->lineno);

		switch (nodes[idx]) {
		case SPN_NODE_POSTINCRMT:
		case SPN_NODE_POSTDECRMT:
			tmp->left = ast;
			break;
		case SPN_NODE_ARRSUB:
			expr = parse_expr(p);
			if (expr == NULL) { /* error  */
				spn_ast_free(ast);
				spn_ast_free(tmp);
				return NULL;
			}

			tmp->left = ast;
			tmp->right = expr;

			if (!spn_accept(p, SPN_TOK_RBRACKET)) {
				/* error: expected closing bracket */
				spn_parser_error(p, "expected `]' after expression in array subscript", NULL);
				spn_value_release(&p->curtok.val);
				/* this frees ast and expr as well */
				spn_ast_free(tmp);
				return NULL;
			}

			break;
		case SPN_NODE_MEMBEROF:
			if (p->curtok.tok != SPN_TOK_IDENT) { /* error: expected identifier as member */
				spn_parser_error(p, "expected identifier after . or -> operator", NULL);
				spn_ast_free(ast);
				spn_ast_free(tmp);
				return NULL;
			}

			ident = parse_term(p);

			if (ident == NULL) { /* error */
				spn_ast_free(ast);
				spn_ast_free(tmp);
				return NULL;
			}

			tmp->left = ast;
			tmp->right = ident;

			break;
		case SPN_NODE_FUNCCALL:
			tmp->left = ast;

			if (p->curtok.tok != SPN_TOK_RPAREN) {
				SpnAST *arglist = parse_call_args(p);

				if (arglist == NULL) {
					spn_ast_free(tmp); /* this frees `ast' too */
					return NULL;
				}

				tmp->right = arglist;
			}

			if (!spn_accept(p, SPN_TOK_RPAREN)) {
				/* error: expected closing parenthesis */
				spn_parser_error(p, "expected `)' after expression in function call", NULL);
				spn_value_release(&p->curtok.val);
				/* this frees ast and arglist as well */
				spn_ast_free(tmp);
				return NULL;
			}

			break;
		default:
			SHANT_BE_REACHED();
		}

		ast = tmp;
	}

	return ast;
}

static SpnAST *parse_term(SpnParser *p)
{
	SpnAST *ast;

	switch (p->curtok.tok) {
	case SPN_TOK_LPAREN:
		spn_lex(p);

		ast = parse_expr(p);
		if (ast == NULL) {
			return NULL;
		}

		if (!spn_accept(p, SPN_TOK_RPAREN)) {
			spn_parser_error(p, "expected `)' after parenthesized expression", NULL);
			spn_value_release(&p->curtok.val);
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	case SPN_TOK_LBRACE:
		return parse_array_literal(p);
	case SPN_TOK_ARGC:
		ast = spn_ast_new(SPN_NODE_ARGC, p->lineno);

		spn_lex(p);
		if (p->error) {
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	case SPN_TOK_FUNCTION:
		/* only allow function expressions in an expression */
		return parse_function(p, 0);
	case SPN_TOK_IDENT:
		ast = spn_ast_new(SPN_NODE_IDENT, p->lineno);
		ast->name = stringvalue(&p->curtok.val);

		spn_lex(p);
		if (p->error) {
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	case SPN_TOK_TRUE:
		ast = spn_ast_new(SPN_NODE_LITERAL, p->lineno);
		ast->value = makebool(1);

		spn_lex(p);
		if (p->error) {
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	case SPN_TOK_FALSE:
		ast = spn_ast_new(SPN_NODE_LITERAL, p->lineno);
		ast->value = makebool(0);

		spn_lex(p);
		if (p->error) {
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	case SPN_TOK_NIL:
		ast = spn_ast_new(SPN_NODE_LITERAL, p->lineno);
		ast->value = makenil();

		spn_lex(p);
		if (p->error) {
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	case SPN_TOK_INT:
	case SPN_TOK_FLOAT:
	case SPN_TOK_STR:
		ast = spn_ast_new(SPN_NODE_LITERAL, p->lineno);
		ast->value = p->curtok.val;

		spn_lex(p);
		if (p->error) {
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	default:
		{
			int tok = p->curtok.tok;
			const void *args[1];
			args[0] = &tok;
			spn_parser_error(p, "unexpected token %i", args);
			spn_value_release(&p->curtok.val);
			return NULL;
		}
	}
}

static SpnAST *parse_array_literal(SpnParser *p)
{
	SpnAST *ast = spn_ast_new(SPN_NODE_ARRAY_LITERAL, p->lineno);
	SpnAST *tail = ast;

	/* skip '{' */
	spn_lex(p);

	while (!spn_accept(p, SPN_TOK_RBRACE)) {
		SpnAST *pair, *node;

		/* parse key or value */
		SpnAST *key = NULL;
		SpnAST *val = parse_expr(p);
		if (val == NULL) {
			spn_ast_free(ast);
			return NULL;
		}

		if (spn_accept(p, SPN_TOK_COLON)) {
			key = val;
			val = parse_expr(p);
			if (val == NULL) {
				spn_ast_free(key);
				spn_ast_free(ast);
				return NULL;
			}
		}

		/* construct key-value pair */
		pair = spn_ast_new(SPN_NODE_ARRAY_KVPAIR, p->lineno);
		pair->left  = key;
		pair->right = val;

		/* and append it to the end of the link list */
		if (tail == ast && tail->left == NULL) {
			tail->left = pair;
		} else {
			node = spn_ast_new(SPN_NODE_ARRAY_LITERAL, p->lineno);
			node->left = pair;
			tail->right = node;
			tail = node;
		}

		if (spn_accept(p, SPN_TOK_COMMA)) {
			if (p->curtok.tok == SPN_TOK_RBRACE) {
				spn_parser_error(p, "trailing comma in array literal is prohibited", NULL);
				spn_ast_free(ast);
				return NULL;
			}
		} else {
			if (p->curtok.tok != SPN_TOK_RBRACE) {
				spn_parser_error(p, "expected ',' or '}' after value in array literal", NULL);
				spn_ast_free(ast);
				return NULL;
			}
		}
	}

	return ast;
}

/* this also makes a linked list */
static SpnAST *parse_decl_args(SpnParser *p)
{
	SpnAST *ast = NULL, *res;
	SpnString *name = stringvalue(&p->curtok.val);

	if (!spn_accept(p, SPN_TOK_IDENT)) {
		spn_parser_error(p, "expected identifier in function argument list", NULL);
		spn_value_release(&p->curtok.val);
		return NULL;
	}

	ast = spn_ast_new(SPN_NODE_DECLARGS, p->lineno);
	ast->name = name;

	res = ast; /* preserve head */

	while (spn_accept(p, SPN_TOK_COMMA)) {
		SpnAST *tmp;
		SpnString *name = objvalue(&p->curtok.val);

		if (!spn_accept(p, SPN_TOK_IDENT)) {
			spn_parser_error(p, "expected identifier in function argument list", NULL);
			spn_value_release(&p->curtok.val);
			spn_ast_free(ast);
			return NULL;
		}

		tmp = spn_ast_new(SPN_NODE_DECLARGS, p->lineno);
		tmp->name = name;   /* this is the actual name   */
		ast->left = tmp;    /* this builds the link list */
		ast = tmp;          /* update head               */
	}

	return res;
}

static SpnAST *parse_call_args(SpnParser *p)
{
	SpnAST *expr, *ast;

	expr = parse_expr(p);
	if (expr == NULL) {
		return NULL; /* fail */
	}

	ast = spn_ast_new(SPN_NODE_CALLARGS, p->lineno);
	ast->right = expr;

	while (spn_accept(p, SPN_TOK_COMMA)) {
		SpnAST *right = parse_expr(p);

		if (right == NULL) { /* fail */
			spn_ast_free(ast);
			return NULL;
		} else {
			SpnAST *tmp = spn_ast_new(SPN_NODE_CALLARGS, p->lineno);
			tmp->left = ast;    /* this node */
			tmp->right = right; /* next node */
			ast = tmp;          /* update head */
		}
	}

	return ast;
}

static SpnAST *parse_binexpr_rightassoc(
	SpnParser *p,
	const enum spn_lex_token toks[],
	const enum spn_ast_node nodes[],
	size_t n,
	SpnAST *(*subexpr)(SpnParser *)
)
{
	int idx;
	SpnAST *ast, *right, *tmp;

	ast = subexpr(p);
	if (ast == NULL) {
		return NULL;
	}

	idx = spn_accept_multi(p, toks, n);
	if (idx < 0) {
		return ast;
	}

	/* apply right recursion */
	right = parse_binexpr_rightassoc(p, toks, nodes, n, subexpr);

	if (right == NULL) {
		spn_ast_free(ast);
		return NULL;
	}

	tmp = spn_ast_new(nodes[idx], p->lineno);
	tmp->left = ast;
	tmp->right = right;

	return tmp;
}

static SpnAST *parse_binexpr_leftassoc(
	SpnParser *p,
	const enum spn_lex_token toks[],
	const enum spn_ast_node nodes[],
	size_t n,
	SpnAST *(*subexpr)(SpnParser *)
)
{
	int idx;
	SpnAST *ast = subexpr(p);
	if (ast == NULL) {
		return NULL;
	}

	/* iteration instead of left recursion (which wouldn't terminate) */
	while ((idx = spn_accept_multi(p, toks, n)) >= 0) {
		SpnAST *tmp, *right = subexpr(p);

		if (right == NULL) {
			spn_ast_free(ast);
			return NULL;
		}

		tmp = spn_ast_new(nodes[idx], p->lineno);
		tmp->left = ast;
		tmp->right = right;
		ast = tmp;
	}

	return ast;
}

/**************
 * Statements *
 **************/
static SpnAST *parse_if(SpnParser *p)
{
	SpnAST *cond, *br_then, *br_else, *br, *ast;
	/* skip `if' */
	if (!spn_lex(p)) {
		spn_parser_error(p, "expected condition after `if'", NULL);
		return NULL;
	}

	cond = parse_expr(p);
	if (cond == NULL) {
		return NULL;
	}

	br_then = parse_block(p);
	if (br_then == NULL) {
		spn_ast_free(cond);
		return NULL;
	}

	/* "else" is optional (hence we set its node to NULL beforehand).
	 * It may be followed by either a block or another if statement.
	 * The reason: we want to stay safe by enforcing blocks, but requiring
	 * the programmer to wrap each and every "else if" into a separate
	 * block is just insane and intolerably ugly -- "else if" is a
	 * common and safe enough construct to be allowed as an exception.
	 */
	br_else = NULL;
	if (spn_accept(p, SPN_TOK_ELSE)) {
		if (p->curtok.tok == SPN_TOK_LBRACE) {
			br_else = parse_block(p);
		} else if (p->curtok.tok == SPN_TOK_IF) {
			br_else = parse_if(p);
		} else {
			spn_parser_error(p, "expected block or 'if' after 'else'", NULL);
			spn_ast_free(cond);
			spn_ast_free(br_then);
			return NULL;
		}

		if (br_else == NULL) {
			spn_ast_free(cond);
			spn_ast_free(br_then);
			return NULL;
		}
	}

	br = spn_ast_new(SPN_NODE_BRANCHES, p->lineno);
	br->left = br_then;
	br->right = br_else;

	ast = spn_ast_new(SPN_NODE_IF, p->lineno);
	ast->left = cond;
	ast->right = br;

	return ast;
}

static SpnAST *parse_while(SpnParser *p)
{
	SpnAST *cond, *body, *ast;

	/* skip `while' */
	if (!spn_lex(p)) {
		spn_parser_error(p, "expected condition after `while'", NULL);
		return NULL;
	}

	cond = parse_expr(p);
	if (cond == NULL) {
		return NULL;
	}

	body = parse_block(p);
	if (body == NULL) {
		spn_ast_free(cond);
		return NULL;
	}

	ast = spn_ast_new(SPN_NODE_WHILE, p->lineno);
	ast->left = cond;
	ast->right = body;

	return ast;
}

static SpnAST *parse_do(SpnParser *p)
{
	SpnAST *cond, *body, *ast;

	/* skip `do' */
	if (!spn_lex(p)) {
		spn_parser_error(p, "expected loop body after `do'", NULL);
		return NULL;
	}

	body = parse_block(p);
	if (body == NULL) {
		return NULL;
	}

	/* expect "while expr;" */
	if (!spn_accept(p, SPN_TOK_WHILE)) {
		spn_parser_error(p, "expected `while' after body of do-while statement", NULL);
		spn_value_release(&p->curtok.val);
		spn_ast_free(body);
		return NULL;
	}

	cond = parse_expr(p);
	if (cond == NULL) {
		spn_ast_free(body);
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_SEMICOLON)) {
		spn_parser_error(p, "expected `;' after condition of do-while statement", NULL);
		spn_value_release(&p->curtok.val);
		spn_ast_free(body);
		spn_ast_free(cond);
		return NULL;
	}

	ast = spn_ast_new(SPN_NODE_DO, p->lineno);
	ast->left = cond;
	ast->right = body;

	return ast;
}

static SpnAST *parse_for(SpnParser *p)
{
	SpnAST *init, *cond, *incr, *body, *h1, *h2, *h3, *ast;
	int parens = 0;

	/* skip `for' */
	if (!spn_lex(p)) {
		spn_parser_error(p, "expected initializer after `for'", NULL);
		return NULL;
	}

	if (spn_accept(p, SPN_TOK_LPAREN)) {
		parens = 1;
	}

	/* the initialization may be either an expression or a declaration */
	if (p->curtok.tok == SPN_TOK_VAR) {
		init = parse_vardecl(p);

		if (init == NULL) {
			return NULL;
		}
	} else {
		init = parse_expr(p);

		if (init == NULL) {
			return NULL;
		}

		if (!spn_accept(p, SPN_TOK_SEMICOLON)) {
			spn_parser_error(p, "expected `;' after initialization of for loop", NULL);
			spn_value_release(&p->curtok.val);
			spn_ast_free(init);
			return NULL;
		}
	}

	cond = parse_expr(p);
	if (cond == NULL) {
		spn_ast_free(init);
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_SEMICOLON)) {
		spn_parser_error(p, "expected `;' after condition of for loop", NULL);
		spn_value_release(&p->curtok.val);
		spn_ast_free(init);
		spn_ast_free(cond);
		return NULL;
	}

	incr = parse_expr(p);
	if (incr == NULL) {
		spn_ast_free(init);
		spn_ast_free(cond);
		return NULL;
	}

	if (parens) {
		if (!spn_accept(p, SPN_TOK_RPAREN)) {
			spn_parser_error(p, "expected ')' after for loop header", NULL);
			spn_ast_free(init);
			spn_ast_free(cond);
			return NULL;
		}
	}

	body = parse_block(p);
	if (body == NULL) {
		spn_ast_free(init);
		spn_ast_free(cond);
		spn_ast_free(incr);
		return NULL;
	}

	/* linked list for the loop header */
	h1 = spn_ast_new(SPN_NODE_FORHEADER, p->lineno);
	h2 = spn_ast_new(SPN_NODE_FORHEADER, p->lineno);
	h3 = spn_ast_new(SPN_NODE_FORHEADER, p->lineno);

	h1->left = init;
	h1->right = h2;
	h2->left = cond;
	h2->right = h3;
	h3->left = incr;

	ast = spn_ast_new(SPN_NODE_FOR, p->lineno);
	ast->left = h1;
	ast->right = body;

	return ast;
}

static SpnAST *parse_break(SpnParser *p)
{
	/* skip `break' */
	if (!spn_lex(p)) {
		spn_parser_error(p, "expected `;' after `break'", NULL);
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_SEMICOLON)) {
		spn_parser_error(p, "expected `;' after `break'", NULL);
		spn_value_release(&p->curtok.val);
		return NULL;
	}

	return spn_ast_new(SPN_NODE_BREAK, p->lineno);
}

static SpnAST *parse_continue(SpnParser *p)
{
	/* skip `continue' */
	if (!spn_lex(p)) {
		spn_parser_error(p, "expected `;' after `continue'", NULL);
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_SEMICOLON)) {
		spn_parser_error(p, "expected `;' after `continue'", NULL);
		spn_value_release(&p->curtok.val);
		return NULL;
	}

	return spn_ast_new(SPN_NODE_CONTINUE, p->lineno);
}

static SpnAST *parse_return(SpnParser *p)
{
	SpnAST *expr;

	/* skip `return' */
	if (!spn_lex(p)) {
		spn_parser_error(p, "expected expression or `;' after `return'", NULL);
		return NULL;
	}

	if (spn_accept(p, SPN_TOK_SEMICOLON)) {
		return spn_ast_new(SPN_NODE_RETURN, p->lineno); /* return without value */
	}

	expr = parse_expr(p);
	if (expr == NULL) {
		return NULL;
	}

	if (spn_accept(p, SPN_TOK_SEMICOLON)) {
		SpnAST *ast = spn_ast_new(SPN_NODE_RETURN, p->lineno);
		ast->left = expr;
		return ast;
	}

	spn_parser_error(p, "expected `;' after expression in return statement", NULL);
	spn_ast_free(expr);

	return NULL;
}

/* this builds a link list of comma-separated variable declarations */
static SpnAST *parse_vardecl(SpnParser *p)
{
	/* `ast' is the head of the list */
	SpnAST *ast = NULL, *tail = NULL;

	/* skip "var" keyword */
	spn_lex(p);

	do {
		SpnString *name = stringvalue(&p->curtok.val);
		SpnAST *expr = NULL, *tmp;

		if (!spn_accept(p, SPN_TOK_IDENT)) {
			spn_parser_error(p, "expected identifier in variable declaration", NULL);
			spn_value_release(&p->curtok.val);
			return NULL;
		}

		if (spn_accept(p, SPN_TOK_ASSIGN)) {
			expr = parse_expr(p);

			if (expr == NULL) {
				spn_object_release(name);
				spn_ast_free(ast);
				return NULL;
			}
		}

		tmp = spn_ast_new(SPN_NODE_VARDECL, p->lineno);
		tmp->name = name;
		tmp->left = expr;

		if (ast == NULL) {
			ast = tmp;
		} else {
			assert(tail != NULL); /* if I've got the logic right */
			tail->right = tmp;
		}

		tail = tmp;
	} while (spn_accept(p, SPN_TOK_COMMA));

	if (!spn_accept(p, SPN_TOK_SEMICOLON)) {
		spn_parser_error(p, "expected `;' after variable initialization", NULL);
		spn_value_release(&p->curtok.val);
		spn_ast_free(ast);
		return NULL;
	}

	return ast;
}

static SpnAST *parse_const(SpnParser *p)
{
	/* `ast' is the head of the list */
	SpnAST *ast = NULL, *tail = NULL;

	/* skip "const" or "global" keyword */
	spn_lex(p);

	do {
		SpnString *name;
		SpnAST *expr, *tmp;

		if (p->curtok.tok != SPN_TOK_IDENT) {
			spn_parser_error(p, "expected identifier in const declaration", NULL);
			spn_value_release(&p->curtok.val);
			return NULL;
		}

		name = stringvalue(&p->curtok.val);
		spn_lex(p);

		if (!spn_accept(p, SPN_TOK_ASSIGN)) {
			spn_parser_error(p, "expected `=' after identifier in const declaration", NULL);
			spn_ast_free(ast);
			return NULL;
		}

		expr = parse_expr(p);
		if (expr == NULL) {
			spn_object_release(name);
			spn_ast_free(ast);
			return NULL;
		}

		tmp = spn_ast_new(SPN_NODE_CONST, p->lineno);
		tmp->name = name;
		tmp->left = expr;

		if (ast == NULL) {
			ast = tmp;
		} else {
			assert(tail != NULL);
			tail->right = tmp;
		}

		tail = tmp;
	} while (spn_accept(p, SPN_TOK_COMMA));

	if (!spn_accept(p, SPN_TOK_SEMICOLON)) {
		spn_parser_error(p, "expected `;' after constant initialization", NULL);
		spn_value_release(&p->curtok.val);
		spn_ast_free(ast);
		return NULL;
	}

	return ast;
}

static SpnAST *parse_expr_stmt(SpnParser *p)
{
	SpnAST *ast = parse_expr(p);
	if (ast == NULL) {
		return NULL;
	}

	if (spn_accept(p, SPN_TOK_SEMICOLON)) {
		return ast;
	}

	spn_parser_error(p, "expected `;' after expression", NULL);
	spn_value_release(&p->curtok.val);
	spn_ast_free(ast);

	return NULL;
}

static SpnAST *parse_empty(SpnParser *p)
{
	/* skip semicolon */
	spn_lex(p);
	if (p->error) {
		return NULL;
	}

	return spn_ast_new(SPN_NODE_EMPTY, p->lineno);
}
