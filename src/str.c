/*
 * str.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Object-oriented wrapper for C strings
 */

#include <string.h>

#include "str.h"
#include "array.h"

static int compare_strings(const void *l, const void *r);
static int equal_strings(const void *l, const void *r);
static void free_string(void *obj);
static unsigned long hash_string(void *obj);

static const SpnClass spn_class_string = {
	"string",
	sizeof(SpnString),
	equal_strings,
	compare_strings,
	hash_string,
	free_string
};

static void free_string(void *obj)
{
	SpnString *str = obj;
	if (str->dealloc) {
		free(str->cstr);
	}

	free(str);
}

static int compare_strings(const void *l, const void *r)
{
	const SpnString *lo = l, *ro = r;
	return strcmp(lo->cstr, ro->cstr);
}

static int equal_strings(const void *l, const void *r)
{
	const SpnString *lo = l, *ro = r;	
	return strcmp(lo->cstr, ro->cstr) == 0;
}

/* since strings are immutable, it's enough to generate the hash on-demand,
 * then store it for later use.
 */
static unsigned long hash_string(void *obj)
{
	SpnString *str = obj;

	if (!str->ishashed) {
		str->hash = spn_hash(str->cstr, str->len);
		str->ishashed = 1;
	}

	return str->hash;
}


SpnString *spn_string_new(const char *cstr)
{
	return spn_string_new_len(cstr, strlen(cstr));
}

SpnString *spn_string_new_nocopy(const char *cstr, int dealloc)
{
	return spn_string_new_nocopy_len(cstr, strlen(cstr), dealloc);
}

SpnString *spn_string_new_len(const char *cstr, size_t len)
{
	char *buf = malloc(len + 1);
	if (buf == NULL) {
		abort();
	}

	memcpy(buf, cstr, len); /* so that strings can hold binary data */
	buf[len] = 0;

	return spn_string_new_nocopy_len(buf, len, 1);
}

SpnString *spn_string_new_nocopy_len(const char *cstr, size_t len, int dealloc)
{
	SpnString *str = spn_object_new(&spn_class_string);

	str->dealloc = dealloc;
	str->len = len;
	str->cstr = (char *)(cstr);
	str->ishashed = 0;

	return str;
}

SpnString *spn_string_concat(SpnString *lhs, SpnString *rhs)
{
	size_t len = lhs->len + rhs->len;

	char *buf = malloc(len + 1);
	if (buf == NULL) {
		abort();
	}

	strcpy(buf, lhs->cstr);
	strcpy(buf + lhs->len, rhs->cstr);

	return spn_string_new_nocopy_len(buf, len, 1);
}

/* TODO: implement this */
char *spn_string_format(const char *fmt, int argc, SpnValue *argv)
{
	return NULL;
}

