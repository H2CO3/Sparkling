# Sparkling Tutorial

## Semicolons

Just like C - but unlike JavaScript -, Sparkling requires every simple statement
(expression statements, `return`, `break`, `continue` and variable declarations)
to end with a semicolon. Compound statements (blocks) and certain structured
statements (`if`, `while` and `for`) need not end with a semi-colon. A semicolon
in itself, without a preceding statement, denotes an empty statement that does
nothing.

## Keywords:

This is a list of all keywords:

- `and`
- `argc`
- `as`
- `break`
- `continue`
- `do`
- `else`
- `false`
- `for`
- `foreach`
- `function`
- `if`
- `in`
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

## Numbers

You can write integers or floats the same as in C:

 * `10` (decimal integer)
 * `0x3f` (hecadecimal integer)
 * `0755` (octal integer)
 * `2.0` (decimal floating-point)
 * `1.1e-2` (decimal floating-point in scientific notation)

## Strings

String literals are enclosed in double quotes. You can access single characters
of the string the same way you can access array members, using the `[]`
operator. Characters are converted to integers (to their character code).
String indices start from zero.

    > print("hello"[1]);
    101


To get the number of characters in a string, use the `sizeof` operator:

    > print(sizeof "hello");
    5

Character literals are enclosed between apostrophees: `'a'`

This is a list of escape sequances (same as in C) that can be used inside
string and character literals:

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

To define an array you can use the `array()` standard library function:

    var empty = array();
    var a = array(1, 2, 3, 4, 5);

You can create an array contaning different types of values. Arrays are indexed
from zero, just like strings.

    > print(array(1, 2, 3, "hello")[3]);
    hello

It is also possible to modify an element in the array by assigning to it:

    > var empty = array();
    > empty["foo"] = "bar";
    > print(empty["foo"]);
    bar

You can remove an element from an array by setting it to `nil`.

To get the number of elements in an array, use `sizeof` (the same way you would
do it with strings):

    > print(sizeof array("foo"));
    1

It is also possible to create arrays with non-integer indices (in fact, array
keys/indices can have any type). There is a convenience standard library
function for this purpose called `dict`:

    var words = dict(
        "hello",  "bonjour",
        "twenty", "vingt",
        "cheese", "fromage"
    );
    print(words["cheese"]); /* prints "fromage" */

As you can see, this function takes key-value pairs and adds them to the array.
The order of elements in an array (when enumerated using an iterator) is
unspecified. It is required that you pass an even number of arguments to this
function - calling it with an odd number of elements raises a runtime error.

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

## Loops

Unlike in C (and similarly to Python), you don't need to wrap loop conditions
inside parentheses. Loops work in the same manner as those in C.

For loop:

    for i = 0; i < 10; ++i {
        print(i);
    }

While loop:

    while i < x {
        print(i++);
    }

Do...while loop:

    var i = 0;
    do {
        print(i++);
    } while i < 10;

## The `if` statement

You don't need to wrap the condition of the `if` stament in parentheses either:


    if 0 == 0 {
        print("equal");
    } else {
        printf("not equal");
    }

There is no separate `elsif` keyword like in PHP or Python; you need to use
`else if`.

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

    /* named function, globally accessible */
    function square(x)
    {
        return x * x;
    }

    /* unnamed function */
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

## The standard library

A detailed description of the standard library functions and global constants
can be found in `doc/stdlib.md`.

