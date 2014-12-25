/*
 * parser.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Simple recursive descent parser
 */

#ifndef SPN_PARSER_H
#define SPN_PARSER_H


#include "api.h"
#include "lex.h"
#include "hashmap.h"

/* a parser object takes a string (Sparkling source code) and parses it
 * to an abstract syntax tree (nodes composed of SpnHashMap objects).
 */

typedef struct SpnParser {
	SpnLexer lexer;     /* private */
	SpnToken *tokens;   /* private */
	size_t num_toks;    /* private */
	size_t cursor;      /* private */
	int error;          /* private */
	char *errmsg;       /* public: the last error message */
} SpnParser;


SPN_API void spn_parser_init(SpnParser *p);
SPN_API void spn_parser_free(SpnParser *p);

/* parses 'src' to an abstract syntax tree. returns NULL on error
 * (in which case, one should inspect p->errmsg)
 */
SPN_API SpnHashMap *spn_parser_parse(SpnParser *p, const char *src);

/* parses an expression and wraps it in a program that
 * just returns its result. Used in the REPL.
 */
SPN_API SpnHashMap *spn_parser_parse_expression(SpnParser *p, const char *src);

SPN_API SpnSourceLocation spn_parser_get_error_location(SpnParser *p);

#endif /* SPN_PARSER_H */
