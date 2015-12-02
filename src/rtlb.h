/*
 * rtlb.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * This file is part of Sparkling.
 *
 * Sparkling is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Sparkling is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Sparkling. If not, see <http://www.gnu.org/licenses/>.
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
 * sort() (uses custom comparison function if given, '<' operator otherwise),
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
 * zip()
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
 * clock()
 * sleep()
 * utctime()
 * localtime()
 * fmtdate()
 * difftime()
 * parse()
 * parseexpr()
 * compilestr()
 * compileast()
 * exprtofn()
 * toint()
 * tofloat()
 * tonumber()
 * require()
 * dynld()
 * backtrace()
 *
 * Methods:
 * --------
 * Function::call
 * Function::apply
 *
 * Constants:
 * ----------
 * String: the default class for strings
 * Array: the default class for arrays
 * HashMap: the default class for hashmaps
 * Function: the default class for functions
 */

/* A convenience function that loads the part of the standard library
 * which is implemented in native code, in C.
 * It registers some free functions, methods, properties and constants.
 * It also generates the classes of built-in object types such as
 * String, Array, HashMap and Function.
 * Call this at most *once* on each virtual machine instance.
 */
SPN_API void spn_load_native_stdlib(SpnVMachine *vm);

#endif /* SPN_RTLB_H */
