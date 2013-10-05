/*
 * private.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 14/09/2013
 * Licensed under the 2-clause BSD License
 *
 * Private parts of the Sparkling API
 */

#ifndef SPN_PRIVATE_H
#define SPN_PRIVATE_H

#include <assert.h>
#include "spn.h"

/* you shall not pass! (assertion for unreachable code paths) */
#define SHANT_BE_REACHED() assert(((void)("code path must not be reached"), 0))

/* number of elements in an array */
#define COUNT(a)	(sizeof(a) / sizeof(a[0]))

/* miminal number of n-byte long elements an s-byte long element fits into */
#define ROUNDUP(s, n)	(((s) + (n) - 1) / (n))

/* macros for extracting information from a VM instruction */
#define OPCODE(i)	(((i) >>  0) & 0x000000ff)
#define OPA(i)		(((i) >>  8) & 0x000000ff)
#define OPB(i)		(((i) >> 16) & 0x000000ff)
#define OPC(i)		(((i) >> 24) & 0x000000ff)
#define OPMID(i)	(((i) >> 16) & 0x0000ffff)
#define OPLONG(i)	(((i) >>  8) & 0x00ffffff)

/* this is a common function so that the disassembler can use it too */
SPN_API int nth_arg_idx(spn_uword *ip, int idx);

#endif /* SPN_PRIVATE_H */

