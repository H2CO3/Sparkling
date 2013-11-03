/*
 * array.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Arrays: fast associative array implementation
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "array.h"

#if UINT_MAX <= 0xffff
#define SPN_LOW_MEMORY_PLATFORM 1
#else
#define SPN_LOW_MEMORY_PLATFORM 0
#endif


#if SPN_LOW_MEMORY_PLATFORM
#define HASH_NSIZES	12
#define ARRAY_MAXSIZE	0x8000
#else
#define HASH_NSIZES	24
#define ARRAY_MAXSIZE	0x4000000
#endif


/* 
 * The usual trick for dynamically sized arrays holds for the hash table
 * part -- expand storage exponentially:
 * 
 * 	if (size + 1 > allocation_size) {
 * 		allocation_size *= 2;
 * 	}
 *
 * However, we don't want the size to be a power of two, since that would
 * simply truncate the hash to its least significant bits, decreasing entropy
 * (the key for a particular value is taken modulo the size of the array).
 * Instead, the sizes are prime numbers close to consecutive powers of two,
 * from 2^3 to 2^25, inclusive.
 */
static unsigned long nthsize(int idx)
{
	static const unsigned long szprims[HASH_NSIZES] = {
		0,		7,		17,		29,
		67,		131,		257,		509,
		1031,		2053,		4099,		8209,
#if !SPN_LOW_MEMORY_PLATFORM
		16381,		32771,		65539,		131063,
		262139,		524287,		1048583,	2097169,
		4194301,	8388617,	16777213,	33554467
#endif
	};

	return szprims[idx];
}

typedef struct KVPair {
	SpnValue key;
	SpnValue val;
} KVPair;

typedef struct TList {
	KVPair pair;
	struct TList *next;
} TList;

struct SpnArray {
	SpnObject	  base;		/* for being a valid object		*/
	SpnValue	  nilval;	/* for returning nil			*/

	SpnValue	 *arr;		/* the array part			*/
	size_t		  arrcnt;	/* logical size				*/
	size_t		  arrallsz;	/* allocation (actual) size		*/

	TList		**buckets;	/* the hash table part			*/
	size_t		  hashcnt;	/* logical size				*/
	int		  hashszidx;	/* index of allocation (actual) size	*/
};

struct SpnIterator {
	SpnArray	 *arr;		/* weak reference to owning array	*/
	size_t		  idx;		/* ordinal number of key-value pair	*/
	size_t		  cursor;	/* the essence				*/
	TList		 *list;		/* linked list entry for the hash part	*/
	int		  inarray;	/* helper flag for traversing hash part	*/
};


static void free_array(void *obj);


static const SpnClass spn_class_array = {
	"array",
	sizeof(SpnArray),
	NULL,
	NULL,
	NULL,
	free_array
};

static unsigned long hash_key(const SpnValue *key);


static TList *list_prepend(TList *head, SpnValue *key, SpnValue *val);
static KVPair *list_find(TList *head, const SpnValue *key);
static TList *list_delete_releasing(TList *head, const SpnValue *key);
static void list_free_releasing(TList *head);


static void expand_array_if_needed(SpnArray *arr, unsigned long idx);
static void insert_and_update_count_array(SpnArray *arr, unsigned long idx, SpnValue *val);
static void expand_hash(SpnArray *arr);
static void insert_and_update_count_hash(SpnArray *arr, SpnValue *key, SpnValue *val);

SpnArray *spn_array_new()
{
	SpnArray *arr = spn_object_new(&spn_class_array);

	arr->nilval.t = SPN_TYPE_NIL;
	arr->nilval.f = 0;

	arr->arr = NULL;
	arr->arrcnt = 0;
	arr->arrallsz = 0;

	arr->buckets = NULL;
	arr->hashcnt = 0;
	arr->hashszidx = 0;

	return arr;
}

static void free_array(void *obj)
{
	SpnArray *arr = obj;
	size_t i;

	for (i = 0; i < arr->arrallsz; i++) {
		spn_value_release(&arr->arr[i]);
	}

	free(arr->arr);

	for (i = 0; i < nthsize(arr->hashszidx); i++) {
		list_free_releasing(arr->buckets[i]);
	}

	free(arr->buckets);
	free(arr);
}

size_t spn_array_count(SpnArray *arr)
{
	return arr->arrcnt + arr->hashcnt;
}

/* getter and setter */

SpnValue *spn_array_get(SpnArray *arr, const SpnValue *key)
{
	unsigned long hash;
	TList *list;
	KVPair *hit;
	
	/* integer key of which the value fits into the array part */
	if (key->t == SPN_TYPE_NUMBER) {
		if (key->f & SPN_TFLG_FLOAT) {
			long i = key->v.fltv;
			if (key->v.fltv == i /* int disguised as a float */
			 && 0 <= i
			 && i < ARRAY_MAXSIZE) {
				unsigned long uidx = i; /* silent the -Wsign-compare warning */
			 	return uidx < arr->arrallsz ? &arr->arr[i] : &arr->nilval;
			}
		} else {
			long i = key->v.intv;
			if (0 <= i && i < ARRAY_MAXSIZE) {
				unsigned long uidx = i; /* silent the -Wsign-compare warning */
				return uidx < arr->arrallsz ? &arr->arr[i] : &arr->nilval;
			}
		}
	}

	/* else: the value goes to/comes from the hash table part */

	/* if the hash table is empty, it cannot contain any value at all */
	if (nthsize(arr->hashszidx) == 0) {
		return &arr->nilval;
	}

	/* else return what the hash part can find */
	hash = hash_key(key) % nthsize(arr->hashszidx);

	/* return what's found in the link list at the computed index
	 * (behaves correctly if the list is empty/NULL)
	 */
	list = arr->buckets[hash];
	hit = list_find(list, key);
	return hit != NULL ? &hit->val : &arr->nilval;
}

void spn_array_set(SpnArray *arr, SpnValue *key, SpnValue *val)
{
	/* integer key of which the value fits into the array part */
	if (key->t == SPN_TYPE_NUMBER) {
		if (key->f & SPN_TFLG_FLOAT) {
			long i = key->v.fltv;	/* truncate */
			if (key->v.fltv == i	/* is it actually an integer? */
			 && 0 <= i			/* fits into array part? */
			 && i < ARRAY_MAXSIZE) {
				insert_and_update_count_array(arr, i, val);
				return;
			}
		} else {
			long i = key->v.intv;
			if (0 <= i && i < ARRAY_MAXSIZE) { /* fits into array part? */
				insert_and_update_count_array(arr, i, val);
				return;
			}
		}
	}

	/* if they key is neither an integer that fits into the array part,
	 * nor an integer disguised as a float which fits into the array part
	 * (i. e. it's negative, too large, nil, boolean, a function
	 * or an object), then it's inserted into the hash table part instead.
	 */

	insert_and_update_count_hash(arr, key, val);
}

void spn_array_remove(SpnArray *arr, SpnValue *key)
{
	SpnValue nilval = { { 0 }, SPN_TYPE_NIL, 0 };
	spn_array_set(arr, key, &nilval);
}

/* iterators */

SpnIterator *spn_iter_new(SpnArray *arr)
{
	SpnIterator *it = malloc(sizeof(*it));
	if (it == NULL) {
		abort();
	}

	it->arr = arr;
	it->idx = 0;
	it->cursor = 0;
	it->list = NULL;
	it->inarray = 1;

	return it;
}

SpnArray *spn_iter_getarray(SpnIterator *it)
{
	return it->arr;
}

void spn_iter_free(SpnIterator *it)
{
	free(it);
}

size_t spn_iter_next(SpnIterator *it, SpnValue *key, SpnValue *val)
{
	SpnArray *arr = it->arr;
	size_t hashsz, i;

	/* search the array part first: cursor in [0...arraysize) (*) */
	if (it->inarray) {
		for (i = it->cursor; i < arr->arrallsz; i++) {
			if (arr->arr[i].t != SPN_TYPE_NIL) {
				/* set up key */
				key->t = SPN_TYPE_NUMBER;
				key->f = 0;
				key->v.intv = i;

				/* set up value */
				*val = arr->arr[i];

				/* advance cursor to next base index */
				it->cursor = i + 1;

				return it->idx++;
			}
		}

		/* if not found, search hash part: cursor in [0...hashsize) */
		it->cursor = 0; /* reset to point to the beginning */
		it->inarray = 0; /* indicate having finished with the array part */
	} else {
		if (it->list != NULL) {
			*key = it->list->pair.key;
			*val = it->list->pair.val;
			it->list = it->list->next;
			return it->idx++;
		}

		/* if the node is NULL, it means we finished the traversal of
		 * the linked list, so we fall through in order to search for
		 * the next bucket
		 */
	}

	hashsz = nthsize(arr->hashszidx);
	for (i = it->cursor; i < hashsz; i++) {
		if (arr->buckets[i] != NULL) {
			it->list = arr->buckets[i];
			it->cursor = i + 1;
			/* sorry for having been clever here, but this is more
			 * convenient with one level of recursion and it
			 * avoids redundancy as well
			 */
			return spn_iter_next(it, key, val);
		}
	}

	/* if not found, there are no more entries */
	assert(it->idx == spn_array_count(arr)); /* sanity check */
	return it->idx;
}

/* 
 * Linked lists for collision resolution with separate chaining
 *
 * -----
 *
 * prepend and not append, so that we can write something like
 * 
 * 	head = list_prepend(head, key_val_pair);
 * 
 * and have O(1) running time, instead of appending to the end
 * by traversing the whole list each time a collision occurs, which is O(n)
 *
 * This function does not retain the key and the value, because
 * this very same function is used when performing the rehash upon
 * expansion, and there no change is needed in the ownership.
 * Thus, when a new non-nil value is actually inserted, then we
 * do the retaining manually in spn_array_set().
 */
static TList *list_prepend(TList *head, SpnValue *key, SpnValue *val)
{
	TList *node = malloc(sizeof(*node));
	if (node == NULL) {
		abort();
	}

	node->pair.key = *key;
	node->pair.val = *val;
	node->next = head;
	return node;
}

static KVPair *list_find(TList *head, const SpnValue *key)
{
	while (head != NULL) {
		KVPair *pair = &head->pair;
		SpnValue *pkey = &pair->key;

		if (spn_value_equal(key, pkey)) {
			return pair;
		}

		head = head->next;
	}

	return NULL;
}

static TList *list_delete_releasing(TList *head, const SpnValue *key)
{
	if (head == NULL) {
		return NULL;
	}

	if (spn_value_equal(&head->pair.key, key)) {
		TList *tmp = head->next;

		spn_value_release(&head->pair.key);
		spn_value_release(&head->pair.val);

		free(head);
		return tmp;
	}

	head->next = list_delete_releasing(head->next, key);
	return head;
}

static void list_free_releasing(TList *node)
{
	while (node != NULL) {
		TList *tmp = node->next;

		spn_value_release(&node->pair.key);
		spn_value_release(&node->pair.val);

		free(node);
		node = tmp;
	}
}

/* Low-level array and hash table helpers */
static void expand_array_if_needed(SpnArray *arr, unsigned long idx)
{
	size_t prevsz = arr->arrallsz;
	size_t i;

	if (idx < arr->arrallsz) {
		return;			/* there's enough room, nothing to do	*/
	}

	if (arr->arrallsz == 0) {	/* if it was empty, allocate space	*/
		arr->arrallsz = 0x4;
	}

	while (idx >= arr->arrallsz) {	/* expand until index fits in vector	*/
		arr->arrallsz <<= 1;
	}

	/* ask the OS to do its job */
	arr->arr = realloc(arr->arr, sizeof(arr->arr[0]) * arr->arrallsz);
	if (arr->arr == NULL) {
		abort();
	}

	/* and fill all the not-yet-existent fields with nil */
	for (i = prevsz; i < arr->arrallsz; i++) {
		arr->arr[i].t = SPN_TYPE_NIL;
		arr->arr[i].f = 0;
	}
}

static void insert_and_update_count_array(SpnArray *arr, unsigned long idx, SpnValue *val)
{
	/* check if the array needs to expand beyond its current size */
	expand_array_if_needed(arr, idx);

	/* then check if a previously nonexistent object is inserted... */
	if (arr->arr[idx].t == SPN_TYPE_NIL
	 && val->t != SPN_TYPE_NIL) {
		arr->arrcnt++;
	}

	/* ...or conversely, a previously existent object is deleted */
	if (arr->arr[idx].t != SPN_TYPE_NIL
	 && val->t == SPN_TYPE_NIL) {
		arr->arrcnt--;
	}

	/* then actually update the value in the array */

	/* RBR idiom: retain first... */
	spn_value_retain(val);

	/* and release only then, in case the old and the new object is
	 * the same and the only reference to the object is held by the array.
	 * In that case, if we released it first, then it would be deallocated,
	 * and the retain function would operate on garbage...
	 */
	spn_value_release(&arr->arr[idx]);		
	arr->arr[idx] = *val;
}

static void expand_hash(SpnArray *arr)
{
	size_t newsz, i, oldsz = nthsize(arr->hashszidx);
	TList **new_buckets;

	/* we must perform a complete re-hash upon expansion,
	 * since the hash values depend on the size of the bucket array.
	 * 
	 * If the hash table reached its maximal capacity,
	 * it cannot be expanded further.
	 */
	if (++arr->hashszidx >= HASH_NSIZES) {
		/* error, array too large */
		fputs("Sparkling: requested array size is too large\n", stderr);
		abort();
	}

	/* expand the bucket vector */
	newsz = nthsize(arr->hashszidx);
	new_buckets = malloc(sizeof(new_buckets[0]) * newsz);
	if (new_buckets == NULL) {
		abort();
	}

	for (i = 0; i < newsz; i++) {
		new_buckets[i] = NULL;
	}

	/* and do a complete rehash */
	for (i = 0; i < oldsz; i++) {
		TList *list = arr->buckets[i];

		while (list != NULL) {
			TList *tmp;
			KVPair *pair = &list->pair;
			unsigned long hash = hash_key(&pair->key) % newsz;
			new_buckets[hash] = list_prepend(new_buckets[hash], &pair->key, &pair->val);

			/* free old list in-place */
			tmp = list->next;
			free(list);
			list = tmp;
		}
	}

	free(arr->buckets);
	arr->buckets = new_buckets;
}

static void insert_and_update_count_hash(SpnArray *arr, SpnValue *key, SpnValue *val)
{
	unsigned long hash, thash;
	KVPair *pair;

	/* if the hash table is empty, then it "contains nil values only"
	 * (i. e., for any key, the getter function will return nil).
	 * As a consequence, if the value to be inserted is nil, then
	 * we can just do nothing, else the hash part must be expanded
	 * in order it to be able to hold the first (non-nil) value.
	 */
	if (nthsize(arr->hashszidx) == 0) {
		if (val->t == SPN_TYPE_NIL) {
			return;
		}

		expand_hash(arr);
	}

	thash = hash_key(key);
	hash = thash % nthsize(arr->hashszidx);

	/* new element? */
	pair = list_find(arr->buckets[hash], key);
	if (pair == NULL) {
		/* element not yet in hash table */

		/* if this is a new element, then `val` is non-nil, so we
		 * increment the count of the hash part
		 * check if the load factor is greater than 1/2, and if so,
		 * double the size to minimize collisions
		 */
		if (val->t != SPN_TYPE_NIL) {
			/* check load factor */
			if (arr->hashcnt > nthsize(arr->hashszidx) / 2) {
				expand_hash(arr);
				/* the hash changes after resizing! */
				hash = thash % nthsize(arr->hashszidx);
			}
			
			/* increment hash count, retain key and value, then
			 * insert them at the beginning of the link list
			 */
			arr->hashcnt++;

			spn_value_retain(key);
			spn_value_retain(val);

			arr->buckets[hash] = list_prepend(arr->buckets[hash], key, val);
		}

		/* else: if the type of the new value is nil, and it wasn't
		 * found in the hash table either, then we're setting nil to nil
		 * -- that's effectively a no-op.
		 */
	} else {
		/* The key-value pair is already in the hash table. Check the
		 * value to be inserted for nil. If it is, then delete
		 * the key and the value and decrease the count.
		 * If it isn't nil, then just change the old *value*
		 * to the new one and do nothing with the key and count.
		 */
		if (val->t == SPN_TYPE_NIL) {
			arr->buckets[hash] = list_delete_releasing(arr->buckets[hash], key);
			arr->hashcnt--;
		} else {
			/* retain first, then release (RBR idiom) */
			spn_value_retain(val);
			spn_value_release(&pair->val);
			pair->val = *val;
		}
	}
}


/* 
 * The hash function
 * Depending on the platform, either of the table lookup method or
 * the checksum-style hash with Duff's device may be faster, choose
 * whichever fits the use case better (define the `SPN_HASH_TABLE'
 * macro at compile-time to use the lookup hash, else the SDBM hash
 * will be used).
 */
unsigned long spn_hash(const void *data, size_t n)
{
	unsigned long h = 0;
	const unsigned char *p = data;
	size_t i;

	if (n == 0) {
		return 0;
	}

#ifdef SPN_HASH_TABLE
	static const unsigned long tab[256] = {
		0xb57c1b8b, 0xaab5382f, 0x2998920e, 0xf8893702, 0xb8cfac71, 0xfb37512c, 0xa7e18e0a, 0x0610f8cb, 
		0x7f02a443, 0xb186acd3, 0x2f263eab, 0xd2e68bf5, 0xd79e9a3c, 0xd29eb02f, 0x57ee4d8c, 0x6fbcc68b, 
		0x5bb0b033, 0xc99b2363, 0x43cd2261, 0x66972dd8, 0xd9a19710, 0x89481888, 0xae9e111f, 0xe75fdff1, 
		0x45a86369, 0x9ebec462, 0xe0824c65, 0xc51268d2, 0xf8fb3852, 0xb16f4074, 0xa7533177, 0xcd10292b, 
		0xdd36db1b, 0x6beda83c, 0xf84e206d, 0xa5943a04, 0x41361c62, 0x06662635, 0x37140422, 0xca6b43fa, 
		0xd50b899b, 0x5abb6fbe, 0xadc84c1c, 0x33817a29, 0x73c8c05c, 0x9ede6deb, 0x46da465b, 0x843e42f5, 
		0x69aa43f5, 0x88ec84c5, 0x5d82b87e, 0x9f8ed521, 0xf97e02a5, 0x047186e2, 0xb3080783, 0xe13a011b, 
		0xf4c97e51, 0x9214eeca, 0xb1d69448, 0x9055d614, 0x78d48fa8, 0x0c730408, 0xe2cd3555, 0x61fcaa29, 
		0xce0765f7, 0xd91621a1, 0x6bf881e8, 0x6a8b849f, 0x169c5362, 0x23f9b77e, 0xe7d01cb3, 0xa5f32da1, 
		0x693ff835, 0x0f3f2ec0, 0x6bf60a6e, 0xe78883bc, 0x946b2edb, 0x746816ff, 0x2f5cfc84, 0xced88d46, 
		0x027b1573, 0xccf9177b, 0xbcbbdcbc, 0xa2c00cb2, 0xe5353c3a, 0xff7adc5e, 0xfc8af76f, 0x8a1f68f1,
		0xa7c65adc, 0x338559f1, 0xcd527c8e, 0x3c3c5686, 0x0c1b64a3, 0xce628906, 0x392de712, 0x7f41372b, 
		0x63938afc, 0xfd741da5, 0x4c84c032, 0xc4e2e4c4, 0x6940f7ea, 0xcc5251c9, 0xca041d4a, 0xd89ddcaf, 
		0x2f3575cb, 0x5165b857, 0x36afa51a, 0x4df5ac46, 0xdf8529e2, 0x36dfe7aa, 0x2621532e, 0x78786e57, 
		0x0312aa36, 0x12fc0224, 0x10a5cb9d, 0xa1f98e26, 0xd74ee906, 0xbd84dc5c, 0x2cf0f14f, 0xc771c4e0, 
		0x49993426, 0x322bb7a0, 0x106faec6, 0x8ebfecc7, 0x74486476, 0xe485de1d, 0x820b9864, 0x722fc355, 
		0x9a59ddee, 0x6e3657a9, 0x2518fffb, 0xe3ca492e, 0x27c94ace, 0x9ccbf5c6, 0xf10d31d9, 0x0f3c49e5, 
		0xa15ec356, 0x515f60e3, 0x4031138e, 0x254eb829, 0xd0f41b09, 0x9a49e2cf, 0x0425abf1, 0x35641212, 
		0xb7d9da82, 0xe4eec46e, 0xca10b2c4, 0x4ab4ea4a, 0x2bc157a5, 0xdd554601, 0x840b1322, 0x984f57bc, 
		0x391fe0ce, 0xb70853cc, 0x1fb1fff6, 0x518c8d9b, 0xd325c874, 0x2bccb8b6, 0x0e4e7c6d, 0x24f1104f, 
		0xd398db9a, 0xb7e745eb, 0x7e0bffdc, 0x24ca2a7c, 0x8b769e0d, 0xc8b357a5, 0xc6897c9d, 0x20596422, 
		0x6d5060c3, 0x1e52c314, 0xfee74d8d, 0x5b2c06b7, 0xb52e9e11, 0x68b898ce, 0xc3f1fc0c, 0xd20d00e3, 
		0x2f7aa9c5, 0xceea27d7, 0x6f49f8b1, 0xc0022b44, 0x31a199f7, 0x2cabcc1b, 0xfae3177c, 0x3b344c58, 
		0x23c9c15d, 0x0373a6d6, 0xf4a6b0e9, 0x8b8b0b01, 0x9065c1d2, 0xefe439dd, 0x9c2b64c9, 0xbf17ddd4, 
		0x5fef3add, 0x341f8fd5, 0x26b98700, 0xf4be4a09, 0x7b47ad66, 0x5e0c1af7, 0x105fe44c, 0x087d7c86, 
		0x34b852c3, 0x719c583c, 0x0de26b20, 0x81b2bf65, 0x16ab3d30, 0x44a87217, 0xa30f3b30, 0x46129ed5, 
		0x96a44e6e, 0x4dc4031a, 0x40862bd5, 0xa19afecf, 0x4948a5e6, 0xa6f8f646, 0xd34e4675, 0x98612067, 
		0x65e3ee05, 0x30f4d54a, 0xec5b628b, 0xa43adc46, 0xe56878d0, 0x0a2f9ea9, 0x320a114d, 0xe2ccf3a4, 
		0xdae6e153, 0x829d5ba7, 0xc6dbde5d, 0x638d18bc, 0x31ab7c4f, 0x49d0e4dd, 0x8fa12d8c, 0xeb3cc6cb, 
		0xbe626331, 0xe0b4e7b6, 0xbd8decc0, 0x6a086dde, 0x26a01710, 0xc9c23dad, 0x842fd955, 0xd8a09fa1, 
		0x59b0ccfe, 0xc5c389b4, 0xe6d553aa, 0x78133979, 0xccd873aa, 0xb6d50638, 0xf6e9c8d0, 0x2292193d, 
		0xd52a8762, 0xa0e3b73e, 0x42470889, 0x24ea177b, 0x40f280ea, 0xf92c9ebe, 0x638ce413, 0x4d1fc937
	};

	for (i = 0; i < n; i++) {
		h ^= tab[p[i] ^ h & 0xff];
	}
#else
	/* this is a variant of the SDBM hash */
	i = (n + 7) >> 3;
	switch (n & 7) {
	case 0: do {	h =  7159 * h + *p++;
	case 7:		h = 13577 * h + *p++;
	case 6:		h = 23893 * h + *p++;
	case 5:		h = 38791 * h + *p++;
	case 4:		h = 47819 * h + *p++;
	case 3:		h = 56543 * h + *p++;
	case 2:		h = 65587 * h + *p++;
	case 1:		h = 77681 * h + *p++;
		} while (--i);
	}
#endif

	return h;
}

static unsigned long hash_key(const SpnValue *key)
{
	switch (key->t) {
	case SPN_TYPE_NIL:	{ return 0;				}
	case SPN_TYPE_BOOL:	{ return !key->v.boolv; /* 0 or 1 */	}
	case SPN_TYPE_NUMBER:	{
		if (key->f & SPN_TFLG_FLOAT) {
			return key->v.fltv == (long)(key->v.fltv)
			     ? (unsigned long)(key->v.fltv)
			     : spn_hash(&key->v.fltv, sizeof(key->v.fltv));
		}

		/* the hash value of an integer is itself */
		return key->v.intv;
	}
	case SPN_TYPE_FUNC:	{
		/* to understand why hashing is done as it is done, see the
		 * notice about function equality above `function_equal()`
		 * in src/spn.c
		 *
		 * if a function is a pending stub but it has no name, then:
		 * 1. that doesn't make sense and it should not happen;
		 * 2. it's impossible to decide whether it's equal to some
		 * other function.
		 */
		assert(key->v.fnv.name != NULL || (key->f & SPN_TFLG_PENDING) == 0);

		/* see http://stackoverflow.com/q/18282032 */
		if (key->f & SPN_TFLG_NATIVE) {
			return spn_hash(&key->v.fnv.r.fn, sizeof(key->v.fnv.r.fn));
		}

		return key->v.fnv.name == NULL
		     ? (unsigned long)(key->v.fnv.r.bc)
		     : spn_hash(key->v.fnv.name, strlen(key->v.fnv.name));
	}
	case SPN_TYPE_STRING:
	case SPN_TYPE_ARRAY:	{
		SpnObject *obj = key->v.ptrv;
		unsigned long (*hashfn)(void *) = obj->isa->hashfn;
		return hashfn != NULL ? hashfn(obj) : (unsigned long)(obj);
	}
	case SPN_TYPE_USRDAT:	{
		if (key->f & SPN_TFLG_OBJECT) {
			SpnObject *obj = key->v.ptrv;
			unsigned long (*hashfn)(void *) = obj->isa->hashfn;
			return hashfn != NULL ? hashfn(obj) : (unsigned long)(obj);			
		}

		return (unsigned long)(key->v.ptrv);
	}
#ifndef NDEBUG
	default:
		fprintf(stderr, "Sparkling: wrong type ID `%d' in array key\n", key->t);
		abort();
#endif
	}

	return 0;
}

