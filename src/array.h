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


/*
 * Values obtained via spn_array_get() should never be modified, only
 * retained and released (if they contain an object).
 */
SPN_API SpnArray	*spn_array_new();
SPN_API size_t		 spn_array_count(SpnArray *arr);

/* getter and setter. both the key and the value are retained. */
SPN_API SpnValue	*spn_array_get(SpnArray *arr, const SpnValue *key);
SPN_API void		 spn_array_set(SpnArray *arr, SpnValue *key, SpnValue *val);

/* this is just a convenience wrapper around `spn_array_set()':
 * it sets the value corresponding to the key to `nil`.
 */
SPN_API void		 spn_array_remove(SpnArray *arr, SpnValue *key);

/* iterators. spn_iter_next() returns the index of the next key-value pair in
 * the array. when finished (i. e. no more key-value pairs), it returns the
 * count of the array. Sets `key` and `val` appropriately.
 */
SPN_API SpnIterator	*spn_iter_new(SpnArray *arr);
SPN_API size_t		 spn_iter_next(SpnIterator *it, SpnValue *key, SpnValue *val);
SPN_API SpnArray	*spn_iter_getarray(SpnIterator *it);
SPN_API void		 spn_iter_free(SpnIterator *it);

#endif /* SPN_ARRAY_H */

