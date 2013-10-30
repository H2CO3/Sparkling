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
#include "ast.h"

/* A token is the most basic lexical element, e. g. a keyword, an operator,
 * a literal
 */

enum spn_lex_token {
	/* End-of-input sentinel token */
	SPN_TOK_EOF = -1,

	/* Variable-content tokens: identifiers, literals */
	SPN_TOK_IDENT,
	SPN_TOK_INT,
	SPN_TOK_FLOAT,
	SPN_TOK_STR,

	/* Keywords */
	SPN_TOK_IF,
	SPN_TOK_ELSE,
	SPN_TOK_WHILE,
	SPN_TOK_DO,
	SPN_TOK_FOR,
	SPN_TOK_FOREACH,
	SPN_TOK_AS,
	SPN_TOK_IN,
	SPN_TOK_BREAK,
	SPN_TOK_CONTINUE,
	SPN_TOK_FUNCTION,
	SPN_TOK_RETURN,
	SPN_TOK_TRUE,
	SPN_TOK_FALSE,
	SPN_TOK_NIL,
	SPN_TOK_NAN,
	SPN_TOK_SIZEOF,
	SPN_TOK_TYPEOF,
	SPN_TOK_VAR,

	/* `and', `or' and `not' are lexed as the corresponding logical
	 * operators; `nil' and `null' are recognized as the exact same token
	 */

	/* Operators, special characters */
	SPN_TOK_LPAREN,
	SPN_TOK_RPAREN,
	SPN_TOK_LBRACKET,
	SPN_TOK_RBRACKET,
	SPN_TOK_LBRACE,
	SPN_TOK_RBRACE,
	SPN_TOK_PLUS,
	SPN_TOK_MINUS,
	SPN_TOK_MUL,
	SPN_TOK_DIV,
	SPN_TOK_MOD,
	SPN_TOK_BITAND,
	SPN_TOK_BITOR,
	SPN_TOK_XOR,
	SPN_TOK_ASSIGN,
	SPN_TOK_LOGNOT,
	SPN_TOK_BITNOT,
	SPN_TOK_LESS,
	SPN_TOK_GREATER,
	SPN_TOK_QMARK,
	SPN_TOK_COLON,
	SPN_TOK_SEMICOLON,
	SPN_TOK_COMMA,
	SPN_TOK_DOT,
	SPN_TOK_DOTDOT,
	SPN_TOK_HASH,
	SPN_TOK_ARROW,
	SPN_TOK_INCR,
	SPN_TOK_DECR,
	SPN_TOK_LOGAND,
	SPN_TOK_LOGOR,
	SPN_TOK_EQUAL,
	SPN_TOK_NOTEQ,
	SPN_TOK_PLUSEQ,
	SPN_TOK_MINUSEQ,
	SPN_TOK_MULEQ,
	SPN_TOK_DIVEQ,
	SPN_TOK_MODEQ,
	SPN_TOK_ANDEQ,
	SPN_TOK_OREQ,
	SPN_TOK_XOREQ,
	SPN_TOK_DOTDOTEQ,
	SPN_TOK_SHL,
	SPN_TOK_SHR,
	SPN_TOK_LEQ,
	SPN_TOK_GEQ,
	SPN_TOK_SHLEQ,
	SPN_TOK_SHREQ
};

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

/* error reporting function. sets p->errmsg */
SPN_API void		 spn_parser_error(SpnParser *p, const char *msg, const void *args[]);

#endif /* SPN_PARSER_H */

