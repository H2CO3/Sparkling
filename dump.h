/*
 * dump.h
 * AST dumping and disassembly routines for the Sparkling REPL
 * Created by Árpád Goretity on 28/09/2014.
 *
 * Licensed under the 2-clause BSD License
 */

#ifndef SPN_DUMP_H
#define SPN_DUMP_H

#include <stddef.h>

#include "api.h"
#include "ast.h"
#include "vm.h"

/* Dump abstract syntax trees as S-expressions (almost) */
SPN_API void spn_dump_ast(SpnAST *ast, int indent);

/* Pretty-print/disassemble bytecode in human-readable form */
SPN_API int spn_dump_assembly(spn_uword *bc, size_t len);

#endif /* SPN_DUMP_H */
