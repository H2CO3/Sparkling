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

#define N_CMDS		4
#define N_FLAGS		1
#define N_ARGS		(N_CMDS + N_FLAGS)

#define CMDS_MASK	0x0f
#define FLAGS_MASK	0xf0

#ifndef LINE_MAX
#define LINE_MAX	0x1000
#endif

enum cmd_args {
	CMD_HELP	= 1 << 0,
	CMD_RUN		= 1 << 1,
	CMD_EXECUTE	= 1 << 2,
	CMD_INTERACT	= 1 << 3,
	FLAG_PRINTNIL	= 1 << 8
};

static enum cmd_args process_args(int argc, char *argv[])
{
	static const struct {
		const char *shopt; /* short flag */
		const char *lnopt; /* long flag */
		enum cmd_args flag;
	} args[N_ARGS] = {
		{ "-h",	"--help",	CMD_HELP	},
		{ "-r",	"--run",	CMD_RUN		},
		{ "-e",	"--execute",	CMD_EXECUTE	},
		{ "-i", "--interact",	CMD_INTERACT	},
		{ "-n", "--print-nil",	FLAG_PRINTNIL	}
	};

	enum cmd_args flags = 0;

	int i;
	for (i = 1; i < argc; i++) {
		int j;

		/* stop processing at first occurrence of "--" */
		if (strcmp(argv[i], "--") == 0) {
			return flags;
		}

		for (j = 0; j < N_ARGS; j++) {
			if (strcmp(argv[i], args[j].shopt) == 0
			 || strcmp(argv[i], args[j].lnopt) == 0) {
				flags |= args[j].flag;
				argv[i] = NULL;
				break;
			}
		}
	}

	return flags;
}

static int show_help()
{
	printf("Usage: spn <flag> [files...]\n");
	printf("Where <flag> is one of:\n\n");
	printf("\t-h, --help\tShow this help then exit\n");
	printf("\t-r, --run\tRun the specified script files\n");
	printf("\t-e, --execute\tExecute command-line arguments\n");
	printf("\t-i, --interact\tEnter interactive (REPL) mode\n");
	printf("\t-n, --print-nil\tExplicitly print nil values\n");
	printf("\t--\t\tIndicates end of options to the interpreter;\n");
	printf("\t\t\tsubsequent argments will be passed to the scripts\n\n");
	printf("\tPlease send bug reports through GitHub:\n");
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
		SpnValue *val;

		if (argv[i] == NULL) {
			continue;
		}

		if (strcmp(argv[i], "--") == 0) {
			break;
		}

		if (args & CMD_RUN) {
			/* check if file is a binary object or source text */
			if (endswith(argv[i], ".spn")) {
				val = spn_ctx_execsrcfile(ctx, argv[i]);
			} else if (endswith(argv[i], ".spo")) {
				val = spn_ctx_execobjfile(ctx, argv[i]);
			} else {
				fprintf(stderr, "Sparkling: generic error: unrecognized file extension\n");
				status = EXIT_FAILURE;
				break;
			}
		} else {
			val = spn_ctx_execstring(ctx, argv[i]);
		}

		if (val != NULL) {
			if (val->t != SPN_TYPE_NIL || args & FLAG_PRINTNIL) {
				spn_value_print(val);
			}
			printf("\n");
		} else {
			/* the bytecode list changes if and only if a new
			 * file or statement is run (but not if a parser error
			 * or a compiler error occurred). This also protects us
			 * from dereferencing `bclist' if it is (still) NULL.
			 */
			status = EXIT_FAILURE;
			break;
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
		SpnValue *val;

		printf("> ");
		if (fgets(buf, sizeof buf, stdin) == NULL) {
			printf("\n");
			break;
		}

		val = spn_ctx_execstring(ctx, buf);
		if (val != NULL) {
			if (val->t != SPN_TYPE_NIL || args & FLAG_PRINTNIL) {
				spn_value_print(val);
				printf("\n");
			}
		}
		/* else the call stack trace is already printed */
	}

	spn_ctx_free(ctx);
	return EXIT_SUCCESS;
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
		fprintf(stderr, "Please specify exactly one of `hrei'\n\n");
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
	default:
		fprintf(stderr, "Sparkling: generic error: internal inconsistency\n\n");
		status = EXIT_FAILURE;
	}

	return status;
}

