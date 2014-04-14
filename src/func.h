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
	const char *name;
	int native;	/* boolean flag, is native?	*/
	int topprg;	/* is top-level program?	*/
	size_t nwords;	/* only if top-level		*/
	spn_uword *env;
	SpnArray *symtab;
	union {
		spn_uword *bc;
		int (*fn)(SpnValue *, int, SpnValue *, void *);
	} repr;
} SpnFunction;

SPN_API SpnFunction *spn_func_new_script(const char *name, spn_uword *bc, spn_uword *env);
SPN_API SpnFunction *spn_func_new_topprg(const char *name, spn_uword *bc, size_t nwords); /* transfers ownership */
SPN_API SpnFunction *spn_func_new_native(const char *name, int (*fn)(SpnValue *, int, SpnValue *, void *));

/* convenience value constructors and an accessor */
SPN_API SpnValue spn_makescriptfunc(const char *name, spn_uword *bc, spn_uword *env);
SPN_API SpnValue spn_maketopprgfunc(const char *name, spn_uword *bc, size_t nwords); /* transfers ownership */
SPN_API SpnValue spn_makenativefunc(const char *name, int (*fn)(SpnValue *, int, SpnValue *, void *));

#define spn_funcvalue(val) ((SpnFunction *)((val)->v.o))

#endif /* SPN_FUNC_H */
