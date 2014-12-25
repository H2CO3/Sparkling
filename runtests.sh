#!/bin/sh

CLR_ERR="\x1b[1;31m"
CLR_SUC="\x1b[1;32m"
CLR_BLD="\x1b[1;49m"
CLR_RST="\x1b[0m"

function test_valid {
	PROGRAM=$1
	FILE=$2

	printf "Testing %s... " $FILE

	if [[ $USE_VALGRIND -ne 0 ]]; then
		$VALGRIND $PROGRAM $FILE;
	else
		$PROGRAM $FILE 2>/dev/null 1>/dev/null;
	fi || {
		echo "${CLR_ERR}unexpectedly rejected valid input$CLR_RST";
		FAILED=$((FAILED+1))
		false;
	} && {
		echo "OK"
		PASSED=$((PASSED+1))
	}
}

function test_invalid {
	PROGRAM=$1
	FILE=$2

	printf "Testing %s... " $FILE

	if [[ $USE_VALGRIND -ne 0 ]]; then
		$VALGRIND $PROGRAM $FILE;
	else
		$PROGRAM $FILE 2>/dev/null 1>/dev/null;
	fi && {
		echo "${CLR_ERR}unexpectedly accepted invalid input$CLR_RST";
		FAILED=$((FAILED+1))
	} || {
		echo "OK"
		PASSED=$((PASSED+1))
	}
}

function run_tests_in_directory {
	TESTDIR=$1
	SPARKLING=$2

	for f in $TESTDIR/p_*; do
		test_valid "$SPARKLING" "$f";
	done

	for f in $TESTDIR/f_*; do
		test_invalid "$SPARKLING" "$f";
	done
}

PASSED=0
FAILED=0

USE_VALGRIND=1
WORKDIR=$(pwd)

pushd 'test'

# Run unit tests for parser
run_tests_in_directory parser "$WORKDIR/bld/spn --dump-ast";

# Run unit tests for compiler
# run_tests_in_directory compiler "$WORKDIR/bld/spn --compile";

# Run unit tests for VM/runtime
# run_tests_in_directory runtime "$WORKDIR/bld/spn";

# Run unit tests for library functions
# run_tests_in_directory stdlib "$WORKDIR/bld/spn";

popd

echo "$CLR_BLD$((PASSED+FAILED)) total, $CLR_SUC$PASSED passed, $CLR_ERR$FAILED failed$CLR_RST"

