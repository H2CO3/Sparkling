/*
 * array.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Arrays: fast associative array implementation
 */

#ifndef SPN_ARRAY_H
#define SPN_ARRAY_H

#include <stddef.h>

#include "api.h"


typedef struct SpnArray SpnArray;
typedef struct SpnIterator SpnIterator;


SPN_API SpnArray	*spn_array_new(void);
SPN_API size_t		 spn_array_count(SpnArray *arr);

/* getter and setter.
 * the getter doesn't alter the ownership of either the key or the value.
 * values obtained via spn_array_get() may be retained and released.
 * the setter retains both the key and the value.
 */
SPN_API void		 spn_array_get(SpnArray *arr, const SpnValue *key, SpnValue *val);
SPN_API void		 spn_array_set(SpnArray *arr, const SpnValue *key, const SpnValue *val);

/* this is just a convenience wrapper around `spn_array_set()':
 * it sets the value corresponding to the key to `nil`.
 */
SPN_API void		 spn_array_remove(SpnArray *arr, const SpnValue *key);

/* convenience getters and setters. `set_strkey()' always copies the string. */
SPN_API void		 spn_array_get_intkey(SpnArray *arr, long idx, SpnValue *val);
SPN_API void		 spn_array_get_strkey(SpnArray *arr, const char *str, SpnValue *val);

SPN_API void		 spn_array_set_intkey(SpnArray *arr, long idx, const SpnValue *val);
SPN_API void		 spn_array_set_strkey(SpnArray *arr, const char *str, const SpnValue *val);

/* iterators. spn_iter_next() returns the index of the next key-value pair in
 * the array. when finished (i. e. no more key-value pairs), it returns the
 * count of the array. Sets `key` and `val` appropriately.
 *
 * Since modifying an array (i. e. inserting, changing or deleting one of its
 * elements) may result in internal pointers being invalidated, it is illegal
 * to change an array while it's being enumerated using an iterator.
 */

SPN_API SpnIterator	*spn_iter_new(SpnArray *arr);
SPN_API size_t		 spn_iter_next(SpnIterator *it, SpnValue *key, SpnValue *val);
SPN_API SpnArray	*spn_iter_getarray(SpnIterator *it);
SPN_API void		 spn_iter_free(SpnIterator *it);

/* convenience value constructor and accessor */
SPN_API SpnValue	 spn_makearray(void);

#define spn_arrayvalue(val) ((SpnArray *)((val)->v.o))

#endif /* SPN_ARRAY_H */
