/* 
 * basic.c
 * a simple Sparkling example program,
 * demonstrating the basic usage of the C API
 *
 * created by Árpád Goretity on 06/09/2013
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <spn/spn.h>
#include <spn/parser.h>
#include <spn/compiler.h>
#include <spn/disasm.h>
#include <spn/vm.h>
#include <spn/rtlb.h>


static int myext_foo(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	printf("myext_foo() called with %d arguments\n\n", argc);

	/* the return type seen by the script is "number" */
	ret->t = SPN_TYPE_NUMBER;
	/* SPN_TFLG_FLOAT indicates a float, 0 denotes an integer */
	ret->f = 0;
	/* pass back 1337 for fun's sake */
	ret->v.intv = 1337;

	return 0; /* normal execution finishes by returning 0 */
}

int main(int argc, char *argv[])
{
	int i;

	SpnParser *p;
	SpnCompiler *c;
	SpnVMachine *vm;

	spn_uword **bytecodes;

	const SpnExtFunc myextlib[] = {
		{ "foo", myext_foo }
	};

	if (argc < 2) {
		fprintf(stderr, "Sparkling: please specify at least one source file\n");
		exit(-1);
	}

	p = spn_parser_new();
	c = spn_compiler_new();
	vm = spn_vm_new();
	bytecodes = malloc((argc - 1) * sizeof(*bytecodes));

	/* load standard library */
	spn_load_stdlib(vm);

	/* load our own extension function */
	spn_vm_addlib(vm, myextlib, 1);

	/* add command line arguments */
	spn_register_args(argc - 1, argv + 1);

	for (i = 1; i < argc; i++) {
		char *src;
		size_t len;
		SpnAST *ast;
		SpnValue *res;

		src = spn_read_text_file(argv[i]);
		if (src == NULL) {
			fprintf(stderr, "Sparkling: can't open source file `%s'\n", argv[i]);
			exit(-1);
		}

		/* parse the source text to an AST */
		ast = spn_parser_parse(p, src);
		if (ast == NULL) {
			exit(-1);
		}

		free(src);

		/* dump textual representation of AST */
		spn_ast_dump(ast);

		/* compile the AST to bytecode */
		bytecodes[i - 1] = spn_compiler_compile(c, ast, &len);
		if (bytecodes[i - 1] == NULL) {
			exit(-1);
		}

		spn_ast_free(ast);

		/* dump disassembly of bytecode */
		spn_disasm(bytecodes[i - 1], len);

		/* run the bytecode */
		res = spn_vm_exec(vm, bytecodes[i - 1]);
		if (res == NULL) {
			exit(-1);
		}

		/* print a human-readable description of the program's return value */
		printf("\nthe result of running `%s' is: ", argv[i]);
		spn_value_print(res);
		printf("\n");

		/* no need to release the returned value, the VM owns it */
	}

	/* we free the bytecodes only when we are done with all of them because
	 * we allow programs in different files to cross-reference each other,
	 * so a program in one file may call a function in another translation
	 * unit. Thus, we need to keep each bytecode image alive during the
	 * entire lifetime of the program.
	 */
	for (i = 1; i < argc; i++) {
		free(bytecodes[i - 1]);
	}

	free(bytecodes);

	spn_vm_free(vm);
	spn_compiler_free(c);
	spn_parser_free(p);

	return 0;
}

