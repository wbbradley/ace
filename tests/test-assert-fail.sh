#!/bin/sh
bin_dir=$1
source_dir=$2

"${bin_dir}/ace" run ${source_dir}/tests/test_assert_fail | grep -E "tests/test_assert_fail.ace:2:17.* assertion failed: .*std.False.* is False"
ret=$?
exit ${ret}
