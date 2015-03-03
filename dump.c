/*
 * dump.c
 * Disassembly routines for the Sparkling REPL
 * Created by Árpád Goretity on 28/09/2014.
 *
 * Licensed under the 2-clause BSD License
 */

#include <stdio.h>
#include <stdarg.h>

#include "dump.h"
#include "private.h"


/* hopefully there'll be no more than 4096 levels of nested function bodies.
 * if you write code that has more of them, you should feel bad (and refactor).
 */
#define MAX_FUNC_NEST	0x1000

/* process executable section ("text") */
static int disasm_exec(spn_uword *bc, size_t textlen)
{
	spn_uword *text = bc + SPN_FUNCHDR_LEN;
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
			spn_die(
				"error disassembling bytecode: more than %d nested"
				"function definitions\n"
				"-- consider refactoring your code!\n",
				MAX_FUNC_NEST - 1 /* -1 for top-level program */
			);
			return -1;
		}

		/* if we reached a function's end, we print some newlines
		 * and "pop" an index off the function end address "stack"
		 * ('ip - 1' is used because we already incremented 'ip')
		 */
		if (ip - 1 == fnend[fnlevel - 1]) {
			fnlevel--;
			printf("\n");
		}

		/* if this is the entry point of a function, then print some
		 * newlines and "push" the entry point onto the "stack"
		 */
		if (opcode == SPN_INS_FUNCTION) {
			printf("\n");
			fnlevel++;
		}

		/* print address ('ip - 1' for the same reason as above) */
		printf("%#08lx", addr);

		/* print indentation */
		for (i = 0; i < fnlevel - 1; i++) {
			printf("\t");
		}

		/* the dump of the function header is not indented, only the
		 * body (a function *is* at the same syntactic level as its
		 * "sibling" instructions are. It is the code of the function
		 * body that is one level deeper.)
		 */
		if (opcode != SPN_INS_FUNCTION) {
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

			printf("jmp\t%+" SPN_SWORD_FMT "\t# target: %#08lx\n", offset, dstaddr);

			break;
		}
		case SPN_INS_JZE:
		case SPN_INS_JNZ: {
			spn_sword offset = *ip++;
			unsigned long dstaddr = ip + offset - bc;
			int reg = OPA(ins);

			printf("%s\tr%d, %+" SPN_SWORD_FMT "\t# target: %#08lx\n",
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
		case SPN_INS_TYPEOF: {
			/* and once again... this array trick is convenient! */
			static const char *const opnames[] = {
				"bitnot",
				"lognot",
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

				printf("%ld\t# %#lx\n", inum, unum);
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
				spn_die(
					"\n\nerror disassembling bytecode:"
					"incorrect constant kind %d in SPN_INS_LDCONST\n"
					"at address %08lx\n",
					type,
					addr
				);
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
		case SPN_INS_ARGV: {
			int opa = OPA(ins);
			printf("ld\tr%d, argv\t# r%d = argv\n", opa, opa);
			break;
		}
		case SPN_INS_NEWARR: {
			int opa = OPA(ins);
			printf("ld\tr%d, new array\n", opa);
			break;
		}
		case SPN_INS_NEWHASH: {
			int opa = OPA(ins);
			printf("ld\tr%d, new hashmap\n", opa);
			break;
		}
		case SPN_INS_IDX_GET: {
			int opa = OPA(ins), opb = OPB(ins), opc = OPC(ins);
			printf("idxget\tr%d, r%d, r%d\t# r%d = r%d[r%d]\n",
				opa, opb, opc, opa, opb, opc);
			break;
		}
		case SPN_INS_IDX_SET: {
			int opa = OPA(ins), opb = OPB(ins), opc = OPC(ins);
			printf("idxset\tr%d, r%d, r%d\t# r%d[r%d] = r%d\n",
				opa, opb, opc, opa, opb, opc);
			break;
		}
		case SPN_INS_ARR_PUSH: {
			int opa = OPA(ins), opb = OPB(ins);
			printf("push\tr%d, r%d\t# r%d.push(r%d)\n", opa, opb, opa, opb);
			break;
		}
		case SPN_INS_FUNCTION: {
			unsigned long bodylen, hdroff = ip - bc;
			int argc, nregs;

			/* ip += bodylen; --> NO! we want to disassemble the
			 * function body, so we don't skip it, obviously.
			 *
			 * we do calculate the end of the function, though,
			 * so it is possible to indicate it in the disassembly
			 */

			bodylen = ip[SPN_FUNCHDR_IDX_BODYLEN];
			argc = ip[SPN_FUNCHDR_IDX_ARGC];
			nregs = ip[SPN_FUNCHDR_IDX_NREGS];
			printf("function (%d args, %d registers, length: %lu, start: %#08lx)\n",
				argc, nregs, bodylen, hdroff);

			/* store the end address of the function body */
			fnend[fnlevel - 1] = ip + SPN_FUNCHDR_LEN + bodylen;

			/* sanity check */
			if (argc > nregs) {
				spn_die(
					"error disassembling bytecode: number of arguments "
					"(%d) is greater than number of registers (%d)!\n",
					argc,
					nregs
				);
				return -1;
			}

			/* we only skip the header -- then 'ip' will point to
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
				spn_die(
					"\n\nerror disassembling bytecode: symbol name length "
					"(%lu) does not match expected (%lu) at address %#08lx\n",
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
		case SPN_INS_CLOSURE: {
			int regidx = OPA(ins);
			int n_upvals = OPB(ins);
			int i;

			printf("closure\tr%d\t; upvalues: ", regidx);

			for (i = 0; i < n_upvals; i++) {
				spn_uword upval_desc = *ip++;
				enum spn_upval_type upval_type = OPCODE(upval_desc);
				int upval_index = OPA(upval_desc);

				char upval_typechr;

				if (i > 0) {
					printf(", ");
				}

				switch (upval_type) {
				case SPN_UPVAL_LOCAL:
					upval_typechr = 'L'; /* [L]ocal */
					break;
				case SPN_UPVAL_OUTER:
					upval_typechr = 'O'; /* [O]uter */
					break;
				default:
					spn_die("error disassembling bytecode: unknown upvalue type %d\n", upval_type);
					return -1;
				}

				printf("%d: #%d [%c]", i, upval_index, upval_typechr);
			}

			printf("\n");
			break;
		}
		case SPN_INS_LDUPVAL: {
			int regidx = OPA(ins);
			int upvalidx = OPB(ins);
			printf("ldupval\tr%d, upval[%d]\n", regidx, upvalidx);
			break;
		}
		case SPN_INS_METHOD: {
			int opa = OPA(ins), opb = OPB(ins), opc = OPC(ins);
			printf("method\tr%d, r%d, r%d\t# r%d = classes[r%d][r%d]\n",
				opa, opb, opc, opa, opb, opc);
			break;
		}
		case SPN_INS_PROPGET: {
			int opa = OPA(ins), opb = OPB(ins), opc = OPC(ins);
			printf("getprop\tr%d, r%d, r%d\t# r%d = getter(r%d, r%d)\n",
				opa, opb, opc, opa, opb, opc);
			break;
		}
		case SPN_INS_PROPSET: {
			int opa = OPA(ins), opb = OPB(ins), opc = OPC(ins);
			printf("setprop\t%d, r%d, r%d\t# setter(r%d, r%d, r%d)\n",
				opa, opb, opc, opa, opb, opc);
			break;
		}
		default:
			spn_die(
				"error disassembling bytecode: "
				"unrecognized opcode %d at address %#08lx\n",
				(int)(opcode),
				addr
			);
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
		printf("%#08lx\tsymbol %d:\t", addr, i);

		switch (kind) {
		case SPN_LOCSYM_STRCONST: {
			const char *cstr = (const char *)(ip);
			size_t len = strlen(cstr);
			size_t nwords = ROUNDUP(len + 1, sizeof(spn_uword));
			unsigned long explen = OPLONG(ins);

			if (len != explen) {
				spn_die(
					"error disassembling bytecode: "
					"string literal at address %#08lx: "
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
				spn_die(
					"error disassembling bytecode: "
					"symbol stub at address %#08lx: "
					"actual name length (%lu) does not match "
					"expected (%lu)\n",
					addr,
					(unsigned long)(len),
					explen
				);
				return -1;
			}

			printf("global '%s'\n", symname);

			ip += nwords;
			break;
		}
		case SPN_LOCSYM_FUNCDEF: {
			size_t offset = *ip++;
			size_t namelen = *ip++;
			const char *name = (const char *)(ip);
			size_t reallen = strlen(name);
			size_t nwords = ROUNDUP(namelen + 1, sizeof(spn_uword));

			if (namelen != reallen) {
				spn_die(
					"error disassembling bytecode: "
					"definition of function '%s' at %#08lx: "
					"actual name length (%lu) does not match "
					"expected (%lu)\n",
					name,
					addr,
					(unsigned long)(reallen),
					(unsigned long)(namelen)
				);
				return -1;
			}

			printf("function %s <start: %#08lx>\n", name, offset);

			ip += nwords;
			break;
		}
		default:
			spn_die(
				"error disassembling bytecode: incorrect local "
				"symbol type %d at address %#08lx\n",
				kind,
				addr
			);
			return -1;
			break;
		}
	}

	if (ip > bc + offset + datalen) {
		spn_die("error disassembling bytecode: bytecode is longer than length in header\n");
		return -1;
	} else if (ip < bc + offset + datalen) {
		spn_die("error disassembling bytecode: bytecode is shorter than length in header\n");
		return -1;
	}

	printf("\n");
	return 0;
}

int spn_dump_assembly(spn_uword *bc, size_t len)
{
	spn_uword symtaboff, symtablen, nregs;

	symtaboff = bc[SPN_FUNCHDR_IDX_BODYLEN] + SPN_FUNCHDR_LEN;
	symtablen = bc[SPN_FUNCHDR_IDX_SYMCNT];
	nregs     = bc[SPN_FUNCHDR_IDX_NREGS];

	/* print program header */
	printf("# program header:\n");
	printf("# number of registers: %" SPN_UWORD_FMT "\n\n", nregs);

	/* disassemble executable section. The length of the executable data
	 * is the symbol table offset minus the length of the program header.
	 */
	if (disasm_exec(bc, symtaboff - SPN_FUNCHDR_LEN) != 0) {
		return -1;
	}

	/* print symtab header */
	printf("\n\n# local symbol table:\n");
	printf("# start address: %#08" SPN_UWORD_FMT_HEX "\n", symtaboff);
	printf("# number of symbols: %" SPN_UWORD_FMT "\n\n", symtablen);

	/* disassemble symbol table. Its length is the overall length of the
	 * bytecode minus the symtab offset.
	 */
	return disasm_symtab(bc, symtaboff, len - symtaboff, symtablen);
}
