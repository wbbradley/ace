#!/bin/bash
set -ex

cd /opt/zion
make DEBUG= test
