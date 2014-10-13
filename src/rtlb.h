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
 * remove(), rename(), tmpfile()
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
 * push(), pop(), last()
 * swap(), reverse()
 *
 * Properties:
 * -----------
 * .length [r]
 */

/* Hashmap library
 * ==============
 * Methods:
 * --------
 * foreach()
 * map()
 * filter()
 * keys(), values()
 *
 * Free functions:
 * ---------------
 * combine()
 *
 * Properties:
 * -----------
 * .length [r]
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
 * require()
 * backtrace()
 *
 * Methods:
 * --------
 * Function::call
 *
 * Constants:
 * ----------
 * String: the default class for strings
 * Array: the default class for arrays
 * HashMap: the defautl class for hashmaps
 */

/* A convenience function that loads the entire standard library.
 * Please call this at most *once* on each virtual machine instance.
 * This registers all free functions, methods, properties and constants.
 */
SPN_API void spn_load_stdlib(SpnVMachine *vm);

#endif /* SPN_RTLB_H */
