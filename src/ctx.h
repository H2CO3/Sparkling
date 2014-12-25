/*
 * ctx.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 12/10/2013
 * Licensed under the 2-clause BSD License
 *
 * A convenience context API
 */

#ifndef SPN_CTX_H
#define SPN_CTX_H

#include "api.h"
#include "rtlb.h"
#include "parser.h"
#include "compiler.h"
#include "hashmap.h"
#include "vm.h"


enum spn_error_type {
	SPN_ERROR_OK,       /* success, no error         */
	SPN_ERROR_SYNTAX,   /* syntax (parser) error     */
	SPN_ERROR_SEMANTIC, /* semantic (compiler) error */
	SPN_ERROR_RUNTIME,  /* runtime error             */
	SPN_ERROR_GENERIC   /* some other kind of error  */
};

typedef struct SpnContext {
	SpnParser parser;
	SpnCompiler *cmp;
	SpnVMachine *vm;
	SpnArray *programs; /* holds all programs ever compiled */

	enum spn_error_type errtype; /* type of the last error */
	const char *errmsg; /* last error message */

	void *info; /* context info initialized to NULL, use freely */
} SpnContext;

SPN_API void spn_ctx_init(SpnContext *ctx);
SPN_API void spn_ctx_free(SpnContext *ctx);

SPN_API enum spn_error_type spn_ctx_geterrtype(SpnContext *ctx);
SPN_API const char *spn_ctx_geterrmsg(SpnContext *ctx);
SPN_API SpnSourceLocation spn_ctx_geterrloc(SpnContext *ctx);

SPN_API SpnArray *spn_ctx_getprograms(SpnContext *ctx); /* read-only array! */
SPN_API void *spn_ctx_getuserinfo(SpnContext *ctx);
SPN_API void spn_ctx_setuserinfo(SpnContext *ctx, void *info);

/* the returned function is owned by the context, you _must not_ release it.
 * It will be deallocated automatically when you free the context.
 * These functions return NULL on error.
 */
SPN_API SpnFunction *spn_ctx_loadstring(SpnContext *ctx, const char *str);
SPN_API SpnFunction *spn_ctx_loadsrcfile(SpnContext *ctx, const char *fname);
SPN_API SpnFunction *spn_ctx_loadobjfile(SpnContext *ctx, const char *fname);
SPN_API SpnFunction *spn_ctx_loadobjdata(SpnContext *ctx, const void *objdata, size_t objsize);

/* these functions call the program with no arguments.
 * If you wish to pass arguments to the program, use the load_* APIs
 * and call spn_ctx_callfunc() on the returned function value object.
 */
SPN_API int spn_ctx_execstring(SpnContext *ctx, const char *str, SpnValue *ret);
SPN_API int spn_ctx_execsrcfile(SpnContext *ctx, const char *fname, SpnValue *ret);
SPN_API int spn_ctx_execobjfile(SpnContext *ctx, const char *fname, SpnValue *ret);
SPN_API int spn_ctx_execobjdata(SpnContext *ctx, const void *objdata, size_t objsize, SpnValue *ret);

/* direct access to the virtual machine */
SPN_API int spn_ctx_callfunc(SpnContext *ctx, SpnFunction *func, SpnValue *ret, int argc, SpnValue argv[]);
SPN_API void spn_ctx_runtime_error(SpnContext *ctx, const char *fmt, const void *args[]);
SPN_API const char **spn_ctx_stacktrace(SpnContext *ctx, size_t *size);

/* compile and evaluate expressions */
SPN_API SpnFunction *spn_ctx_compile_expr(SpnContext *ctx, const char *expr);

/* The following functions give users access to the parser
 * and the compiler separately.
 * They all return NULL upon encountering an error.
 *
 * Parser functions return an _owning_ (strong) pointer to the root
 * of the resulting AST!
 * 'spn_ctx_parse()' tries to parse a top-level program (statements);
 * 'spn_ctx_parse_expr()' tries to parse an expression.
 *
 * 'spn_ctx_compile_ast()' attempts to compile an AST into bytecode.
 * The AST must be valid (i. e. it must come directly from the parser
 * or it should be validated somehow); if it is invalid (inconsistent
 * with the expectations of the compiler), the behavior is undefined!
 * 'spn_ctx_compile_ast()' returns a _non-owning_ (weak) pointer to
 * the generated function! (The function is owned by the context.)
 */
SPN_API SpnHashMap *spn_ctx_parse(SpnContext *ctx, const char *src);
SPN_API SpnHashMap *spn_ctx_parse_expr(SpnContext *ctx, const char *src);
SPN_API SpnFunction *spn_ctx_compile_ast(SpnContext *ctx, SpnHashMap *ast);

/* accessors for library functions, other globals and class descriptors */
SPN_API void        spn_ctx_addlib_cfuncs(SpnContext *ctx, const char *libname, const SpnExtFunc  fns[],  size_t n);
SPN_API void        spn_ctx_addlib_values(SpnContext *ctx, const char *libname, const SpnExtValue vals[], size_t n);
SPN_API SpnHashMap *spn_ctx_getglobals(SpnContext *ctx);
SPN_API SpnHashMap *spn_ctx_getclasses(SpnContext *ctx);

#endif /* SPN_CTX_H */
