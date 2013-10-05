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


#include "spn.h"
#include "lex.h"
#include "ast.h"

/* A token is the most basic lexical element, e. g. a keyword, an operator,
 * a literal
 */

typedef struct SpnToken {
	enum spn_lex_token tok;
	SpnValue val;
} SpnToken;

/* a parser object takes a string (Sparkling source code) and parses it
 * to an abstract syntax tree (SpnAST).
 */

typedef struct SpnParser {
	const char	*pos;		/* private */
	SpnToken	 curtok;	/* private */
	int		 eof;		/* private */
	int		 error;		/* private */
	unsigned long	 lineno;	/* private */
	char		*errmsg;	/* public: the last error message */
} SpnParser;


SPN_API SpnParser	*spn_parser_new();
SPN_API void	 	 spn_parser_free(SpnParser *p);

/* parse `src' to an abstract syntax tree. returns NULL on error
 * (in which case, one should inspect p->errmsg)
 */
SPN_API SpnAST		*spn_parser_parse(SpnParser *p, const char *src);

/* printf-style error reporting function. sets p->errmsg */
SPN_API void		 spn_parser_error(SpnParser *p, const char *fmt, ...);

#endif /* SPN_PARSER_H */

