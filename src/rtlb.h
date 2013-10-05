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

/* getline(), print(), printf()
 * fopen(), fclose(),
 * fprintf(), fgetline(),
 * fread(), fwrite,
 * stdin(), stdout(), stderr(),
 * fflush(), ftell(), fseek(), feof()
 * remove(), rename(), tmpnam(), tmpfile()
 */
#define SPN_LIBSIZE_IO 20
SPN_API const SpnExtFunc spn_libio[SPN_LIBSIZE_IO];

/* indexof(), substr(), substrto(), substrfrom(), nthchar(),
 * split(), repeat()
 * tolower(), toupper()
 * fmtstring()
 * tonumber(), toint(), tofloat()
 * eval()
 */
#define SPN_LIBSIZE_STRING 14
SPN_API const SpnExtFunc spn_libstring[SPN_LIBSIZE_STRING];

/* array(), dict()
 * sort() [uses '<' operator], sortcmp() [uses custom comparator function]
 * linearsrch(), binarysrch(), contains()
 * subarray(), join()
 * enumerate()
 * insert(), insertarr(), delrange(), clear()
 * getiter(), next(), closeiter()
 */
#define SPN_LIBSIZE_ARRAY 16
SPN_API const SpnExtFunc spn_libarray[SPN_LIBSIZE_ARRAY];

/* abs(), min(), max()
 * floor(), ceil(), round()
 * hypot(), sqrt(), cbrt(), pow(), exp(), exp2(), exp10(), log(), log2(), log10()
 * sin(), cos(), tan(), cot(), sinh(), cosh(), tanh(), coth()
 * asin(), acos(), atan(), acot(), asinh(), acosh(), atanh(), acoth(), atan2()
 * deg2rad(), rad2deg()
 * random(), seed()
 * isfin(), isinf(), isnan()
 */
#define SPN_LIBSIZE_MATH 33
SPN_API const SpnExtFunc spn_libmath[SPN_LIBSIZE_MATH];

/* time()
 * gmtime()
 * localtime()
 * strftime()
 * difftime()
 */
#define SPN_LIBSIZE_TIME 5
SPN_API const SpnExtFunc spn_libtime[SPN_LIBSIZE_TIME];

/* getenv()
 * getargs()
 * system()
 * assert()
 * exit()
 */
#define SPN_LIBSIZE_SYS 5
SPN_API const SpnExtFunc spn_libsys[SPN_LIBSIZE_SYS];

/* A convenience function that loads the entire standard library.
 * Please call this at most *once* on each virtual machine instance.
 */
SPN_API void spn_load_stdlib(SpnVMachine *vm);

/* this registers the "argv" vector, i. e. it gives the library function
 * `getargs()' access to command-line arguments. The `argc' and `argv'
 * variables are assumed to be the arguments of `main()' -- the strings are
 * not copied, you have to make sure that they are valid throughout the
 * lifetime of the program.
 */
SPN_API void spn_register_args(int argc, char **argv);

#endif /* SPN_RTLB_H */

