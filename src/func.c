/*
 * func.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 05/02/2014
 * Licensed under the 2-clause BSD License
 *
 * Reference-counted function objects
 */

#include <assert.h>

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

static void free_func(void *obj)
{
	SpnFunction *func = obj;

	if (func->topprg) {
		free(func->repr.bc);
		spn_object_release(func->symtab);
	}
}

static const SpnClass spn_class_func = {
	sizeof(SpnFunction),
	equal_func,
	NULL,
	hash_func,
	free_func
};

SpnFunction *spn_func_new_script(const char *name, spn_uword *bc, SpnFunction *env)
{
	SpnFunction *func = spn_object_new(&spn_class_func);

	assert(env->topprg); /* environment must be a top-level program */
	assert(env->readsymtab); /* must've already run top-level program */

	func->native = 0;
	func->topprg = 0;
	func->nwords = 0;

	func->name = name;
	func->env = env;
	func->readsymtab = 0;
	func->symtab = env->symtab;
	func->repr.bc = bc;

	return func;
}

SpnFunction *spn_func_new_topprg(const char *name, spn_uword *bc, size_t nwords)
{
	SpnFunction *func = spn_object_new(&spn_class_func);

	func->native = 0;
	func->topprg = 1;
	func->nwords = nwords;

	func->name = name;
	func->env = func; /* top program's environment is itself */
	func->readsymtab = 0;
	func->symtab = spn_array_new();
	func->repr.bc = bc;

	return func;
}

SpnFunction *spn_func_new_native(const char *name, int (*fn)(SpnValue *, int, SpnValue *, void *))
{
	SpnFunction *func = spn_object_new(&spn_class_func);

	func->native = 1;
	func->topprg = 0;
	func->nwords = 0;

	func->name = name;
	func->env = NULL;
	func->readsymtab = 0;
	func->symtab = NULL;
	func->repr.fn = fn;

	return func;
}

/* convenience value constructors */

SpnValue spn_makescriptfunc(const char *name, spn_uword *bc, SpnFunction *env)
{
	SpnFunction *func = spn_func_new_script(name, bc, env);
	SpnValue ret;
	ret.type = SPN_TYPE_FUNC;
	ret.v.o = func;
	return ret;
}

SpnValue spn_maketopprgfunc(const char *name, spn_uword *bc, size_t nwords)
{
	SpnFunction *func = spn_func_new_topprg(name, bc, nwords);
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

