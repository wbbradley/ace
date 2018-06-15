#!/bin/bash
set -e

if [ $# -eq 1 ]; then
    PATH=$1:$PATH
fi

for f in tests/test_*.zion
do
    zion run expect -p $f
done
exit 0
