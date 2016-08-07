/*
 * hashmap.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 26/09/2014
 * Licensed under the 2-clause BSD License
 *
 * Hash map - arbitrary discrete map from and to SpnValues
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "hashmap.h"
#include "str.h"
#include "misc.h"


typedef struct Bucket {
	SpnValue key;
	SpnValue value;
} Bucket;

static int bucket_is_used(const Bucket *bucket)
{
	/* nil key with non-nil val (or vice versa) is not allowed */
	assert(isnil(&bucket->key) == isnil(&bucket->value));
	return notnil(&bucket->key);
}

static int bucket_is_empty(const Bucket *bucket)
{
	assert(isnil(&bucket->key) == isnil(&bucket->value));
	return isnil(&bucket->key);
}

/* retains key and value */
static void insert_into_bucket(
	Bucket *bucket,
	const SpnValue *key,
	const SpnValue *value,
	int should_retain
)
{
	assert(bucket_is_empty(bucket));
	assert(notnil(key));
	assert(notnil(value));

	if (should_retain) {
		spn_value_retain(key);
		spn_value_retain(value);
	}

	bucket->key = *key;
	bucket->value = *value;
}

static void erase_bucket(Bucket *bucket)
{
	assert(bucket_is_used(bucket));

	/* relinquish ownership of key and value (RAII/DIRR),
	 * then clean them by setting them to nil.
	 */
	spn_value_release(&bucket->key);
	spn_value_release(&bucket->value);

	bucket->key = spn_nilval;
	bucket->value = spn_nilval;
}

struct SpnHashMap {
	SpnObject  base;
	Bucket    *buckets;         /* actual array for key-value pairs           */
	size_t     allocsize;       /* number of buckets                          */
	size_t     count;           /* number of key-value pairs                  */
	size_t     max_hash_offset; /* global upper bound on number of collisions */
};

static void free_hashmap(void *obj);
static void free_bucket_array(Bucket *buckets, size_t n);
static void expand_and_rehash(SpnHashMap *hm);

static const SpnClass spn_class_hashmap = {
	sizeof(SpnHashMap),
	SPN_CLASS_UID_HASHMAP,
	"hashmap",
	NULL,
	NULL,
	NULL,
	NULL,
	free_hashmap
};


SpnHashMap *spn_hashmap_new()
{
	SpnHashMap *hm = spn_object_new(&spn_class_hashmap);

	hm->buckets = NULL;
	hm->allocsize = 0;
	hm->count = 0;
	hm->max_hash_offset = 0;

	return hm;
}

static void free_hashmap(void *obj)
{
	SpnHashMap *hm = obj;
	free_bucket_array(hm->buckets, hm->allocsize);
}

size_t spn_hashmap_count(SpnHashMap *hm)
{
	return hm->count;
}

SpnValue spn_makehashmap(void)
{
	return makeobject(spn_hashmap_new());
}


/* Internal functions */

static size_t modulo_mask(SpnHashMap *hm)
{
	/* cannot index into empty table; allocsize must be a power of two */
	assert(hm->allocsize > 0 && !(hm->allocsize & (hm->allocsize - 1)));
	return hm->allocsize - 1;
}

static size_t hash_index(SpnHashMap *hm, const SpnValue *key)
{
	return spn_hash_value(key) & modulo_mask(hm);
}

static int should_rehash(SpnHashMap *hm)
{
	/* keep load factor below 0.75 */
	return hm->allocsize == 0 || hm->count > hm->allocsize * 3 / 4;
}

/* Returns a pointer to the bucket containing 'key',
 * or NULL if the key is not in the hash table.
 */
static Bucket *find_key(SpnHashMap *hm, const SpnValue *key)
{
	size_t h, hash_offset;

	/* do not try dividing by zero;
	 * there are no entries in an empty table.
	 */
	if (hm->allocsize == 0) {
		return NULL;
	}

	/* linear probing */
	h = hash_index(hm, key);
	hash_offset = 0;

	do {
		/* only occupied buckets may contain a key-value pair */
		if (bucket_is_used(&hm->buckets[h])
		 && spn_value_equal(&hm->buckets[h].key, key)) {
			return &hm->buckets[h];
		}

		/* wrap around end of bucket array if necessary */
		h = (h + 1) & modulo_mask(hm);
		hash_offset++;
	} while (hash_offset <= hm->max_hash_offset);

	/* key was not found */
	return NULL;
}

static void insert_nonexistent_norehash(
	SpnHashMap *hm,
	const SpnValue *key,
	const SpnValue *value,
	int should_retain
)
{
	size_t h, hash_offset;

	assert(should_rehash(hm) == 0);    /* table must be large enough  */
	assert(hm->count < hm->allocsize); /* there must be empty buckets */
	assert(find_key(hm, key) == NULL); /* the key must not exist yet  */

	h = hash_index(hm, key);
	hash_offset = 0;

	/* find an empty bucket */
	while (bucket_is_used(&hm->buckets[h])) {
		h = (h + 1) & modulo_mask(hm);
		hash_offset++;
	}

	insert_into_bucket(&hm->buckets[h], key, value, should_retain);
	assert(bucket_is_used(&hm->buckets[h]));

	hm->count++;

	if (hash_offset > hm->max_hash_offset) {
		hm->max_hash_offset = hash_offset;
	}
}

/* key public getter function */
SpnValue spn_hashmap_get(SpnHashMap *hm, const SpnValue *key)
{
	Bucket *node = find_key(hm, key);
	return node ? node->value : spn_nilval;
}

/* The public setter function */
void spn_hashmap_set(SpnHashMap *hm, const SpnValue *key, const SpnValue *val)
{
	Bucket *bucket = find_key(hm, key);

	/* If key is already found in the table,
	 * its corresponding value MUST be non-nil!
	 * (since assigning nil to a value recycles its bucket.)
	 */
	if (bucket != NULL) {
		assert(bucket_is_used(bucket));

		if (notnil(val)) {
			/* retain new before releasing old to ensure memory safety. */
			spn_value_retain(val);
			spn_value_release(&bucket->value);
			bucket->value = *val;
		} else {
			/* assigning nil to a previously non-nil value: deletion */
			hm->count--;
			erase_bucket(bucket);
			assert(bucket_is_empty(bucket));
		}

		/* I'm outta here! */
		return;
	}

	/* Step 2: If the key is not found, and the new value
	 * is nil, we don't need to do anything at all.
	 */
	if (isnil(val)) {
		return;
	}

	/* Step 3: Otherwise we'll need to insert it.
	 * When the load factor crosses 3/4, we perform a complete rehash.
	 * This operation invalidates 'hm->buckets'.
	 */
	if (should_rehash(hm)) {
		expand_and_rehash(hm);
	}

	/* Finally, the new key is inserted */
	insert_nonexistent_norehash(hm, key, val, 1);
}

/* a helper for 'expand_and_rehash()' which
 * allocates and initializes a bucket array.
 */
static Bucket *alloc_bucket_array(size_t n)
{
	Bucket *buckets = spn_malloc(n * sizeof buckets[0]);

	size_t i;
	for (i = 0; i < n; i++) {
		buckets[i].key = spn_nilval;
		buckets[i].value = spn_nilval;
	}

	return buckets;
}

static void free_bucket_array(Bucket *buckets, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		spn_value_release(&buckets[i].key);
		spn_value_release(&buckets[i].value);
	}

	free(buckets);
}

static void expand_and_rehash(SpnHashMap *hm)
{
	Bucket *oldbuckets = hm->buckets;
	size_t oldsize = hm->allocsize;
	size_t i;

	/* Allocation size needs to be a power of two,
	 * since masking is used for modulo division.
	 */
	hm->allocsize = oldsize ? oldsize * 2 : 8;
	hm->buckets = alloc_bucket_array(hm->allocsize);

	/* Reset internal state */
	hm->count = 0;
	hm->max_hash_offset = 0;

	/* When expanding, our situation is a little bit better
	 * than a full-fledged insert, since we know that we are
	 * inserting into an empty table. Consequently, we don't
	 * have to check for already-existing keys, since we
	 * explicitly disallow duplicates during the insertion.
	 * We don't need to check for nils either, because they
	 * are trivially filtered out right within the loop.
	 */
	for (i = 0; i < oldsize; i++) {
		/* only re-insert non-empty (used) buckets */
		if (bucket_is_used(&oldbuckets[i])) {
			/* do not retain - ownership transfer (move) is performed */
			insert_nonexistent_norehash(hm, &oldbuckets[i].key, &oldbuckets[i].value, 0);
		}
	}

	/* no retains were caused by insert_nonexistent_norehash(),
	 * so we don't need to explicitly release the keys and values.
	 */
	free(oldbuckets);
}

void spn_hashmap_delete(SpnHashMap *hm, const SpnValue *key)
{
	spn_hashmap_set(hm, key, &spn_nilval);
}

SpnValue spn_hashmap_get_strkey(SpnHashMap *hm, const char *key)
{
	SpnString key_str = spn_string_emplace_nonretained_for_hashmap(key);
	SpnValue key_val = makeobject(&key_str);

	return spn_hashmap_get(hm, &key_val);
}

void spn_hashmap_set_strkey(SpnHashMap *hm, const char *key, const SpnValue *val)
{
	SpnValue str = makestring(key);
	spn_hashmap_set(hm, &str, val);
	spn_value_release(&str);
}

size_t spn_hashmap_next(SpnHashMap *hm, size_t cursor, SpnValue *key, SpnValue *val)
{
	size_t size = hm->allocsize;
	size_t i;

	for (i = cursor; i < size; i++) {
		Bucket *bucket = &hm->buckets[i];

		if (bucket_is_used(bucket)) {
			*key = bucket->key;
			*val = bucket->value;
			return i + 1;
		}
	}

	return 0;
}
