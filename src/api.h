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
#define SPN_API extern
#endif	/* __cplusplus */

/* a VM word is the smallest integer type which is at least 32 bits wide */
#if UINT_MAX >= 0xffffffffu
typedef unsigned int spn_uword;
typedef signed int spn_sword;
#define SPN_UWORD_FMT "u"
#define SPN_SWORD_FMT "d"
#define SPN_UWORD_FMT_HEX "x"
#else
typedef unsigned long spn_uword;
typedef signed long spn_sword;
#define SPN_UWORD_FMT "lu"
#define SPN_SWORD_FMT "ld"
#define SPN_UWORD_FMT_HEX "lx"
#endif

/* it is guaranteed that at least this many octets fit into an 'spn_uword' */
#define SPN_WORD_OCTETS 4

/* The usual 'stringify after macro expansion' trick. */
#define SPN_STRINGIFY_X(x) #x
#define SPN_STRINGIFY_(x) SPN_STRINGIFY_X(x)

/* System directory where some standard library modules are installed */
#define SPN_LIBDIR SPN_STRINGIFY_(SPARKLING_LIBDIR_RAW)

/*
 * Object API
 * Reference-counted objects: construction, memory management, etc.
 */

/* lowest Unique ID available for use to user code
 * Values higher than this are guaranteed not to be
 * used by classes in the Sparkling engine's core.
 */
enum {
	SPN_USER_CLASS_UID_BASE = 0x10000
};

/* A (potentially non-exhaustive) list of class UIDs
 * defined (and used) in the Sparkling core.
 */
enum {
	SPN_CLASS_UID_STRING      = 1,
	SPN_CLASS_UID_ARRAY       = 2,
	SPN_CLASS_UID_HASHMAP     = 3,
	SPN_CLASS_UID_FUNCTION    = 4,
	SPN_CLASS_UID_FILEHANDLE  = 5,
	SPN_CLASS_UID_SYMTABENTRY = 6,
	SPN_CLASS_UID_SYMBOLSTUB  = 7
};

typedef struct SpnClass {
	size_t instsz;                     /* sizeof(instance)                    */
	unsigned long UID;                 /* unique identifier of the class      */
	const char *name;                  /* unique name of the class            */
	int (*equal)(void *, void *);      /* non-zero: equal, zero: different    */
	int (*compare)(void *, void *);    /* -1, +1, 0: lhs is <, >, == to rhs   */
	unsigned long (*hashfn)(void *);   /* cache the hash if immutable!        */
	char *(*description)(void *, int); /* returns normal or debug description */
	void (*destructor)(void *);        /* shouldn't call free on its argument */
} SpnClass;

typedef struct SpnObject {
	const SpnClass *isa;
	unsigned refcnt;
} SpnObject;

/* class membership test */
SPN_API int spn_object_member_of_class(void *obj, const SpnClass *cls);

/* allocates a partially initialized (only the 'isa' and 'refcount' members
 * are set up) object of class 'isa'. The returned instance should go through
 * a dedicated constructor (see e. g. spn_string_new()).
 */
SPN_API void *spn_object_new(const SpnClass *isa);

/* tests objects for equality. two objects are considered equal if they are
 * of the same class, and either their pointers compare equal or they have
 * a non-NULL 'compare' member function which returns nonzero.
 */
SPN_API int spn_object_equal(void *lp, void *rp);

/* ordered comparison of objects. follows the common C idiom:
 * returns -1 if lhs < rhs, 0 if lhs == rhs, 1 if lhs > rhs
 */
SPN_API int spn_object_cmp(void *lp, void *rp);

/* Returns a dynamically allocatd string describing the object instance.
 * If 'debug' is true (nonzero), it returns the debug description.
 * The returned string must be free()'d.
 */
SPN_API char *spn_object_description(SpnObject *obj, int debug);

/* these reference counting functions are called quite often.
 * for the sake of speed, they should probably be inlined. C89 doesn't have
 * inline', though, so we rely on the link-time optimizer to inline them.
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


#define SPN_MASK_TTAG 0x00ff
#define SPN_MASK_FLAG 0xff00

/* basic type tags */
enum {
	SPN_TTAG_NIL,
	SPN_TTAG_BOOL,
	SPN_TTAG_NUMBER,   /* floating-point if 'FLOAT' flag is set */
	SPN_TTAG_RAWPTR,   /* "weak user info"                      */
	SPN_TTAG_OBJECT    /* reference-counted pointer type        */
};

/* additional type information flags */
enum {
	SPN_FLAG_FLOAT  = 1 << 8  /* number is floating-point */
};

/* complete type definitions */
enum {
	SPN_TYPE_NIL                = SPN_TTAG_NIL,
	SPN_TYPE_BOOL               = SPN_TTAG_BOOL,
	SPN_TYPE_INT                = SPN_TTAG_NUMBER,
	SPN_TYPE_FLOAT              = SPN_TTAG_NUMBER | SPN_FLAG_FLOAT,
	SPN_TYPE_RAWPTR             = SPN_TTAG_RAWPTR,
	SPN_TYPE_OBJECT             = SPN_TTAG_OBJECT
};

/* type checking */
#define spn_typetag(t)      ((t) & SPN_MASK_TTAG)
#define spn_typeflag(t)     ((t) & SPN_MASK_FLAG)
#define spn_valtype(val)    (spn_typetag((val)->type))
#define spn_valflag(val)    (spn_typeflag((val)->type))
#define spn_classuid(val)   (((SpnObject *)(val)->v.o)->isa->UID)
#define spn_classname(val)  (((SpnObject *)(val)->v.o)->isa->name)


#define spn_isnil(val)          (spn_valtype(val) == SPN_TTAG_NIL)
#define spn_isbool(val)         (spn_valtype(val) == SPN_TTAG_BOOL)
#define spn_isnumber(val)       (spn_valtype(val) == SPN_TTAG_NUMBER)
#define spn_israwptr(val)       (spn_valtype(val) == SPN_TTAG_RAWPTR)
#define spn_isobject(val)       (spn_valtype(val) == SPN_TTAG_OBJECT)

#define spn_isstring(val)       (spn_isobject(val) && spn_classuid(val) == SPN_CLASS_UID_STRING)
#define spn_isarray(val)        (spn_isobject(val) && spn_classuid(val) == SPN_CLASS_UID_ARRAY)
#define spn_ishashmap(val)      (spn_isobject(val) && spn_classuid(val) == SPN_CLASS_UID_HASHMAP)
#define spn_isfunc(val)         (spn_isobject(val) && spn_classuid(val) == SPN_CLASS_UID_FUNCTION)
#define spn_isfilehandle(val)   (spn_isobject(val) && spn_classuid(val) == SPN_CLASS_UID_FILEHANDLE)
#define spn_issymtabentry(val)  (spn_isobject(val) && spn_classuid(val) == SPN_CLASS_UID_SYMTABENTRY)
#define spn_issymbolstub(val)   (spn_isobject(val) && spn_classuid(val) == SPN_CLASS_UID_SYMBOLSTUB)


#define spn_notnil(val)         (spn_valtype(val) != SPN_TTAG_NIL)
#define spn_isint(val)          (spn_isnumber(val) && ((((val)->type) & SPN_FLAG_FLOAT) == 0))
#define spn_isfloat(val)        (spn_isnumber(val) && ((((val)->type) & SPN_FLAG_FLOAT) != 0))

/* getting the value of a tagged union. These do *not* check the type.
 * More of these macros can be found in headers implementing specific
 * types (strings, arrays, functions).
 */
#define spn_boolvalue(val)  ((val)->v.b)
#define spn_intvalue(val)   ((val)->v.i)
#define spn_floatvalue(val) ((val)->v.f)
#define spn_ptrvalue(val)   ((val)->v.p)
#define spn_objvalue(val)   ((val)->v.o)

typedef struct SpnValue {
	int type;       /* type          */
	union {         /* value union   */
		int    b;   /* Boolean value */
		long   i;   /* integer value */
		double f;   /* float value   */
		void  *p;   /* pointer value */
		void  *o;   /* object value  */
	} v;
} SpnValue;

/* force integer or floating-point number out of an SpnValue.
 * These can potentially be unsafe: a double may not be
 * able to exactly represent all longs, and converting
 * a double which is outside the range of long to a long is
 * undefined behavior.
 */
SPN_API long spn_intvalue_f(const SpnValue *val);
SPN_API double spn_floatvalue_f(const SpnValue *val);

/* Convenience constructors
 * Again, more of these constructors are implemented for each
 * specific object type separately in various header files.
 */
SPN_API SpnValue spn_makebool(int b);
SPN_API SpnValue spn_makeint(long i);
SPN_API SpnValue spn_makefloat(double f);
SPN_API SpnValue spn_makerawptr(void *p);
SPN_API SpnValue spn_makeobject(void *o);

/* 'nil' and Boolean constants */
SPN_API const SpnValue spn_nilval;
SPN_API const SpnValue spn_falseval;
SPN_API const SpnValue spn_trueval;

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

/* prints the user-readable representation of a value to a given stream */
SPN_API void spn_value_print(FILE *stream, const SpnValue *val);
SPN_API void spn_debug_print(FILE *stream, const SpnValue *val);
SPN_API void spn_repl_print(const SpnValue *val);
/* the only exception being REPL, which automatically redirects to stdout */

/* returns a string describing an SPN_TTAG_* type tag */
SPN_API const char *spn_typetag_name(int ttag);

/* returns a string describing the type of an SpnValue */
SPN_API const char *spn_value_type_name(const SpnValue *val);


/*
 * File access API
 * Reading source and bytecode (or any other kind of text/binary) files
 */

/* a convenience function for reading source files into memory */
SPN_API char *spn_read_text_file(const char *name);

/* another convenience function for reading binary files.
 * WARNING: 'sz' represents the file size in bytes. If you are using
 * this function to read compiled Sparkling object files, make sure to
 * divide the returned size by sizeof(spn_uword) in order to obtain
 * the code length in machine words.
 */
SPN_API void *spn_read_binary_file(const char *name, size_t *sz);

#endif /* SPN_API_H */
