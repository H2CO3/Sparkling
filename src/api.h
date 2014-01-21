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

/* additional type information flags and masks (0: none) */
enum spn_val_flag {
	SPN_TFLG_OBJECT		= 1 << 0,	/* type is an object type	*/
	SPN_TFLG_FLOAT		= 1 << 1,	/* number is floating-point	*/
	SPN_TFLG_NATIVE		= 1 << 2,	/* function is native		*/
	SPN_TFLG_PENDING	= 1 << 3	/* unresolved (stub) symbol	*/
};

typedef struct SpnValue SpnValue;

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

/* hashing (for generic data and for SpnValue structs) */
SPN_API unsigned long spn_hash(const void *data, size_t n);
SPN_API unsigned long spn_hash_value(const SpnValue *obj);

/* prints the user-readable representation of a value to stdout */
SPN_API void spn_value_print(const SpnValue *val);

/* returns a string describing a particular type */
SPN_API const char *spn_type_name(enum spn_val_type type);


/*
 * Object API
 * Reference-counted objects: construction, memory management, etc.
 */

typedef struct SpnClass {
	const char *name;				/* set once, then read-only		*/
	size_t instsz;					/* sizeof(the_struct)			*/
	int (*equal)(const void *, const void *);	/* non-zero: equal, zero: different	*/
	int (*compare)(const void *, const void *);	/* -1, +1, 0: lhs is <, >, == to rhs	*/
	unsigned long (*hashfn)(void *);		/* cache the hash if immutable!		*/
	void (*destructor)(void *);			/* should call free() on its argument	*/
} SpnClass;

typedef struct SpnObject {
	const SpnClass *isa;
	unsigned refcnt;
} SpnObject;

/* allocates a partially uninitialized (only the `isa` and `refcount` members
 * are set up) object of clas `isa`. The returned instance should go through
 * a dedicated constructor (see e. g. spn_string_new()).
 */
SPN_API void *spn_object_new(const SpnClass *isa);

/* returns o->isa->name */
SPN_API const char *spn_object_type(const void *o); /* for typeof() */

/* tests objects for equality. two objects are considered equal if they are
 * of the same class, and either their pointers compare equal or they have
 * a non-NULL `compare` member function which returns nonzero.
 */
SPN_API int spn_object_equal(const void *lhs, const void *rhs);

/* ordered comparison of objects. follows the common C idiom:
 * returns -1 if lhs < rhs, 0 if lhs == rhs, 1 if lhs > rhs
 */
SPN_API int spn_object_cmp(const void *lhs, const void *rhs);

/* these reference counting functions are called quite often.
 * for the sake of speed, they should probably be inlined. C89 doesn't have
 * `inline`, though, so we rely on the linker being able to inline them.
 */

/* increments the reference count of an object */
SPN_API void spn_object_retain(void *o);

/* decrements the reference count of an object.
 * calls the destructor and frees the instance if its reference count
 * drops to 0.
 */
SPN_API void spn_object_release(void *o);


/*
 * File access API
 * Reading source and bytecode (or any other text/binary) files
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

