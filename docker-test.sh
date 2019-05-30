#!/bin/bash
set -ex

cd /opt/zion
mkdir -p /var/zion
export ZION_PATH=/opt/zion:/opt/zion/lib:/opt/zion/tests

make BUILD_DIR=/var/zion test

./zion test
