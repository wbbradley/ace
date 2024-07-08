#!/bin/bash
set -ex

cd /opt/cider
make DEBUG= test
