/*
 * vm.h
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 03/05/2013
 * Licensed under the 2-clause BSD License
 *
 * The Sparkling virtual machine
 */

#ifndef SPN_VM_H
#define SPN_VM_H

#include <stddef.h>
#include <string.h>

#include "spn.h"

/* an extension function written in C. It receives a pointer to the return
 * value, the number of call arguments, a pointer to an array of the arguments.
 * Arguments should not be modified. The only exception is that retaining
 * (and then releasing) them is allowed if you need to store them for later
 * use. The return value (in the script) of the function should be set through
 * the pointer to it. The actual C return value of the function should be zero
 * if it succeeded, or nonzero to signal an error.
 * `name` should be a non-NULL pointer, it will be used as the name of the
 * function visible to the script. It is not copied, so it should be valid
 * while you want the function to be available to the script.
 */
typedef struct SpnExtFunc {
	const char *name;
	int (*fn)(SpnValue *, int, SpnValue *, void *);
} SpnExtFunc;

/* the virtual machine */
typedef struct SpnVMachine SpnVMachine;

SPN_API SpnVMachine	 *spn_vm_new();
SPN_API void		  spn_vm_free(SpnVMachine *vm);

/* runs the bytecode pointed to by `bc`. Copies the return value of the
 * program to `*retval'. Returns 0 if successful, nonzero on error.
 */
SPN_API int		  spn_vm_exec(SpnVMachine *vm, spn_uword *bc, SpnValue *retval);

/* calls a Sparkling function from C-land */
SPN_API int spn_vm_callfunc(
	SpnVMachine *vm,
	SpnValue *fn,
	SpnValue *retval,
	int argc,
	SpnValue *argv
);

/* this function does NOT copy the names of the native functions,
 * so make sure that they are pointers during the entire runtime
 */
SPN_API void		  spn_vm_addlib(SpnVMachine *vm, const SpnExtFunc fns[], size_t n);

/* get and set user data */
SPN_API void		 *spn_vm_getcontext(SpnVMachine *vm);
SPN_API void		  spn_vm_setcontext(SpnVMachine *vm, void *ctx);

/* retrieves the last runtime error message */
SPN_API const char	 *spn_vm_errmsg(SpnVMachine *vm);

/* returns an array of strings containing a symbolicated stack trace.
 * Must be `free()`'d when you're done with it.
 */
SPN_API const char	**spn_vm_stacktrace(SpnVMachine *vm, size_t *size);

/* layout of a Sparkling bytecode file:
 * 
 * +------------------------------------+
 * | Sparkling magic bytes (spn_uword)	|
 * +------------------------------------+
 * | symbol table offset (spn_uword)	|
 * +------------------------------------+
 * | number of symbols (spn_uword)	|
 * +------------------------------------+
 * | size of 1st frame (spn_uword)	|
 * +------------------------------------+
 * | executable code			|
 * +------------------------------------+
 * | symbol table			|
 * +------------------------------------+
 */

/* the name used in the bytecode to indicate a lambda function */
#define SPN_LAMBDA_NAME		"@lambda@"

/* Bytecode magic number */
#define SPN_MAGIC		0x4e50537f	/* "\x7fSPN" */

/* description of the program header format */
#define SPN_HDRIDX_MAGIC	0
#define SPN_HDRIDX_SYMTABOFF	1
#define SPN_HDRIDX_SYMTABLEN	2
#define SPN_HDRIDX_FRMSIZE	3
#define SPN_PRGHDR_LEN		4

/* format of a Sparkling function's bytecode representation */
#define SPN_FUNCHDR_IDX_BODYLEN	0
#define SPN_FUNCHDR_IDX_ARGC	1
#define SPN_FUNCHDR_IDX_NREGS	2
#define SPN_FUNCHDR_LEN		3

/* macros for creating opcodes and instructions
 * instructions can have different formats:
 * format "void":	8 bits opcode, 24+ bits padding (no operands)
 * format "A":		8 bits opcode, 8 bits operand A, 16+ bits padding
 * format "AB":		8 bits opcode, 8 bits operand A, 8 bits operand B, 8+ bits padding
 * format "ABC":	8 bits opcode, 8 bits operand A, 8 bits operand B, 8 bits operand C, 0+ bits padding
 * format "mid":	8 bits opcode, 8 bits operand A, 16 bits operand B, 0+ bits padding
 * format "long":	8 bits opcode, 24 bits operand A, 0+ bits padding
 * 
 * note that this format is independent of the number of bits in a byte.
 * even if CHAR_BIT is not 8, the spn_uword (which is at least 32 bits wide)
 * is always split into 8-bit parts.
 */
#define SPN_MKINS_VOID(o)		((spn_uword)((o) & 0xff))
#define SPN_MKINS_A(o, a)		((spn_uword)(((o) & 0xff) | (((a) & 0xff) << 8)))
#define SPN_MKINS_AB(o, a, b)		((spn_uword)(((o) & 0xff) | (((a) & 0xff) << 8) | (((b) & 0xff) << 16)))
#define SPN_MKINS_ABC(o, a, b, c)	((spn_uword)(((o) & 0xff) | (((a) & 0xff) << 8) | (((b) & 0xff) << 16) | (((c) & 0xff) << 24)))
#define SPN_MKINS_MID(o, a, b)		((spn_uword)(((o) & 0xff) | (((a) & 0xff) << 8) | (((b) & 0xffff) << 16)))
#define SPN_MKINS_LONG(o, a)		((spn_uword)(((o) & 0xff) | (((a) & 0xffffff) << 8)))

/* constant kinds (see SPN_INST_LDCONST and comment (IV) for more info) */
enum spn_const_kind {
	SPN_CONST_NIL,		/* nil, obviously	*/
	SPN_CONST_TRUE,		/* Boolean true		*/
	SPN_CONST_FALSE,	/* Boolean false	*/
	SPN_CONST_INT,		/* integer literal	*/
	SPN_CONST_FLOAT		/* floating literal	*/
};

/* kinds of local symbol table entries
 * 
 * the format of each symbol table entry is as follows:
 * 
 * SPN_LOCSYM_STRCONST: the long `a` operand of the instruction is the length
 * in bytes of the string literal. The bytes following the instruction are
 * the bytes of the string literal including a 0-terminator. (as usually, the
 * number of `spn_uword`s overlaying the bytes is
 * length / sizeof(spn_uword) + 1)
 * 
 * SPN_LOCSYM_FUNCSTUB: the layout is the same as that of STRCONST, it's just
 * that FUNCSTUB represents a named, unresolved function, not a string literal.
 * (the long `a` operand contains the length of the function name.)
 * 
 * SPN_LOCSYM_LAMBDA: the long `a` operand of the instruction, interpreted as
 * an `unsigned long`, represents the offset of the entry point of a lambda
 * function, measured from the beginning of the bytecode.
 */
enum spn_local_symbol {
	SPN_LOCSYM_STRCONST,
	SPN_LOCSYM_FUNCSTUB,
	SPN_LOCSYM_LAMBDA
};

/* 
 * Instruction set of the virtual machine
 */
enum spn_vm_ins {
	SPN_INS_CALL,		/* a = b(...) [total: c arguments] (I)	*/
	SPN_INS_RET,		/* return a			(II)	*/
	SPN_INS_JMP,		/* unconditional jump			*/
	SPN_INS_JZE,		/* conditional jump if a == false	*/
	SPN_INS_JNZ,		/* conditional jump if a == true	*/
	SPN_INS_EQ,		/* a = b == c			(III)	*/
	SPN_INS_NE,		/* a = b != c				*/
	SPN_INS_LT,		/* a = b < c				*/
	SPN_INS_LE,		/* a = b <= c				*/
	SPN_INS_GT,		/* a = b > c				*/
	SPN_INS_GE,		/* a = b >= c				*/
	SPN_INS_ADD,		/* a = b + c				*/
	SPN_INS_SUB,		/* a = b - c				*/
	SPN_INS_MUL,		/* a = b * c				*/
	SPN_INS_DIV,		/* a = b / c				*/
	SPN_INS_MOD,		/* a = b % c				*/
	SPN_INS_NEG,		/* a = -b				*/
	SPN_INS_INC,		/* ++a					*/
	SPN_INS_DEC,		/* --a					*/
	SPN_INS_AND,		/* a = b & c				*/
	SPN_INS_OR,		/* a = b | c				*/
	SPN_INS_XOR,		/* a = b ^ c				*/
	SPN_INS_SHL,		/* a = b << c				*/
	SPN_INS_SHR,		/* a = b >> c				*/
	SPN_INS_BITNOT,		/* a = ~b				*/
	SPN_INS_LOGNOT,		/* a = !b				*/
	SPN_INS_SIZEOF,		/* a = sizeof(b)			*/
	SPN_INS_TYPEOF,		/* a = typeof(b)			*/
	SPN_INS_CONCAT,		/* a = b .. c				*/
	SPN_INS_LDCONST,	/* a = <constant> 		(IV)	*/
	SPN_INS_LDSYM,		/* a = local symtab[b]		(V)	*/
	SPN_INS_MOV,		/* a = b				*/
	SPN_INS_NEWARR,		/* a = new array			*/
	SPN_INS_ARRGET,		/* a = b[c]				*/
	SPN_INS_ARRSET,		/* a[b] = c				*/
	SPN_INS_NTHARG,		/* a = argv[b] (accesses varargs only!)	*/
	SPN_INS_GLBSYM		/* add to global symtab		(VI)	*/
};

/* Remarks:
 * --------
 * 
 * (I): the CALL instruction must know and fill in the number of arguments and
 * the return address in the next (to-be-pushed) stack frame. Arguments:
 * a: return value; b: function to call; c: number of call-time arguments
 * (the following `c' octets are register indices which point to the
 * call arguments of the function)
 * 
 * (II): the caller stores the return value from the called frame
 * to its own operation stack, then pops (frees) the called frame.
 * 
 * (III): operators that have a result/destination register (that's pretty much
 * everything except halt, return and jumps) should release its value before
 * overwriting its contents in order to avoid memory leaks. Also, since the
 * destination register can potentially be the same as one of the sources,
 * instructions with a destination AND source registers need to compute the
 * result beforehand, store it in a temporary variable, and only after all
 * calculations are done, should the destination register be updated.
 * 
 * (IV): constants include `nil`, boolean literals, integer and floating-point
 * literals. integer and floating-point literals take up one or more additional
 * machine word, so they need special treatment (namely, the instruction
 * pointer is to be increased appropriately when loading such constants)
 *
 * (V): this loads a symbol in the local symbol table. Local symtab entries
 * can be string literals, unresolved references to functions and lambdas.
 * 
 * (VI): the data following the instruction is laid out as follows:
 * there's a 0-terminated C string which is the name of the function,
 * padded with zeroes to overlay an entire number of `spn_uword`s. Then the
 * length of the function body in `spn_uword`s (*without* the three
 * `spn_uword`s of the header), the number of declaration arguments (some like
 * to call them "formal parameters"), and finally the total number of registers
 * (without the 2 extra slots, but including the registers reserved for the
 * declaration arguments) follow. (The size of a stack frame allocated at
 * runtime may be greater than the number of registers as indicated by the
 * bytecode if there are extra call-time arguments.) Then the actual bytecode
 * of the function body follows.
 * 
 * The instruction, besides the GLBSYM opcode, contains the length of the
 * symbol name in bytes (without the terminating NUL) in argument A.
 * It is checkeded that this number equals to the return value of `strlen()`
 * called on the function name.
 * 
 * Here's some visualisation for `function foobar(x, y) { return x + y; }`.
 * The vertical bars are boundaries of `spn_uword`s.
 * 
 * +-------------------------------------------------------------------------+
 * | SPN_INS_GLBSYM | 'foob' | 'ar\0\0' | 2 | 2 | 3 | ADD 2, 0, 1 | RETURN 2 |
 * +-------------------------------------------------------------------------+
 *                                        ^   ^   ^   ^
 *                                        |   |   |   +--- actual entry point
 *                                        |   |   +--- total register count
 *                                        |   +--- number of decl. arguments
 *                                        +--- body length, without header
 */

#endif /* SPN_VM_H */

