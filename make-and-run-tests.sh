#!/bin/sh
set -ex

cd /opt/zion

make VERBOSE=1 -j8

./zionc test
./zionc run test_hello_world
