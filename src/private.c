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
#include <string.h>

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


/* implementation of the symbol stub class */

static int symstub_equal(void *lhs, void *rhs)
{
	SymbolStub *ls = lhs, *rs = rhs;
	return strcmp(ls->name, rs->name) == 0;
}

static unsigned long symstub_hash(void *obj)
{
	/* I couldn't be bothered to implement caching */
	SymbolStub *symstub = obj;
	const char *name = symstub->name;
	size_t length = strlen(name);
	return spn_hash_bytes(name, length);
}

static const SpnClass SymbolStub_class = {
	sizeof(SymbolStub),
	symstub_equal,
	NULL,
	symstub_hash,
	NULL
};


/* constructor */
SpnValue make_symstub(const char *name)
{
	SymbolStub *obj = spn_object_new(&SymbolStub_class);
	obj->name = name;
	return makestrguserinfo(obj);
}

int is_symstub(const SpnValue *val)
{
	if (isstrguserinfo(val)) {
		SpnObject *obj = objvalue(val);
		return obj->isa == &SymbolStub_class;
	}

	return 0;
}

