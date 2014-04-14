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


SpnAST *spn_ast_new(enum spn_ast_node node, int lineno)
{
	SpnAST *ast = spn_malloc(sizeof(*ast));

	ast->node	= node;
	ast->value	= makenil();
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
