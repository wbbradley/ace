#!/usr/bin/env bash

bin_dir=$1
src_dir=$3

export PATH="$bin_dir:$PATH"

cd "$src_dir" || {
  echo "Could not cd to src dir $src_dir/.."
  exit 1
}

echo "Running tests in $(pwd)..."
cider help
cider test
