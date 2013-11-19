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


#define N_CMDS		6
#define N_FLAGS		2
#define N_ARGS		(N_CMDS + N_FLAGS)

#define CMDS_MASK	0x00ff
#define FLAGS_MASK	0xff00

#ifndef LINE_MAX
#define LINE_MAX	0x1000
#endif

enum cmd_args {
	CMD_HELP	= 1 << 0,
	CMD_RUN		= 1 << 1,
	CMD_EXECUTE	= 1 << 2,
	CMD_INTERACT	= 1 << 3,
	CMD_COMPILE	= 1 << 4,
	CMD_DISASM	= 1 << 5,

	FLAG_PRINTNIL	= 1 << 8,
	FLAG_PRINTRET	= 1 << 9
};

static enum cmd_args process_args(int argc, char *argv[])
{
	static const struct {
		const char *shopt; /* short option */
		const char *lnopt; /* long option */
		enum cmd_args mask;
	} args[N_ARGS] = {
		{ "-h",	"--help",	CMD_HELP	},
		{ "-r",	"--run",	CMD_RUN		},
		{ "-e",	"--execute",	CMD_EXECUTE	},
		{ "-i", "--interact",	CMD_INTERACT	},
		{ "-c", "--compile",	CMD_COMPILE	},
		{ "-d", "--disasm",	CMD_DISASM	},
		{ "-n", "--print-nil",	FLAG_PRINTNIL	},
		{ "-t", "--print-ret",	FLAG_PRINTRET	}
	};

	enum cmd_args opts = 0;

	int i;
	for (i = 1; i < argc; i++) {
		int j;

		/* stop processing at first occurrence of "--" */
		if (strcmp(argv[i], "--") == 0) {
			break;
		}

		for (j = 0; j < N_ARGS; j++) {
			if (strcmp(argv[i], args[j].shopt) == 0
			 || strcmp(argv[i], args[j].lnopt) == 0) {
				opts |= args[j].mask;
				argv[i] = NULL;
				break;
			}
		}
	}

	return opts;
}

static int show_help()
{
	printf("Usage: spn <command> [flags...] [files...] [-- scriptargs...] \n");
	printf("Where <command> is one of:\n\n");
	printf("\t-h, --help\tShow this help then exit\n");
	printf("\t-r, --run\tRun the specified script files\n");
	printf("\t-e, --execute\tExecute command-line arguments\n");
	printf("\t-i, --interact\tEnter interactive (REPL) mode\n");
	printf("\t-c, --compile\tCompile source files to bytecode\n");
	printf("\t-d, --disasm\tDisassemble bytecode files\n\n");
	printf("Flags consist of zero or more of the following options:\n\n");
	printf("\t-n, --print-nil\tExplicitly print nil values\n");
	printf("\t-t, --print-ret\tPrint return value of script\n\n");
	printf("The special option `--' indicates the end of options to the\n");
	printf("interpreter; subsequent arguments will be passed to the scripts.\n\n");
	printf("Please send bug reports through GitHub:\n\n");
	printf("\t<http://github.com/H2CO3/Sparkling>\n\n");

	return EXIT_SUCCESS;
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
	if (spn_vm_errmsg(ctx->vm) == ctx->errmsg) {
		size_t n;
		unsigned i;
		const char **bt = spn_vm_stacktrace(ctx->vm, &n);

		fprintf(stderr, "Call stack:\n\n");

		for (i = 0; i < n; i++) {
			fprintf(stderr, "\t[%-4u]\tin %s\n", i, bt[i]);
		}

		fprintf(stderr, "\n");
	}
}

static void register_args(SpnContext *ctx, int argc, char *argv[])
{
	int i, j;
	SpnExtValue vals[2];
	SpnArray *arr = spn_array_new();

	/* find the first argument to be passed to the script */
	for (i = 0; i < argc; i++) {
		if (argv[i] == NULL) {
			continue;
		}

		if (strcmp(argv[i], "--") == 0) {
			i++;
			break;
		}
	}

	for (j = i; j < argc; j++) {
		SpnValue key, val;
		SpnString *str = spn_string_new_nocopy(argv[j], 0);

		key.t = SPN_TYPE_NUMBER;
		key.f = 0;
		key.v.intv = j - i;

		val.t = SPN_TYPE_STRING;
		val.f = SPN_TFLG_OBJECT;
		val.v.ptrv = str;

		spn_array_set(arr, &key, &val);
		spn_object_release(str);
	}

	vals[0].name = "argc";
	vals[0].value.t = SPN_TYPE_NUMBER;
	vals[0].value.f = 0;
	vals[0].value.v.intv = argc - i;

	vals[1].name = "argv";
	vals[1].value.t = SPN_TYPE_ARRAY;
	vals[1].value.f = SPN_TFLG_OBJECT;
	vals[1].value.v.ptrv = arr;

	spn_vm_addglobals(ctx->vm, vals, sizeof(vals) / sizeof(vals[0]));
	spn_object_release(arr);
}

/* This checks if the file starts with a shebang, so that it can be run as a
 * stand-alone script if the shell supports this notation. This is necessary
 * because Sparkling doesn't recognize '#' as a line comment delimiter.
 */
static int run_script_file(SpnContext *ctx, const char *fname, SpnValue *ret)
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
			src = buf + strlen(buf);
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

	err = spn_ctx_execstring(ctx, src, ret);
	free(buf);
	return err;
}

static int run_files_or_args(int argc, char *argv[], enum cmd_args args)
{
	SpnContext *ctx = spn_ctx_new();
	int i, status = EXIT_SUCCESS;

	/* register command-line arguments */
	register_args(ctx, argc, argv);

	for (i = 1; i < argc; i++) {
		SpnValue ret;
		int err;

		if (argv[i] == NULL) {
			continue;
		}

		if (strcmp(argv[i], "--") == 0) {
			break;
		}

		if (args & CMD_RUN) {
			/* check if file is a binary object or source text */
			if (endswith(argv[i], ".spn")) {
				err = run_script_file(ctx, argv[i], &ret);
			} else if (endswith(argv[i], ".spo")) {
				err = spn_ctx_execobjfile(ctx, argv[i], &ret);
			} else {
				fprintf(stderr, "Sparkling: generic error: invalid file extension\n");
				status = EXIT_FAILURE;
				break;
			}
		} else {
			err = spn_ctx_execstring(ctx, argv[i], &ret);
		}

		if (err != 0) {
			fprintf(stderr, "%s\n", ctx->errmsg);
			print_stacktrace_if_needed(ctx);
			status = EXIT_FAILURE;
			break;
		} else {
			if (args & FLAG_PRINTRET) {
				if (ret.t != SPN_TYPE_NIL || args & FLAG_PRINTNIL) {
					spn_value_print(&ret);
					printf("\n");
				}
			}

			spn_value_release(&ret);
		}
	}

	spn_ctx_free(ctx);
	return status;
}

static int enter_repl(int argc, char *argv[], enum cmd_args args)
{
	SpnContext *ctx = spn_ctx_new();

	/* register command-line arguments */
	register_args(ctx, argc, argv);

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
				spn_value_release(&ret);
			}
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
	for (i = 1; i < argc; i++) {
		static char outname[FILENAME_MAX];
		char *dotp;
		FILE *outfile;
		spn_uword *bc;
		size_t nwords;

		if (argv[i] == NULL) {
			continue;
		}

		if (strcmp(argv[i], "--") == 0) {
			break;
		}

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

	for (i = 1; i < argc; i++) {
		spn_uword *bc;
		size_t fsz, bclen;

		if (argv[i] == NULL) {
			continue;
		}

		if (strcmp(argv[i], "--") == 0) {
			break;
		}

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

int main(int argc, char *argv[])
{
	int status;
	enum cmd_args args = process_args(argc, argv);

	int cmds_on = 0;
	int i;
	for (i = 0; i < N_CMDS; i++) {
		cmds_on += (args >> i) & 0x01;
	}

	if (cmds_on != 1) {
		fprintf(stderr, "Please specify exactly one of `hreicd'\n\n");
		exit(EXIT_FAILURE);
	}

	switch (args & CMDS_MASK) {
	case CMD_HELP:
		status = show_help();
		break;
	case CMD_RUN:
	case CMD_EXECUTE:
		status = run_files_or_args(argc, argv, args);
		break;
	case CMD_INTERACT:
		printf("Sparkling build %s, copyright (C) 2013, Árpád Goretity\n\n", REPL_VERSION);
		status = enter_repl(argc, argv, args);
		break;
	case CMD_COMPILE:
		printf("Sparkling build %s, copyright (C) 2013, Árpád Goretity\n\n", REPL_VERSION);
		/* XXX: this function modifies filenames in `argv' */
		status = compile_files(argc, argv);
		break;
	case CMD_DISASM:
		status = disassemble_files(argc, argv);
		break;
	default:
		fprintf(stderr, "Sparkling: generic error: internal inconsistency\n\n");
		status = EXIT_FAILURE;
	}

	return status;
}

