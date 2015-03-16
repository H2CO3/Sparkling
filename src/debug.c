/*
 * debug.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 15/03/2015
 * Licensed under the 2-clause BSD License
 *
 * Emitting debugging information
 */

#include "debug.h"
#include "array.h"
#include "private.h"

SpnHashMap *spn_dbg_new(void)
{
	SpnHashMap *debug_info = spn_hashmap_new();

	/* insns: maps bytecode address to source location
	 * vars: maps address and variable name to register number
	 */
	SpnValue insns = makearray();
	SpnValue vars = makearray();

	spn_hashmap_set_strkey(debug_info, "insns", &insns);
	spn_hashmap_set_strkey(debug_info, "vars", &vars);

	spn_value_release(&insns);
	spn_value_release(&vars);

	return debug_info;
}

void spn_dbg_emit_source_location(
	SpnHashMap *debug_info,
	size_t begin,
	size_t end,
	SpnHashMap *ast,
	int regno
)
{
	SpnValue vinsns;
	SpnArray *insns;
	SpnValue vexpr;
	SpnValue line, column;
	SpnValue vbegin, vend;
	SpnValue vregno;
	SpnHashMap *expr;

	/* if we are not asked to emit debug info, give up */
	if (debug_info == NULL) {
		return;
	}

	vinsns = spn_hashmap_get_strkey(debug_info, "insns");
	insns = arrayvalue(&vinsns);

	vexpr = makehashmap();
	expr = hashmapvalue(&vexpr);

	line = spn_hashmap_get_strkey(ast, "line");
	column = spn_hashmap_get_strkey(ast, "column");

	vbegin = makeint(begin);
	vend = makeint(end);
	vregno = makeint(regno);

	spn_hashmap_set_strkey(expr, "line", &line);
	spn_hashmap_set_strkey(expr, "column", &column);
	spn_hashmap_set_strkey(expr, "begin", &vbegin);
	spn_hashmap_set_strkey(expr, "end", &vend);
	spn_hashmap_set_strkey(expr, "register", &vregno);

	spn_array_push(insns, &vexpr);
	spn_value_release(&vexpr);
}

void spn_dbg_set_filename(SpnHashMap *debug_info, const char *fname)
{
	if (debug_info) {
		SpnValue str = makestring(fname);
		spn_hashmap_set_strkey(debug_info, "file", &str);
		spn_value_release(&str);
	}
}

const char *spn_dbg_get_filename(SpnHashMap *debug_info)
{
	if (debug_info) {
		SpnValue fname = spn_hashmap_get_strkey(debug_info, "file");

		if (isstring(&fname)) {
			return stringvalue(&fname)->cstr;
		}
	}

	return "???";
}

SpnSourceLocation spn_dbg_get_frame_source_location(SpnStackFrame frame)
{
	SpnHashMap *debug_info = NULL;

	if (frame.function->env) {
		debug_info = frame.function->env->debug_info;
	}

	return spn_dbg_get_raw_source_location(debug_info, frame.exc_address);
}

SpnSourceLocation spn_dbg_get_raw_source_location(SpnHashMap *debug_info, ptrdiff_t address)
{
	SpnSourceLocation loc = { 0, 0 };

	if (debug_info) {
		SpnValue vinsns = spn_hashmap_get_strkey(debug_info, "insns");
		SpnArray *insns = arrayvalue(&vinsns);

		size_t n = spn_array_count(insns);
		size_t i;

		/* this is a 'long', because there's no PTRDIFF_MAX in C89 */
		long address_window_width = LONG_MAX;

		/* search for narrowest bytecode range containing 'address'.
		 * XXX: TODO: this may potentially be slow for large files;
		 * benchmark it and use binary search instead, if necessary.
		 */
		for (i = 0; i < n; i++) {
			SpnValue vexpression = spn_array_get(insns, i);
			SpnHashMap *expression = hashmapvalue(&vexpression);

			SpnValue vline = spn_hashmap_get_strkey(expression, "line");
			SpnValue vcolumn = spn_hashmap_get_strkey(expression, "column");
			SpnValue vbegin = spn_hashmap_get_strkey(expression, "begin");
			SpnValue vend = spn_hashmap_get_strkey(expression, "end");

			unsigned line = intvalue(&vline);
			unsigned column = intvalue(&vcolumn);
			ptrdiff_t begin = intvalue(&vbegin);
			ptrdiff_t end = intvalue(&vend);

			if (begin <= address && address < end
			 && end - begin < address_window_width) {
				/* if the range contains the target address, and it
				 * is narrower than the previous one, then memoize it
				 */
				loc.line = line;
				loc.column = column;

				address_window_width = end - begin;
			}
		}
	}

	return loc;
}
