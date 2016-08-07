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
#include "misc.h"
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

char *spn_object_description(SpnObject *obj, int debug)
{
	char *str;

	if (obj->isa->description) {
		return obj->isa->description(obj, debug);
	}

	/* for classes that don't implement 'description()', use a default format:
	 * '<' + class name + ' ' + "0x" + pointer value in hex + '>' + '\0'
	 * an overly generous size of CHAR_BIT * sizeof(size_t) is used for the size of
	 * the hexadecimal representation of the pointer because I don't want to
	 * think about dividing by 4 (bits per hex digit) and worry about correctly
	 * rounding it up and potentially having a buffer overflow error.
	 * Oh, and let's hope sizeof(unsigned long) >= sizeof(SpnObject *)...
	 */
	assert(sizeof(unsigned long) >= sizeof(obj));
	str = spn_malloc(1 + strlen(obj->isa->name) + 1 + 2 + sizeof(unsigned long) * CHAR_BIT + 1 + 1);

	sprintf(
		str,
		"<%s 0x%0*lx>",
		obj->isa->name,
		(int)(sizeof(unsigned long) * CHAR_BIT + 3) / 4, /* 4 bits per hex digit, rounded up */
		(unsigned long)(obj)
	);

	return str;
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

SpnValue spn_makerawptr(void *p)
{
	SpnValue ret;
	ret.type = SPN_TYPE_RAWPTR;
	ret.v.p = p;
	return ret;
}

SpnValue spn_makeobject(void *o)
{
	SpnValue ret;
	ret.type = SPN_TYPE_OBJECT;
	ret.v.o = o;
	return ret;
}

const SpnValue spn_nilval   = { SPN_TYPE_NIL,  { 0 } };
const SpnValue spn_falseval = { SPN_TYPE_BOOL, { 0 } };
const SpnValue spn_trueval  = { SPN_TYPE_BOOL, { 1 } };


void spn_value_retain(const SpnValue *val)
{
	if (isobject(val)) {
		spn_object_retain(objvalue(val));
	}
}

void spn_value_release(const SpnValue *val)
{
	if (isobject(val)) {
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
	case SPN_TTAG_RAWPTR: { return ptrvalue(lhs) == ptrvalue(rhs); }
	case SPN_TTAG_OBJECT: { return spn_object_equal(objvalue(lhs), objvalue(rhs)); }

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
	assert(isobject(lhs) && isobject(rhs));
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
	case SPN_TTAG_RAWPTR: {
		return (unsigned long)(ptrvalue(key));
	}
	case SPN_TTAG_OBJECT: {
		SpnObject *obj = objvalue(key);
		unsigned long (*hashfn)(void *) = obj->isa->hashfn;
		return hashfn ? hashfn(obj) : (unsigned long)(obj);
	}
	default:
		SHANT_BE_REACHED();
	}

	return 0;
}

/* Helpers for printing values */

static void print_indent(FILE *stream, int level)
{
	int i;
	for (i = 0; i < level; i++) {
		fprintf(stream, "    ");
	}
}

static void print_array(FILE *stream, SpnArray *array, int level);
static void print_hashmap(FILE *stream, SpnHashMap *hm, int level);

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

/* Actually printing values */

static void spn_value_print_internal(FILE *stream, const SpnValue *val, int debug)
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
	case SPN_TTAG_RAWPTR: {
		fprintf(stream, "<pointer %p>", ptrvalue(val));
		break;
	}
	case SPN_TTAG_OBJECT: {
		/* TODO: do not special-case arrays and hashmaps */
		switch (classuid(val)) {
		case SPN_CLASS_UID_ARRAY: {
			print_array(stream, arrayvalue(val), 0);
			break;
		}
		case SPN_CLASS_UID_HASHMAP: {
			print_hashmap(stream, hashmapvalue(val), 0);
			break;
		}
		default:
			{
				char *description = spn_object_description(objvalue(val), debug);
				fputs(description, stream);
				free(description);
				break;
			}
		}

		break;
	}
	default:
		SHANT_BE_REACHED();
		break;
	}
}

void spn_value_print(FILE *stream, const SpnValue *val)
{
	spn_value_print_internal(stream, val, 0 /* false, do not debug */);
}

void spn_debug_print(FILE *stream, const SpnValue *val)
{
	spn_value_print_internal(stream, val, 1 /* true, debug */);
}

void spn_repl_print(const SpnValue *val)
{
	/* only print strings in debug format by default */
	spn_value_print_internal(stdout, val, isstring(val));
}

const char *spn_typetag_name(int ttag)
{
	/* XXX: black magic, relies on the order of enum members */
	static const char *const typenames[] = {
		"nil",
		"bool",
		"number",
		"pointer",
		"object",
	};

	assert(ttag < COUNT(typenames));
	return typenames[ttag];
}

const char *spn_value_type_name(const SpnValue *val)
{
	/* For objects, return the class name */
	if (isobject(val)) {
		return classname(val);
	}

	/* For primitives, just do a table lookup */
	return spn_typetag_name(valtype(val));
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
