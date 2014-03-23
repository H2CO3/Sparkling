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

#include "api.h"
#include "vm.h"


/* convenience SpnValue-related re-defines. For internal use only! */
#define isnil(val)		spn_isnil(val)
#define isbool(val)		spn_isbool(val)
#define isnumber(val)		spn_isnumber(val)
#define isstring(val)		spn_isstring(val)
#define isarray(val)		spn_isarray(val)
#define isfunc(val)		spn_isfunc(val)
#define isuserinfo(val)		spn_isuserinfo(val)

#define isobject(val)		spn_isobject(val)
#define typetag(t)		spn_typetag(t)
#define typeflag(t)		spn_typeflag(t)
#define valtype(val)		spn_valtype(val)
#define valflag(val)		spn_valflag(val)

#define isint(val)		spn_isint(val)
#define isfloat(val)		spn_isfloat(val)
#define isweakuserinfo(val)	spn_isweakuserinfo(val)
#define isstrguserinfo(val)	spn_isstrguserinfo(val)

#define boolvalue(val)		spn_boolvalue(val)
#define intvalue(val)		spn_intvalue(val)
#define floatvalue(val)		spn_floatvalue(val)
#define ptrvalue(val)		spn_ptrvalue(val)
#define objvalue(val)		spn_objvalue(val)

#define stringvalue(val)	spn_stringvalue(val)
#define arrayvalue(val)		spn_arrayvalue(val)
#define funcvalue(val)		spn_funcvalue(val)

#define makenil()		spn_makenil()
#define makebool(b)		spn_makebool(b)
#define makeint(i)		spn_makeint(i)
#define makefloat(f)		spn_makefloat(f)
#define makescriptfunc(n, b, e)	spn_makescriptfunc(n, b, e)
#define makenativefunc(n, f)	spn_makenativefunc(n, f)
#define makeweakuserinfo(p)	spn_makeweakuserinfo(p)
#define makestrguserinfo(o)	spn_makestrguserinfo(o)

/* invokes spn_string_new(s) */
#define makestring(s)		spn_makestring(s)

/* this invokes spn_string_new_nocopy(s, 0) */
#define makestring_nocopy(s)	spn_makestring_nocopy(s)

/* and this invokes spn_string_new_nocopy_len(s, n, d) */
#define makestring_nocopy_len(s, n, d) spn_makestring_nocopy_len(s, n, d)

#define makearray()		spn_makearray()

/* the following stuff is primarily (not exlusively) for use in the VM */

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

/* maximal number of registers per stack frame in the VM. Do not change
 * or hell breaks loose -- the instruction format depends on this.
 */
#define MAX_REG_FRAME	256

/* this is a common function so that the disassembler can use it too */
SPN_API int nth_arg_idx(spn_uword *ip, int idx);

/* "safe" allocator functions */
SPN_API void *spn_malloc(size_t n);
SPN_API void *spn_realloc(void *p, size_t n);

/* typedef for function to be called on spn_load_stdlib */
typedef void (*spn_libld_func)(SpnVMachine *);
/* register function to be called on spn_load_stdlib */
SPN_API void spn_add_libld(spn_libld_func f);
/* run functions registered with above function */
SPN_API void spn_run_libld(SpnVMachine *vm);

#endif /* SPN_PRIVATE_H */

