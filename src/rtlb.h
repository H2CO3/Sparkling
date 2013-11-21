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
 * 
 * Constants: stdin, stdout, stderr
 */
#define SPN_LIBSIZE_IO 17
SPN_API const SpnExtFunc spn_libio[SPN_LIBSIZE_IO];

/* Functions:
 * indexof(), substr(), substrto(), substrfrom()
 * split(), repeat()
 * tolower(), toupper()
 * fmtstring()
 * tonumber(), toint(), tofloat()
 */
#define SPN_LIBSIZE_STRING 12
SPN_API const SpnExtFunc spn_libstring[SPN_LIBSIZE_STRING];

/* Functions:
 * array(), dict()
 * sort() [uses '<' operator], sortcmp() [uses custom comparator function]
 * linearsrch(), binarysrch(), contains()
 * subarray(), join()
 * enumerate()
 * insert(), insertarr(), delrange(), clear()
 */
#define SPN_LIBSIZE_ARRAY 14
SPN_API const SpnExtFunc spn_libarray[SPN_LIBSIZE_ARRAY];

/* Functions:
 * abs(), min(), max()
 * floor(), ceil(), round()
 * hypot(), sqrt(), cbrt(), pow(), exp(), exp2(), exp10(), log(), log2(), log10()
 * sin(), cos(), tan(), cot(), sinh(), cosh(), tanh(), coth()
 * asin(), acos(), atan(), acot(), asinh(), acosh(), atanh(), acoth(), atan2()
 * deg2rad(), rad2deg()
 * random(), seed()
 * isfin(), isinf(), isnan(), isfloat(), isint()
 * fact(), binom()
 * 
 * Constants: M_E, M_LOG2E, M_LOG10E, M_LN2, M_LN10, M_PI, M_PI_2, M_PI_4,
 * M_1_PI, M_2_PI, M_2_SQRTPI, M_SQRT2, M_SQRT1_2
 */
#define SPN_LIBSIZE_MATH 37
SPN_API const SpnExtFunc spn_libmath[SPN_LIBSIZE_MATH];

/* Functions:
 * time(), microtime()
 * gmtime()
 * localtime()
 * strftime()
 * difftime()
 */
#define SPN_LIBSIZE_TIME 6
SPN_API const SpnExtFunc spn_libtime[SPN_LIBSIZE_TIME];

/* Functions:
 * getenv()
 * system()
 * assert()
 * exit()
 */
#define SPN_LIBSIZE_SYS 4
SPN_API const SpnExtFunc spn_libsys[SPN_LIBSIZE_SYS];

/* A convenience function that loads the entire standard library.
 * Please call this at most *once* on each virtual machine instance.
 * Along with the standard library functions, this also registers some useful
 * globals such as math constants, standard streams, etc.
 */
SPN_API void spn_load_stdlib(SpnVMachine *vm);

#endif /* SPN_RTLB_H */

