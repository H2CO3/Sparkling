/*
 * hashmap.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 26/09/2014
 * Licensed under the 2-clause BSD License
 *
 * Hash map - arbitrary discrete map from and to SpnValues
 */

#ifndef SPN_HASHMAP_H
#define SPN_HASHMAP_H

#include <stddef.h>

#include "api.h"

typedef struct SpnHashMap SpnHashMap;

SPN_API SpnHashMap *spn_hashmap_new();
SPN_API size_t spn_hashmap_count(SpnHashMap *hm);

/* get() does not touch the ownership of *val,
 * whereas set() retains both the key and the value.
 */
SPN_API void spn_hashmap_get(SpnHashMap *hm, const SpnValue *key, SpnValue *val);
SPN_API void spn_hashmap_set(SpnHashMap *hm, const SpnValue *key, const SpnValue *val);

/* a synonym for set(hm, key, nil) */
SPN_API void spn_hashmap_delete(SpnHashMap *hm, const SpnValue *key);

/* convenience getter and setter */
SPN_API void spn_hashmap_get_strkey(SpnHashMap *hm, const char *key, SpnValue *val);
SPN_API void spn_hashmap_set_strkey(SpnHashMap *hm, const char *key, const SpnValue *val);

/* Iterator. 'cursor' must be 0 on first call. Returns new value of cursor
 * if a next key-value pair has been found, and 0 when there are no more
 * keys and values to enumerate. Sets *key and *val to next key and value,
 * respectively, except when 0 is returned in which case *key and *val are
 * not modified. Ownership of key and value is not touched.
 */
SPN_API size_t spn_hashmap_next(SpnHashMap *hm, size_t cursor, SpnValue *key, SpnValue *val);

SPN_API SpnValue spn_makehashmap(void);

#define spn_hashmapvalue(val) ((SpnHashMap *)((val)->v.o))

#endif /* SPN_HASHMAP_H */
