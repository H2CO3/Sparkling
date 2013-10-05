The Sparkling C API
===================

The Sparkling API is bidirectional: the native host program and a Sparkling
script can control each other.

Loading, compiling and running Sparkling scripts
------------------------------------------------
Sparkling is a compiled language: the interpreter performs the usual
transformations on the code before running it. Namely, source code is broken
into tokens by the lexer, the token stream is analyzed by the parser to form
an abstract syntax tree (AST), then the AST is walked by the compiler, a step
where actual executable code (bytecode) is generated.

Then, the bytecode is fed into the virtual machine (VM) which executes the
instructions and responds with the return value of the program.

The Sparkling API provides data types and functions for all these tasks.

    typedef struct SpnParser SpnParser;

`SpnParser` represents the parser (no surprise here). Its API consists of
memory management (creation, destruction) functions, an actual parser function
and error handling.

    typedef struct SpnAST SpnAST;

An abstract syntax tree object. AST nodes have at most two children, and
they are connected in a righ-leaning manner (the first child is called the
left child, the second child is the right child).

    SpnParser *spn_parser_new();

Creates an initialized parser object.

    void spn_parser_free(SpnParser *);

Deallocates a parser object.

    SpnAST *spn_parser_parse(SpnParser *, const char *);

This function takes Sparkling source text and parses it into an AST. On error,
sets the error message and returns `NULL`. The returned AST shall be freed
using `spn_ast_free()` after use.

    typedef struct SpnCompiler SpnCompiler;

A compiler takes the syntax tree of a program and outputs a bytecode image.
This bytecode can be run on the Sparkling virtual machine.

    SpnCompiler *spn_compiler_new();
    void spn_compiler_free(SpnCompiler *);

Memory management functions.

    spn_uword *spn_compiler_compile(SpnCompiler *, SpnAST *, size_t *);

Compiles an AST into bytecode. Returns a pointer to the beginning of the
bytecode image on success, `NULL` on error. If the compilation was successful,
the returned pointer must be `free()`d after use. If the `size_t *` pointer
is not `NULL`, it is set to the number of `spn_uword` objects in the bytecode
-- this comes handy if you want to write the bytecode to a file.

The Sparkling bytecode format is platform-dependent, i. e. bytecode generated
on some platform will always run on the same platform, it may be serialized
to file safely and it can be retrieved and run in another host program/process,
but this is not portable *across* different platforms. (Specifically, the
size of the `long` data type, the representation of floating-point numbers
and the representation of negative signed integers and endianness may vary.)

    const char *spn_compiler_errmsg(SpnCompiler *);

Returns the last error message. Check this if the compilation of an AST failed.

    typedef struct SpnVMachine SpnVMachine;

A virtual machine is an object that manages the execution of bytecode images.

    SpnVMachine *spn_vm_new();
    void spn_vm_free(SpnVMachine *);

Memory management functions.

    SpnValue *spn_vm_exec(SpnVMachine *vm, spn_uword *bc);

Runs the bytecode pointed to by `bc`. Returns the result of the execution.

    void spn_vm_addlib(SpnVMachine *vm, const SpnExtFunc fns[], size_t n);

Registers `n` native (C language) extension functions to be made visible by all
scripts running on the specified virtual machine instance.

    void *spn_vm_getcontext(SpnVMachine *);

Returns the context info of the virtual machine. This is set and retrieved by
the user exclusively. It's important mainly when one want to communicate with
extension functions.

    void spn_vm_setcontext(SpnVMachine *vm, void *ctx);

Sets the context info of `vm` to the user-supplied `ctx`.

    const char *spn_vm_errmsg(SpnVMachine *);

If a runtime error occurred, returns the last error message.

Writing native extension functions
----------------------------------
Native extension functions must have the following signature:

    int my_extfunc(SpnValue *ret, int argc, SpnValue *argv, void *ctx);

Whenever a registered native function is called from within a Sparkling script,
the corresponding C function receives a pointer where the return value must be
written (this is initialized to `nil` by default, so if a function doesn't set
this argument, it will return `nil` into Sparkling-land), the number of
arguments it was called with in `argc`, and a pointer of the first element of
the argument vector, which is an array of `argc` value structures.

The user-controlled context info field of the virtual machine is also passed
to extension functions in the `ctx` parameter.

Native extension functions must return 0 on success or nonzero on error
(returning a non-zero value will generate a runtime error in the VM).
The `argv` array and its members are never to be modified; the only exception
to this rule is that the user can retain the members (along with copying them)
to store them for later use. In this case, they are to be released as well.
Memory management functions `spn_value_retain()` and `spn_value_release()` are
provided for this purpose.

One word about the return value. Values are represented using the `SpnValue`
struct, which is essentially a tagged union (with some additional flags).
Values can be of type `nil`, Boolean, number (integer or floating-point),
function, user data, string and array. Strings, arrays and user data values
marked as such are object types (i. e. they have their `SPN_TFLG_OBJECT` flag
set in the structure). It means that they are reference counted. As an
implementation detail, it is reuired that if a native extension funcion returns
a value of object type, i. e. a string, an array or an object-typed user data,
then it shall own a reference to it (because internally, it will be released
by the virtual machine when it's not needed). So, if, for example, one of
the arguments is returned from a function (not impossible), then it should be
retained.

In other words: the following is **wrong:**

    int memory_corruption(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
    {
        if (argc > 0)
            *ret = argv[0];
    
        return 0;
    }

This is how it should have been done:

    int well_done(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
    {
        if (argc > 0) {
            spn_value_retain(&argv[0]);
            *ret = argv[0];
        }
    
        return 0;
    }

When creating a value, one must do the following:

1. Create an instance of an SpnValue struct.

2. Set its type using the type enum and additional flags if needed.
`val.t` must be one of `SPN_TYPE_NIL`, `SPN_TYPE_BOOL`, `SPN_TYPE_NUMBER`,
`SPN_TYPE_FUNCTION`, `SPN_TYPE_STRING`, `SPN_TYPE_ARRAY` or `SPN_TYPE_USRDAT`.

   If its type is Boolean, its `v.boolv` member should be set to zero for
   false, nonzero for true.

   If the type is number, then the `SPN_TFLG_FLOAT` flag may be set in the
   `f` member if the number is a floating-point value. If this is the case,
   the `v.fltv` member must be set, else the `v.intv` member is to be used.

   If the value is a function, then the `SPN_TFLG_NATIVE` flag may be assigned
   to the `f` member. A native function must then be represented by a function
   pointer of type
   
       int (*)(SpnValue *, int, SpnValue *, void *)
       
   A non-native (script) function is represented by a pointer to its function
   header in a bytecode image. They must also have their `v.fncv.symtabidx`
   member set to the index of the local symbol table in the VM that represents
   their environment. However, **this is not something a native extension
   function normally does.**

   If, and only if, a value is a string, an array or an object-based user data
   structure, then the `f` member should be set to `SPN_TFLG_OBJECT`.

3. Sparkling API functions typically copy and retain input values, and return
   non-owning pointers when giving output to the caller. Thus, if you want to
   use a value longer than an immediate operation, you typically retain **and**
   copy the structure too. In the other direction, when you supply a value to a
   function, you can pass the address of an automatic local (block-scope)
   variable -- it will be safely copied, and the value inside will also be
   retained if it's an object.

