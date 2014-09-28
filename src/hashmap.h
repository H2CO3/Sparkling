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

SPN_API void spn_hashmap_get(SpnHashMap *hm, const SpnValue *key, SpnValue *val);
SPN_API void spn_hashmap_set(SpnHashMap *hm, const SpnValue *key, const SpnValue *val);
SPN_API void spn_hashmap_delete(SpnHashMap *hm, const SpnValue *key);

SPN_API size_t spn_hashmap_next(SpnHashMap *hm, size_t cursor, SpnValue *key, SpnValue *val);

#endif /* SPN_HASHMAP_H */
