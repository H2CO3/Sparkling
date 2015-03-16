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

#include "api.h"
#include "array.h"
#include "hashmap.h"
#include "func.h"

/* an extension function written in C. It receives a pointer to the return
 * value, the number of call arguments, a pointer to an array of the arguments.
 * Arguments should not be modified. The only exception is that retaining
 * (and then releasing) them is allowed if you need to store them for later
 * use. The return value (in the script) of the function should be set through
 * the pointer to it. The actual C return value of the function should be zero
 * if it succeeded, or nonzero to signal an error.
 * 'name' should be a non-NULL pointer, it will be used as the name of the
 * function visible to the script. It is not copied, so it should be valid
 * while you want the function to be available to the script.
 */
typedef struct SpnExtFunc {
	const char *name;
	int (*fn)(SpnValue *, int, SpnValue *, void *);
} SpnExtFunc;

/* A generic global object. This can hold any SpnValue, not just functions. */
typedef struct SpnExtValue {
	const char *name;
	SpnValue value;
} SpnExtValue;

/* the virtual machine */
typedef struct SpnVMachine SpnVMachine;

/* A structure representing a frame of the call stack.
 * An array of this structure is returned by 'spn_vm_stacktrace()'.
 *
 * The 'function' member is the function object that corresponds
 * to the stack frame.
 *
 * Meanwhile, 'return_address' is an offset into the bytecode of
 * the _caller_ of 'function'. This address is where control flow
 * would have been transferred, had the function returned.
 * Hence, with the help of the debug information, 'return_address'
 * can be used to infer the instruction/location from where 'function'
 * has been called (the last machine word of that SPN_INS_CALL
 * instruction is located at address 'return_address - 1').
 *
 * A negative 'return_address' indicates that the caller
 * was a native (C) function and not a Sparkling function/script.
 *
 * 'exc_address' is an approximation of the address where
 * the actual runtime error has occurred:
 *
 *  - for frame #0, this is the same as the return value of
 *    'spn_vm_exception_addr()'.
 *  - for frames #1 and higher, it is computed by subtracting one
 *    from the return adddress of the previous (calling) frame.
 *    Actually, this will be the address of the last parameter
 *    passed to the function (or the call instruction itself if
 *    there were no parameters), since the return address always
 *    immediately follows the call instruction. So the debug info
 *    will correctly map it to the source location of the call.
 *
 * (if the caller is a native function, then the return address
 * of the frame will be negative, and so will be the return
 * address minus one, so this computation doesn't mess up the
 * indication of whether the callee returns to C or Sparkling.)
 *
 * 'sp' is an opaque pointer that stores the stack pointer of
 * the frame. It is used by 'spn_vm_get_register()'.
 */
typedef struct SpnStackFrame {
	SpnFunction *function;
	ptrdiff_t exc_address;
	ptrdiff_t return_address;
	void *sp;
} SpnStackFrame;

SPN_API SpnVMachine *spn_vm_new(void);
SPN_API void         spn_vm_free(SpnVMachine *vm);

/* calls a Sparkling function from C-land. */
SPN_API int spn_vm_callfunc(
	SpnVMachine *vm,
	SpnFunction *fn,
	SpnValue *retval,
	int argc,
	SpnValue *argv
);

/* These functions copy both the names of the values and the library name,
 * so 'libname' and 'fns[i].name' can be safely destroyed after the call.
 */
SPN_API void  spn_vm_addlib_cfuncs(SpnVMachine *vm, const char *libname, const SpnExtFunc  fns[],  size_t n);
SPN_API void  spn_vm_addlib_values(SpnVMachine *vm, const char *libname, const SpnExtValue vals[], size_t n);

/* get and set context info (arbitrarily usable pointer) */
SPN_API void *spn_vm_getcontext(SpnVMachine *vm);
SPN_API void  spn_vm_setcontext(SpnVMachine *vm, void *ctx);

/* the getter retrieves the last runtime error message, whereas the setter
 * can be used by native extension functions before returning an error code
 * in order to generate a custom error message.
 */
SPN_API const char *spn_vm_geterrmsg(SpnVMachine *vm);
SPN_API void        spn_vm_seterrmsg(SpnVMachine *vm, const char *fmt, const void *args[]);

/* returns an array of stack frame descriptors forming a symbolicated stack trace.
 * Return value must be free()'d when you're done with it.
 */
SPN_API SpnStackFrame *spn_vm_stacktrace(SpnVMachine *vm, size_t *size);

/* returns the value of the register at index 'index' within
 * the stack frame represented by 'frame'.
 */
SPN_API SpnValue spn_vm_get_register(SpnStackFrame *frame, size_t index);

/* returns the immediate address where a runtime error occurred.
 * Returns a negative value if the top-most function is a native C function.
 */
SPN_API ptrdiff_t spn_vm_exception_addr(SpnVMachine *vm);

/* this returns a hashmap that contains the global constants and functions.
 * the keys are symbol names (SpnString instances).
 * The returned pointer is non-owning: the result of a call to this function
 * is only valid as long as the virtual machine 'vm' is itself alive as well.
 */
SPN_API SpnHashMap *spn_vm_getglobals(SpnVMachine *vm);

/* Returns the class descriptor table of the VM.
 * You can use the returned array to add, modify or remove the class
 * of a certain object. A class descriptor must be a hashmap (SpnHashMap value).
 *
 * The returned global class descriptor must be either indexed using one of
 * the SPN_TTAG_* enums (as integers), or by a hashmap or a user info object.
 * The philosophy behind this decision is that all objects of the same class
 * have the same type, conceptually. Primitive types (numbers, strings, etc.)
 * have their own, limited set of possible values. Custom objects, however,
 * are best realized using either hashmaps, or maybe even user info objects,
 * so not all hashmap or user info instances have to belong to the same "type".
 *
 * Class descriptors must only be indexed with identifiers (strings that meet
 * the requirements of an identifier, e. g. no special characters or spaces)
 * or using the special integer values in the 'spn_method_index' enum.
 * Each value in a class descriptor must be a function (SpnFunction value).
 *
 * Similarly to 'spn_vm_getglobals()', the returned pointer is non-owning
 * (you must not use it after the VM is freed nor should you release it).
 */

SPN_API SpnHashMap *spn_vm_getclasses(SpnVMachine *vm);


/* layout of a Sparkling bytecode file:
 *
 * +------------------------------------+
 * | Length of executable code          |
 * +------------------------------------+
 * | number of formal parameters (0)    |
 * +------------------------------------+
 * | size of 1st frame                  |
 * +------------------------------------+
 * | number of local symbols            |
 * +------------------------------------+
 * | executable code                    |
 * +------------------------------------+
 * | symbol table                       |
 * +------------------------------------+
 */

/* the name used in the bytecode to indicate a lambda function */
#define SPN_LAMBDA_NAME		"<lambda>"

/* format of a Sparkling function's bytecode representation */
#define SPN_FUNCHDR_IDX_BODYLEN 0
#define SPN_FUNCHDR_IDX_ARGC    1
#define SPN_FUNCHDR_IDX_NREGS   2
#define SPN_FUNCHDR_IDX_SYMCNT  3
#define SPN_FUNCHDR_LEN         4

/* macros for creating opcodes and instructions
 * instructions can have different formats:
 * format "void":   8 bits opcode, 24+ bits padding (no operands)
 * format "A":      8 bits opcode, 8 bits operand A, 16+ bits padding
 * format "AB":     8 bits opcode, 8 bits operand A, 8 bits operand B, 8+ bits padding
 * format "ABC":    8 bits opcode, 8 bits operand A, 8 bits operand B, 8 bits operand C, 0+ bits padding
 * format "mid":    8 bits opcode, 8 bits operand A, 16 bits operand B, 0+ bits padding
 * format "long":   8 bits opcode, 24 bits operand A, 0+ bits padding
 *
 * note that this format is independent of the number of bits in a byte.
 * even if CHAR_BIT is not 8, the spn_uword (which is at least 32 bits wide)
 * is always split into 8-bit parts.
 */
#define SPN_MKINS_VOID(o)         ((spn_uword)((o) & 0xff))
#define SPN_MKINS_A(o, a)         ((spn_uword)(((o) & 0xff) | (((spn_uword)(a) & 0xff) << 8)))
#define SPN_MKINS_AB(o, a, b)     ((spn_uword)(((o) & 0xff) | (((spn_uword)(a) & 0xff) << 8) | (((spn_uword)(b) & 0xff) << 16)))
#define SPN_MKINS_ABC(o, a, b, c) ((spn_uword)(((o) & 0xff) | (((spn_uword)(a) & 0xff) << 8) | (((spn_uword)(b) & 0xff) << 16) | (((spn_uword)(c) & 0xff) << 24)))
#define SPN_MKINS_MID(o, a, b)    ((spn_uword)(((o) & 0xff) | (((spn_uword)(a) & 0xff) << 8) | (((spn_uword)(b) & 0xffff) << 16)))
#define SPN_MKINS_LONG(o, a)      ((spn_uword)(((o) & 0xff) | (((spn_uword)(a) & 0xffffff) << 8)))

/* constant kinds (see SPN_INST_LDCONST and comment (IV) for more info) */
enum spn_const_kind {
	SPN_CONST_NIL,   /* nil, obviously   */
	SPN_CONST_TRUE,  /* Boolean true     */
	SPN_CONST_FALSE, /* Boolean false    */
	SPN_CONST_INT,   /* integer literal  */
	SPN_CONST_FLOAT  /* floating literal */
};

/* kinds of local symbol table entries
 *
 * the format of each symbol table entry is as follows:
 *
 * SPN_LOCSYM_STRCONST: the long 'a' operand of the instruction is the length
 * in bytes of the string literal. The bytes following the instruction are
 * the bytes of the string literal including a 0-terminator. (as usually, the
 * number of 'spn_uword's overlaying the bytes is
 * length / sizeof(spn_uword) + 1)
 *
 * SPN_LOCSYM_SYMSTUB: the layout is the same as that of STRCONST, it's just
 * that SYMSTUB represents a named, unresolved global, not a string literal.
 * (the long 'a' operand contains the length of the symbol name.)
 *
 * SPN_LOCSYM_FUNCDEF: the 'spn_uword's following the instruction are:
 * 1. the offset of the entry point of the function in the bytecode;
 * 2. the length of the name of the function.
 * The following bytes contain the actual name string in the usual format.
 */
enum spn_local_symbol {
	SPN_LOCSYM_STRCONST,
	SPN_LOCSYM_SYMSTUB,
	SPN_LOCSYM_FUNCDEF
};

/*
 * Type of an upvalue (a captured variable in a closure)
 */
enum spn_upval_type {
	SPN_UPVAL_LOCAL,
	SPN_UPVAL_OUTER
};

/*
 * Instruction set of the virtual machine
 */
enum spn_vm_ins {
	SPN_INS_CALL,     /* a = b(...) [total: c arguments] (I)  */
	SPN_INS_RET,      /* return a (II)                        */
	SPN_INS_JMP,      /* unconditional jump                   */
	SPN_INS_JZE,      /* conditional jump if a == false       */
	SPN_INS_JNZ,      /* conditional jump if a == true        */
	SPN_INS_EQ,       /* a = b == c (III)                     */
	SPN_INS_NE,       /* a = b != c                           */
	SPN_INS_LT,       /* a = b < c                            */
	SPN_INS_LE,       /* a = b <= c                           */
	SPN_INS_GT,       /* a = b > c                            */
	SPN_INS_GE,       /* a = b >= c                           */
	SPN_INS_ADD,      /* a = b + c                            */
	SPN_INS_SUB,      /* a = b - c                            */
	SPN_INS_MUL,      /* a = b * c                            */
	SPN_INS_DIV,      /* a = b / c                            */
	SPN_INS_MOD,      /* a = b % c                            */
	SPN_INS_NEG,      /* a = -b                               */
	SPN_INS_INC,      /* ++a                                  */
	SPN_INS_DEC,      /* --a                                  */
	SPN_INS_AND,      /* a = b & c                            */
	SPN_INS_OR,       /* a = b | c                            */
	SPN_INS_XOR,      /* a = b ^ c                            */
	SPN_INS_SHL,      /* a = b << c                           */
	SPN_INS_SHR,      /* a = b >> c                           */
	SPN_INS_BITNOT,   /* a = ~b                               */
	SPN_INS_LOGNOT,   /* a = !b                               */
	SPN_INS_TYPEOF,   /* a = typeof(b)                        */
	SPN_INS_CONCAT,   /* a = b .. c                           */
	SPN_INS_LDCONST,  /* a = <constant> (IV)                  */
	SPN_INS_LDSYM,    /* a = local symtab[b] (V)              */
	SPN_INS_MOV,      /* a = b                                */
	SPN_INS_ARGV,     /* a = argv (contains all arguments)    */
	SPN_INS_NEWARR,   /* a = new array                        */
	SPN_INS_NEWHASH,  /* a = new hashmap                      */
	SPN_INS_IDX_GET,  /* a = b[c]                             */
	SPN_INS_IDX_SET,  /* a[b] = c                             */
	SPN_INS_ARR_PUSH, /* a.push(b); used for array literals   */
	SPN_INS_FUNCTION, /* function definition (VI)             */
	SPN_INS_GLBVAL,   /* add a value to the global symtab     */
	SPN_INS_CLOSURE,  /* create closure from free func (VII)  */
	SPN_INS_LDUPVAL,  /* a = upvalues[b];                     */
	SPN_INS_METHOD,   /* a = classes[b][c] (VIII)             */
	SPN_INS_PROPGET,  /* a = classes[b].getter(b, c) (IX)     */
	SPN_INS_PROPSET   /* classes[a].setter(a, b, c) (X)       */
};

/* Remarks:
 * --------
 *
 * (I): the CALL instruction must know and fill in the number of arguments and
 * the return address in the next (to-be-pushed) stack frame. Arguments:
 * a: return value; b: function to call; c: number of call-time arguments
 * (the following 'c' octets are register indices which point to the
 * call arguments of the function)
 *
 * (II): the caller stores the return value from the called frame
 * to its own operation stack, then pops (frees) the called frame.
 *
 * (III): operators that have a result/destination register (that's pretty
 * much everything except, return and jumps) should release its value before
 * overwriting its contents in order to avoid memory leaks. Also, since the
 * destination register can potentially be the same as one of the sources,
 * instructions with a destination AND source registers need to compute the
 * result beforehand, store it in a temporary variable, and only after all
 * calculations are done, should the destination register be updated.
 *
 * (IV): constants include 'nil', boolean literals, integer and floating-point
 * literals. integer and floating-point literals take up one or more additional
 * machine word, so they need special treatment (namely, the instruction
 * pointer is to be increased appropriately when loading such constants)
 *
 * (V): this loads a symbol in the local symbol table. Local symtab entries
 * can be string literals, unresolved references to globals and references
 * to functions (either global or lambda) defined in the current file.
 *
 * (VI): the data following the instruction is the header of the function
 * followed by the actual bytecode. The instruction itself has no parameters.
 *
 * Here's some visualisation for 'function foobar(x, y) { return x + y; }'.
 * The vertical bars are boundaries of 'spn_uword's.
 *
 *                    | <-- header -> |
 * +-----------------------------------------------------------+
 * | SPN_INS_FUNCTION | 2 | 2 | 3 | 0 | ADD 2, 0, 1 | RETURN 2 |
 * +-----------------------------------------------------------+
 *                      ^   ^   ^   ^   ^
 *                      |   |   |   |   +--- actual entry point
 *                      |   |   |   +--- number of symbols in local symtab (*)
 *                      |   |   +--- total register count (variables & temp)
 *                      |   +--- number of declaration arguments (**)
 *                      +--- body length, without function header
 *
 * (*)  The number of local symbols is always 0 unless the function
 *      represents the top-level translation unit.
 * (**) The number of declaration arguments ("formal parameters") is
 *      always 0 in the top-level function.
 *
 * (VII): SPN_INS_CLOSURE takes two parameters, the index of the register
 * in which a free function resides (operand A), and the number of upvalues
 * (operand B). After the opcode follow as many 'spn_uword's as there are
 * upvalues (B). Each spn_uword describes an upvalue (a local variable in an
 * enclosing  function, captured by the closure) using the following format:
 *
 * +-------------------------------+------------------------------+
 * | Upvalue type (OPCODE, 8 bits) | Upvalue index (OPA), 8 bits) |
 * +-------------------------------+------------------------------+
 *
 * The upvalue type is one of the members of the 'spn_upval_type' enum:
 *
 * - SPN_UPVAL_LOCAL represents an upvalue which is a local variable of the
 * immediately enclosing function.
 * - SPN_UPVAL_OUTER, on the other hand, refers to a variable which itself is
 * in the closure of the enclosing function (i. e. it's not an own local
 * variable thereof, but rather a variable of a function two or more levels
 * up the lexical scope).
 *
 * The instruction reads the unbound function value from the specified
 * register, creates a closure function object from it, sets its upvalues,
 * then replaces the contents of the register (the original function object)
 * with the newly created closure.
 *
 * (VIII): SPN_INS_METHOD looks up a method by name 'c' in the class
 * descriptor of object 'b'. The object may be an array (most commonly),
 * a string, or even a (usually strong) user info value.
 *
 * (IX): SPN_INS_PROPGET calls the property getter method of object 'b',
 * passing in the index/name 'c'.
 *
 * (X): SPN_INS_PROPSET calls the property setter method of object 'a',
 * passing in the index/name 'b' and its new value 'c'.
 */

#endif /* SPN_VM_H */
