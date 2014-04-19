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

#define RESERVED_ENTRY(w, t) { w, sizeof(w) - 1, t }


/* Structure for searching reserved keywords */
typedef struct TReserved {
	const char *word;
	size_t len;
	enum spn_lex_token tok;
} TReserved;

/* Helper functions for the lexer */

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
	case '#':
	case '(':
	case ')':
	case '[':
	case ']':
	case '{':
	case '}': return 1;
	default:  return 0;
	}
}

static int is_ident_begin(char c)
{
	return isalpha(c) || c == '_';
}

static int is_ident(char c)
{
	return isalnum(c) || c == '_';
}

static int is_octal(char c)
{
	return '0' <= c && c <= '7';
}

static int is_num_begin(const char *s)
{
	return isdigit(s[0]) || (s[0] == '.' && isdigit(s[1]));
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

/* For characters and strings */
static int unescape_char(SpnParser *p)
{
	/* skip leading backslash '\' */
	switch (*++p->pos) {
	case '\\': p->pos++; return '\\';
	case '/':  p->pos++; return  '/';
	case '\'': p->pos++; return '\'';
	case '"':  p->pos++; return '\"';
	case 'a':  p->pos++; return '\a';
	case 'b':  p->pos++; return '\b';
	case 'f':  p->pos++; return '\f';
	case 'n':  p->pos++; return '\n';
	case 'r':  p->pos++; return '\r';
	case 't':  p->pos++; return '\t';
	case '0':  p->pos++; return '\0';

	case 'x': {
		p->pos++;
		if (isxdigit(p->pos[0]) && isxdigit(p->pos[1])) {
			int hn = hexch_to_int(*p->pos++);
			int ln = hexch_to_int(*p->pos++);
			return (hn << 4) | ln;
		} else {
			/* invalid hex escape sequence */
			long lc0 = p->pos[0];
			long lc1 = p->pos[1];
			const void *args[2];
			args[0] = &lc0;
			args[1] = &lc1;
			spn_parser_error(p, "invalid hex escape sequence '\\x%c%c'", args);
			return -1;
		}
	}
	default:
		{
			long lc = p->pos[0];
			const void *args[1];
			args[0] = &lc;
			spn_parser_error(p, "invalid escape sequence '\\%c'", args);
			return -1;
		}
	}
}

/* Handling whitespace and comments (treated as whitespace) */

static void skip_space(SpnParser *p)
{
	while (isspace(p->pos[0])) {
		if (p->pos[0] == '\n') {
			if (p->pos[1] == '\r') {
				p->pos++;
			}

			p->lineno++;
		} else if (p->pos[0] == '\r') {
			if (p->pos[1] == '\n') {
				p->pos++;
			}

			p->lineno++;
		}

		p->pos++;
	}
}

static int skip_comment(SpnParser *p)
{
	while (p->pos[0] == '/' && p->pos[1] == '*'
	    || p->pos[0] == '/' && p->pos[1] == '/') {

		/* then-branch: block comments; else-branch: line comments */
		if (p->pos[0] == '/' && p->pos[1] == '*') {
			/* skip block comment beginning marker */
			p->pos += 2;

			if (p->pos[0] == 0) {
				/* error: unterminated comment */
				spn_parser_error(p, "unterminated comment", NULL);
				return 0;
			}

			while (p->pos[0] != '*' || p->pos[1] != '/') {
				if (p->pos[1] == 0) {
					/* error: unterminated comment */
					spn_parser_error(p, "unterminated comment", NULL);
					return 0;
				}

				if (isspace(p->pos[0])) {
					skip_space(p);
				} else {
					p->pos++;
				}
			}

			/* skip comment end marker */
			p->pos += 2;
		} else {
			/* skip line comment marker */
			p->pos += 2;

			/* advance until next newline or end-of-input */
			while (p->pos[0] != '\0'
			    && p->pos[0] != '\n'
			    && p->pos[0] != '\r') {
				p->pos++;
			}

			/* skip the bastard */
			if (p->pos[0]) {
				skip_space(p);
			}
		}
	}

	return 1;
}

static int skip_space_and_comment(SpnParser *p)
{
	while (isspace(p->pos[0])
	    || p->pos[0] == '/' && p->pos[1] == '*'
	    || p->pos[0] == '/' && p->pos[1] == '/') {
		skip_space(p);

		if (!skip_comment(p)) {
			return 0;
		}
	}

	return 1;
}

/* Lexers for token-subtypes */

static int lex_op(SpnParser *p)
{
	size_t i;

	/* The order of entries in this array matters because linear search is
	 * performed on it, and if '+' gets caught before '++', we're in trouble
	 */
	static const TReserved ops[] = {
		RESERVED_ENTRY("++",	SPN_TOK_INCR),
		RESERVED_ENTRY("+=",	SPN_TOK_PLUSEQ),
		RESERVED_ENTRY("+",	SPN_TOK_PLUS),
		RESERVED_ENTRY("--",	SPN_TOK_DECR),
		RESERVED_ENTRY("-=",	SPN_TOK_MINUSEQ),
		RESERVED_ENTRY("->",	SPN_TOK_ARROW),
		RESERVED_ENTRY("-",	SPN_TOK_MINUS),
		RESERVED_ENTRY("*=",	SPN_TOK_MULEQ),
		RESERVED_ENTRY("*",	SPN_TOK_MUL),
		RESERVED_ENTRY("/=",	SPN_TOK_DIVEQ),
		RESERVED_ENTRY("/",	SPN_TOK_DIV),
		RESERVED_ENTRY("%=",	SPN_TOK_MODEQ),
		RESERVED_ENTRY("%",	SPN_TOK_MOD),
		RESERVED_ENTRY("==",	SPN_TOK_EQUAL),
		RESERVED_ENTRY("=",	SPN_TOK_ASSIGN),
		RESERVED_ENTRY("!=",	SPN_TOK_NOTEQ),
		RESERVED_ENTRY("!",	SPN_TOK_LOGNOT),
		RESERVED_ENTRY("?",	SPN_TOK_QMARK),
		RESERVED_ENTRY(":",	SPN_TOK_COLON),
		RESERVED_ENTRY("..=",	SPN_TOK_DOTDOTEQ),
		RESERVED_ENTRY("..",	SPN_TOK_DOTDOT),
		RESERVED_ENTRY(".",	SPN_TOK_DOT),
		RESERVED_ENTRY(",",	SPN_TOK_COMMA),
		RESERVED_ENTRY(";",	SPN_TOK_SEMICOLON),
		RESERVED_ENTRY("<<=",	SPN_TOK_SHLEQ),
		RESERVED_ENTRY("<<",	SPN_TOK_SHL),
		RESERVED_ENTRY("<=",	SPN_TOK_LEQ),
		RESERVED_ENTRY("<",	SPN_TOK_LESS),
		RESERVED_ENTRY(">>=",	SPN_TOK_SHREQ),
		RESERVED_ENTRY(">>",	SPN_TOK_SHR),
		RESERVED_ENTRY(">=",	SPN_TOK_GEQ),
		RESERVED_ENTRY(">",	SPN_TOK_GREATER),
		RESERVED_ENTRY("&&",	SPN_TOK_LOGAND),
		RESERVED_ENTRY("&=",	SPN_TOK_ANDEQ),
		RESERVED_ENTRY("&",	SPN_TOK_BITAND),
		RESERVED_ENTRY("||",	SPN_TOK_LOGOR),
		RESERVED_ENTRY("|=",	SPN_TOK_OREQ),
		RESERVED_ENTRY("|",	SPN_TOK_BITOR),
		RESERVED_ENTRY("^=",	SPN_TOK_XOREQ),
		RESERVED_ENTRY("^",	SPN_TOK_XOR),
		RESERVED_ENTRY("~",	SPN_TOK_BITNOT),
		RESERVED_ENTRY("#",	SPN_TOK_HASH),
		RESERVED_ENTRY("(",	SPN_TOK_LPAREN),
		RESERVED_ENTRY(")",	SPN_TOK_RPAREN),
		RESERVED_ENTRY("[",	SPN_TOK_LBRACKET),
		RESERVED_ENTRY("]",	SPN_TOK_RBRACKET),
		RESERVED_ENTRY("{",	SPN_TOK_LBRACE),
		RESERVED_ENTRY("}",	SPN_TOK_RBRACE)
	};

	for (i = 0; i < COUNT(ops); i++) {
		if (strncmp(p->pos, ops[i].word, ops[i].len) == 0) {
			p->curtok.tok = ops[i].tok;
			p->pos += ops[i].len;
			return 1;
		}
	}

	SHANT_BE_REACHED();
	return 0;
}

static int lex_number(SpnParser *p)
{
	const char *end = p->pos;
	if (end[0] == '0' && end[1] != '.') { /* hexadecimal or octal literal */
		end++;
		if (end[0] == 'x' || end[0] == 'X') { /* hexadecimal */
			char *tmp;
			long i;

			end++;

			while (isxdigit(end[0])) {
				end++;
			}

			i = strtol(p->pos, &tmp, 0);
			if (tmp != end) {
				/* error */
				spn_parser_error(p, "cannot parse hexadecimal integer literal", NULL);
				return 0;
			}

			p->pos = end;
			p->curtok.tok = SPN_TOK_INT;
			p->curtok.val = makeint(i);

			return 1;
		} else { /* octal */
			char *tmp;
			long i;

			while (is_octal(end[0])) {
				end++;
			}

			i = strtol(p->pos, &tmp, 0);
			if (tmp != end) {
				/* error */
				spn_parser_error(p, "cannot parse octal integer literal", NULL);
				return 0;
			}

			p->pos = end;
			p->curtok.tok = SPN_TOK_INT;
			p->curtok.val = makeint(i);
			return 1;
		}
	} else { /* decimal */
		int isfloat = 0, hadexp = 0;

		/* walk past initial digits of radix */
		while (isdigit(end[0])) {
			end++;
		}

		/* skip decimal point if present */
		if (end[0] == '.') {
			isfloat = 1;
			end++;
		} else if (end[0] == 'e' || end[0] == 'E') {
			end++;
			isfloat = 1;
			hadexp = 1;
		}

		if (isfloat) {
			char *tmp;
			double d;

			/* walk past fractional or exponent part, if any */
			while (isdigit(end[0])) {
				end++;
			}

			if (!hadexp) {
				/* walk past exponent part, if any */
				if (end[0] == 'e' || end[0] == 'E') {
					end++;
					if (end[0] == '+' || end[0] == '-') {
						end++;
					}

					if (!isdigit(end[0])) {
						/* error: missing exponent part */
						spn_parser_error(p, "exponent in decimal floating-point literal is missing", NULL);
						return 0;
					}

					while (isdigit(end[0])) {
						end++;
					}
				}
			}

			d = strtod(p->pos, &tmp);

			if (tmp != end) {
				/* error */
				spn_parser_error(p, "cannot parse decimal floating-point literal", NULL);
				return 0;
			}

			p->pos = end;
			p->curtok.tok = SPN_TOK_FLOAT;
			p->curtok.val = makefloat(d);

			return 1;
		} else {
			char *tmp;
			long i = strtol(p->pos, &tmp, 0);

			if (tmp != end) {
				spn_parser_error(p, "cannot parse decimal integer literal", NULL);
				return 0;
			}

			p->pos = end;
			p->curtok.tok = SPN_TOK_INT;
			p->curtok.val = makeint(i);

			return 1;
		}
	}
}

static int lex_ident(SpnParser *p)
{
	size_t diff, i;
	char *buf;
	const char *end = p->pos;

	/* here, order does not matter - keywords and identifiers have to be
	 * delimited by whitespace or special characters, so there's no
	 * ambiguity in lexing them
	 */
	static const TReserved kwds[] = {
		RESERVED_ENTRY("and",		SPN_TOK_LOGAND),
		RESERVED_ENTRY("argc",		SPN_TOK_ARGC),
		RESERVED_ENTRY("break",		SPN_TOK_BREAK),
		RESERVED_ENTRY("const",		SPN_TOK_CONST),
		RESERVED_ENTRY("continue",	SPN_TOK_CONTINUE),
		RESERVED_ENTRY("do",		SPN_TOK_DO),
		RESERVED_ENTRY("else",		SPN_TOK_ELSE),
		RESERVED_ENTRY("false",		SPN_TOK_FALSE),
		RESERVED_ENTRY("for",		SPN_TOK_FOR),
		RESERVED_ENTRY("function",	SPN_TOK_FUNCTION),
		RESERVED_ENTRY("global",	SPN_TOK_CONST),
		RESERVED_ENTRY("if",		SPN_TOK_IF),
		RESERVED_ENTRY("nil",		SPN_TOK_NIL),
		RESERVED_ENTRY("not",		SPN_TOK_LOGNOT),
		RESERVED_ENTRY("null",		SPN_TOK_NIL),
		RESERVED_ENTRY("or",		SPN_TOK_LOGOR),
		RESERVED_ENTRY("return",	SPN_TOK_RETURN),
		RESERVED_ENTRY("sizeof",	SPN_TOK_SIZEOF),
		RESERVED_ENTRY("true",		SPN_TOK_TRUE),
		RESERVED_ENTRY("typeof",	SPN_TOK_TYPEOF),
		RESERVED_ENTRY("var",		SPN_TOK_VAR),
		RESERVED_ENTRY("while",		SPN_TOK_WHILE)
	};

	while (is_ident(*end)) {
		end++;
	}

	diff = end - p->pos;

	/* check if the word is one of the reserved keywords */

	for (i = 0; i < COUNT(kwds); i++) {
		if (diff == kwds[i].len
		 && strncmp(p->pos, kwds[i].word, kwds[i].len) == 0) {
		 	p->pos = end;
		 	p->curtok.tok = kwds[i].tok;
		 	return 1; /* if so, mark it as such */
		 }
	}

	/* if not, it's a proper identifier */
	buf = spn_strndup(p->pos, diff);
	p->curtok.tok = SPN_TOK_IDENT;
	p->curtok.val = makestring_nocopy_len(buf, diff, 1);
	p->pos = end;

	return 1;
}

static int lex_char(SpnParser *p)
{
	/* this is unsigned so that shifting into the MSB is well-defined */
	unsigned long i = 0;
	int n = 0;

	/* skip leading character literal delimiter apostrophe */
	p->pos++;
	if (p->pos[0] == '\'') {
		/* empty character literal */
		spn_parser_error(p, "empty character literal", NULL);
		return 0;
	}

	while (p->pos[0] != '\'') {
		if (p->pos[0] == 0) {
			/* premature end of char literal */
			spn_parser_error(p, "end of input before closing apostrophe in character literal", NULL);
			return 0;
		}

		/* TODO: should this always be 8 instead? */
		i <<= CHAR_BIT;
		if (p->pos[0] == '\\') {
			int c = unescape_char(p);
			if (c < 0) {
				/* error unescaping the character */
				return 0;
			}
			i += c;
		} else {
			i += *p->pos++;
		}

		n++;
	}

	/* skip trailing character literal delimiter apostrophe */
	p->pos++;

	if (n > 8) {
		/* character literal is too long */
		spn_parser_error(p, "character literal longer than 8 bytes", NULL);
		return 0;
	}

	/* XXX: this: http://stackoverflow.com/q/18922601 says that the
	 * assignment operator cannot overflow, so long = unsigned long
	 * should be defined. Is this right?
	 */
	p->curtok.tok = SPN_TOK_INT;
	p->curtok.val = makeint(i);

	return 1;
}

static int lex_string(SpnParser *p)
{
	size_t sz = 0x10;
	size_t n = 0;
	char *buf = spn_malloc(sz);

	/* skip string beginning marker double quotation mark */
	p->pos++;

	while (p->pos[0] != '"') {
		if (p->pos[0] == 0) {
			/* premature end of string literal */
			free(buf);
			spn_parser_error(p, "end of input before closing \" in string literal", NULL);
			return 0;
		}

		if (p->pos[0] == '\\') {
			int c = unescape_char(p);
			if (c < 0) {
				/* error unescaping the character */
				free(buf);
				return 0;
			}

			buf[n++] = c;
		} else {
			buf[n++] = *p->pos++;
		}

		/* expand the buffer if necessary */
		if (n >= sz) {
			sz *= 2;
			buf = spn_realloc(buf, sz);
		}
	}

	buf[n] = 0;

	/* skip string ending marker double quotation mark */
	p->pos++;

	p->curtok.tok = SPN_TOK_STR;
	p->curtok.val = makestring_nocopy_len(buf, n, 1);

	return 1;
}

/* The main lexer function */

int spn_lex(SpnParser *p)
{
	/* for error reporting */
	long lc;
	const void *args[1];

	/* skip whitespace and comments before token */
	if (!skip_space_and_comment(p)) {
		return 0;
	}

	/* just so that it can always be released safely if
	 * an unexpected token is encountered, without having to
	 * know its exact type. Also, the object value (`val.v.o')
	 * member is explicitly set to NULL because some parser methods
	 * expecting an identifier read this member before knowing
	 * whether or not the token is indeed an identifier. So we must
	 * set it if we don't want to invoke undefined behavior.
	 */
	p->curtok.val = makenil();
	p->curtok.val.v.o = NULL;

	/* this needs to come before the check for `is_special()',
	 * since numbers can begin with a dot too, therefore checking if the
	 * decimal floating-point literal starts with `.<digits>' must
	 * be done before blindly assuming that `.' is always the memberof
	 * operator.
	 */
	if (is_num_begin(p->pos)) {
		return lex_number(p);
	}

	if (is_ident_begin(p->pos[0])) {
		return lex_ident(p);
	}

	if (is_special(p->pos[0])) {
		return lex_op(p);
	}

	if (p->pos[0] == '\'') {
		return lex_char(p);
	}

	if (p->pos[0] == '"') {
		return lex_string(p);
	}

	/* end-of-input */
	if (p->pos[0] == 0) {
		p->eof = 1;
		p->curtok.tok = SPN_TOK_EOF;
		return 0;
	}

	/* nothing matched so far -- error */
	lc = p->pos[0];
	args[0] = &lc;
	spn_parser_error(p, "unexpected character `%c'", args);
	return 0;
}

int spn_accept(struct SpnParser *p, enum spn_lex_token tok)
{
	if (p->curtok.tok == tok) {
		spn_lex(p);
		return !p->error; /* return success status */
	}

	return 0;
}

int spn_accept_multi(struct SpnParser *p, const enum spn_lex_token toks[], size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		if (spn_accept(p, toks[i])) {
			return i;
		}
	}

	return -1;
}

