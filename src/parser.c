/*
 * parser.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Simple recursive descent parser
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "parser.h"
#include "lex.h"
#include "str.h"
#include "private.h"
#include "array.h"

/* for 'accept_multi()' */
typedef struct TokenAndNode {
	const char *token;
	const char *node;
	int (*fn)(SpnParser *, SpnHashMap *, SpnHashMap *); /* for parse_postfix() */
} TokenAndNode;

/* Parsers (productions/nonterminals, terminals) */

static SpnHashMap *parse_program(SpnParser *p);
static SpnHashMap *parse_stmt(SpnParser *p, int is_global);
static SpnHashMap *parse_function(SpnParser *p);
static SpnHashMap *parse_expr(SpnParser *p);

static SpnHashMap *parse_assignment(SpnParser *p);
static SpnHashMap *parse_concat(SpnParser *p);
static SpnHashMap *parse_condexpr(SpnParser *p);

static SpnHashMap *parse_logical_or(SpnParser *p);
static SpnHashMap *parse_logical_and(SpnParser *p);
static SpnHashMap *parse_bitwise_or(SpnParser *p);
static SpnHashMap *parse_bitwise_xor(SpnParser *p);
static SpnHashMap *parse_bitwise_and(SpnParser *p);

static SpnHashMap *parse_comparison(SpnParser *p);
static SpnHashMap *parse_shift(SpnParser *p);
static SpnHashMap *parse_additive(SpnParser *p);
static SpnHashMap *parse_multiplicative(SpnParser *p);

static SpnHashMap *parse_prefix(SpnParser *p);
static SpnHashMap *parse_postfix(SpnParser *p);
static SpnHashMap *parse_term(SpnParser *p);

static SpnHashMap *parse_array_literal(SpnParser *p);
static SpnHashMap *parse_hashmap_literal(SpnParser *p);
static SpnArray *parse_decl_args(SpnParser *p);
static SpnArray *parse_decl_args_oldstyle(SpnParser *p);
static SpnArray *parse_decl_args_newstyle(SpnParser *p);

static SpnHashMap *parse_fnstmt(SpnParser *p);
static SpnHashMap *parse_if(SpnParser *p);
static SpnHashMap *parse_while(SpnParser *p);
static SpnHashMap *parse_do(SpnParser *p);
static SpnHashMap *parse_for(SpnParser *p);
static SpnHashMap *parse_break(SpnParser *p);
static SpnHashMap *parse_continue(SpnParser *p);
static SpnHashMap *parse_return(SpnParser *p);
static SpnHashMap *parse_vardecl(SpnParser *p);
static SpnHashMap *parse_extern(SpnParser *p);
static SpnHashMap *parse_expr_stmt(SpnParser *p);
static SpnHashMap *parse_empty(SpnParser *p);
static SpnHashMap *parse_block(SpnParser *p);
static SpnHashMap *parse_block_expecting(SpnParser *p, const char *where);


static SpnHashMap *parse_binexpr_rightassoc(
	SpnParser *p,
	const TokenAndNode tokens[],
	size_t n,
	SpnHashMap *(*subexpr)(SpnParser *)
);

static SpnHashMap *parse_binexpr_leftassoc(
	SpnParser *p,
	const TokenAndNode tokens[],
	size_t n,
	SpnHashMap *(*subexpr)(SpnParser *)
);

static SpnHashMap *parse_binexpr_noassoc(
	SpnParser *p,
	const TokenAndNode tokens[],
	size_t n,
	SpnHashMap *(*subexpr)(SpnParser *)
);

/* Miscellaneous helpers */

static int is_at_eof(SpnParser *p)
{
	return p->cursor >= p->num_toks;
}

static int is_at_token(SpnParser *p, const char *str)
{
	if (is_at_eof(p)) {
		return 0;
	}

	return strcmp(p->tokens[p->cursor].value, str) == 0;
}

static SpnToken *lookahead(SpnParser *p, size_t offset)
{
	if (p->cursor + offset >= p->num_toks) {
		return NULL;
	}

	return &p->tokens[p->cursor + offset];
}

static SpnToken *accept_token_string(SpnParser *p, const char *str)
{
	if (is_at_token(p, str)) {
		return &p->tokens[p->cursor++];
	}

	return NULL;
}

static SpnToken *accept_token_type(SpnParser *p, enum spn_token_type type)
{
	if (is_at_eof(p)) {
		return NULL;
	}

	if (p->tokens[p->cursor].type == type) {
		return &p->tokens[p->cursor++];
	}

	return NULL;
}

/* returns the next token if it is found in 'tokens'.
 * Sets *index to the index it was found at.
 * Returns NULL and doesn't touch *index otherwise.
 * 'tokens' points to elements of an array of array of two strings.
 * The first element (index 0) of the inner array is a token,
 * the second one (index 1) is its corresponding AST node type.
 * 'n' is the length of the outer array - the number of token-node pairs.
 */
static SpnToken *accept_multi(SpnParser *p, const TokenAndNode tokens[], size_t n, size_t *index)
{
	size_t i;
	for (i = 0; i < n; i++) {
		SpnToken *token = accept_token_string(p, tokens[i].token);
		if (token) {
			*index = i;
			return token;
		}
	}

	return NULL;
}

/* Public API */

void spn_parser_init(SpnParser *p)
{
	spn_lexer_init(&p->lexer);
	p->tokens = NULL;
	p->num_toks = 0;
	p->cursor = 0;
	p->error = 0;
	p->errmsg = NULL;
}

void spn_parser_free(SpnParser *p)
{
	spn_lexer_free(&p->lexer);
	spn_free_tokens(p->tokens, p->num_toks);
	free(p->errmsg);
}

SpnSourceLocation spn_parser_get_error_location(SpnParser *p)
{
	/* if a lexing error occurred, obtain the error location from the lexer */
	if (p->tokens == NULL) {
		return p->lexer.location;
	}

	/* else return the location of the token under the cursor */
	if (p->num_toks > 0) {
		size_t tok_index = p->cursor < p->num_toks ? p->cursor : p->num_toks - 1;
		return p->tokens[tok_index].location;
	} else {
		SpnSourceLocation zero_loc = { 0, 0 };
		return zero_loc;
	}
}

static void parser_error(SpnParser *p, const char *fmt, const void *args[])
{
	if (p->error) {
		return; /* only report first syntax error */
	}

	free(p->errmsg);
	p->errmsg = spn_string_format_cstr(fmt, NULL, args);
	p->error = 1;
}

/* returns 0 if the lexing was successful.
 * Returns non-zero and sets the error if an error occurred.
 */
static int setup_parser(SpnParser *p, const char *src)
{
	/* it is safe to free the tokens array unconditionally, since
	 * if the previous execution of the lexer resulted in an error,
	 * then 'p->tokens' is set to NULL and 'p->num_toks' is set to 0.
	 */
	spn_free_tokens(p->tokens, p->num_toks);

	p->tokens = spn_lexer_lex(&p->lexer, src, &p->num_toks);

	if (p->tokens) {
		p->error = 0;
		p->cursor = 0;
		return 0;
	}

	free(p->errmsg);
	p->errmsg = spn_lexer_steal_errmsg(&p->lexer);
	p->error = 1;
	return -1;
}

/* AST writer API
 * --------------
 *
 * These functions help building the abstract syntax tree.
 */

/* returns non-zero if nodes of type 'type' need a child array */
static int ast_type_needs_children(const char *type);

/*
 * The set of possible keys, node types etc. is generally known at
 * compile time (if one is messing around with generating ASTs at runtime,
 * that is memory-safe anyway so the warning below doesn't concern him.)
 * Hence, we mostly use 'makestring_nocopy()' so as to avoid extraneous
 * dynamic allocation. This, however, means that we need to use string
 * literals (that have static storage duration) so the strings are not
 * deallocated (or otherwise invalidated) prematurely.
 */
static void ast_set_property(SpnHashMap *node, const char *key, const SpnValue *val)
{
	SpnValue pname = makestring_nocopy(key);
	spn_hashmap_set(node, &pname, val);
	spn_value_release(&pname);
}

/* Similarly, 'type' must also be a string literal in this function */
static SpnHashMap *ast_new(const char *type, SpnSourceLocation loc)
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

/* Sets 'child' as the child of parent 'node'.
 * This transfers ownership ("xfer"), releases 'child'!
 * 'key' must be a string literal too.
 */
static void ast_set_child_xfer(SpnHashMap *node, const char *key, SpnHashMap *child)
{
	SpnValue val = makeobject(child);
	ast_set_property(node, key, &val);
	spn_object_release(child);
}

/* Returns the 'children' array of 'node'.
 * Naturally, the returned pointer is *non-owning*.
 */
static SpnArray *ast_get_children(SpnHashMap *node)
{
	SpnValue val = spn_hashmap_get_strkey(node, "children");
	assert(isarray(&val));
	return arrayvalue(&val);
}

/* Adds 'child' to the children array of 'node'.
 * Transfers ownership - releases 'child'!
 */
static void ast_push_child_xfer(SpnHashMap *node, SpnHashMap *child)
{
	SpnArray *children = ast_get_children(node);
	SpnValue vchild = makeobject(child);

	spn_array_push(children, &vchild);
	spn_object_release(child);
}

/* This returns a *non-owning* pointer to a newly appended child */
static SpnHashMap *ast_append_child(SpnHashMap *node, const char *type, SpnSourceLocation loc)
{
	SpnHashMap *child = ast_new(type, loc);
	ast_push_child_xfer(node, child);
	return child;
}

static int ast_type_needs_children(const char *type)
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

/* If 'expr' is a function expression, then sets its name to 'name'. */
static void set_name_if_is_function(SpnHashMap *expr, SpnValue name)
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

/* returns a string literal with the name
 * of the identifier token as its value.
 */
static SpnHashMap *ident_to_string(SpnToken *ident)
{
	SpnValue namestring;
	SpnHashMap *ast;

	assert(ident != NULL);
	assert(ident->type == SPN_TOKEN_WORD);

	ast = ast_new("literal", ident->location);
	namestring = makestring(ident->value);
	ast_set_property(ast, "value", &namestring);
	spn_value_release(&namestring);

	return ast;
}


/* re-initialize the parser object (so that it can be reused for parsing
 * multiple translation units), then kick off the actual recursive descent
 * parser to process the source text
 */
SpnHashMap *spn_parser_parse(SpnParser *p, const char *src)
{
	/* return NULL on error */
	if (setup_parser(p, src)) {
		return NULL;
	}

	return parse_program(p);
}

SpnHashMap *spn_parser_parse_expression(SpnParser *p, const char *src)
{
	SpnHashMap *expr;

	/* check for lexing errors */
	if (setup_parser(p, src)) {
		return NULL;
	}

	expr = parse_expr(p);

	if (expr == NULL) {
		return NULL;
	}

	if (is_at_eof(p)) {
		/* fake location, since our parsed expression doesn't
		 * really contain a return statement or a program
		 */
		SpnSourceLocation zeroloc = { 0, 0 };

		SpnHashMap *program = ast_new("program", zeroloc);
		SpnHashMap *return_stmt = ast_append_child(program, "return", zeroloc);
		ast_set_child_xfer(return_stmt, "expr", expr);

		return program;
	}

	/* it is an error if we are not at the end of the source */
	spn_object_release(expr);
	parser_error(p, "garbage after input", NULL);
	return NULL;
}

static SpnHashMap *parse_program(SpnParser *p)
{
	SpnSourceLocation begin = { 1, 1 };
	SpnHashMap *program = ast_new("program", begin);

	while (is_at_eof(p) == 0) {
		SpnHashMap *stmt = parse_stmt(p, 1);

		if (stmt == NULL) {
			spn_object_release(program);
			return NULL;
		}

		ast_push_child_xfer(program, stmt);
	}

	return program;
}

static SpnHashMap *parse_stmt(SpnParser *p, int is_global)
{
	static const struct {
		const char *token;              /* a token corresponding to a production... */
		SpnHashMap *(*fn)(SpnParser *); /* ...and a parser function that implements it */
	} parsers[] = {
		{ "let",      parse_vardecl  },
		{ "fn",       parse_fnstmt   },
		{ "if",       parse_if       },
		{ "while",    parse_while    },
		{ "for",      parse_for      },
		{ "do",       parse_do       },
		{ "return",   parse_return   },
		{ "break",    parse_break    },
		{ "continue", parse_continue },
		{ "{",        parse_block    },
		{ ";",        parse_empty    }
	};

	size_t i;
	for (i = 0; i < COUNT(parsers); i++) {
		if (is_at_token(p, parsers[i].token)) {
			return parsers[i].fn(p);
		}
	}

	/* special cases follow */
	if (is_at_token(p, "extern")) {
		if (is_global) {
			return parse_extern(p);
		} else {
			parser_error(p, "'extern' declarations are only allowed at file scope", NULL);
			return NULL;
		}
	}

	/* there's always hope */
	return parse_expr_stmt(p);
}

/* Helper for 'parse_function()':
 * This parser attempts to parse an expression, it
 * then constructs a block statement containing only
 * a return statement which returns that expression.
 * Hence, this block statement will be a valid function body.
 */
static SpnHashMap *parse_function_body_expression(SpnParser *p, SpnSourceLocation loc)
{
	SpnHashMap *return_stmt;
	SpnHashMap *block;
	SpnHashMap *expr = parse_expr(p);

	if (expr == NULL) {
		return NULL;
	}

	block = ast_new("block", loc);
	return_stmt = ast_append_child(block, "return", loc);
	ast_set_child_xfer(return_stmt, "expr", expr);

	return block;
}

static SpnHashMap *parse_function(SpnParser *p)
{
	SpnHashMap *ast, *body;
	SpnArray *declargs;
	SpnValue declargsval;
	SpnToken *token = accept_token_string(p, "fn");
	SpnToken *arrow; /* non-NULL if we have a one-expression function */

	assert(token != NULL);

	/* Parse formal parameters (declaration-time arguments) */
	declargs = parse_decl_args(p);
	if (declargs == NULL) {
		return NULL;
	}

	declargsval = makeobject(declargs);

	/* Parse function body */
	arrow = accept_token_string(p, "->");
	if (arrow) {
		body = parse_function_body_expression(p, arrow->location);
	} else {
		body = parse_block_expecting(p, "function body");
	}

	if (body == NULL) {
		spn_object_release(declargs);
		return NULL;
	}

	/* this "function" is not the (now-nonexistent) "function"
	 * keyword, but the node type of the function definition AST.
	 */
	ast = ast_new("function", token->location);

	ast_set_property(ast, "declargs", &declargsval);
	ast_set_child_xfer(ast, "body", body);

	spn_value_release(&declargsval);

	return ast;
}

/* This calls 'parse_block()' if we are looking at a left-brace '{'.
 * Otherwise, it reports an error and returns NULL.
 * The 'where' argument is a brief description of the production that
 * we are currently parsing (e. g. "if statement" or "function body").
 */
static SpnHashMap *parse_block_expecting(SpnParser *p, const char *where)
{
	const void *args[1];

	if (is_at_token(p, "{")) {
		return parse_block(p);
	}

	args[0] = where;
	parser_error(p, "expecting block in %s", args);
	return NULL;
}

static SpnHashMap *parse_block(SpnParser *p)
{
	SpnHashMap *node;
	SpnToken *rbrace;
	SpnToken *lbrace = accept_token_string(p, "{");
	assert(lbrace != NULL);

	node = ast_new("block", lbrace->location);

	/* spin around while there are tokens or the block has ended */
	while (!((rbrace = accept_token_string(p, "}")) || is_at_eof(p))) {
		SpnHashMap *stmt = parse_stmt(p, 0);

		if (stmt == NULL) {
			spn_object_release(node);
			return NULL;
		}

		ast_push_child_xfer(node, stmt);
	}

	/* blocks must end with a closing right-brace */
	if (rbrace == NULL) {
		parser_error(p, "expecting '}' at end of block", NULL);
		spn_object_release(node);
		return NULL;
	}

	return node;
}

static SpnHashMap *parse_expr(SpnParser *p)
{
	return parse_assignment(p);
}

static SpnHashMap *parse_assignment(SpnParser *p)
{
	static const TokenAndNode tokens[] = {
		{ "=",   "assign" },
		{ "+=",  "+="     },
		{ "-=",  "-="     },
		{ "*=",  "*="     },
		{ "/=",  "/="     },
		{ "%=",  "%="     },
		{ "&=",  "&="     },
		{ "|=",  "|="     },
		{ "^=",  "^="     },
		{ "<<=", "<<="    },
		{ ">>=", ">>="    },
		{ "..=", "..="    }
	};

	return parse_binexpr_rightassoc(p, tokens, COUNT(tokens), parse_concat);
}

static SpnHashMap *parse_concat(SpnParser *p)
{
	static const TokenAndNode tokens[] = { { "..", "concat" } };
	return parse_binexpr_leftassoc(p, tokens, COUNT(tokens), parse_condexpr);
}

static SpnHashMap *parse_condexpr(SpnParser *p)
{
	SpnHashMap *cond, *br_true, *br_false, *node;
	SpnToken *qmark;

	cond = parse_logical_or(p);
	if (cond == NULL) {
		return NULL;
	}

	qmark = accept_token_string(p, "?");
	if (qmark == NULL) {
		return cond;
	}

	br_true = parse_expr(p);
	if (br_true == NULL) {
		spn_object_release(cond);
		return NULL;
	}

	if (accept_token_string(p, ":") == NULL) {
		/* error, expected ':' */
		parser_error(p, "expected ':' in conditional expression", NULL);
		spn_object_release(cond);
		spn_object_release(br_true);
		return NULL;
	}

	br_false = parse_condexpr(p);
	if (br_false == NULL) {
		spn_object_release(cond);
		spn_object_release(br_true);
		return NULL;
	}

	node = ast_new("condexpr", qmark->location);

	ast_set_child_xfer(node, "cond", cond);
	ast_set_child_xfer(node, "true", br_true);
	ast_set_child_xfer(node, "false", br_false);

	return node;
}

/* Functions to parse binary mathematical expressions
 * in ascending order of precedence.
 */
static SpnHashMap *parse_logical_or(SpnParser *p)
{
	static const TokenAndNode tokens[] = {
		{ "or", "or" },
		{ "||", "or" }
	};

	return parse_binexpr_leftassoc(p, tokens, COUNT(tokens), parse_logical_and);
}

static SpnHashMap *parse_logical_and(SpnParser *p)
{
	static const TokenAndNode tokens[] = {
		{ "and", "and" },
		{ "&&",  "and" }
	};

	return parse_binexpr_leftassoc(p, tokens, COUNT(tokens), parse_comparison);
}

static SpnHashMap *parse_comparison(SpnParser *p)
{
	static const TokenAndNode tokens[] = {
		{ "==", "==" },
		{ "!=", "!=" },
		{ "<",  "<", },
		{ ">",  ">", },
		{ "<=", "<=" },
		{ ">=", ">=" }
	};

	return parse_binexpr_noassoc(p, tokens, COUNT(tokens), parse_bitwise_or);
}

static SpnHashMap *parse_bitwise_or(SpnParser *p)
{
	static const TokenAndNode tokens[] = { { "|", "bit_or" } };
	return parse_binexpr_leftassoc(p, tokens, COUNT(tokens), parse_bitwise_xor);
}

static SpnHashMap *parse_bitwise_xor(SpnParser *p)
{
	static const TokenAndNode tokens[] = { { "^", "bit_xor" } };
	return parse_binexpr_leftassoc(p, tokens, COUNT(tokens), parse_bitwise_and);
}

static SpnHashMap *parse_bitwise_and(SpnParser *p)
{
	static const TokenAndNode tokens[] = { { "&", "bit_and" } };
	return parse_binexpr_leftassoc(p, tokens, COUNT(tokens), parse_shift);
}

static SpnHashMap *parse_shift(SpnParser *p)
{
	static const TokenAndNode tokens[] = {
		{ "<<", "<<" },
		{ ">>", ">>" }
	};

	return parse_binexpr_leftassoc(p, tokens, COUNT(tokens), parse_additive);
}

static SpnHashMap *parse_additive(SpnParser *p)
{
	static const TokenAndNode tokens[] = {
		{ "+", "+" },
		{ "-", "-" }
	};

	return parse_binexpr_leftassoc(p, tokens, COUNT(tokens), parse_multiplicative);
}

static SpnHashMap *parse_multiplicative(SpnParser *p)
{
	static const TokenAndNode tokens[] = {
		{ "*", "*"   },
		{ "/", "/"   },
		{ "%", "mod" }
	};

	return parse_binexpr_leftassoc(p, tokens, COUNT(tokens), parse_prefix);
}

static SpnHashMap *parse_prefix(SpnParser *p)
{
	static const TokenAndNode tokens[] = {
		{ "++",     "pre_inc"  },
		{ "--",     "pre_dec"  },
		{ "+",      "un_plus"  },
		{ "-",      "un_minus" },
		{ "!",      "not"      },
		{ "not",    "not"      },
		{ "~",      "bit_not"  },
	};

	SpnHashMap *child, *ast;
	size_t index;
	SpnToken *op = accept_multi(p, tokens, COUNT(tokens), &index);

	if (op == NULL) {
		return parse_postfix(p);
	}

	/* right recursion for right-associative operators */
	child = parse_prefix(p);
	if (child == NULL) { /* error */
		return NULL;
	}

	ast = ast_new(tokens[index].node, op->location);
	ast_set_child_xfer(ast, "right", child);

	return ast;
}

/* Helper functions for 'parse_postfix()'.
 * They return an error code: 0 on success, non-0 on error.
 * Upon encountering an error, they clean up after themselves:
 * they free 'ast' and 'tmp'.
 *
 * 'ast' is the last parsed node; it will become a child of 'tmp'.
 * 'tmp' represents the node currently being parsed.
 */

static int parse_postfix_incdec(SpnParser *p, SpnHashMap *ast, SpnHashMap *tmp)
{
	ast_set_child_xfer(tmp, "left", ast);
	return 0;
}

static int parse_subscript(SpnParser *p, SpnHashMap *ast, SpnHashMap *tmp)
{
	/* Array or hashmap indexing */
	SpnHashMap *expr = parse_expr(p);
	if (expr == NULL) { /* error  */
		spn_object_release(ast);
		spn_object_release(tmp);
		return -1;
	}

	ast_set_child_xfer(tmp, "object", ast);
	ast_set_child_xfer(tmp, "index", expr);

	if (accept_token_string(p, "]") == NULL) {
		/* error: expected closing bracket */
		parser_error(p, "expected ']' after index in array subscript", NULL);
		spn_object_release(tmp);
		return -1;
	}

	return 0;
}

static int parse_memberof(SpnParser *p, SpnHashMap *ast, SpnHashMap *tmp)
{
	/* Property accessor, dot notation */
	SpnToken *ident;
	SpnValue namestring;

	if ((ident = accept_token_type(p, SPN_TOKEN_WORD)) == NULL) {
		/* error: expected identifier as member */
		parser_error(p, "expecting property name after '.' operator", NULL);
		spn_object_release(ast);
		spn_object_release(tmp);
		return -1;
	}

	/* do not check for reserved words explicitly
	 * -- they are allowed in property names.
	 */
	ast_set_child_xfer(tmp, "object", ast);

	namestring = makestring(ident->value);
	ast_set_property(tmp, "name", &namestring);
	spn_value_release(&namestring);

	return 0;
}

static int parse_call(SpnParser *p, SpnHashMap *ast, SpnHashMap *tmp)
{
	/* Function call */
	ast_set_child_xfer(tmp, "func", ast);

	while (!accept_token_string(p, ")")) {
		SpnHashMap *param = parse_expr(p);

		if (param == NULL) {
			spn_object_release(tmp); /* this frees 'ast' too */
			return -1;
		}

		ast_push_child_xfer(tmp, param);

		/* comma ',' or closing parenthesis ')' must follow */
		if (accept_token_string(p, ",")) {
			if (is_at_token(p, ")")) {
				parser_error(p, "trailing comma after last function argument", NULL);
				spn_object_release(tmp);
				return -1;
			}
		} else if (!is_at_token(p, ")")) {
			parser_error(p, "expecting ',' or ')' after function argument", NULL);
			spn_object_release(tmp); /* this frees ast and param */
			return -1;
		}
	}

	return 0;
}

static int parse_sugared_subscript(SpnParser *p, SpnHashMap *ast, SpnHashMap *tmp)
{
	/* syntactic sugar for raw indexing with string literal */
	SpnToken *ident;
	SpnHashMap *index;

	if ((ident = accept_token_type(p, SPN_TOKEN_WORD)) == NULL) {
		/* error: expected identifier as member */
		parser_error(p, "expecting member name after '::' operator", NULL);
		spn_object_release(ast);
		spn_object_release(tmp);
		return -1;
	}

	/* build index which is a string literal */
	index = ident_to_string(ident);

	ast_set_child_xfer(tmp, "object", ast);
	ast_set_child_xfer(tmp, "index", index);

	return 0;
}

static SpnHashMap *parse_postfix(SpnParser *p)
{
	static const TokenAndNode tokens[] = {
		{ "++", "post_inc",  parse_postfix_incdec    },
		{ "--", "post_dec",  parse_postfix_incdec    },
		{ "[",  "subscript", parse_subscript         },
		{ ".",  "memberof",  parse_memberof          },
		{ "(",  "call",      parse_call              },
		{ "::", "subscript", parse_sugared_subscript }
	};

	size_t index;
	SpnToken *op;

	SpnHashMap *ast = parse_term(p);
	if (ast == NULL) { /* error */
		return NULL;
	}

	/* iteration instead of left recursion - we want to terminate */
	while ((op = accept_multi(p, tokens, COUNT(tokens), &index)) != NULL) {
		SpnHashMap *tmp = ast_new(tokens[index].node, op->location);

		/* we can just return NULL since the parser functions
		 * clean up after themselves if they find an error.
		 */
		if (tokens[index].fn(p, ast, tmp) != 0) {
			return NULL;
		}

		/* update hierarchy - child node is the new parent */
		ast = tmp;
	}

	return ast;
}

static SpnHashMap *parse_term(SpnParser *p)
{
	SpnToken *token;

	/* Parenthesized expression */
	if (accept_token_string(p, "(")) {
		SpnHashMap *ast = parse_expr(p);
		if (ast == NULL) {
			return NULL;
		}

		if (accept_token_string(p, ")") == NULL) {
			parser_error(p, "expecting ')' after parenthesized expression", NULL);
			spn_object_release(ast);
			return NULL;
		}

		return ast;
	}

	/* Array literal */
	if (is_at_token(p, "[")) {
		return parse_array_literal(p);
	}

	/* Hashmap literal */
	if (is_at_token(p, "{")) {
		return parse_hashmap_literal(p);
	}

	/* Function expression */
	if (is_at_token(p, "fn")) {
		return parse_function(p); /* parse function expression */
	}

	/* '$': argv, the argument vector */
	if ((token = accept_token_string(p, "$")) != NULL) {
		return ast_new("argv", token->location);
	}

	/* literal nil */
	if ((token = accept_token_string(p, "nil"))  != NULL) {
		return ast_new("literal", token->location); /* 'value' is nil by default */
	}

	/* Boolean literals */
	if ((token = accept_token_string(p, "true")) != NULL) {
		SpnHashMap *ast = ast_new("literal", token->location);
		ast_set_property(ast, "value", &spn_trueval);
		return ast;
	}

	if ((token = accept_token_string(p, "false")) != NULL) {
		SpnHashMap *ast = ast_new("literal", token->location);
		ast_set_property(ast, "value", &spn_falseval);
		return ast;
	}

	/* Identifiers/names for variables, globals and functions
	 * This needs to come after we have tested for each valid
	 * word (i. e. nil, boolean and function literals),
	 * since this 'jolly joker' call catches *all* word-like tokens.
	 */
	if ((token = accept_token_type(p, SPN_TOKEN_WORD)) != NULL) {
		SpnHashMap *ast;
		SpnValue name;

		if (spn_token_is_reserved(token->value)) {
			const void *args[1];
			args[0] = token->value;
			parser_error(p, "'%s' is a keyword and cannot be a variable name", args);
			return NULL;
		}

		ast = ast_new("ident", token->location);
		name = makestring(token->value);
		ast_set_property(ast, "name", &name);
		spn_value_release(&name);

		return ast;
	}

	/* Integer literal, which may be a character */
	if ((token = accept_token_type(p, SPN_TOKEN_INT))  != NULL
	 || (token = accept_token_type(p, SPN_TOKEN_CHAR)) != NULL) {
		long n = spn_token_to_integer(token);
		SpnValue val = makeint(n);
		SpnHashMap *ast = ast_new("literal", token->location);
		ast_set_property(ast, "value", &val);
		return ast;
	}

	/* Floating-point literal */
	if ((token = accept_token_type(p, SPN_TOKEN_FLOAT)) != NULL) {
		double x = strtod(token->value, NULL);
		SpnValue val = makefloat(x);
		SpnHashMap *ast = ast_new("literal", token->location);
		ast_set_property(ast, "value", &val);
		return ast;
	}

	/* string literal */
	if ((token = accept_token_type(p, SPN_TOKEN_STRING)) != NULL) {
		size_t len;
		char *unescaped = spn_unescape_string_literal(token->value, &len);
		SpnValue val = spn_makestring_nocopy_len(unescaped, len, 1);
		SpnHashMap *ast = ast_new("literal", token->location);
		ast_set_property(ast, "value", &val);
		spn_value_release(&val);
		return ast;
	}

	/* otherwise, we've got either an unexpected token,
	 * or a premature end-of-input condition
	 */
	if (is_at_eof(p)) {
		parser_error(p, "unexpected end of input", NULL);
	} else {
		const void *args[1];
		args[0] = p->tokens[p->cursor].value;
		parser_error(p, "unexpected '%s'", args);
	}

	return NULL;
}

static SpnHashMap *parse_array_literal(SpnParser *p)
{
	SpnHashMap *ast;
	SpnToken *lbracket = accept_token_string(p, "[");
	assert(lbracket != NULL);

	ast = ast_new("array", lbracket->location);

	/* 'while we are not at ]' is enough for the condition, since a
	 * premature end-of-input condition would be catched by parse_expr().
	 */
	while (!accept_token_string(p, "]")) {
		/* parse value */
		SpnHashMap *expr = parse_expr(p);
		if (expr == NULL) {
			spn_object_release(ast);
			return NULL;
		}

		ast_push_child_xfer(ast, expr);

		/* comma ',' or closing bracket ']' must follow */
		if (accept_token_string(p, ",") == NULL && !is_at_token(p, "]")) {
			parser_error(p, "expecting ',' or ']' after array element", NULL);
			spn_object_release(ast);
			return NULL;
		}
	}

	return ast;
}

static void set_object_member_name_if_function(SpnHashMap *key, SpnHashMap *val)
{
	/* first, check if the key is a string literal */
	const char *keytype;
	SpnValue keyval;
	SpnValue keytype_val = spn_hashmap_get_strkey(key, "type");

	assert(isstring(&keytype_val));
	keytype = stringvalue(&keytype_val)->cstr;

	/* if key is not a literal, it can't be a string literal */
	if (strcmp(keytype, "literal") != 0) {
		return;
	}

	/* if the value of the literal is not a string,
	 * then the value is not a string literal either
	 */
	keyval = spn_hashmap_get_strkey(key, "value");
	if (!isstring(&keyval)) {
		return;
	}

	/* but if it is a string literal, _and_ the value
	 * is a function literal, then lets set its name.
	 */
	set_name_if_is_function(val, keyval);
}

/* parse a key in a hashmap literal */
static SpnHashMap *parse_hashmap_key(SpnParser *p)
{
	/* first, check if it's a single identifier;
	 * if so, transform it into a string literal.
	 */
	SpnToken *ident = lookahead(p, 0);
	SpnToken *colon = lookahead(p, 1);

	if (ident && colon
	 && ident->type == SPN_TOKEN_WORD
	 && colon->type == SPN_TOKEN_PUNCT
	 && strcmp(colon->value, ":") == 0) {
		/* skip identifier */
		accept_token_type(p, SPN_TOKEN_WORD);

		/* extract identifier into string literal */
		return ident_to_string(ident);
	}

	/* otherwise, fall back to parsing a generic expression */
	return parse_expr(p);
}

static SpnHashMap *parse_hashmap_literal(SpnParser *p)
{
	SpnHashMap *ast;
	SpnToken *lbrace = accept_token_string(p, "{");

	assert(lbrace != NULL);

	ast = ast_new("hashmap", lbrace->location);

	while (!accept_token_string(p, "}")) {
		SpnHashMap *key, *val, *pair;
		SpnToken *colon;

		/* parse key */
		key = parse_hashmap_key(p);
		if (key == NULL) {
			spn_object_release(ast);
			return NULL;
		}

		/* expect key-value delimiter */
		if ((colon = accept_token_string(p, ":")) == NULL) {
			parser_error(p, "expecting ':' between hashmap key and value", NULL);
			spn_object_release(key);
			spn_object_release(ast);
			return NULL;
		}

		/* parse value */
		val = parse_expr(p);
		if (val == NULL) {
			spn_object_release(key);
			spn_object_release(ast);
			return NULL;
		}

		/* add name to function literal if it's a method */
		set_object_member_name_if_function(key, val);

		/* construct key-value pair */
		pair = ast_new("kvpair", colon->location);
		ast_set_child_xfer(pair, "key", key);
		ast_set_child_xfer(pair, "value", val);
		ast_push_child_xfer(ast, pair);

		/* comma ',' or closing brace '}' must follow */
		if (accept_token_string(p, ",") == NULL && !is_at_token(p, "}")) {
			parser_error(p, "expecting ',' or '}' after key-value pair", NULL);
			spn_object_release(ast); /* this frees ast and param */
			return NULL;
		}
	}

	return ast;
}

static SpnArray *parse_decl_args(SpnParser *p)
{
	if (is_at_token(p, "(")) {
		return parse_decl_args_oldstyle(p);
	} else {
		return parse_decl_args_newstyle(p);
	}
}

static void emit_param_name_reserved_error(SpnParser *p, SpnToken *argname)
{
	const void *args[1];
	args[0] = argname->value;
	parser_error(p, "'%s' is a keyword and cannot be a parameter name", args);
}

static void push_param_name(SpnArray *array, SpnToken *argname)
{
	SpnValue argname_val = makestring(argname->value);
	spn_array_push(array, &argname_val);
	spn_value_release(&argname_val);
}

static SpnArray *parse_decl_args_oldstyle(SpnParser *p)
{
	SpnToken *param_name;
	SpnArray *array = spn_array_new();
	SpnToken *rparen;
	SpnToken *comma = NULL;

	assert(is_at_token(p, "("));
	accept_token_string(p, "(");

	while ((param_name = accept_token_type(p, SPN_TOKEN_WORD)) != NULL) {
		if (spn_token_is_reserved(param_name->value)) {
			emit_param_name_reserved_error(p, param_name);
			spn_object_release(array);
			return NULL;
		}

		push_param_name(array, param_name);

		/* comma ',' or closing parenthesis ')' must follow */
		comma = accept_token_string(p, ",");
		if (comma == NULL) {
			break;
		}
	}

	rparen = accept_token_string(p, ")");

	if (rparen == NULL) {
		parser_error(p, "expecting ')' or ',' after function argument", NULL);
		spn_object_release(array);
		return NULL;
	}

	if (comma != NULL) {
		parser_error(p, "trailing comma after last function argument", NULL);
		spn_object_release(array);
		return NULL;
	}

	return array;
}

static SpnArray *parse_decl_args_newstyle(SpnParser *p)
{
	SpnArray *array = spn_array_new();
	SpnToken *argname;

	while ((argname = accept_token_type(p, SPN_TOKEN_WORD)) != NULL) {
		if (spn_token_is_reserved(argname->value)) {
			emit_param_name_reserved_error(p, argname);
			spn_object_release(array);
			return NULL;
		}

		push_param_name(array, argname);
	}

	return array;
}

static SpnHashMap *parse_binexpr_rightassoc(
	SpnParser *p,
	const TokenAndNode tokens[],
	size_t n,
	SpnHashMap *(*subexpr)(SpnParser *)
)
{
	size_t index;
	SpnHashMap *left, *right, *node;
	SpnToken *op;

	left = subexpr(p);
	if (left == NULL) { /* error */
		return NULL;
	}

	op = accept_multi(p, tokens, n, &index);
	if (op == NULL) {
		return left;
	}

	/* apply right recursion */
	right = parse_binexpr_rightassoc(p, tokens, n, subexpr);

	if (right == NULL) { /* error */
		spn_object_release(left);
		return NULL;
	}

	node = ast_new(tokens[index].node, op->location);
	ast_set_child_xfer(node, "left", left);
	ast_set_child_xfer(node, "right", right);

	return node;
}

static SpnHashMap *parse_binexpr_leftassoc(
	SpnParser *p,
	const TokenAndNode tokens[],
	size_t n,
	SpnHashMap *(*subexpr)(SpnParser *)
)
{
	size_t index;
	SpnToken *op;

	SpnHashMap *ast = subexpr(p);
	if (ast == NULL) { /* error */
		return NULL;
	}

	/* iteration instead of left recursion (which wouldn't terminate) */
	while ((op = accept_multi(p, tokens, n, &index)) != NULL) {
		SpnHashMap *tmp;
		SpnHashMap *right = subexpr(p);

		if (right == NULL) {
			spn_object_release(ast);
			return NULL;
		}

		tmp = ast_new(tokens[index].node, op->location);
		ast_set_child_xfer(tmp, "left", ast);
		ast_set_child_xfer(tmp, "right", right);

		ast = tmp;
	}

	return ast;
}

static SpnHashMap *parse_binexpr_noassoc(
	SpnParser *p,
	const TokenAndNode tokens[],
	size_t n,
	SpnHashMap *(*subexpr)(SpnParser *)
)
{
	size_t index;
	SpnToken *op;
	SpnHashMap *left, *right, *top;

	left = subexpr(p);
	if (left == NULL) { /* error */
		return NULL;
	}

	op = accept_multi(p, tokens, n, &index);
	if (op == NULL) {
		return left;
	}

	right = subexpr(p);
	if (right == NULL) { /* error */
		spn_object_release(left);
		return NULL;
	}

	top = ast_new(tokens[index].node, op->location);
	ast_set_child_xfer(top, "left", left);
	ast_set_child_xfer(top, "right", right);

	return top;
}

/**************
 * Statements *
 **************/

static SpnHashMap *parse_if(SpnParser *p)
{
	SpnHashMap *cond, *br_then, *br_else, *ast;

	/* skip 'if' */
	SpnToken *token = accept_token_string(p, "if");
	assert(token != NULL);

	cond = parse_expr(p);
	if (cond == NULL) {
		return NULL;
	}

	br_then = parse_block_expecting(p, "if statement");
	if (br_then == NULL) {
		spn_object_release(cond);
		return NULL;
	}

	/* "else" is optional (hence we set its node to NULL beforehand).
	 * It may be followed by either a block or another if statement.
	 * The reason: we want to stay safe by enforcing blocks, but requiring
	 * the programmer to wrap each and every "else if" into a separate
	 * block is just insane and intolerably ugly -- "else if" is a
	 * common and safe enough construct to be allowed as an exception.
	 */
	br_else = NULL;
	if (accept_token_string(p, "else")) {
		if (is_at_token(p, "{")) {
			br_else = parse_block(p);
		} else if (is_at_token(p, "if")) {
			br_else = parse_if(p);
		} else {
			parser_error(p, "expecting block or 'if' in 'else' branch", NULL);
			spn_object_release(cond);
			spn_object_release(br_then);
			return NULL;
		}

		if (br_else == NULL) { /* error while parsing 'else' clause */
			spn_object_release(cond);
			spn_object_release(br_then);
			return NULL;
		}
	}

	ast = ast_new("if", token->location);

	ast_set_child_xfer(ast, "cond", cond);
	ast_set_child_xfer(ast, "then", br_then);

	if (br_else) {
		ast_set_child_xfer(ast, "else", br_else);
	}

	return ast;
}

static SpnHashMap *parse_while(SpnParser *p)
{
	SpnHashMap *cond, *body, *ast;

	/* skip 'while' */
	SpnToken *token = accept_token_string(p, "while");
	assert(token != NULL);

	cond = parse_expr(p);
	if (cond == NULL) {
		return NULL;
	}

	body = parse_block_expecting(p, "body of while loop");
	if (body == NULL) {
		spn_object_release(cond);
		return NULL;
	}

	ast = ast_new("while", token->location);
	ast_set_child_xfer(ast, "cond", cond);
	ast_set_child_xfer(ast, "body", body);
	return ast;
}

static SpnHashMap *parse_do(SpnParser *p)
{
	SpnHashMap *cond, *body, *ast;

	/* skip 'do' */
	SpnToken *token = accept_token_string(p, "do");
	assert(token != NULL);

	body = parse_block_expecting(p, "body of do-while loop");
	if (body == NULL) {
		return NULL;
	}

	/* expect "while expr;" */
	if (accept_token_string(p, "while") == NULL) {
		parser_error(p, "expecting 'while' after body of do-while loop", NULL);
		spn_object_release(body);
		return NULL;
	}

	cond = parse_expr(p);
	if (cond == NULL) {
		spn_object_release(body);
		return NULL;
	}

	if (accept_token_string(p, ";") == NULL) {
		parser_error(p, "expecting ';' after condition of do-while loop", NULL);
		spn_object_release(body);
		spn_object_release(cond);
		return NULL;
	}

	ast = ast_new("do", token->location);
	ast_set_child_xfer(ast, "cond", cond);
	ast_set_child_xfer(ast, "body", body);
	return ast;
}

static SpnHashMap *parse_for(SpnParser *p)
{
	SpnHashMap *ast, *cond, *incr, *body;

	/* skip 'for' */
	SpnToken *token = accept_token_string(p, "for");
	assert(token != NULL);

	ast = ast_new("for", token->location);

	/* the initialization may be either an expression or a declaration */
	if (is_at_token(p, "let")) {
		SpnHashMap *init = parse_vardecl(p);

		if (init == NULL) {
			spn_object_release(ast);
			return NULL;
		}

		ast_set_child_xfer(ast, "init", init);
	} else {
		SpnHashMap *init = parse_expr(p);

		if (init == NULL) {
			spn_object_release(ast);
			return NULL;
		}

		ast_set_child_xfer(ast, "init", init);

		if (accept_token_string(p, ";") == NULL) {
			parser_error(p, "expecting ';' after initialization of for loop", NULL);
			spn_object_release(ast);
			return NULL;
		}
	}

	cond = parse_expr(p);
	if (cond == NULL) {
		spn_object_release(ast);
		return NULL;
	}

	ast_set_child_xfer(ast, "cond", cond);

	if (accept_token_string(p, ";") == NULL) {
		parser_error(p, "expecting ';' after condition of for loop", NULL);
		spn_object_release(ast);
		return NULL;
	}

	incr = parse_expr(p);
	if (incr == NULL) {
		spn_object_release(ast);
		return NULL;
	}

	ast_set_child_xfer(ast, "increment", incr);

	body = parse_block_expecting(p, "body of for loop");
	if (body == NULL) {
		spn_object_release(ast);
		return NULL;
	}

	ast_set_child_xfer(ast, "body", body);

	return ast;
}

static SpnHashMap *parse_break(SpnParser *p)
{
	/* skip 'break' */
	SpnToken *token = accept_token_string(p, "break");
	assert(token != NULL);

	/* eat semicolon, if any */
	accept_token_string(p, ";");

	return ast_new("break", token->location);
}

static SpnHashMap *parse_continue(SpnParser *p)
{
	/* skip 'continue' */
	SpnToken *token = accept_token_string(p, "continue");
	assert(token != NULL);

	/* eat semicolon if any */
	accept_token_string(p, ";");

	return ast_new("continue", token->location);
}

static SpnHashMap *parse_return(SpnParser *p)
{
	SpnHashMap *expr, *ast;

	/* skip 'return' */
	SpnToken *token = accept_token_string(p, "return");
	assert(token != NULL);

	if (accept_token_string(p, ";")) {
		return ast_new("return", token->location); /* return without value */
	}

	expr = parse_expr(p);
	if (expr == NULL) {
		return NULL;
	}

	if (accept_token_string(p, ";") == NULL) {
		parser_error(p, "expecting ';' after expression in return statement", NULL);
		spn_object_release(expr);
		return NULL;
	}

	ast = ast_new("return", token->location);
	ast_set_child_xfer(ast, "expr", expr);
	return ast;
}

/* this builds a link list of comma-separated variable declarations */
static SpnHashMap *parse_vardecl(SpnParser *p)
{
	/* 'ast' is the head of the list */
	SpnHashMap *ast;

	/* skip "let" keyword */
	SpnToken *let = accept_token_string(p, "let");
	assert(let != NULL);

	ast = ast_new("vardecl", let->location);

	do {
		/* 'expr' is the optional initializer expression
		 * 'child' is the node that actually contains the identifier
		 * and the initialization expression.
		 */
		SpnHashMap *expr = NULL, *child;
		SpnToken *ident = accept_token_type(p, SPN_TOKEN_WORD);
		SpnValue identval;

		if (ident == NULL) {
			parser_error(p, "expected identifier in variable declaration", NULL);
			spn_object_release(ast);
			return NULL;
		}

		/* reserved keywords can't be used as variable or function names */
		if (spn_token_is_reserved(ident->value)) {
			const void *args[1];
			args[0] = ident->value;
			parser_error(p, "'%s' is a keyword and cannot be a variable name", args);
			spn_object_release(ast);
			return NULL;
		}

		identval = makestring(ident->value);

		/* the initializer expression is optional */
		if (accept_token_string(p, "=")) {
			expr = parse_expr(p);

			if (expr == NULL) {
				spn_value_release(&identval);
				spn_object_release(ast);
				return NULL;
			}

			/* if the initializer expression is a function,
			 * then its name should be the name of the variable
			 */
			set_name_if_is_function(expr, identval);
		}

		/* set up single variable declaration node... */
		child = ast_new("variable", ident->location);
		ast_set_property(child, "name", &identval);

		spn_value_release(&identval);

		if (expr) {
			ast_set_child_xfer(child, "init", expr);
		}

		/* ...and add it to the parent */
		ast_push_child_xfer(ast, child);
	} while (accept_token_string(p, ","));

	if (accept_token_string(p, ";") == NULL) {
		parser_error(p, "expected ';' after variable declaration", NULL);
		spn_object_release(ast);
		return NULL;
	}

	return ast;
}

static SpnHashMap *parse_extern(SpnParser *p)
{
	SpnHashMap *ast;

	/* skip "extern" keyword */
	SpnToken *token = accept_token_string(p, "extern");
	assert(token != NULL);

	ast = ast_new("constdecl", token->location);

	do {
		SpnHashMap *expr, *child;
		SpnValue identval;
		SpnToken *ident = accept_token_type(p, SPN_TOKEN_WORD);

		if (ident == NULL) {
			parser_error(p, "expected identifier in extern declaration", NULL);
			spn_object_release(ast);
			return NULL;
		}

		if (spn_token_is_reserved(ident->value)) {
			const void *args[1];
			args[0] = ident->value;
			parser_error(p, "'%s' is a keyword and cannot be the name of a global", args);
			spn_object_release(ast);
			return NULL;
		}

		if (accept_token_string(p, "=") == NULL) {
			parser_error(p, "expected '=' after name of extern declaration", NULL);
			spn_object_release(ast);
			return NULL;
		}

		expr = parse_expr(p);
		if (expr == NULL) {
			spn_object_release(ast);
			return NULL;
		}

		identval = makestring(ident->value);
		child = ast_new("constant", ident->location);

		set_name_if_is_function(expr, identval);
		ast_set_property(child, "name", &identval);
		ast_set_child_xfer(child, "init", expr);
		ast_push_child_xfer(ast, child);

		spn_value_release(&identval);
	} while (accept_token_string(p, ","));

	if (accept_token_string(p, ";") == NULL) {
		parser_error(p, "expected ';' after global initialization", NULL);
		spn_object_release(ast);
		return NULL;
	}

	return ast;
}

/* function statement */
static SpnHashMap *parse_fnstmt(SpnParser *p)
{
	SpnHashMap *fnexpr, *body, *ast, *var;
	SpnArray *declargs;
	SpnValue declargsval;
	SpnToken *token = accept_token_string(p, "fn");
	SpnToken *name = accept_token_type(p, SPN_TOKEN_WORD);
	SpnValue nameval;

	assert(token != NULL);

	/* parse function name */
	if (name == NULL) {
		parser_error(p, "expecting function name", NULL);
		return NULL;
	}

	/* make sure it's not a reserved keyword */
	if (spn_token_is_reserved(name->value)) {
		const void *args[1];
		args[0] = name->value;
		parser_error(p, "keyword '%s' cannot be used as a function name", args);
		return NULL;
	}

	/* parse formal parameters */
	declargs = parse_decl_args(p);
	if (declargs == NULL) {
		return NULL;
	}

	declargsval = makeobject(declargs);

	/* parse function body */
	body = parse_block_expecting(p, "function body");

	if (body == NULL) {
		spn_object_release(declargs);
		return NULL;
	}

	nameval = makestring(name->value);

	/* build function expression */
	fnexpr = ast_new("function", token->location);

	ast_set_property(fnexpr, "declargs", &declargsval);
	ast_set_property(fnexpr, "name", &nameval);
	ast_set_child_xfer(fnexpr, "body", body);

	/* variable name is the same as the name of the function */
	var = ast_new("variable", name->location);
	ast_set_property(var, "name", &nameval);

	/* relinquish ownership of values */
	spn_value_release(&declargsval);
	spn_value_release(&nameval);

	/* initializer of the variable is the function expression */
	ast_set_child_xfer(var, "init", fnexpr);

	/* the declaration statement has only one child (variable) */
	ast = ast_new("vardecl", token->location);
	ast_push_child_xfer(ast, var);

	return ast;
}

static SpnHashMap *parse_expr_stmt(SpnParser *p)
{
	SpnHashMap *ast = parse_expr(p);
	if (ast == NULL) {
		return NULL;
	}

	if (accept_token_string(p, ";") == NULL) {
		parser_error(p, "expected ';' after expression", NULL);
		spn_object_release(ast);
		return NULL;
	}

	return ast;
}

static SpnHashMap *parse_empty(SpnParser *p)
{
	/* skip semicolon */
	SpnToken *semicolon = accept_token_string(p, ";");
	assert(semicolon != NULL);

	return ast_new("empty", semicolon->location);
}
