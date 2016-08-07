/*
 * ast.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 30/07/2016
 * Licensed under the 2-clause BSD License
 *
 * Abstract Syntax Tree representation
 */

#include <string.h>

#include "ast.h"
#include "misc.h"


SpnString *ast_get_string(SpnHashMap *ast, const char *key)
{
	SpnValue value = spn_hashmap_get_strkey(ast, key);
	assert(isstring(&value));
	return stringvalue(&value);
}

SpnString *ast_get_string_optional(SpnHashMap *ast, const char *key)
{
	SpnValue value = spn_hashmap_get_strkey(ast, key);
	assert(isstring(&value) || isnil(&value));
	return isstring(&value) ? stringvalue(&value) : NULL;
}

SpnArray *ast_get_array(SpnHashMap *ast, const char *key)
{
	SpnValue value = spn_hashmap_get_strkey(ast, key);
	assert(isarray(&value));
	return arrayvalue(&value);
}

const char *ast_get_type(SpnHashMap *ast)
{
	SpnString *typestr = ast_get_string(ast, "type");
	return typestr->cstr;
}

int type_equal(const char *p, const char *q)
{
	return strcmp(p, q) == 0;
}

SpnArray *ast_get_children(SpnHashMap *ast)
{
	return ast_get_array(ast, "children");
}

SpnHashMap *ast_get_nth_child(SpnArray *children, size_t index)
{
	SpnValue child_val = spn_array_get(children, index);
	assert(ishashmap(&child_val));
	return hashmapvalue(&child_val);
}

SpnHashMap *ast_get_child_byname(SpnHashMap *ast, const char *key)
{
	SpnValue child = spn_hashmap_get_strkey(ast, key);
	assert(ishashmap(&child));
	return hashmapvalue(&child);
}

SpnHashMap *ast_get_child_byname_optional(SpnHashMap *ast, const char *key)
{
	SpnValue child = spn_hashmap_get_strkey(ast, key);
	assert(ishashmap(&child) || isnil(&child));
	return ishashmap(&child) ? hashmapvalue(&child) : NULL;
}

SpnHashMap *ast_shallow_copy(SpnHashMap *ast)
{
	size_t cursor = 0;
	SpnValue key, val;
	SpnHashMap *dup = spn_hashmap_new();

	while ((cursor = spn_hashmap_next(ast, cursor, &key, &val)) != 0) {
		spn_hashmap_set(dup, &key, &val);
	}

	return dup;
}

void ast_set_property(SpnHashMap *node, const char *key, const SpnValue *val)
{
	SpnValue pname = makestring_nocopy(key);
	spn_hashmap_set(node, &pname, val);
	spn_value_release(&pname);
}

SpnHashMap *ast_new(const char *type, SpnSourceLocation loc)
{
	SpnHashMap *node = spn_hashmap_new();

	SpnValue vtype = makestring_nocopy(type);
	SpnValue line = makeint(loc.line);
	SpnValue col = makeint(loc.column);

	ast_set_property(node, "type", &vtype);
	ast_set_property(node, "line", &line);
	ast_set_property(node, "column", &col);

	spn_value_release(&vtype);

	/* if the node an explicit 'children' array, add it */
	if (ast_type_needs_children(type)) {
		SpnValue children = makearray();
		ast_set_property(node, "children", &children);
		spn_value_release(&children);
	}

	return node;
}

void ast_set_child_xfer(SpnHashMap *node, const char *key, SpnHashMap *child)
{
	SpnValue val = makeobject(child);
	ast_set_property(node, key, &val);
	spn_object_release(child);
}

void ast_push_child_xfer(SpnHashMap *node, SpnHashMap *child)
{
	SpnArray *children = ast_get_children(node);
	SpnValue vchild = makeobject(child);

	spn_array_push(children, &vchild);
	spn_object_release(child);
}

SpnHashMap *ast_append_child(SpnHashMap *node, const char *type, SpnSourceLocation loc)
{
	SpnHashMap *child = ast_new(type, loc);
	ast_push_child_xfer(node, child);
	return child;
}

int ast_type_needs_children(const char *type)
{
	/* this array contains the node types that need an
	 * explicit 'children' array because they may have
	 * an arbitrary number of children.
	 */
	static const char *const partypes[] = {
		"block",
		"program",
		"call",
		"vardecl",
		"constdecl",
		"array",
		"hashmap"
	};

	size_t i;
	for (i = 0; i < COUNT(partypes); i++) {
		if (strcmp(type, partypes[i]) == 0) {
			return 1;
		}
	}

	return 0;
}

void set_name_if_is_function(SpnHashMap *expr, SpnValue name)
{
	SpnString *type;
	SpnValue typeval = spn_hashmap_get_strkey(expr, "type");

	assert(isstring(&typeval));
	type = stringvalue(&typeval);

	assert(isstring(&name));

	if (strcmp(type->cstr, "function") == 0) {
		ast_set_property(expr, "name", &name);
	}
}
