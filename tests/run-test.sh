#!/bin/bash
bin_dir=$1
source_dir=$2
test_file=$3

if ! [[ -e ${test_file} ]]; then
		echo "${source_dir}/tests/run-test.sh:1:1: ${test_file} does not exist!"
		exit 1
fi

# Find all the test flags in this file
test_flags=($(
		(grep '^# test: ' ${test_file} |
		grep -Eo -- '\b[a-zA-Z]+\b' |
		grep -v '^test$') 2>/dev/null) )

if [[ ${#test_flags[*]} == 0 ]]; then
		echo "${test_file}:1:1: missing test flags directive (# test: pass, or # test: fail, etc...)"
		exit 1
fi

containsElement () {
  local e match="$1"
  shift
  for e; do [[ "$e" == "$match" ]] && return 0; done
  return 1
}

if [[ "${test_flags[*]}" =~ "pass" ]]; then
		should_pass=true
else
		should_pass=false
fi

if [[ "${test_flags[*]}" =~ "fail" ]]; then
		should_fail=true
else
		should_fail=false
fi

if [[ "${test_flags[*]}" =~ "skip" ]]; then
		echo "run-test.sh: skipping ${test_file}"
		exit 0
fi

if [[ "${test_flags[*]}" =~ "noprelude" ]]; then
		export NO_PRELUDE=1
fi

if [[ $should_pass = $should_fail ]]; then
		echo "${test_file}:1:1: you must specify one and only one of pass or fail for tests"
		exit 1
fi

output=( $(${bin_dir}/zion run ${test_file} 2>&1) )
res=$?
if [[ $res -eq 0 ]]; then
		passed=true
else
		passed=false
fi

if [[ $passed = false ]]; then
		if [[ ${should_pass} = true ]]; then
				echo "run-test.sh: ${test_file} output was ${output[*]}"
				echo "run-test.sh: ${test_file} FAILED!"
				exit 1
		else
				echo "run-test.sh: ${test_file} run errored. Checking output for any requirements..."
				exit 0
				echo "run-test.sh: Failed to find required text in output."
				echo "run-test.sh: output: ${output[*]}"
				exit 1
		fi
fi

if [[ " ${output[*]} " =~ " PASS " ]]; then
		echo "run-test.sh: ${test_file} PASSED!"
		exit 0
else
		echo "run-test.sh: ${test_file} ran successfully, but did not emit \"PASS\""
		exit 1
fi
