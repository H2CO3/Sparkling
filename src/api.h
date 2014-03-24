/*
 * api.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Public parts of the Sparkling API
 */

#ifndef SPN_API_H
#define SPN_API_H

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

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
 * Object API
 * Reference-counted objects: construction, memory management, etc.
 */

typedef struct SpnClass {
	size_t instsz;				/* sizeof(instance)			*/
	int (*equal)(void *, void *);		/* non-zero: equal, zero: different	*/
	int (*compare)(void *, void *);		/* -1, +1, 0: lhs is <, >, == to rhs	*/
	unsigned long (*hashfn)(void *);	/* cache the hash if immutable!		*/
	void (*destructor)(void *);		/* shouldn't call free on its argument	*/
} SpnClass;

typedef struct SpnObject {
	const SpnClass *isa;
	unsigned refcnt;
} SpnObject;

/* allocates a partially initialized (only the `isa` and `refcount` members
 * are set up) object of class `isa`. The returned instance should go through
 * a dedicated constructor (see e. g. spn_string_new()).
 */
SPN_API void *spn_object_new(const SpnClass *isa);

/* tests objects for equality. two objects are considered equal if they are
 * of the same class, and either their pointers compare equal or they have
 * a non-NULL `compare` member function which returns nonzero.
 */
SPN_API int spn_object_equal(void *lp, void *rp);

/* ordered comparison of objects. follows the common C idiom:
 * returns -1 if lhs < rhs, 0 if lhs == rhs, 1 if lhs > rhs
 */
SPN_API int spn_object_cmp(void *lp, void *rp);

/* these reference counting functions are called quite often.
 * for the sake of speed, they should probably be inlined. C89 doesn't have
 * `inline`, though, so we rely on the link-time optimizer to inline them.
 */

/* increments the reference count of an object */
SPN_API void spn_object_retain(void *o);

/* decrements the reference count of an object.
 * calls the destructor and frees the instance if its reference count
 * drops to 0.
 */
SPN_API void spn_object_release(void *o);


/* 
 * Value API
 * (reference-counted generic values and corresponding types)
 */

/* SPN_TFLG_PENDING denotes an unresolved reference to a global symbol.
 * This type is to be used exclusively in the local symbol table.
 * Any reference to such a symbol makes the Sparkling virtual machine attempt
 * to resolve the reference, and if it succeeds, it updates the symbol in the
 * local symbol table, then it loads the value as usual.
 * If the symbol cannot be resolved, a runtime error is generated.
 */


#define SPN_MASK_TTAG 0x00ff
#define SPN_MASK_FLAG 0xff00

/* basic type tags */
enum {
	SPN_TTAG_NIL,
	SPN_TTAG_BOOL,
	SPN_TTAG_NUMBER,	/* floating-point if `FLOAT' flag is iset	*/
	SPN_TTAG_STRING,
	SPN_TTAG_ARRAY,
	SPN_TTAG_FUNC,
	SPN_TTAG_USERINFO	/* strong pointer when `OBJECT' flag is set	*/
};

/* additional type information flags */
enum {
	SPN_FLAG_OBJECT		= 1 << 8,	/* type is an object type	*/
	SPN_FLAG_FLOAT		= 1 << 9,	/* number is floating-point	*/
	SPN_FLAG_PENDING	= 1 << 10	/* value is pending. XXX: this should be removed ASAP! */
};

/* complete type definitions */
#define SPN_TYPE_NIL		 SPN_TTAG_NIL
#define SPN_TYPE_BOOL		 SPN_TTAG_BOOL
#define SPN_TYPE_INT		 SPN_TTAG_NUMBER
#define SPN_TYPE_FLOAT		(SPN_TTAG_NUMBER   | SPN_FLAG_FLOAT)
#define SPN_TYPE_FUNC		(SPN_TTAG_FUNC     | SPN_FLAG_OBJECT)
#define SPN_TYPE_STRING		(SPN_TTAG_STRING   | SPN_FLAG_OBJECT)
#define SPN_TYPE_ARRAY		(SPN_TTAG_ARRAY    | SPN_FLAG_OBJECT)
#define SPN_TYPE_WEAKUSERINFO	 SPN_TTAG_USERINFO
#define SPN_TYPE_STRGUSERINFO	(SPN_TTAG_USERINFO | SPN_FLAG_OBJECT)

/* type checking */
#define spn_isobject(val)	((((val)->type) & SPN_FLAG_OBJECT) != 0)
#define spn_typetag(t)		((t) & SPN_MASK_TTAG)
#define spn_typeflag(t)		((t) & SPN_MASK_FLAG)
#define spn_valtype(val)	spn_typetag((val)->type)
#define spn_valflag(val)	spn_typeflag((val)->type)

#define spn_isnil(val)		(spn_valtype(val) == SPN_TTAG_NIL)
#define spn_isbool(val)		(spn_valtype(val) == SPN_TTAG_BOOL)
#define spn_isnumber(val)	(spn_valtype(val) == SPN_TTAG_NUMBER)
#define spn_isstring(val)	(spn_valtype(val) == SPN_TTAG_STRING)
#define spn_isarray(val)	(spn_valtype(val) == SPN_TTAG_ARRAY)
#define spn_isfunc(val)		(spn_valtype(val) == SPN_TTAG_FUNC)
#define spn_isuserinfo(val)	(spn_valtype(val) == SPN_TTAG_USERINFO)

#define spn_isint(val)		(spn_isnumber(val) && ((((val)->type) & SPN_FLAG_FLOAT) == 0))
#define spn_isfloat(val)	(spn_isnumber(val) && ((((val)->type) & SPN_FLAG_FLOAT) != 0))
#define spn_isweakuserinfo(val)	(spn_isuserinfo(val) && !spn_isobject(val))
#define spn_isstrguserinfo(val)	(spn_isuserinfo(val) &&  spn_isobject(val))

/* getting the value of a tagged union. These do *not* check the type.
 * More of these macros can be found in headers implementing specific
 * types (strings, arrays, functions).
 */
#define spn_boolvalue(val)	((val)->v.b)
#define spn_intvalue(val)	((val)->v.i)
#define spn_floatvalue(val)	((val)->v.f)
#define spn_ptrvalue(val)	((val)->v.p)
#define spn_objvalue(val)	((val)->v.o)

typedef struct SpnValue {
	int type;		/* type		 */
	union {			/* value union	 */
		int	 b;	/* Boolean value */
		long	 i;	/* integer value */
		double	 f;	/* float value	 */
		void	*p;	/* user info	 */
		void	*o;	/* object value	 */
	} v;
} SpnValue;

/* Convenience constructors
 * Again, more of these constructors are implemented for each
 * specific object type separately in various header files.
 */
SPN_API SpnValue spn_makenil(void);
SPN_API SpnValue spn_makebool(int b);
SPN_API SpnValue spn_makeint(long i);
SPN_API SpnValue spn_makefloat(double f);
SPN_API SpnValue spn_makeweakuserinfo(void *p);
SPN_API SpnValue spn_makestrguserinfo(void *o);

/* reference counting */
SPN_API void spn_value_retain(const SpnValue *val);
SPN_API void spn_value_release(const SpnValue *val);

/* testing values for (in)equality */
SPN_API int spn_value_equal(const SpnValue *lhs, const SpnValue *rhs);
SPN_API int spn_value_noteq(const SpnValue *lhs, const SpnValue *rhs);

/* ordered comparison of values. The two value objects must be comparable
 * (i. e. both of them must either be numbers or comparable objects)
 * returns -1 if lhs < rhs, 0 if lhs == rhs, 1 if lhs > rhs
 */
SPN_API int spn_value_compare(const SpnValue *lhs, const SpnValue *rhs);

/* returns non-zero if ordered comparison of the two values makes sense */
SPN_API int spn_values_comparable(const SpnValue *lhs, const SpnValue *rhs);

/* hashing (for generic data and for SpnValue structs) */
SPN_API unsigned long spn_hash_bytes(const void *data, size_t n);
SPN_API unsigned long spn_hash_value(const SpnValue *obj);

/* prints the user-readable representation of a value to stdout */
SPN_API void spn_value_print(const SpnValue *val);

/* returns a string describing a particular type */
SPN_API const char *spn_type_name(int type);


/*
 * File access API
 * Reading source and bytecode (or any other kind of text/binary) files
 */

/* a convenience function for reading source files into memory */
SPN_API char *spn_read_text_file(const char *name);

/* another convenience function for reading binary files.
 * WARNING: `sz' represents the file size in bytes. If you are using
 * this function to read compiled Sparkling object files, make sure to
 * divide the returned size by sizeof(spn_uword) in order to obtain
 * the code length in machine words.
 */
SPN_API void *spn_read_binary_file(const char *name, size_t *sz);


#endif /* SPN_API_H */

