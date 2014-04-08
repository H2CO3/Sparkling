// 
// jspn.js
// JavaScript bindings for Sparkling
// Created by Árpád Goretity on 06/04/2014.
// 
// Licensed under the 2-clause BSD License
//

JSpn = {
	// Takes:	source text (string)
	// Returns:	compiled function or error message index (number).
	_compile:	Module.cwrap("jspn_compile", "number", [ "string" ]),

	// Takes:	source text (string)
	// Returns:	compiled function or error object
	compile:	function(source) {
				var fnIndex = JSpn._compile(source);

				if (fnIndex == JSpn._errorIndex) {
					return null;
				}

				return function() {
					var argv = Array.prototype.slice.apply(arguments);
					return JSpn._execute(fnIndex, argv);
				};
			},

	// Takes:	function index (number)
	// 		comma-separated argument index list (string)
	// Returns:	result of call (value index, double number)
	_c_execute:	Module.cwrap("jspn_execute", "number", [ "number", "string" ]),

	// Takes: 	function index (double number)
	// 		argument list (array)
	// Returns:	index of result of call (double number)
	_execute:	function(fnidx, argv) {
				var retidx;

				var argindices = [];
				var i;
				for (i = 0; i < argv.length; i++) {
					argindices[i] = JSpn._addValue(argv[i]);
				}

				retidx = JSpn._c_execute(fnidx, argindices.join(","));

				if (retidx == JSpn._errorIndex) {
					return null;
				}

				return JSpn._getValue(retidx);
			},

	// returns last error message
	error:		Module.cwrap("jspn_errmsg", "string", []),
	// returns a type string corresponding to the value at index
	_typeOf:	Module.cwrap("jspn_typeof", "string", [ "number" ]),

	// getters (they all take a value index)
	// for some reason, jspn_getbool returns a number, so we need to wrap
	// it in another function, which converts the number to a boolean...
	_c_getBool:	Module.cwrap("jspn_getbool", "boolean", [ "number" ]),
	_getBool:	function(validx) { return !!JSpn._c_getBool(validx); },
	_getInt:	Module.cwrap("jspn_getint", "number",   [ "number" ]),
	_getFloat:	Module.cwrap("jspn_getfloat", "number", [ "number" ]),
	_getString:	Module.cwrap("jspn_getstr", "string",   [ "number" ]),

	// one getter above all
	_getValue:	function(validx) {
				var intVal, floatVal;

				switch (JSpn._typeOf(validx)) {
				case "nil":	return undefined;
				case "bool":	return JSpn._getBool(validx);
				case "number":	{
					intVal = JSpn._getInt(validx);
					floatVal = JSpn._getFloat(validx);
					return intVal == floatVal ? intVal : floatVal;
				}
				case "string":	return JSpn._getString(validx);
				default:	throw "Sparkling: invalid return type: " + JSpn._typeOf(validx);
						return null;
				}
			},

	// Setters. They return the index of the created object.
	_addNil:	Module.cwrap("jspn_addnil",    "number", []),
	_addBool:	Module.cwrap("jspn_addbool",   "number", [ "boolean" ]),
	_addNumber:	Module.cwrap("jspn_addnumber", "number", [ "number" ]),
	_addString:	Module.cwrap("jspn_addstr",    "number", [ "string" ]),


	// adds a generic value based on its type
	_addValue:	function(val) {
				switch (typeof val) {
				case "undefined":	return JSpn._addNil();
				case "boolean":		return JSpn._addBool(val);
				case "number":		return JSpn._addNumber(val);
				case "string":		return JSpn._addString(val);
				default:
							throw "Sparkling: invalid parameter type: " + typeof val;
							return JSpn._errorIndex;
				}
			},

	// value that is returned upon encountering an error
	// (be it a parser, compiler or runtime error)
	_errorIndex: -2.0
};

