/*
 * jspn.c
 * JavaScript bindings for Sparkling
 * Created by Árpád Goretity on 06/04/2014.
 *
 * Licensed under the 2-clause BSD License
 */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "api.h"
#include "ctx.h"
#include "private.h"

#define NIL_INDEX (-1.0)
#define ERR_INDEX (-2.0)


static SpnContext *get_global_context(void)
{
	static SpnContext *ctx = NULL;

	if (ctx == NULL) {
		ctx = spn_ctx_new();
	}

	return ctx;
}

static SpnArray *get_global_values(void)
{
	static SpnArray *values = NULL;

	if (values == NULL) {
		values = spn_array_new();
	}

	return values;
}

static double rndfloat(void)
{
	static seed = 1;

	if (seed) {
		srand(time(NULL));
		seed = 0;
	}

	return rand() * 1.0 / RAND_MAX;
}


static double add_to_global_values(const SpnValue *val)
{
	SpnArray *values = get_global_values();
	static double offset = 0.0;
	int counter = 0;
	double x;
	SpnValue key, aux;

	/* nil values need special treatment: we can't simply add
	 * nil to the array because its slot wouldn't appear to
	 * be used. So, then it could be overwritten with something
	 * else, and then we would find that a nil returned by a
	 * function "magically" transformed into something else.
	 * So, if the value is a nil, we return a special index
	 * which indicates that the value was nil.
	 */
	if (isnil(val)) {
		return NIL_INDEX;
	}

	/* randomly look for a key that is not yet used.
	 * Retry at most 5 times; if that fails, assume that the
	 * region with the current offset is full, so increase
	 * the offset and try again.
	 *
	 * This algo is nice because eventually it will be able to
	 * find an appropriate key, no matter what.
	 * (provided that `offset' doesn't get to 2^53, but let's
	 * hope noone wants to add 2^53 objects to the poor array)
	 */
	do {
		x = rndfloat() + offset;
		key = makefloat(x);
		spn_array_get(values, &key, &aux);

		/* if too many retries, assume full region */
		if (++counter > 5) {
			offset++;
			counter = 0;
		}
	} while (!isnil(&aux)); /* already used? retry! */

	spn_array_set(values, &key, val);
	return x;
}


/* returns a (double) value index that corresponds to either
 * a function (on success) or an error string (on error)
 */
SPN_API double jspn_compile(const char *src)
{
	SpnContext *ctx = get_global_context();
	SpnValue func;

	/* on error, return error index */
	if (spn_ctx_loadstring(ctx, src, &func) != 0) {
		return ERR_INDEX;
	}

	/* no need for spn_value_release(): result function is non-owning */
	return add_to_global_values(&func);
}

/* parses a comma-separated list of doubles.
 * No whitespace or any other kind of filling is allowed
 * between the floats.
 */
static double *parse_indices(const char *str, int *count)
{
	int n = 0, i = 0;
	const char *p = str;
	char *end;
	double *res;

	if (str == NULL || *str == 0) {
		*count = 0;
		return NULL;
	}

	/* count numbers */
	while (1) {
		const char *q = strchr(p, ',');
		n++;
		if (q) {
			p = q + 1;
		} else {
			break;
		}
	}

	res = malloc(n * sizeof res[0]);

	if (res == NULL) {
		*count = 0;
		return NULL;
	}

	/* actually parse string */
	p = str;
	for (i = 0; i < n; i++) {
		res[i] = strtod(p, &end);
		p = end + 1;
	}

	*count = n;
	return res;
}

/* returns (double) object index */
SPN_API double jspn_execute(double fnidx, const char *args)
{
	double validx;
	SpnContext *ctx = get_global_context();
	SpnArray *values = get_global_values();
	SpnValue func, fnkey = makefloat(fnidx), ret;

	/* parse and obtain arguments */
	int argc, i;
	SpnValue *argv;
	double *indices = parse_indices(args, &argc);
	argv = malloc(argc * sizeof argv[0]);

	for (i = 0; i < argc; i++) {
		SpnValue index = makefloat(indices[i]);
		spn_array_get(values, &index, &argv[i]);
	}

	free(indices);

	/* obtain function object */
	spn_array_get(values, &fnkey, &func);

	/* call it, return ERR_INDEX on error */
	if (spn_ctx_callfunc(ctx, &func, &ret, argc, argv) != 0) {
		validx = ERR_INDEX;
	} else {
		validx = add_to_global_values(&ret);
		spn_value_release(&ret);
	}

	free(argv);
	return validx;
}

SPN_API const char *jspn_errmsg(void)
{
	return spn_ctx_geterrmsg(get_global_context());
}

static void get_value(double key, SpnValue *val)
{
	SpnArray *values = get_global_values();
	SpnValue idx = makefloat(key);
	spn_array_get(values, &idx, val);
}

SPN_API const char *jspn_typeof(double key)
{
	SpnValue val;
	get_value(key, &val);
	return spn_type_name(val.type);
}

/* getters */
SPN_API int jspn_getbool(double key)
{
	SpnValue b;
	get_value(key, &b);
	return isbool(&b) ? boolvalue(&b) : 0;
}

SPN_API long jspn_getint(double key)
{
	SpnValue n;
	get_value(key, &n);

	if (!isnum(&n)) {
		return 0;
	}

	return isint(&n) ? intvalue(&n) : floatvalue(&n);
}

SPN_API double jspn_getfloat(double key)
{
	SpnValue n;
	get_value(key, &n);

	if (!isnum(&n)) {
		return 0;
	}

	return isfloat(&n) ? floatvalue(&n) : intvalue(&n);
}

SPN_API const char *jspn_getstr(double key)
{
	SpnValue val;
	get_value(key, &val);
	return isstring(&val) ? stringvalue(&val)->cstr : NULL;
}

/* TODO: add array getters! */

/* setters */

SPN_API double jspn_addnil(void)
{
	return NIL_INDEX;
}

SPN_API double jspn_addbool(int b)
{
	SpnValue val = makebool(!!b);
	return add_to_global_values(&val);
}

SPN_API double jspn_addnumber(double n)
{
	SpnValue val = makefloat(n);
	return add_to_global_values(&val);
}

SPN_API double jspn_addstr(const char *s)
{
	SpnValue val = makestring(s);
	double validx = add_to_global_values(&val);
	spn_value_release(&val);
	return validx;
}

/* TODO: do something with arrays. How to add_array()?
 * (add an already-existing, precomposed array? Or
 * accept argc+argv and make a new array? or what?
 */

/* TODO: also add array SETTERS as well (getters are
 * already mentioned in another TODO above...)
 */

/* TODO: do something with user info values */

