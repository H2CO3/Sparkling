/*
 * disasm.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 11/09/2013
 * Licensed under the 2-clause BSD License
 *
 * Disassembler for Sparkling bytecode files
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "disasm.h"
#include "private.h"

static void bail(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");
	exit(-1);
}

static void disasm_exec(spn_uword *bc, size_t textlen);
static void disasm_symtab(spn_uword *bc, size_t offset, size_t datalen, int nsyms);

void spn_disasm(spn_uword *bc, size_t len)
{
	unsigned long symtaboff, symtablen, nregs, magic;

	/* read program header */
	magic = bc[SPN_HDRIDX_MAGIC];
	if (magic != SPN_MAGIC) {
		bail("invalid magic number");
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
	disasm_exec(bc, symtaboff - SPN_PRGHDR_LEN);

	/* print symtab header */
	printf("\n\n# local symbol table:\n");
	printf("# start address: 0x%08lx\n", symtaboff);
	printf("# number of symbols: %lu\n\n", symtablen);

	/* disassemble symbol table. Its length is the overall length of the
	 * bytecode minus the symtab offset.
	 */
	disasm_symtab(bc, symtaboff, len - symtaboff, symtablen);
}


/* hopefully there'll be no more than 256 levels of nested function bodies.
 * if you write code that has more of them, you should feel bad.
 */
#define MAX_FUNC_NEST	0x100

/* process executable section ("text") */
static void disasm_exec(spn_uword *bc, size_t textlen)
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
				MAX_FUNC_NEST - 1
			);
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
				     "at address %08lx\n", addr);
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
					"\n\nfunction name length (%lu) does not match"
					"expected (%lu) at address 0x%08lx\n",
					(unsigned long)(reallen),
					namelen,
					addr
				);
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
					"\n\nsymbol name length (%lu) does not match"
					"expected (%lu) at address 0x%08lx\n",
					(unsigned long)(reallen),
					(unsigned long)(namelen),
					addr
				);
			}

			printf("st\tr%d, global <%s>\n", regidx, symname);

			ip += nwords;
			break;
		}
		default:
			bail("unrecognized opcode %d at address %08lx\n", opcode, addr);
			break;
		}
	}
}
	
/* process local symbol table ("data") */
static void disasm_symtab(spn_uword *bc, size_t offset, size_t datalen, int nsyms)
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
			bail("incorrect local symbol type %d at address 0x%08lx\n", addr);
			break;
		}
	}

	if (ip > bc + offset + datalen) {
		bail("bytecode is longer than length in header\n");
	} else if (ip < bc + offset + datalen) {
		bail("bytecode is shorter than length in header\n");
	}

	printf("\n");
}

