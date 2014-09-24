/*
 * lex.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Lexical analyser
 */

#ifndef SPN_LEX_H
#define SPN_LEX_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "api.h"
#include "parser.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* look for the next token and store it in p->curtok.
 * returns 1 on success, 0 on error or end-of-input.
 */
SPN_API int spn_lex(SpnParser *p);

/* check if the next token is `tok', return 1 if it is, 0 otherwise */
SPN_API int spn_accept(SpnParser *p, enum spn_lex_token tok);

/* returns the index of the token if found (in the [0...n) interval),
 * returns -1 if not found
 */
SPN_API int spn_accept_multi(SpnParser *p, const enum spn_lex_token toks[], size_t n);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SPN_LEX_H */
