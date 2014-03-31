/*
 * rtlb.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Run-time support library
 */

#ifndef SPN_RTLB_H
#define SPN_RTLB_H

#include "vm.h"

/* Functions:
 * getline(), print(), printf()
 * fopen(), fclose()
 * fprintf(), fgetline()
 * fread(), fwrite()
 * fflush(), ftell(), fseek(), feof()
 * remove(), rename(), tmpnam(), tmpfile()
 * readfile()
 * 
 * Constants: stdin, stdout, stderr
 */
#define SPN_LIBSIZE_IO 18
SPN_API const SpnExtFunc spn_libio[SPN_LIBSIZE_IO];

/* Functions:
 * indexof(), substr(), substrto(), substrfrom()
 * split(), repeat()
 * tolower(), toupper()
 * fmtstr()
 * tonumber(), toint(), tofloat()
 */
#define SPN_LIBSIZE_STRING 12
SPN_API const SpnExtFunc spn_libstring[SPN_LIBSIZE_STRING];

/* Functions:
 * sort() (uses custom comparison function if given, `<' operator otherwise),
 * linsearch(), binsearch(), contains()
 * subarray(), join()
 * foreach(), reduce(), filter(), map() (returns an array of mapped elements)
 * insert(), insertarr(), delrange(), clear()
 */
#define SPN_LIBSIZE_ARRAY 14
SPN_API const SpnExtFunc spn_libarray[SPN_LIBSIZE_ARRAY];

/* Real functions:
 * abs(), min(), max()
 * floor(), ceil(), round(), sgn()
 * hypot(), sqrt(), cbrt(), pow(), exp(), exp2(), exp10(), log(), log2(), log10()
 * sin(), cos(), tan(), sinh(), cosh(), tanh()
 * asin(), acos(), atan(), atan2()
 * deg2rad(), rad2deg()
 * random(), seed()
 * isfin(), isinf(), isnan(), isfloat(), isint()
 * fact(), binom()
 * 
 * Complex functions:
 * cplx_add(), cplx_sub(), cplx_mul(), cplx_div()
 * cplx_conj(), cplx_abs()
 * cplx_sin(), cplx_cos(), cplx_tan()
 * can2pol(), pol2can()
 * 
 * Constants: M_E, M_LOG2E, M_LOG10E, M_LN2, M_LN10, M_PI, M_PI_2, M_PI_4,
 * M_1_PI, M_2_PI, M_2_SQRTPI, M_SQRT2, M_SQRT1_2
 */
#define SPN_LIBSIZE_MATH 49
SPN_API const SpnExtFunc spn_libmath[SPN_LIBSIZE_MATH];

/* Functions:
 * getenv()
 * system()
 * assert()
 * exit()
 * time()
 * gmtime()
 * localtime()
 * strftime()
 * difftime()
 * compile()
 * loadfile()
 */
#define SPN_LIBSIZE_SYS 11
SPN_API const SpnExtFunc spn_libsys[SPN_LIBSIZE_SYS];

/* A convenience function that loads the entire standard library.
 * Please call this at most *once* on each virtual machine instance.
 * Along with the standard library functions, this also registers some useful
 * globals such as math constants, standard streams, etc.
 */
SPN_API void spn_load_stdlib(SpnVMachine *vm);

#endif /* SPN_RTLB_H */

