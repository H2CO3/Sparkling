/*
 * spn.c
 * The Sparkling interpreter
 * Created by Árpád Goretity on 05/10/2013.
 *
 * Licensed under the 2-clause BSD License
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#if USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#if USE_ANSI_COLORS
#define CLR_ERR "\x1b[1;31m"
#define CLR_VAL "\x1b[1;32m"
#define CLR_RST "\x1b[0m"
#else
#define CLR_ERR ""
#define CLR_VAL ""
#define CLR_RST ""
#endif

#include "spn.h"
#include "array.h"
#include "str.h"
#include "func.h"
#include "ctx.h"
#include "private.h"
#include "dump.h"

#define N_CMDS     6
#define N_FLAGS    2
#define N_ARGS    (N_CMDS + N_FLAGS)

#define CMDS_MASK  0x00ff
#define FLAGS_MASK 0xff00

#ifndef LINE_MAX
#define LINE_MAX   0x1000
#endif

enum cmd_args {
	CMD_HELP      = 1 << 0,
	CMD_EVAL      = 1 << 1,
	CMD_RUN       = 1 << 2,
	CMD_COMPILE   = 1 << 3,
	CMD_DISASM    = 1 << 4,
	CMD_DUMPAST   = 1 << 5,

	FLAG_PRINTNIL = 1 << 8,
	FLAG_PRINTRET = 1 << 9
};

/* 'pos' is the index of the first non-option */
static enum cmd_args process_args(int argc, char *argv[], int *pos)
{
	static const struct {
		const char *shopt; /* short option */
		const char *lnopt; /* long option */
		enum cmd_args mask;
	} args[N_ARGS] = {
		{ "-h", "--help",      CMD_HELP      },
		{ "-e", "--eval",      CMD_EVAL      },
		{ "-r", "--run",       CMD_RUN       },
		{ "-c", "--compile",   CMD_COMPILE   },
		{ "-d", "--disasm",    CMD_DISASM    },
		{ "-a", "--dump-ast",  CMD_DUMPAST   },
		{ "-n", "--print-nil", FLAG_PRINTNIL },
		{ "-t", "--print-ret", FLAG_PRINTRET }
	};

	enum cmd_args opts = 0;

	int i;
	for (i = 1; i < argc; i++) {
		/* search the first non-command and non-flag argument:
		 * it is the file to be processed (or an unrecognized flag)
		 */

		enum cmd_args arg = 0;
		int j;
		for (j = 0; j < N_ARGS; j++) {
			if (strcmp(argv[i], args[j].shopt) == 0
			 || strcmp(argv[i], args[j].lnopt) == 0) {
				arg = args[j].mask;
				break;
			}
		}

		if (arg == 0) {
			/* not an option or unrecognized */
			break;
		} else {
			opts |= arg;
		}
	}

	*pos = i;
	return opts;
}

static void show_help(const char *progname)
{
	printf("Usage: %s [command] [flags...] [file [scriptargs...]] \n", progname);
	printf("Where <command> is one of:\n\n");
	printf("\t-h, --help\tShow this help then exit\n");
	printf("\t-e, --eval\tEvaluate command-line arguments as expressions\n");
	printf("\t-r, --run\tRun command-line arguments as statements\n");
	printf("\t-c, --compile\tCompile source files to bytecode\n");
	printf("\t-d, --disasm\tDisassemble bytecode files\n");
	printf("\t-a, --dump-ast\tDump abstract syntax tree of files\n\n");
	printf("Flags consist of zero or more of the following options:\n\n");
	printf("\t-n, --print-nil\tPrint nil return values in REPL\n");
	printf("\t-t, --print-ret\tPrint result of scripts passed as arguments\n\n");
	printf("Please send bug reports via GitHub:\n\n");
	printf("\t<http://github.com/H2CO3/Sparkling>\n\n");
}

/* this is used to check the extension of a file in order to
 * decide if it's a text (source) or a binary (object) file
 */
static int endswith(const char *haystack, const char *needle)
{
	size_t hsl = strlen(haystack);
	size_t ndl = strlen(needle);

	if (hsl < ndl) {
		return 0;
	}

	return strstr(haystack + hsl - ndl, needle) != NULL;
}

static void print_stacktrace_if_needed(SpnContext *ctx)
{
	 /* if a runtime error occurred, we print a stack trace. */
	if (spn_ctx_geterrtype(ctx) == SPN_ERROR_RUNTIME) {
		size_t n;
		unsigned i;
		long addr = spn_ctx_exception_addr(ctx);
		SpnStackFrame *bt = spn_ctx_stacktrace(ctx, &n);

		if (addr < 0) {
			fprintf(stderr, "Runtime error in native code");
		} else {
			fprintf(stderr, "Runtime error @ %#08lx", addr);
		}

		fprintf(stderr, " - Call stack:\n\n");

		for (i = 0; i < n; i++) {
			fprintf(stderr, "\t[%-4u]\tin %s\n", i, bt[i].function->name);
		}

		fprintf(stderr, "\n");

		free(bt);
	}
}

static void print_location_and_errmsg(SpnSourceLocation loc, const char *errmsg)
{
	fprintf(stderr, "near line %u, char %u: %s\n", loc.line, loc.column, errmsg);
}

static int run_script_file(SpnContext *ctx, const char *fname, int argc, char *argv[])
{
	SpnValue *vals;
	int err, i;

	/* compile */
	SpnFunction *fn = spn_ctx_compile_srcfile(ctx, fname);

	if (fn == NULL) {
		enum spn_error_type errtype = spn_ctx_geterrtype(ctx);

		if (errtype == SPN_ERROR_SYNTAX || errtype == SPN_ERROR_SEMANTIC) {
			SpnSourceLocation loc = spn_ctx_geterrloc(ctx);
			const char *errmsg = spn_ctx_geterrmsg(ctx);
			print_location_and_errmsg(loc, errmsg);
		} else {
			fprintf(stderr, "%s\n", spn_ctx_geterrmsg(ctx));
		}

		return -1;
	}

	/* make arguments array */
	vals = spn_malloc(argc * sizeof vals[0]);
	for (i = 0; i < argc; i++) {
		vals[i] = makestring_nocopy(argv[i]);
	}

	/* run program; throw away return value */
	err = spn_ctx_callfunc(ctx, fn, NULL, argc, vals);

	/* free arguments array */
	for (i = 0; i < argc; i++) {
		spn_value_release(&vals[i]);
	}

	free(vals);

	if (err != 0) {
		/* this can only mean a runtime error */
		fprintf(stderr, "%s\n", spn_ctx_geterrmsg(ctx));
		print_stacktrace_if_needed(ctx);
	}

	return err;
}

static int run_file(const char *fname, int argc, char *argv[])
{
	int status = EXIT_SUCCESS;

	SpnContext ctx;
	spn_ctx_init(&ctx);

	/* check if file is a binary object or source text */
	if (endswith(fname, ".spn")) {
		if (run_script_file(&ctx, fname, argc, argv) != 0) {
			status = EXIT_FAILURE;
		}
	} else if (endswith(fname, ".spo")) {
		if (spn_ctx_execobjfile(&ctx, fname, NULL) != 0) {
			fprintf(stderr, "%s\n", spn_ctx_geterrmsg(&ctx));
			print_stacktrace_if_needed(&ctx);
			status = EXIT_FAILURE;
		}
	} else {
		fputs("generic error: invalid file extension\n", stderr);
		status = EXIT_FAILURE;
	}

	spn_ctx_free(&ctx);
	return status;
}

static int eval_args(int argc, char *argv[])
{
	int status = EXIT_SUCCESS;
	int i;

	SpnContext ctx;
	spn_ctx_init(&ctx);

	for (i = 0; i < argc; i++) {
		SpnValue val;
		SpnFunction *fn = spn_ctx_compile_expr(&ctx, argv[i]);
		if (fn == NULL) {
			SpnSourceLocation loc = spn_ctx_geterrloc(&ctx);
			const char *errmsg = spn_ctx_geterrmsg(&ctx);

			print_location_and_errmsg(loc, errmsg);
			status = EXIT_FAILURE;
			continue;
		}

		if (spn_ctx_callfunc(&ctx, fn, &val, 0, NULL) != 0) {
			printf("%s\n", spn_ctx_geterrmsg(&ctx));
			print_stacktrace_if_needed(&ctx);

			status = EXIT_FAILURE;
			continue;
		}

		spn_repl_print(&val);
		spn_value_release(&val);
		printf("\n");
	}

	spn_ctx_free(&ctx);
	return status;
}

static int run_args(int argc, char *argv[], enum cmd_args args)
{
	int status = EXIT_SUCCESS;
	int i;

	SpnContext ctx;
	spn_ctx_init(&ctx);

	for (i = 0; i < argc; i++) {
		SpnValue val;
		if (spn_ctx_execstring(&ctx, argv[i], &val) != 0) {
			const char *errmsg = spn_ctx_geterrmsg(&ctx);
			enum spn_error_type errtype = spn_ctx_geterrtype(&ctx);

			if (errtype == SPN_ERROR_SYNTAX || errtype == SPN_ERROR_SEMANTIC) {
				SpnSourceLocation loc = spn_ctx_geterrloc(&ctx);
				print_location_and_errmsg(loc, errmsg);
			} else {
				fprintf(stderr, "%s\n", errmsg);
				print_stacktrace_if_needed(&ctx);
			}

			status = EXIT_FAILURE;
			break;
		}

		if (args & FLAG_PRINTRET) {
			spn_repl_print(&val);
			printf("\n");
		}

		spn_value_release(&val);
	}

	spn_ctx_free(&ctx);
	return status;
}

#if USE_READLINE
static char *history_file_name(void)
{
	char *history_path;
	size_t homedir_len, fname_len;

	const char *history_filename = ".spn_history";
	const char *homedir = getenv("HOME");

	if (homedir == NULL) {
		return NULL;
	}

	/* construct history file name */
	homedir_len = strlen(homedir);
	fname_len = strlen(history_filename);

	/* +1 for '/', +1 for terminating NUL */
	history_path = spn_malloc(homedir_len + 1 + fname_len + 1);
	strcpy(history_path, homedir);
	strcat(history_path, "/");
	strcat(history_path, history_filename);

	return history_path;
}
#endif /* USE_READLINE */

static int enter_repl(enum cmd_args args)
{
	int session_no = 1;

#if USE_READLINE
	char *fhistory = history_file_name();
#endif

	SpnContext ctx;
	spn_ctx_init(&ctx);

#if USE_READLINE
	/* try reading the history file */
	read_history(fhistory);
#endif

	while (1) {
		SpnValue ret;
		int status;

#if USE_READLINE
		char *buf;

		/* 32 characters are enough for 'spn:> ' and the 0-terminator;
		 * CHAR_BIT * sizeof session_no is enough for session_no
		 */
		char prompt[CHAR_BIT * sizeof session_no + 32];
#else
		static char buf[LINE_MAX];
#endif

#if USE_READLINE
		sprintf(prompt, "spn:%d> ", session_no);
		fflush(stdout);

		if ((buf = readline(prompt)) == NULL) {
			printf("\n");
			break;
		}

		/* only add non-empty lines to the history */
		if (buf[0] != 0) {
			add_history(buf);
		}
#else
		printf("spn:%d> ", session_no);
		fflush(stdout);

		if (fgets(buf, sizeof buf, stdin) == NULL) {
			printf("\n");
			break;
		}
#endif

		/* first, try treating the input as a statement.
		 * If that fails, try interpreting it as an expression.
		 */
		status = spn_ctx_execstring(&ctx, buf, &ret);
		if (status != 0) {
			if (spn_ctx_geterrtype(&ctx) == SPN_ERROR_RUNTIME) {
				fprintf(stderr, CLR_ERR "%s" CLR_RST "\n", spn_ctx_geterrmsg(&ctx));
				print_stacktrace_if_needed(&ctx);
			} else {
				SpnFunction *fn;
				/* Save the original error message and location,
				 * because it's probably going to be more meaningful.
				 */
				SpnSourceLocation orig_loc = spn_ctx_geterrloc(&ctx);
				const char *errmsg = spn_ctx_geterrmsg(&ctx);
				size_t len = strlen(errmsg);
				char *orig_errmsg = spn_malloc(len + 1);
				memcpy(orig_errmsg, errmsg, len + 1);

				/* if the error was a syntactic or semantic error, then
				 * probably it was already there originally (when we were
				 * treating the source as a statement). So we print the
				 * original error message instead.
				 * If, however, the error is a run-time exception, then
				 * we managed to parse and compile the string as an
				 * expression, so it's the new error message that is relevant.
				 */
				fn = spn_ctx_compile_expr(&ctx, buf);
				if (fn == NULL) {
					/* this is a parser or compiler error */
					fprintf(stderr, CLR_ERR);
					print_location_and_errmsg(orig_loc, orig_errmsg);
					fprintf(stderr, CLR_RST "\n");
				} else {
					if (spn_ctx_callfunc(&ctx, fn, &ret, 0, NULL) != 0) {
						/* this will be a runtime error */
						fprintf(stderr, CLR_ERR "%s" CLR_RST "\n", spn_ctx_geterrmsg(&ctx));
						print_stacktrace_if_needed(&ctx);
					} else {
						printf("= " CLR_VAL);
						spn_repl_print(&ret);
						printf(CLR_RST "\n");
						spn_value_release(&ret);
					}
				}

				free(orig_errmsg);
			}
		} else {
			if (notnil(&ret) || args & FLAG_PRINTNIL) {
				printf(CLR_VAL);
				spn_repl_print(&ret);
				printf(CLR_RST "\n");
			}

			spn_value_release(&ret);
		}

#if USE_READLINE
		free(buf);
#endif

		session_no++;
	}

	spn_ctx_free(&ctx);

#if USE_READLINE
	write_history(fhistory);
	free(fhistory);
#endif

	return EXIT_SUCCESS;
}

/* XXX: this function modifies filenames in 'argv' */
static int compile_files(int argc, char *argv[])
{
	int status = EXIT_SUCCESS;
	int i;

	SpnContext ctx;
	spn_ctx_init(&ctx);

	for (i = 0; i < argc; i++) {
		static char outname[FILENAME_MAX];
		char *dotp;
		FILE *outfile;
		SpnFunction *fn;
		spn_uword *bc;
		size_t nwords;

		printf("compiling file '%s'...", argv[i]);
		fflush(stdout);
		fflush(stderr);

		fn = spn_ctx_compile_srcfile(&ctx, argv[i]);
		if (fn == NULL) {
			SpnSourceLocation loc = spn_ctx_geterrloc(&ctx);
			const char *errmsg = spn_ctx_geterrmsg(&ctx);
			printf("\n");
			print_location_and_errmsg(loc, errmsg);
			status = EXIT_FAILURE;
			break;
		}

		/* cut off extension, construct output file name */
		dotp = strrchr(argv[i], '.');
		if (dotp != NULL) {
			*dotp = 0;
		}

		sprintf(outname, "%s.spo", argv[i]);

		outfile = fopen(outname, "wb");
		if (outfile == NULL) {
			fprintf(stderr, "\nI/O error: can't open file '%s'\n", outname);
			status = EXIT_FAILURE;
			break;
		}

		assert(fn->topprg);
		bc = fn->repr.bc;
		nwords = fn->nwords;

		if (fwrite(bc, sizeof bc[0], nwords, outfile) < nwords) {
			fprintf(stderr, "\nI/O error: can't write to file '%s'\n", outname);
			fclose(outfile);
			status = EXIT_FAILURE;
			break;
		}

		fclose(outfile);

		printf(" done.\n");
	}

	spn_ctx_free(&ctx);
	return status;
}


static int disassemble_files(int argc, char *argv[])
{
	int status = EXIT_SUCCESS;
	int i;

	for (i = 0; i < argc; i++) {
		spn_uword *bc;
		size_t fsz, bclen;

		bc = spn_read_binary_file(argv[i], &fsz);
		if (bc == NULL) {
			fprintf(stderr, "I/O error: could not read file '%s'\n", argv[i]);
			status = EXIT_FAILURE;
			break;
		}

		printf("Assembly dump of file %s:\n\n", argv[i]);

		bclen = fsz / sizeof(bc[0]);
		if (spn_dump_assembly(bc, bclen) != 0) {
			free(bc);
			status = EXIT_FAILURE;
			break;
		}

		printf("--------\n\n");

		free(bc);
	}

	return status;
}


static int dump_ast_of_files(int argc, char *argv[])
{
	int i;
	int status = EXIT_SUCCESS;

	SpnParser parser;
	spn_parser_init(&parser);

	for (i = 0; i < argc; i++) {
		SpnHashMap *ast;
		SpnValue astval;

		char *src = spn_read_text_file(argv[i]);
		if (src == NULL) {
			fprintf(stderr, "I/O error: cannot read file '%s'\n", argv[i]);
			status = EXIT_FAILURE;
			break;
		}

		ast = spn_parser_parse(&parser, src);
		free(src);

		if (ast == NULL) {
			SpnSourceLocation loc = spn_parser_get_error_location(&parser);
			print_location_and_errmsg(loc, parser.errmsg);
			status = EXIT_FAILURE;
			break;
		}

		astval.type = SPN_TYPE_HASHMAP;
		astval.v.o = ast;
		spn_repl_print(&astval);
		spn_object_release(ast);

		printf("\n");
	}

	spn_parser_free(&parser);
	return status;
}

static void print_version()
{
	printf("Sparkling build %s, copyright (C) 2013-2015, Árpád Goretity\n\n", REPL_VERSION);
}

int main(int argc, char *argv[])
{
	int status, pos;
	enum cmd_args args;

	if (argc < 1) {
		spn_die("internal error: argc < 1\n\n");
	}

	args = process_args(argc, argv, &pos);

	switch (args & CMDS_MASK) {
	case 0:
		/* if no files are given, then enter the REPL.
		 * Else run the specified file with the given arguments.
		 */
		if (pos == argc) {
			print_version();
			status = enter_repl(args);
		} else {
			status = run_file(argv[pos], argc - pos, &argv[pos]);
		}

		break;
	case CMD_HELP:
		show_help(argv[0]);
		status = EXIT_SUCCESS;
		break;
	case CMD_EVAL:
		status = eval_args(argc - pos, &argv[pos]);
		break;
	case CMD_RUN:
		status = run_args(argc - pos, &argv[pos], args);
		break;
	case CMD_COMPILE:
		/* XXX: this function modifies filenames in 'argv' */
		status = compile_files(argc - pos, &argv[pos]);
		break;
	case CMD_DISASM:
		status = disassemble_files(argc - pos, &argv[pos]);
		break;
	case CMD_DUMPAST:
		status = dump_ast_of_files(argc - pos, &argv[pos]);
		break;
	default:
		fprintf(stderr, "error: more than one command specified\n");
		status = EXIT_FAILURE;
		break;
	}

	return status;
}
