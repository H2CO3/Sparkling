/*
 * object.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 03/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Object API: reference counted objects
 */

#include <stdlib.h>

#include "spn.h"

const char *spn_object_type(const void *o) /* for typeof() */
{
	const SpnObject *obj = o;
	return obj->isa->name;
}

int spn_object_equal(const void *lhs, const void *rhs)
{
	const SpnObject *lo = lhs, *ro = rhs;

	if (lo->isa != ro->isa) {
		return 0;
	}

	if (lo->isa->equal != NULL) {
		return lo->isa->equal(lo, ro);
	} else {
		return lo == ro;
	}
}

int spn_object_cmp(const void *lhs, const void *rhs)
{
	const SpnObject *lo = lhs, *ro = rhs;

	assert(lo->isa == ro->isa);
	assert(lo->isa->compare != NULL);

	return lo->isa->compare(lo, ro);
}

void *spn_object_new(const SpnClass *isa)
{
	SpnObject *obj = malloc(isa->instsz);
	if (obj == NULL) {
		abort();
	}

	obj->isa = isa;
	obj->refcnt = 1;

	return obj;
}

void spn_object_retain(void *o)
{
	SpnObject *obj = o;
	obj->refcnt++;
}

void spn_object_release(void *o)
{
	SpnObject *obj = o;

	/* it is the destructor's responsibility to `free()` the instance */
	if (--obj->refcnt == 0) {
		obj->isa->destructor(obj);
	}
}

