/*
 * compiler.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * AST -> bytecode compiler
 */

#ifndef SPN_COMPILER_H
#define SPN_COMPILER_H

#include <stddef.h>

#include "spn.h"
#include "ast.h"
#include "vm.h"

/* a compiler object takes an AST and outputs bytecode */
typedef struct SpnCompiler SpnCompiler;

SPN_API SpnCompiler	*spn_compiler_new();
SPN_API void		 spn_compiler_free(SpnCompiler *cmp);

/* returns a pointer to bytecode that can be passed to spn_vm_exec()
 * or it can be written to a file. If `sz' is not a NULL pointer, it is
 * set to the length of the bytecode (measured in sizeof(spn_uword) units).
 * the returned pointer must be `free()`ed by the caller.
 * returns NULL on error.
 */
SPN_API spn_uword	*spn_compiler_compile(SpnCompiler *cmp, SpnAST *ast, size_t *sz);

/* obtain the last error message */
SPN_API	const char	*spn_compiler_errmsg(SpnCompiler *cmp);

#endif /* SPN_COMPILER_H */

