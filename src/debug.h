/*
 * debug.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 15/03/2015
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
 *
 * Emitting debugging information
 */

#ifndef SPN_DEBUG_H
#define SPN_DEBUG_H

#include "api.h"
#include "hashmap.h"
#include "lex.h"
#include "vm.h"


/* Creates a new debug infor object (a hashmap with some
 * special structure). To be used in the compiler.
 */
SPN_API SpnHashMap *spn_dbg_new(void);

/* adds the appropriate line and column number information
 * to 'debug_info'. For use in the compiler; most probably
 * you won't need to call this yourself.
 */
SPN_API void spn_dbg_emit_source_location(
	SpnHashMap *debug_info, /* the debug info object                   */
	size_t begin,           /* bytecode start, inclusive (begin <= IP) */
	size_t end,             /* bytecode end, exclusive (IP < end)      */
	SpnHashMap *ast,        /* AST node to get info from               */
	int regno               /* register number of expression result    */
);

/* use these liberally. getter returns "???" if no filename found.
 * if there's no 'debug_info', you may safely pass NULL.
 */
SPN_API void spn_dbg_set_filename(SpnHashMap *debug_info, const char *fname);
SPN_API const char *spn_dbg_get_filename(SpnHashMap *debug_info);

/* returns (0, 0) if source location is not available */
SPN_API SpnSourceLocation spn_dbg_get_frame_source_location(SpnStackFrame frame);

/* a bit lower-level function, for obtaining the source
 * location for arbitrary bytecode addresses
 */
SPN_API SpnSourceLocation spn_dbg_get_raw_source_location(
	SpnHashMap *debug_info,
	ptrdiff_t address
);

#endif /* SPN_DEBUG_H */
