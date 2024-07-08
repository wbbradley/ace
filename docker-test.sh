#!/bin/bash
set -ex

cd /opt/ace
make DEBUG= test
