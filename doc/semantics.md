Anatomy and semantics of a Sparkling program
============================================

§1. General concepts
--------------------
§1.1. Sparkling programs consist of statements. Statements are executed in the
order they are written in the source files.

§1.2. The execution of a program finishes either when the last statement has
been executed, a run-time error occurred, or the user requested termination in
some other manner.

§1.3. Statements operate on data. Individual, discrete pieces of data are
called values. All values have a type.

§1.4. A type is an abstract property of a value that describes the behavior of
its contents. A type is defined by the set of possible values and operations.
A type in Sparkling is one of nil, boolean, number (either integer or
floating-point), function, string, array or user info.

§1.4.1. The nil type denotes the lack of any other valid value. Its only
possible value is `nil`, and a lexical synonym for the value `nil` is `null`.

§1.4.2. The boolean type is the result of a logical operation (as in elementary
logic and Boolean algebra) or that of a comparison. It only has two possible
values: true and false.

§1.4.3. The number data type is used to represent scalar values. Numbers are
signed, i. e. they can represent positive and negative numbers as well as zero.
They can be either integers or floating-point numbers.

§1.4.4. The function type represents a piece of executable code, either named
or unnamed. Global functions and lambda functions have function type.

§1.4.5. The string type encloses an array of bytes that represent either text
or binary data. They also have a size, which is an integer number, the number
of bytes in the string.

§1.4.6. The array type is a compound and mutable type. It is an unordered
collection of key-value pairs. Keys (indices) and values can be of any type.
Keys and values can be set and retrieved at any time. Arrays have a size, which
is the number of key-value pairs in the collection.

§1.4.7. The user info type represents any custom value. They are to be accessed
and manipulated using dedicated functions only. User info objects are used
typically in conjunction with the native C extension API.

§1.5. Values are obtained by evaluating expressions. An expression is said to
"have type `T`" if its value also has type `T`.

§1.6. Identifiers

§1.6.1. An identifier is a sequence of lowercase and uppercase letters, digits
and the '_' (underscore) character, but it may not begin with a digit.

§1.6.2. Reserved keywords (listed in the grammar) may not be used as
identifiers, although they satisfy the above conditions.

§1.7. Scope.
Scope is a the set of places in the code from where an identifier is visible.
All named functions have global visibility (i. e. they are visible across all
source files).

§1.7.1. Function bodies and the top level program have separate scope:
a variable declared in a function is not visible at program (file) scope and
vice versa. Furthermore, a variable declared in the body of one function is
not visible in the body of another function.

§1.7.2. Variables declared in a scope are visible from within that scope
and all enclosed sub-scopes, except if the sub-scope is a function scope,
as per §1.7.1.

§2. Statements
--------------
§2.1. The function statement (`function-statement`).
§2.1.1. A function statement defines a named function with zero or more
arguments. A function is an individual piece of code representing an operation.
It can be called with or without arguments and may or may not return a value.
In a function call expression, call arguments are evaluated and bound to formal
parameters, and the code in the function operates on its arguments accordingly.
The type of a function is function.

§2.1.2. Formal parameters act as variables local to the function. As such, all
formal parameters must have a distinct name within a function statement.

§2.1.3. Named functions are visible at global scope. They're just like other
global constants. As such, it is illegal to define a function with the name of
an already existing global and vice versa. Doing so results in a runtime error.

(remark: as a consequence, translation units that define named functions or
other globals can only be run once on a certain virtual machine.)

§2.2. The if statement (`if-statement`).
The if statement implements run-time decision and branching. The condition of
the if statement has to be of type boolean. If it is true, the statement in
the block following the condition ("then-branch") is evaluated. Otherwise, if
the condition is false, and the statement has an "else" branch, then the
statement in the "else" branch is evaluated.

§2.3. The for statement (`for-statement`).
The for statement evaluates its initializer statement once, then it executes
its body as long as its condition evaluates to true. (Thus, the condition must
be an expression of type boolean). The "incrementing" expression is evaluated
each time the loop body finished execution. The initializer statement may be
either an expression statement or a variable declaration. If it is a variable
declaration, then the scope of the declared identifiers is limited to the loop:
they are only visible in the condition, in the increment expression and inside
the block of the loop body.

§2.4. The while statement (`while-statement`).
The while statement is another loop statement that executes its body repeatedly
as long as its condition is true. The condition must be an expression of type
boolean.

§2.5. The do-while statement (`do-while-statement`).
The do-while loop statement evaluates its body, then it evaluates its condition.
The condition must be a boolean expression. If the condition is true, it starts
over (transfers control flow back to the beginning of the loop body).

§2.6. The return statement (`return-statement`).
The return statement transfers control flow to the calling context, optionally
handing a value to it.

§2.6.1. If the return statement is inside a function, then the
calling context is either the caller function (if the called function was
called from within another function) or the top-level program (if the called
function was called from the top-level program scope), and the return value of
the called function will be the value of the expression specified in the return
statement.

§2.6.2. Otherwise (if the return statement is at program scope), the calling
context is the native runtime environment, and the execution of a return
statement causes the termination of the Sparkling program. The `spn_vm_exec()`
C function will copy over into C-land the value of the expression specified
in the return statement, and it will return zero.

§2.6.3. If there is no expression in the return statement, returning `nil` is
implicitly assumed.

§2.7. The block statement (`block-statement`).
The block statement is a compound statement (one that encloses multiple sub-
-statements). Executing a block statement means that all its sub-statements are
executed in order. Block statements open a new scope.

§2.8. The break statement.
The break statement causes the execution of the innermost loop to terminate
immediately. It is an error to place a break statement outside a loop.

§2.9. The continue statement.
The continue statement causes the execution of the innermost loop to continue
from the beginning of the loop body with the next iteration. Before jumping
to the beginning of the loop body, in a for loop, the increment expression is
evaluated. It is illegal to place a continue statement outside a loop.

§2.10. The empty statement (`empty-statement`).
The empty statement is a no-op, it does nothing.

§2.11. The variable declaration statement (`variable-declaration`).
§2.11.1. The variable declaration statement brings a variable in scope. The
variable can be accessed inside the current scope and enclosing scopes (as
described in §1.6), but only in statements and expressions following the
declaration. It is illegal to initialize a variable with an expression referring
to itself (due to an implementation detail, such a variable will have an
indeterminate value). If there is no initializer expression, the variable is
implicitly initialized with `nil`.

§2.11.2. It is illegal to declare a variable that has the name of a variable
which already exists (which is already visible) in a scope.

§2.11.3. Comma-separated variable declarations happen in their syntactic order
(i. e. the first declaration is compiled first, then the second, etc.)

§2.12. The global constant declaration statement (`const-statement`). The
constant declaration statement defines a named global value. The value that is
associated with the name cannot be changed after its initialization.

§2.13. The expression statement (`expression-statement`).
The expression statement is a statement of which the only purpose is evaluating
an expression. As a general advice, for clarity's sake, the top-level
expression of an expression statement should only be a function call, an
assignment or a pre- or postincrement or -decrement expression.

Expression statements are used primarily to perform side effects. Thus, if
an expression in an expression statement has no side effects, it is allowed
to be optimized away (but this feature is currently unimplemented).

§3. Expressions
---------------
An expression is a combination of values and operations which evaluates to a
single value. In general, operations are executed in the order that operator
associativity and precedence requires it. There are exceptions, though, so
associativity and precedence is NOT the same as order of evaluation.

§3.1. Assignments. Assignments are expressions which perform an operation based
on an assignment operator, the left-hand side and the right-hand side, then
store the result of the operation into the left-hand-side, then yield the
result. The left-hand side of an assignment operator must always be a variable
or a member of an array.

§3.1.1. Simple assignment. The simple assignment operator `=` assigns the value
of the right-hand side to the left-hand side and yields their value.

§3.1.2. Compound assignments. Compound assignment operators have the form
`<LHS> <OP>= <RHS>` and they are equivalent with `<LHS> = <LHS> <OP> <RHS>`,
except that the left-hand side is only evaluated once.

§3.2. The concatenation operator.
The concatenation operator takes two strings as its operands, and yields its
right-hand side with the left-hand-side appended to it. It is an error to
concatenate two non-string values.

§3.3. The conditional operator.
The conditional operator evaluates its first operand, which should be of type
boolean. If the result is true, it evaluates and yields its second operand, the
third operand is left unevaluated. Otherwise (if the result is false), it
evaluates and yields its third operand, and the second one stays unevaluated.

§3.4. Logical operators.
Logical operators have short-circuiting behavior: if the result of the
expression can be decided after evaluating the first operand, then the second
operand is not evaluated. I. e., both of the expressions
	false && <subexpr>
and
	true || <subexpr>
will refuse to evaluate `<subexpr>`.

Logical operators must be supplied with two boolean expressions. The logical
AND operator yields true if both of its operands evaluate to true. Otherwise,
it yields false. The logical OR operator yields true if at least one of its
arguments evaluate to true, else it yields false.

§3.5. Comparison operators

§3.5.1. Equality operators

Equality operators `==` and `!=` take two values of any type, and they return
true if the values are considered equal or unequal, respectively. Two values
are equal if and only if:

- their types are the same **and** either of the following conditions are met:
	- they are both nil, or
	- they are numbers and have the same numeric value, or
	- they are booleans and they have the same truth value, or
	- they are strings and contain the same number of bytes (characters)
	  in the same order;
	- they are functions and they refer to the same executable entity
	  (for native functions, this means that the underlying C function
	  pointers compare equal; for Sparkling functions, this means that
	  the bytecode pointers point to the same function entry point.), or
	- they are arrays and they both reference the same array object, or
	- they are both user info values but not objects and reference the
	  same C pointer, or
	- they are user info values and objects and the `spn_object_equal()`
	  function returns true (nonzero) when called on them.

§3.5.2. Ordered comparison operators. Ordered comparison operators
(`<`, `>`, `<=`, `>=`) work with values of the same type. Only values of
comparable types may be given as operands to these operators. Comparable types
are numbers and strings.

These operators yield true if the left-hand side is less than, greater than,
less than or equal to, or greater than or equal to the right-hand side,
respectively. For numbers, the usual numeric ordering is significant;
for strings, lexicographical ordering by ascending character codes defines
the order.

§3.6. Bitwise operators. The bitwise operators `&`, `^` and `|` perform
bit-by-bit AND, XOR and OR operations on their integer operands. Operators
`<<` and `>>` shift the left (first) operand by as many places to the left or
to the right, respectively, as the integer value of the right (second) operand
specifies it. It is an error to attempt to perform bitwise operations on
non-integers.

§3.7. Arithmetic operators. Arithmetic operators `+`, `-`, `*` and `/` perform
addition, subtraction, multiplication and division, respectively. If either of
the operands is a floating-point number, the result will be a floating-point
number as well. If both operands evaluate to an integer, the result will be an
integer too. The `%` modulo operator performs a modulo division on its operands
and yields the remainder of the division. It may only be used on integers.

§3.8. Prefix operators

§3.8.1. The prefix unary `+` operator. Yields its operand of type number.It is
an error to use this operator with a non-number operand.

§3.8.2. The prefix unary `-` operator. Yields -1 times of its operand of type
number. If the operand is an integer, the result is an integer, and if the
operand is a floating-point number, the result is a floating-point value too.
It is an error to use this operator with a non-number operand.

§3.8.3. The prefix `++` operator. It increments its numeric operand by one,
and yields the already incremented value. Its operand must be a variable or an
array member. The operand must be of type number.

§3.8.4. The prefix `--` operator. It decrements its numeric operand by one,
and yields the already decremented value. Its operand must be a variable or an
array member. The operand must be of type number.

§3.8.4. The `!` operator. It takes a boolean operand and yields its
negated value.

§3.8.5. The `~` operator. It takes an integer operand and yields the its
bitwise complement.

§3.8.6. The `#` operator. It yields the N-th variadic (unnamed) argument of the
enclosing function, where argument #0 is the first variadic argument.
If the operand is more than or equal to the number of variadic arguments,
an exception is raised. It is an error to pass a negative, floating-point or
non-number operand to this operator.

§3.8.7. The `sizeof` operator. The sizeof operator yields the conceptual size
(length) of its argument. For nil, this is 0, for strings, it is the number
of bytes in the string, for arrays, it is the number of key-value pairs in the
array. If called on any other value, `sizeof' yields 1.

§3.8.8. The `typeof` operator. Yields a string representation of the type of
its operand. Thus, one of the strings "nil", "bool", "number", "function",
"string", "array" or "userinfo" will be returned.

§3.9. Postfix operators

§3.9.1. The postfix `++` operator. It increments its numeric operand by one,
and yields the non-incremented (original) value. Its operand must be a variable
or an array member. The operand must be of type number.

§3.9.2. The postfix `--` operator. It decrements its numeric operand by one,
and yields the non-decremented (original) value. Its operand must be a variable
or an array member. The operand must be of type number.

§3.9.3. The `[]` operator. This operator requires a subscript expression in
addition to its (left-hand side) operand. The LHS must be an array or a string.
If the left operand is an array, then the result of the operation is a reference
to the value in the array that corresponds to the key specified by the subscript
expression. If the left-hand-side operand is a string, then the RHS must be an
integer in the range `[-N...N)`, where `N` is the length of the string.
Then, the result of the operation is an integer - the character code of the
character at the specified index in the string if the index is non-negative.
If the index is negative, then the operator counts backwards (returns the
`idx`th character from the right, where -1 is the index of the last character).
If the LHS is not an array or a string, the string subscript is not an integer
expression or it is out of bounds, this operator raises a runtime error.
**Since strings are not mutable, an error is also thrown when a subscript
expression with a string on its LHS is assigned to.**

§3.9.4. The `()` operator. The `()` operator may have zero or more additional
operands between the two parentheses (along with the left-hand side), which
will become the arguments of the function. The LHS must be a function. The `()`
operator calls the function, binding the call-time arguments to its formal
parameters, maintaining their order. The expressions passed as arguments are
evaluated from left to right. The operator yields the return value of the
function. Function arguments are passed by value. Arrays follow pointer
semantics: assigning a new array to a function argument won't change the value
visible to the calling context, but assigning to an **element** of an array does
change its value as seen by both the calling context and the called function.

§3.9.5. The memberof operator has two different, semantically equivalent forms:
`<array> . member` and `<array> -> member`. It works the same as the postfix []
operator, except that the operand on the right-hand side must be an identifier,
(name), and that name itself, as a string key, will be used to retrieve or set
the array member.

§3.10. Function expressions (`function-expression`).
Function expressions create unnamed functions on the fly. A lambda function
behaves the same way as a global function, except that it doesn't have a name
and it isn't at global scope (that is, a lambda function is not visible outside
its translation unit).

§3.11. Literals

§3.11.1. Array literals (`array-literal`)

Array literals provide a convenient way to create arrays out of known keys and
values. Array literals consist of key-value pairs. If a key is not present,
then it is implicitly assumed to be an integer, of which the value reflects the
position of the key-value pair in question (the first pair is at position 0).

§3.11.2. Non-array literals

Non-array (scalar) literals represent a single constant value. These include
`nil` (or its synonym, `null`); decimal, octal and hexadecimal integer numbers,
character constants (enclosed by apostrophes, interpreted as a big-endian
integer number composed using the bytes of its characters), floating-point
(decimal fractional) numbers, strings (enclosed between double quotation marks),
and the Boolean literals `true` and `false`.

§3.12. The `argc` keyword

`argc` is a special keyword that evaluates to an integer which is the number of
arguments that the function in which the `argc` keyword is used was called
with. At file scope, this value is always zero.

