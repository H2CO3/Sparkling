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
#include <stdarg.h>

#include "api.h"


/* convenience SpnValue-related re-defines. For internal use only! */
#define isnil(val)      spn_isnil(val)
#define isbool(val)     spn_isbool(val)
#define isnum(val)      spn_isnumber(val)
#define isstring(val)   spn_isstring(val)
#define isarray(val)    spn_isarray(val)
#define ishashmap(val)  spn_ishashmap(val)
#define isfunc(val)     spn_isfunc(val)
#define isuserinfo(val) spn_isuserinfo(val)

#define isobject(val)   spn_isobject(val)
#define typetag(t)      spn_typetag(t)
#define typeflag(t)     spn_typeflag(t)
#define valtype(val)    spn_valtype(val)
#define valflag(val)    spn_valflag(val)

#define notnil(val)         spn_notnil(val)
#define isint(val)          spn_isint(val)
#define isfloat(val)        spn_isfloat(val)
#define isweakuserinfo(val) spn_isweakuserinfo(val)
#define isstrguserinfo(val) spn_isstrguserinfo(val)

#define boolvalue(val)      spn_boolvalue(val)
#define intvalue(val)       spn_intvalue(val)
#define floatvalue(val)     spn_floatvalue(val)
#define ptrvalue(val)       spn_ptrvalue(val)
#define objvalue(val)       spn_objvalue(val)

#define stringvalue(val)    spn_stringvalue(val)
#define arrayvalue(val)     spn_arrayvalue(val)
#define hashmapvalue(val)   spn_hashmapvalue(val)
#define funcvalue(val)      spn_funcvalue(val)

#define makebool(b)             spn_makebool(b)
#define makeint(i)              spn_makeint(i)
#define makefloat(f)            spn_makefloat(f)
#define makescriptfunc(n, b, e) spn_makescriptfunc((n), (b), (e))
#define maketopprgfunc(n, b, w) spn_maketopprgfunc((n), (b), (w))
#define makenativefunc(n, f)    spn_makenativefunc((n), (f))
#define makeweakuserinfo(p)     spn_makeweakuserinfo(p)
#define makestrguserinfo(o)     spn_makestrguserinfo(o)
#define makeclosure(p)          spn_makeclosure(p)

/* invokes spn_string_new(s) */
#define makestring(s)    spn_makestring(s)

/* this invokes spn_string_new_nocopy(s, 0) */
#define makestring_nocopy(s)    spn_makestring_nocopy(s)

/* invokes spn_makestring_len((s), (n)) */
#define makestring_len(s, n) spn_makestring_len((s), (n))

/* and this invokes spn_string_new_nocopy_len(s, n, d) */
#define makestring_nocopy_len(s, n, d) spn_makestring_nocopy_len((s), (n), (d))

#define makearray()    spn_makearray()
#define makehashmap()  spn_makehashmap()

/* the following stuff is primarily (not exlusively) for use in the VM */

/* you shall not pass! (assertion for unreachable code paths) */
#define SHANT_BE_REACHED() assert(((void)("code path must not be reached"), 0))

/* number of elements in an array */
#define COUNT(a)    (sizeof(a) / sizeof(a[0]))

/* miminal number of n-byte long elements an s-byte long element fits into */
#define ROUNDUP(s, n)    (((s) + (n) - 1) / (n))

/* macros for extracting information from a VM instruction */
#define OPCODE(i)   (((i) >>  0) & 0x000000ff)
#define OPA(i)      (((i) >>  8) & 0x000000ff)
#define OPB(i)      (((i) >> 16) & 0x000000ff)
#define OPC(i)      (((i) >> 24) & 0x000000ff)
#define OPMID(i)    (((i) >> 16) & 0x0000ffff)
#define OPLONG(i)   (((i) >>  8) & 0x00ffffff)

/* maximal number of registers per stack frame in the VM. Do not change
 * or hell breaks loose -- the instruction format depends on this.
 */
#define MAX_REG_FRAME	256

/* this is a common function so that the disassembler can use it too */
SPN_API int nth_arg_idx(spn_uword *ip, int idx);

/* "safe" allocator functions */
SPN_API void *spn_malloc(size_t n);
SPN_API void *spn_realloc(void *p, size_t n);

/* fatal failure: prints formatted error message and 'abort()'s */
#ifdef __GNUC__
__attribute__((__noreturn__, __format__(__printf__, 1, 2)))
#endif /* __GNUC__ */
SPN_API void spn_die(const char *fmt, ...);

/* guess what this one does */
#ifdef __GNUC__
__attribute__((__noreturn__, __format__(__printf__, 1, 0)))
#endif /* __GNUC__ */
SPN_API void spn_diev(const char *fmt, va_list args);

/* this is a helper class for the virtual machine,
 * used for representing a pending (unresolved) symbol
 * "stub" in the local symbol table.
 */
typedef struct SymbolStub  {
	SpnObject base;
	const char *name;
} SymbolStub;

/* creates a SymbolStub value. Does _not_ copy `name'. */
SPN_API SpnValue make_symstub(const char *name);

/* returns nonzero if `val' represents an unresolved symbol, 0 otherwise */
SPN_API int is_symstub(const SpnValue *val);

/* yields the symbol stub object of an SpnValue */
#define symstubvalue(val) ((SymbolStub *)((val)->v.o))

#endif /* SPN_PRIVATE_H */
