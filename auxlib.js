/*
 * auxlib.js
 * JavaScript bindings for Sparkling
 * Created by Arpad Goretity on 03/09/2014
 *
 * This file is part of Sparkling.
 *
 * Sparkling is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Sparkling is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Sparkling. If not, see <http://www.gnu.org/licenses/>.
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
