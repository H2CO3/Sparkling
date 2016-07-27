/*
 * array.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Arrays: indexed value containers
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "array.h"
#include "private.h"
#include "str.h"


struct SpnArray {
	SpnObject base;        /* for being a valid object          */
	SpnValue *vector;      /* the actual raw array of values    */
	size_t    count;       /* logical size                      */
	size_t    allocsize;   /* allocation (actual) size          */
};


static void free_array(void *obj);

static const SpnClass spn_class_array = {
	sizeof(SpnArray),
	SPN_CLASS_UID_ARRAY,
	"array",
	NULL,
	NULL,
	NULL,
	NULL,
	free_array
};

SpnArray *spn_array_new(void)
{
	SpnArray *array = spn_object_new(&spn_class_array);
	array->vector = NULL;
	array->count = 0;
	array->allocsize = 0;
	return array;
}

static void free_array(void *obj)
{
	SpnArray *arr = obj;
	size_t i;

	for (i = 0; i < arr->count; i++) {
		spn_value_release(&arr->vector[i]);
	}

	free(arr->vector);
}

size_t spn_array_count(SpnArray *arr)
{
	return arr->count;
}

SpnValue spn_array_get(SpnArray *arr, size_t index)
{
	if (index >= arr->count) {
		unsigned long ulindex = index, ulcount = arr->count;
		spn_die("array index %lu is too high (size = %lu)\n", ulindex, ulcount);
	}

	return arr->vector[index];
}

void spn_array_set(SpnArray *arr, size_t index, const SpnValue *val)
{
	if (index >= arr->count) {
		unsigned long ulindex = index, ulcount = arr->count;
		spn_die("array index %lu is too high (size = %lu)\n", ulindex, ulcount);
	}

	spn_value_retain(val);
	spn_value_release(&arr->vector[index]);
	arr->vector[index] = *val;
}

void spn_array_insert(SpnArray *arr, size_t index, const SpnValue *val)
{
	size_t i, maxindex;

	/* index == arr->count is allowed (insertion at end) */
	if (index > arr->count) {
		unsigned long ulindex = index, ulcount = arr->count;
		spn_die("array index %lu is too high (size = %lu)\n", ulindex, ulcount);
	}

	maxindex = arr->count++;

	if (arr->allocsize == 0) {
		arr->allocsize = 8;
		arr->vector = spn_malloc(arr->allocsize * sizeof arr->vector[0]);
	}

	if (arr->count > arr->allocsize) {
		arr->allocsize *= 2;
		arr->vector = spn_realloc(arr->vector, arr->allocsize * sizeof arr->vector[0]);
	}

	for (i = maxindex; i > index; i--) {
		arr->vector[i] = arr->vector[i - 1];
	}

	spn_value_retain(val);
	arr->vector[index] = *val;
}

void spn_array_remove(SpnArray *arr, size_t index)
{
	size_t i;

	if (index >= arr->count) {
		unsigned long ulindex = index, ulcount = arr->count;
		spn_die("array index %lu is too high (size = %lu)\n", ulindex, ulcount);
	}

	spn_value_release(&arr->vector[index]);
	arr->count--;

	for (i = index; i < arr->count; i++) {
		arr->vector[i] = arr->vector[i + 1];
	}
}

void spn_array_inject(SpnArray *arr, size_t index, SpnArray *other)
{
	size_t i, j;
	size_t n_arr = arr->count, n_other = other->count;

	if (index > n_arr) {
		unsigned long ulindex = index, ulcount = n_arr;
		spn_die("array index %lu is too high (size = %lu)\n", ulindex, ulcount);
	}

	/* expand array */
	spn_array_setsize(arr, n_arr + n_other);

	/* shift elements at positions >= index towards end of array */
	for (i = n_arr; i > index; i--) {
		arr->vector[i - 1 + n_other] = arr->vector[i - 1];
	}

	/* take ownership of new elements, insert them at 'index' */
	for (i = index, j = 0; j < n_other; i++, j++) {
		spn_value_retain(&other->vector[j]);
		arr->vector[i] = other->vector[j];
	}
}

void spn_array_push(SpnArray *arr, const SpnValue *val)
{
	spn_array_insert(arr, arr->count, val);
}

/* removes an element from the end */
void spn_array_pop(SpnArray *arr)
{
	if (arr->count == 0) {
		spn_die("cannot pop an empty array");
	}

	spn_array_remove(arr, arr->count - 1);
}

void spn_array_setsize(SpnArray *arr, size_t newsize)
{
	size_t oldsize = arr->count, i;

	/* if newsize != oldsize, then exactly one
	 * of the loops below will be executed.
	 */

	/* if the new size is greater than the old one, then append nils */
	for (i = oldsize; i < newsize; i++) {
		spn_array_push(arr, &spn_nilval);
	}

	/* else if the new size is less than the old one, just chop off the end */
	for (i = newsize; i < oldsize; i++) {
		spn_array_pop(arr);
	}
}

/* convenience value constructor */
SpnValue spn_makearray(void)
{
	return makeobject(spn_array_new());
}
