#!/bin/sh
bin_dir=$1
source_dir=$2
test_file=$3

! "${bin_dir}/zion" run ${source_dir}/${test_file}
ret=$?
echo "Program returned ${ret}"
exit ${ret}
