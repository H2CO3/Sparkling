# Sparkling Tutorial

## Semicolons

Just like C - but unlike JavaScript -, Sparkling requires every simple statement
(expression statements, `return`, `break`, `continue` and variable declarations)
to end with a semicolon. Compound statements (blocks) and certain structured
statements (`if`, `while` and `for`) need not end with a semi-colon. A semicolon
in itself, without a preceding statement, denotes an empty statement that does
nothing.

## Comments

There are two types of comments in Sparkling: one-line comments:

    // this is a one-line comment

and block comments:

    /* block comments can
       span several lines
     */

## Keywords:

This is a list of all reserved words (keywords and names corresponding to
other tokens):

- `and`
- `argc`
- `break`
- `const`
- `continue`
- `do`
- `else`
- `false`
- `for`
- `function`
- `global`
- `if`
- `nil`
- `not`
- `null`
- `or`
- `return`
- `sizeof`
- `true`
- `typeof`
- `var`
- `while`

## Variables

All variables need to be declared with `var` before they can be used. Variables
can be initialized when declared. For example:

    var i = 10;
    var j;

You can combine multiple declarations by separating them with commas:

    var i, j;

An identifier that is undeclared is assumed to refer to a global constant. It
is not possible to assign to globals (for safety reasons), but it is possible
to retrieve their value. (This is how function calls work.) Trying to acces an
undefined global (or a global with the value `nil`) results in a runtime error.

Variable names and other identifiers can begin with a lowercase or capital
English letter  (`a...z`, `A...Z`) or with an underscore (`_`), then any of
`a...z`, `A...Z`, `0...9`, or `_` follows.

## Constants

Constants can be declared and initialized at file scope only, using the `const`
or `global` keyword. It is obligatory to initialize a constant. It is also
obligatory for the initializer expression to be non-`nil` (initializing a
constant to `nil` will result in a runtime error).

    const E_SQUARED = exp(2);
    
    const my_number = 1 + 2, my_string = "foobar";

    global myLibrary = {
        "foo": function() {
                   print("foo");
               }
    };

## Numbers

You can write integers or floats in the same manner as in C:

 * `10` (decimal integer)
 * `0x3f` (hecadecimal integer)
 * `0755` (octal integer)
 * `2.0` (decimal floating-point)
 * `1.1e-2` (decimal floating-point in scientific notation)

The special values `M_NAN` and `M_INF` can be found in the standard maths
library, and they represent the floating-point NaN and positive infinity.

## Strings

String literals are enclosed in double quotes. You can access the individual
characters of a string in the same way you would access array members, i. e.
using the `[]` operator. Characters are represented by integers (by their
character code). Indexing starts from zero.

    > print("hello"[1]);
    101


To get the number of bytes in a string, use the `sizeof` operator:

    > print(sizeof "hello");
    5

Character literals are enclosed between apostrophes: `'a'`

This is a list of escape sequances (they are the same as in C)
that can be used in string and character literals:

    \\		->		\
    \/		->		/
    \'		->		'
    \"		->		"
    \a		->		bell
    \b		->		backspace
    \f		->		form feed
    \n		->		LF
    \r		->		CR
    \t		->		TAB
    \0		->		NUL, char code 0
    \xHH	->		the character with code HH, where HH denotes
    two hexadecimal digits

## Arrays

To define an array you can compose array literals:

    var empty = {};
    var primes = { 2, 3, 5 };

You can create an array contaning different types of values. Arrays indexing
starts with zero.

    > print({ 1, 2, 3, "hello" }[3]);
    hello

It is also possible to modify an element in the array by assigning to it:

    > var empty = {};
    > empty["foo"] = "bar";
    > print(empty["foo"]);
    bar

You can remove an element from an array by setting it to `nil`.

To get the number of elements in an array, use `sizeof` (the same way you would
do it with strings):

    > print(sizeof { "foo" });
    1

It is also possible to create arrays with non-integer indices (in fact, array
keys/indices can have any type). The order of elements in an array (e. g. when
enumerated using an iterator) is unspecified.

To create an associative array, use associative array literals. Keys and values
are separated by a colon:

    var words = { "cheese": "fromage", "apple": "pomme" };

It is even possible to intermix the two types:

    var mixed = { "foo", "bar": "baz", "quirk", "lol": 1337 };

Here, `"foo"` will have the key 0, `"baz"` corresponds to `"bar"`, `"quirk"` to
2, and 1337 to `"lol"`.

Array members corresponding to a string key can be accessed using the
convenience dot notation:

    an_array.some_member

is the same as

    an_array["some_member"]

## Expressions:

This is a short list of the most important operators:

 * `++` - pre- and post increment
 * `--` - pre- and post decrement
 * `+` - unary plus and addition
 * `-` - unary minus and subtraction
 * `*` - multiplication
 * `/` - division (truncates when applied to integers)
 * `..` - string concatenation
 * `=` - assignment
 * `+=`, `-=`, `*=`, `/=`, `..=`, `&=`, `|=`, `^=`, `<<=`, `>>=` - compound
assignments
 * `?:` - conditional operator
 * `sizeof`, `typeof` - size and type information
 * `.`, `->`: shorthand for array access (indexes with a string)
 * `&&`, `||`: logical AND and OR
 * `==`, `!=`, `<=`, `>=`, `<`, `>`: comparison operators
 * `&`, `|`, `^`: bitwise AND, OR and XOR
 * `<<`, `>>`: bitwise left and right shift
 * `#`: prefix operator, takes a non-negative integer expression. Yields the
`n`th variadic argument of the function it is used within (where `n` is the
value of its operand), starting from zero. If the value of the integer
expression is greater than or equal to the number of variadic arguments, throws
and exception. It also throws a runtime exception if it is supplied a negative,
non-integral or non-number argument.

## Loops

Unlike in C (and similarly to Python), you don't need to wrap loop conditions
inside parentheses. Loops work in the same manner as those in C.

`for` loop:

    for var i = 0; i < 10; ++i { // the scope of i is the loop only
        print(i);
    }

`while` loop:

    while i < x {
        print(i++);
    }

`do...while` loop:

    var i = 0;
    do {
        print(i++);
    } while i < 10;

## The if statement

You don't need to wrap the condition of the `if` stament in parentheses either:

    if 0 == 0 {
        print("equal");
    } else {
        printf("not equal");
    }

In order to implement multi-way branching, use `else if`:

    if i == 0 {
        print("zero");
    } else if i > 0 {
        print("greater then zero");
    } else {
        print("less then zero");
    }

In the condition of a loop or an if statement, you can only use boolean values.
Trying to use an expression of any other type will cause a runtime error.

## Functions

You can create named and anonymous functions with the `function` keyword:

    /* named functions are globally accessible */
    function square(x)
    {
        return x * x;
    }

    /* in contrast, unnamed functions have local scope */
    var fn = function(x) {
        return x + 1;
    };

Named functions are only allowed at file scope. Unnamed functions are allowed
everywhere, but due to an ambiguity in the grammar, at program scope, a
statement starting with the `function` keyword will always be assumed to
introduce a named function (function statement). If you want unnamed functions
(function expressions) at program scope, put them inside parentheses:

    (function (x) {
        return x * x;
    }(42));

If you don't explicitly return anything from a function, it will implicitly
return `nil`. The same applies to the entire translation unit itself.

To invoke a function, use the `()` operator:

    > print(square(10));
    100

To get the number of arguments with which a function has been called, use the
`argc` keyword:

    > function bar() { print(argc); }
    > bar();
    0
    > bar("foo");
    1
    > bar("baz", "quirk", nil);
    3
    >

To access the variadic (unnamed) arguments of a function, use the `#` operator,
as described in the "expressions" section.

## The standard library

A detailed description of the standard library functions and global constants
can be found in `doc/stdlib.md`.

## Getting started with the C API

Typically, you access the Sparkling engine using the Context API. It's quite
straightforward to use. First, create a new Sparkling context object:

   SpnContext *ctx = spn_ctx_new();

Then you may take different approaches. If you only want to run a program once,
then use `spn_ctx_execstring()` or `spn_ctx_execsrcfile()`. These are
convenience wrappers around other functions that parse, compile and run the
given string or source file in one go. They return 0 on success and nonzero
on error.

If a program has run successfully, then its return value will be in
`retVal`. You must relinquish ownership of this value if you no longer need it
by calling `spn_value_release()` on it (since SpnValue are reference counted).

    SpnValue retval;
    if (spn_ctx_execstring(ctx, "return \"Hello world!\";", &retval) == 0) {
        /* show return value */
        printf("Return value: ");
        spn_value_print(&retval);
        printf("\n");

        /* then dispose of it */
        spn_value_release(&retval);
    }

If an error occurs, then an error message is available by calling the
`spn_ctx_geterrmsg()` function (the returned pointer is only valid as long as
you do not run another program in the context structure, so copy the string if
you need it later!). You can also request a stack trace if the error was a
runtime error by calling the `spn_ctx_stacktrace()` function.

    else {
        fputs(spn_ctx_geterrmsg(ctx), stderr);

        if (spn_ctx_geterrtype(ctx) == SPN_ERROR_RUNTIME) {
            size_t i, n;

            const char **bt = spn_ctx_stacktrace(ctx, &n);

            for (i = 0; i < n; i++) {
                printf("frame %zu: %s\n", i, bt[i]);
            }

            free(bt);
        }
    }

The type of the last error is provided by `spn_ctx_geterrtype()`.

It is also possible that you want to run a program multiple times. Then, for
performance reasons, you may want to avoid parsing and compiling it repeatedly.
In that case, you can use `spn_ctx_loadstring()` and `spn_ctx_loadsrcfile()`
for parsing and compiling the source once. Once compiled, you can run the
resulting code with the help of the `spn_ctx_execbytecode()` function.

    spn_uword *bc = spn_ctx_loadstring(ctx, "print(42);");
    if (bc != NULL) {
        /* `bc' is a pointer to a bytecode array representing the program */
        int i;
        for (i = 0; i < 1000000; i++) { /* run the program a lot of times */
            SpnValue retval;
            if (spn_ctx_execbytecode(ctx, bc, &retval) == 0) {
                /* optionally use return value, then release it */
                spn_value_release(&retval);
            } else {
                /* handle runtime error */
                break;		
            }
        }
    } else {
        /* handle parser or syntax error */
    }

When you no longer need access to the Sparkling engine, you must have to
free its context object in order to reclaim all resources:

    spn_context_free(ctx);

# Advanced C API concepts

You can do even better using the Context API. You can extend a context with
libraries/modules/packages, call Sparkling functions from within a native
extension function, and you can even run a Sparkling function as if it was
the main program. For information on these features, please consult the
C API reference in `capi.md`.

