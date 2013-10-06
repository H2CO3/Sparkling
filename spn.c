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

#include "spn.h"
#include "parser.h"
#include "compiler.h"
#include "disasm.h"
#include "vm.h"
#include "rtlb.h"

#define N_FLAGS 4

enum cmd_args {
	CMD_HELP	= 1 << 0,
	CMD_RUN		= 1 << 1,
	CMD_EXECUTE	= 1 << 2,
	CMD_INTERACT	= 1 << 3
};

static enum cmd_args process_args(int argc, char *argv[])
{
	static const struct {
		const char *shopt; /* short flag */
		const char *lnopt; /* long flag */
		enum cmd_args flag;
	} args[N_FLAGS] = {
		{ "-h",	"--help",	CMD_HELP	},
		{ "-r",	"--run",	CMD_RUN		},
		{ "-e",	"--execute",	CMD_EXECUTE	},
		{ "-i", "--interact",	CMD_INTERACT	}
	};

	enum cmd_args flags = 0;

	int i;
	for (i = 1; i < argc; i++) {
		int j;

		/* stop processing at first occurrence of "--" */
		if (strcmp(argv[i], "--") == 0) {
			return flags;
		}

		for (j = 0; j < N_FLAGS; j++) {
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
	printf("Sparkling, a C-style scripting language\n\n");
	printf("Usage: spn <flag> [files...]\n");
	printf("Where <flag> is one of:\n\n");
	printf("\t-h, --help\tShow this help then exit\n");
	printf("\t-r, --run\tRun the specified script files\n");
	printf("\t-e, --execute\tExecute command-line arguments\n");
	printf("\t-i, --interact\tEnter interactive (REPL) mode\n");
	printf("\t--\t\tIndicates end of options to spn; subsequent argments\n");
	printf("\t\t\twill be passed to the script\n\n");
	printf("\tPlease send bug reports through GitHub:\n");
	printf("\t<http://github.com/H2CO3/Sparkling>\n\n");
	return 0;
}

/* link list holding all the bytecode files
 * this is necessary because we don't free bytecode arrays after
 * having run them -- let the user run all the lines in the same
 * session. But we definitely do want to free them at the end
 * in order them not to leak memory.
 */
struct bc_list {
	spn_uword *bc;
	struct bc_list *next;
};


/* the `is_file' Boolean switch specifies if we are executing files whose
 * names are in `argv' (nonzero), or we evaluate the arguments themselves
 * as strings (zero)
 */
static int run_files_or_args(int argc, char *argv[], int is_file)
{
	SpnParser *p = spn_parser_new();
	SpnCompiler *c = spn_compiler_new();
	SpnVMachine *vm = spn_vm_new();

	int status = 0;

	struct bc_list *head = NULL;

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

	/* register command-line arguments and standard library functions */
	spn_register_args(argc - i, &argv[i]);
	spn_load_stdlib(vm);

	for (i = 1; i < argc; i++) {
		SpnAST *ast;
		spn_uword *bc;
		SpnValue *val;
		char *buf;
		struct bc_list *next;

		if (argv[i] == NULL) {
			continue;
		}

		if (strcmp(argv[i], "--") == 0) {
			break;
		}

		if (is_file) {
			buf = spn_read_text_file(argv[i]);
			if (buf == NULL) {
				fprintf(stderr, "Sparkling: could not open file `%s'\n", argv[i]);
				status = -1;
				break;
			}
		} else {
			buf = argv[i];
		}

		ast = spn_parser_parse(p, buf);

		if (is_file) {
			free(buf);
		}

		if (ast == NULL) {
			status = -1;
			break;
		}

		bc = spn_compiler_compile(c, ast, NULL);
		spn_ast_free(ast);

		if (bc == NULL) {
			status = -1;
			break;
		}

		next = malloc(sizeof(*next));
		if (next == NULL) {
			fprintf(stderr, "Sparkling: could not allocate memory\n\n");
			status = -1;
			break;
		}

		next->bc = bc;
		next->next = head;
		head = next;

		val = spn_vm_exec(vm, bc);

		if (val == NULL) {
			printf("Assembly dump of errant bytecode:\n\n");
			status = -1;
		} else {
			spn_value_print(val);
			printf("\n");
		}
	}

	spn_parser_free(p);
	spn_compiler_free(c);
	spn_vm_free(vm);

	/* free bytecode link list */
	while (head != NULL) {
		struct bc_list *tmp = head->next;
		free(head->bc);
		free(head);
		head = tmp;
	}

	return status;
}

static int enter_repl()
{
	static char buf[0x1000];

	struct bc_list *head = NULL;
		

	SpnParser *p = spn_parser_new();
	SpnCompiler *c = spn_compiler_new();
	SpnVMachine *vm = spn_vm_new();

	spn_load_stdlib(vm);

	printf("Entering interactive mode.\n\n");

	while (1) {
		SpnAST *ast;
		spn_uword *bc;
		size_t len;
		SpnValue *val;
		struct bc_list *next;

		printf("> ");
		if (fgets(buf, sizeof buf, stdin) == NULL) {
			printf("\n");
			break;
		}

		ast = spn_parser_parse(p, buf);
		if (ast == NULL) {
			continue;
		}

		bc = spn_compiler_compile(c, ast, &len);

		spn_ast_free(ast);

		if (bc == NULL) {
			continue;
		}

		/* add the bytecode to the "free list" */
		next = malloc(sizeof(*next));
		if (next == NULL) {
			printf("Could not allocate memory\n");
			break;
		}
		
		next->bc = bc;
		next->next = head;
		head = next;

		val = spn_vm_exec(vm, bc);

		if (val != NULL) {
			spn_value_print(val);
		} else {
			printf("Assembly dump of errant bytecode:\n\n");
			spn_disasm(bc, len);
		}

		printf("\n");
	}
	
	spn_parser_free(p);
	spn_compiler_free(c);
	spn_vm_free(vm);
	
	/* free the bytecode link list */
	while (head != NULL) {
		struct bc_list *tmp = head->next;
		free(head->bc);
		free(head);
		head = tmp;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int status;
	enum cmd_args flags = process_args(argc, argv);

	int flags_on = 0;
	int i;
	for (i = 0; i < N_FLAGS; i++) {
		flags_on += (flags >> i) & 0x01;
	}

	if (flags_on != 1) {
		fprintf(stderr, "Please specify exactly one of `hrei'\n\n");
		exit(-1);
	}

	switch (flags) {
	case CMD_HELP:
		status = show_help();
		break;
	case CMD_RUN:
		status = run_files_or_args(argc, argv, 1);
		break;
	case CMD_EXECUTE:
		status = run_files_or_args(argc, argv, 0);
		break;
	case CMD_INTERACT:
		status = enter_repl();
		break;
	default:
		fprintf(stderr, "Sparkling: internal inconsistency\n\n");
		status = -1;
	}
	
	return status;
}

