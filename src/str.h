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

#include "api.h"

typedef struct SpnString {
	SpnObject     base;     /* private          */
	char         *cstr;     /* public, readonly */
	size_t        len;      /* public, readonly */
	int           dealloc;  /* private          */
	int           ishashed; /* private          */
	unsigned long hash;     /* private          */
} SpnString;

/* these create an SpnString object. "nocopy" versions don't copy the
 * buffer passed in, others do. "len" versions don't need a 0-terminated
 * string, others do. The 'dealloc' flag should be non-zero if you want the
 * backing buffer to be freed when the destructor runs.
 */
SPN_API	SpnString *spn_string_new(const char *cstr);
SPN_API	SpnString *spn_string_new_nocopy(const char *cstr, int dealloc);
SPN_API	SpnString *spn_string_new_len(const char *cstr, size_t len);
SPN_API	SpnString *spn_string_new_nocopy_len(const char *cstr, size_t len, int dealloc);

/*************** WARNING ***************
 *
 * This constructor only exists so that 'spn_hashmap_get_strkey()'
 * does not need to allocate a new string each time it is called,
 * because it is superfluous and wasteful, and can lead to serious
 * degradation of performance due to the memory fragmentation it
 * may potentially cause.
 *
 * This function takes a pointer to a C string, and returns a string
 * object initialized with it, without allocating it dynamically.
 * The reference count of the returned object is set to the special
 * value UINT_MAX to indicate that it should not be released or retained.
 *
 * In other words, HERE BE DRAGONS. This function should ONLY EVER
 * BE USED by 'spn_hashmap_get_strkey()'. DO NOT USE IT yourself!
 */
SPN_API SpnString spn_string_emplace_nonretained_for_hashmap(const char *cstr);

/* appends rhs to the end of lhs and returns the result.
 * the original strings aren't modified.
 */
SPN_API SpnString *spn_string_concat(SpnString *lhs, SpnString *rhs);

/* The following functions create a formatted string.
 * The format specifiers are documented in doc/stdlib.md.
 */

/* This is a function that is used internally only, e. g. when generating
 * custom error messages in the parser, the compiler and the virtual machine,
 * since there, it's easy to check the number and type of arguments manually
 * before compilation time. It is not advised that you use this function;
 * in order to get meaningful error messages, use 'spn_string_format_obj()'
 * instead.
 */
SPN_API char *spn_string_format_cstr(
	const char *fmt,    /* printf-style format string  */
	size_t *len,        /* on return, length of string */
	const void **argv   /* array of objects to format  */
);

/* this function is used for creating format strings in the Sparkling standard
 * runtime library. It fills in the error message argument if an error is
 * encountered. In this case, '*errmsg' must be free()'d after use.
 * It is strongly encouraged that you use this function only with user-supplied
 * format strings (because the two functions above are inherently unsafe when
 * there's no reliable way to determine the number of arguments a particular
 * format string requires). It's still OK to use the non-checking functions
 * as long as you use constant format strings only and you provide all the
 * necessary format arguments to the formatter functions.
 */
SPN_API SpnString *spn_string_format_obj(
	SpnString *fmt,     /* printf-style format string  */
	int argc,           /* number of objects to format */
	SpnValue *argv,     /* array of objects to format  */
	char **errmsg       /* error description           */
);

/* convenience value constructors and an accessor */
SPN_API SpnValue spn_makestring(const char *s);
SPN_API SpnValue spn_makestring_len(const char *s, size_t len);
SPN_API SpnValue spn_makestring_nocopy(const char *s);
SPN_API SpnValue spn_makestring_nocopy_len(const char *s, size_t len, int dealloc);

#define spn_stringvalue(val) ((SpnString *)((val)->v.o))

#endif /* SPN_STR_H */
