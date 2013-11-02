/*
 * ctx.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 12/10/2013
 * Licensed under the 2-clause BSD License
 *
 * A convenience context API
 */

#include "ctx.h"

static void prepend_bytecode_list(SpnContext *ctx, spn_uword *bc, size_t len);
static void free_bytecode_list(struct spn_bc_list *head);

SpnContext *spn_ctx_new()
{
	SpnContext *ctx = malloc(sizeof(*ctx));
	if (ctx == NULL) {
		abort();
	}

	ctx->p      = spn_parser_new();
	ctx->cmp    = spn_compiler_new();
	ctx->vm     = spn_vm_new();
	ctx->bclist = NULL;
	ctx->errmsg = NULL;
	ctx->info   = NULL;

	spn_vm_setcontext(ctx->vm, ctx);
	spn_load_stdlib(ctx->vm);

	return ctx;
}

void spn_ctx_free(SpnContext *ctx)
{
	spn_parser_free(ctx->p);
	spn_compiler_free(ctx->cmp);
	spn_vm_free(ctx->vm);

	free_bytecode_list(ctx->bclist);
	free(ctx);
}

spn_uword *spn_ctx_loadstring(SpnContext *ctx, const char *str)
{
	SpnAST *ast;
	spn_uword *bc;
	size_t len;

	/* attempt parsing, handle error */
	ast = spn_parser_parse(ctx->p, str);
	if (ast == NULL) {
		ctx->errmsg = ctx->p->errmsg;
		return NULL;
	}

	/* attempt compilation, handle error */
	bc = spn_compiler_compile(ctx->cmp, ast, &len);
	spn_ast_free(ast);

	if (bc == NULL) {
		ctx->errmsg = spn_compiler_errmsg(ctx->cmp);
		return NULL;
	}

	/* prepend bytecode to the link list */
	prepend_bytecode_list(ctx, bc, len);

	return bc;
}

spn_uword *spn_ctx_loadsrcfile(SpnContext *ctx, const char *fname)
{
	char *src;
	spn_uword *bc;

	src = spn_read_text_file(fname);
	if (src == NULL) {
		ctx->errmsg = "Sparkling: I/O error: could not read source file";
		return NULL;
	}

	bc = spn_ctx_loadstring(ctx, src);
	free(src);

	return bc;
}

spn_uword *spn_ctx_loadobjfile(SpnContext *ctx, const char *fname)
{
	spn_uword *bc;
	size_t filesize, nwords;

	bc = spn_read_binary_file(fname, &filesize);
	if (bc == NULL) {
		ctx->errmsg = "Sparkling: I/O error: could not read object file";
		return NULL;
	}

	/* the size of the object file is not the same
	 * as the number of machine words in the bytecode
	 */
	nwords = filesize / sizeof(*bc);
	prepend_bytecode_list(ctx, bc, nwords);

	return bc;
}

SpnValue *spn_ctx_execstring(SpnContext *ctx, const char *str)
{
	spn_uword *bc = spn_ctx_loadstring(ctx, str);
	if (bc == NULL) {
		return NULL;
	}

	return spn_ctx_execbytecode(ctx, bc);
}

SpnValue *spn_ctx_execsrcfile(SpnContext *ctx, const char *fname)
{
	spn_uword *bc = spn_ctx_loadsrcfile(ctx, fname);
	if (bc == NULL) {
		return NULL;
	}

	return spn_ctx_execbytecode(ctx, bc);
}

SpnValue *spn_ctx_execobjfile(SpnContext *ctx, const char *fname)
{
	spn_uword *bc = spn_ctx_loadobjfile(ctx, fname);
	if (bc == NULL) {
		return NULL;
	}

	return spn_ctx_execbytecode(ctx, bc);
}

/* NB: this does **not** add the bytecode to the linked list */
SpnValue *spn_ctx_execbytecode(SpnContext *ctx, spn_uword *bc)
{
	SpnValue *val = spn_vm_exec(ctx->vm, bc);
	if (val == NULL) {
		ctx->errmsg = spn_vm_errmsg(ctx->vm);
		return NULL;
	}

	return val;
}

/* private bytecode link list functions */

static void prepend_bytecode_list(SpnContext *ctx, spn_uword *bc, size_t len)
{
	struct spn_bc_list *node = malloc(sizeof(*node));
	if (node == NULL) {
		abort();
	}

	node->bc = bc;
	node->len = len;
	node->next = ctx->bclist;
	ctx->bclist = node;
}

static void free_bytecode_list(struct spn_bc_list *head)
{
	while (head != NULL) {
		struct spn_bc_list *tmp = head->next;
		free(head->bc);
		free(head);
		head = tmp;
	}
}

