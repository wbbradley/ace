#!/usr/bin/env bash
bin_dir=$1
source_dir=$2
test_file=$3

# ECHO='echo -e'
ECHO='printf'

if [[ -t 1 ]] && [[ -t 2 ]] && [[ "$VIMRUNTIME" == "" ]]; then
	export WMAKE_INTERACTIVE="1"
	export C_RED="\e[1;31m"
	export C_GREEN="\e[1;32m"
	export C_YELLOW="\e[1;33m"
	export C_RESET="\e[0m"
else
	export WMAKE_INTERACTIVE="0"
	export C_RED=""
	export C_GREEN=""
	export C_YELLOW=""
	export C_RESET=""
fi

if ! [[ -e ${test_file} ]]; then
	$ECHO "${source_dir}/tests/run-test.sh:1:1: ${test_file} does not exist!\n"
	exit 1
fi

# Find all the test flags in this file
mapfile -t test_flags < <((grep '^# test: ' "${test_file}" |
	grep -Eo -- '\b[a-zA-Z]+\b' |
	grep -v '^test$') 2>/dev/null)

if [[ ${#test_flags[*]} == 0 ]]; then
	$ECHO "${test_file}:1:1: missing test flags directive (# test: pass, or # test: fail, etc...)\n"
	exit 1
fi

# Find all the expect directives in this file
mapfile -t expects < <(grep -E '^# expect: .+$' "${test_file}" | cut -c 11-)

# Find all the reject directives in this file
mapfile -t rejects < <(grep -E '^# reject: .+$' "${test_file}" | cut -c 11-)

containsElement () {
  local e match="$1"
  shift
  for e; do [[ "$e" == "$match" ]] && return 0; done
  return 1
}

if [[ "${test_flags[*]}" =~ "pass" ]]; then
	should_pass=true
	if [[ ${#expects[*]} -eq 0 ]]; then
		# $ECHO "Defaulting expects to PASS"
		expects=( PASS )
	fi
else
	should_pass=false
fi

if [[ "${test_flags[*]}" =~ "fail" ]]; then
	should_fail=true
else
	should_fail=false
fi

if [[ "${test_flags[*]}" =~ "skip" ]]; then
	$ECHO "run-test.sh: ${C_YELLOW}SKIPPED${C_RESET} $(basename "${test_file}")!\n"
	exit 0
fi

if [[ "${test_flags[*]}" =~ "noprelude" ]]; then
	export NO_PRELUDE=1
fi

if [[ $should_pass = "$should_fail" ]]; then
	$ECHO "${test_file}:1:1: ${C_RED}error${C_RESET}: you must specify one and only one of pass or fail for tests\n"
	exit 1
fi

# Get the exit code and output of the compilation and running of the test file
output=$(mktemp -q)
trap 'rm -f $output' EXIT

# The next line is intended to ease the path from seeing a bunch of failing tests, to narrowing in
# on reproducible test failure. This should save future humans time in trying to reproduce the
# test-run in their debugger.
[ "$DEBUG_TESTS" != "" ] && $ECHO ZION_PATH="\"${ZION_PATH}\"" "'${bin_dir}/zion'" run "'${test_file}'\n"

("${bin_dir}/zion" run "${test_file}" 2>&1) > "$output"
res=$?

if [ $res -eq 0 ]; then
	passed=true
else
	passed=false
fi

if [ $passed != "${should_pass}" ]; then
	$ECHO "run-test.sh: $(basename "${test_file}") output was:\n"
	cat "$output"
	$ECHO "run-test.sh: ${C_RED}FAILED${C_RESET} ${test_file}!\n"
	exit 1
fi

for ((i=0;i < ${#expects[*]}; ++i)); do
	expect="${expects[$i]}"
	# $ECHO "Expecting \"${expect}\"..."
	if ! grep -E "$expect" "$output" >/dev/null; then
		$ECHO "run-test.sh: $(basename "${test_file}") output was:\n"
		cat "$output"
		$ECHO "$0:$LINENO:1: error: Could not find '$expect' in output.\n"
		$ECHO "run-test.sh: ${C_RED}FAILED${C_RESET} ${test_file}!\n"
		exit 1
	fi
done

for ((i=0;i < ${#rejects[*]}; ++i)); do
	reject="${rejects[$i]}"
	# $ECHO "Expecting \"${reject}\"..."
	if grep -E "$reject" "$output"; then
		$ECHO "run-test.sh: $(basename "${test_file}") output was:\n"
		cat "$output"
		$ECHO "$0:$LINENO:1: error: Found '$reject' in output.\n"
		$ECHO "run-test.sh: ${C_RED}FAILED${C_RESET} ${test_file}!\n"
		exit 1
	fi
done

$ECHO "run-test.sh: ${C_GREEN}PASSED!${C_RESET} $(basename "${test_file}")\n"
exit 0
