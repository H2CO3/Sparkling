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
#include "debug.h"


void spn_ctx_init(SpnContext *ctx)
{
	spn_parser_init(&ctx->parser);

	ctx->cmp      = spn_compiler_new();
	ctx->vm       = spn_vm_new();
	ctx->programs = spn_array_new();
	ctx->errtype  = SPN_ERROR_OK;
	ctx->errmsg   = NULL;
	ctx->info     = NULL;

#if USE_DYNAMIC_LOADING
	ctx->dynmods  = spn_array_new();
#else /* USE_DYNAMIC_LOADING */
	ctx->dynmods  = NULL;
#endif /* USE_DYNAMIC_LOADING */

	spn_vm_setcontext(ctx->vm, ctx);
	spn_load_stdlib(ctx->vm);
}

#if USE_DYNAMIC_LOADING
static void close_dynmod_handles(SpnContext *ctx)
{
	size_t i;
	size_t n = spn_array_count(ctx->dynmods);

	for (i = 0; i < n; i++) {
		SpnValue val = spn_array_get(ctx->dynmods, i);
		void *handle = ptrvalue(&val);

		/* call destructor, if any */
		SpnLibCloseFunc closefunc;
		closefunc = (SpnLibCloseFunc)spn_get_symbol(handle, SPN_LIB_CLOSE_FUNC_STR);

		if (closefunc) {
			closefunc(ctx);
		}

		spn_close_library(handle);
	}

	spn_object_release(ctx->dynmods);
}
#endif /* USE_DYNAMIC_LOADING */

void spn_ctx_free(SpnContext *ctx)
{
	spn_parser_free(&ctx->parser);
	spn_compiler_free(ctx->cmp);
	spn_vm_free(ctx->vm);

	spn_object_release(ctx->programs);

#if USE_DYNAMIC_LOADING
	close_dynmod_handles(ctx);
#endif /* USE_DYNAMIC_LOADING */
}

enum spn_error_type spn_ctx_geterrtype(SpnContext *ctx)
{
	return ctx->errtype;
}

const char *spn_ctx_geterrmsg(SpnContext *ctx)
{
	switch (ctx->errtype) {
	case SPN_ERROR_OK:       return NULL;
	case SPN_ERROR_SYNTAX:   return ctx->parser.errmsg;
	case SPN_ERROR_SEMANTIC: return spn_compiler_errmsg(ctx->cmp);
	case SPN_ERROR_RUNTIME:  return spn_vm_geterrmsg(ctx->vm);
	case SPN_ERROR_GENERIC:  return ctx->errmsg;
	default:                 return NULL;
	}
}

SpnSourceLocation spn_ctx_geterrloc(SpnContext *ctx)
{
	SpnSourceLocation zero_loc = { 0, 0 };

	switch (ctx->errtype) {
	case SPN_ERROR_SYNTAX:   return spn_parser_get_error_location(&ctx->parser);
	case SPN_ERROR_SEMANTIC: return spn_compiler_errloc(ctx->cmp);
	default:                 return zero_loc; /* runtime; generic; no error */
	}
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
static void add_to_programs(SpnContext *ctx, SpnFunction *fn)
{
	SpnValue val;
	val.type = SPN_TYPE_FUNC;
	val.v.o = fn;
	spn_array_push(ctx->programs, &val);
}

/* the essence */

SpnFunction *spn_ctx_compile_string(SpnContext *ctx, const char *str, int debug)
{
	SpnFunction *result;

	/* attempt parsing, handle error */
	SpnHashMap *ast = spn_ctx_parse(ctx, str);
	if (ast == NULL) {
		return NULL;
	}

	/* attempt compilation, add function to context */
	result = spn_ctx_compile_ast(ctx, ast, debug);
	spn_object_release(ast);

	return result;
}

SpnFunction *spn_ctx_compile_srcfile(SpnContext *ctx, const char *fname, int debug)
{
	char *src;
	SpnFunction *result;

	ctx->errtype = SPN_ERROR_OK;

	src = spn_read_text_file(fname);
	if (src == NULL) {
		ctx->errtype = SPN_ERROR_GENERIC;
		ctx->errmsg = "I/O error: could not read source file";
		return NULL;
	}

	result = spn_ctx_compile_string(ctx, src, debug);
	free(src);

	spn_dbg_set_filename(result->debug_info, fname);

	return result;
}

SpnFunction *spn_ctx_loadobjfile(SpnContext *ctx, const char *fname)
{
	spn_uword *bc;
	size_t filesize, nwords;
	SpnFunction *result;

	ctx->errtype = SPN_ERROR_OK;

	bc = spn_read_binary_file(fname, &filesize);
	if (bc == NULL) {
		ctx->errtype = SPN_ERROR_GENERIC;
		ctx->errmsg = "I/O error: could not read object file";
		return NULL;
	}

	/* the size of the object file is not the same
	 * as the number of machine words in the bytecode
	 */
	nwords = filesize / sizeof bc[0];
	result = spn_func_new_topprg(SPN_TOPFN, bc, nwords, NULL);

	add_to_programs(ctx, result);
	spn_object_release(result); /* still alive, retained by array */
	return result;
}

SpnFunction *spn_ctx_loadobjdata(SpnContext *ctx, const void *objdata, size_t objsize)
{
	size_t nwords;
	spn_uword *bc = spn_malloc(objsize);
	SpnFunction *result;

	memcpy(bc, objdata, objsize);
	ctx->errtype = SPN_ERROR_OK;

	/* the size of the object file is not the same
	 * as the number of machine words in the bytecode
	 */
	nwords = objsize / sizeof bc[0];
	result = spn_func_new_topprg(SPN_TOPFN, bc, nwords, NULL);

	add_to_programs(ctx, result);
	spn_object_release(result); /* still alive, retained by array */
	return result;
}

int spn_ctx_execstring(SpnContext *ctx, const char *str, SpnValue *ret)
{
	SpnFunction *fn = spn_ctx_compile_string(ctx, str, 1); /* debug */

	if (fn == NULL) {
		return -1;
	}

	return spn_ctx_callfunc(ctx, fn, ret, 0, NULL);
}

int spn_ctx_execsrcfile(SpnContext *ctx, const char *fname, SpnValue *ret)
{
	SpnFunction *fn = spn_ctx_compile_srcfile(ctx, fname, 1); /* debug */

	if (fn == NULL) {
		return -1;
	}

	return spn_ctx_callfunc(ctx, fn, ret, 0, NULL);
}

int spn_ctx_execobjfile(SpnContext *ctx, const char *fname, SpnValue *ret)
{
	SpnFunction *fn = spn_ctx_loadobjfile(ctx, fname);

	if (fn == NULL) {
		return -1;
	}

	return spn_ctx_callfunc(ctx, fn, ret, 0, NULL);
}

int spn_ctx_execobjdata(SpnContext *ctx, const void *objdata, size_t objsize, SpnValue *ret)
{
	SpnFunction *fn = spn_ctx_loadobjdata(ctx, objdata, objsize);

	if (fn == NULL) {
		return -1;
	}

	return spn_ctx_callfunc(ctx, fn, ret, 0, NULL);
}

/* abstraction (well, sort of) of the virtual machine API */

int spn_ctx_callfunc(SpnContext *ctx, SpnFunction *func, SpnValue *ret, int argc, SpnValue argv[])
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

SpnFunction *spn_ctx_compile_expr(SpnContext *ctx, const char *expr, int debug)
{
	SpnFunction *result;

	/* parse as expression */
	SpnHashMap *ast = spn_ctx_parse_expr(ctx, expr);
	if (ast == NULL) {
		return NULL;
	}

	/* compile AST and add resulting function to context */
	result = spn_ctx_compile_ast(ctx, ast, debug);
	spn_object_release(ast);

	return result;
}

SpnHashMap *spn_ctx_parse(SpnContext *ctx, const char *src)
{
	SpnHashMap *ast = spn_parser_parse(&ctx->parser, src);

	if (ast == NULL) {
		ctx->errtype = SPN_ERROR_SYNTAX;
	} else {
		ctx->errtype = SPN_ERROR_OK;
	}

	return ast;
}

SpnHashMap *spn_ctx_parse_expr(SpnContext *ctx, const char *src)
{
	SpnHashMap *ast = spn_parser_parse_expression(&ctx->parser, src);

	if (ast == NULL) {
		ctx->errtype = SPN_ERROR_SYNTAX;
	} else {
		ctx->errtype = SPN_ERROR_OK;
	}

	return ast;
}

SpnFunction *spn_ctx_compile_ast(SpnContext *ctx, SpnHashMap *ast, int debug)
{
	SpnFunction *fn = spn_compiler_compile(ctx->cmp, ast, debug);

	if (fn == NULL) {
		ctx->errtype = SPN_ERROR_SEMANTIC;
		return NULL;
	}

	ctx->errtype = SPN_ERROR_OK;

	/* add program to list of programs, balance reference count */
	add_to_programs(ctx, fn);
	spn_object_release(fn);

	return fn;
}


/* Virtual Machine accessors */
SpnStackFrame *spn_ctx_stacktrace(SpnContext *ctx, size_t *size)
{
	return spn_vm_stacktrace(ctx->vm, size);
}

ptrdiff_t spn_ctx_exception_addr(SpnContext *ctx)
{
	return spn_vm_exception_addr(ctx->vm);
}

void spn_ctx_addlib_cfuncs(SpnContext *ctx, const char *libname, const SpnExtFunc fns[], size_t n)
{
	spn_vm_addlib_cfuncs(ctx->vm, libname, fns, n);
}

void spn_ctx_addlib_values(SpnContext *ctx, const char *libname, const SpnExtValue vals[], size_t n)
{
	spn_vm_addlib_values(ctx->vm, libname, vals, n);
}

SpnHashMap *spn_ctx_getglobals(SpnContext *ctx)
{
	return spn_vm_getglobals(ctx->vm);
}

SpnHashMap *spn_ctx_getclasses(SpnContext *ctx)
{
	return spn_vm_getclasses(ctx->vm);
}

#if USE_DYNAMIC_LOADING
void spn_ctx_add_dynmod(SpnContext *ctx, void *handle)
{
	SpnValue val = makeweakuserinfo(handle);
	spn_array_push(ctx->dynmods, &val);
}
#endif /* USE_DYNAMIC_LOADING */
