#!/bin/bash
bin_dir=$1
source_dir=$2
test_file=$3

tmpfile=$(mktemp $TMPDIR/zion-test-log.XXXXXX)
echo "If this file exists then $0 did not exit cleanly." > $tmpfile
trap "rm $tmpfile" EXIT

# echo "Logging test output to $tmpfile"

cd ${source_dir}
if ! "${bin_dir}/zion" run ${test_file} >> $tmpfile
then
	echo FAILED: Program returned a non-zero exit code.
	echo ${test_file} output was
	cat $tmpfile
	exit 1
fi

echo ${test_file} output was
cat $tmpfile

if ! grep -E '^PASS$' $tmpfile
then
	echo FAILED: Could not find PASS in output.
	exit 1
fi
echo PASSED!
