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

/* An attempt to detect sizeof(unsigned long) at compile-time.
 * I'm not much into the C preprocessor, but since it computes
 * every integral expression using the widest integer known to
 * its corresponding C compiler, and that C compiler might not
 * have an integer type wide enough to represent 0x2fffffffffff,
 * the may overflow and the comparison will probably fail.
 * Hence we do a little workaround. The idea is this:
 * 1. ULONG_MAX is always >= 0xffffffff (guaranteed by C89).
 * 2. If ULONG_MAX == 0xffffffff, we're most probably on 32-bit.
 * 3. Else it's presumably safe to assume something more than
 *    32-bit and check if all the size primes fit into unsigned
 *    long, with a little bit of extra safety reserve.
 */
#if ULONG_MAX > 0xffffffffu
	#if ULONG_MAX >= 0x2fffffffffffu
		#define ALL_SIZES_FIT 1
	#else
		#define ALL_SIZES_FIT 0
	#endif /* ULONG_MAX >= 0x2fffffffffffu */
#else
	#define ALL_SIZES_FIT 0
#endif /* ULONG_MAX > 0xffffffffu */


typedef struct Bucket {
	SpnValue key;
	SpnValue value;
	struct Bucket *next;
} Bucket;

struct SpnHashMap {
	SpnObject   base;
	int         sizeindex;   /* index into 'sizes' array (see below)   */
	size_t      keycount;    /* number of keys in the table            */
	size_t      valcount;    /* number of non-nil values. <= keycount. */
	Bucket     *buckets;     /* actual array for key-value pairs       */
};

static void free_hashmap(void *obj);
static void expand_or_rehash(SpnHashMap *hm, int always_expand);

/* Hash table allocation sizes. These are primes of which
 * the ratio asymptotically approaches phi, the golden ratio.
 */
static const unsigned long sizes[] = {
	0x0,             0x3,             0x7,             0xD,
	0x17,            0x29,            0x47,            0x7F,
	0xBF,            0xFB,            0x17F,           0x277,
	0x43F,           0x6BB,           0xAF3,           0x11AB,
	0x1CB7,          0x2EB7,          0x4BF7,          0x79FF,
	0xC5FB,          0x13FFF,         0x205FF,         0x345F7,
	0x549EF,         0x88FD5,         0xDD9EF,         0x1669FF,
	0x2441FF,        0x3AABFF,        0x5EEDFF,        0x9999F5,
	0xF887FF,        0x19221FB,       0x28AA9D9,       0x41CCBE5,
	0x6A777F7,       0xAC443EF,       0x116BB9EF,      0x1C2FFDF3,
	0x2D9BB9FD,      0x49CBB7EF,      0x77676FE5,      0xC13327FF,
#if ALL_SIZES_FIT
	0x1389A97E3,     0x1F9CDBDF5,     0x3326855D7,     0x52C3613FF,
	0x85E9E69EF,     0xD8AD47DF9,     0x15E972E7D5,    0x23744765E9,
	0x395DBA4BFB,    0x5CD201B1F7,    0x962FBBFDFF,    0xF301BDADF7,
	0x1893179ABF3,   0x27C333759E9,   0x40564B105F1,   0x68197E85FF9,
	0xA86FC9967E5,   0x11089481C7E9,  0x1B8F911B2DFB,  0x2C98259CF549
#endif /* ALL_SIZES_FIT */
};

static const SpnClass spn_class_hashmap = {
	sizeof(SpnHashMap),
	NULL,
	NULL,
	NULL,
	free_hashmap
};


SpnHashMap *spn_hashmap_new()
{
	SpnHashMap *hm = spn_object_new(&spn_class_hashmap);

	hm->sizeindex = 0;
	hm->keycount = 0;
	hm->valcount = 0;
	hm->buckets = NULL;

	return hm;
}

static void free_hashmap(void *obj)
{
	SpnHashMap *hm = obj;
	size_t allocsize = sizes[hm->sizeindex];
	size_t i;

	for (i = 0; i < allocsize; i++) {
		spn_value_release(&hm->buckets[i].key);
		spn_value_release(&hm->buckets[i].value);
	}

	free(hm->buckets);
}

size_t spn_hashmap_count(SpnHashMap *hm)
{
	return hm->valcount;
}

static Bucket *find_key(Bucket *head, const SpnValue *key)
{
	Bucket *node = head;
	assert(node != NULL);

	do {
		if (spn_value_equal(&node->key, key)) {
			return node;
		}

		node = node->next;
	} while (node != NULL);

	return NULL;
}

/* This returns '&buckets[headidx]' itself if possible (i. e. if
 * that slot is empty), and searches for a nearby node otherwise.
 * Since we consistently enforce a load factor strictly less than 1
 * (namely, 1 / phi ~= 3 / 5), this search should NEVER, EVER fail.
 *
 * Note that here the load factor means the ratio of slots that
 * contain a key. After deleting some items, it may be the case that
 * this is more than the perceived size of the hash table, i. e. the
 * number of non-nil values, regardless of whether they correspond
 * to a key.
 */
static Bucket *find_next_empty(Bucket *buckets, size_t headidx, size_t allocsize)
{
	size_t i = headidx, n = allocsize;

	assert(headidx < allocsize);

	while (n--) {
		Bucket *node = &buckets[i];

		if (isnil(&node->key)) {
			assert(isnil(&node->value));
			assert(node->next == NULL);
			return node;
		}

		i++;

		/* wrap around end of table */
		if (i >= allocsize) {
			i = 0;
		}
	}

	assert("No empty slot found, infinite loop detected! Fatal error" == 0);
	return NULL;
}

void spn_hashmap_get(SpnHashMap *hm, const SpnValue *key, SpnValue *val)
{
	Bucket *head, *node;
	size_t index;
	size_t allocsize = sizes[hm->sizeindex];

	/* avoid division by 0 - an empty map has no values anyway */
	if (allocsize == 0) {
		*val = makenil();
		return;
	}

	/* compute hash and get candidate bucket */
	index = spn_hash_value(key) % allocsize;
	head = &hm->buckets[index]; /* not NULL */
	node = find_key(head, key);
	*val = node ? node->value : makenil();
}

void spn_hashmap_set(SpnHashMap *hm, const SpnValue *key, const SpnValue *val)
{
	Bucket *bucket, *fresh, *home;
	unsigned long hash;
	size_t index;

	assert(notnil(key));

	/* Step 0: check degenerate cases and avoid division by 0 */
	if (sizes[hm->sizeindex] == 0) {
		/* inserting nil into an empty hash table is a no-op */
		if (isnil(val)) {
			return;
		}

		/* else we will need to make room for the value */
		expand_or_rehash(hm, 1);
	}

	hash = spn_hash_value(key);
	index = hash % sizes[hm->sizeindex];

	/* Step 1: We search for a bucket that already contains the
	 * key, becaue we need to replace the old value if it exists.
	 */
	bucket = find_key(&hm->buckets[index], key);

	/* If key is already found in the table: */
	if (bucket != NULL) {
		/* check all possible cases:
		 * 1. changing nil to nil is a no-op
		 * 2. changing nil to non-nil means insertion
		 * 3. changing non-nil to nil means removal
		 * 4. changing non-nil to non-nil should be executed,
		 *    but it doesn't affect the count of the values.
		 *
		 * The count of the keys is not changed here, though,
		 * since we are re-using an existing key if possible.
		 */
		if (isnil(&bucket->value) && isnil(val)) {
			return;
		} else if (isnil(&bucket->value) && notnil(val)) {
			hm->valcount++;
		} else if (notnil(&bucket->value) && isnil(val)) {
			hm->valcount--;
		}

		/* don't touch key, just replace the value */
		spn_value_retain(val);
		spn_value_release(&bucket->value);
		bucket->value = *val;

		return;
	}

	/* Step 2: If the key is not found, and the new value
	 * is nil, we don't need to do anything at all.
	 */
	if (isnil(val)) {
		return;
	}

	/* Step 3: Otherwise we'll need to insert it, so we increase
	 * the counts and take ownership of the key and the value.
	 */
	hm->keycount++;
	hm->valcount++;

	spn_value_retain(key);
	spn_value_retain(val);

	/* When the load factor crosses 1 / phi (aka 5 / 8), we do
	 * a complete rehash. If it's only the number of keys that
	 * exceeded the maximal load factor, but the value count is
	 * smaller than the maximal load factor, then the bucket
	 * vector is not actually grown. Instead, the non-nil values
	 * are reinserted into a table of the same size, freeing up
	 * unused keys.
	 *
	 * If, however, the number of non-nil values is also greater
	 * than the maximal load factor, then the array of vectors
	 * is expanded before rehashing.
	 *
	 * This operation invalidates 'index' and 'hm->buckets'...
	 */
	if (8 * hm->keycount > 5 * sizes[hm->sizeindex]) {
		expand_or_rehash(hm, 0);
	}

	/* ...so we just re-compute them after the reallocation. */
	index = hash % sizes[hm->sizeindex];
	home = &hm->buckets[index];

	/* If the home position of the key is empty, we are done */
	if (isnil(&home->key)) {
		assert(isnil(&home->value));
		assert(home->next == NULL);

		home->key = *key;
		home->value = *val;
		return;
	}

	/* Else a collision happened, so we pick an empty/unused bucket
	 * near 'index'. We then fill it in and link it into the chain.
	 */
	fresh = find_next_empty(hm->buckets, index, sizes[hm->sizeindex]);
	assert(fresh != NULL);
	assert(fresh != home); /* avoid accidental circular self-references */
	assert(fresh->next == NULL);
	assert(isnil(&fresh->key));
	assert(isnil(&fresh->value));

	/* set key and value */
	fresh->key = *key;
	fresh->value = *val;

	/* Insert fresh bucket into 2nd place of list. */
	fresh->next = home->next;
	home->next = fresh;
}

static void expand_or_rehash(SpnHashMap *hm, int always_expand)
{
	size_t oldsize, newsize, i;
	Bucket *oldbuckets, *newbuckets;

	oldsize = sizes[hm->sizeindex];
	oldbuckets = hm->buckets;

	/* check if there are indeed more values than healthy,
	 * or it is just that too many keys have been deleted
	 */
	if (8 * hm->valcount > 5 * oldsize || always_expand) {
		hm->sizeindex++;

		if (hm->sizeindex >= COUNT(sizes)) {
			fputs("exceeded maximal size of hashmap", stderr);
			abort();
		}
	}

	newsize = sizes[hm->sizeindex];
	newbuckets = spn_malloc(newsize * sizeof newbuckets[0]);

	/* the new bucket array starts out all empty */
	for (i = 0; i < newsize; i++) {
		newbuckets[i].key = makenil();
		newbuckets[i].value = makenil();
		newbuckets[i].next = NULL;
	}

	/* When expanding, our situation is a little bit better
	 * than a full-fledged insert, since we know that we are
	 * inserting into an empty table. Consequently, we don't
	 * have to check for already-existing keys, since we
	 * explicitly disallow duplicates during the insertion.
	 * We don't need to check for nils either, because they
	 * are trivially filtered out right within the loop.
	 */
	for (i = 0; i < oldsize; i++) {
		size_t index;
		Bucket *home, *bucket;

		if (isnil(&oldbuckets[i].key)) {
			continue;
		}

		/* If the key exists, but the value is nil, then
		 * the value is conceptually not in the hash table.
		 * So we just relinquish ownership of the value
		 * and continue.
		 */
		if (isnil(&oldbuckets[i].value)) {
			spn_value_release(&oldbuckets[i].key);
			continue;
		}

		/* find slot for new key-value pair */
		index = spn_hash_value(&oldbuckets[i].key) % newsize;
		home = &newbuckets[index];

		/* if it's empty yet, we insert it and we're done */
		if (isnil(&home->key)) {
			assert(isnil(&home->value));
			assert(home->next == NULL);

			home->key   = oldbuckets[i].key;
			home->value = oldbuckets[i].value;
			continue;
		}

		/* else we find a nearby empty slot and link it into the list */
		bucket = find_next_empty(newbuckets, index, newsize);

		assert(bucket != NULL);
		assert(bucket != home); /* avoid circular references */
		assert(bucket->next == NULL);
		assert(isnil(&bucket->key));
		assert(isnil(&bucket->value));

		/* perform insertion, no fiddling with ownership needed */
		bucket->key   = oldbuckets[i].key;
		bucket->value = oldbuckets[i].value;

		/* link it in */
		bucket->next = home->next;
		home->next = bucket;
	}

	hm->keycount = hm->valcount;
	hm->buckets = newbuckets;
	free(oldbuckets);
}

void spn_hashmap_delete(SpnHashMap *hm, const SpnValue *key)
{
	SpnValue nilval = makenil();
	spn_hashmap_set(hm, key, &nilval);
}

size_t spn_hashmap_next(SpnHashMap *hm, size_t cursor, SpnValue *key, SpnValue *val)
{
	size_t size = sizes[hm->sizeindex];
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
