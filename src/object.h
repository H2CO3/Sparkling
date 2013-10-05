/*
 * object.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 03/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Object API: reference counted objects
 */

#ifndef SPN_OBJECT_H
#define SPN_OBJECT_H

#include <stddef.h>
#include <assert.h>

#ifdef __cplusplus
#define SPN_API extern "C"
#else	/* __cplusplus */
#define SPN_API
#endif	/* __cplusplus */


typedef struct SpnClass {
	const char *name;				/* set once, then read-only		*/
	size_t instsz;					/* sizeof(the_struct)			*/
	int (*equal)(const void *, const void *);	/* non-zero: equal, zero: different	*/
	int (*compare)(const void *, const void *);	/* -1, +1, 0: lhs is <, >, == to rhs	*/
	unsigned long (*hashfn)(void *);		/* cache the hash if immutable!		*/
	void (*destructor)(void *);			/* should call free() on its argument	*/
} SpnClass;

typedef struct SpnObject {
	const SpnClass *isa;
	unsigned refcnt;
} SpnObject;

/* allocates a partially uninitialized (only the `isa` and `refcount` members
 * are set up) object of clas `isa`. The returned instance should go through
 * a dedicated constructor (see e. g. spn_string_new()).
 */
SPN_API void *spn_object_new(const SpnClass *isa);

/* returns o->isa->name */
SPN_API const char *spn_object_type(const void *o); /* for typeof() */

/* tests objects for equality. two objects are considered equal if they are
 * of the same class, and either their pointers compare equal or they have
 * a non-NULL `compare` member function which returns nonzero.
 */
SPN_API int spn_object_equal(const void *lhs, const void *rhs);

/* ordered comparison of objects. follows the common C idiom:
 * returns -1 if lhs < rhs, 0 if lhs == rhs, 1 if lhs > rhs
 */
SPN_API int spn_object_cmp(const void *lhs, const void *rhs);

/* these reference counting functions are called quite often.
 * for the sake of speed, they should probably be inlined. C89 doesn't have
 * `inline`, though, so we rely on the linker being able to inline them.
 */

/* increments the reference count of an object */
SPN_API void spn_object_retain(void *o);

/* decrements the reference count of an object.
 * calls the destructor and frees the instance if its reference count
 * drops to 0.
 */
SPN_API void spn_object_release(void *o);

#endif /* SPN_OBJECT_H */

