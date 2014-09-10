/*
 * auxlib.js
 * JavaScript bindings for Sparkling
 * Created by Arpad Goretity on 03/09/2014
 *
 * Licensed under the 2-clause BSD License
 */

mergeInto(LibraryManager.library, {
	jspn_callJSFunc: function jspn_callJSFunc(funcIndex, argc, argv) {
		var wrappedFunctions = Sparkling['_wrappedFunctions'];
		var valueAtIndex = Sparkling['_valueAtIndex'];
		var addJSValue = Sparkling['_addJSValue'];
		var getIntFromArray = Sparkling['_getIntFromArray'];

		var fn = wrappedFunctions[funcIndex];
		var i, index;
		var retVal;
		var arg;
		var args = [];

		for (i = 0; i < argc; i++) {
			index = getIntFromArray(argv, i);
			arg = valueAtIndex(index);
			args.push(arg);
		}

		retVal = fn.apply(undefined, args);
		return addJSValue(retVal);
	},
	jspn_jseval_helper: function jspn_jseval_helper(srcPtr) {
		var addJSValue = Sparkling['_addJSValue'];
		var src = Pointer_stringify(srcPtr);
		var val = eval(src);
		return addJSValue(val);
	}
});
