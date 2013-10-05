/*
 * spn.c
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

#include "spn.h"
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
		    || val->t == SPN_TYPE_USRDAT);
		
		spn_object_retain(val->v.ptrv);
	}
}

void spn_value_release(SpnValue *val)
{
	if (val->f & SPN_TFLG_OBJECT) {
		assert(val->t == SPN_TYPE_STRING
		    || val->t == SPN_TYPE_ARRAY
		    || val->t == SPN_TYPE_USRDAT);

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
	
	/* if both are native, then they must point to the same function */
	if (lhs->f & SPN_TFLG_NATIVE) {
		return lhs->v.fnv.r.fn == rhs->v.fnv.r.fn;
	}

	/* if both are script functions, then they must either have the same
	 * name to be equal, or they must point to the same lambda function
	 */
	if (lhs->v.fnv.name != NULL && rhs->v.fnv.name != NULL) {
		return strcmp(lhs->v.fnv.name, rhs->v.fnv.name) == 0;
	} else if (lhs->v.fnv.name == NULL && rhs->v.fnv.name == NULL) {
		/* an unnamed stub is nonsense (it's impossible to resolve) */
		assert((lhs->f & SPN_TFLG_PENDING) == 0);
		assert((rhs->f & SPN_TFLG_PENDING) == 0);
		
		return lhs->v.fnv.r.bc == rhs->v.fnv.r.bc;
	} else {
		/* if one of them has a name but the other one hasn't, then
		 * they cannot possibly be equal
		 */
		return 0;
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
	case SPN_TYPE_NIL:	{ return 1; /* nil can only be nil */		}
	case SPN_TYPE_BOOL:	{ return !lhs->v.boolv == !rhs->v.boolv;	}
	case SPN_TYPE_NUMBER:	{ return numeric_equal(lhs, rhs);		}
	case SPN_TYPE_FUNC:	{ return function_equal(lhs, rhs);		}
	case SPN_TYPE_STRING:
	case SPN_TYPE_ARRAY:	{
		return spn_object_equal(lhs->v.ptrv, rhs->v.ptrv);
	}
	case SPN_TYPE_USRDAT:	{
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

void spn_value_print(const SpnValue *val)
{
	switch (val->t) {
	case SPN_TYPE_NIL:
		printf("nil");
		break;
	case SPN_TYPE_BOOL:
		printf("%s", val->v.boolv ? "true" : "false");
		break;
	case SPN_TYPE_NUMBER:
		if (val->f & SPN_TFLG_FLOAT) {
			printf("%.*g", DBL_DIG, val->v.fltv);
		} else {
			printf("%ld", val->v.intv);
		}
		
		break;
	case SPN_TYPE_FUNC: {
		const char *name = val->v.fnv.name ? val->v.fnv.name : SPN_LAMBDA_NAME;
		
		if (val->f & SPN_TFLG_NATIVE) {
			printf("<native function %s()>", name);
		} else {
			const void *ptr = val->v.fnv.r.bc;
			printf("<script function %s() %p>", name, ptr);
		}
		
		break;
	}
	case SPN_TYPE_STRING: {
		SpnString *s = val->v.ptrv;
		printf("%s", s->cstr);
		break;
	}
	case SPN_TYPE_ARRAY: {
		printf("<array %p>", val->v.ptrv);
		break;
	}
	case SPN_TYPE_USRDAT:
		printf("<userdata %p>", val->v.ptrv);
		break;
	default:
		fprintf(stderr, "wrong type ID `%d' in print_val\n", val->t);
		SHANT_BE_REACHED();
		break;
	}
}


static char *read_file2mem(const char *name, size_t *sz, int nulterm)
{
	long n;
	size_t len;
	char *buf;
	FILE *f;
	
	f = fopen(name, "r");
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

