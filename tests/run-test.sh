#!/usr/bin/env bash
bin_dir=$1
source_dir=$2
test_file=$3

if ! [[ -e ${test_file} ]]; then
		echo "${source_dir}/tests/run-test.sh:1:1: ${test_file} does not exist!"
		exit 1
fi

# Find all the test flags in this file
mapfile -t test_flags < <((grep '^# test: ' "${test_file}" |
		grep -Eo -- '\b[a-zA-Z]+\b' |
		grep -v '^test$') 2>/dev/null)

if [[ ${#test_flags[*]} == 0 ]]; then
		echo "${test_file}:1:1: missing test flags directive (# test: pass, or # test: fail, etc...)"
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
				# echo "Defaulting expects to PASS"
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
		echo "run-test.sh: ${test_file} SKIPPED!"
		exit 0
fi

if [[ "${test_flags[*]}" =~ "noprelude" ]]; then
		export NO_PRELUDE=1
fi

if [[ $should_pass = "$should_fail" ]]; then
		echo "${test_file}:1:1: you must specify one and only one of pass or fail for tests"
		exit 1
fi

# Get the exit code and output of the compilation and running of the test file
output=$(mktemp -q)
trap 'rm -f $output' EXIT

# The next line is intended to ease the path from seeing a bunch of failing tests, to narrowing in
# on reproducible test failure. This should save future humans time in trying to reproduce the
# test-run in their debugger.
echo ZION_PATH="\"${ZION_PATH}\"" "'${bin_dir}/zion'" run "'${test_file}'"

("${bin_dir}/zion" run "${test_file}" 2>&1) > "$output"
res=$?

if [[ $res -eq 0 ]]; then
		passed=true
else
		passed=false
fi

if [[ $passed != "${should_pass}" ]]; then
		echo "run-test.sh: ${test_file} output was:"
		cat "$output"
		echo "run-test.sh: ${test_file} FAILED!"
		exit 1
fi

for ((i=0;i < ${#expects[*]}; ++i)); do
		expect="${expects[$i]}"
		# echo "Expecting \"${expect}\"..."
		if ! grep -E "$expect" "$output"; then
				echo "run-test.sh: ${test_file} output was:"
				cat "$output"
				echo "$0:$LINENO:1: error: Could not find '$expect' in output."
				echo "run-test.sh: ${test_file} FAILED!"
				exit 1
		fi
done

for ((i=0;i < ${#rejects[*]}; ++i)); do
		reject="${rejects[$i]}"
		# echo "Expecting \"${reject}\"..."
		if grep -E "$reject" "$output"; then
				echo "run-test.sh: ${test_file} output was:"
				cat "$output"
				echo "$0:$LINENO:1: error: Found '$reject' in output."
				echo "run-test.sh: ${test_file} FAILED!"
				exit 1
		fi
done

echo "run-test.sh: ${test_file} PASSED!"
exit 0
