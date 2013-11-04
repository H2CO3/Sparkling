# Sparkling Tutorial

1. Semicolons

The same as in C and different then JavaScript every statement mast end with semicolon.

2. Keywords:

This is a list of all keywords:

and, else, in, return, argc, false, nan, sizeof, as, for, nil, true, break,
foreach, not, typeof, continue, function, null, var, do, if, or, while

3. Variables

All varaibles need to be declared first with `var`. Variables can be initialized when
declared.
example:

```
var i = 10;
var j;
```

You can combine multiple declarations:

```
var i, j;
```

Variables as other identifiers can begin with a lowercase or capital English letter 
(a...z, A...Z) or with an underscore (_) then any of { a...z, A...Z, 0...9, _ (underscore) } follows. 

4. Numbers

You can write integers or floats the same as in C:

 * 10
 * 20
 * 1.1e-2

5. Strings

You write strings in double quotes. You can access single characters of the string the same as arrays
but elements are converted to integers. Strings are indexed from zero.

```
> print("hello"[1]);
101
```

To get number of characters in array you need to use `sizeof` keyword.

```
> print(sizeof "hello");
5
```

This is a list of escape sequances (same as in C) that can be used inside strings:

 * \\		->		\
 * \/		->		/
 * \'		->		'
 * \"		->		"
 * \a		->		bell
 * \b		->		backspace
 * \f		->		form feed
 * \n		->		LF
 * \r		->		CR
 * \t		->		TAB
 * \0		->		NUL, char code 0
 * \xHH		->		the character with code HH, where HH denotes two hexadecimal digits

6. Arrays

To define an array you need to use `array` the same as in php:

```
var empty = array();
var a = array(1,2,3,4,5);
```

You can create an array contaning different types of values. Arrays are indexed from zero the same
as strings.

```
> print(array(1,2,3, "hello")[3]);
hello
```

To get number of elements in array you need to use sizeof (the same as with strings):

```
> print(sizeof array(1));
1
```

7. Expressions:

this is the list of operators:

 * `++` - pre and post increment
 * `--` - pre and post decrement
 * `+` - plus
 * `-` - minus
 * `..` - string concatenations

8. Loops

In sparkling different then in C (and similar to Python) you don't need to wrap
loop expressions inside parentheses. All loops work the same as those from C.

8.1. For loop
  

```
for i = 0; i < 10; ++i {
    print(i);
}
```

8.2. While loop

```
while i < x {
    print(i++);
}
```

8.3. Do..While loop
```
var i = 0;
do {
    print(i++);
} while i < 10;
```

9. If statement

The same as with loops you don't need to wrap if stament with parentheses:

```
if 0 == 0 {
    print('equal');
} else {

}
```
There is no separated elsif keyword like in php or Python you need to use `else if`

```
if i == 0 {
    print("zero");
} else if i > 0 {
    print("greater then zero");
} else {
    print("less then zero");
}
```

Inside if statement you can only use boolean values trying to use numbers like 0 or 1
will cause runtime error.

10. Functions

You can write named and anonymous functions with `function` keyword:

```
function square(x) {
    return x*x;
}
```

if function don't return anything it return `nil`

to invoke a function you do the same as in C

```
print(square(10));
```

## Standard Library
### Functions
1. IO Functions
* getline
* print
* printf
* fopen
* fclose
* fprintf
* fgetline
* fread
* fwrite
* fflush
* ftell
* fseek
* feof
* remove
* rename
* tmpnam
* tmpfile

2. String functions

* indexof
* substr
* substrto
* substrfrom
* split
* repeat
* tolower
* toupper
* fmtstring
* tonumber
* toint
* tofloat

3. Array functions
* array
* dict
* sort
* sortcmp
* linearsrch
* binarysrch
* contains
* subarray
* join
* enumerate
* insert
* insertarr
* delrange
* clear
* iter
* next

4. Math functions
* abs
* min
* max
* floor
* ceil
* round
* hypot
* sqrt
* cbrt
* pow
* exp
* exp2
* exp10
* log
* log2
* log10
* sin
* cos
* tan
* sinh
* cosh
* tanh
* asin
* acos
* atan
* atan2
* deg2rad
* rad2deg
* random
* seed
* isfin
* isinf
* isnan
* isfloat
* isint
* fact
* binom

5. Datetime functions
* time
* gmtime
* localtime
* strftime
* difftime

5. System functions
* getenv
* system
* assert
* exit

### Standards Streams
1. stdin
2. stdout
3. stderr

### Constants
1. M_E
2. M_LOG2E
3. M_LOG10E
4. M_LN2
5. M_LN10
6. M_PI
7. M_PI_2
8. M_PI_4
9. M_1_PI
10. M_2_PI
11. M_2_SQRTPI
12. M_SQRT2
13. M_SQRT1_2

