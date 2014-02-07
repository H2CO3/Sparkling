/*
 * func.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 05/02/2014
 * Licensed under the 2-clause BSD License
 *
 * Reference-counted function objects
 */

#include "func.h"


static int equal_func(void *lp, void *rp)
{
	SpnFunction *lhs = lp, *rhs = rp;

	if (lhs->native != rhs->native) {
		return 0;
	}

	if (lhs->native) {
		return lhs->repr.fn == rhs->repr.fn;
	} else {
		return lhs->repr.bc == rhs->repr.bc;
	}
}

static unsigned long hash_func(void *obj)
{
	SpnFunction *func = obj;

	if (func->native) {
		return (unsigned long)(func->repr.fn);
	} else {
		return (unsigned long)(func->repr.bc);
	}
}

static const SpnClass spn_class_func = {
	sizeof(SpnFunction),
	equal_func,
	NULL,
	hash_func,
	NULL /* free_func */
};

SpnFunction *spn_func_new_script(const char *name, spn_uword *bc, spn_uword *env)
{
	SpnFunction *func = spn_object_new(&spn_class_func);
	func->native = 0;
	func->name = name;
	func->env = env;
	func->repr.bc = bc;
	return func;
}

SpnFunction *spn_func_new_native(const char *name, int (*fn)(SpnValue *, int, SpnValue *, void *))
{
	SpnFunction *func = spn_object_new(&spn_class_func);
	func->native = 1;
	func->name = name;
	func->env = NULL;
	func->repr.fn = fn;
	return func;
}

/* convenience value constructors */

SpnValue spn_makescriptfunc(const char *name, spn_uword *bc, spn_uword *env)
{
	SpnFunction *func = spn_func_new_script(name, bc, env);
	SpnValue ret;
	ret.type = SPN_TYPE_FUNC;
	ret.v.o = func;
	return ret;
}

SpnValue spn_makenativefunc(const char *name, int (*fn)(SpnValue *, int, SpnValue *, void *))
{
	SpnFunction *func = spn_func_new_native(name, fn);
	SpnValue ret;
	ret.type = SPN_TYPE_FUNC;
	ret.v.o = func;
	return ret;
}

