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

    int spn_vm_exec(SpnVMachine *vm, spn_uword *bc, SpnValue *retval);

Runs the bytecode pointed to by `bc`. Places the result of the execution into
`retval`. Returns 0 on success, nonzero on error. `retval` is to be released
when you're done with it.

    void spn_vm_addlib_cfuncs(SpnVMachine *vm, const char *libname,
        const SpnExtFunc fns[], size_t n);

Registers `n` native (C language) extension functions to be made visible by all
scripts running on the specified virtual machine instance. if `libname` is NULL,
the functions will be available at global namespace. Else a new global array
is be created with the name `libname`, and the functions will be members of
this global array. This is how you can create "modules" or "namespaces".

    void spn_vm_addlib_values(SpnVMachine *vm, const char *libname,
        SpnExtValue fns[], size_t n);

This function is similar to `spn_vm_addlib_cfuncs()`, but it accepts any valid
`SpnValue`, not just C exntension functions.

    void *spn_vm_getcontext(SpnVMachine *);

Returns the context info of the virtual machine. This is set and retrieved by
the user exclusively. It's important mainly when one want to communicate with
extension functions.

    void spn_vm_setcontext(SpnVMachine *vm, void *ctx);

Sets the context info of `vm` to the user-supplied `ctx`.

    const char *spn_vm_geterrmsg(SpnVMachine *);

If a runtime error occurred, returns the last error message.

    void spn_vm_seterrmsg(SonVMachhine *, const char *, const void *[]);

This function should only be called from within a native extension function,
in case of erroneous termination (before returning a non-zero status code).
It sets a custom - formatted - error message that will be embedded inside
the string that `spn_vm_geterrmsg()` returns.

    int spn_vm_callfunc(
        SpnVMachine *vm,
        SpnValue *fn,
        SpnValue *retval,
        int argc,
        SpnValue *argv
    );

This function makes it possible to call any Sparkling (or native) function
from within a native extension function. Throws a runtime error if:

1. its `fn` argument does not contain a value of function type, or
2. if `fn` is a native function and it returns a non-zero status code.

Script functions are tied to virtual machine instances, so if `fn` is not a
native funciton, then it should be implemented in a translation unit that has
already been run on the virtual machine `vm`. (That is, calling a non-native
function is only valid on the same virtual machine that was used to obtain
the value `fn` itself.)

Returns the status code of `fn`.

**Important:** if `spn_vm_callfunc()` returns nonzero (indicating that the
called function failed in some way or another), then your native extension
function (the caller) is **obliged** to return an error code as well; there's
no possibility for graceful error recovery. This is required because of an
implmentation detail: each native function has its own "pseudo-frame" on the
call stack in order it to be shown in the stack trace if it returns an error
status code. If it terminates normally, then this pseudo-frame is, of course,
popped off of the call stack. If, however, the function reports an error, the
dummy frame isn't removed (so that the stack trace is complete).

Now, if a non-native caller function tries to access its frame, this will
lead to an off-by-one (or two, or however many functions do not follow this
convention) error, and the caller will operate on the frame of another
function, corrputing the stack.

Usually, this constraint should not be a problem, though -- only fatal errors
should lead a function to return an error status code anyway. Non-critical
errors (from which recovery is considered possible) should be reported by the
means of some other mechanism (e. g. by returning `nil' or a Boolean status
flag to the caller).

Using the convenience context API
---------------------------------
The Sparkling API also provides an even easier interface, called the context
API. A context object encapsulates a parser, a compiler, a virtual machine,
and each bytecode file that has been loaded into that context.

    SpnContext *spn_ctx_new();
    void spn_ctx_free(SpnContext *ctx);

These functions create and destroy a context object. `spn_ctx_new()` sets
itself as the context info of the contained virtual machine. If you need to use
the user info from within an extension function, use the `info` field of the
context structure.

    spn_uword *spn_ctx_loadstring(SpnContext *ctx, const char *str);
    spn_uword *spn_ctx_loadsrcfile(SpnContext *ctx, const char *fname);

These functions attempt to parse and compile a source string. If an error is
encountered during either phase, they set the `errmsg` field of the context
object to the last error message and they return `NULL`. Else they return the
compiled bytecode. The return value is a non-owning pointer -- it should not
be `free()`d explicitly, it is deleted when you free the context object.

The bytecode objects are accumulated in the `bclist` member of the context,
which is a link list. The head of the list contains the most recently created
bytecode array as well as its length.

    spn_uword *spn_ctx_loadobjfile(SpnContext *ctx, const char *fname);

Reads a compiled object file and returns a non-owning pointer to its contents.
Prepends the bytecode to the beginning of the `bclist` link list, as described
above. On error, it returns `NULL` and it sets the `errmsg` member of the
context.

    int spn_ctx_execstring(SpnContext *ctx, const char *str, SpnValue *ret);
    int spn_ctx_execsrcfile(SpnContext *ctx, const char *fname, SpnValue *ret);
    int spn_ctx_execobjfile(SpnContext *ctx, const char *fname, SpnValue *ret);

These wrapper functions call the corresponding `spn_ctx_load*` function, but
they also attempt to execute the resulting compiled bytecode. If a run-time
error is encountered, they return nonzero and set the `errmsg` member to the
error message reported by the virtual machine. Else they copy the result of
the successfully executed program to `ret` and return zero.

    int spn_ctx_execbytecode(SpnContext *ctx, spn_uword *bc, SpnValue *ret);

Unlike the previously enumerated functions, this function **does not add the
bytecode to the bytecode list of the context.** Thus, generally, it should only
be used on bytecode returned by one of the `spn_ctx_load*` or `spn_ctx_exec*`
functions. (If you use it on any other bytecode object, then make sure to
preserve it while necessary. But you really do not want to do that.)
Runs the specified program and copies its result into `ret`; returns zero on
success and nonzero on error, in which case, it sets `errmsg` in `ctx`.

    int spn_ctx_callfunc(
        SpnContext *ctx,
        SpnValue *func,
        SpnValue *ret,
        int argc,
        SpnValue argv[]
    );

    void spn_ctx_runtime_error(SpnContext *ctx, const char *fmt, const void *args[]);

    const char **spn_ctx_stacktrace(SpnContext *ctx, size_t *size);

    void spn_ctx_addlib_cfuncs(SpnContext *ctx, const char *libname,
        const SpnExtFunc fns[], size_t n);

    void spn_ctx_addlib_values(SpnContext *ctx, const char *libname,
        SpnExtValue vals[], size_t n);

These are equivalent with calling `spn_vm_callfunc()`, `spn_vm_seterrmsg()`,
`spn_vm_stacktrace()`, `spn_vm_addlib_cfuncs()` and `spn_vm_addlib_values()`,
respectively, on `ctx->vm`.

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
function, user info, string and array. Strings, arrays and user info values
marked as such are object types (i. e. they have their `SPN_TFLG_OBJECT` flag
set in the structure). It means that they are reference counted. As an
implementation detail, it is reuired that if a native extension funcion returns
a value of object type, i. e. a string, an array or an object-typed user info,
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
`SPN_TYPE_FUNCTION`, `SPN_TYPE_STRING`, `SPN_TYPE_ARRAY` or `SPN_TYPE_USERINFO`.

   If its type is Boolean, its `v.boolv` member should be set to zero for
   false, 1 for true. (`true' must **always** be 1!)

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

   If, and only if, a value is a string, an array or an object-based user info
   structure, then the `f` member should be set to `SPN_TFLG_OBJECT`.

3. Sparkling API functions typically copy and retain input values, and return
   non-owning pointers when giving output to the caller. Thus, if you want to
   use a value longer than an immediate operation, you typically retain **and**
   copy the structure too. In the other direction, when you supply a value to a
   function, you can pass the address of an automatic local (block-scope)
   variable -- it will be safely copied, and the value inside will also be
   retained if it's an object.

