#!/bin/sh
set +e

cmake --build .
ctest
