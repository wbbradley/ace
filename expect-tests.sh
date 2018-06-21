#!/bin/bash
set -e

if [ $# -eq 1 ]; then
    PATH=$1:$PATH
fi

zion bin expect
for f in tests/test_*.zion
do
    ./expect.zx -p $f
done
exit 0
