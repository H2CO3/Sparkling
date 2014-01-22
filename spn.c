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

#include "spn.h"
#include "array.h"
#include "ctx.h"
#include "spn.h"
#include "private.h"


#define N_CMDS		5
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
	CMD_DUMPAST	= 1 << 4,

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
		{ "-a", "--dump-ast",	CMD_DUMPAST	},
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

	spn_ctx_addlib_values(ctx, NULL, &vals, 1);
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


/* Disassembling */

#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif /* __GNUC__ */
static void bail(const char *fmt, ...)
{
	va_list args;

	fprintf(stderr, "Sparkling: bytecode error: ");

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");
}

/* hopefully there'll be no more than 256 levels of nested function bodies.
 * if you write code that has more of them, you should feel bad (and refactor).
 */
#define MAX_FUNC_NEST	0x100

/* process executable section ("text") */
static int disasm_exec(spn_uword *bc, size_t textlen)
{
	spn_uword *text = bc + SPN_PRGHDR_LEN;
	spn_uword *ip = text;
	spn_uword *fnend[MAX_FUNC_NEST];
	int i, fnlevel = 0;

	printf("# executable section:\n\n");

	fnend[fnlevel++] = text + textlen;
	while (ip < text + textlen) {
		spn_uword ins = *ip++;
		enum spn_vm_ins opcode = OPCODE(ins);
		unsigned long addr = ip - 1 - bc;

		if (fnlevel >= MAX_FUNC_NEST) {
			bail(
				"more than %d nested function definitions\n"
				"-- consider refactoring your code!\n",
				MAX_FUNC_NEST - 1 /* -1 for top-level program */
			);
			return -1;
		}

		/* if we reached a function's end, we print some newlines
		 * and "pop" an index off the function end address "stack"
		 * (`ip - 1` is used because we already incremented `ip`)
		 */
		if (ip - 1 == fnend[fnlevel - 1]) {
			fnlevel--;
			printf("\n");
		}

		/* if this is the entry point of a function, then print some
		 * newlines and "push" the entry point onto the "stack"
		 */
		if (opcode == SPN_INS_GLBFUNC) {
			printf("\n");
			fnlevel++;
		}

		/* print address (`ip - 1` for the same reason as above) */
		printf("0x%08lx", addr);

		/* print indentation */
		for (i = 0; i < fnlevel - 1; i++) {
			printf("\t");
		}

		/* the dump of the function header is not indented, only the
		 * body (a function *is* at the same syntactic level as its
		 * "sibling" instructions are. It is the code of the function
		 * body that is one level deeper.)
		 */
		if (opcode != SPN_INS_GLBFUNC) {
			printf("\t");
		}

		switch (opcode) {
		case SPN_INS_CALL: {
			int retv = OPA(ins);
			int func = OPB(ins);
			int argc = OPC(ins);
			int i;

			printf("call\tr%d = r%d(", retv, func);

			for (i = 0; i < argc; i++) {
				if (i > 0) {
					printf(", ");
				}

				printf("r%d", nth_arg_idx(ip, i));
			}

			printf(")\n");

			/* skip call arguments */
			ip += ROUNDUP(argc, SPN_WORD_OCTETS);

			break;
		}
		case SPN_INS_RET: {
			int opa = OPA(ins);
			printf("ret\tr%d\n", opa);
			break;
		}
		case SPN_INS_JMP: {
			spn_sword offset = *ip++;
			unsigned long dstaddr = ip + offset - bc;

			printf("jmp\t%+" SPN_SWORD_FMT "\t# target: 0x%08lx\n", offset, dstaddr);

			break;
		}
		case SPN_INS_JZE:
		case SPN_INS_JNZ: {
			spn_sword offset = *ip++;
			unsigned long dstaddr = ip + offset - bc;
			int reg = OPA(ins);

			printf("%s\tr%d, %+" SPN_SWORD_FMT "\t# target: 0x%08lx\n",
				opcode == SPN_INS_JZE ? "jze" : "jnz",
				reg,
				offset,
				dstaddr
			);

			break;
		}
		case SPN_INS_EQ:
		case SPN_INS_NE:
		case SPN_INS_LT:
		case SPN_INS_LE:
		case SPN_INS_GT:
		case SPN_INS_GE:
		case SPN_INS_ADD:
		case SPN_INS_SUB:
		case SPN_INS_MUL:
		case SPN_INS_DIV:
		case SPN_INS_MOD: {
			/* XXX: here we should pay attention to the order of
			 * these opcodes - we rely on them being in the order
			 * in which they are enumerated above because this
			 * way we can use an array of opcode names
			 */
			static const char *const opnames[] = {
				"eq",
				"ne",
				"lt",
				"le",
				"gt",
				"ge",
				"add",
				"sub",
				"mul",
				"div",
				"mod"
			};

			int opidx = opcode - SPN_INS_EQ; /* XXX black magic */
			int opa = OPA(ins), opb = OPB(ins), opc = OPC(ins);
			printf("%s\tr%d, r%d, r%d\n", opnames[opidx], opa, opb, opc);

			break;
		}
		case SPN_INS_NEG: {
			int opa = OPA(ins), opb = OPB(ins);
			printf("neg\tr%d, r%d\n", opa, opb);
			break;
		}
		case SPN_INS_INC:
		case SPN_INS_DEC: {
			int opa = OPA(ins);
			printf("%s\tr%d\n", opcode == SPN_INS_INC ? "inc" : "dec", opa);
			break;
		}
		case SPN_INS_AND:
		case SPN_INS_OR:
		case SPN_INS_XOR:
		case SPN_INS_SHL:
		case SPN_INS_SHR: {
			/* again, beware the order of enum members */
			static const char *const opnames[] = {
				"and",
				"or",
				"xor",
				"shl",
				"shr"
			};

			/* XXX and the usual woodoo */
			int opidx = opcode - SPN_INS_AND;
			int opa = OPA(ins), opb = OPB(ins), opc = OPC(ins);
			printf("%s\tr%d, r%d, r%d\n", opnames[opidx], opa, opb, opc);

			break;
		}
		case SPN_INS_BITNOT:
		case SPN_INS_LOGNOT:
		case SPN_INS_SIZEOF:
		case SPN_INS_TYPEOF: {
			/* and once again... this array trick is convenient! */
			static const char *const opnames[] = {
				"bitnot",
				"lognot",
				"sizeof",
				"typeof"
			};

			int opidx = opcode - SPN_INS_BITNOT;
			int opa = OPA(ins), opb = OPB(ins);
			printf("%s\tr%d, r%d\n", opnames[opidx], opa, opb);

			break;
		}
		case SPN_INS_CONCAT: {
			int opa = OPA(ins), opb = OPB(ins), opc = OPC(ins);
			printf("concat\tr%d, r%d, r%d\n", opa, opb, opc);
			break;
		}
		case SPN_INS_LDCONST: {
			int dest = OPA(ins);
			int type = OPB(ins);

			printf("ld\tr%d, ", dest);

			switch (type) {
			case SPN_CONST_NIL:	printf("nil\n");	break;
			case SPN_CONST_TRUE:	printf("true\n");	break;
			case SPN_CONST_FALSE:	printf("false\n");	break;
			case SPN_CONST_INT: {
				long inum;
				unsigned long unum; /* formatted as hex */

				memcpy(&inum, ip, sizeof(inum));
				memcpy(&unum, ip, sizeof(unum));
				ip += ROUNDUP(sizeof(inum), sizeof(spn_uword));

				printf("%ld\t# 0x%lx\n", inum, unum);
				break;
			}
			case SPN_CONST_FLOAT: {
				double num;
				memcpy(&num, ip, sizeof(num));
				ip += ROUNDUP(sizeof(num), sizeof(spn_uword));

				printf("%.15f\n", num);
				break;
			}
			default:
				bail("\n\nincorrect constant kind %d in SPN_INS_LDCONST\n"
				     "at address %08lx\n", type, addr);
				return -1;
				break;
			}

			break;
		}
		case SPN_INS_LDSYM: {
			int regidx = OPA(ins), symidx = OPMID(ins);
			printf("ld\tr%d, symbol %d\n", regidx, symidx);
			break;
		}
		case SPN_INS_MOV: {
			int opa = OPA(ins), opb = OPB(ins);
			printf("mov\tr%d, r%d\n", opa, opb);
			break;
		}
		case SPN_INS_LDARGC: {
			int opa = OPA(ins);
			printf("ld\tr%d, argc\n", opa);
			break;
		}
		case SPN_INS_NEWARR: {
			int opa = OPA(ins);
			printf("ld\tr%d, new array\n", opa);
			break;
		}
		case SPN_INS_ARRGET: {
			int opa = OPA(ins), opb = OPB(ins), opc = OPC(ins);
			printf("arrget\tr%d, r%d, r%d\t# r%d = r%d[r%d]\n", opa, opb, opc, opa, opb, opc);
			break;
		}
		case SPN_INS_ARRSET: {
			int opa = OPA(ins), opb = OPB(ins), opc = OPC(ins);
			printf("arrset\tr%d, r%d, r%d\t# r%d[r%d] = r%d\n", opa, opb, opc, opa, opb, opc);
			break;
		}
		case SPN_INS_NTHARG: {
			int opa = OPA(ins), opb = OPB(ins);
			printf("getarg\tr%d, r%d\t# r%d = argv[r%d]\n", opa, opb, opa, opb);
			break;
		}
		case SPN_INS_GLBFUNC: {
			const char *symname = (const char *)(ip);
			unsigned long namelen = OPLONG(ins);
			size_t nwords = ROUNDUP(namelen + 1, sizeof(spn_uword));

			size_t reallen = strlen(symname);

			unsigned long bodylen, entry;
			int argc, nregs;

			if (namelen != reallen) {
				bail(
					"\n\nfunction name length (%lu) does not match "
					"expected (%lu) at address 0x%08lx\n",
					(unsigned long)(reallen),
					namelen,
					addr
				);
				return -1;
			}

			/* skip symbol name, obtain function entry point */
			ip += nwords;
			entry = ip - bc;

			/* ip += bodylen; --> NO! we want to disassemble the
			 * function body, so we don't skip it, obviously.
			 *
			 * we do calculate the end of the function, though,
			 * so it is possible to indicate it in the disassembly
			 */

			bodylen = ip[SPN_FUNCHDR_IDX_BODYLEN];
			argc = ip[SPN_FUNCHDR_IDX_ARGC];
			nregs = ip[SPN_FUNCHDR_IDX_NREGS];
			printf("function %s (%d args, %d regs, code length: %lu, entry: 0x%08lx)\n",
				symname, argc, nregs, bodylen, entry);

			/* store the end address of the function body */
			fnend[fnlevel - 1] = ip + SPN_FUNCHDR_LEN + bodylen;

			/* sanity check */
			if (argc > nregs) {
				bail(
					"error: number of arguments (%d) is greater "
					"than number of registers (%d)!\n",
					argc,
					nregs
				);
				return -1;
			}

			/* we only skip the header -- then `ip` will point to
			 * the actual instruction stream of the function body
			 */
			ip += SPN_FUNCHDR_LEN;

			break;
		}
		case SPN_INS_GLBVAL: {
			int regidx = OPA(ins);
			const char *symname = (const char *)(ip);
			size_t namelen = OPMID(ins);
			size_t reallen = strlen(symname);
			size_t nwords = ROUNDUP(namelen + 1, sizeof(spn_uword));

			if (namelen != reallen) {
				bail(
					"\n\nsymbol name length (%lu) does not match "
					"expected (%lu) at address 0x%08lx\n",
					(unsigned long)(reallen),
					(unsigned long)(namelen),
					addr
				);
				return -1;
			}

			printf("st\tr%d, global <%s>\n", regidx, symname);

			ip += nwords;
			break;
		}
		default:
			bail("unrecognized opcode %d at address %08lx\n", opcode, addr);
			return -1;
			break;
		}
	}

	return 0;
}
	
/* process local symbol table ("data") */
static int disasm_symtab(spn_uword *bc, size_t offset, size_t datalen, int nsyms)
{
	spn_uword *ip = bc + offset;

	int i;
	for (i = 0; i < nsyms; i++) {
		spn_uword ins = *ip++;
		int kind = OPCODE(ins);
		unsigned long addr = ip - 1 - bc;

		/* print address */
		printf("0x%08lx\tsymbol %d:\t", addr, i);

		switch (kind) {
		case SPN_LOCSYM_STRCONST: {
			const char *cstr = (const char *)(ip);
			size_t len = strlen(cstr);
			size_t nwords = ROUNDUP(len + 1, sizeof(spn_uword));
			unsigned long explen = OPLONG(ins);

			if (len != explen) {
				bail(
					"string literal at address %08lx: "
					"actual string length (%lu) does not match "
					"expected (%lu)\n",
					addr,
					(unsigned long)(len),
					explen
				);
				return -1;
			}

			printf("string, length = %lu \"%s\"\n", explen, cstr);

			ip += nwords;
			break;
		}
		case SPN_LOCSYM_SYMSTUB: {
			const char *symname = (const char *)(ip);
			size_t len = strlen(symname);
			size_t nwords = ROUNDUP(len + 1, sizeof(spn_uword));
			unsigned long explen = OPLONG(ins);

			if (len != explen) {
				bail(
					"symbol stub at address %08lx: "
					"actual name length (%lu) does not match "
					"expected (%lu)\n",
					addr,
					(unsigned long)(len),
					explen
				);
				return -1;
			}

			printf("global `%s'\n", symname);

			ip += nwords;
			break;	
		}
		case SPN_LOCSYM_LAMBDA: {
			unsigned long off = OPLONG(ins);
			printf("lambda function <entry point: 0x%08lx>\n", off);
			break;
		}
		default:
			bail("incorrect local symbol type %d at address 0x%08lx\n", kind, addr);
			return -1;
			break;
		}
	}

	if (ip > bc + offset + datalen) {
		bail("bytecode is longer than length in header\n");
		return -1;
	} else if (ip < bc + offset + datalen) {
		bail("bytecode is shorter than length in header\n");
		return -1;
	}

	printf("\n");
	return 0;
}

static int disasm_bytecode(spn_uword *bc, size_t len)
{
	unsigned long symtaboff, symtablen, nregs, magic;

	/* read program header */
	magic = bc[SPN_HDRIDX_MAGIC];
	if (magic != SPN_MAGIC) {
		bail("invalid magic number");
		return -1;
	}

	symtaboff = bc[SPN_HDRIDX_SYMTABOFF];
	symtablen = bc[SPN_HDRIDX_SYMTABLEN];
	nregs     = bc[SPN_HDRIDX_FRMSIZE];

	/* print program header */
	printf("# program header:\n");
	printf("# magic number: 0x%08lx\n", magic);
	printf("# number of registers: %lu\n\n", nregs);

	/* disassemble executable section. The length of the executable data
	 * is the symbol table offset minus the length of the program header.
	 */
	if (disasm_exec(bc, symtaboff - SPN_PRGHDR_LEN) != 0) {
		return -1;
	}

	/* print symtab header */
	printf("\n\n# local symbol table:\n");
	printf("# start address: 0x%08lx\n", symtaboff);
	printf("# number of symbols: %lu\n\n", symtablen);

	/* disassemble symbol table. Its length is the overall length of the
	 * bytecode minus the symtab offset.
	 */
	return disasm_symtab(bc, symtaboff, len - symtaboff, symtablen);
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
		if (disasm_bytecode(bc, bclen) != 0) {
			free(bc);
			status = EXIT_FAILURE;
			break;
		}

		printf("--------\n\n");

		free(bc);
	}

	return status;
}


/* Dumping abstract syntax trees as S-expressions (almost) */

static void dump_indent(int i)
{
	while (i-- > 0) {
		fputs("    ", stdout);
	}
}

static void dump_ast(SpnAST *ast, int indent)
{
	static const char *const nodnam[] = {
		"program",
		"block-statement",
		"function-statement",

		"while",
		"do-while",
		"for",
		"if",

		"break",
		"continue",
		"return",
		"empty-statement",
		"vardecl",
		"global-constant",

		"assign",
		"assign-add",
		"assign-subtract",
		"assign-multiply",
		"assign-divide",
		"assign-modulo",
		"assign-and",
		"assign-or",
		"assign-xor",
		"assign-left-shift",
		"assign-right-shift",
		"assign-concat",

		"concatenate",
		"conditional-ternary",

		"add",
		"subtract",
		"multiply",
		"divide",
		"modulo",

		"bitwise-and",
		"bitwise-or",
		"bitwise-xor",
		"left-shift",
		"right-shift",

		"logical-and",
		"logical-or",

		"equals",
		"not-equal",
		"less-than",
		"less-than-or-equal",
		"greater-than",
		"greater-than-or-equal",

		"unary-plus",
		"unary-minus",
		"preincrement",
		"predecrement",
		"sizeof",
		"typeof",
		"logical-not",
		"bitwise-not",
		"nth-arg",

		"postincrement",
		"postdecrement",
		"array-subscript",
		"memberof",
		"function-call",

		"identifier",
		"literal",
		"function-expr",
		"argc",
		"array-literal",
		"key-value-pair",

		"decl-argument",
		"call-argument",
		"branches",
		"for-header",
		"generic-compound"
	};

	dump_indent(indent);
	printf("(%s", nodnam[ast->node]);

	/* print name, if any */
	if (ast->name != NULL) {
		printf(" name = \"%s\"", ast->name->cstr);
	}

	/* print formatted value */
	if ((ast->value.t == SPN_TYPE_NIL && ast->node == SPN_NODE_LITERAL)
	  || ast->value.t != SPN_TYPE_NIL) {
		printf(" value = ");
		spn_value_print(&ast->value);
	}

	if (ast->left != NULL || ast->right != NULL) {
		fputc('\n', stdout);
	}

	/* dump subtrees, if any */
	if (ast->left != NULL) {
		dump_ast(ast->left, indent + 1);
	}

	if (ast->right != NULL) {
		dump_ast(ast->right, indent + 1);
	}

	if (ast->left != NULL || ast->right != NULL) {
		dump_indent(indent);
	}

	puts(")");
}

static int dump_ast_of_files(int argc, char *argv[])
{
	SpnParser *parser = spn_parser_new();
	int status = EXIT_SUCCESS;

	int i;
	for (i = 0; i < argc; i++) {
		SpnAST *ast;

		char *src = spn_read_text_file(argv[i]);
		if (src == NULL) {
			fprintf(stderr, "Sparkling: I/O error: cannot read file `%s'\n", argv[i]);
			status = EXIT_FAILURE;
			break;
		}

		ast = spn_parser_parse(parser, src);
		free(src);

		if (ast == NULL) {
			fputs(parser->errmsg, stderr);
			status = EXIT_FAILURE;
			break;
		}

		dump_ast(ast, 0);
		spn_ast_free(ast);
	}

	spn_parser_free(parser);
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
	case CMD_DUMPAST:
		print_version();
		status = dump_ast_of_files(argc - pos, &argv[pos]);
		break;
	default:
		fprintf(stderr, "Sparkling: generic error: internal inconsistency\n\n");
		status = EXIT_FAILURE;
	}

	return status;
}

