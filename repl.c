/* 
 * repl.c
 * The Sparkling interpreter
 * Created by Árpád Goretity on 05/10/2013.
 * 
 * Licensed under the 2-clause BSD License
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "spn.h"
#include "array.h"
#include "ctx.h"
#include "repl.h"
#include "disasm.h"


#define N_CMDS		4
#define N_FLAGS		2
#define N_ARGS		(N_CMDS + N_FLAGS)

#define CMDS_MASK	0x00ff
#define FLAGS_MASK	0xff00

#ifndef LINE_MAX
#define LINE_MAX	0x1000
#endif

enum cmd_args {
	CMD_HELP	= 1 << 0,
	CMD_EXECUTE	= 1 << 1,
	CMD_COMPILE	= 1 << 2,
	CMD_DISASM	= 1 << 3,

	FLAG_PRINTNIL	= 1 << 8,
	FLAG_PRINTRET	= 1 << 9
};

/* `pos' is the index of the first non-option */
static enum cmd_args process_args(int argc, char *argv[], int *pos)
{
	static const struct {
		const char *shopt; /* short option */
		const char *lnopt; /* long option */
		enum cmd_args mask;
	} args[N_ARGS] = {
		{ "-h",	"--help",	CMD_HELP	},
		{ "-e",	"--execute",	CMD_EXECUTE	},
		{ "-c", "--compile",	CMD_COMPILE	},
		{ "-d", "--disasm",	CMD_DISASM	},
		{ "-n", "--print-nil",	FLAG_PRINTNIL	},
		{ "-t", "--print-ret",	FLAG_PRINTRET	}
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
	printf("\t-e, --execute\tExecute command-line arguments\n");
	printf("\t-c, --compile\tCompile source files to bytecode\n");
	printf("\t-d, --disasm\tDisassemble bytecode files\n\n");
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
	/* if the error message of the contex is the same as
	 * the error message of the virtual machine, then a
	 * runtime error occurred, so we print a stack trace.
	 */
	if (spn_vm_geterrmsg(ctx->vm) == ctx->errmsg) {
		size_t n;
		unsigned i;
		const char **bt = spn_vm_stacktrace(ctx->vm, &n);

		fprintf(stderr, "Call stack:\n\n");

		for (i = 0; i < n; i++) {
			fprintf(stderr, "\t[%-4u]\tin %s\n", i, bt[i]);
		}

		fprintf(stderr, "\n");

		free(bt);
	}
}

static void register_args(SpnContext *ctx, int argc, char *argv[])
{
	int i;
	SpnExtValue vals;
	SpnArray *arr = spn_array_new();

	for (i = 0; i < argc; i++) {
		SpnValue key, val;
		SpnString *str = spn_string_new_nocopy(argv[i], 0);

		key.t = SPN_TYPE_NUMBER;
		key.f = 0;
		key.v.intv = i;

		val.t = SPN_TYPE_STRING;
		val.f = SPN_TFLG_OBJECT;
		val.v.ptrv = str;

		spn_array_set(arr, &key, &val);
		spn_object_release(str);
	}

	vals.name = "argv";
	vals.value.t = SPN_TYPE_ARRAY;
	vals.value.f = SPN_TFLG_OBJECT;
	vals.value.v.ptrv = arr;

	spn_vm_addglobals(ctx->vm, &vals, 1);
	spn_object_release(arr);
}

/* This checks if the file starts with a shebang, so that it can be run as a
 * stand-alone script if the shell supports this notation. This is necessary
 * because Sparkling doesn't recognize '#' as a line comment delimiter.
 */
static int run_script_file(SpnContext *ctx, const char *fname)
{
	char *buf = spn_read_text_file(fname);
	const char *src;
	int err;

	if (buf == NULL) {
		ctx->errmsg = "Sparkling: I/O error: cannot read file";
		return -1;
	}

	/* if starts with shebang, search for beginning of 2nd line */
	if (buf[0] == '#' && buf[1] == '!') {
		const char *pn = strchr(buf, '\n');
		const char *pr = strchr(buf, '\r');

		if (pn == NULL && pr == NULL) {
			/* empty script */
			free(buf);
			return 0;
		} else {
			if (pn == NULL) {
				src = pr + 1;
			} else if (pr == NULL) {
				src = pn + 1;
			} else {
				src = pn < pr ? pr + 1 : pn + 1;
			}
		}
	} else {
		src = buf;
	}

	err = spn_ctx_execstring(ctx, src, NULL);
	free(buf);
	return err;
}

static int run_file(const char *fname, int argc, char *argv[])
{
	SpnContext *ctx = spn_ctx_new();
	int status = EXIT_SUCCESS;
	int err;

	/* register command-line arguments */
	register_args(ctx, argc, argv);

	/* check if file is a binary object or source text */
	if (endswith(fname, ".spn")) {
		err = run_script_file(ctx, fname);
	} else if (endswith(fname, ".spo")) {
		err = spn_ctx_execobjfile(ctx, fname, NULL);
	} else {
		err = -1;
		ctx->errmsg = "Sparkling: generic error: invalid file extension";
	}

	if (err != 0) {
		fprintf(stderr, "%s\n", ctx->errmsg);
		print_stacktrace_if_needed(ctx);
		status = EXIT_FAILURE;
	}

	spn_ctx_free(ctx);
	return status;
}

static int run_args(int argc, char *argv[], enum cmd_args args)
{
	SpnContext *ctx = spn_ctx_new();
	int status = EXIT_SUCCESS;

	int i;
	for (i = 0; i < argc; i++) {
		SpnValue val;
		if (spn_ctx_execstring(ctx, argv[i], &val) != 0) {
			fprintf(stderr, "%s\n", ctx->errmsg);
			print_stacktrace_if_needed(ctx);
			status = EXIT_FAILURE;
			break;
		}

		if (args & FLAG_PRINTRET) {
			spn_value_print(&val);
			printf("\n");
		}

		spn_value_release(&val);
	}

	spn_ctx_free(ctx);
	return status;
}

static int enter_repl(enum cmd_args args)
{
	SpnContext *ctx = spn_ctx_new();

	while (1) {
		SpnValue ret;
		int status;

#if USE_READLINE
		char *buf;
#else
		static char buf[LINE_MAX];
#endif

#if USE_READLINE
		if ((buf = readline("> ")) == NULL) {
			printf("\n");
			break;
		}

		/* only add non-empty lines to the history */
		if (buf[0] != 0) {
			add_history(buf);
		}
#else
		printf("> ");
		if (fgets(buf, sizeof buf, stdin) == NULL) {
			printf("\n");
			break;
		}
#endif

		status = spn_ctx_execstring(ctx, buf, &ret);
		if (status != 0) {
			fprintf(stderr, "%s\n", ctx->errmsg);
			print_stacktrace_if_needed(ctx);
		} else {
			if (ret.t != SPN_TYPE_NIL || args & FLAG_PRINTNIL) {
				spn_value_print(&ret);
				printf("\n");
			}

			spn_value_release(&ret);
		}

#if USE_READLINE
		free(buf);
#endif
	}

	spn_ctx_free(ctx);
	return EXIT_SUCCESS;
}

/* XXX: this function modifies filenames in `argv' */
static int compile_files(int argc, char *argv[])
{
	SpnContext *ctx = spn_ctx_new();
	int status = EXIT_SUCCESS;

	int i;
	for (i = 0; i < argc; i++) {
		static char outname[FILENAME_MAX];
		char *dotp;
		FILE *outfile;
		spn_uword *bc;
		size_t nwords;

		printf("compiling file `%s'...", argv[i]);
		fflush(stdout);
		fflush(stderr);

		bc = spn_ctx_loadsrcfile(ctx, argv[i]);
		if (bc == NULL) {
			fprintf(stderr, "\n%s\n", ctx->errmsg);
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
			fprintf(stderr, "\nSparkling: I/O error: can't open file `%s'\n", outname);
			status = EXIT_FAILURE;
			break;
		}

		nwords = ctx->bclist->len;
		if (fwrite(bc, sizeof(*bc), nwords, outfile) < nwords) {
			fprintf(stderr, "\nSparkling: I/O error: can't write to file `%s'\n", outname);
			fclose(outfile);
			status = EXIT_FAILURE;
			break;
		}

		fclose(outfile);

		printf(" done.\n");
	}

	spn_ctx_free(ctx);
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
			fprintf(stderr, "Sparkling: I/O error: could not read file `%s'\n", argv[i]);
			status = EXIT_FAILURE;
			break;
		}

		printf("Assembly dump of file %s:\n\n", argv[i]);

		bclen = fsz / sizeof(bc[0]);
		spn_disasm(bc, bclen);

		printf("--------\n\n");

		free(bc);
	}

	return status;
}

static void print_version()
{
	printf("Sparkling build %s, copyright (C) 2013-2014, Árpád Goretity\n\n", REPL_VERSION);
}

int main(int argc, char *argv[])
{
	int status, pos;
	enum cmd_args args;

	if (argc < 1) {
		fprintf(stderr, "Sparkling: internal error: argc < 1\n\n");
		exit(EXIT_FAILURE);
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
	case CMD_EXECUTE:
		status = run_args(argc - pos, &argv[pos], args);
		break;
	case CMD_COMPILE:
		print_version();
		/* XXX: this function modifies filenames in `argv' */
		status = compile_files(argc - pos, &argv[pos]);
		break;
	case CMD_DISASM:
		print_version();
		status = disassemble_files(argc - pos, &argv[pos]);
		break;
	default:
		fprintf(stderr, "Sparkling: generic error: internal inconsistency\n\n");
		status = EXIT_FAILURE;
	}

	return status;
}

