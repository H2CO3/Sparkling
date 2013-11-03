/*
 * spn.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Public parts of the Sparkling API
 */

#ifndef SPN_SPN_H
#define SPN_SPN_H

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "object.h"

#ifdef __cplusplus
#define SPN_API extern "C"
#else	/* __cplusplus */
#define SPN_API
#endif	/* __cplusplus */


/* a VM word is the smallest integer type which is at least 32 bits wide */
#if UINT_MAX >= 0xffffffffu
typedef unsigned int spn_uword;
typedef signed int spn_sword;
#define SPN_UWORD_FMT "u"
#define SPN_SWORD_FMT "d"
#else
typedef unsigned long spn_uword;
typedef signed long spn_sword;
#define SPN_UWORD_FMT "lu"
#define SPN_SWORD_FMT "ld"
#endif

/* it is guaranteed that at least this many octets fit into an `spn_uword` */
#define SPN_WORD_OCTETS 4


/* 
 * Value API
 * (reference-counted generic values and corresponding types)
 */

/* SPN_TFLG_PENDING denotes an unresolved reference to a global function.
 * This type is to be used exclusively in the local symbol table.
 * A call to such a function makes the Sparkling virtual machine attempt
 * to resolve the reference, and if it succeeds, it updates
 * the symbol in the local symtab, then it calls the function.
 * If the symbol cannot be resolved, a runtime error is generated.
 */

/* main types */
enum spn_val_type {
	SPN_TYPE_NIL,
	SPN_TYPE_BOOL,
	SPN_TYPE_NUMBER,	/* floating-point if `FLOAT' flag is iset	*/
	SPN_TYPE_FUNC,
	SPN_TYPE_STRING,
	SPN_TYPE_ARRAY,
	SPN_TYPE_USRDAT		/* strong pointer when `OBJECT' flag is set	*/
};

typedef struct SpnValue SpnValue;

/* additional type information flags and masks (0: none) */
enum spn_val_flag {
	SPN_TFLG_OBJECT		= 1 << 0,	/* type is an object type	*/
	SPN_TFLG_FLOAT		= 1 << 1,	/* number is floating-point	*/
	SPN_TFLG_NATIVE		= 1 << 2,	/* function is native		*/
	SPN_TFLG_PENDING	= 1 << 3	/* unresolved (stub) symbol	*/
};

/* `symtabidx' is the index of the local symbol table
 * which represents the environment of the function
 */
struct SpnValue {
	union {
		int boolv;			/* Boolean value  */
		long intv;			/* integer value  */
		double fltv;			/* float value	  */
		void *ptrv;			/* object value	  */
		struct {
			const char *name;	/* function name  */
			int symtabidx;		/* environment	  */
			union {
				int (*fn)(SpnValue *, int, SpnValue *, void *);
				spn_uword *bc;
			} r;			/* representation */
		} fnv;				/* function value */
	} v;					/* value union	  */
	enum spn_val_type t;			/* type	tag	  */
	enum spn_val_flag f;			/* extra flags	  */
};

/* reference counting */
SPN_API void spn_value_retain(SpnValue *val);
SPN_API void spn_value_release(SpnValue *val);

/* testing values for (in)equality */
SPN_API int spn_value_equal(const SpnValue *lhs, const SpnValue *rhs);
SPN_API int spn_value_noteq(const SpnValue *lhs, const SpnValue *rhs);

/* ordered comparison */

/* prints a user-readable representation of a value to stdout */
SPN_API void spn_value_print(const SpnValue *val);

/* a convenience function for reading source files into memory */
SPN_API char *spn_read_text_file(const char *name);

/* another convenience function for reading binary files.
 * WARNING: `sz' represents the file size in bytes. If you are using
 * this function to read Sparkling compiled object files, make sure to
 * divide the returned size by sizeof(spn_uword) in order to obtain
 * the code length in machine words.
 */
SPN_API void *spn_read_binary_file(const char *name, size_t *sz);

#endif /* SPN_SPN_H */

