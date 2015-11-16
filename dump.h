/*
 * dump.h
 * Disassembly routines for the Sparkling REPL
 * Created by Árpád Goretity on 28/09/2014.
 *
 * This file is part of Sparkling.
 *
 * Sparkling is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Sparkling is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Sparkling. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SPN_DUMP_H
#define SPN_DUMP_H

#include <stddef.h>

#include "api.h"
#include "vm.h"

/* Pretty-print/disassemble bytecode in human-readable form */
SPN_API int spn_dump_assembly(spn_uword *bc, size_t len);

#endif /* SPN_DUMP_H */
