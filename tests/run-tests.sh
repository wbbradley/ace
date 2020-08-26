#!/usr/bin/env bash

if [[ -t 1 ]] && [[ -t 2 ]] && [[ "$VIMRUNTIME" == "" ]]; then
	export C_RED="\\e[1;31m"
	export C_GREEN="\\e[1;32m"
	export C_YELLOW="\\e[1;33m"
	export C_RESET="\\e[0m"
else
	export C_RED=""
	export C_GREEN=""
	export C_YELLOW=""
	export C_RESET=""
fi

function logged_run() {
	[ "$DEBUG_TESTS" != "" ] && echo "$@"
	eval "$@"
}

if ! command -v mapfile 2>/dev/null 1>/dev/null; then
		echo "$0:$LINENO:1: error: you need a newer version of bash (one that supports mapfile)"
		exit 1
fi

run_test="$(dirname "$0")/run-test.sh"

# Check all the shell scripts.
if command -v shellcheck; then
	if ! logged_run shellcheck "$0"; then
			echo "$0: shellcheck $0 failed."
			exit 1
	fi

	if ! logged_run shellcheck "$run_test"; then
			echo "$0: shellcheck ${run_test} failed."
			exit 1
	fi
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

# initialize a semaphore with a given number of tokens
open_sem() {
    mkfifo pipe-$$
    exec 3<>pipe-$$
    rm pipe-$$
    local i=$1
    for((;i>0;i--)); do
        printf %s 000 >&3
    done
}

results_dir=$(dirname $(mktemp -u))/zion-$$
mkdir -p "$results_dir"
echo "Results will be stored in $results_dir"

cleanup() {
  wait -f

  failures=$(find "$results_dir" | grep "\.fail$" | wc -l)
  successes=$(find "$results_dir" | grep "\.pass$" | wc -l)

  echo "$failures failures."
  echo "$successes successes."

  rm -rf "$results_dir"

  if [ $failures -ne 0 ]; then
    exit 1
  else
    exit 0
  fi
}

# run the given command asynchronously and pop/push tokens
run_with_lock(){
  local x
  # this read waits until there is something to read
  read -u 3 -n 3 x && ((0==x)) || cleanup
  runs+=1
  (
    file_log="$results_dir/$4.log"
    mkdir -p "$(dirname "$file_log")"
    touch "$file_log"
    ( "$@"; )
    # push the return code of the command to the semaphore
    ret=$?
    printf '%.3d' $ret >&3
    if [ "$ret" != "0" ]; then
      mv "$file_log"{,.fail}
    else
      mv "$file_log"{,.pass}
    fi
  )&
}

N=8
open_sem $N

for test_file in tests/{*,}/test_*.zion
do
  test_file="${test_file/\/\//\/}"
  if [ "$TEST_FILTER" != "" ]; then
    if ! grep "$TEST_FILTER" <(echo "$test_file"); then
      continue
    fi
  fi
  [[ "$DEBUG_TESTS" != "" ]] && echo "$0: Invoking run-test.sh on ${test_file}..."

  ZION_PATH="${ZION_PATH}:$(dirname "${test_file}")" \
    run_with_lock \
      "${run_test}" \
      "${bin_dir}" \
      "${source_dir}" \
      "${test_file}"
done

cleanup
