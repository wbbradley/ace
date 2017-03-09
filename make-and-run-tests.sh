#!/bin/sh
set +e
set +x

cd /opt/zion
ls -tr -la | grep -i cmake
cmake --version
ctest --version

cmake .
ls -tr -la | grep -i cmake

make -j4
ls -tr -la | grep -i cmake

ctest
