/*
 * str.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Object-oriented wrapper for C strings
 */

#ifndef SPN_STR_H
#define SPN_STR_H

#include <stddef.h>

#include "spn.h"
#include "object.h"

typedef struct SpnString {
	SpnObject	 base;		/* private		*/
	char		*cstr;		/* public, readonly	*/
	size_t		 len;		/* public, readonly	*/
	int		 dealloc;	/* private		*/
	int		 ishashed;	/* private		*/
	unsigned long	 hash;		/* private		*/
} SpnString;

/* these create an SpnString object. "nocopy" versions don't copy the
 * buffer passed in, others do. "len" versions don't need a 0-terminated
 * string, others do. The `dealloc` flag should be non-zero if you want the
 * backing buffer to be freed when the destructor runs.
 */
SPN_API	SpnString	*spn_string_new(const char *cstr);
SPN_API	SpnString	*spn_string_new_nocopy(const char *cstr, int dealloc);
SPN_API	SpnString	*spn_string_new_len(const char *cstr, size_t len);
SPN_API	SpnString	*spn_string_new_nocopy_len(const char *cstr, size_t len, int dealloc);

/* appends rhs to the end of lhs and returns the result.
 * the original strings aren't modified.
 */
SPN_API SpnString	*spn_string_concat(SpnString *lhs, SpnString *rhs);

/* Creates a formatted string.
 * The format specifiers are documented in doc/stdlib.md.
 */
SPN_API char *spn_string_format_cstr(
	const char *fmt,	/* printf-style format string	*/
	size_t *len,		/* on return, length of string	*/
	const void **argv	/* array of objects to format	*/
);

SPN_API SpnString *spn_string_format_obj(
	SpnString *fmt,		/* printf-style format string	*/
	SpnValue *argv		/* array of objects to format	*/
);

/* (*): if this flag is zero, then `argv' should point to the first element of
 * an array of void pointers, each of which have a base type appropriate for
 * the corresponding conversion specifier, i. e. one of `long' (for integers),
 * `double' (for floating-point numbers), `const signed char', or `const
 * unsigned char' (for strings), `int' (for Booleans) and `void` for pointers.
 * If the flag is nonzero, then `argv' should point to the first element of an
 * array of `SpnValue' strucs of the appropriate type.
 */

#endif /* SPN_STR_H */

