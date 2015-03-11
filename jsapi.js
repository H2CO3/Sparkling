/*
 * jsapi.js
 * JavaScript bindings for Sparkling
 * Created by Arpad Goretity on 01/09/2014.
 *
 * Licensed under the 2-clause BSD License
 */

(function () {
	// Takes a JavaScript string, returns the index of
	// the function representing the compiled program
	var compile = Module.cwrap('jspn_compile', 'number', ['string']);

	var compileExpr = Module.cwrap('jspn_compileExpr', 'number', ['string']);

	var parse = Module.cwrap('jspn_parse', 'number', ['string']);

	var parseExpr = Module.cwrap('jspn_parseExpr', 'number', ['string']);

	var compileAST = Module.cwrap('jspn_compileAST', 'number', ['number']);

	// Takes a function index and an array index.
	// Returns the index of the value which is the result of calling the
	// function at the given index in the specified array as arguments.
	var call = Module.cwrap('jspn_call', 'number', ['number', 'number']);

	// takes the name of a global value, returns its referencing index
	var getGlobal = Module.cwrap('jspn_getGlobal', 'number', ['string']);

	// Given a JavaScript value, converts it to a Sparkling value
	// and returns its referencing index
	var addJSValue = function (val) {
		var typeMap = {
			'undefined': addNil,
			'boolean':   addBool,
			'number':    addNumber,
			'string':    addString,
			'object':    addObject,
			'function':  addFunction
		};

		var typeErrorFn = function () {
			throw "Unrecognized type: " + typeof val;
		};

		var conversionFn = typeMap[typeof val] || typeErrorFn;

		return conversionFn(val);
	};

	// Private helpers for addJSValue
	var addNil = Module.cwrap('jspn_addNil', 'number', []);
	var addBool = Module.cwrap('jspn_addBool', 'number', ['number']);
	var addNumber = Module.cwrap('jspn_addNumber', 'number', ['number']);
	var addString = Module.cwrap('jspn_addString', 'number', ['string']);

	var addObject = function (val) {
		// unfortunately, typeof null === 'object'...
		if (val === null) {
			return addNil();
		} else if (val instanceof Array) {
			return addArray(val);
		} else if (val instanceof SparklingUserInfo) {
			return addUserInfo(val);
		} else {
			return addDictionary(val);
		}
	};

	var addArray = function (val) {
		var length = val.length;
		var i;
		var index;
		var indexBuffer = Module._malloc(length * 4);
		var result;

		for (i = 0; i < length; i++) {
			index = addJSValue(val[i]);
			Module.setValue(indexBuffer + i * 4, index, 'i32');
		}

		result = addArrayWithIndexBuffer(indexBuffer, length);
		Module._free(indexBuffer);
		return result;
	};

	var addDictionary = function (val) {
		var keys = Object.keys(val);
		var length = keys.length;
		var i;
		var key;
		var keyIndex, valIndex;
		var indexBuffer = Module._malloc(length * 2 * 4);
		var result;

		for (i = 0; i < length; i++) {
			key = keys[i];
			keyIndex = addJSValue(key);
			valIndex = addJSValue(val[key]);
			Module.setValue(indexBuffer + (2 * i + 0) * 4, keyIndex, 'i32');
			Module.setValue(indexBuffer + (2 * i + 1) * 4, valIndex, 'i32');
		}

		result = addDictionaryWithIndexBuffer(indexBuffer, length);
		Module._free(indexBuffer);
		return result;
	};

	var addUserInfo = function (val) {
		return val.index;
	};

	// Adds a native array that contains the object at the
	// specified referencing indices, then returns the
	// referencing index of the newly created array.
	// Parameters: (int32_t *indexBuffer, size_t numberOfObjects)
	var addArrayWithIndexBuffer = Module.cwrap('jspn_addArrayWithIndexBuffer', 'number', ['number', 'number']);

	// Similar to addArrayWithIndexBuffer, but also adds the keys
	// and constructs a dictionary instead of an array.
	// Parameters: (int32_t *indexBuffer, size_t numberOfKeyValuePairs)
	var addDictionaryWithIndexBuffer = Module.cwrap('jspn_addDictionaryWithIndexBuffer', 'number', ['number', 'number']);

	// this belongs to addFunction
	var wrappedFunctions = [];

	var addFunction = function (val) {
		// optimization: if this function is a wrapper around
		// a function that comes from Sparkling anyway, then
		// just return its original index.
		if (val.fnIndex !== undefined) {
			return val.fnIndex;
		}

		// Else, i. e. if 'val' is a naked JavaScript function,
		// then create a wrapper around it
		var wrapIndex = wrappedFunctions.length;
		wrappedFunctions.push(val);
		return addWrapperFunction(wrapIndex);
	};

	// Takes an index into the wrappedFunctions array.
	// Returns the referencing index of an SpnValue<SpnFunction> that,
	// when called, will call the aforementioned JavaScript function.
	var addWrapperFunction = Module.cwrap('jspn_addWrapperFunction', 'number', ['number']);

	// This is the inverse of 'addJSValue'. Given an internal
	// referencing index, pulls out the corresponding SpnValue
	// and converts it into a JavaScript value.
	var valueAtIndex = function (index) {
		/*
		var TYPE_NIL      = 0,
		    TYPE_BOOL     = 1,
		    TYPE_NUMBER   = 2,
		    TYPE_STRING   = 3,
		    TYPE_ARRAY    = 4,
		    TYPE_HASHMAP  = 5,
		    TYPE_FUNC     = 6,
		    TYPE_USERINFO = 7;
		*/

		var conversionFunctions = [
			function () { return undefined; },
			getBool,
			getNumber,
			getString,
			getArray,
			getHashmap,
			getFunction,
			getUserInfo
		];

		var typeErrorFn = function () {
			throw "unknown type tag";
		};

		var typeIndex = typeAtIndex(index);
		var conversionFn = conversionFunctions[typeIndex] || typeErrorFn;

		return conversionFn(index);
	};

	var typeAtIndex = Module.cwrap('jspn_typeAtIndex', 'number', ['number']);

	// Module.cwrap seems <s>not to interpret the 'boolean'
	// return type correctly</s> not to support 'boolean' at all,
	// either as a return type or as an argument type, and passing
	// 'boolean' as an argument type will throw an exception.
	// Hence the following work-around.
	var getBool = function (index) {
		return !!rawGetBool(index);
	}

	var rawGetBool = Module.cwrap('jspn_getBool', 'number', ['number']);
	var getNumber = Module.cwrap('jspn_getNumber', 'number', ['number']);
	var getString = Module.cwrap('jspn_getString', 'string', ['number']);

	function SparklingUserInfo(index) {
		this.index = index;
		return this;
	}

	// Just returns a wrapper object
	var getUserInfo = function (index) {
		return new SparklingUserInfo(index);
	};

	var getArray = function (index) {
		var i;
		var values = [];
		var length = countOfArrayAtIndex(index);
		var indexBuffer = Module._malloc(length * 4);

		getValueIndicesOfArrayAtIndex(index, indexBuffer);

		for (i = 0; i < length; i++) {
			valueIndex = Module.getValue(indexBuffer + i * 4, 'i32');
			values[i] = valueAtIndex(valueIndex);
		}

		Module._free(indexBuffer);

		return values;
	};

	var getHashmap = function (index) {
		var i;
		var keyIndex, valueIndex, key, value;
		var object = {};
		var length = countOfHashMapAtIndex(index);
		var indexBuffer = Module._malloc(length * 2 * 4);

		getKeyAndValueIndicesOfHashMapAtIndex(index, indexBuffer);

		for (i = 0; i < length; i++) {
			keyIndex = Module.getValue(indexBuffer + (2 * i + 0) * 4, 'i32');
			key = valueAtIndex(keyIndex);

			valueIndex = Module.getValue(indexBuffer + (2 * i + 1) * 4, 'i32');
			value = valueAtIndex(valueIndex);

			if (typeof key !== 'string') {
				Module._free(indexBuffer);
				throw "keys must be strings";
			}

			object[key] = value;
		}

		Module._free(indexBuffer);

		return object;
	};

	var countOfArrayAtIndex = Module.cwrap('jspn_countOfArrayAtIndex', 'number', ['number']);
	var countOfHashMapAtIndex = Module.cwrap('jspn_countOfHashMapAtIndex', 'number', ['number']);
	var getValueIndicesOfArrayAtIndex = Module.cwrap('jspn_getValueIndicesOfArrayAtIndex', null, ['number', 'number']);
	var getKeyAndValueIndicesOfHashMapAtIndex = Module.cwrap('jspn_getKeyAndValueIndicesOfHashMapAtIndex', null, ['number', 'number']);

	var getFunction = function (fnIndex) {
		// XXX: should we check if the value at given index is really a function?

		var result = function () {
			var argv = Array.prototype.slice.apply(arguments);
			var argvIndex = addArray(argv); // returns the index of an SpnValue<SpnArray>
			var retIndex = call(fnIndex, argvIndex);

			if (retIndex < 0) {
				throw Sparkling.lastErrorMessage();
			}

			return valueAtIndex(retIndex);
		};

		// optimization, see the relevant comment in addFunction
		result.fnIndex = fnIndex;
		return result;
	};

	// Takes a pointer to SpnArray and an integer index,
	// Returns the integer value of the array's element at that index.
	var getIntFromArray = Module.cwrap('jspn_getIntFromArray', 'number', ['number', 'number']);

	var backtrace = Module.cwrap('jspn_backtrace', 'string', []);

	var lastErrorLine = Module.cwrap('jspn_lastErrorLine', 'number', []);
	var lastErrorColumn = Module.cwrap('jspn_lastErrorColumn', 'number', []);

	Sparkling = {
		// export these values as "private" symbols
		// in order that auxlib.js be able to use them
		_wrappedFunctions: wrappedFunctions,
		_valueAtIndex: valueAtIndex,
		_addJSValue: addJSValue,
		_getIntFromArray: getIntFromArray,

		// Public API
		compile: function (src) {
			var fnIndex = compile(src);
			return fnIndex < 0 ? undefined : getFunction(fnIndex);
		},

		compileExpr: function (src) {
			var fnIndex = compileExpr(src);
			return fnIndex < 0 ? undefined : getFunction(fnIndex);
		},

		parse: function (src) {
			var astIndex = parse(src);
			return astIndex < 0 ? undefined : getHashmap(astIndex);
		},

		parseExpr: function (src) {
			var astIndex = parseExpr(src);
			return astIndex < 0 ? undefined : getHashmap(astIndex);
		},

		compileAST: function (ast) {
			var astIndex = addDictionary(ast);
			var fnIndex = compileAST(astIndex);
			return fnIndex < 0 ? undefined : getFunction(fnIndex);
		},

		lastErrorMessage: Module.cwrap('jspn_lastErrorMessage', 'string', []),
		lastErrorType: Module.cwrap('jspn_lastErrorType', 'string', []),
		lastErrorLocation: function () {
			return {
				line: lastErrorLine(),
				column: lastErrorColumn()
			};
		},

		backtrace: function () {
			var bt = backtrace();
			return bt ? bt.split("\n") : [];
		},

		getGlobal: function (name) {
			return valueAtIndex(getGlobal(name));
		},

		// Frees all memory used by Sparkling values generated
		// by Sparkling code. (This includes the return values
		// of functions as well as the results of automatic
		// conversion between JavaScript and Sparkling in both
		// directions.)
		freeAll: Module.cwrap('jspn_freeAll', null, []),

		reset: Module.cwrap('jspn_reset', null, [])
	};
}());
