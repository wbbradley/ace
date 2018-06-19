#!/bin/bash
set -ex

cd /opt/zion
cmake .
make -j8

export ZION_PATH=/opt/zion:/opt/zion/lib:/opt/zion/tests
./zion test
./expect-tests.sh /opt/zion
