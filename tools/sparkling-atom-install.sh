#!/bin/bash

if [ "$(which apm)" ]; then
	apm install language-sparkling
else
	printf "Atom Package Manager (apm) is not installed.\nInstall it, then launch this script once again.\n"
fi
