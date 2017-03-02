#!/bin/sh
set +e
set +x

echo "Running from "`pwd`
ls -tr -la
cmake --version
ctest --version

cmake --build .
ctest
