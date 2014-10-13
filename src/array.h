/*
 * array.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Arrays: indexed value containers
 */

#ifndef SPN_ARRAY_H
#define SPN_ARRAY_H

#include <stddef.h>

#include "api.h"


typedef struct SpnArray SpnArray;


SPN_API SpnArray *spn_array_new(void);
SPN_API size_t    spn_array_count(SpnArray *arr);

/* getter and setter.
 * the getter doesn't alter the ownership of the value, whereas the
 * setter retains it.
 * 'index' must not exceed the bounds of the array:
 * 0 <= index < spn_array_count(array)
 */
SPN_API void spn_array_get(SpnArray *arr, size_t index, SpnValue *val);
SPN_API void spn_array_set(SpnArray *arr, size_t index, const SpnValue *val);

SPN_API void spn_array_insert(SpnArray *arr, size_t index, const SpnValue *val);
SPN_API void spn_array_remove(SpnArray *arr, size_t index);

/* inserts the elements of 'other' at index 'index' */
SPN_API void spn_array_inject(SpnArray *arr, size_t index, SpnArray *other);

/* inserts an element at the end */
SPN_API void spn_array_push(SpnArray *arr, const SpnValue *val);

/* removes an element from the end */
SPN_API void spn_array_pop(SpnArray *arr);

/* expand or shrink the array
 * inserts nils to/removes elements from the end
 */
SPN_API void spn_array_setsize(SpnArray *arr, size_t newsize);

/* convenience value constructor and accessor */
SPN_API SpnValue spn_makearray(void);

#define spn_arrayvalue(val) ((SpnArray *)((val)->v.o))

#endif /* SPN_ARRAY_H */
