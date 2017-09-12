#!/bin/sh
set -ex

cd /opt/zion
cmake --version
ctest --version

cmake .
make VERBOSE=1 -j8

./zionc test
./zionc run test_hello_world
