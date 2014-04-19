/*
 * str.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Object-oriented wrapper for C strings
 */

#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>

#include "str.h"
#include "array.h"
#include "private.h"


static int compare_strings(void *lhs, void *rhs);
static int equal_strings(void *lhs, void *rhs);
static void free_string(void *obj);
static unsigned long hash_string(void *obj);

static const SpnClass spn_class_string = {
	sizeof(SpnString),
	equal_strings,
	compare_strings,
	hash_string,
	free_string
};

static void free_string(void *obj)
{
	SpnString *str = obj;

	if (str->dealloc) {
		free(str->cstr);
	}
}

static int compare_strings(void *lp, void *rp)
{
	SpnString *lhs = lp, *rhs = rp;
	return strcmp(lhs->cstr, rhs->cstr);
}

static int equal_strings(void *lp, void *rp)
{
	SpnString *lhs = lp, *rhs = rp;
	return strcmp(lhs->cstr, rhs->cstr) == 0;
}

/* since strings are immutable, it's enough to generate the hash on-demand,
 * then store it for later use.
 */
static unsigned long hash_string(void *obj)
{
	SpnString *str = obj;

	if (!str->ishashed) {
		str->hash = spn_hash_bytes(str->cstr, str->len);
		str->ishashed = 1;
	}

	return str->hash;
}


SpnString *spn_string_new(const char *cstr)
{
	return spn_string_new_len(cstr, strlen(cstr));
}

SpnString *spn_string_new_nocopy(const char *cstr, int dealloc)
{
	return spn_string_new_nocopy_len(cstr, strlen(cstr), dealloc);
}

SpnString *spn_string_new_len(const char *cstr, size_t len)
{
	char *buf = spn_malloc(len + 1);
	memcpy(buf, cstr, len); /* so that strings can hold binary data */
	buf[len] = 0;

	return spn_string_new_nocopy_len(buf, len, 1);
}

SpnString *spn_string_new_nocopy_len(const char *cstr, size_t len, int dealloc)
{
	SpnString *str = spn_object_new(&spn_class_string);

	str->dealloc = dealloc;
	str->len = len;
	str->cstr = (char *)(cstr);
	str->ishashed = 0;

	return str;
}

SpnString *spn_string_concat(SpnString *lhs, SpnString *rhs)
{
	size_t len = lhs->len + rhs->len;
	char *buf = spn_malloc(len + 1);

	memcpy(buf, lhs->cstr, lhs->len);
	memcpy(buf + lhs->len, rhs->cstr, rhs->len);
	buf[len] = 0;

	return spn_string_new_nocopy_len(buf, len, 1);
}

/*********************************************
 * Creating printf()-style formatted strings *
 *********************************************/

struct string_builder {
	size_t len;
	size_t allocsz;
	char *buf;
};

static void init_builder(struct string_builder *bld)
{
	bld->len = 0;
	bld->allocsz = 0x10;
	bld->buf = spn_malloc(bld->allocsz);
}

static void expand_buffer(struct string_builder *bld, size_t extra)
{
	if (bld->allocsz < bld->len + extra) {
		while (bld->allocsz < bld->len + extra) {
			bld->allocsz *= 2;
		}

		bld->buf = spn_realloc(bld->buf, bld->allocsz);
	}
}

/* appends a literal string */
static void append_string(struct string_builder *bld, const char *str, size_t len)
{
	expand_buffer(bld, len);
	memcpy(bld->buf + bld->len, str, len);
	bld->len += len;
}


/* an upper bound for number of characters required to print an integer:
 * number of bits, +1 for sign, +2 for base prefix
 */
#define PR_LONG_DIGITS (sizeof(long) * CHAR_BIT + 3)

enum format_flags {
	FLAG_ZEROPAD		= 1 << 0, /* pad field width with zeroes, not spaces	*/
	FLAG_NEGATIVE		= 1 << 1, /* the number to be printed is negative	*/
	FLAG_EXPLICITSIGN	= 1 << 2, /* always print '+' or '-' sign		*/
	FLAG_PADSIGN		= 1 << 3, /* prepend space if negative			*/
	FLAG_EXPONENTSIGN	= 1 << 4, /* explicitly signed exponent (+/-e...)	*/
	FLAG_BASEPREFIX		= 1 << 5, /* prepend "0b", "0" or "0x" prefix		*/
	FLAG_CAPS		= 1 << 6  /* for hex: use 'A'..'Z' instead of 'a'..'z';
					   * for `%e': use 'E' instead of 'e'		*/
};

/* XXX: this should really be an inline function */
#define APPEND_BASE_PREFIX(bs, bg, caps)		\
	do {						\
		switch (bs) {				\
		case 2:					\
			*--bg = 'b';			\
			*--bg = '0';			\
			break;				\
		case 8:					\
			*--bg = '0';			\
			break;				\
		case 10:				\
			break;				\
		case 16:				\
			*--bg = (caps) ? 'X' : 'x';	\
			*--bg = '0';			\
			break;				\
		default:				\
			abort();			\
			break;				\
		}					\
	} while(0)

static int prefix_length(unsigned base)
{
	switch (base) {
	case 2:  return 2; /* "0b" */
	case 8:  return 1; /* "0"  */
	case 10: return 0; /* none */
	case 16: return 2; /* "0x" */
	default: abort(); return -1;
	}
}

static const char *digitset(unsigned base, int caps)
{
	switch (base) {
	case 2:  return "01";
	case 8:  return "01234567";
	case 10: return "0123456789";
	case 16: return caps ? "0123456789ABCDEF" : "0123456789abcdef";
	default: abort(); return NULL;
	}
}

static unsigned base_for_specifier(char spec)
{
	switch (spec) {
	case 'i': /* fallthru */
	case 'd': /* fallthru */
	case 'u': return 10;
	case 'b': return  2;
	case 'o': return  8;
	case 'x': /* fallthru */
	case 'X': return 16;
	default : abort(); return 0;
	}
}

struct format_args {
	enum format_flags flags;
	int width;
	int precision;
	char spec;
};

static void init_format_args(struct format_args *args)
{
	args->flags = 0;
	args->width = -1;
	args->precision = -1;
	args->spec = 0;
}

static const void *getarg_raw(void *argv, int *argidx)
{
	const void **real_argv = argv;
	const void *arg = real_argv[*argidx];
	++*argidx;
	return arg;
}

static SpnValue *getarg_val(void *argv, int *argidx)
{
	SpnValue *real_argv = argv;
	SpnValue *arg = &real_argv[*argidx];
	++*argidx;
	return arg;
}


/* I've learnt this technique from Apple's vfprintf() (BSD libc heritage?) */
static char *ulong2str(
	char *end,
	unsigned long n,
	unsigned base,
	int width,
	enum format_flags flags
)
{
	char *begin = end;
	const char *digits = digitset(base, flags & FLAG_CAPS);
	char s; /* sign character (if any) or 0 */

	do {
		*--begin = digits[n % base];
		n /= base;
	} while (n > 0);

	if (flags & FLAG_NEGATIVE) {
		s = '-';
	} else if (flags & FLAG_EXPLICITSIGN) {
		s = '+';
	} else if (flags & FLAG_PADSIGN) {
		s = ' ';
	} else {
		s = 0;
	}

	if (flags & FLAG_ZEROPAD) {
		while (width > end - begin + 1 + prefix_length(base)) {
			*--begin = '0';
		}

		if (s != 0) {
			*--begin = s;
		} else {
			/* if there's no sign, fill first place with zeroes */
			if (width > end - begin + prefix_length(base)) {
				*--begin = '0';
			}
		}

		if (flags & FLAG_BASEPREFIX) {
			/* if base prefix is present, there shall be no sign */
			assert(s == 0);
			APPEND_BASE_PREFIX(base, begin, flags & FLAG_CAPS);
		} else {
			/* if there's no base prefix, fill rest with zeroes */
			int i;
			for (i = 0; i < prefix_length(base); i++) {
				if (width > end - begin) {
					*--begin = '0';
				}
			}
		}
	} else {
		if (s != 0) {
			*--begin = s;
		}

		if (flags & FLAG_BASEPREFIX) {
			/* if base prefix is present, there shall be no sign */
			assert(s == 0);
			APPEND_BASE_PREFIX(base, begin, flags & FLAG_CAPS);
		}

		while (width > end - begin) {
			*--begin = ' ';
		}
	}

	return begin;
}

/* helper for print_special_fp(): appends string, pads with space if needed */
static void append_padded(struct string_builder *bld, const char *str, int width)
{
	size_t len = strlen(str);
	size_t tmpwidth = width < 0 ? 0 : width;

	if (tmpwidth > len) {
		size_t pad = tmpwidth - len;
		expand_buffer(bld, pad);
		while (pad--) {
			bld->buf[bld->len++] = ' ';
		}
	}

	append_string(bld, str, len);
}

/* appends the formatted string representing a special floating-point number */
static void print_special_fp(
	struct string_builder *bld,
	enum format_flags flags,
	int width,
	double x
)
{
	if (x != x) {
		/* NaN */
		const char *str = flags & FLAG_CAPS ? "NAN" : "nan";
		append_padded(bld, str, width);
	} else if (x == +1.0 / 0.0) {
		/* positive infinity */
		const char *str;

		if (flags & FLAG_EXPLICITSIGN) {
			str = flags & FLAG_CAPS ? "+INF" : "+inf";
		} else if (flags & FLAG_PADSIGN) {
			str = flags & FLAG_CAPS ? " INF" : " inf";
		} else {
			str = flags & FLAG_CAPS ? "INF" : "inf";
		}

		append_padded(bld, str, width);
	} else if (x == -1.0 / 0.0) {
		/* negative infinity */
		const char *str = flags & FLAG_CAPS ? "-INF" : "-inf";
		append_padded(bld, str, width);
	}
}

/* this takes only non-negative (except -0) numbers that are not NaN or inf */
static char *double2str(
	char *end,
	double x,
	int width,
	int prec,
	enum format_flags flags
)
{
	char *begin = end;
	double frac, whole;
	frac = modf(x, &whole);

	if (prec > 0) {
		/* round fractional part */
		int i;

		for (i = 0; i < prec; i++) {
			frac *= 10.0;
		}

		frac = floor(frac + 0.5);

		for (i = 0; i < prec; i++) {
			int digit = fmod(frac, 10.0);
			*--begin = '0' + digit;
			frac /= 10.0;
		}

		*--begin = '.';

		whole += frac;
	} else {
		/* round to integers */
		whole = floor(x + 0.5);
	}

	do {
		int digit = fmod(whole, 10.0);
		*--begin = '0' + digit;
		whole /= 10.0;
	} while (whole >= 1.0);

	if (flags & FLAG_ZEROPAD) {
		while (width >= 0 && width > end - begin + 1) {
			*--begin = '0';
		}

		if (flags & FLAG_NEGATIVE) {
			*--begin = '-';
		} else if (flags & FLAG_EXPLICITSIGN) {
			*--begin = '+';
		} else if (flags & FLAG_PADSIGN) {
			*--begin = ' ';
		} else {
			*--begin = '0';
		}
	} else {
		if (flags & FLAG_NEGATIVE) {
			*--begin = '-';
		} else if (flags & FLAG_EXPLICITSIGN) {
			*--begin = '+';
		} else if (flags & FLAG_PADSIGN) {
			*--begin = ' ';
		}

		while (width >= 0 && width > end - begin) {
			*--begin = ' ';
		}
	}

	return begin;
}


enum format_error_kind {
	TYPE_MISMATCH,
	EXPECT_INTEGER,
	INVALID_SPECIFIER,
	OUT_OF_ARGUMENTS
};

/* helper function for emitting meaningful error messages */
static void format_errmsg(char **msg, enum format_error_kind kind, int argidx, ...)
{
	va_list args;

	if (msg == NULL) {
		return;
	}

	va_start(args, argidx);

	switch (kind) {
	case TYPE_MISMATCH: {
		int expect = va_arg(args, int);
		int actual = va_arg(args, int);

		const void *argv[3];
		argv[0] = &argidx;
		argv[1] = spn_type_name(expect);
		argv[2] = spn_type_name(actual);

		*msg = spn_string_format_cstr("type mismatch in argument %i: expected %s, got %s", NULL, argv);
		break;
	}
	case EXPECT_INTEGER: {
		const void *argv[1];
		argv[0] = &argidx;
		*msg = spn_string_format_cstr("type mismatch in argument %i: expected integer, got floating-point", NULL, argv);
		break;
	}
	case INVALID_SPECIFIER: {
		long ch = va_arg(args, int); /* `char' is promoted to `int' */

		const void *argv[2];
		argv[0] = &ch;
		argv[1] = &argidx;
		*msg = spn_string_format_cstr("invalid format specifier `%%%c' at index %i", NULL, argv);
		break;
	}
	case OUT_OF_ARGUMENTS: {
		const void *argv[1];
		argv[0] = &argidx;
		*msg = spn_string_format_cstr("too few arguments (%i)", NULL, argv);
		break;
	}
	default:
		SHANT_BE_REACHED();
	}

	va_end(args);
}

/* returns zero on success, nonzero on error */
static int append_format(
	struct string_builder *bld,
	const struct format_args *args,
	void *argv,
	int *argidx,
	int isval,
	char **errmsg
)
{
	switch (args->spec) {
	case '%':
		append_string(bld, "%", 1);
		break;
	case 's': {
		const char *str;
		size_t len;

		if (isval) {
			SpnString *strobj;

			/* must be a string */
			SpnValue *val = getarg_val(argv, argidx);
			if (!isstring(val)) {
				format_errmsg(
					errmsg,
					TYPE_MISMATCH,
					*argidx,
					SPN_TYPE_STRING,
					val->type
				);
				return -1;
			}

			strobj = stringvalue(val);
			str = strobj->cstr;
			len = strobj->len;
		} else {
			str = getarg_raw(argv, argidx);
			len = strlen(str);
		}

		if (args->precision >= 0 && args->precision < len) {
			len = args->precision;
		}

		if (args->width >= 0 && args->width > len) {
			size_t pad = args->width - len;
			expand_buffer(bld, pad);

			while (pad-- > 0) {
				bld->buf[bld->len++] = ' ';
			}
		}

		append_string(bld, str, len);
		break;
	}
	case 'i':
	case 'd':
	case 'b':
	case 'o':
	case 'u':
	case 'x':
	case 'X': {
		char *buf, *end, *begin;
		size_t len = PR_LONG_DIGITS;
		enum format_flags flags = args->flags;
		unsigned base = base_for_specifier(args->spec);
		long n;
		unsigned long u;

		if (isval) {
			/* must be a number */
			SpnValue *val = getarg_val(argv, argidx);
			if (!isnumber(val)) {
				format_errmsg(
					errmsg,
					TYPE_MISMATCH,
					*argidx,
					SPN_TTAG_NUMBER,
					val->type
				);
				return -1;
			}

			if (isint(val)) {
				n = intvalue(val);
			} else {
				n = floatvalue(val); /* truncate */
			}
		} else {
			/* "%i" expects an int, others expect a long */
			if (args->spec == 'i') {
				n = *(const int *)getarg_raw(argv, argidx);
			} else {
				n = *(const long *)getarg_raw(argv, argidx);
			}
		}

		if (args->spec == 'i' || args->spec == 'd') {
			/* signed conversion specifiers */
			if (n < 0) {
				flags |= FLAG_NEGATIVE;
				u = -n;
			} else {
				u = n;
			}
		} else {
			/* unsigned conversion specifiers */
			u = n;
		}

		if (args->spec == 'X') {
			flags |= FLAG_CAPS;
		}

		if (args->width >= 0 && args->width > len) {
			len = args->width;
		}

		buf = spn_malloc(len);
		end = buf + len;
		begin = ulong2str(end, u, base, args->width, flags);

		assert(buf <= begin);
		append_string(bld, begin, end - begin);
		free(buf);

		break;
	}
	case 'c': {
		unsigned char ch;
		int len = 1; /* one character is one character long... */

		if (isval) {
			/* must be an integer */
			SpnValue *val = getarg_val(argv, argidx);

			if (!isnumber(val)) {
				format_errmsg(
					errmsg,
					TYPE_MISMATCH,
					*argidx,
					SPN_TTAG_NUMBER,
					val->type
				);
				return -1;
			}

			if (isfloat(val)) {
				format_errmsg(errmsg, EXPECT_INTEGER, *argidx);
				return -1;
			}

			ch = intvalue(val);
		} else {
			ch = *(const long *)getarg_raw(argv, argidx);
		}

		if (args->width > len) {
			len = args->width;
		}

		expand_buffer(bld, len);

		while (len-- > 1) {
			bld->buf[bld->len++] = ' ';
		}

		bld->buf[bld->len++] = ch;

		break;
	}
	case 'f':
	case 'F': {
		char *buf, *end, *begin;
		size_t len;
		int prec;
		double x;
		enum format_flags flags = args->flags;

		if (isval) {
			SpnValue *val = getarg_val(argv, argidx);
			if (!isnumber(val)) {
				format_errmsg(
					errmsg,
					TYPE_MISMATCH,
					*argidx,
					SPN_TTAG_NUMBER,
					val->type
				);
				return -1;
			}

			if (isfloat(val)) {
				x = floatvalue(val);
			} else {
				x = intvalue(val);
			}
		} else {
			x = *(const double *)getarg_raw(argv, argidx);
		}

		if (args->spec == 'F') {
			flags |= FLAG_CAPS;
		}

		/* handle special cases */
		if (+1.0 / x == +1.0 / -0.0) {
			/* negative zero: set sign flag and carry on */
			flags |= FLAG_NEGATIVE;
		} else if (
			x != x		/*  NaN */
		     || x == +1.0 / 0.0	/* +inf */
		     || x == -1.0 / 0.0	/* -inf */
		) {
			print_special_fp(bld, flags, args->width, x);
			break;
		}

		if (x < 0.0) {
			flags |= FLAG_NEGATIVE;
			x = -x;
		}

		/* at this point, `x' is non-negative or -0 */

		if (x >= 1.0) {
			len = ceil(log10(x)) + 1; /* 10 ^ n is n + 1 digits long */
		} else {
			len = 1; /* leading zero needs exactly one character */
		}

		prec = args->precision < 0 ? DBL_DIG : args->precision;

		len += prec + 3; /* decimal point, sign, leading zero */

		if (args->width >= 0 && args->width > len) {
			len = args->width;
		}

		buf = spn_malloc(len);
		end = buf + len;
		begin = double2str(end, x, args->width, prec, flags);

		assert(buf <= begin);
		append_string(bld, begin, end - begin);
		free(buf);

		break;
	}
	case 'B': {
		int boolval;
		const char *str;
		size_t len;

		if (isval) {
			/* must be a boolean */
			SpnValue *val = getarg_val(argv, argidx);
			if (!isbool(val)) {
				format_errmsg(
					errmsg,
					TYPE_MISMATCH,
					*argidx,
					SPN_TTAG_BOOL,
					val->type
				);
				return -1;
			}

			boolval = boolvalue(val);
		} else {
			boolval = *(const int *)getarg_raw(argv, argidx);
		}

		str = boolval ? "true" : "false";
		len = strlen(str);

		if (args->precision >= 0 && args->precision < len) {
			len = args->precision;
		}

		if (args->width >= 0 && args->width > len) {
			size_t pad = args->width - len;
			expand_buffer(bld, pad);

			while (pad-- > 0) {
				bld->buf[bld->len++] = ' ';
			}
		}

		append_string(bld, str, len);
		break;
	}
	default:
		format_errmsg(errmsg, INVALID_SPECIFIER, ++*argidx, args->spec);
		return -1;
	}

	return 0;
}


/* the actual string format parser
 * Although it's not in the documentation of `printf()`, but in addition to the
 * `%d` conversion specifier, this supports `%i`, which takes an `int` argument
 * instead of a `long`. It is used only for formatting error messages (since
 * Sparkling integers are all `long`s), but feel free to use it yourself.
 *
 * if `errmsg' is not a NULL pointer, and an error occurred while creating the
 * format string, then on return, `*errmsg' will point to a string containing
 * a message that describes the error.
 */
static char *make_format_string(
	const char *fmt,
	size_t *len,
	int argc,
	void *argv,
	int isval,
	char **errmsg
)
{
	struct string_builder bld;
	int argidx = 0;
	const char *s = fmt;
	const char *p = s;	/* points to the beginning of the next
				 * non-format part of the format string
				 */

	init_builder(&bld);

	while (*s) {
		if (*s == '%') {
			struct format_args args;
			init_format_args(&args);

			/* append preceding non-format string chunk */
			if (s > p) {
				append_string(&bld, p, s - p);
			}

			s++;

			/* Actually parse the format string.
			 * '#' flag: prepend base prefix (0b, 0, 0x)
			 */
			if (*s == '#') {
				args.flags |= FLAG_BASEPREFIX;
				s++;
			}

			/* ' ' (space) flag: prepend space if non-negative
			 * '+' flag: always prepend explicit + or - sign
			 */
			if (*s == ' ') {
				args.flags |= FLAG_PADSIGN;
				s++;
			} else if (*s == '+') {
				args.flags |= FLAG_EXPLICITSIGN;
				s++;
			}

			/* leading 0 flag: pad field with zeroes */
			if (*s == '0') {
				args.flags |= FLAG_ZEROPAD;
				s++;
			}

			/* field width specifier */
			if (isdigit(*s)) {
				args.width = 0;
				while (isdigit(*s)) {
					args.width *= 10;
					args.width += *s++ - '0';
				}
			} else if (*s == '*') {
				s++;
				if (isval) {
					SpnValue *widthptr;

					/* check argc if the caller wants us to do so */
					if (argc >= 0 && argidx >= argc) {
						format_errmsg(errmsg, OUT_OF_ARGUMENTS, argidx);
						free(bld.buf);
						return NULL;
					}

					/* width specifier must be an integer */
					widthptr = getarg_val(argv, &argidx);
					if (!isnumber(widthptr)) {
					 	format_errmsg(
					 		errmsg,
					 		TYPE_MISMATCH,
					 		argidx,
					 		SPN_TTAG_NUMBER,
					 		widthptr->type
					 	);
						free(bld.buf);
						return NULL;
					}

					if (isfloat(widthptr)) {
				 		format_errmsg(
				 			errmsg,
				 			EXPECT_INTEGER,
				 			argidx
				 		);
						free(bld.buf);
						return NULL;
					}

					args.width = intvalue(widthptr);
				} else {
					const int *widthptr = getarg_raw(argv, &argidx);
					args.width = *widthptr;
				}
			}

			/* precision/maximal length specifier */
			if (*s == '.') {
				s++;

				if (*s == '+') {
					args.flags |= FLAG_EXPONENTSIGN;
					s++;
				}

				args.precision = 0;
				if (isdigit(*s)) {
					while (isdigit(*s)) {
						args.precision *= 10;
						args.precision += *s++ - '0';
					}
				} else if (*s == '*') {
					s++;
					if (isval) {
						SpnValue *precptr;

						/* check argc if the caller wants us to do so */
						if (argc >= 0 && argidx >= argc) {
							format_errmsg(errmsg, OUT_OF_ARGUMENTS, argidx);
							free(bld.buf);
							return NULL;
						}

						/* precision must be an integer too */
						precptr = getarg_val(argv, &argidx);

						if (!isnumber(precptr)) {
							format_errmsg(
								errmsg,
								TYPE_MISMATCH,
								argidx,
								SPN_TTAG_NUMBER,
								precptr->type
							);
							free(bld.buf);
							return NULL;
						}

						if (isfloat(precptr)) {
					 		format_errmsg(
					 			errmsg,
					 			EXPECT_INTEGER,
					 			argidx
					 		);
							free(bld.buf);
							return NULL;
						}

						args.precision = intvalue(precptr);
					} else {
						const int *precptr = getarg_raw(argv, &argidx);
						args.precision = *precptr;
					}
				}
			}

			args.spec = *s++;

			/* check argc if the caller wants us to do so */
			if (argc >= 0 && argidx >= argc) {
				format_errmsg(errmsg, OUT_OF_ARGUMENTS, argidx);
				free(bld.buf);
				return NULL;
			}

			/* append parsed format string */
			if (append_format(&bld, &args, argv, &argidx, isval, errmsg) != 0) {
				free(bld.buf);
				return NULL;
			}

			/* update non-format chunk base pointer */
			p = s;
		} else {
			s++;
		}
	}

	/* if the format string doesn't end with a conversion specifier,
	 * then just append the last non-format (literal) string chunk
	 */
	if (s > p) {
		append_string(&bld, p, s - p);
	}

	/* append terminating NUL byte */
	expand_buffer(&bld, 1);
	bld.buf[bld.len] = 0;

	if (len != NULL) {
		*len = bld.len;
	}

	return bld.buf;
}

char *spn_string_format_cstr(const char *fmt, size_t *len, const void **argv)
{
	return make_format_string(fmt, len, -1, argv, 0, NULL);
}

SpnString *spn_string_format_obj(SpnString *fmt, int argc, SpnValue *argv, char **errmsg)
{
	size_t len;
	char *buf = make_format_string(fmt->cstr, &len, argc, argv, 1, errmsg);
	return buf ? spn_string_new_nocopy_len(buf, len, 1) : NULL;
}

/* convenience value constructors */

static SpnValue string_to_val(SpnString *str)
{
	SpnValue ret;
	ret.type = SPN_TYPE_STRING;
	ret.v.o = str;
	return ret;
}

SpnValue spn_makestring(const char *s)
{
	SpnString *str = spn_string_new(s);
	return string_to_val(str);
}

SpnValue spn_makestring_nocopy(const char *s)
{
	SpnString *str = spn_string_new_nocopy(s, 0);
	return string_to_val(str);
}

SpnValue spn_makestring_nocopy_len(const char *s, size_t len, int dealloc)
{
	SpnString *str = spn_string_new_nocopy_len(s, len, dealloc);
	return string_to_val(str);
}

