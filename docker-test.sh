#!/bin/bash
set -ex

cd /opt/zion
mkdir -p $HOME/var/zion
export ZION_PATH=/usr/local/share/zion/lib

make test
