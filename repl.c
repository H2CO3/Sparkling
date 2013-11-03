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

#include "spn.h"
#include "ctx.h"
#include "repl.h"
#include "disasm.h"


#define N_CMDS		6
#define N_FLAGS		1
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

	FLAG_PRINTNIL	= 1 << 8
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
		{ "-n", "--print-nil",	FLAG_PRINTNIL	}
	};

	enum cmd_args opts = 0;

	int i;
	for (i = 1; i < argc; i++) {
		int j;

		/* stop processing at first occurrence of "--" */
		if (strcmp(argv[i], "--") == 0) {
			return opts;
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
	printf("\t-n, --print-nil\tExplicitly print nil values\n\n");
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

static int run_files_or_args(int argc, char *argv[], enum cmd_args args)
{
	SpnContext *ctx = spn_ctx_new();
	int status = EXIT_SUCCESS;

	/* find the first argument to be passed to the script */
	int i;
	for (i = 1; i < argc; i++) {
		if (argv[i] == NULL) {
			continue;
		}

		if (strcmp(argv[i], "--") == 0) {
			i++;
			break;
		}
	}

	/* register command-line arguments */
	spn_register_args(argc - i, &argv[i]);

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
				err = spn_ctx_execsrcfile(ctx, argv[i], &ret);
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
			if (ret.t != SPN_TYPE_NIL || args & FLAG_PRINTNIL) {
				spn_value_print(&ret);
			}
			printf("\n");
			spn_value_release(&ret);
		}
	}

	spn_ctx_free(ctx);
	return status;
}

static int enter_repl(enum cmd_args args)
{
	static char buf[LINE_MAX];
	SpnContext *ctx = spn_ctx_new();

	while (1) {
		SpnValue ret;
		int status;

		printf("> ");
		if (fgets(buf, sizeof buf, stdin) == NULL) {
			printf("\n");
			break;
		}

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

	printf("Sparkling build %s, copyright (C) 2013, Árpád Goretity\n\n", REPL_VERSION);

	switch (args & CMDS_MASK) {
	case CMD_HELP:
		status = show_help();
		break;
	case CMD_RUN:
	case CMD_EXECUTE:
		status = run_files_or_args(argc, argv, args);
		break;
	case CMD_INTERACT:
		status = enter_repl(args);
		break;
	case CMD_COMPILE:
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

