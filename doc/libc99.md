Documentation of the Sparkling C99 library
===============================================================

The Sparkling C99 library provides functions not provided by the standard
library which require C99. Here followsa non-formal description of 
the semantics of the standard library functions.

Note that it is required to call these functions with the correct (specified in
the "signature" of each function) number of arguments, else they raise an error.
An exception to this rule is a variadic function (denoted by an ellipsis `...`
in its argument list), which may be called with at least as many arguments as
required.

Note that, at the implementation level, the backing C functions that implement
the standard libraries expect that they are only called through the convenience
context API, i. e. that their "context info" pointer points to an SpnContext
structure.


    int microtime(void)

Returns the current Unix timestamp in microseconds.
