#!/bin/bash
# This script builds the tags for use when doing
# development on the compiler and the standard library.

ztags=/var/tmp/ztags
rm -f "$ztags"

trap 'rm -f "$ztags"' EXIT

ctags -R .
gather-zion-tags() {
  keyword=$1
  code=$2
  allow_indent=$3
  spacePlus="[[:space:]]+"
  if [ -n "$allow_indent" ]; then
    indent="[[:space:]]*"
  else
    indent=""
  fi
  grep -ERn "^$indent$keyword$spacePlus.*" lib/*.zion \
    | sed \
      -Ene \
      's/^([^:]+):([^:]+):('"$indent$keyword$spacePlus"'([a-zA-Z0-9_]+).*)$/\4	\1	\/^\3$\/;"	'"$code"'/p' \
    >> "$ztags"
}

gather-zion-tags "let" "v"
gather-zion-tags "fn" "f" 1
gather-zion-tags "class" "s"
gather-zion-tags "instance" "s"
gather-zion-tags "newtype" "s"
gather-zion-tags "struct" "s"

cat tags >> "$ztags"
sort "$ztags" > tags
rm "$ztags"
