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

    # this is another type of one-line comment

and block comments:

    /* block comments can
       span several lines
     */

## Keywords:

This is a list of all reserved words (keywords and names corresponding to
other tokens):

- `and`
- `break`
- `continue`
- `do`
- `else`
- `extern`
- `false`
- `fn`
- `for`
- `if`
- `let`
- `nil`
- `not`
- `null`
- `or`
- `return`
- `true`
- `typeof`
- `var`
- `while`

## Variables

All variables need to be declared with `var` or `let` before they
can be used. Variables can be initialized when declared. For example:

    var i = 10;
    var j;

You can combine multiple declarations by separating them with commas:

    var i, j;
    let foo = "bar", bar = "quirk";

An identifier that is undeclared is assumed to refer to a global constant. It
is not possible to assign to globals (for safety reasons), but it is possible
to retrieve their value. (This is how function calls work.) Trying to acces an
undefined global (or a global with the value `nil`) results in a runtime error.

Variable names and other identifiers can begin with a lowercase or capital
English letter  (`a...z`, `A...Z`) or with an underscore (`_`), then any of
`a...z`, `A...Z`, `0...9`, or `_` follows.

By convention, variables declared with `let` are usually considered constants.

## Global Constants

Constants can be declared and initialized at file scope only, using the `extern`
keyword. It is obligatory to initialize a constant. It is also obligatory for
the initializer expression to be non-`nil` (initializing a constant to `nil`
will result in a runtime error).

    extern E_SQUARED = exp(2);
    
    extern my_number = 1 + 2, my_string = "foobar";

    extern myLibrary = {
        "foo": fn {
                   print("foo");
               }
    };

Constants are globally available once declared.

## Numbers

You can write integers or floats in the same manner as in C:

 * `10` (decimal integer)
 * `0x3f` (hexadecimal integer)
 * `0o755` (octal integer)
 * `0b01001011` (binary integer)
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

To get the number of bytes in a string, use the `length` property:

    > var str = "hello"; print(str.length);
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

To create an array you can write array literals:

    var empty = [];
    var primes = [ 2, 3, 5 ];

You can create an array contaning different types of values. Array indexing
starts with zero.

    > print([ 1, 2, 3, "hello" ][3]);
    hello

It is also possible to modify an element in the array by assigning to it:

    > var a = ["baz"];
    > a[0] = "bar";
    > print(a[0]);
    bar

To get the number of elements in an array, use the `length` property –
similarly to strings.

    var arr = [ "foo", "bar", "baz" ];
    print(arr.length);

Prints `3`.

To add an element to the array, use `push()`:

	> var a = [];
	> a.push(1);
	> a.push(2);
	> a
	= [
	    1
	    2
	]

To remove the last element of a non-empty array, call `pop()`.

You can remove an element from the middle of an array by calling its `erase`
method with the appropriate index:

    > var a = [ "foo", "bar", "baz" ];
    > a.erase(1);
    > print(a);
    [
        "foo"
        "baz"
    ]

## Hashmaps

Hashmaps are associative containers (key-value pairs). Keys can be of any
type except `nil` and the `NaN` floating-point value. Values can be of any
type and value.

To create a hashmap, use hashmap literals. Keys and values are separated by a
colon:

    var words = { "cheese": "fromage", "apple": "pomme" };

It is even possible to intermix multiple types:

    var mixed = { 0: "foo", "bar": "baz", 2: "quirk", "lol": 1337 };

Here, `"foo"` will have the key 0, `"baz"` corresponds to `"bar"`, `"quirk"` to
2, and 1337 to `"lol"`.

By the way, this is one idiomatic way of implementing and using modules or
libraries in Sparkling: one assigns functions as members to a global hashmap
and accesses them using the bracket notation.

Use the `keys` and `values` methods of hashmaps to retrieve an array of
all keys and all values, respectively. The following code snippet:

    var a = { "foo": "bar", "baz": "quirk" };
    print(a.keys());
    print(a.values());

may output this:

	[
		"baz"
		"foo"
	]
	[
		"quirk"
		"bar"
	]

## Objects, methods, properties

Sparkling contains syntactic (and somewhat semantic) sugar for treating certain
values as objects. Some of the built-in types as well as user-defined values
can be used in an object-oriented manner. More specificallly:

 - Strings, arrays and functions have built-in properties and a "class
   descriptor" which contains functions. These functions can be used as
   methods and/or property accessors on the aforementioned values when
   called with the `obj.method()` or `obj.property` syntax.

 - Hashmaps also have this kind of class descriptor, but they are treated
   differently. When a method or a property accessor is called on a hashmap,
   it's first searched for in the hashmap itself (recursively, through the
   `"super"` chain). And only if it is not found there, will the lookup
   mechanism revert to searching in the default class descriptor of hashmaps.

 - User info values can be added to the global class descriptor using
   the C API, on a per-instance basis. There's no default class for them.

Classes and objects can inherit from one another. If a method or property
cannot be found on a particular object, then its ancestors are searched
recursively, by means of the `"super"` key:

	var superObj = {
		"foo": fn (self, n) {
			print("n = ", n);
		}
	};

	var other = {
		"super": superObj,
		"bar": fn (self, k) {
			print("k = ", k);
		}
	};

	> other.bar(42);
	k = 42
	> other.foo(1337);
	n = 1337

Property accessors follow a special structure. A snippet of code is worth a
thousand words:

    var anObject = {
        "awesumProperty": {
            "get": fn (self /*, name */) {
                return self["backingMember"];
            },
            "set": fn (self, newValue /*, name */) {
                self["backingMember"] = newValue;
            }
        }
    };

With this setup, the expression

    anObject.awesumProperty

will call the getter (and yield its return value), whereas the assignment

    anObject.awesumProperty = 1337;

will call the setter with the value `1337`. NB: the property setter syntax
ignores the return value of the setter and always yields the right-hand-side
of the assignment. (Given the usual semantics of the assignment statement,
you shouldn't expect anything else anyway.)

**It is strongly recommended that your getter methods do not modify state or
otherwise perform side effects.** People expect getters to be pure functions.

If a getter or setter method – in the structure described above – is not
found on an object (nor anywhere in its ancestor chain), **and** if the
object is a hashmap itself, then raw hashmap indexing (with the property
name as a string key) will take place instead. (Beware: this means that
you have to implement both accessor functions for read-only properties as
well, else the accessor structure will be overwritten upon an accidental
assignment.)

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
 * `typeof` - type information, yields a type string
 * `.`: property access (calls getter or setter if exists)
 * `&&`, `||`: logical AND and OR
 * `==`, `!=`, `<=`, `>=`, `<`, `>`: comparison operators
 * `&`, `|`, `^`: bitwise AND, OR and XOR
 * `<<`, `>>`: bitwise left and right shift

## Loops

Unlike in C (and similarly to Python), you don't need to wrap loop conditions
inside parentheses. Loops work in the same manner as those in C.

`for` loop:

    for var i = 0; i < 10; ++i { // the scope of i is the loop only
        print(i);
    }

`for` loop with parentheses around the loop header:

    for (var i = 0; i < 10; ++i) {
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

You don't need to wrap the condition of the `if` stament in parentheses either.
However, it is obligatory to use the curly braces around the body of the `if`
and `else` statements:

    if 0 == 0 {
        print("equal");
    } else {
        print("not equal");
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

You can create named and anonymous functions with the `fn` keyword.
If you initialize a local variable or a global constant with a function
expression, then the function name will be deduced from the name of the
variable or constant:

    let square = fn (x) {
        return x * x;
    };

    extern f = fn (x) {
        return x + 1;
    };

Parentheses around function parameters are optional; if they are omitted,
then parameter names are not separated by commas; otherwise, they are:

    let multiple_params_1 = fn (a, b) {
        return a + b;
    };

    let multiple_params_2 = fn x y {
        return x * y;
    };

    let no_params_1 = fn () {
        print("Look, I have no params");
    };

    let no_params_2 = fn {
        print("Me neither");
    };

If you don't explicitly return anything from a function, it will implicitly
return `nil`. The same applies to the entire translation unit itself (since
it is represented by a function too).

To invoke a function, use the `()` operator:

    > print(square(10));
    100

To get the number of arguments with which a function has been called, use
`$.length`:

    > extern bar = fn () { print($.length); }
    > bar();
    0
    > bar("foo");
    1
    > bar("baz", "quirk", nil);
    3
    >

To access the variadic (unnamed) arguments of a function, use the `$` array,
which contains all the call arguments of the function.
This array is also referred to as the "argument vector" or simply `argv`.

## The standard library

A detailed description of the standard library functions and global constants
can be found in `doc/stdlib.md`.

## Getting started with the C API

Typically, you access the Sparkling engine using the Context API. It's quite
straightforward to use. First, create a new Sparkling context object:

   SpnContext ctx;
   spn_ctx_init(&ctx);

Then you may take different approaches. If you only want to run a program once,
then use `spn_ctx_execstring()` or `spn_ctx_execsrcfile()`. These are
convenience wrappers around other functions that parse, compile and run the
given string or source file in one go. They return 0 on success and nonzero
on error.

If a program has run successfully, then its return value will be in
`retVal`. You must relinquish ownership of this value if you no longer need it
by calling `spn_value_release()` on it (since SpnValue are reference counted).

    SpnValue retval;
    if (spn_ctx_execstring(&ctx, "return \"Hello world!\";", &retval) == 0) {
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
        fputs(spn_ctx_geterrmsg(&ctx), stderr);

        if (spn_ctx_geterrtype(&ctx) == SPN_ERROR_RUNTIME) {
            size_t i, n;

            SpnStackFrame *bt = spn_ctx_stacktrace(&ctx, &n);

            for (i = 0; i < n; i++) {
                printf("frame %zu: %s\n", i, bt[i].function->name);
            }

            free(bt);
        }
    }

The type of the last error is provided by `spn_ctx_geterrtype()`.

It is also possible that you want to run a program multiple times. Then, for
performance reasons, you may want to avoid parsing and compiling it repeatedly.
In that case, you can use `spn_ctx_compile_string()` and `spn_ctx_compile_srcfile()`
for parsing and compiling the source once. Once compiled, you can run the
resulting code with the help of the `spn_ctx_callfunc()` function.

    SpnFunction *main_func = spn_ctx_compile_string(&ctx, "print(42);")
    if (main_func == NULL) {
        /* handle parser or syntax error */
    } else {
        /* 'main_func' contains a function describing the main program */
        int i;
        for (i = 0; i < 1000000; i++) { /* run the program a lot of times */
            SpnValue retval;
            if (spn_ctx_callfunc(&ctx, main_func, &retval, 0, NULL) == 0) {
                /* optionally use return value, then release it */
                spn_value_release(&retval);
            } else {
                /* handle runtime error */
                break;		
            }
        }
    }

If you don't want to run a full program, you can just compile a single
expression using `spn_ctx_compile_expr()` and call the returned function
with `spn_ctx_callfunc()`, as described above.

When you no longer need access to the Sparkling engine, you must free the
context object in order to reclaim all resources:

    spn_context_free(&ctx);

# Advanced C API concepts

You can do even better using the Context API. You can extend a context with
libraries/modules/packages, call Sparkling functions from within a native
extension function, and you can even run a Sparkling function as if it was
the main program. For information on these features, please consult the
C API reference in `capi.md`.

