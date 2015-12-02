/*
 * lex.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * This file is part of Sparkling.
 *
 * Sparkling is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Sparkling is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Sparkling. If not, see <http://www.gnu.org/licenses/>.
 *
 * Lexical analyser
 */

#ifndef SPN_LEX_H
#define SPN_LEX_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum spn_token_type {
	SPN_TOKEN_WSPACE,
	SPN_TOKEN_WORD,
	SPN_TOKEN_STRING,
	SPN_TOKEN_CHAR,
	SPN_TOKEN_INT,
	SPN_TOKEN_FLOAT,
	SPN_TOKEN_PUNCT
};

typedef struct SpnSourceLocation {
	unsigned line;
	unsigned column;
} SpnSourceLocation;

typedef struct SpnToken {
	enum spn_token_type type;
	SpnSourceLocation location;
	ptrdiff_t offset;
	char *value;
} SpnToken;

typedef struct SpnLexer {
	SpnSourceLocation location;
	const char *source;
	const char *cursor;
	const char *lastline;
	char *errmsg;
	int eof;
} SpnLexer;

SPN_API void spn_lexer_init(SpnLexer *lexer);
SPN_API void spn_lexer_free(SpnLexer *lexer);

/* always returns a non-NULL pointer if lexing succeeded,
 * even if there weren't any tokens at all.
 * Upon successful return, '*count' is the number of tokens, 0 otherwise.
 */
SPN_API SpnToken *spn_lexer_lex(SpnLexer *lexer, const char *src, size_t *count);

/* returns the error message of the lexer as an owning pointer;
 * sets the lexer's internal error message pointer to NULL
 * (thereby disposing of its ownership).
 */
SPN_API char *spn_lexer_steal_errmsg(SpnLexer *lexer);

SPN_API int spn_token_is_reserved(const char *str);
SPN_API void spn_free_tokens(SpnToken *buf, size_t n);

SPN_API long spn_char_literal_toint(const char *chr);
SPN_API char *spn_unescape_string_literal(const char *str, size_t *outlen);

/* 'token' must be either an integer or a character literal.
 * Calling this function on any other kind of token is
 * considered to be a constraint violation.
 */
SPN_API long spn_token_to_integer(SpnToken *token);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SPN_LEX_H */
