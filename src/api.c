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


/*  The hash function is a variant of the SDBM hash */
unsigned long spn_hash(const void *data, size_t n)
{
	unsigned long h = 0;
	const unsigned char *p = data;
	size_t i = (n + 7) / 8;

	if (n == 0) {
		return 0;
	}

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
	SpnObject *obj = spn_malloc(isa->instsz);

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

