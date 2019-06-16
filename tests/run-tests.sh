#!/usr/bin/env bash
if ! shellcheck "$0"; then
		echo "$0: shellcheck $0 failed."
		exit 1
fi

if ! command -v mapfile 2>/dev/null 1>/dev/null; then
		echo "$0:$LINENO:1: error: you need a newer version of bash (one that supports mapfile)"
		exit 1
fi

run_test="$(dirname "$0")/run-test.sh"
if ! shellcheck "$run_test"; then
		echo "$0: shellcheck ${run_test} failed."
		exit 1
fi

bin_dir=$1
source_dir=$2
test_dir=$3

if ! [[ -d ${test_dir} ]]; then
		echo "$0:$LINENO:1: error: ${test_file} does not exist!"
		exit 1
fi

declare -i passed runs
passed=0
runs=0
failed_tests=()
for test_file in "${test_dir}"/test_*.zion; do
		runs+=1
		[[ "$DEBUG_TESTS" != "" ]] && echo "$0: Invoking run-test.sh on ${test_file}..."
		if "${run_test}" "${bin_dir}" "${source_dir}" "${test_file}"; then
				passed+=1
		else
				failed_tests+=("$test_file")
				echo "$0:$LINENO:1: error: test ${test_file} failed"
				if [[ "${FAIL_FAST}" != "" ]]; then
					echo "FAIL_FAST was specified. Quitting..."
					exit 1
				fi
		fi
done

if [[ ${#failed_tests[*]} != 0 ]]; then
		echo "$0:$LINENO:1: Tests failed ($((runs-passed))/${runs}):"
		for failed_test in ${failed_tests[*]}; do
				printf "\t%s\n" "$failed_test"
		done
		exit 1
else
		echo "Tests passed (${passed}/${runs})!"
		exit 0
fi
