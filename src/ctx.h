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
#include "vm.h"


enum spn_error_type {
	SPN_ERROR_OK,		/* success, no error		*/
	SPN_ERROR_SYNTAX,	/* syntax (parser) error	*/
	SPN_ERROR_SEMANTIC,	/* semantic (compiler) error	*/
	SPN_ERROR_RUNTIME,	/* runtime error		*/
	SPN_ERROR_GENERIC	/* some other kind of error	*/
};

struct spn_bc_list {
	spn_uword *bc;
	size_t len;
	struct spn_bc_list *next;
};

typedef struct SpnContext SpnContext;

SPN_API SpnContext *spn_ctx_new();
SPN_API void spn_ctx_free(SpnContext *ctx);

SPN_API enum spn_error_type spn_ctx_geterrtype(SpnContext *ctx);
SPN_API const char *spn_ctx_geterrmsg(SpnContext *ctx);
SPN_API const struct spn_bc_list *spn_ctx_getbclist(SpnContext *ctx);
SPN_API void *spn_ctx_getuserinfo(SpnContext *ctx);
SPN_API void spn_ctx_setuserinfo(SpnContext *ctx, void *info);

/* these return non-owning pointers that should *not* be freed --
 * they will be deallocated automatically when you free the context.
 */
SPN_API spn_uword	 *spn_ctx_loadstring(SpnContext *ctx, const char *str);
SPN_API spn_uword	 *spn_ctx_loadsrcfile(SpnContext *ctx, const char *fname);
SPN_API spn_uword	 *spn_ctx_loadobjfile(SpnContext *ctx, const char *fname);

SPN_API int		  spn_ctx_execstring(SpnContext *ctx, const char *str, SpnValue *ret);
SPN_API int		  spn_ctx_execsrcfile(SpnContext *ctx, const char *fname, SpnValue *ret);
SPN_API int		  spn_ctx_execobjfile(SpnContext *ctx, const char *fname, SpnValue *ret);
SPN_API int		  spn_ctx_execbytecode(SpnContext *ctx, spn_uword *bc, SpnValue *ret);

/* direct access to the virtual machine */
SPN_API int		  spn_ctx_callfunc(SpnContext *ctx, SpnValue *func, SpnValue *ret, int argc, SpnValue argv[]);
SPN_API void		  spn_ctx_runtime_error(SpnContext *ctx, const char *fmt, const void *args[]);
SPN_API const char	**spn_ctx_stacktrace(SpnContext *ctx, size_t *size);
SPN_API void		  spn_ctx_clean(SpnContext *ctx);

SPN_API void		  spn_ctx_addlib_cfuncs(SpnContext *ctx, const char *libname, const SpnExtFunc fns[], size_t n);
SPN_API void		  spn_ctx_addlib_values(SpnContext *ctx, const char *libname, SpnExtValue vals[], size_t n);
SPN_API SpnArray	 *spn_ctx_getglobals(SpnContext *ctx);

#endif /* SPN_CTX_H */

