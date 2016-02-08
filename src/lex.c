/*
 * lex.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Lexical analyser
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "lex.h"
#include "str.h"
#include "private.h"


#define RESERVED_ENTRY(w) { w, sizeof(w) - 1 }

/* Structure for searching reserved keywords */
typedef struct Reserved {
	const char *str;
	size_t len;
} Reserved;

/* Helper functions for the lexer */

static void lexer_error(SpnLexer *lexer, const char *fmt, const void *args[])
{
	/* update the column because next_token wasn't called anymore
	 * because of the error, so it couldn't update the location.
	 */
	lexer->location.column = lexer->cursor - lexer->lastline + 1;

	free(lexer->errmsg);
	lexer->errmsg = spn_string_format_cstr(fmt, NULL, args);
}

static int is_special(char c)
{
	switch (c) {
	case '+':
	case '-':
	case '*':
	case '/':
	case '%':
	case '=':
	case '!':
	case '?':
	case ':':
	case '.':
	case ',':
	case ';':
	case '<':
	case '>':
	case '&':
	case '|':
	case '^':
	case '~':
	case '$':
	case '(':
	case ')':
	case '[':
	case ']':
	case '{':
	case '}': return 1;
	default:  return 0;
	}
}

static int is_word_begin(char c)
{
	return isalpha(c) || c == '_';
}

static int is_word(char c)
{
	return isalnum(c) || c == '_';
}

static int is_octal(int c)
{
	return '0' <= c && c <= '7';
}

static int is_binary(int c)
{
	return '0' <= c && c <= '1';
}

static int is_radix_prefix(char c)
{
	char ch = tolower(c);
	return ch == 'x' || ch == 'o' || ch == 'b';
}

static int hexch_to_int(char c)
{
	switch (c) {
	case '0': case '1':
	case '2': case '3':
	case '4': case '5':
	case '6': case '7':
	case '8': case '9': return c - '0';
	case 'a': case 'A': return 10;
	case 'b': case 'B': return 11;
	case 'c': case 'C': return 12;
	case 'd': case 'D': return 13;
	case 'e': case 'E': return 14;
	case 'f': case 'F': return 15;
	default: SHANT_BE_REACHED();
	}

	return -1;
}

static int is_at_comment(SpnLexer *lexer)
{
	return lexer->cursor[0] == '#'
	    || lexer->cursor[0] == '/' && lexer->cursor[1] == '/'
	    || lexer->cursor[0] == '/' && lexer->cursor[1] == '*';
}

/* checks if next token is a whitespace or a comment */
static int is_at_space(SpnLexer *lexer)
{
	return isspace(lexer->cursor[0]) || is_at_comment(lexer);
}

/* checks if next token is an identifier or a reserved keyword */
static int is_at_word_begin(SpnLexer *lexer)
{
	return is_word_begin(lexer->cursor[0]);
}

static int is_at_string(SpnLexer *lexer)
{
	return lexer->cursor[0] == '"';
}

static int is_at_char(SpnLexer *lexer)
{
	return lexer->cursor[0] == '\'';
}

/* checks if next token is a decimal, octal, hex or binary integer literal
 * or a floating-point literal
 */
static int is_at_number(SpnLexer *lexer)
{
	return isdigit(lexer->cursor[0]);
}

/* checks if next token is punctuation (e. g. an operator) */
static int is_at_punct(SpnLexer *lexer)
{
	return is_special(lexer->cursor[0]);
}

/* checks if end-of-input was reached */
static int is_at_eof(SpnLexer *lexer)
{
	return lexer->cursor[0] == 0;
}

/* Because not every system may have this in libc */
static char *spn_strndup(const char *s, size_t n)
{
	const char *t = s;
	char *r = spn_malloc(n + 1);
	char *p = r;

	while (*t && t < s + n) {
		*p++ = *t++;
	}

	*p = 0;
	return r;
}

/* Returns 0 if the escape sequence at the cursor is valid,
 * and non-zero if it is incorrect
 */
static int check_escape_char(SpnLexer *lexer)
{
	assert(*lexer->cursor == '\\');

	/* skip leading backslash '\' */
	switch (*++lexer->cursor) {
	case '\\':
	case '/':
	case '\'':
	case '"':
	case 'a':
	case 'b':
	case 'f':
	case 'n':
	case 'r':
	case 't':
		lexer->cursor++;
		return 0;

	case 'x': {
		lexer->cursor++;
		if (isxdigit(lexer->cursor[0]) && isxdigit(lexer->cursor[1])) {
			lexer->cursor += 2;
			return 0;
		} else {
			/* invalid hex escape sequence */
			long lc0 = lexer->cursor[0];
			long lc1 = lexer->cursor[1];
			const void *args[2];
			args[0] = &lc0;
			args[1] = &lc1;
			lexer_error(lexer, "invalid hex escape sequence '\\x%c%c'", args);
			return -1;
		}
	}
	default:
		{
			long lc = lexer->cursor[0];
			const void *args[1];
			args[0] = &lc;
			lexer_error(lexer, "invalid escape sequence '\\%c'", args);
			return -1;
		}
	}
}

/* this assumes that the given escape sequence is correct. */
static unsigned char unescape_char(const char *seq, size_t *len)
{
	assert(*seq == '\\');

	switch (*++seq) {
	case '\\': *len = 2; return '\\';
	case '/':  *len = 2; return '/';
	case '"':  *len = 2; return '"';
	case '\'': *len = 2; return '\'';
	case 'a':  *len = 2; return '\a';
	case 'b':  *len = 2; return '\b';
	case 'f':  *len = 2; return '\f';
	case 'n':  *len = 2; return '\n';
	case 'r':  *len = 2; return '\r';
	case 't':  *len = 2; return '\t';
	case 'x': {
		unsigned char hi_nib = hexch_to_int(*++seq);
		unsigned char lo_nib = hexch_to_int(*++seq);
		*len = 4;
		return (hi_nib << 4) | lo_nib;
	}
	default:
		*len = 0;
		SHANT_BE_REACHED();
		return 0;
	}
}

/* Handling whitespace and comments (treated as whitespace) */

static void skip_space(SpnLexer *lexer)
{
	while (isspace(lexer->cursor[0])) {
		if (lexer->cursor[0] == '\n') {
			if (lexer->cursor[1] == '\r') {
				lexer->cursor++;
			}

			lexer->location.line++;
			lexer->lastline = ++lexer->cursor;
		} else if (lexer->cursor[0] == '\r') {
			if (lexer->cursor[1] == '\n') {
				lexer->cursor++;
			}

			lexer->location.line++;
			lexer->lastline = ++lexer->cursor;
		} else {
			lexer->cursor++;
		}
	}
}

static int skip_comment(SpnLexer *lexer)
{
	while (is_at_comment(lexer)) {
		/* then-branch: block comments; else-branch: line comments */
		if (lexer->cursor[0] == '/' && lexer->cursor[1] == '*') {
			/* skip block comment beginning marker */
			lexer->cursor += 2;

			if (lexer->cursor[0] == 0) {
				/* error: unterminated comment */
				lexer_error(lexer, "unterminated comment", NULL);
				return 0;
			}

			while (lexer->cursor[0] != '*' || lexer->cursor[1] != '/') {
				if (lexer->cursor[0] == 0 || lexer->cursor[1] == 0) {
					/* error: unterminated comment */
					lexer_error(lexer, "unterminated comment", NULL);
					return 0;
				}

				if (isspace(lexer->cursor[0])) {
					skip_space(lexer);
				} else {
					lexer->cursor++;
				}
			}

			/* skip comment end marker */
			lexer->cursor += 2;
		} else {
			/* actually, we don't need to skip the comment marker separately,
			 * since neither '/' nor '#' is a newline character or NUL.
			 * Just advance the cursor until the next newline or end-of-input...
			 */
			while (lexer->cursor[0] != 0
			    && lexer->cursor[0] != '\n'
			    && lexer->cursor[0] != '\r') {
				lexer->cursor++;
			}

			/* ...and skip the bastard! */
			if (lexer->cursor[0]) {
				skip_space(lexer);
			}
		}
	}

	return 1;
}

/* Lexers for token-subtypes */

static int lex_space(SpnLexer *lexer, SpnToken *token)
{
	while (is_at_space(lexer)) {
		skip_space(lexer);
		if (skip_comment(lexer) == 0) {
			return 0;
		}
	}

	token->type = SPN_TOKEN_WSPACE;
	token->value = NULL;

	return 1;
}

static int lex_op(SpnLexer *lexer, SpnToken *token)
{
	/* The order of entries in this array matters because linear search is
	 * performed on it, and if '+' gets caught before '++', we're in trouble
	 */
	static const Reserved ops[] = {
		RESERVED_ENTRY("("),
		RESERVED_ENTRY(")"),
		RESERVED_ENTRY("["),
		RESERVED_ENTRY("]"),
		RESERVED_ENTRY("{"),
		RESERVED_ENTRY("}"),
		RESERVED_ENTRY(","),
		RESERVED_ENTRY(";"),
		RESERVED_ENTRY("++"),
		RESERVED_ENTRY("+="),
		RESERVED_ENTRY("+"),
		RESERVED_ENTRY("--"),
		RESERVED_ENTRY("-="),
		RESERVED_ENTRY("->"),
		RESERVED_ENTRY("-"),
		RESERVED_ENTRY("*="),
		RESERVED_ENTRY("*"),
		RESERVED_ENTRY("/="),
		RESERVED_ENTRY("/"),
		RESERVED_ENTRY("%="),
		RESERVED_ENTRY("%"),
		RESERVED_ENTRY("=="),
		RESERVED_ENTRY("="),
		RESERVED_ENTRY("!="),
		RESERVED_ENTRY("!"),
		RESERVED_ENTRY("..="),
		RESERVED_ENTRY(".."),
		RESERVED_ENTRY("."),
		RESERVED_ENTRY("<<="),
		RESERVED_ENTRY("<<"),
		RESERVED_ENTRY("<="),
		RESERVED_ENTRY("<"),
		RESERVED_ENTRY(">>="),
		RESERVED_ENTRY(">>"),
		RESERVED_ENTRY(">="),
		RESERVED_ENTRY(">"),
		RESERVED_ENTRY("&&"),
		RESERVED_ENTRY("&="),
		RESERVED_ENTRY("&"),
		RESERVED_ENTRY("||"),
		RESERVED_ENTRY("|="),
		RESERVED_ENTRY("|"),
		RESERVED_ENTRY("^="),
		RESERVED_ENTRY("^"),
		RESERVED_ENTRY("?"),
		RESERVED_ENTRY("::"),
		RESERVED_ENTRY(":"),
		RESERVED_ENTRY("$"),
		RESERVED_ENTRY("~")
	};

	size_t i;
	for (i = 0; i < COUNT(ops); i++) {
		if (strncmp(lexer->cursor, ops[i].str, ops[i].len) == 0) {
			token->type = SPN_TOKEN_PUNCT;
			token->value = spn_strndup(lexer->cursor, ops[i].len);
			lexer->cursor += ops[i].len;
			return 1;
		}
	}

	SHANT_BE_REACHED();
	return 0;
}

static int lex_number(SpnLexer *lexer, SpnToken *token)
{
	const char *end = lexer->cursor;
	int isfloat = 0;

	/* hexadecimal, octal or binary _integer_ literal */
	if (end[0] == '0' && is_radix_prefix(end[1])) {
		int (*classify)(int) = NULL;

		/* skip '0' */
		end++;

		switch (tolower(*end++)) {
		case 'x':
			classify = isxdigit;
			break;
		case 'o':
			classify = is_octal;
			break;
		case 'b':
			classify = is_binary;
			break;
		default:
			SHANT_BE_REACHED();
			break;
		}

		while (classify(end[0])) {
			end++;
		}

		/* if there are no digits after the 2-char prefix, it's an error */
		if (end - lexer->cursor <= 2) {
			lexer_error(lexer, "expecting digits after 0x, 0o or 0b", NULL);
			return 0;
		}

		token->type = SPN_TOKEN_INT;
		token->value = spn_strndup(lexer->cursor, end - lexer->cursor);
		lexer->cursor = end;

		return 1;
	}

	/* decimal integer or floating-point.
	 * Walk past initial digits of radix.
	 */
	while (isdigit(end[0])) {
		end++;
	}

	/* skip decimal point if present */
	if (end[0] == '.') {
		isfloat = 1;
		end++;

		while (isdigit(end[0])) {
			end++;
		}
	}

	if (tolower(end[0]) == 'e') {
		isfloat = 1;
		end++;

		if (end[0] == '+' || end[0] == '-') {
			end++;
		}

		if (!isdigit(end[0])) {
			/* error: missing exponent part */
			lexer_error(lexer, "exponent in floating-point literal is missing", NULL);
			return 0;
		}

		while (isdigit(end[0])) {
			end++;
		}
	}

	token->type = isfloat ? SPN_TOKEN_FLOAT : SPN_TOKEN_INT;
	token->value = spn_strndup(lexer->cursor, end - lexer->cursor);
	lexer->cursor = end;

	return 1;
}

static int lex_word(SpnLexer *lexer, SpnToken *token)
{
	const char *end = lexer->cursor;

	while (is_word(end[0])) {
		end++;
	}

	token->type = SPN_TOKEN_WORD;
	token->value = spn_strndup(lexer->cursor, end - lexer->cursor);
	lexer->cursor = end;

	return 1;
}

static int lex_char(SpnLexer *lexer, SpnToken *token)
{
	int n = 0;

	/* skip leading delimiter apostrophe */
	const char *begin = lexer->cursor++;

	if (lexer->cursor[0] == '\'') {
		/* empty character literal */
		lexer_error(lexer, "empty character literal", NULL);
		return 0;
	}

	while (lexer->cursor[0] != '\'') {
		if (lexer->cursor[0] == 0) {
			/* premature end of char literal */
			lexer_error(lexer, "end of input before closing \"'\" in character literal", NULL);
			return 0;
		}

		if (lexer->cursor[0] == '\n' || lexer->cursor[0] == '\r') {
			lexer_error(lexer, "character literal must not contain a newline", NULL);
			return 0;
		}

		if (lexer->cursor[0] == '\\') {
			if (check_escape_char(lexer)) {
				/* error in escape sequence */
				return 0;
			}
		} else {
			lexer->cursor++;
		}

		n++;
	}

	/* skip closing apostrophe */
	lexer->cursor++;

	if (n > 8) {
		/* character literal is too long */
		lexer_error(lexer, "character literal longer than 8 bytes", NULL);
		return 0;
	}

	token->type = SPN_TOKEN_CHAR;
	token->value = spn_strndup(begin, lexer->cursor - begin);

	return 1;
}

static int lex_string(SpnLexer *lexer, SpnToken *token)
{
	/* skip string beginning marker double quotation mark */
	const char *begin = lexer->cursor++;

	while (lexer->cursor[0] != '"') {
		if (lexer->cursor[0] == 0) {
			/* premature end of string literal */
			lexer_error(lexer, "end of input before closing '\"' in string literal", NULL);
			return 0;
		}

		if (lexer->cursor[0] == '\n' || lexer->cursor[0] == '\r') {
			lexer_error(lexer, "string literal must not contain a newline", NULL);
			return 0;
		}

		if (lexer->cursor[0] == '\\') {
			if (check_escape_char(lexer)) {
				/* error in escape sequence */
				return 0;
			}
		} else {
			lexer->cursor++;
		}
	}

	/* skip closing quotation mark */
	lexer->cursor++;

	token->type = SPN_TOKEN_STRING;
	token->value = spn_strndup(begin, lexer->cursor - begin);

	return 1;
}

/* The main lexer function */
static int next_token(SpnLexer *lexer, SpnToken *token)
{
	/* for error reporting */
	long lc;
	const void *args[1];

	/* the actual lexer functions */
	static const struct {
		int (*predicate)(SpnLexer *);
		int (*extract)(SpnLexer *, SpnToken *);
	} fns[] = {
		{ is_at_space,      lex_space  },
		{ is_at_punct,      lex_op     },
		{ is_at_number,     lex_number },
		{ is_at_word_begin, lex_word   },
		{ is_at_char,       lex_char   },
		{ is_at_string,     lex_string }
	};

	size_t i;

	/* Set up location of lexer and token */
	lexer->location.column = lexer->cursor - lexer->lastline + 1;
	token->location = lexer->location;
	token->offset = lexer->cursor - lexer->source;

	/* try each lexer function in order */
	for (i = 0; i < COUNT(fns); i++) {
		if (fns[i].predicate(lexer)) {
			return fns[i].extract(lexer, token);
		}
	}

	/* end-of-input */
	if (is_at_eof(lexer)) {
		lexer->eof = 1;
		return 0;
	}

	/* nothing matched so far -- error */
	lc = lexer->cursor[0];
	args[0] = &lc;
	lexer_error(lexer, "unexpected character '%c'", args);
	return 0;
}

void spn_lexer_init(SpnLexer *lexer)
{
	lexer->location.line = 0;
	lexer->location.column = 0;
	lexer->source = NULL;
	lexer->cursor = NULL;
	lexer->lastline = NULL;
	lexer->errmsg = NULL;
	lexer->eof = 0;
}

void spn_lexer_free(SpnLexer *lexer)
{
	free(lexer->errmsg);
}

SpnToken *spn_lexer_lex(SpnLexer *lexer, const char *src, size_t *count)
{
	size_t alloc_size = 8;
	size_t n = 0;
	SpnToken *buf = spn_malloc(alloc_size * sizeof buf[0]);
	SpnToken token;

	lexer->location.line = 1;
	lexer->location.column = 1;

	lexer->source = src;
	lexer->cursor = src;
	lexer->lastline = src;

	lexer->eof = 0;

	while (next_token(lexer, &token)) {
		if (token.type == SPN_TOKEN_WSPACE) {
			continue;
		}

		if (n >= alloc_size) {
			alloc_size *= 2;
			buf = spn_realloc(buf, alloc_size * sizeof buf[0]);
		}

		buf[n++] = token;
	}

	/* if 'next_token()' returned false and we haven't reached
	 * the end of the input, then there has been an error.
	 */
	if (lexer->eof == 0) {
		spn_free_tokens(buf, n);
		*count = 0;
		return NULL;
	}

	*count = n;
	return buf;
}

/* std::move()-style ownership transfer of error message */
char *spn_lexer_steal_errmsg(SpnLexer *lexer)
{
	char *errmsg = lexer->errmsg;
	lexer->errmsg = NULL;
	return errmsg;
}

int spn_token_is_reserved(const char *str)
{
	static const char *const kwds[] = {
		"and",
		"break",
		"continue",
		"do",
		"else",
		"extern",
		"false",
		"fn",
		"for",
		"if",
		"in",
		"let",
		"nil",
		"not",
		"or",
		"return",
		"true",
		"typeof",
		"while"
	};

	size_t i;
	for (i = 0; i < COUNT(kwds); i++) {
		if (strcmp(str, kwds[i]) == 0) {
			return 1;
		}
	}

	return 0;
}

void spn_free_tokens(SpnToken *buf, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		free(buf[i].value);
	}

	free(buf);
}

long spn_char_literal_toint(const char *chr)
{
	/* this is unsigned so that overflow is well-defined */
	unsigned long n = 0;

	/* skip leading single quotation mark */
	assert(*chr == '\'');
	chr++;

	while (*chr != '\'') {
		n <<= CHAR_BIT;

		if (*chr == '\\') {
			size_t len;
			n += unescape_char(chr, &len);
			chr += len;
		} else {
			n += *chr++;
		}
	}

	return n;
}

char *spn_unescape_string_literal(const char *str, size_t *outlen)
{
	/* since each escape sequence is more than one character long, it is guaranteed
	 * that the unescaped string will not be longer than the escaped one.
	 */
	size_t maxlen = strlen(str);
	char *buf = spn_malloc(maxlen + 1);
	char *p = buf;

	/* skip leading double quotation mark */
	assert(*str == '"');
	str++;

	while (*str != '"') {
		if (*str == '\\') {
			size_t len;
			*p++ = unescape_char(str, &len);
			str += len;
		} else {
			*p++ = *str++;
		}
	}

	assert(p - buf <= maxlen);

	/* NUL-terminate the string and return its length */
	*p = 0;
	*outlen = p - buf;
	return buf;
}

long spn_token_to_integer(SpnToken *token)
{
	enum spn_token_type type = token->type;
	const char *value = token->value;
	size_t len = strlen(value);
	int base;

	assert(type == SPN_TOKEN_INT || type == SPN_TOKEN_CHAR);

	if (type == SPN_TOKEN_CHAR) {
		return spn_char_literal_toint(value);
	}

	/* if we got here, the token must be an integer literal */
	if (len < 2) {
		/* can only be a single digit - assume decimal */
		return strtol(value, NULL, 10);
	}

	/* if the token is at least 2 characters long,
	 * it can have a prefix specifying the base.
	 */
	switch (value[1]) {
	case 'b': case 'B':
		base = 2;
		value += 2; /* skip '0b' prefix */
		break;
	case 'o': case 'O':
		base = 8;
		value += 2; /* skip '0o' prefix */
		break;
	case 'x': case 'X':
		base = 16;
		value += 2; /* skip '0x' prefix */
		break;
	default:
		base = 10;
		break;
	}

	return strtol(value, NULL, base);
}
