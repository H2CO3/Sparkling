Documentation of the Sparkling standard runtime support library
===============================================================

The Sparkling standard library provides pre-defined functions to perform common,
simple tasks such as basic I/O, querying operating system properties, generating
random numbers, computing real->real mathematical functions, etc. Here follows
a non-formal description of the semantics of the standard library functions.

Note that it is required to call these functions with the correct (specified in
the "signature" of each function) number of arguments, else they raise an error.
An exception to this rule is a variadic function (denoted by an ellipsis `...`
in its argument list), which may be called with at least as many arguments as
required.

Note that, at the implementation level, the backing C functions that implement
the standard libraries expect that they are only called through the convenience
context API, i. e. that their "context info" pointer points to an SpnContext
structure.

A complete list of functions can be found in the `rtlb.h` header file.

1. I/O library
--------------

    userinfo stdin
    userinfo stdout
    userinfo stderr

Globals of type "userinfo", representing the standard input, output and error
stream, respectively.

    string getline(void)

Reads a line from the standard input and returns it as a string. Reads until
either a line separator character (`'\n'` or whatever it is on the host
operating system) is reached or end-of-file is encountered.

    nil print(...)

Prints a human-readable (debug) description of its arguments. Returns `nil`.

    nil printf(string format, ...)

Writes a formatted stream to the standard input. It has similar semantics to
that of `printf()` in the C standard library. Valid conversion specifiers are:

 - `%%` prints a literal percent symbol
 - `%[W.P]s` prints a string. If the precision (`P`) is present, it prints at
 most `P` characters. If the field width (`W`) is greater than the length of
 the string, then the string is padded with spaces until it fits.
 - `%[+| ][0][W]{d|u|o|x|X|b}` formats an integer as signed decimal, unsigned
 decimal, octal, lowercase and uppercase hexadecimal or binary, respectively.
 If `W` is present, prepends space before the digits so that at least `W`
 characters are outputted. If `+` is present, always adds an (explicit) leading
 sign (`+` or `-`). If `' '` (a space character) is specified instead of a `+`
 as the sign, then only the negative sign is printed, and for non-negative
 numbers, a space is prepended. Octal, hex and binary conversion specifiers
 always treat the integer as unsigned.
 - `%c` takes an integer argument and prints its corresponding character (based
 on the character code).
 - `%[+| ][W][.P]f` formats a floating-point number. If an integer number is
 given, it is converted to a floating-point number. The rules for using `W`,
 `+` and `' '` are the same as they were in the case of `%d`. If an explicit
 precision (`.P`) is specified, then prints exactly P decimal digits after the
 decimal point, rounding the result if necessary. If an explicit precision is
 not specified, then it's assumed to be `DBL_DIG` (taken from the C standard
 library of the host platform).
 - `%B` formats a Boolean value. Prints either true or false.
 - Width and precision may both be specified as `*`, in which case the actual
 width or precision is determined by looking at the next argument of the
 function, which must be an integer (one additional argument is used for each
 such variable-length format specifier).

If either an unrecognized conversion specifier is encountered, or an argument
of incorrect (mismatching) type is found, or the argument list is exhaused
(i. e. there are less convertible arguments passed to this function than the
format string contains conversion specifiers), or the variable-length width
and/or precision specifiers are not integers, this function throws a runtime
error.

<!-- this comment is needed because Markdown sucks. -->

    userinfo fopen(string name, string mode)

Opens the file `name` in mode `mode`. The meaning of the mode string is
identical to that of the second argument of `fopen()` in the C standard library.
Returns an user info value representing the open file if successful, or `nil`
on failure.

    nil fclose(userinfo file)

closes the file object associated with `file`.

    nil fprintf(userinfo file, string format, ...)
    string fgetline(userinfo file)

These work in the same manner as `printf()` and `getline()`, but they operate
on the specified file instead of `stdout`.

    string fread(userinfo file, int length)

Reads `length` bytes from the open file `file`. Returns the bytes as a string
on success, `nil` on failure.

    bool fwrite(userinfo file, string buf)

writes the characters in the string `buf` into the file `file`. Returns true
on success, false on error.

    nil fflush(userinfo file)

flushes the buffer of `file`, which must be a file opened for writing.

    int ftell(userinfo file)

returns the position indicator of `file`, i. e. the offset where the next
read or write operation occurs. Returns a negative value on failure.

    bool fseek(userinfo file, int off, string whence)

Sets the file position indicator to `off`. `whence` should be one of `"cur"`,
`"set"` or `"end"`. Its meaning is the same as it is when used with the C
stdlib function `fseek()`.

    bool feof(userinfo file)

returns true if the position indicator of `file` is at the end, false otherwise.

    bool remove(string fname)

Deletes the file with the name `fname`. Returns true on success, false on error.

    bool rename(string old, string new)

renames file `old` so that it will have the name `new`.
Returns true on success, false on failure.

    userinfo tmpfile(void)

This function returns a temporary file handle.

    string readfile(string filename)

Reads the contents of the file named `filename` and returns it as a string.

2. String manipulation
----------------------

The functions in the string library are implemented as methods on strings.
This means that their first argument is always the string being operated on.
Consequently, a function "declared" in this manner:

    method(string str, type_1 arg_1, type2 arg2, ...)

is to be called on a string as a method like this:

    atr.method(arg_1, arg_2, ...)

where `str` is a string object, so the first parameter will always be
bound to the string itself.

    int find(string haystack, string needle [, int offset])

Searches for the first occurrence of `needle` in `haystack`, beginning from
the `offset`th character (if given). If `offset` is negative, then it indexes
the string *backwards,* i. e. the function will start searching from position
`length - |offset|`, where `length` is the length of the string. Returns -1 if
the target string could not be found.

    string substr(string str, int offset, int length)

Creates a stubstring of length `length` starting from position `offset` (i. e.
it copies the region `[offset, offset + length)` from the original string).

    string substrto(string str, int length);
    string substrfrom(string str, int offset);

These are equivalent with `substr(str, 0, length)` and with
`substr(str, offset, length - offset)`, respectively.

    array split(string str, string sep)

searches `str` for occurrences of `sep` (the separator), and splits `str` into
substrings such that `sep` will be a boundary of each chunk. `sep` is not
included in the returned substrings. Using a different wording:
`join(split(str, sep), sep)` returns the original string.

    string repeat(string str, int n)

Concatenates `str` with itself `n` times and returns the result.

    string tolower(string str)
    string toupper(string str)

These return a copy of `str` with all alphabetical characters changed to
lower- or uppercase, respectively.

    string format(string format, ...)

Works the same way as `printf()`, but instead of printing to stdout, it returns
the whole formatted string.

A (read-only) property named `length` is also available on strings, which
yields the number of bytes (which is not necessarily the number of characters)
in the string.

3. Array handling
-----------------

Similarly to the functions of the string library, most array functions are
implemented as methods on array objects.

    nil sort(array arr [, function comparator])

`sort()` sorts the elements of `arr` in ascending order, using the comparator
if present, or using the built-in `<` operator if no comparator is specified.
The comparator function takes two arguments: two elements of the array to be
compared. It must return `true` if its first argument compares less than the
second one, and `false` otherwise.

    int find(array arr, any element)

Returns the index at which `element` is found in the array, or -1 if the
element can't be found in the array.

    int pfind(array arr, function predicate)

Returns the index of the first element for which `predicate` returns `true`.
If no such element can be found, returns `-1`.

    int bsearch(array arr, any element [, function comparator])

Returns the index of `element` or -1 if the element is not contained in the
array. If a `comparator` function is specified, then it will be used to
determine ordering: it is passed two distinct elements of the array, and it
must return true if its first argument is "less than" (ordered before) its
second argument and false otherwise. If no comparator function is given, then
the "less than" (`<`) operator will be used. The array must be sorted in
ascending order for this function to work correctly.

    bool any(array arr, function predicate)
    bool all(array arr, function predicate)

These functions investigate if any or all of the elements of `arr` match the
given `predicate`. The predicate must return a Boolean. If the first argument
of `any` is an empty array, then `false` is returned. If the first argument
of `all` is an empty array, then `true` is returned. `predicate` is called
with an item in `arr` as its first parameter and the corresponding key as the
second one.

    array slice(array arr, number start, number length)

Returns a subarray of `arr` by copying its elements in the range
`[start, start + length)`.

    string join(array arr, string sep)

All elements in the array must be strings.
Returns the concatenation of the elements of `arr`, interleaved with `sep`.

    nil foreach(array arr, function callback)

Iterates through the elements of the array `arr`, calling `callback` with each
value in the array (like `callback(arr[index], index)`).
The callback function must return `nil` or a Boolean. If it returns `false`,
then the enumeration is aborted and the `foreach()` function returns.

**Warning:** it is illegal to modify an array while it is being enumerated.

    any reduce(array arr, any identity, any callback(any first, any second))

Loops over `arr`, calling `callback()` with two arguments upon each iteration.
The first argument of the callback function is `identity` during the first
iteration, and the return value of the previous call to the function otherwise.
The second argument is the next value in the `arr` array. `reduce()` returns
the result of the last call to `callback()`. Here's a possible implementation:

    function reduce(arr, identity, callback) {
        var x = identity;
        for (var i = 0; i < arr.length; i++) {
            x = callback(x, arr[i]);
        }
        return x;
    }

**Warning:** Just like in the case of `foreach()`, you must not mutate the
array while it is being `reduce()`d.

    array filter(array arr, bool predicate(any value, integer index))

Returns the elements in `arr` for which `predicate()` returns
true. The implementation may look something like this:

    function filter(arr, predicate) {
        var res = [];
        foreach(arr, function(val, index) {
            if predicate(val, index) {
                res.push(val);
            }
        });
        return res;
    }

You must not mutate the array while it is being `filter()`ed.

    array map(array arr, any callback(any val, integer index))

Calls `callback()` with each key-value pair of `arr` (in an unspecified order).
Returns a new array that contains the same keys as `arr`, and of which the
values correspond to the return values of `callback()` called with the
appropriate key-value pair. In short, the effect of this function is roughly
equivalent with the following pseudo-code:

    function map(arr, callback) {
        var res = [];
        foreach(arr, function(val, index) {
            res.push(callback(val, index));
        });
        return res;
    }

Again, you must not modify `arr` while it is being `map()`ped over.

    nil insert(array arr, any elem, int index)

Inserts `elem` at position `index` into `arr`, shifting all elements in the
range `[index, arr.length)` to the right by one. `index` shall be in the
interval `[0, arr.length)`.

    nil inject(array self, array needle [, int index])

Inserts each element of `needle` between the elements of `self`, starting
at index `index`. Shifts the elements of `self` in the range
`[index, self.length)` to the right by `needle.length` places.
If `index` is not specified, it is assumed to be `self.length`.
Again, `index` should be in the range `[0, self.length]`.

    nil erase(array arr, int index)

Removes the element of `arr` at index `index` and shifts the rest of the
elements to the left by one, so that the array remains contiguous.
The size of `arr` will be decremented by one.
`index` must be in the range `[0, arr.length)`.

    array concat(...)

Receives zero or more arrays. Returns a new array containing all elements of
each of the arguments in order. When given no arguments, returns an empty array.

    nil push(array arr, any elem)

Performs the operation `arr.insert(elem, arr.length)`.

    any pop(array arr)

Removes the last element of `arr` and returns it. "Last" means the element at
index `arr.length - 1`.

    any last(array arr)

Returns the last element of `arr` (that is, `arr[arr.length - 1]`).
Throws a runtime error if `arr` is empty.

    nil swap(array arr, int idx1, int idx2)

Swaps the elements of `arr` at indices `idx1` and `idx2`. Both indices must
be within the range `[0, arr.length)`.

    array reverse(array arr)

Returns an array of which the values are those of `arr`, in reverse order.

The following properties are available on arays:

    int length

Evaluates to the number of keys (and values) in the array.

4. Hashmaps
-----------

Just like functions in the array library, most of the hashmap functions are
also implemented as methods.

    array keys(hashmap self)
    array values(hashmap self)

returns an array of the keys and values, respectively, of the given hashmap.

    foreach(hashmap self, function callback)
    hashmap map(hashmap self, function transform)
    hashmap filter(hashmap self, function predicate)

These methods are similar to their corresponding pairs in the array library,
except that these operate on hashmaps. As such, they don't guarantee
the order of keys and values, and instead of integral indices, the second
parameter that is passed to the callback functions is the appropriate key.

    hashmap combine(array keys, array values)

Returns a hashmap of which the keys are the elements of `keys`, and the
corresponding values are the elements of `values`, in order.

Hashmaps also have a `length` property which yields the number of (non-`nil`)
values in a particular hashmap.

5. Real, integer and complex mathematical functions
---------------------------------------------------

Function names are self-explanatory. A. k. a., "nobody ain't no time for
writing the documentation". :) (will do this when I have some spare time.)

Trigonometric functions take the angle in radians, arcus functions return it
in radians. `round()`, `floor()` and `ceil()` return an `int`. `min()` and
`max()` take any number of arguments but at least one. `random()` returns
a `float` between 0 and 1 inclusive.

`isnan()`, `isinf()`, `isfin()`, `isfloat()` and `isint()` return true if the
number passed in is `NaN` (not a number), infinite, finite, floating-point or
an integer, respectively.

`fact(n)` and `binom(n, k)` compute factorial and the binomial coefficient.
These functions operate on integers only. (The C standard library doesn't
provide the gamma function, and it's not used that often except in highly
specialized computations, so it's simply excluded from the Sparkling stdlib.)

Complex numbers are represented by arrays which have at least the following
keys as strings: `re` and `im`, which correspond to the real and imaginary
parts. This is the so-called canonical form of complex numbers. The functions
that convert between canonical and polar/trigonometric forms understand (and
produce) arrays with the keys `r` and `theta`. See `can2pol()` and `pol2can()`
below.

`cplx_add()`, `cplx_sub()`, `cplx_mul()` and `cplx_div()` perform basic
arithmetic operations on two complex numbers.

`cplx_sin()`, `cplx_cos()`, `cplx_tan()` and `cplx_cot()` compute
trigonometric functions of complex numbers.

`cplx_conj()` returns the conjugate of its argument.

`can2pol()` and `pol2can()` convert between the canonical form (Cartesian
coordinates) and the trigonometric form (polar coordinates). Complex numbers
in the trigonometric form are realized using an array of two numbers,
assigned to the keys `r` and `theta`.

    array range(int n)
    array range(int begin, int end)
    array range(float begin, float end, float step)

The first variation of the `range()` function produces an array of `n`
integers, in the half-closed interval `[0, n)`.

The second variation returns an array of `begin - end` integers, in the
half-closed range `[begin, end)`.

The third version returns an array of floating-point numbers, in the **closed
interval** `[begin, end]`, with the difference between two consecutive values
being `step`.

The following global constants (known for their existence in the BSD and GNU C
libraries) are also available:

    number M_E
    number M_PI
    number M_SQRT2
    number M_PHI
    number M_NAN: "Not a Number" value
    number M_INF: positive infinity

6. Accessing the shell, the OS and the Sparkling engine
-------------------------------------------------------

    string getenv(string name)

Returns the value of the environment variable `name`, or `nil` if it's not set.

    int system(string cmd)

Runs the command `cmd` in the shell, returns the exit status.

    nil assert(bool cond, string errmsg)

Evaluates `cond`, and if it is false, terminates the program, printing the
error message to the standard error stream.

    int time(void)

Returns the current Unix timestamp in seconds.

    hashmap utctime(int timestamp)
    hashmap localtime(int timestamp)

Returns a hashmap representing the timestamp, interpreted with respect to UTC
or the local zone time. The keys are strings:

 - "sec", "min", "hour" contain the number of seconds, minutes and hours as
 integers.
 - "mday" corresponds to the ordinal number of the day in the month.
 - values corresponding to "month" and "year" contain the month and year number
 as integers.
 - "wday" and "yday" yield the number of the day within the week and the year,
 respectively.
 - "isdst" is a boolean which is true if DST is in effect, and false if DST
 is not currently in effect or if DST information is unavailable.

    string fmtdate(string fmt, array timespec)

Returns a formatted date/time string from an array returned by `utctime()` or
`localtime()`. The format specification follows the rules of the C standard
library function `strftime()`.

    float difftime(int ts2, int ts1)

Returns the difference between the two timestamps.

    function compile(string source)

This function parses and compiles the supplied source code. On success, it
returns the compiled function. On error, it returns an error string.

    function exprtofn(string source)

Tries to parse and compile `source` as if it was an expression. On success,
returns a function which, when called, will evaluate the expression.
Returns an error message upon failure.

    int toint(string str)
    float tofloat(string str)
    [ int | float ] tonumber(string str)

These convert the target string to an integer or a floating-point number.
`tonumber()` tries to guess if the target string is a float or an int by
searching for a radix point `.` and/or an exponent (`e` or `E`) in it.
If it finds one, it invokes `tofloat()`, otherwise it invokes `toint()`.

    any call(function fn, array argv)

Calls the function `fn` with the elements of the `argv` array as arguments,
returning the return value of `fn` itself. Throws an error if `argv` is not an
array. This function is implemented as a method on function objects.

The arguments of the called function `fn` will be the values in `argv`
in sequence.

    function require(string filename)

Loads, compiles and executes the given file. Returns the result of running the
file. Throws a runtime error upon failure.

    array backtrace()

This function returns the stack trace, as an array of strings, which are
the names of the currently active functions, at the point of execution
where it is called.

The following global symbolic constants are available:

    hashmap Array

The default class for array objects.

    hashmap String

The default class for string objects.

    hashmap HashMap

The default class for hashmap objects.

    hashmap Function

The default class for function objects.

