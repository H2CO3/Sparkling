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
#include "private.h"


typedef enum BucketState {
	Fresh,
	Occupied,
	Recycled
} BucketState;

typedef struct Bucket {
	SpnValue key;
	SpnValue value;
	BucketState state;
} Bucket;

static int bucket_is_part_of_cluster(const Bucket *bucket)
{
	return bucket->state != Fresh;
}

static int bucket_is_empty(const Bucket *bucket)
{
	return bucket->state != Occupied;
}

static int bucket_is_occupied(const Bucket *bucket)
{
	return bucket->state == Occupied;
}

/* retains key and value */
static void insert_into_bucket(Bucket *bucket, const SpnValue *key, const SpnValue *value)
{
	assert(bucket_is_empty(bucket));

	spn_value_retain(key);
	spn_value_retain(value);

	bucket->key = *key;
	bucket->value = *value;

	bucket->state = Occupied;
}

static void recycle_bucket(Bucket *bucket)
{
	/* deallocate key and value (RAII/DIRR),
	 * then clean them by setting them to nil.
	 */
	spn_value_release(&bucket->key);
	spn_value_release(&bucket->value);

	bucket->key = spn_nilval;
	bucket->value = spn_nilval;

	/* mark as Recycled rather than Fresh because the latter
	 * would terminate the linear probing prematurely
	 */
	bucket->state = Recycled;
}

struct SpnHashMap {
	SpnObject  base;
	size_t     allocsize; /* number of buckets                */
	size_t     count;     /* number of key-value pairs        */
	Bucket    *buckets;   /* actual array for key-value pairs */
};

static void free_hashmap(void *obj);
static void free_bucket_array(Bucket *buckets, size_t n);
static void expand_and_rehash(SpnHashMap *hm);


static const SpnClass spn_class_hashmap = {
	sizeof(SpnHashMap),
	SPN_CLASS_UID_HASHMAP,
	NULL,
	NULL,
	NULL,
	free_hashmap
};


SpnHashMap *spn_hashmap_new()
{
	SpnHashMap *hm = spn_object_new(&spn_class_hashmap);

	hm->allocsize = 0;
	hm->count = 0;
	hm->buckets = NULL;

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
	SpnValue val;
	val.type = SPN_TYPE_HASHMAP;
	val.v.o = spn_hashmap_new();
	return val;
}


/* Internal functions */

static size_t hash_index(SpnHashMap *hm, const SpnValue *key)
{
	/* cannot index into empty table */
	assert(hm->allocsize > 0);

	/* TODO: ensure that hm->allocsize is a power of two */
	return spn_hash_value(key) & (hm->allocsize - 1);
}

/* Returns a pointer to the bucket containing 'key',
 * or NULL if the key is not in the hash table.
 */
static Bucket *find_key(SpnHashMap *hm, const SpnValue *key)
{
	size_t h;

	/* do not try dividing by zero;
	 * there are no entries in an empty table.
	 */
	if (hm->allocsize == 0) {
		return NULL;
	}

	/* linear probing */
	h = hash_index(hm, key);

	while (bucket_is_part_of_cluster(&hm->buckets[h])) {
		/* only occupied buckets may contain a key-value pair */
		if (bucket_is_occupied(&hm->buckets[h])
		 && spn_value_equal(&hm->buckets[h].key, key)) {
			return &hm->buckets[h];
		}

		/* wrap around end of bucket array if necessary */
		h++;
		if (h >= hm->allocsize) {
			h = 0;
		}
	}

	/* key was not found */
	return NULL;
}

/* The public getter function */
SpnValue spn_hashmap_get(SpnHashMap *hm, const SpnValue *key)
{
	Bucket *node = find_key(hm, key);
	return node ? node->value : spn_nilval;
}

/* The public setter function */
void spn_hashmap_set(SpnHashMap *hm, const SpnValue *key, const SpnValue *val)
{
	Bucket *bucket = find_key(hm, key);
	size_t h;

	/* If key is already found in the table,
	 * its corresponding value MUST be non-nil!
	 * (since assigning nil to a value recycles its bucket.)
	 */
	if (bucket != NULL) {
		assert(notnil(&bucket->value));

		if (notnil(val)) {
			/* retain new before releasing old to ensure memory safety. */
			spn_value_retain(val);
			spn_value_release(&bucket->value);
			bucket->value = *val;
		} else {
			/* assigning nil to a previously non-nil value: deletion */
			hm->count--;
			recycle_bucket(bucket);
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

	/* Step 3: Otherwise we'll need to insert it, so we increase
	 * the count and take ownership of the key and the value.
	 */
	hm->count++;

	/* When the load factor crosses 1 / phi (approx. 5 / 8),
	 * we do a complete rehash.
	 *
	 * This operation invalidates 'hm->buckets'...
	 */
	if (8 * hm->count > 5 * hm->allocsize) {
		expand_and_rehash(hm);
	}

	/* ...so we just re-compute everything after the reallocation,
	 * and perform linear probing.
	 *
	 * It is safe to call 'hash_index()' here, since count is at
	 * least 1 because we just incremented it.
	 */
	h = hash_index(hm, key);

	/* if this assertion fires, then find_key() was
	 * unable to correctly find an existing key.
	 */
	assert(bucket_is_empty(&hm->buckets[h])
		|| !spn_value_equal(&hm->buckets[h].key, key));

	/* find first fresh or recycled bucket */
	while (bucket_is_occupied(&hm->buckets[h])) {
		/* wrap around end of array if needed */
		h++;
		if (h >= hm->allocsize) {
			h = 0;
		}
	}

	/* set key and value of bucket (retaining both the
	 * key and the value), then mark it as occupied
	 */
	insert_into_bucket(&hm->buckets[h], key, val);
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
		buckets[i].state = Fresh;
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
	size_t oldsize, newsize, i;
	Bucket *oldbuckets, *newbuckets;

	oldbuckets = hm->buckets;
	oldsize = hm->allocsize;

	/* Allocation size needs to be a power of two,
	 * since masking is used for modulo division.
	 */
	if (oldsize == 0) {
		newsize = 8;
	} else {
		newsize = oldsize * 2;
	}

	newbuckets = alloc_bucket_array(newsize);

	hm->buckets = newbuckets;
	hm->allocsize = newsize;

	/* When expanding, our situation is a little bit better
	 * than a full-fledged insert, since we know that we are
	 * inserting into an empty table. Consequently, we don't
	 * have to check for already-existing keys, since we
	 * explicitly disallow duplicates during the insertion.
	 * We don't need to check for nils either, because they
	 * are trivially filtered out right within the loop.
	 */
	for (i = 0; i < oldsize; i++) {
		size_t h;

		/* do not re-insert empty (fresh or recycled) buckets */
		if (bucket_is_empty(&oldbuckets[i])) {
			continue;
		}

		/* hash the key, insert into table using linear probing.
		 * It is safe to call 'hash_index()' because newsize
		 * is always strictly positive (in fact, it's >= 8).
		 */
		h = hash_index(hm, &oldbuckets[i].key);

		while (bucket_is_occupied(&newbuckets[h])) {
			h++;
			if (h >= newsize) {
				h = 0;
			}
		}

		/* some sanity checks */
		assert(newbuckets[h].state == Fresh);
		assert(bucket_is_occupied(&oldbuckets[i]));

		/* ownership transfer! */
		newbuckets[h] = oldbuckets[i];
	}

	/* we do not need to release individual old buckets
	 * due to the ownership transfer mentioned above.
	 * Hence, we just plain 'free()' the bucket array.
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

	SpnValue key_val;
	key_val.type = SPN_TYPE_STRING;
	key_val.v.o = &key_str;

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

		if (notnil(&bucket->value)) {
			*key = bucket->key;
			*val = bucket->value;
			return i + 1;
		}
	}

	return 0;
}
