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

