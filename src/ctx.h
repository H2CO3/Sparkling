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

#include "spn.h"
#include "rtlb.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"


struct spn_bc_list {
	spn_uword *bc;
	size_t len;
	struct spn_bc_list *next;
};

typedef struct SpnContext {
	SpnParser *p;
	SpnCompiler *cmp;
	SpnVMachine *vm;
	struct spn_bc_list *bclist; /* holds all bytecodes ever compiled */
	const char *errmsg; /* most recent error message */
	void *info; /* user data initialized to NULL, use freely */
} SpnContext;

SPN_API SpnContext	*spn_ctx_new();
SPN_API void		 spn_ctx_free(SpnContext *ctx);

/* these return non-owning pointers that should *not* be freed --
 * they will be deallocated automatically when you free the context.
 */
SPN_API spn_uword	*spn_ctx_loadstring(SpnContext *ctx, const char *str);
SPN_API spn_uword	*spn_ctx_loadsrcfile(SpnContext *ctx, const char *fname);
SPN_API spn_uword	*spn_ctx_loadobjfile(SpnContext *ctx, const char *fname);

SPN_API int		 spn_ctx_execstring(SpnContext *ctx, const char *str, SpnValue *ret);
SPN_API int		 spn_ctx_execsrcfile(SpnContext *ctx, const char *fname, SpnValue *ret);
SPN_API int		 spn_ctx_execobjfile(SpnContext *ctx, const char *fname, SpnValue *ret);
SPN_API int		 spn_ctx_execbytecode(SpnContext *ctx, spn_uword *bc, SpnValue *ret);

#endif /* SPN_CTX_H */

