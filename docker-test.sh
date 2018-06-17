#!/bin/bash
set -ex

cd /opt/zion
cmake .
make -j8 zion
./zion test
./expect-tests.sh /opt/zion
