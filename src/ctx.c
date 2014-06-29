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
#include "func.h"
#include "private.h"


struct SpnContext {
	SpnParser *parser;
	SpnCompiler *cmp;
	SpnVMachine *vm;
	SpnArray *programs; /* holds all programs ever compiled */

	enum spn_error_type errtype; /* type of the last error */
	const char *errmsg; /* last error message */

	void *info; /* context info initialized to NULL, use freely */
};

SpnContext *spn_ctx_new(void)
{
	SpnContext *ctx = spn_malloc(sizeof(*ctx));

	ctx->parser   = spn_parser_new();
	ctx->cmp      = spn_compiler_new();
	ctx->vm       = spn_vm_new();
	ctx->programs = spn_array_new();
	ctx->errtype  = SPN_ERROR_OK;
	ctx->errmsg   = NULL;
	ctx->info     = NULL;

	spn_vm_setcontext(ctx->vm, ctx);
	spn_load_stdlib(ctx->vm);

	return ctx;
}

void spn_ctx_free(SpnContext *ctx)
{
	spn_parser_free(ctx->parser);
	spn_compiler_free(ctx->cmp);
	spn_vm_free(ctx->vm);

	spn_object_release(ctx->programs);
	free(ctx);
}

enum spn_error_type spn_ctx_geterrtype(SpnContext *ctx)
{
	return ctx->errtype;
}

const char *spn_ctx_geterrmsg(SpnContext *ctx)
{
	switch (ctx->errtype) {
	case SPN_ERROR_OK:		return NULL;
	case SPN_ERROR_SYNTAX:		return ctx->parser->errmsg;
	case SPN_ERROR_SEMANTIC:	return spn_compiler_errmsg(ctx->cmp);
	case SPN_ERROR_RUNTIME:		return spn_vm_geterrmsg(ctx->vm);
	case SPN_ERROR_GENERIC:		return ctx->errmsg;
	default:			return NULL;
	}
}

void spn_ctx_clearerror(SpnContext *ctx)
{
	ctx->errtype = SPN_ERROR_OK;
}

SpnArray *spn_ctx_getprograms(SpnContext *ctx)
{
	return ctx->programs;
}

void *spn_ctx_getuserinfo(SpnContext *ctx)
{
	return ctx->info;
}

void spn_ctx_setuserinfo(SpnContext *ctx, void *info)
{
	ctx->info = info;
}

/* private helper function for adding a program to
 * the list of compiled programs in a context
 */
static void add_to_programs(SpnContext *ctx, const SpnValue *fn)
{
	size_t idx = spn_array_count(ctx->programs);
	spn_array_set_intkey(ctx->programs, idx, fn);
}

/* the essence */

int spn_ctx_loadstring(SpnContext *ctx, const char *str, SpnValue *result)
{
	SpnAST *ast;
	int err;

	ctx->errtype = SPN_ERROR_OK;

	/* attempt parsing, handle error */
	ast = spn_parser_parse(ctx->parser, str);
	if (ast == NULL) {
		ctx->errtype = SPN_ERROR_SYNTAX;
		return -1;
	}

	/* attempt compilation, handle error */
	err = spn_compiler_compile(ctx->cmp, ast, result);
	spn_ast_free(ast);

	if (err != 0) {
		ctx->errtype = SPN_ERROR_SEMANTIC;
		return -2;
	}

	/* add compiled bytecode to program list */
	add_to_programs(ctx, result);
	spn_value_release(result); /* still alive, retained by array */
	return 0;
}

int spn_ctx_loadsrcfile(SpnContext *ctx, const char *fname, SpnValue *result)
{
	char *src;
	int err;

	ctx->errtype = SPN_ERROR_OK;

	src = spn_read_text_file(fname);
	if (src == NULL) {
		ctx->errtype = SPN_ERROR_GENERIC;
		ctx->errmsg = "I/O error: could not read source file";
		return -1;
	}

	err = spn_ctx_loadstring(ctx, src, result);
	free(src);

	return err;
}

int spn_ctx_loadobjfile(SpnContext *ctx, const char *fname, SpnValue *result)
{
	spn_uword *bc;
	size_t filesize, nwords;

	ctx->errtype = SPN_ERROR_OK;

	bc = spn_read_binary_file(fname, &filesize);
	if (bc == NULL) {
		ctx->errtype = SPN_ERROR_GENERIC;
		ctx->errmsg = "I/O error: could not read object file";
		return -1;
	}

	/* the size of the object file is not the same
	 * as the number of machine words in the bytecode
	 */
	nwords = filesize / sizeof(*bc);
	*result = maketopprgfunc(SPN_TOPFN, bc, nwords);

	add_to_programs(ctx, result);
	spn_value_release(result); /* still alive, retained by array */
	return 0;
}

int spn_ctx_execstring(SpnContext *ctx, const char *str, SpnValue *ret)
{
	SpnValue fn;

	if (spn_ctx_loadstring(ctx, str, &fn) != 0) {
		return -1;
	}

	return spn_ctx_callfunc(ctx, &fn, ret, 0, NULL);
}

int spn_ctx_execsrcfile(SpnContext *ctx, const char *fname, SpnValue *ret)
{
	SpnValue fn;

	if (spn_ctx_loadsrcfile(ctx, fname, &fn) != 0) {
		return -1;
	}

	return spn_ctx_callfunc(ctx, &fn, ret, 0, NULL);
}

int spn_ctx_execobjfile(SpnContext *ctx, const char *fname, SpnValue *ret)
{
	SpnValue fn;

	if (spn_ctx_loadobjfile(ctx, fname, &fn) != 0) {
		return -1;
	}

	return spn_ctx_callfunc(ctx, &fn, ret, 0, NULL);
}

/* abstraction (well, sort of) of the virtual machine API */

int spn_ctx_callfunc(SpnContext *ctx, const SpnValue *func, SpnValue *ret, int argc, SpnValue argv[])
{
	int status;

	ctx->errtype = SPN_ERROR_OK;

	status = spn_vm_callfunc(ctx->vm, func, ret, argc, argv);
	if (status != 0) {
		ctx->errtype = SPN_ERROR_RUNTIME;
	}

	return status;
}

void spn_ctx_runtime_error(SpnContext *ctx, const char *fmt, const void *args[])
{
	spn_vm_seterrmsg(ctx->vm, fmt, args);
}

const char **spn_ctx_stacktrace(SpnContext *ctx, size_t *size)
{
	return spn_vm_stacktrace(ctx->vm, size);
}

void spn_ctx_addlib_cfuncs(SpnContext *ctx, const char *libname, const SpnExtFunc fns[], size_t n)
{
	spn_vm_addlib_cfuncs(ctx->vm, libname, fns, n);
}

void spn_ctx_addlib_values(SpnContext *ctx, const char *libname, const SpnExtValue vals[], size_t n)
{
	spn_vm_addlib_values(ctx->vm, libname, vals, n);
}

SpnArray *spn_ctx_getglobals(SpnContext *ctx)
{
	return spn_vm_getglobals(ctx->vm);
}

