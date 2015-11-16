/*
 * func.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 05/02/2014
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
 * Reference-counted function objects
 */

#ifndef SPN_FUNC_H
#define SPN_FUNC_H

#include "api.h"
#include "array.h"
#include "hashmap.h"

typedef struct SpnFunction {
	SpnObject base;
	const char *name;        /* name of the function                */
	int native;              /* boolean flag, is native?            */
	int topprg;              /* is top-level program?               */
	int is_closure;          /* is closure?                         */
	size_t nwords;           /* only if top-level                   */
	struct SpnFunction *env; /* program in which function resides   */
	int readsymtab;          /* top-level only: parsed symtab yet?  */
	SpnArray *symtab;        /* symtab to look for local symbols in */
	SpnArray *upvalues;      /* upvalues if function is a closure   */
	union {
		spn_uword *bc;
		int (*fn)(SpnValue *, int, SpnValue *, void *);
	} repr;                  /* representation                      */
	SpnHashMap *debug_info;  /* optional debug info if top-level    */
} SpnFunction;

/* 'name' is always a weak pointer, regardless of whether
 * the function is a top-level program, a normal Sparkling
 * function or a native extension function.
 *
 * if 'topprg' is true, then env == the function itself:
 * the environment (in which to look for the local symbol
 * table) of the top-level program is itself, naturally.
 *
 * The 'env' pointer is always weak too (it does not own
 * the environment function object).
 *
 * In contrast, 'symtab' is strong if self is a top-level
 * program, and weak otherwise. In either case, it points
 * to the symbol table of the containing program.
 *
 * 'readsymtab' (applicable to top-level programs only)
 * indicates whether the program has already been run
 * at least once, and thus whether its local symbol table
 * has already been parsed and stored in the SpnFunction.
 *
 * 'upvalues' is always a strong pointer, since it's tied
 * to a specific instance of a closure (i. e. it belongs
 * to the closure only and nothing else). It is freed if
 * the closure object is deallocated.
 *
 * 'repr.bc' is a strong pointer if the function object
 * designates a top-level program; otherwise (when the
 * function object represents a free script function or
 * a closure) it is a weak pointer.
 */

SPN_API SpnFunction *spn_func_new_script(const char *name, spn_uword *bc, SpnFunction *env);

/* transfers ownership of both bytecode and debug information */
SPN_API SpnFunction *spn_func_new_topprg(const char *name, spn_uword *bc, size_t nwords, SpnHashMap *debug);

SPN_API SpnFunction *spn_func_new_native(const char *name, int (*fn)(SpnValue *, int, SpnValue *, void *));
SPN_API SpnFunction *spn_func_new_closure(SpnFunction *prototype);

/* convenience value constructors and an accessor */
SPN_API SpnValue spn_makescriptfunc(const char *name, spn_uword *bc, SpnFunction *env);

 /* this one transfers ownerships too */
SPN_API SpnValue spn_maketopprgfunc(const char *name, spn_uword *bc, size_t nwords, SpnHashMap *debug);
SPN_API SpnValue spn_makenativefunc(const char *name, int (*fn)(SpnValue *, int, SpnValue *, void *));
SPN_API SpnValue spn_makeclosure(SpnFunction *prototype);

#define spn_funcvalue(val) ((SpnFunction *)((val)->v.o))

#endif /* SPN_FUNC_H */
