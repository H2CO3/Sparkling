/*
 * ast.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 30/07/2016
 * Licensed under the 2-clause BSD License
 *
 * Abstract Syntax Tree representation
 */

#ifndef SPN_AST_H
#define SPN_AST_H

#include "str.h"
#include "array.h"
#include "hashmap.h"
#include "lex.h"


/* AST reader API
 * --------------
 * Helper functions for walking the AST and
 * obtaining various properties thereof along the way
 */
SpnString *ast_get_string(SpnHashMap *ast, const char *key);
SpnString *ast_get_string_optional(SpnHashMap *ast, const char *key);

/* Returns the 'children' array of 'node'.
 * Naturally, the returned pointer is *non-owning*.
 */
SpnArray *ast_get_children(SpnHashMap *ast);
SpnArray *ast_get_array(SpnHashMap *ast, const char *key);

SpnHashMap *ast_get_nth_child(SpnArray *children, size_t index);
SpnHashMap *ast_get_child_byname(SpnHashMap *ast, const char *key);
SpnHashMap *ast_get_child_byname_optional(SpnHashMap *ast, const char *key);

/* used for getting the type of an AST node.
 * Returns a (non-owning) pointer to the type string inside the AST node.
 */
const char *ast_get_type(SpnHashMap *ast);

/* returns nonzero if the two node type strings are equal, and zero otherwise */
int type_equal(const char *p, const char *q);



/* AST writer API
 * --------------
 * These functions help building the abstract syntax tree.
 */

/* 'type' must be a string literal in this function */
SpnHashMap *ast_new(const char *type, SpnSourceLocation loc);

SpnHashMap *ast_shallow_copy(SpnHashMap *ast);

/*
 * The set of possible keys, node types etc. is generally known at
 * compile time (if one is messing around with generating ASTs at runtime,
 * that is memory-safe anyway so the warning below doesn't concern him.)
 * Hence, we mostly use 'makestring_nocopy()' so as to avoid extraneous
 * dynamic allocation. This, however, means that we need to use string
 * literals (that have static storage duration) so the strings are not
 * deallocated (or otherwise invalidated) prematurely.
 */
void ast_set_property(SpnHashMap *node, const char *key, const SpnValue *val);

/* Sets 'child' as the child of parent 'node'.
 * This transfers ownership ("xfer"), releases 'child'!
 * 'key' must be a string literal too.
 */
void ast_set_child_xfer(SpnHashMap *node, const char *key, SpnHashMap *child);

/* Adds 'child' to the children array of 'node'.
 * Transfers ownership - releases 'child'!
 */
void ast_push_child_xfer(SpnHashMap *node, SpnHashMap *child);

/* This returns a *non-owning* pointer to a newly appended child */
SpnHashMap *ast_append_child(SpnHashMap *node, const char *type, SpnSourceLocation loc);

/* returns non-zero if nodes of type 'type' need a child array */
int ast_type_needs_children(const char *type);

/* If 'expr' is a function expression, then sets its name to 'name' */
void set_name_if_is_function(SpnHashMap *expr, SpnValue name);

#endif /* SPN_AST_H */
