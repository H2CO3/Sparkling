/*
 * jsapi.c
 * JavaScript bindings for Sparkling
 * Created by Arpad Goretity on 01/09/2014.
 *
 * Licensed under the 2-clause BSD License
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>


#include "api.h"
#include "ctx.h"
#include "private.h"
#include "func.h"
#include "array.h"


// forward-declare Sparkling library functions
static int jspn_callWrappedFunc(SpnValue *, int, SpnValue[], void *);
static int jspn_valueToIndex(SpnValue *, int, SpnValue[], void *);
static int jspn_jseval(SpnValue *, int, SpnValue[], void *);

static SpnContext *jspn_global_ctx = NULL;
static SpnArray *jspn_global_vals = NULL;

static SpnContext *get_global_context(void)
{
	if (jspn_global_ctx == NULL) {
		jspn_global_ctx = spn_ctx_new();

		static const SpnExtFunc lib[] = {
			{ "jspn_callWrappedFunc", jspn_callWrappedFunc },
			{ "jspn_valueToIndex",    jspn_valueToIndex    },
			{ "jseval",               jspn_jseval          }
		};

		spn_ctx_addlib_cfuncs(jspn_global_ctx, NULL, lib, sizeof lib / sizeof lib[0]);
	}

	return jspn_global_ctx;
}

static SpnArray *get_global_values(void)
{
	if (jspn_global_vals == NULL) {
		jspn_global_vals = spn_array_new();
	}

	return jspn_global_vals;
}

#define ERROR_INDEX       (-1)
#define FIRST_VALUE_INDEX   0

static int next_value_index = FIRST_VALUE_INDEX;

static int add_to_values(SpnValue *val)
{
	spn_array_set_intkey(get_global_values(), next_value_index, val);
	return next_value_index++;
}

static SpnValue value_by_index(int index)
{
	SpnValue result;
	spn_array_get_intkey(get_global_values(), index, &result);
	return result;
}

extern void jspn_reset(void)
{
	if (jspn_global_ctx != NULL) {
		spn_ctx_free(jspn_global_ctx);
		jspn_global_ctx = NULL;
	}

	if (jspn_global_vals != NULL) {
		spn_object_release(jspn_global_vals);
		jspn_global_vals = NULL;
	}

	next_value_index = FIRST_VALUE_INDEX;
}

extern void jspn_freeAll(void)
{
	SpnArray *globals = get_global_values();
	SpnValue nilval = makenil();

	for (int i = 0; i < next_value_index; i++) {
		spn_array_set_intkey(globals, i, &nilval);
	}

	next_value_index = FIRST_VALUE_INDEX;
}

extern int jspn_compile(const char *src)
{
	SpnFunction *fn = spn_ctx_loadstring(get_global_context(), src);
	if (fn == NULL) {
		return ERROR_INDEX;
	}

	SpnValue fnval = { .type = SPN_TYPE_FUNC, .v.o = fn };
	return add_to_values(&fnval);
}

extern int jspn_compileExpr(const char *src)
{
	SpnFunction *fn = spn_ctx_compile_expr(get_global_context(), src);
	if (fn == NULL) {
		return ERROR_INDEX;
	}

	SpnValue fnval = { .type = SPN_TYPE_FUNC, .v.o = fn };
	return add_to_values(&fnval);
}

extern int jspn_call(int func_index, int argv_index, size_t argc)
{
	SpnValue func_val = value_by_index(func_index);
	if (!isfunc(&func_val)) {
		const void *args[] = { spn_type_name(func_val.type) };
		spn_ctx_runtime_error(get_global_context(), "attempt to call value of non-function type %s",args);
		return ERROR_INDEX;
	}

	SpnValue argv_val = value_by_index(argv_index);
	assert(isarray(&argv_val));

	SpnFunction *func = funcvalue(&func_val);
	SpnArray *argv = arrayvalue(&argv_val);

	// malloc is used instead of a VLA so that we can avoid
	// working around the UB in the special case of argc == 0
	SpnValue *args = malloc(argc * sizeof args[0]);
	for (size_t i = 0; i < argc; i++) {
		spn_array_get_intkey(argv, i, &args[i]);
	}

	SpnValue result;
	int err = spn_ctx_callfunc(
		get_global_context(),
		func,
		&result,
		argc,
		args
	);

	free(args);

	if (err != 0) {
		return ERROR_INDEX;
	}

	int result_index = add_to_values(&result);
	spn_value_release(&result);
	return result_index;
}

extern const char *jspn_lastErrorMessage(void)
{
	return spn_ctx_geterrmsg(get_global_context());
}

extern const char *jspn_lastErrorType(void)
{
	enum spn_error_type errtype = spn_ctx_geterrtype(get_global_context());
	switch (errtype) {
	case SPN_ERROR_OK:       return "OK";
	case SPN_ERROR_SYNTAX:   return "syntax";
	case SPN_ERROR_SEMANTIC: return "semantic";
	case SPN_ERROR_RUNTIME:  return "runtime";
	case SPN_ERROR_GENERIC:  return "generic";
	default:                 return "unknown";
	}
}

extern int jspn_getGlobal(const char *name)
{
	SpnArray *globals = spn_ctx_getglobals(get_global_context());
	SpnValue global;
	spn_array_get_strkey(globals, name, &global);
	return add_to_values(&global);
}

extern void jspn_setGlobal(const char *name, int index)
{
	SpnValue val = value_by_index(index);
	SpnArray *globals = spn_ctx_getglobals(get_global_context());
	spn_array_set_strkey(globals, name, &val);
}

extern const char *jspn_backtrace(void)
{
	static char *buf = NULL;

	// since the returned pointer will be converted to a
	// JavaScript string immediately after this function
	// returns, it's safe to free the buffer during the
	// next call.
	free(buf);
	buf = NULL;

	size_t len = 0;
	size_t n;
	const char **bt = spn_ctx_stacktrace(get_global_context(), &n);

	if (n == 0) {
		free(bt);
		return NULL;
	}

	for (size_t i = 0; i < n; i++) {
		size_t oldlen = len;
		size_t slen = strlen(bt[i]);
		len += slen + 1; // +1 for newline
		// I assume realloc()'ing in the Emscripten heap, which
		// itself is heap-allocated anyway, should not fail...
		buf = realloc(buf, len);
		memcpy(buf + oldlen, bt[i], slen);
		buf[oldlen + slen] = '\n';
	}

	// Last newline is overwritten with the terminating 0
	buf[len - 1] = 0;
	free(bt);

	return buf;
}

// Setters (JavaScript -> Sparkling/C)
extern int jspn_addNil(void)
{
	SpnValue val = makenil();
	return add_to_values(&val);
}

extern int jspn_addBool(int b)
{
	SpnValue val = makebool(!!b);
	return add_to_values(&val);
}

extern int jspn_addNumber(double x)
{
	SpnValue val = floor(x) == x ? makeint(x) : makefloat(x);
	return add_to_values(&val);
}

extern int jspn_addString(const char *str)
{
	SpnValue val = makestring(str);
	int index = add_to_values(&val);
	spn_value_release(&val);
	return index;
}

extern int jspn_addArrayWithIndexBuffer(int32_t *indexBuffer, size_t n_objects)
{
	SpnArray *array = spn_array_new();

	for (size_t i = 0; i < n_objects; i++) {
		int32_t index = indexBuffer[i];
		SpnValue val = value_by_index(index);
		spn_array_set_intkey(array, i, &val);
	}

	SpnValue arrval = { .type = SPN_TYPE_ARRAY, .v.o = array };
	int result_index = add_to_values(&arrval);
	spn_object_release(array);
	return result_index;
}

extern int jspn_addDictionaryWithIndexBuffer(int32_t *indexBuffer, size_t n_key_val_pairs)
{
	SpnArray *dict = spn_array_new();

	for (size_t i = 0; i < n_key_val_pairs; i++) {
		int32_t key_index = indexBuffer[2 * i + 0];
		int32_t val_index = indexBuffer[2 * i + 1];
		SpnValue key = value_by_index(key_index);
		SpnValue val = value_by_index(val_index);
		spn_array_set(dict, &key, &val);
	}

	SpnValue dictval = { .type = SPN_TYPE_ARRAY, .v.o = dict };
	int result_index = add_to_values(&dictval);
	spn_object_release(dict);
	return result_index;
}

// Getters (Sparkling/C -> JavaScript)
extern int jspn_typeAtIndex(int index)
{
	SpnValue val = value_by_index(index);
	return valtype(&val);
}

extern int jspn_getBool(int index)
{
	SpnValue val = value_by_index(index);
	assert(isbool(&val));
	return boolvalue(&val);
}

extern double jspn_getNumber(int index)
{
	SpnValue val = value_by_index(index);
	assert(isnum(&val));
	return val.type & SPN_FLAG_FLOAT ? val.v.f : val.v.i;
}

extern const char *jspn_getString(int index)
{
	SpnValue val = value_by_index(index);
	assert(isstring(&val));
	return stringvalue(&val)->cstr;
}

// This is a terrible, ugly hack that makes
// security enthusiasts cry and vomit.
extern int jspn_addWrapperFunction(int funcIndex)
{
	static SpnFunction *wrapperGenerator = NULL;

	if (wrapperGenerator == NULL) {
		wrapperGenerator = spn_ctx_loadstring(
			get_global_context(),
			"let funcIndex = argv[0];"
			"return function() {"
			"	let _argc = argc, _argv = argv;"
			"	var args = {};"
			"	for (var i = 0; i < _argc; ++i) {"
			"		args[i] = jspn_valueToIndex(_argv[i]);"
			"	}"
			"	return jspn_callWrappedFunc(funcIndex, args);"
			"};"
		);
		assert(wrapperGenerator != NULL);
	}

	SpnValue indexVal[] = { makeint(funcIndex) };
	SpnValue wrapper;
	int err = spn_ctx_callfunc(
		get_global_context(),                  // context
		wrapperGenerator,                      // callee
		&wrapper,                              // return value
		sizeof indexVal / sizeof indexVal[0],  // argc
		indexVal                               // argv
	);

	assert(err == 0);

	int wrapperIndex = add_to_values(&wrapper);
	spn_value_release(&wrapper);
	return wrapperIndex;
}

// Helpers for addWrapperFunction
static int jspn_valueToIndex(SpnValue *ret, int argc, SpnValue argv[], void *ctx)
{
	assert(argc == 1);
	int index = add_to_values(&argv[0]);
	*ret = makeint(index);
	return 0;
}

extern int jspn_callJSFunc(int funcIndex, size_t argc, SpnArray *argv);

static int jspn_callWrappedFunc(SpnValue *ret, int argc, SpnValue argv[], void *ctx)
{
	assert(argc == 2);
	assert(isint(&argv[0]));
	assert(isarray(&argv[1]));

	int funcIndex = intvalue(&argv[0]);
	SpnArray *argIndices = arrayvalue(&argv[1]);
	size_t count = spn_array_count(argIndices);
	int retValIndex = jspn_callJSFunc(funcIndex, count, argIndices);
	spn_array_get_intkey(get_global_values(), retValIndex, ret);
	spn_value_retain(ret);

	return 0;
}

// helper for jspn_jseval, implemented in JavaScript
extern int jspn_jseval_helper(char *src);

static int jspn_jseval(SpnValue *ret, int argc, SpnValue argv[], void *ctx)
{
	SpnString *src;
	int index;

	if (argc != 1) {
		spn_ctx_runtime_error(ctx, "expecting one argument", NULL);
		return -1;
	}

	if (!isstring(&argv[0])) {
		spn_ctx_runtime_error(ctx, "argument must be a string", NULL);
		return -2;
	}

	src = stringvalue(&argv[0]);
	index = jspn_jseval_helper(src->cstr);

	if (index < FIRST_VALUE_INDEX) {
		spn_ctx_runtime_error(ctx, "error in eval()", NULL);
		return -3;
	}

	spn_array_get_intkey(get_global_values(), index, ret);
	spn_value_retain(ret);

	return 0;
}

extern int jspn_getIntFromArray(SpnArray *array, int index)
{
	SpnValue val;
	spn_array_get_intkey(array, index, &val);
	assert(isint(&val));
	return intvalue(&val);
}

// it is simpler and less error-prone to do the pointer
// arithmetic in C... although I could technically
// do this right from JavaScript, but this way it's
// more convenient.
// Also, I don't have to export the `add_to_values()` function.
extern int jspn_addValueFromArgv(SpnValue argv[], int index)
{
	return add_to_values(&argv[index]);
}

extern size_t jspn_countOfArrayAtIndex(int index)
{
	SpnValue val = value_by_index(index);
	assert(isarray(&val));
	return spn_array_count(arrayvalue(&val));
}

extern void jspn_getKeyAndValueIndicesOfArrayAtIndex(int index, int32_t *indexBuffer)
{
	SpnValue arrayval = value_by_index(index);
	assert(isarray(&arrayval));

	SpnArray *array = arrayvalue(&arrayval);
	SpnIterator *it = spn_iter_new(array);
	size_t n = spn_array_count(array);
	size_t i;

	SpnValue key, value;
	while ((i = spn_iter_next(it, &key, &value)) < n) {
		indexBuffer[i * 2 + 0] = add_to_values(&key);
		indexBuffer[i * 2 + 1] = add_to_values(&value);
	}

	spn_iter_free(it);
}
