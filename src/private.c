/*
 * private.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 14/09/2013
 * Licensed under the 2-clause BSD License
 *
 * Private parts of the Sparkling API
 */

#include "private.h"

int nth_arg_idx(spn_uword *ip, int idx)
{
	int wordidx = idx / SPN_WORD_OCTETS;
	int shift = 8 * (idx % SPN_WORD_OCTETS);
	
	spn_uword word = ip[wordidx];
	int regidx = (word >> shift) & 0xff;
	
	return regidx;
}

