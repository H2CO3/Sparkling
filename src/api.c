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
#include "str.h"
#include "vm.h"
#include "private.h"
#include "array.h"
#include "hashmap.h"
#include "func.h"

/*
 * Object API
 */

static int class_equal(const SpnClass *clsA, const SpnClass *clsB)
{
	return clsA->UID == clsB->UID;
}

int spn_object_member_of_class(void *obj, const SpnClass *cls)
{
	SpnObject *object = obj;
	return class_equal(object->isa, cls);
}

int spn_object_equal(void *lp, void *rp)
{
	SpnObject *lhs = lp, *rhs = rp;

	if (!class_equal(lhs->isa, rhs->isa)) {
		return 0;
	}

	if (lhs->isa->equal != NULL) {
		return lhs->isa->equal(lhs, rhs);
	} else {
		return lhs == rhs;
	}
}

int spn_object_cmp(void *lp, void *rp)
{
	SpnObject *lhs = lp, *rhs = rp;

	assert(class_equal(lhs->isa, rhs->isa));
	assert(lhs->isa->compare != NULL);

	return lhs->isa->compare(lhs, rhs);
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
	if (--obj->refcnt == 0) {
		if (obj->isa->destructor) {
			obj->isa->destructor(obj);
		}

		free(obj);
	}
}

/*
 * Value API
 */
long spn_intvalue_f(SpnValue *val)
{
	if (spn_isint(val)) {
		return spn_intvalue(val);
	}

	return spn_floatvalue(val);
}

double spn_floatvalue_f(SpnValue *val)
{
	if (spn_isfloat(val)) {
		return spn_floatvalue(val);
	}

	return spn_intvalue(val);
}

SpnValue spn_makebool(int b)
{
	SpnValue ret;
	ret.type = SPN_TYPE_BOOL;
	ret.v.b = b;
	return ret;
}

SpnValue spn_makeint(long i)
{
	SpnValue ret;
	ret.type = SPN_TYPE_INT;
	ret.v.i = i;
	return ret;
}

SpnValue spn_makefloat(double f)
{
	SpnValue ret;
	ret.type = SPN_TYPE_FLOAT;
	ret.v.f = f;
	return ret;
}

SpnValue spn_makeweakuserinfo(void *p)
{
	SpnValue ret;
	ret.type = SPN_TYPE_WEAKUSERINFO;
	ret.v.p = p;
	return ret;
}

SpnValue spn_makestrguserinfo(void *o)
{
	SpnValue ret;
	ret.type = SPN_TYPE_STRGUSERINFO;
	ret.v.o = o;
	return ret;
}

const SpnValue spn_nilval   = { SPN_TYPE_NIL,  { 0 } };
const SpnValue spn_falseval = { SPN_TYPE_BOOL, { 0 } };
const SpnValue spn_trueval  = { SPN_TYPE_BOOL, { 1 } };


void spn_value_retain(const SpnValue *val)
{
	if (isobject(val)) {
		assert(isstring(val) || isarray(val)
		   || ishashmap(val) || isfunc(val)
		   || isuserinfo(val));

		spn_object_retain(objvalue(val));
	}
}

void spn_value_release(const SpnValue *val)
{
	if (isobject(val)) {
		assert(isstring(val) || isarray(val)
		   || ishashmap(val) || isfunc(val)
		   || isuserinfo(val));

		spn_object_release(objvalue(val));
	}
}


static int numeric_equal(const SpnValue *lhs, const SpnValue *rhs)
{
	assert(isnum(lhs) && isnum(rhs));

	if (isfloat(lhs)) {
		return isfloat(rhs) ? floatvalue(lhs) == floatvalue(rhs)
		                    : floatvalue(lhs) == intvalue(rhs);
	} else {
		return isfloat(rhs) ? intvalue(lhs) == floatvalue(rhs)
		                    : intvalue(lhs) == intvalue(rhs);
	}
}


int spn_value_equal(const SpnValue *lhs, const SpnValue *rhs)
{
	/* first, make sure that we compare values of the same type
	 * (values of different types cannot possibly be equal)
	 */
	if (valtype(lhs) != valtype(rhs)) {
		return 0;
	}

	switch (valtype(lhs)) {
	case SPN_TTAG_NIL:    { return 1; /* nil can only be nil */ }
	case SPN_TTAG_BOOL:   { return boolvalue(lhs) == boolvalue(rhs); }
	case SPN_TTAG_NUMBER: { return numeric_equal(lhs, rhs); }

	case SPN_TTAG_STRING:
	case SPN_TTAG_ARRAY:
	case SPN_TTAG_HASHMAP:
	case SPN_TTAG_FUNC: {
		return spn_object_equal(objvalue(lhs), objvalue(rhs));
	}

	case SPN_TTAG_USERINFO: {
		/* an object can not equal a non-object */
		if (isobject(lhs) != isobject(rhs)) {
			return 0;
		}

		if (isobject(lhs)) {
			return spn_object_equal(objvalue(lhs), objvalue(rhs));
		} else {
			return ptrvalue(lhs) == ptrvalue(rhs);
		}
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


/* Functions for performing ordered comparison */

static int numeric_compare(const SpnValue *lhs, const SpnValue *rhs)
{
	assert(isnum(lhs) && isnum(rhs));

	if (isfloat(lhs)) {
		if (isfloat(rhs)) {
			return floatvalue(lhs) < floatvalue(rhs) ? -1
			     : floatvalue(lhs) > floatvalue(rhs) ? +1
			     :                                      0;
		} else {
			return floatvalue(lhs) < intvalue(rhs) ? -1
			     : floatvalue(lhs) > intvalue(rhs) ? +1
			     :                                    0;
		}
	} else {
		if (isfloat(rhs)) {
			return intvalue(lhs) < floatvalue(rhs) ? -1
			     : intvalue(lhs) > floatvalue(rhs) ? +1
			     :                                    0;
		} else {
			return intvalue(lhs) < intvalue(rhs) ? -1
			     : intvalue(lhs) > intvalue(rhs) ? +1
			     :                                  0;
		}
	}
}

int spn_value_compare(const SpnValue *lhs, const SpnValue *rhs)
{
	if (isnum(lhs) && isnum(rhs)) {
		return numeric_compare(lhs, rhs);
	}

	/* else assume comparable objects */
	return spn_object_cmp(objvalue(lhs), objvalue(rhs));
}

int spn_values_comparable(const SpnValue *lhs, const SpnValue *rhs)
{
	if (isnum(lhs) && isnum(rhs)) {
		return 1;
	}

	if (isobject(lhs) && isobject(rhs)) {
		SpnObject *obl = objvalue(lhs), *obr = objvalue(rhs);
		if (!class_equal(obl->isa, obr->isa)) {
			return 0;
		}

		return obl->isa->compare != NULL;
	}

	return 0;
}

/* The hash function is the SMDB hash */
unsigned long spn_hash_bytes(const void *data, size_t n)
{
	unsigned long hash = 0;
	const unsigned char *p = data;

	while (n--) {
		hash *= 65599;
		hash += *p++;
	}

	return hash;
}

unsigned long spn_hash_value(const SpnValue *key)
{
	switch (valtype(key)) {
	case SPN_TTAG_NIL:	{ return 0; }
	case SPN_TTAG_BOOL:	{ return boolvalue(key); /* 0 or 1 */ }
	case SPN_TTAG_NUMBER: {
		if (isfloat(key)) {
			double f = floatvalue(key);

			/* only test for integer if it fits into one (anti-UB) */
			if (LONG_MIN <= f && f <= LONG_MAX) {
				long i = f; /* truncate */

				if (f == i) {
					/* it's really an integer.
					 * This takes care of the +/- 0 problem too
					 * (since 0 itself is an integer)
					 */
					return i;
				}
			} else {
				return spn_hash_bytes(&f, sizeof f);
			}
		}

		/* the hash value of an integer is itself */
		return intvalue(key);
	}
	case SPN_TTAG_STRING:
	case SPN_TTAG_ARRAY:
	case SPN_TTAG_HASHMAP:
	case SPN_TTAG_FUNC: {
		SpnObject *obj = objvalue(key);
		unsigned long (*hashfn)(void *) = obj->isa->hashfn;
		return hashfn ? hashfn(obj) : (unsigned long)(obj);
	}
	case SPN_TTAG_USERINFO: {
		if (isobject(key)) {
			SpnObject *obj = objvalue(key);
			unsigned long (*hashfn)(void *) = obj->isa->hashfn;
			return hashfn ? hashfn(obj) : (unsigned long)(obj);
		}

		return (unsigned long)(ptrvalue(key));
	}
	default:
		SHANT_BE_REACHED();
	}

	return 0;
}

static void print_array(FILE *stream, SpnArray *array, int level);
static void print_hashmap(FILE *stream, SpnHashMap *hm, int level);

static void print_indent(FILE *stream, int level)
{
	int i;
	for (i = 0; i < level; i++) {
		fprintf(stream, "    ");
	}
}

static void inner_aux_print(FILE *stream, const SpnValue *val, int level)
{
	if (isarray(val)) {
		print_array(stream, arrayvalue(val), level);
	} else if (ishashmap(val)) {
		print_hashmap(stream, hashmapvalue(val), level);
	} else {
		spn_debug_print(stream, val);
	}
}

static void print_array(FILE *stream, SpnArray *array, int level)
{
	size_t i;
	size_t n = spn_array_count(array);

	fprintf(stream, "[\n");

	for (i = 0; i < n; i++) {
		SpnValue val = spn_array_get(array, i);

		print_indent(stream, level + 1);
		inner_aux_print(stream, &val, level + 1);
		fprintf(stream, "\n");
	}

	print_indent(stream, level);
	fprintf(stream, "]");
}

static void print_hashmap(FILE *stream, SpnHashMap *hm, int level)
{
	SpnValue key, val;
	size_t i = 0;

	fprintf(stream, "{\n");

	while ((i = spn_hashmap_next(hm, i, &key, &val)) != 0) {
		print_indent(stream, level + 1);

		inner_aux_print(stream, &key, level + 1);
		fprintf(stream, ": ");
		inner_aux_print(stream, &val, level + 1);
		fprintf(stream, "\n");
	}

	print_indent(stream, level);
	fprintf(stream, "}");
}

void spn_value_print(FILE *stream, const SpnValue *val)
{
	switch (valtype(val)) {
	case SPN_TTAG_NIL: {
		fputs("nil", stream);
		break;
	}
	case SPN_TTAG_BOOL: {
		fputs(boolvalue(val) ? "true" : "false", stream);
		break;
	}
	case SPN_TTAG_NUMBER: {
		if (isfloat(val)) {
			fprintf(stream, "%.*g", DBL_DIG, floatvalue(val));
		} else {
			fprintf(stream, "%ld", intvalue(val));
		}

		break;
	}
	case SPN_TTAG_STRING: {
		SpnString *s = stringvalue(val);
		fputs(s->cstr, stream);
		break;
	}
	case SPN_TTAG_ARRAY: {
		SpnArray *array = objvalue(val);
		print_array(stream, array, 0);
		break;
	}
	case SPN_TTAG_HASHMAP: {
		SpnHashMap *hashmap = objvalue(val);
		print_hashmap(stream, hashmap, 0);
		break;
	}
	case SPN_TTAG_FUNC: {
		SpnFunction *func = funcvalue(val);
		void *p;

		if (func->native) {
			p = (void *)(ptrdiff_t)(func->repr.fn);
		} else {
			p = func->repr.bc;
		}

		fprintf(stream, "<function %p>", p);
		break;
	}
	case SPN_TTAG_USERINFO: {
		void *ptr = isobject(val) ? objvalue(val) : ptrvalue(val);
		fprintf(stream, "<userinfo %p>", ptr);
		break;
	}
	default:
		SHANT_BE_REACHED();
		break;
	}
}

void spn_debug_print(FILE *stream, const SpnValue *val)
{
	switch (valtype(val)) {
	case SPN_TTAG_STRING:
		/* TODO: do proper escaping */
		fprintf(stream, "\"");
		spn_value_print(stream, val);
		fprintf(stream, "\"");
		break;
	case SPN_TTAG_ARRAY:
		fprintf(stream, "<array %p>", objvalue(val));
		break;
	case SPN_TTAG_HASHMAP:
		fprintf(stream, "<hashmap %p>", objvalue(val));
		break;
	default:
		spn_value_print(stream, val);
		break;
	}
}

void spn_repl_print(const SpnValue *val)
{
	switch (valtype(val)) {
	case SPN_TTAG_STRING:
		spn_debug_print(stdout, val);
		break;
	default:
		spn_value_print(stdout, val);
		break;
	}
}

const char *spn_type_name(int type)
{
	/* XXX: black magic, relies on the order of enum members */
	static const char *const typenames[] = {
		"nil",
		"bool",
		"number",
		"string",
		"array",
		"hashmap",
		"function",
		"userinfo"
	};

	return typenames[typetag(type)];
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
