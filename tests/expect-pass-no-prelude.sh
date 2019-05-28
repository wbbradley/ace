#!/bin/bash
source_dir=$2
NO_PRELUDE=1 ${source_dir}/tests/expect-pass.sh $*
