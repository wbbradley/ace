#!/bin/bash
set -e
for f in tests/test_*.zion
do
    python expect.py -p $f
done

