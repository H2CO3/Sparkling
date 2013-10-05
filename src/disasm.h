/*
 * disasm.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 11/09/2013
 * Licensed under the 2-clause BSD License
 *
 * Disassembler for Sparkling bytecode files
 */

#ifndef SPN_DISASM_H
#define SPN_DISASM_H

#include "spn.h"
#include "vm.h"

/* prints disassembly of file to standard output stream */
SPN_API void spn_disasm(spn_uword *bc, size_t len);

#endif /* SPN_DISASM_H */

