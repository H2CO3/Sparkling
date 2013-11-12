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

A complete list of functions can be found in the `rtlb.h` header file.

1. I/O library (spn_libio)
--------------------------

    usrdat stdin
    usrdat stdout
    usrdat stderr

Globals of type "user data", representing the standard input, output and error
stream, respectively.

    string getline(void)
Reads a new line from the standard input and returns it as a string.
The maximal size of a line is dictated by the C library macro `LINE_MAX`.
If not present, Sparkling defines this macro to 4096.

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
 - `%[+| ][W][.P][+]e` formats a floating-point number in the same manner as
 `%f`, but it uses scientific (exponential) notation, e. g. `1.337e+3`. If a
 second `+` sign, after the decimal point, is present, then the exponent will
 always have an explicit sign.
 - `%[W]q` formats an escaped string. If the field width modifier is present,
 it prints at most `W` characters.
 - `%B` formats a Boolean value. Prints either true or false.
 - Width and precision may both be specified as `*`, in which case the actual
 width or precision is determined by looking at the next argument of the
 function (one additional argument is used for each such variable-length format
 specifier).

<!-- this comment is needed because Markdown sucks. -->

    userdata fopen(string name, string mode)

Opens the file `name` in mode `mode`. The meaning of the mode string is
identical to that of the second argument of `fopen()` in the C standard library.
Returns an user data value representing the open file if successful, or `nil`
on failure.

    nil fclose(userdata file)

closes the file object associated with `file`.

    nil fprintf(userdata file, string format, ...)
    string fgetline(userdata file)

These work the same as `printf()` and `getline()`, but they operate on the
specified file instead of `stdout`.

    string fread(userdata file, int length)

Reads `length` bytes from the open file `file`. Returns the bytes as a string
on success, `nil` on failure.

    bool fwrite(userdata file, string buf)

writes the characters in the string `buf` into the file `file`. Returns true
on success, false on error.

    nil fflush(userdata file)

flushes the buffer of `file`, which must be a file opened for writing.

    int ftell(userdata file)

returns the position indicator of `file`, i. e. the offset where the next
read or write operation occurs. Returns a negative value on failure.

    bool fseek(userdata file, int off, string whence)

Sets the file position indicator to `off`. `whence` should be one of `"cur"`,
`"set"` or `"end"`. Its meaning is the same as it is when used with the C
stdlib function `fseek()`.

    bool feof(userdata file)

returns true if the position indicator of `file` is at the end, false otherwise.

    bool remove(string fname)

Deletes the file with the name `fname`. Returns true on success, false on error.

    bool rename(string old, string new)

renames file `old` so that it will have the name `new`.
Returns true on success, false on failure.

    string tmpnam(void)
    userdata tmpfile(void)

These function return the name of a temporary file, or an alrady open handle
to it, respectively.

2. String manipulation (spn_libstring)
--------------------------------------
    int indexof(string haystack, string needle [, int offset])

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

    string fmtstring(string format, ...)

Works the same way as `printf()`, but instead of printing to stdout, it returns
the whole formatted string.

    int toint(string str)
    float tofloat(string str)
    [ int | float ] tonumber(string str)

These convert the target string to an integer or a floating-point number.
`tonumber()` tries to guess if the target string is a float or an int by
searching for a radix point `.` and/or an exponent (`e` or `E`) in it.
If it finds one, it invokes `tofloat()`, otherwise it invokes `toint()`.

3. Array handling (spn_libarray)
--------------------------------
    array array(...)

returns an array filled with the arguments of the function. Order is preserved;
indices will be integers starting from 0, increasing by one. The return value
of this function is thus equivalent with the `@[ ]` sequence literal notation.

    array dict(...)

Returns an array filled with keys and values in the following way: every
argument with an even index in the argument list of the function will become
a key, and the next value will be the corresponding value. Order is not
preserved. The return value of this function is equivalent with the `@{ }`
dictionary literal notation.

    nil sort(array arr)
    nil sortcmp(array arr, function comparator)

`sort()` sorts the elements of `arr` in ascending order, using the `<` operator.
The array to be sorted must contain integer keys only, and they keys should
span the range `[0, sizeof arr)` without gap. `sortcmp()` works similarly to
`sort()`, but it uses the function `comparator()` to compare elements. It should
should return true if its first argument is ordered before the second one.

    int linearsrch(array arr, any element)

These functions return the index at which `element` is found in the array,
or -1 if is is not in the array. `binarysrch()` can only be used on sorted
arrays. The array must fulfill all the criteria that a sortable array has
(see above).

    bool contains(array arr, any element)

returns true if `element` is in `arr`, else returns false.

    string join(array arr, string sep)

All elements in the array must be strings, the array must have integer indices
only, ranging from `0` to `sizeof arr`. The return value is the concatenation
of the elements interleaved by `sep`.

    nil enumerate(array a, function callback [, any context])

Iterates through the elements of the array `a`, calling `callback` for each
key-value pair in the array (like `callback(key, a[key], context)`). The
context info is an optional argument. The callback function must return `nil`
or a Boolean. If it returns `false`, the enumeration is aborted and the
`enumerate()` function returns.

4. Real and integer mathematical functions (spn_libmath)
--------------------------------------------------------
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

The following global constants (known for their existence in the BSD and GNU C
libraries) are also available:

    number M_E
    number M_LOG2E
    number M_LOG10E
    number M_LN2
    number M_LN10
    number M_PI
    number M_PI_2
    number M_PI_4
    number M_1_PI
    number M_2_PI
    number M_2_SQRTPI
    number M_SQRT2
    number M_SQRT1_2
    number M_NAN: "Not a Number" value
    number M_INF: positive infinity

5. Basic time and date manipulation (spn_libtime)
-------------------------------------------------
    int time(void)

Returns the current Unix timestamp in seconds.

    array gmtime(int timestamp)
    array localtime(int timestamp)

Returns an array representing the timestamp, interpreted with respect to UTC
or the local zone time. The keys in the array are strings:

 - "sec", "min", "hour" contain the number of seconds, minutes and hours as
 integers.
 - "mday" corresponds to the ordinal number of the day in the month.
 - values corresponding to "mon" and "year" contain the month and year number
 as integers.
 - "wday" and "yday" yield the number of the day within the week and the year,
 respectively.
 - "isdst" is another integer which is greater than zero if DST is in effect,
 less than zero if DST information is unavailable, and zero if DST is not
 currently in effect.

    string strftime(string fmt, array timespec)

Returns a formatted date/time string from an array returned by `gmtime()` or
`localtime()`. The format specification follows the rules of the C standard
library function `strftime()`.

    float difftime(int ts2, int ts1)

Returns the difference between the two timestamps.

6. Interfacing with the shell and the OS (spn_libsys)
-----------------------------------------------------
    string getenv(string name)

Returns the value of the environment variable `name`, or `nil` if it's not set.

    int system(string cmd)

Runs the command `cmd` in the shell, returns the exit status.

    nil assert(bool cond, string errmsg)

Evaluates `cond`, and if it is false, terminates the program, printing the
error message to the standard error stream.

    nil exit(int status)

terminates the **host program** by calling the C standard library function
`exit()` with the specified exit status code.

