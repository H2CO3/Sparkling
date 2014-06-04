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
		/* a closure cannot possibly equal a non-closure */
		if (lhs->is_closure != rhs->is_closure) {
			return 0;
		}

		/* Both functions are closures.
		 * Two closures are equal if and only if they
		 * are the same object, since the notion of
		 * equality based on their behavior depends
		 * on their environment and upvalues.
		 */
		if (lhs->is_closure) {
			return lhs == rhs;
		}

		/* Neither of the functions is a closure.
		 * Two non-closure Sparkling function objects
		 * are equal if and only if they point to the
		 * same function in the bytecode.
		 */
		return lhs->repr.bc == rhs->repr.bc;
	}
}

static unsigned long hash_func(void *obj)
{
	SpnFunction *func = obj;

	if (func->native) {
		return (unsigned long)(func->repr.fn);
	} else {
		if (func->is_closure) {
			return (unsigned long)(func);
		} else {
			return (unsigned long)(func->repr.bc);
		}
	}
}

static void free_func(void *obj)
{
	SpnFunction *func = obj;

	/* if the function represents a top-level program,
	 * the free the array containing the local symbol table
	 */
	if (func->topprg) {
		free(func->repr.bc);
		spn_object_release(func->symtab);
	}

	/* if the function is a closure,
	 * then free the array of upvalues
	 */
	if (func->is_closure) {
		assert(func->upvalues);
		spn_object_release(func->upvalues);
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
	func->is_closure = 0;
	func->nwords = 0;		/* unused */

	func->name = name;
	func->env = env;
	func->readsymtab = 0;		/* unused */
	func->symtab = env->symtab;	/* weak pointer */
	func->upvalues = NULL;		/* unused */
	func->repr.bc = bc;		/* weak pointer */

	return func;
}

SpnFunction *spn_func_new_topprg(const char *name, spn_uword *bc, size_t nwords)
{
	SpnFunction *func = spn_object_new(&spn_class_func);

	func->native = 0;
	func->topprg = 1;
	func->is_closure = 0;
	func->nwords = nwords;

	func->name = name;
	func->env = func; /* top program's environment is itself */
	func->readsymtab = 0;
	func->symtab = spn_array_new();
	func->upvalues = NULL; /* unused */
	func->repr.bc = bc; /* strong pointer */

	return func;
}

SpnFunction *spn_func_new_native(const char *name, int (*fn)(SpnValue *, int, SpnValue *, void *))
{
	SpnFunction *func = spn_object_new(&spn_class_func);

	func->native = 1;
	func->topprg = 0;
	func->is_closure = 0;
	func->nwords = 0;	/* unused */

	func->name = name;
	func->env = NULL;	/* unused */
	func->readsymtab = 0;	/* unused */
	func->symtab = NULL;	/* unused */
	func->upvalues = NULL;	/* unused */
	func->repr.fn = fn;

	return func;
}

SPN_API SpnFunction *spn_func_new_closure(SpnFunction *prototype)
{
	SpnFunction *func = spn_object_new(&spn_class_func);

	/* only a Sparkling function can be used as the prototype
	 * of a closure, native ones can't. A top-level program
	 * cannot be used to form a closure either.
	 */
	assert(prototype->native == 0);
	assert(prototype->topprg == 0);
	assert(prototype->is_closure == 0);

	func->native = 0;
	func->topprg = 0;
	func->is_closure = 1;
	func->nwords = 0;			/* unused */

	/* func->symtab is always a weak pointer for closures,
	 * because the prototype is never a top-level program,
	 * and only function objects that represent a top-level
	 * program have a strong symbol table pointer.
	 */

	func->name = prototype->name;		/* weak pointer */
	func->env = prototype->env; 		/* weak pointer */
	func->readsymtab = 0;			/* unused */
	func->symtab = prototype->symtab;	/* weak pointer */
	func->upvalues = spn_array_new();
	func->repr = prototype->repr;

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

SpnValue spn_makeclosure(SpnFunction *prototype)
{
	SpnFunction *func = spn_func_new_closure(prototype);
	SpnValue ret;
	ret.type = SPN_TYPE_FUNC;
	ret.v.o = func;
	return ret;
}
