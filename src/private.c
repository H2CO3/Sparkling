/*
 * private.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 14/09/2013
 * Licensed under the 2-clause BSD License
 *
 * Private parts of the Sparkling API
 */

#include <stdlib.h>

#include "private.h"

static spn_libld_func libld_funcs[100];
static int spn_libld_n = 0;

int nth_arg_idx(spn_uword *ip, int idx)
{
	int wordidx = idx / SPN_WORD_OCTETS;
	int shift = 8 * (idx % SPN_WORD_OCTETS);

	spn_uword word = ip[wordidx];
	int regidx = (word >> shift) & 0xff;

	return regidx;
}

void *spn_malloc(size_t n)
{
	void *ptr = malloc(n);

	if (ptr == NULL) {
		abort();
	}

	return ptr;
}

void *spn_realloc(void *ptr, size_t n)
{
	void *ret = realloc(ptr, n);

	if (ret == NULL) {
		abort();
	}

	return ret;
}

void spn_add_libld(spn_libld_func f)
{
	libld_funcs[spn_libld_n++] = f;
}

void spn_run_libld(SpnVMachine *vm)
{
	int i;
	for(i = 0; i < spn_libld_n; i++)
		libld_funcs[i](vm);
}

