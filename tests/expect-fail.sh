#!/bin/sh
bin_dir=$1
source_dir=$2
test_file=$3

if ! "${bin_dir}/zion" run ${source_dir}/${test_file}
then
	echo "${test_file} did not pass, which was expected."
	echo PASS
	exit 0
else
	echo "${test_file} passed, which was unexpected."
	echo FAIL
	exit 1
fi
