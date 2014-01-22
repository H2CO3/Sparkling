/*
 * api.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Public parts of the Sparkling API
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <float.h>
#include <assert.h>

#include "api.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "private.h"


/* 
 * Value API
 */

void spn_value_retain(SpnValue *val)
{
	if (val->f & SPN_TFLG_OBJECT) {
		assert(val->t == SPN_TYPE_STRING
		    || val->t == SPN_TYPE_ARRAY
		    || val->t == SPN_TYPE_USERINFO);

		spn_object_retain(val->v.ptrv);
	}
}

void spn_value_release(SpnValue *val)
{
	if (val->f & SPN_TFLG_OBJECT) {
		assert(val->t == SPN_TYPE_STRING
		    || val->t == SPN_TYPE_ARRAY
		    || val->t == SPN_TYPE_USERINFO);

		spn_object_release(val->v.ptrv);
	}
}


static int numeric_equal(const SpnValue *lhs, const SpnValue *rhs)
{
	assert(lhs->t == SPN_TYPE_NUMBER && rhs->t == SPN_TYPE_NUMBER);

	if (lhs->f & SPN_TFLG_FLOAT) {
		return rhs->f & SPN_TFLG_FLOAT
		     ? lhs->v.fltv == rhs->v.fltv
		     : lhs->v.fltv == rhs->v.intv;
	} else {
		return rhs->f & SPN_TFLG_FLOAT
		     ? lhs->v.intv == rhs->v.fltv
		     : lhs->v.intv == rhs->v.intv;
	}
}

/* functions are considered equal if either their names are not
 * NULL (i. e. none of them are closures) and the names are the
 * same, or their names are both NULL (both functions are
 * lambdas) and they point to the same entry point inside
 * the bytecode.
 */
static int function_equal(const SpnValue *lhs, const SpnValue *rhs)
{
	assert(lhs->t == SPN_TYPE_FUNC && rhs->t == SPN_TYPE_FUNC);

	/* a native function cannot be the same as a script function */
	if ((lhs->f & SPN_TFLG_NATIVE) != (rhs->f & SPN_TFLG_NATIVE)) {
		return 0;
	}

	/* if they are equal, they must point to the same function */
	if (lhs->f & SPN_TFLG_NATIVE) {
		return lhs->v.fnv.r.fn == rhs->v.fnv.r.fn;
	} else {
		return lhs->v.fnv.r.bc == rhs->v.fnv.r.bc;
	}
}

int spn_value_equal(const SpnValue *lhs, const SpnValue *rhs)
{
	/* first, make sure that we compare values of the same type
	 * (values of different types cannot possibly be equal)
	 */
	if (lhs->t != rhs->t) {
		return 0;
	}

	switch (lhs->t) {
	case SPN_TYPE_NIL:	{ return 1; /* nil can only be nil */	}
	case SPN_TYPE_BOOL:	{ return lhs->v.boolv == rhs->v.boolv;	}
	case SPN_TYPE_NUMBER:	{ return numeric_equal(lhs, rhs);	}
	case SPN_TYPE_FUNC:	{ return function_equal(lhs, rhs);	}
	case SPN_TYPE_STRING:
	case SPN_TYPE_ARRAY:	{
		return spn_object_equal(lhs->v.ptrv, rhs->v.ptrv);
	}
	case SPN_TYPE_USERINFO:	{
		if ((lhs->f & SPN_TFLG_OBJECT) != (rhs->f & SPN_TFLG_OBJECT)) {
			return 0;
		}

		return lhs->f & SPN_TFLG_OBJECT
		     ? spn_object_equal(lhs->v.ptrv, rhs->v.ptrv)
		     : lhs->v.ptrv == rhs->v.ptrv;
	}
	default:
		SHANT_BE_REACHED();
	}

	return 0;
}

int spn_value_noteq(const SpnValue *lhs, const SpnValue *rhs)
{
	return !spn_value_equal(lhs, rhs);
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

unsigned long spn_hash_value(const SpnValue *key)
{
	switch (key->t) {
	case SPN_TYPE_NIL:	{ return 0;				}
	case SPN_TYPE_BOOL:	{ return key->v.boolv; /* 0 or 1 */	}
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
		} else {
			return (unsigned long)(key->v.fnv.r.bc);
		}
	}
	case SPN_TYPE_STRING:
	case SPN_TYPE_ARRAY:	{
		SpnObject *obj = key->v.ptrv;
		unsigned long (*hashfn)(void *) = obj->isa->hashfn;
		return hashfn != NULL ? hashfn(obj) : (unsigned long)(obj);
	}
	case SPN_TYPE_USERINFO:	{
		if (key->f & SPN_TFLG_OBJECT) {
			SpnObject *obj = key->v.ptrv;
			unsigned long (*hashfn)(void *) = obj->isa->hashfn;
			return hashfn != NULL ? hashfn(obj) : (unsigned long)(obj);			
		}

		return (unsigned long)(key->v.ptrv);
	}
	default:
		SHANT_BE_REACHED();
	}

	return 0;
}

void spn_value_print(const SpnValue *val)
{
	switch (val->t) {
	case SPN_TYPE_NIL:	{
		fputs("nil", stdout);
		break;
	}
	case SPN_TYPE_BOOL:	{
		fputs(val->v.boolv ? "true" : "false", stdout);
		break;
	}
	case SPN_TYPE_NUMBER:	{
		if (val->f & SPN_TFLG_FLOAT) {
			printf("%.*g", DBL_DIG, val->v.fltv);
		} else {
			printf("%ld", val->v.intv);
		}

		break;
	}
	case SPN_TYPE_FUNC:	{
		const char *name = val->v.fnv.name ? val->v.fnv.name : SPN_LAMBDA_NAME;

		if (val->f & SPN_TFLG_NATIVE) {
			printf("<native function %s>", name);
		} else {
			const void *ptr = val->v.fnv.r.bc;
			printf("<script function %p: %s>", ptr, name);
		}

		break;
	}
	case SPN_TYPE_STRING:	{
		SpnString *s = val->v.ptrv;
		fputs(s->cstr, stdout);
		break;
	}
	case SPN_TYPE_ARRAY:	{
		printf("<array %p>", val->v.ptrv);
		break;
	}
	case SPN_TYPE_USERINFO:	{
		printf("<userinfo %p>", val->v.ptrv);
		break;
	}
	default:
		SHANT_BE_REACHED();
		break;
	}
}

const char *spn_type_name(enum spn_val_type type)
{
	/* XXX: black magic, relies on the order of enum members */
	static const char *const typenames[] = {
		"nil",
		"bool",
		"number",
		"function",
		"string",
		"array",
		"userinfo"
	};

	return typenames[type];
}


/*
 * Object API
 */

int spn_object_equal(const void *lhs, const void *rhs)
{
	const SpnObject *lo = lhs, *ro = rhs;

	if (lo->isa != ro->isa) {
		return 0;
	}

	if (lo->isa->equal != NULL) {
		return lo->isa->equal(lo, ro);
	} else {
		return lo == ro;
	}
}

int spn_object_cmp(const void *lhs, const void *rhs)
{
	const SpnObject *lo = lhs, *ro = rhs;

	assert(lo->isa == ro->isa);
	assert(lo->isa->compare != NULL);

	return lo->isa->compare(lo, ro);
}

void *spn_object_new(const SpnClass *isa)
{
	SpnObject *obj = malloc(isa->instsz);
	if (obj == NULL) {
		abort();
	}

	obj->isa = isa;
	obj->refcnt = 1;

	return obj;
}

void spn_object_retain(void *o)
{
	SpnObject *obj = o;
	obj->refcnt++;
}

void spn_object_release(void *o)
{
	SpnObject *obj = o;

	/* it is the destructor's responsibility to `free()` the instance */
	if (--obj->refcnt == 0) {
		obj->isa->destructor(obj);
	}
}


/*
 * File access API
 */

static char *read_file2mem(const char *name, size_t *sz, int nulterm)
{
	long n;
	size_t len;
	char *buf;
	FILE *f;

	f = fopen(name, "rb");
	if (f == NULL) {
		return NULL;
	}

	if (fseek(f, 0, SEEK_END) < 0) {
		fclose(f);
		return NULL;
	}

	n = ftell(f);
	if (n < 0) {
		fclose(f);
		return NULL;
	}

	/* don't get confused by empty text files */
	if (n == 0 && nulterm) {
		fclose(f);

		buf = malloc(1);
		if (buf == NULL) {
			return NULL;
		}

		buf[0] = 0;
		return buf;
	}

	if (fseek(f, 0, SEEK_SET) < 0) {
		fclose(f);
		return NULL;
	}

	len = nulterm ? n + 1 : n;
	buf = malloc(len);
	if (buf == NULL) {
		fclose(f);
		return NULL;
	}

	if (fread(buf, n, 1, f) < 1) {
		fclose(f);
		free(buf);
		return NULL;
	}

	fclose(f);

	if (nulterm) {
		buf[n] = 0;
	}

	if (sz != NULL) {
		*sz = n;
	}

	return buf;
}


char *spn_read_text_file(const char *name)
{
	return read_file2mem(name, NULL, 1);
}

void *spn_read_binary_file(const char *name, size_t *sz)
{
	return read_file2mem(name, sz, 0);
}

