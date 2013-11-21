/*
 * rtlb.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Run-time support library
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include <limits.h>

#include "rtlb.h"
#include "str.h"
#include "array.h"
#include "ctx.h"

#ifndef LINE_MAX
#define LINE_MAX 0x1000
#endif

/* definitions for maths library and others */

#ifndef M_E
#define M_E		2.71828182845904523536028747135266250
#endif

#ifndef M_LOG2E
#define M_LOG2E		1.44269504088896340735992468100189214
#endif

#ifndef M_LOG10E
#define M_LOG10E	0.434294481903251827651128918916605082
#endif

#ifndef M_LN2
#define M_LN2		0.693147180559945309417232121458176568
#endif

#ifndef M_LN10
#define M_LN10		2.30258509299404568401799145468436421
#endif

#ifndef M_PI
#define M_PI		3.14159265358979323846264338327950288
#endif

#ifndef M_PI_2
#define M_PI_2		1.57079632679489661923132169163975144
#endif

#ifndef M_PI_4
#define M_PI_4		0.785398163397448309615660845819875721
#endif

#ifndef M_1_PI
#define M_1_PI		0.318309886183790671537767526745028724
#endif

#ifndef M_2_PI
#define M_2_PI		0.636619772367581343075535053490057448
#endif

#ifndef M_2_SQRTPI
#define M_2_SQRTPI	1.12837916709551257389615890312154517
#endif

#ifndef M_SQRT2
#define M_SQRT2		1.41421356237309504880168872420969808
#endif

#ifndef M_SQRT1_2
#define	M_SQRT1_2	0.707106781186547524400844362104849039
#endif

#ifndef M_PHI
#define M_PHI		1.61803398874989484820458683436563811
#endif

/***************
 * I/O library *
 ***************/

static int rtlb_getline(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	char buf[LINE_MAX];
	char *p;

	/* handle EOF correctly */
	if (fgets(buf, sizeof(buf), stdin) == NULL) {
		return 0;
	}

	/* remove trailing newline */
	p = strchr(buf, '\n');
	if (p != NULL) {
		*p = 0;
	}

	p = strchr(buf, '\r');
	if (p != NULL) {
		*p = 0;
	}

	ret->t = SPN_TYPE_STRING;
	ret->f = SPN_TFLG_OBJECT;
	ret->v.ptrv = spn_string_new(buf);
	
	return 0;
}

static int rtlb_print(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	int i;
	for (i = 0; i < argc; i++) {
		spn_value_print(&argv[i]);
	}

	printf("\n");

	return 0;
}

static int rtlb_printf(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *fmt;
	SpnString *res;

	if (argc < 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING) {
		return -2;
	}

	fmt = argv[0].v.ptrv;
	res = spn_string_format_obj(fmt, &argv[1]);

	if (res != NULL) {
		fputs(res->cstr, stdout);

		ret->t = SPN_TYPE_NUMBER;
		ret->f = 0;
		ret->v.intv = res->len;

		spn_object_release(res);
	} else {
		return -3;
	}

	return 0;
}

static int rtlb_fopen(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	FILE *fp;
	SpnString *fname, *mode;

	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING || argv[1].t != SPN_TYPE_STRING) {
		return -2;
	}

	fname = argv[0].v.ptrv;
	mode = argv[1].v.ptrv;
	fp = fopen(fname->cstr, mode->cstr);
	if (fp != NULL) {
		ret->t = SPN_TYPE_USRDAT;
		ret->f = 0;
		ret->v.ptrv = fp;
	} else {
		ret->t = SPN_TYPE_NIL;
		ret->f = 0;
	}

	return 0;
}

static int rtlb_fclose(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	FILE *fp;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_USRDAT) {
		return -2;
	}

	fp = argv[0].v.ptrv;
	fclose(fp);
	return 0;
}

static int rtlb_fprintf(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *fmt;
	SpnString *res;
	FILE *stream;

	if (argc < 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_USRDAT) {
		return -2;
	}

	if (argv[1].t != SPN_TYPE_STRING) {
		return -2;
	}

	stream = argv[0].v.ptrv;
	fmt = argv[1].v.ptrv;
	res = spn_string_format_obj(fmt, &argv[2]);

	if (res != NULL) {
		fputs(res->cstr, stream);

		ret->t = SPN_TYPE_NUMBER;
		ret->f = 0;
		ret->v.intv = res->len;

		spn_object_release(res);
	} else {
		return -3;
	}

	return 0;
}

/* XXX: should this remove newlines as well, like getline()? */
static int rtlb_fgetline(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	char buf[LINE_MAX];
	FILE *fp;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_USRDAT) {
		return -2;
	}

	fp = argv[0].v.ptrv;

	if (fgets(buf, sizeof(buf), fp) != NULL) {
		ret->t = SPN_TYPE_STRING;
		ret->f = SPN_TFLG_OBJECT;
		ret->v.ptrv = spn_string_new(buf);
	}
	/* on EOF, return nil */

	return 0;
}

static int rtlb_fread(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	long n;
	char *buf;
	FILE *fp;

	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_USRDAT
	 || argv[1].t != SPN_TYPE_NUMBER
	 || argv[1].f != 0) {
		return -2;
	}

	fp = argv[0].v.ptrv;
	n = argv[1].v.intv;

	buf = malloc(n);
	if (buf == NULL) {
		abort();
	}

	if (fread(buf, n, 1, fp) != 1) {
		free(buf);
		ret->t = SPN_TYPE_NIL;
		ret->f = 0;
	} else {
		ret->t = SPN_TYPE_STRING;
		ret->f = SPN_TFLG_OBJECT;
		ret->v.ptrv = spn_string_new_nocopy_len(buf, n, 1);
	}

	return 0;
}

static int rtlb_fwrite(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	FILE *fp;
	SpnString *str;

	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_USRDAT || argv[1].t != SPN_TYPE_STRING) {
		return -2;
	}

	fp = argv[0].v.ptrv;
	str = argv[1].v.ptrv;

	ret->t = SPN_TYPE_BOOL;
	ret->f = 0;
	ret->v.boolv = fwrite(str->cstr, str->len, 1, fp) == 1;

	return 0;
}

/* if passed `nil`, flushes all streams by calling `fflush(NULL)` */
static int rtlb_fflush(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	FILE *fp = NULL;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_USRDAT && argv[0].t != SPN_TYPE_NIL) {
		return -2;
	}

	if (argv[0].t == SPN_TYPE_USRDAT) {
		fp = argv[0].v.ptrv;
	}

	ret->t = SPN_TYPE_BOOL;
	ret->f = 0;
	ret->v.boolv = !fflush(fp);

	return 0;
}

static int rtlb_ftell(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	FILE *fp;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_USRDAT) {
		return -2;
	}

	fp = argv[0].v.ptrv;

	ret->t = SPN_TYPE_NUMBER;
	ret->f = 0;
	ret->v.intv = ftell(fp);

	return 0;
}

static int rtlb_fseek(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	long off;
	int flag;
	FILE *fp;
	SpnString *whence;

	if (argc != 3) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_USRDAT
	 || argv[1].t != SPN_TYPE_NUMBER
	 || argv[1].f != 0
	 || argv[2].t != SPN_TYPE_STRING) {
		return -2;
	}

	fp = argv[0].v.ptrv;
	off = argv[1].v.intv;
	whence =  argv[2].v.ptrv;

	if (strcmp(whence->cstr, "set") == 0) {
		flag = SEEK_SET;
	} else if (strcmp(whence->cstr, "cur") == 0) {
		flag = SEEK_CUR;
	} else if (strcmp(whence->cstr, "end") == 0) {
		flag = SEEK_END;
	} else {
		return -3;
	}

	ret->t = SPN_TYPE_BOOL;
	ret->f = 0;
	ret->v.boolv = !fseek(fp, off, flag);

	return 0;
}

static int rtlb_feof(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	FILE *fp;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_USRDAT) {
		return -2;
	}

	fp = argv[0].v.ptrv;

	ret->t = SPN_TYPE_BOOL;
	ret->f = 0;
	ret->v.boolv = feof(fp);

	return 0;
}

static int rtlb_remove(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *fname;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING) {
		return -2;
	}

	fname = argv[0].v.ptrv;

	ret->t = SPN_TYPE_BOOL;
	ret->f = 0;
	ret->v.boolv = !remove(fname->cstr);

	return 0;
}

static int rtlb_rename(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *oldname, *newname;

	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING || argv[1].t != SPN_TYPE_STRING) {
		return -2;
	}

	oldname = argv[0].v.ptrv;
	newname = argv[1].v.ptrv;

	ret->t = SPN_TYPE_BOOL;
	ret->f = 0;
	ret->v.boolv = !rename(oldname->cstr, newname->cstr);

	return 0;
}

static int rtlb_tmpnam(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	/* let's not use tmpnam()'s static buffer for thread safety */
	char buf[L_tmpnam];

	if (tmpnam(buf) != NULL) {
		ret->t = SPN_TYPE_STRING;
		ret->f = SPN_TFLG_OBJECT;
		ret->v.ptrv = spn_string_new(buf);
	} else {
		/* tmpnam() failed, return nil */
		ret->t = SPN_TYPE_NIL;
		ret->f = 0;
	}

	return 0;
}

static int rtlb_tmpfile(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	FILE *fp = tmpfile();

	if (fp != NULL) {
		ret->t = SPN_TYPE_USRDAT;
		ret->f = 0;
		ret->v.ptrv = fp;
	} else {
		ret->t = SPN_TYPE_NIL;
		ret->f = 0;
	}

	return 0;
}

const SpnExtFunc spn_libio[SPN_LIBSIZE_IO] = {
	{ "getline",	rtlb_getline	},
	{ "print",	rtlb_print	},
	{ "printf",	rtlb_printf	},
	{ "fopen",	rtlb_fopen	},
	{ "fclose",	rtlb_fclose	},
	{ "fprintf",	rtlb_fprintf	},
	{ "fgetline",	rtlb_fgetline	},
	{ "fread",	rtlb_fread	},
	{ "fwrite",	rtlb_fwrite	},
	{ "fflush",	rtlb_fflush	},
	{ "ftell",	rtlb_ftell	},
	{ "fseek",	rtlb_fseek	},
	{ "feof",	rtlb_feof	},
	{ "remove",	rtlb_remove	},
	{ "rename",	rtlb_rename	},
	{ "tmpnam",	rtlb_tmpnam	},
	{ "tmpfile",	rtlb_tmpfile	}
};


/******************
 * String library *
 ******************/

static int rtlb_indexof(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *haystack, *needle;
	const char *pos;
	long off = 0;
	long len; /* length of haystack, because we need a signed type */

	if (argc != 2 && argc != 3) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING || argv[1].t != SPN_TYPE_STRING) {
		return -2;
	}

	/* if an offset is specified, respect it */
	if (argc == 3) {
		if (argv[2].t != SPN_TYPE_NUMBER || argv[2].f != 0) {
			/* not an integer */
			return -3;
		}

		off = argv[2].v.intv;
	}

	haystack = argv[0].v.ptrv;
	needle   = argv[1].v.ptrv;
	len = haystack->len;

	/* if the offset is negative, count from the end of the string */
	if (off < 0) {
		off = len + off;
	}

	/* if still not good (absolute value of offset too big), then throw */
	if (off < 0 || off > len) {
		return -4;
	}

	pos = strstr(haystack->cstr + off, needle->cstr);

	ret->t = SPN_TYPE_NUMBER;
	ret->f = 0;
	ret->v.intv = pos != NULL ? pos - haystack->cstr : -1;

	return 0;	
}

/* main substring function, used by substr(), substrto() and substrfrom() */
static int rtlb_aux_substr(SpnValue *ret, SpnString *str, long begin, long length)
{
	char *buf;
	long slen = str->len;

	if (begin < 0 || begin > slen) {
		return -1;
	}

	if (length < 0 || length > slen) {
		return -2;
	}

	if (begin + length > slen) {
		return -3;
	}

	buf = malloc(length + 1);
	if (buf == NULL) {
		abort();
	}

	memcpy(buf, str->cstr + begin, length);
	buf[length] = 0;

	ret->t = SPN_TYPE_STRING;
	ret->f = SPN_TFLG_OBJECT;
	ret->v.ptrv = spn_string_new_nocopy_len(buf, length, 1);

	return 0;
}

static int rtlb_substr(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *str;
	long begin, length;

	if (argc != 3) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING
	 || argv[1].t != SPN_TYPE_NUMBER || argv[1].f != 0
	 || argv[2].t != SPN_TYPE_NUMBER || argv[2].f != 0) {
		return -2;
	}

	str = argv[0].v.ptrv;
	begin = argv[1].v.intv;
	length = argv[2].v.intv;

	return rtlb_aux_substr(ret, str, begin, length);
}

static int rtlb_substrto(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *str;
	long length;

	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING
	 || argv[1].t != SPN_TYPE_NUMBER || argv[1].f != 0) {
		return -2;
	}

	str = argv[0].v.ptrv;
	length = argv[1].v.intv;

	return rtlb_aux_substr(ret, str, 0, length);
}

static int rtlb_substrfrom(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *str;
	long begin, length, slen;
	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING
	 || argv[1].t != SPN_TYPE_NUMBER || argv[1].f != 0) {
		return -2;
	}

	str = argv[0].v.ptrv;
	slen = str->len;
	begin = argv[1].v.intv;
	length = slen - begin;

	return rtlb_aux_substr(ret, str, begin, length);
}

static int rtlb_split(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	const char *s, *t;
	long i = 0;

	SpnString *haystack, *needle;
	SpnArray *arr;
	SpnValue key, val;

	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING || argv[1].t != SPN_TYPE_STRING) {
		return -2;
	}

	haystack = argv[0].v.ptrv;
	needle = argv[1].v.ptrv;

	arr = spn_array_new();

	ret->t = SPN_TYPE_ARRAY;
	ret->f = SPN_TFLG_OBJECT;
	ret->v.ptrv = arr;

	key.t = SPN_TYPE_NUMBER;
	key.f = 0;

	val.t = SPN_TYPE_STRING;
	val.f = SPN_TFLG_OBJECT;

	s = haystack->cstr;
	t = strstr(s, needle->cstr);

	while (1) {
		const char *p = t != NULL ? t : haystack->cstr + haystack->len;
		size_t len = p - s;
		char *buf = malloc(len + 1);
		if (buf == NULL) {
			abort();
		}

		memcpy(buf, s, len);
		buf[len] = 0;

		key.v.intv = i++;
		val.v.ptrv = spn_string_new_nocopy_len(buf, len, 1);
		spn_array_set(arr, &key, &val);
		spn_object_release(val.v.ptrv);

		if (t == NULL) {
			break;
		}

		s = t + needle->len;
		t = strstr(s, needle->cstr);
	}

	return 0;
}

static int rtlb_repeat(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	char *buf;
	size_t i, len, n;
	SpnString *str, *rep;

	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING
	 || argv[1].t != SPN_TYPE_NUMBER
	 || argv[1].f != 0) {
		return -2;
	}

	if (argv[1].v.intv < 0) {
		return -3;
	}

	str = argv[0].v.ptrv;
	n = argv[1].v.intv;
	len = str->len * n;

	buf = malloc(len + 1);
	if (buf == NULL) {
		abort();
	}

	for (i = 0; i < n; i++) {
		memcpy(buf + i * str->len, str->cstr, str->len);
	}

	buf[len] = 0;
	rep = spn_string_new_nocopy_len(buf, len, 1);

	ret->t = SPN_TYPE_STRING;
	ret->f = SPN_TFLG_OBJECT;
	ret->v.ptrv = rep;

	return 0;
}

static int rtlb_aux_trcase(SpnValue *ret, int argc, SpnValue *argv, int upc)
{
	const char *p;
	char *buf, *s;
	SpnString *str;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING) {
		return -2;
	}

	str = argv[0].v.ptrv;
	p = str->cstr;

	buf = malloc(str->len + 1);
	if (buf == NULL) {
		abort();
	}

	s = buf;
	while (*p) {
		*s++ = upc ? toupper(*p++) : tolower(*p++);
	}

	*s = 0;

	ret->t = SPN_TYPE_STRING;
	ret->f = SPN_TFLG_OBJECT;
	ret->v.ptrv = spn_string_new_nocopy_len(buf, str->len, 1);

	return 0;
}

static int rtlb_tolower(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_trcase(ret, argc, argv, 0);
}

static int rtlb_toupper(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_trcase(ret, argc, argv, 1);
}

static int rtlb_fmtstring(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *fmt;
	SpnString *res;

	if (argc < 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING) {
		return -2;
	}

	fmt = argv[0].v.ptrv;
	res = spn_string_format_obj(fmt, &argv[1]);

	if (res != NULL) {
		ret->t = SPN_TYPE_STRING;
		ret->f = SPN_TFLG_OBJECT;
		ret->v.ptrv = res;
	}
	/* else implicitly return nil */

	return 0;
}

static int rtlb_toint(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *str;
	long base;

	if (argc < 1 || argc > 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING) {
		return -2;
	}

	if (argc == 2 && (argv[1].t != SPN_TYPE_NUMBER || argv[1].f != 0)) {
		return -3;
	}

	str = argv[0].v.ptrv;
	base = argc == 2 ? argv[1].v.intv : 0;

	if (base == 1 || base < 0 || base > 36) {
		return -4;
	}

	ret->t = SPN_TYPE_NUMBER;
	ret->f = 0;
	ret->v.intv = strtol(str->cstr, NULL, base);

	return 0;
}

static int rtlb_tofloat(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *str;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING) {
		return -2;
	}

	str = argv[0].v.ptrv;

	ret->t = SPN_TYPE_NUMBER;
	ret->f = SPN_TFLG_FLOAT;
	ret->v.fltv = strtod(str->cstr, NULL);

	return 0;
}

static int rtlb_tonumber(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *str;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING) {
		return -2;
	}

	str = argv[0].v.ptrv;

	if (strpbrk(str->cstr, ".eE") != NULL) {
		return rtlb_tofloat(ret, argc, argv, ctx);
	} else {
		return rtlb_toint(ret, argc, argv, ctx);
	}
}

const SpnExtFunc spn_libstring[SPN_LIBSIZE_STRING] = {
	{ "indexof",	rtlb_indexof	},
	{ "substr",	rtlb_substr	},
	{ "substrto",	rtlb_substrto	},
	{ "substrfrom",	rtlb_substrfrom	},
	{ "split",	rtlb_split	},
	{ "repeat",	rtlb_repeat	},
	{ "tolower",	rtlb_tolower	},
	{ "toupper",	rtlb_toupper	},
	{ "fmtstring",	rtlb_fmtstring	},
	{ "tonumber",	rtlb_tonumber	},
	{ "toint",	rtlb_toint	},
	{ "tofloat",	rtlb_tofloat	},
};


/*****************
 * Array library *
 *****************/ /* TODO: implement */

static int rtlb_array(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnArray *arr;
	int i;

	arr = spn_array_new();
	for (i = 0; i < argc; i++) {
		SpnValue idx;
		idx.t = SPN_TYPE_NUMBER;
		idx.f = 0;
		idx.v.intv = i;
		spn_array_set(arr, &idx, &argv[i]);
	}

	ret->t = SPN_TYPE_ARRAY;
	ret->f = SPN_TFLG_OBJECT;
	ret->v.ptrv = arr;

	return 0;
}

static int rtlb_dict(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnArray *arr;
	int i;

	if (argc % 2 != 0) {
		fprintf(stderr, "odd number of values in dict()\n");
		return -1;
	}

	arr = spn_array_new();
	for (i = 0; i < argc; i += 2) {
		spn_array_set(arr, &argv[i], &argv[i + 1]);
	}

	ret->t = SPN_TYPE_ARRAY;
	ret->f = SPN_TFLG_OBJECT;
	ret->v.ptrv = arr;

	return 0;
}

static int rtlb_contains(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	size_t n;
	SpnIterator *it;
	SpnArray *arr;
	SpnValue key, val;

	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_ARRAY) {
		return -2;
	}

	ret->t = SPN_TYPE_BOOL;
	ret->f = 0;
	ret->v.boolv = 0;

	arr = argv[0].v.ptrv;
	n = spn_array_count(arr);
	it = spn_iter_new(arr);

	while (spn_iter_next(it, &key, &val) < n) {
		if (spn_value_equal(&argv[1], &val)) {
			ret->v.boolv = 1;
			break;
		}
	}

	spn_iter_free(it);
	return 0;
}

static int rtlb_join(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	size_t n, i, len = 0;
	char *buf = NULL;
	SpnArray *arr;
	SpnString *delim;

	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_ARRAY || argv[1].t != SPN_TYPE_STRING) {
		return -2;
	}

	arr = argv[0].v.ptrv;
	n = spn_array_count(arr);

	delim = argv[1].v.ptrv;

	for (i = 0; i < n; i++) {
		size_t addlen;
		SpnValue *val;
		SpnString *str;

		SpnValue key;
		key.t = SPN_TYPE_NUMBER;
		key.f = 0;
		key.v.intv = i;

		val = spn_array_get(arr, &key);
		if (val->t != SPN_TYPE_STRING) {
			free(buf);
			return -3;
		}

		/* XXX: this should really be solved using
		 * exponential buffer expansion. Maybe in alpha 2...
		 */

		str = val->v.ptrv;
		addlen = i > 0 ? delim->len + str->len : str->len;

		buf = realloc(buf, len + addlen + 1);
		if (buf == NULL) {
			abort();
		}

		if (i > 0) {
			memcpy(buf + len, delim->cstr, delim->len);
			memcpy(buf + len + delim->len, str->cstr, str->len);
		} else {
			memcpy(buf + len, str->cstr, str->len);
		}

		len += addlen;
	}

	ret->t = SPN_TYPE_STRING;
	ret->f = SPN_TFLG_OBJECT;

	if (i > 0) {
		/* this may catch one error or two */
		assert(buf != NULL);

		/* add NUL terminator */
		buf[len] = 0;
		ret->v.ptrv = spn_string_new_nocopy_len(buf, len, 1);
	} else {
		assert(buf == NULL);

		/* if there were no items to concatenate, return empty string
		 * (this is necessary because `buf` is now NULL,
		 * and we definitely don't want this to segfault)
		 */
		ret->v.ptrv = spn_string_new_nocopy_len("", 0, 0);
	}

	return 0;
}

/* XXX this relies on `ctx' pointing to an SpnContext object!
 * argv[0] is the array to enumerate
 * argv[1] is the callback function
 * args[0] is the key passed to the callback
 * args[1] is the value passed to the callback
 * args[2] is the user info passed to the callback (if any)
 * cbret is the return value of the callback function
 */
static int rtlb_enumerate(SpnValue *ret, int argc, SpnValue *argv, void *data)
{
	size_t n;
	int status = 0;
	SpnArray *arr;
	SpnIterator *it;
	SpnValue args[3]; /* key, value and optional user info */

	if (argc < 2 || argc > 3) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_ARRAY || argv[1].t != SPN_TYPE_FUNC) {
		return -2;
	}

	/* if there's any user info, store it */
	if (argc > 2) {
		args[2] = argv[2];
	}

	arr = argv[0].v.ptrv;
	it = spn_iter_new(arr);
	n = spn_array_count(arr);

	/* argc is always the same as the number of arguments in args,
	 * because both have two leading elements (the array and the
	 * callback in `argv', and the key and the value in `args').
	 */
	while (spn_iter_next(it, &args[0], &args[1]) < n) {
		SpnValue cbret;
		SpnContext *ctx = data;
		spn_vm_callfunc(ctx->vm, &argv[1], &cbret, argc, args);

		/* the callback must return a Boolean or nothing */
		if (cbret.t == SPN_TYPE_BOOL) {
			if (cbret.v.boolv == 0) {
				break;
			}
		} else if (cbret.t != SPN_TYPE_NIL) {
			spn_value_release(&cbret);
			status = -3;
			break;
		}
	}

	spn_iter_free(it);

	return status;
}


const SpnExtFunc spn_libarray[SPN_LIBSIZE_ARRAY] = {
	{ "array",	rtlb_array	},
	{ "dict",	rtlb_dict	},
	{ "sort",	NULL		},
	{ "sortcmp",	NULL		},
	{ "linearsrch",	NULL		},
	{ "binarysrch",	NULL		},
	{ "contains",	rtlb_contains	},
	{ "subarray",	NULL		},
	{ "join",	rtlb_join	},
	{ "enumerate",	rtlb_enumerate	},
	{ "insert",	NULL		},
	{ "insertarr",	NULL		},
	{ "delrange",	NULL		},
	{ "clear",	NULL		}
};


/*****************
 * Maths library *
 *****************/

/* this is a little helper function to get a floating-point value
 * out of an SpnValue, even if it contains an integer.
 */
static double val2float(SpnValue *val)
{
	assert(val->t == SPN_TYPE_NUMBER);
	return val->f & SPN_TFLG_FLOAT ? val->v.fltv : val->v.intv;
}

/* cube root function, because c89 doesn't define it */
static double rtlb_aux_cbrt(double x)
{
	double s = x < 0 ? -1.0 : +1.0;
	return s * pow(s * x, 1.0 / 3.0);
}

/* 2 ^ x and 10 ^ x (not in stdlib either) */
static double rtlb_aux_exp2(double x)
{
	return pow(2.0, x);
}

static double rtlb_aux_exp10(double x)
{
	return pow(10.0, x);
}

/* base-2 logarithm */
static double rtlb_aux_log2(double x)
{
	return log(x) / M_LN2;
}

/* rounding */
static double rtlb_aux_round(double x)
{
	return floor(x + 0.5);
}

static int rtlb_aux_intize(SpnValue *ret, int argc, SpnValue *argv, void *ctx, double (*fn)(double))
{
	double x;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER) {
		return -2;
	}

	x = val2float(&argv[0]);

	if (x < LONG_MIN || x > LONG_MAX) {
		return -3;
	}

	ret->t = SPN_TYPE_NUMBER;
	ret->f = 0;
	ret->v.intv = fn(x);

	return 0;
}

static int rtlb_floor(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_intize(ret, argc, argv, ctx, floor);
}

static int rtlb_ceil(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_intize(ret, argc, argv, ctx, ceil);
}

static int rtlb_round(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_intize(ret, argc, argv, ctx, rtlb_aux_round);
}

static int rtlb_aux_unmath(SpnValue *ret, int argc, SpnValue *argv, void *ctx, double (*fn)(double))
{
	double x;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER) {
		return -2;
	}

	x = val2float(&argv[0]);

	ret->t = SPN_TYPE_NUMBER;
	ret->f = SPN_TFLG_FLOAT;
	ret->v.fltv = fn(x);

	return 0;
}

/* I've done my best refactoring this. Still utterly ugly. Any suggestions? */
static int rtlb_sqrt(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, sqrt);
}

static int rtlb_cbrt(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, rtlb_aux_cbrt);
}

static int rtlb_exp(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, exp);
}

static int rtlb_exp2(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, rtlb_aux_exp2);
}

static int rtlb_exp10(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, rtlb_aux_exp10);
}

static int rtlb_log(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, log);
}

static int rtlb_log2(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, rtlb_aux_log2);
}

static int rtlb_log10(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, log10);
}

static int rtlb_sin(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, sin);
}

static int rtlb_cos(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, cos);
}

static int rtlb_tan(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, tan);
}

static int rtlb_sinh(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, sinh);
}

static int rtlb_cosh(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, cosh);
}

static int rtlb_tanh(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, tanh);
}

static int rtlb_asin(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, asin);
}

static int rtlb_acos(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, acos);
}

static int rtlb_atan(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_unmath(ret, argc, argv, ctx, atan);
}
/* end of horror */

static int rtlb_atan2(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER
	 || argv[1].t != SPN_TYPE_NUMBER) {
		return -2;
	}

	ret->t = SPN_TYPE_NUMBER;
	ret->f = SPN_TFLG_FLOAT;
	ret->v.fltv = atan2(val2float(&argv[0]), val2float(&argv[1]));

	return 0;
}

static int rtlb_hypot(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	double h = 0.0;
	int i;

	for (i = 0; i < argc; i++) {
		double x;

		if (argv[i].t != SPN_TYPE_NUMBER) {
			return -1;
		}

		x = val2float(&argv[i]);
		h += x * x;
	}

	ret->t = SPN_TYPE_NUMBER;
	ret->f = SPN_TFLG_FLOAT;
	ret->v.fltv = sqrt(h);

	return 0;
}

static int rtlb_deg2rad(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER) {
		return -2;
	}

	ret->t = SPN_TYPE_NUMBER;
	ret->f = SPN_TFLG_FLOAT;
	ret->v.fltv = val2float(&argv[0]) / 180.0 * M_PI;

	return 0;
}

static int rtlb_rad2deg(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER) {
		return -2;
	}

	ret->t = SPN_TYPE_NUMBER;
	ret->f = SPN_TFLG_FLOAT;
	ret->v.fltv = val2float(&argv[0]) / M_PI * 180.0;

	return 0;
}

static int rtlb_random(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	/* I am sorry for the skew, but no way I am putting an unbounded loop
	 * into this function. `rand()` is already pretty bad on its own, so
	 * it's pointless to try improving it for simple use cases. If one
	 * needs a decent PRNG, one will use a dedicated library anyway.
	 */
	ret->t = SPN_TYPE_NUMBER;
	ret->f = SPN_TFLG_FLOAT;
	ret->v.fltv = rand() * 1.0 / RAND_MAX;

	return 0;
}

static int rtlb_seed(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER || argv[0].f != 0) {
		return -2;
	}

	srand(argv[0].v.intv);

	return 0;
}

/* C89 doesn't provide these, so here are implementations that *may* work.
 * if they don't work on your platform, let me know and I'll do something
 * about it.
 */
static int rtlb_aux_isnan(double x)
{
	return x != x;
}

static int rtlb_aux_isfin(double x)
{
	double zero = x - x;
	return x == x && zero == zero;
}

static int rtlb_aux_isinf(double x)
{
	double zero = x - x;
	return x == x && zero != zero;
}

/* floating-point classification: double -> boolean */
static int rtlb_aux_fltclass(SpnValue *ret, int argc, SpnValue *argv, void *ctx, int (*fn)(double))
{
	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER) {
		return -2;
	}

	ret->t = SPN_TYPE_BOOL;
	ret->f = 0;
	ret->v.boolv = fn(val2float(&argv[0]));

	return 0;
}

static int rtlb_isfin(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_fltclass(ret, argc, argv, ctx, rtlb_aux_isfin);
}

static int rtlb_isinf(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_fltclass(ret, argc, argv, ctx, rtlb_aux_isinf);
}

static int rtlb_isnan(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_fltclass(ret, argc, argv, ctx, rtlb_aux_isnan);
}

static int rtlb_abs(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER) {
		return -2;
	}

	*ret = argv[0];

	if (ret->f & SPN_TFLG_FLOAT) {
		ret->v.fltv = fabs(ret->v.fltv);
	} else if (ret->v.intv < 0) {
		ret->v.intv = -ret->v.intv;
	}

	return 0;
}

static int rtlb_pow(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER || argv[1].t != SPN_TYPE_NUMBER) {
		return -2;
	}

	ret->t = SPN_TYPE_NUMBER;

	/* if either of the base or the exponent is real,
	 * then the result is real too.
	 * Furthermore, if both the base and the exponent are integers,
	 * but the exponent is negative, then the result is real.
	 * The result is a integer only when the base is an integer and
	 * the exponent is a non-negative integer at the same time.
	 */
	if (argv[0].f & SPN_TFLG_FLOAT || argv[1].f & SPN_TFLG_FLOAT) {
		ret->f = SPN_TFLG_FLOAT;
		ret->v.fltv = pow(val2float(&argv[0]), val2float(&argv[1]));	
	} else if (argv[1].v.intv < 0) {
		ret->f = SPN_TFLG_FLOAT;
		ret->v.fltv = pow(val2float(&argv[0]), val2float(&argv[1]));	
	} else {
		/* base, exponent, result */
		long b = argv[0].v.intv;
		long e = argv[1].v.intv;
		long r = 1;

		/* exponentation by squaring - http://stackoverflow.com/q/101439/ */
		while (e != 0) {
			if (e & 0x01) {
				r *= b;
			}

			b *= b;
			e >>= 1;
		}

		ret->f = 0;
		ret->v.intv = r;
	}

	return 0;
}

static int rtlb_min(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	int i;

	if (argc < 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER) {
		return -2;
	}

	*ret = argv[0];

	for (i = 1; i < argc; i++) {
		if (argv[i].t != SPN_TYPE_NUMBER) {
			return -2;
		}

		if (argv[i].f & SPN_TFLG_FLOAT) {
			if (ret->f & SPN_TFLG_FLOAT) {
				if (argv[i].v.fltv < ret->v.fltv) {
					*ret = argv[i];
				}
			} else {
				if (argv[i].v.fltv < ret->v.intv) {
					*ret = argv[i];
				}
			}
		} else {
			if (ret->f & SPN_TFLG_FLOAT) {
				if (argv[i].v.intv < ret->v.fltv) {
					*ret = argv[i];
				}
			} else {
				if (argv[i].v.intv < ret->v.intv) {
					*ret = argv[i];
				}
			}
		}
	}

	return 0;
}

static int rtlb_max(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	int i;

	if (argc < 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER) {
		return -2;
	}

	*ret = argv[0];

	for (i = 1; i < argc; i++) {
		if (argv[i].t != SPN_TYPE_NUMBER) {
			return -2;
		}

		if (argv[i].f & SPN_TFLG_FLOAT) {
			if (ret->f & SPN_TFLG_FLOAT) {
				if (argv[i].v.fltv > ret->v.fltv) {
					*ret = argv[i];
				}
			} else {
				if (argv[i].v.fltv > ret->v.intv) {
					*ret = argv[i];
				}
			}
		} else {
			if (ret->f & SPN_TFLG_FLOAT) {
				if (argv[i].v.intv > ret->v.fltv) {
					*ret = argv[i];
				}
			} else {
				if (argv[i].v.intv > ret->v.intv) {
					*ret = argv[i];
				}
			}
		}
	}

	return 0;
}

static int rtlb_isfloat(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	if (argc != 1) {
		return -1;
	}

	ret->t = SPN_TYPE_BOOL;
	ret->f = 0;

	if (argv[0].t == SPN_TYPE_NUMBER) {
		ret->v.boolv = (argv[0].f & SPN_TFLG_FLOAT) != 0;
	} else {
		ret->v.boolv = 0;
	}

	return 0;
}

static int rtlb_isint(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	if (argc != 1) {
		return -1;
	}

	ret->t = SPN_TYPE_BOOL;
	ret->f = 0;

	if (argv[0].t == SPN_TYPE_NUMBER) {
		ret->v.boolv = (argv[0].f & SPN_TFLG_FLOAT) == 0;
	} else {
		ret->v.boolv = 0;
	}

	return 0;
}

static int rtlb_fact(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	long i;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER || argv[0].f & SPN_TFLG_FLOAT) {
		return -2;
	}

	if (argv[0].v.intv < 0) {
		return -3;
	}

	ret->t = SPN_TYPE_NUMBER;
	ret->f = 0;
	ret->v.intv = 1;

	for (i = 2; i <= argv[0].v.intv; i++) {
		ret->v.intv *= i;
	}

	return 0;
}

static int rtlb_binom(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	long n, k, i, j, m, p;

	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER || argv[0].f & SPN_TFLG_FLOAT
	 || argv[1].t != SPN_TYPE_NUMBER || argv[1].f & SPN_TFLG_FLOAT) {
		return -2;
	}

	n = argv[0].v.intv;
	k = argv[1].v.intv;

	if (n < 0 || k < 0 || n < k) {
		return -3;
	}

	p = 1; /* accumulates the product */

	m = k < n - k ? k : n - k; /* min(k, n - k) */
	k = m; /* so that the multiplied numbers are large enough */
	i = n - k + 1;
	j = 1;
	while (m-- > 0) {
		/* not equivalent with p *= i++ / j++ due to precedence */
		p = p * i++ / j++;
	}

	ret->t = SPN_TYPE_NUMBER;
	ret->f = 0;
	ret->v.intv = p;

	return 0;
}

const SpnExtFunc spn_libmath[SPN_LIBSIZE_MATH] = {
	{ "abs",	rtlb_abs	},
	{ "min",	rtlb_min	},
	{ "max",	rtlb_max	},
	{ "floor",	rtlb_floor	},
	{ "ceil",	rtlb_ceil	},
	{ "round",	rtlb_round	},
	{ "hypot",	rtlb_hypot	},
	{ "sqrt",	rtlb_sqrt	},
	{ "cbrt",	rtlb_cbrt	},
	{ "pow",	rtlb_pow	},
	{ "exp",	rtlb_exp	},
	{ "exp2",	rtlb_exp2	},
	{ "exp10",	rtlb_exp10	},
	{ "log",	rtlb_log	},
	{ "log2",	rtlb_log2	},
	{ "log10",	rtlb_log10	},
	{ "sin",	rtlb_sin	},
	{ "cos",	rtlb_cos	},
	{ "tan",	rtlb_tan	},
	{ "sinh",	rtlb_sinh	},
	{ "cosh",	rtlb_cosh	},
	{ "tanh",	rtlb_tanh	},
	{ "asin",	rtlb_asin	},
	{ "acos",	rtlb_acos	},
	{ "atan",	rtlb_atan	},
	{ "atan2",	rtlb_atan2	},
	{ "deg2rad",	rtlb_deg2rad	},
	{ "rad2deg",	rtlb_rad2deg	},
	{ "random",	rtlb_random	},
	{ "seed",	rtlb_seed	},
	{ "isfin",	rtlb_isfin	},
	{ "isinf",	rtlb_isinf	},
	{ "isnan",	rtlb_isnan	},
	{ "isfloat",	rtlb_isfloat	},
	{ "isint",	rtlb_isint	},
	{ "fact",	rtlb_fact	},
	{ "binom",	rtlb_binom	}
};


/*********************
 * Date/time library *
 *********************/

static int rtlb_time(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	/* XXX: is time_t guaranteed to be signed? Is it unsigned on any
	 * sensible implementation we should care about?
	 */
	ret->t = SPN_TYPE_NUMBER;
	ret->f = 0;
	ret->v.intv = time(NULL);

	return 0;
}

static int rtlb_microtime(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ret->t = SPN_TYPE_NUMBER;
	ret->f = 0;
	ret->v.intv = 1000000 * tv.tv_sec + tv.tv_usec;

	return 0;
}

/* helper function that does the actual job filling the array from a struct tm.
 * `islocal` is a flag which is nonzero if localtime() is to be called, and
 * zero if gmtime() should be called. The other arguments and the return value
 * correspond exactly to that of the rtlb_gmtime() and rtlb_localtime().
 */
static int rtlb_aux_gettm(SpnValue *ret, int argc, SpnValue *argv, void *ctx, int islocal)
{
	time_t tmstp;
	struct tm *ts;

	SpnArray *arr;
	SpnValue key, val;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER || argv[0].f != 0) {
		return -2;
	}

	/* the argument of this function is an integer returned by time() */
	tmstp = argv[0].v.intv;
	ts = islocal ? localtime(&tmstp) : gmtime(&tmstp);

	arr = spn_array_new();

	key.t = SPN_TYPE_STRING;
	key.f = SPN_TFLG_OBJECT;

	val.t = SPN_TYPE_NUMBER;
	val.f = 0;

	/* make an SpnArray out of the returned struct tm */
	key.v.ptrv = spn_string_new_nocopy("sec", 0);
	val.v.intv = ts->tm_sec;
	spn_array_set(arr, &key, &val);
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("min", 0);
	val.v.intv = ts->tm_min;
	spn_array_set(arr, &key, &val);
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("hour", 0);
	val.v.intv = ts->tm_hour;
	spn_array_set(arr, &key, &val);
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("mday", 0);
	val.v.intv = ts->tm_mday;
	spn_array_set(arr, &key, &val);
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("mon", 0);
	val.v.intv = ts->tm_mon;
	spn_array_set(arr, &key, &val);
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("year", 0);
	val.v.intv = ts->tm_year;
	spn_array_set(arr, &key, &val);
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("wday", 0);
	val.v.intv = ts->tm_wday;
	spn_array_set(arr, &key, &val);
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("yday", 0);
	val.v.intv = ts->tm_yday;
	spn_array_set(arr, &key, &val);
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("isdst", 0);
	val.v.intv = ts->tm_isdst;
	spn_array_set(arr, &key, &val);
	spn_object_release(key.v.ptrv);

	/* return the array */
	ret->t = SPN_TYPE_ARRAY;
	ret->f = SPN_TFLG_OBJECT;
	ret->v.ptrv = arr;

	return 0;
}

static int rtlb_gmtime(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_gettm(ret, argc, argv, ctx, 0);
}

static int rtlb_localtime(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	return rtlb_aux_gettm(ret, argc, argv, ctx, 1);
}

#define RTLB_STRFTIME_BUFSIZE 0x100

static int rtlb_strftime(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	char *buf;
	struct tm ts;
	size_t len;

	SpnValue key;
	SpnValue *val;
	SpnString *fmt;
	SpnArray *arr;

	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING || argv[1].t != SPN_TYPE_ARRAY) {
		return -2;
	}

	/* first argument is the format, second one is the array that 
	 * rtlb_aux_gettm() returned
	 */
	fmt = argv[0].v.ptrv;
	arr = argv[1].v.ptrv;

	key.t = SPN_TYPE_STRING;
	key.f = SPN_TFLG_OBJECT;

	/* convert array back to a struct tm
	 * (TODO: ensure that all values are integers)
	 */
	key.v.ptrv = spn_string_new_nocopy("sec", 0);
	val = spn_array_get(arr, &key);
	ts.tm_sec = val->v.intv;
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("min", 0);
	val = spn_array_get(arr, &key);
	ts.tm_min = val->v.intv;
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("hour", 0);
	val = spn_array_get(arr, &key);
	ts.tm_hour = val->v.intv;
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("mday", 0);
	val = spn_array_get(arr, &key);
	ts.tm_mday = val->v.intv;
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("mon", 0);
	val = spn_array_get(arr, &key);
	ts.tm_mon = val->v.intv;
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("year", 0);
	val = spn_array_get(arr, &key);
	ts.tm_year = val->v.intv;
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("wday", 0);
	val = spn_array_get(arr, &key);
	ts.tm_wday = val->v.intv;
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("yday", 0);
	val = spn_array_get(arr, &key);
	ts.tm_yday = val->v.intv;
	spn_object_release(key.v.ptrv);

	key.v.ptrv = spn_string_new_nocopy("isdst", 0);
	val = spn_array_get(arr, &key);
	ts.tm_isdst = val->v.intv;
	spn_object_release(key.v.ptrv);

	buf = malloc(RTLB_STRFTIME_BUFSIZE);
	if (buf == NULL) {
		abort();
	}

	/* actually do the formatting */
	len = strftime(buf, RTLB_STRFTIME_BUFSIZE, fmt->cstr, &ts);

	/* set return value */
	ret->t = SPN_TYPE_STRING;
	ret->f = SPN_TFLG_OBJECT;
	ret->v.ptrv = spn_string_new_nocopy_len(buf, len, 1);

	return 0;
}

static int rtlb_difftime(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_NUMBER || argv[0].f != 0
	 || argv[1].t != SPN_TYPE_NUMBER || argv[1].f != 0) {
		return -2;
	}

	ret->t = SPN_TYPE_NUMBER;
	ret->f = SPN_TFLG_FLOAT;
	ret->v.fltv = difftime(argv[0].v.intv, argv[1].v.intv);

	return 0;
}

const SpnExtFunc spn_libtime[SPN_LIBSIZE_TIME] = {
	{ "time",	rtlb_time	},
	{ "microtime",	rtlb_microtime	},
	{ "gmtime",	rtlb_gmtime	},
	{ "localtime",	rtlb_localtime	},
	{ "strftime",	rtlb_strftime	},
	{ "difftime",	rtlb_difftime	},
};


/***************************
 * OS/Shell access library *
 ***************************/

static int rtlb_getenv(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *name;
	const char *env;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING) {
		return -2;
	}

	name = argv[0].v.ptrv;
	env = getenv(name->cstr);

	if (env != NULL) {
		ret->t = SPN_TYPE_STRING;
		ret->f = SPN_TFLG_OBJECT;
		ret->v.ptrv = spn_string_new_nocopy(env, 0);
	}
	/* else implicitly return nil */

	return 0;
}

static int rtlb_system(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	SpnString *cmd;
	int code;

	if (argc != 1) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_STRING) {
		return -2;
	}

	cmd = argv[0].v.ptrv;
	code = system(cmd->cstr);

	ret->t = SPN_TYPE_NUMBER;
	ret->f = 0;
	ret->v.intv = code;

	return 0;
}

static int rtlb_assert(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	if (argc != 2) {
		return -1;
	}

	if (argv[0].t != SPN_TYPE_BOOL || argv[1].t != SPN_TYPE_STRING) {
		return -2;
	}

	/* actual assertion */
	if (argv[0].v.boolv == 0) {
		SpnString *msg = argv[1].v.ptrv;
		fprintf(stderr, "Sparkling: assertion failed: %s\n", msg->cstr);
		return -3;
	}

	return 0;
}

static int rtlb_exit(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	/* assume successful termination if no exit code is given */
	int code = 0;

	if (argc > 1) {
		return -1;
	}

	if (argc > 0) {
		if (argv[0].t != SPN_TYPE_NUMBER || argv[0].f != 0) {
			return -2;
		}

		code = argv[0].v.intv;
	}

	exit(code);
	return 0;
}

const SpnExtFunc spn_libsys[SPN_LIBSIZE_SYS] = {
	{ "getenv",	rtlb_getenv	},
	{ "system",	rtlb_system	},
	{ "assert",	rtlb_assert	},
	{ "exit",	rtlb_exit	}
};


static void load_stdlib_functions(SpnVMachine *vm)
{
	spn_vm_addlib(vm, spn_libio, SPN_LIBSIZE_IO);
	spn_vm_addlib(vm, spn_libstring, SPN_LIBSIZE_STRING);
	spn_vm_addlib(vm, spn_libarray, SPN_LIBSIZE_ARRAY);
	spn_vm_addlib(vm, spn_libmath, SPN_LIBSIZE_MATH);
	spn_vm_addlib(vm, spn_libtime, SPN_LIBSIZE_TIME);
	spn_vm_addlib(vm, spn_libsys, SPN_LIBSIZE_SYS);
}

#define SPN_N_STD_CONSTANTS 19

static void load_stdlib_constants(SpnVMachine *vm)
{
	static SpnExtValue values[SPN_N_STD_CONSTANTS];

	/* standard I/O streams */
	values[0].name = "stdin";
	values[0].value.t = SPN_TYPE_USRDAT;
	values[0].value.f = 0;
	values[0].value.v.ptrv = stdin;

	values[1].name = "stdout";
	values[1].value.t = SPN_TYPE_USRDAT;
	values[1].value.f = 0;
	values[1].value.v.ptrv = stdout;

	values[2].name = "stderr";
	values[2].value.t = SPN_TYPE_USRDAT;
	values[2].value.f = 0;
	values[2].value.v.ptrv = stderr;

	/* mathematical constants */
	values[3].name = "M_E";
	values[3].value.t = SPN_TYPE_NUMBER;
	values[3].value.f = SPN_TFLG_FLOAT;
	values[3].value.v.fltv = M_E;

	values[4].name = "M_LOG2E";
	values[4].value.t = SPN_TYPE_NUMBER;
	values[4].value.f = SPN_TFLG_FLOAT;
	values[4].value.v.fltv = M_LOG2E;

	values[5].name = "M_LOG10E";
	values[5].value.t = SPN_TYPE_NUMBER;
	values[5].value.f = SPN_TFLG_FLOAT;
	values[5].value.v.fltv = M_LOG10E;

	values[6].name = "M_LN2";
	values[6].value.t = SPN_TYPE_NUMBER;
	values[6].value.f = SPN_TFLG_FLOAT;
	values[6].value.v.fltv = M_LN2;

	values[7].name = "M_LN10";
	values[7].value.t = SPN_TYPE_NUMBER;
	values[7].value.f = SPN_TFLG_FLOAT;
	values[7].value.v.fltv = M_LN10;

	values[8].name = "M_PI";
	values[8].value.t = SPN_TYPE_NUMBER;
	values[8].value.f = SPN_TFLG_FLOAT;
	values[8].value.v.fltv = M_PI;

	values[9].name = "M_PI_2";
	values[9].value.t = SPN_TYPE_NUMBER;
	values[9].value.f = SPN_TFLG_FLOAT;
	values[9].value.v.fltv = M_PI_2;

	values[10].name = "M_PI_4";
	values[10].value.t = SPN_TYPE_NUMBER;
	values[10].value.f = SPN_TFLG_FLOAT;
	values[10].value.v.fltv = M_PI_4;

	values[11].name = "M_1_PI";
	values[11].value.t = SPN_TYPE_NUMBER;
	values[11].value.f = SPN_TFLG_FLOAT;
	values[11].value.v.fltv = M_1_PI;

	values[12].name = "M_2_PI";
	values[12].value.t = SPN_TYPE_NUMBER;
	values[12].value.f = SPN_TFLG_FLOAT;
	values[12].value.v.fltv = M_2_PI;

	values[13].name = "M_2_SQRTPI";
	values[13].value.t = SPN_TYPE_NUMBER;
	values[13].value.f = SPN_TFLG_FLOAT;
	values[13].value.v.fltv = M_2_SQRTPI;

	values[14].name = "M_SQRT2";
	values[14].value.t = SPN_TYPE_NUMBER;
	values[14].value.f = SPN_TFLG_FLOAT;
	values[14].value.v.fltv = M_SQRT2;

	values[15].name = "M_SQRT1_2";
	values[15].value.t = SPN_TYPE_NUMBER;
	values[15].value.f = SPN_TFLG_FLOAT;
	values[15].value.v.fltv = M_SQRT1_2;

	values[16].name = "M_PHI";
	values[16].value.t = SPN_TYPE_NUMBER;
	values[16].value.f = SPN_TFLG_FLOAT;
	values[16].value.v.fltv = M_PHI;

	/* NaN and infinity */
	values[17].name = "M_NAN";
	values[17].value.t = SPN_TYPE_NUMBER;
	values[17].value.f = SPN_TFLG_FLOAT;
	values[17].value.v.fltv = 0.0 / 0.0; /* silent NaN */

	values[18].name = "M_INF";
	values[18].value.t = SPN_TYPE_NUMBER;
	values[18].value.f = SPN_TFLG_FLOAT;
	values[18].value.v.fltv = 1.0 / 0.0; /* silent +inf */

	spn_vm_addglobals(vm, values, SPN_N_STD_CONSTANTS);
}

void spn_load_stdlib(SpnVMachine *vm)
{
	load_stdlib_functions(vm);
	load_stdlib_constants(vm);
}

