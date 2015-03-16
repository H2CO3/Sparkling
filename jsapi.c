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
#include "str.h"
#include "array.h"
#include "hashmap.h"


// forward-declare Sparkling library functions
static int jspn_callWrappedFunc(SpnValue *, int, SpnValue[], void *);
static int jspn_valueToIndex(SpnValue *, int, SpnValue[], void *);
static int jspn_jseval(SpnValue *, int, SpnValue[], void *);

static SpnContext *jspn_global_ctx = NULL;
static SpnArray *jspn_global_vals = NULL;

static SpnContext *get_global_context(void)
{
	if (jspn_global_ctx == NULL) {
		static SpnContext ctx;
		spn_ctx_init(&ctx);

		jspn_global_ctx = &ctx;

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

static int add_to_values(SpnValue val)
{
	spn_array_push(get_global_values(), &val);
	return next_value_index++;
}

static SpnValue value_by_index(int index)
{
	return spn_array_get(get_global_values(), index);
}

// Various 'static' variables that only need to be created once for
// performance reasons.
// As they're owned by a particular context, they need to be re-created
// when the context is deallocated.
static SpnFunction *wrapperGenerator = NULL;

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

	wrapperGenerator = NULL;
	next_value_index = FIRST_VALUE_INDEX;
}

extern void jspn_freeAll(void)
{
	SpnArray *globals = get_global_values();
	spn_array_setsize(globals, 0);
	next_value_index = FIRST_VALUE_INDEX;
}

extern int jspn_compile(const char *src)
{
	SpnFunction *fn = spn_ctx_compile_string(get_global_context(), src, 1);
	if (fn == NULL) {
		return ERROR_INDEX;
	}

	return add_to_values((SpnValue){ .type = SPN_TYPE_FUNC, .v.o = fn });
}

extern int jspn_compileExpr(const char *src)
{
	SpnFunction *fn = spn_ctx_compile_expr(get_global_context(), src, 1);
	if (fn == NULL) {
		return ERROR_INDEX;
	}

	return add_to_values((SpnValue){ .type = SPN_TYPE_FUNC, .v.o = fn });
}

extern int jspn_parse(const char *src)
{
	SpnHashMap *ast = spn_ctx_parse(get_global_context(), src);
	if (ast == NULL) {
		return ERROR_INDEX;
	}

	SpnValue val = { .type = SPN_TYPE_HASHMAP, .v.o = ast };
	int index = add_to_values(val);
	spn_object_release(ast);
	return index;
}

extern int jspn_parseExpr(const char *src)
{
	SpnHashMap *ast = spn_ctx_parse_expr(get_global_context(), src);
	if (ast == NULL) {
		return ERROR_INDEX;
	}

	SpnValue val = { .type = SPN_TYPE_HASHMAP, .v.o = ast };
	int index = add_to_values(val);
	spn_object_release(ast);
	return index;
}

extern int jspn_compileAST(int astIndex)
{
	SpnValue astVal = value_by_index(astIndex);
	assert(ishashmap(&astVal));

	SpnHashMap *ast = hashmapvalue(&astVal);
	SpnFunction *fn = spn_ctx_compile_ast(get_global_context(), ast, 1);
	if (fn == NULL) {
		return ERROR_INDEX;
	}

	return add_to_values((SpnValue){ .type = SPN_TYPE_FUNC, .v.o = fn });
}

extern int jspn_call(int func_index, int argv_index)
{
	SpnValue func_val = value_by_index(func_index);
	if (!isfunc(&func_val)) {
		const void *args[] = { spn_type_name(func_val.type) };
		spn_ctx_runtime_error(get_global_context(), "attempt to call value of non-function type %s", args);
		return ERROR_INDEX;
	}

	SpnValue argv_val = value_by_index(argv_index);
	assert(isarray(&argv_val));

	SpnFunction *func = funcvalue(&func_val);
	SpnArray *argv = arrayvalue(&argv_val);
	size_t argc = spn_array_count(argv);

	// malloc is used instead of a VLA so that we can avoid
	// working around the UB in the special case of argc == 0
	SpnValue *args = malloc(argc * sizeof args[0]);
	for (size_t i = 0; i < argc; i++) {
		args[i] = spn_array_get(argv, i);
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

	int result_index = add_to_values(result);
	spn_value_release(&result);
	return result_index;
}

extern const char *jspn_lastErrorMessage(void)
{
	return spn_ctx_geterrmsg(get_global_context());
}

extern unsigned jspn_lastErrorLine(void)
{
	return spn_ctx_geterrloc(get_global_context()).line;
}

extern unsigned jspn_lastErrorColumn(void)
{
	return spn_ctx_geterrloc(get_global_context()).column;
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
	SpnHashMap *globals = spn_ctx_getglobals(get_global_context());
	SpnValue global = spn_hashmap_get_strkey(globals, name);
	return add_to_values(global);
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
	SpnStackFrame *bt = spn_ctx_stacktrace(get_global_context(), &n);

	if (n == 0) {
		free(bt);
		return NULL;
	}

	for (size_t i = 0; i < n; i++) {
		size_t oldlen = len;
		size_t slen = strlen(bt[i].function->name);
		len += slen + 1; // +1 for newline
		// I assume realloc()'ing in the Emscripten heap, which
		// itself is heap-allocated anyway, should not fail...
		buf = realloc(buf, len);
		memcpy(buf + oldlen, bt[i].function->name, slen);
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
	return add_to_values(spn_nilval);
}

extern int jspn_addBool(int b)
{
	return add_to_values(b ? spn_trueval : spn_falseval);
}

extern int jspn_addNumber(double x)
{
	return add_to_values(floor(x) == x ? makeint(x) : makefloat(x));
}

extern int jspn_addString(const char *str)
{
	SpnValue val = makestring(str);
	int index = add_to_values(val);
	spn_value_release(&val);
	return index;
}

extern int jspn_addArrayWithIndexBuffer(int32_t *indexBuffer, size_t n_objects)
{
	SpnArray *array = spn_array_new();

	for (size_t i = 0; i < n_objects; i++) {
		int32_t index = indexBuffer[i];
		SpnValue val = value_by_index(index);
		spn_array_push(array, &val);
	}

	int result_index = add_to_values((SpnValue){ .type = SPN_TYPE_ARRAY, .v.o = array });
	spn_object_release(array);
	return result_index;
}

extern int jspn_addDictionaryWithIndexBuffer(int32_t *indexBuffer, size_t n_key_val_pairs)
{
	SpnHashMap *dict = spn_hashmap_new();

	for (size_t i = 0; i < n_key_val_pairs; i++) {
		int32_t key_index = indexBuffer[2 * i + 0];
		int32_t val_index = indexBuffer[2 * i + 1];
		SpnValue key = value_by_index(key_index);
		SpnValue val = value_by_index(val_index);
		spn_hashmap_set(dict, &key, &val);
	}

	int result_index = add_to_values((SpnValue){ .type = SPN_TYPE_HASHMAP, .v.o = dict });
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
	return isfloat(&val) ? floatvalue(&val) : intvalue(&val);
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
	if (wrapperGenerator == NULL) {
		wrapperGenerator = spn_ctx_compile_string(
			get_global_context(),
			"let funcIndex = argv[0];"
			"return fn {"
			"	let args = argv.map(jspn_valueToIndex);"
			"	return jspn_callWrappedFunc(funcIndex, args);"
			"};",
			0 /* no need to debug this function */
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

	int wrapperIndex = add_to_values(wrapper);
	spn_value_release(&wrapper);
	return wrapperIndex;
}

// Helpers for addWrapperFunction
static int jspn_valueToIndex(SpnValue *ret, int argc, SpnValue argv[], void *ctx)
{
	assert(argc >= 1); // usually 2 because 'map' passes in the index too
	int index = add_to_values(argv[0]);
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
	*ret = spn_array_get(get_global_values(), retValIndex);
	spn_value_retain(ret);

	return 0;
}

// helper for jspn_jseval, implemented in JavaScript
extern int jspn_jseval_helper(const char *src);

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

	*ret = spn_array_get(get_global_values(), index);
	spn_value_retain(ret);

	return 0;
}

extern int jspn_getIntFromArray(SpnArray *array, int index)
{
	SpnValue val = spn_array_get(array, index);
	assert(isint(&val));
	return intvalue(&val);
}

// it is simpler and less error-prone to do the pointer
// arithmetic in C... although I could technically
// do this right from JavaScript, but this way it's
// more convenient.
// Also, I don't have to export the 'add_to_values()' function.
extern int jspn_addValueFromArgv(SpnValue argv[], int index)
{
	return add_to_values(argv[index]);
}

extern size_t jspn_countOfArrayAtIndex(int index)
{
	SpnValue val = value_by_index(index);
	assert(isarray(&val));
	return spn_array_count(arrayvalue(&val));
}

extern size_t jspn_countOfHashMapAtIndex(int index)
{
	SpnValue val = value_by_index(index);
	assert(ishashmap(&val));
	return spn_hashmap_count(hashmapvalue(&val));
}

extern void jspn_getValueIndicesOfArrayAtIndex(int index, int32_t *indexBuffer)
{
	SpnValue arrayval = value_by_index(index);
	assert(isarray(&arrayval));

	SpnArray *arr = arrayvalue(&arrayval);
	size_t n = spn_array_count(arr);

	for (size_t i = 0; i < n; i++) {
		SpnValue val = spn_array_get(arr, i);
		indexBuffer[i] = add_to_values(val);
	}
}

extern void jspn_getKeyAndValueIndicesOfHashMapAtIndex(int index, int32_t *indexBuffer)
{
	SpnValue hmval = value_by_index(index);
	assert(ishashmap(&hmval));

	SpnHashMap *hm = hashmapvalue(&hmval);
	size_t i = 0, cursor = 0;

	SpnValue key, value;
	while ((cursor = spn_hashmap_next(hm, cursor, &key, &value)) != 0) {
		indexBuffer[i * 2 + 0] = add_to_values(key);
		indexBuffer[i * 2 + 1] = add_to_values(value);
		i++;
	}
}
