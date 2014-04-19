/*
 * func.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 05/02/2014
 * Licensed under the 2-clause BSD License
 *
 * Reference-counted function objects
 */

#ifndef SPN_FUNC_H
#define SPN_FUNC_H

#include "api.h"
#include "array.h"

typedef struct SpnFunction {
	SpnObject base;
	const char *name;	 /* name of the function (NULL: lambda)	*/
	int native;		 /* boolean flag, is native?		*/
	int topprg;		 /* is top-level program?		*/
	size_t nwords;		 /* only if top-level			*/
	struct SpnFunction *env; /* program in which function resides	*/
	int readsymtab;		 /* top-level only: parsed symtab yet?	*/
	SpnArray *symtab;	 /* symtab to look for local symbols in	*/
	union {
		spn_uword *bc;
		int (*fn)(SpnValue *, int, SpnValue *, void *);
	} repr;			 /* representation			*/
} SpnFunction;

/* if topprog is true, then env == the function itself:
 * the environment (in which to look for the local symbol
 * table) of the top-level program is itself, naturally.
 *
 * The `env' pointer is always weak (i. e. it does not own
 * the environment function object).
 * In contrast, `symtab' is strong if self is a top-level
 * program, and weak otherwise. In either case, it points
 * to the symbol table of the containing program.
 *
 * `readsymtab' (applicable to top-level programs only)
 * indicates whether the program has already been run
 * at least once, and thus whether its local symbol table
 * has already been parsed and stored in the SpnFunction.
 */

SPN_API SpnFunction *spn_func_new_script(const char *name, spn_uword *bc, SpnFunction *env);
SPN_API SpnFunction *spn_func_new_topprg(const char *name, spn_uword *bc, size_t nwords); /* transfers ownership */
SPN_API SpnFunction *spn_func_new_native(const char *name, int (*fn)(SpnValue *, int, SpnValue *, void *));

/* convenience value constructors and an accessor */
SPN_API SpnValue spn_makescriptfunc(const char *name, spn_uword *bc, SpnFunction *env);
SPN_API SpnValue spn_maketopprgfunc(const char *name, spn_uword *bc, size_t nwords); /* transfers ownership */
SPN_API SpnValue spn_makenativefunc(const char *name, int (*fn)(SpnValue *, int, SpnValue *, void *));

#define spn_funcvalue(val) ((SpnFunction *)((val)->v.o))

#endif /* SPN_FUNC_H */

