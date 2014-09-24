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

/* I/O library
 * ===========
 * Free functions:
 * ---------------
 * getline(), print(), dbgprint(), printf()
 * fopen(), fclose()
 * fprintf(), fgetline()
 * fread(), fwrite()
 * fflush(), ftell(), fseek(), feof()
 * remove(), rename(), tmpnam(), tmpfile()
 * readfile()
 *
 * Constants:
 * ----------
 * stdin, stdout, stderr
 */

/* String library
 * ==============
 * Methods:
 * --------
 * indexof(), substr(), substrto(), substrfrom()
 * split(), repeat()
 * tolower(), toupper()
 * fmtstr()
 *
 * Properties:
 * -----------
 * .class  [r]
 * .length [r]
 */

/* Array library
 * =============
 * Methods:
 * --------
 * sort() (uses custom comparison function if given, `<' operator otherwise),
 * find(), pfind(), bsearch()
 * any(), all()
 * slice()
 * join()
 * foreach(), reduce(), filter(), map()
 * insert(), inject(), erase(), concat()
 * merge(), convol()
 * push(), pop()
 * swap(), reverse()
 *
 * Free functions:
 * ---------------
 * combine()
 *
 * Properties:
 * -----------
 * .class  [rw]
 * .length [r]
 * .keys   [r]
 * .values [r]
 */

/* Maths library
 * =============
 * Free functions:
 * ---------------
 *
 * Real:
 * abs(), min(), max(), range()
 * floor(), ceil(), round(), sgn()
 * hypot(), sqrt(), cbrt(), pow(), exp(), exp2(), exp10(), log(), log2(), log10()
 * sin(), cos(), tan(), sinh(), cosh(), tanh()
 * asin(), acos(), atan(), atan2()
 * deg2rad(), rad2deg()
 * random(), seed()
 * isfin(), isinf(), isnan(), isfloat(), isint()
 * fact(), binom()
 *
 * Complex:
 * cplx_add(), cplx_sub(), cplx_mul(), cplx_div()
 * cplx_conj(), cplx_abs()
 * cplx_sin(), cplx_cos(), cplx_tan()
 * can2pol(), pol2can()
 *
 * Constants:
 * ----------
 * M_E, M_PI, M_SQRT2, M_PHI, M_INF, M_NAN
 */

/* System/Utility library
 * ======================
 * Free functions:
 * ---------------
 * getenv()
 * system()
 * assert()
 * time()
 * utctime()
 * localtime()
 * fmtdate()
 * difftime()
 * compile()
 * exprtofn()
 * toint()
 * tofloat()
 * tonumber()
 * call()
 * require()
 * backtrace()
 *
 * Properties:
 * -----------
 * .class [rw] -- on values of type user info
 *
 * Constants:
 * ----------
 * getter (special ID of the getter method of a class)
 * setter (special ID of the setter method of a class)
 * Array: the default class for arrays
 * String: the default class for strings
 */

/* A convenience function that loads the entire standard library.
 * Please call this at most *once* on each virtual machine instance.
 * This registers all free functions, methods, properties and constants.
 */
SPN_API void spn_load_stdlib(SpnVMachine *vm);

#endif /* SPN_RTLB_H */
