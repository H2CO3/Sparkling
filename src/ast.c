/*
 * ast.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 03/05/2013
 * Licensed under the 2-clause BSD License
 *
 * AST: a right-leaning abstract syntax tree
 */

#include <stdlib.h>
#include "ast.h"
#include "private.h"


static void dump_ast(SpnAST *ast, int indent);

SpnAST *spn_ast_new(enum spn_ast_node node, unsigned long lineno)
{
	SpnAST *ast = malloc(sizeof(*ast));
	if (ast == NULL) {
		abort();
	}

	ast->node	= node;
	ast->value.t	= SPN_TYPE_NIL;
	ast->value.f	= 0;
	ast->name	= NULL;
	ast->lineno	= lineno;
	ast->left	= NULL;
	ast->right	= NULL;

	return ast;
}

void spn_ast_free(SpnAST *ast)
{
	if (ast == NULL) {
		return;
	}

	spn_value_release(&ast->value);

	if (ast->name != NULL) {
		spn_object_release(ast->name);
	}

	spn_ast_free(ast->left);
	spn_ast_free(ast->right);

	free(ast);
}

static void dump_indent(int i)
{
	while (i-- > 0) {
		fputs("    ", stdout);
	}
}

void spn_ast_dump(SpnAST *ast)
{
	dump_ast(ast, 0);
}

static void dump_ast(SpnAST *ast, int indent)
{
	static const char *const nodnam[] = {
		"program",
		"block-statement",
		"function-statement",

		"while",
		"do-while",
		"for",
		"foreach",
		"if",

		"break",
		"continue",
		"return",
		"empty-statement",
		"vardecl",
		"global-constant",

		"assign",
		"assign-add",
		"assign-subtract",
		"assign-multiply",
		"assign-divide",
		"assign-modulo",
		"assign-and",
		"assign-or",
		"assign-xor",
		"assign-left-shift",
		"assign-right-shift",
		"assign-concat",

		"concatenate",
		"conditional-ternary",

		"add",
		"subtract",
		"multiply",
		"divide",
		"modulo",

		"logical-and",
		"logical-or",
		"bitwise-and",
		"bitwise-or",
		"bitwise-xor",
		"left-shift",
		"right-shift",

		"equals",
		"not-equal",
		"less-than",
		"less-than-or-equal",
		"greater-than",
		"greater-than-or-equal",

		"unary-plus",
		"unary-minus",
		"preincrement",
		"predecrement",
		"sizeof",
		"typeof",
		"logical-not",
		"bitwise-not",
		"nth-arg",

		"postincrement",
		"postdecrement",
		"array-subscript",
		"memberof",
		"function-call",

		"identifier",
		"literal",
		"function-expr",

		"decl-argument",
		"call-argument",
		"branches",
		"for-header",
		"generic-compound"
	};

	dump_indent(indent);
	printf("(%s", nodnam[ast->node]);

	/* print name, if any */
	if (ast->name != NULL) {
		printf(" name = \"%s\"", ast->name->cstr);
	}

	/* print formatted value */
	if ((ast->value.t == SPN_TYPE_NIL && ast->node == SPN_NODE_LITERAL)
	  || ast->value.t != SPN_TYPE_NIL) {
		printf(" value = ");
		spn_value_print(&ast->value);
	}

	if (ast->left != NULL || ast->right != NULL) {
		fputc('\n', stdout);
	}

	/* dump subtrees, if any */
	if (ast->left != NULL) {
		dump_ast(ast->left, indent + 1);
	}

	if (ast->right != NULL) {
		dump_ast(ast->right, indent + 1);
	}

	if (ast->left != NULL || ast->right != NULL) {
		dump_indent(indent);
	}

	puts(")");
}

