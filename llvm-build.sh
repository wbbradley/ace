#!/bin/bash
set -e

function require() {
	command -v "$1" || (echo "You must have $1 installed." && exit 1)
}

require cmake
require make
require git
require swig

# Specify which version of LLVM you'd like to use (corresponds to branch names in
# https://github.com/llvm-mirror/... repos.)
RELEASE=release_80
SRCDIR=$HOME/src

# We need to make sure you have the LLVM sources.
mkdir -p "$SRCDIR"
LLVM_ROOT="$SRCDIR"/llvm

# The built LLVM will be installed here:
INSTALL_DIR="$HOME/opt/llvm/$RELEASE"

function get-sources-for() {
	# Try to make sure we have this LLVM repo, synced to the right branch.
	if [ ! -d "$2" ]; then
		git clone "git@github.com:llvm-mirror/$1" "$2"
	else
		echo "$2 already exists, skipping cloning $1 into it..."
		(cd "$2" && git fetch)
	fi

	cd "$2"
	git checkout -B "$RELEASE" "origin/$RELEASE"
}

# Get the sources
get-sources-for llvm      "$LLVM_ROOT"
get-sources-for lldb      "$LLVM_ROOT/tools/lldb"
get-sources-for clang     "$LLVM_ROOT/tools/clang"
get-sources-for libcxx    "$LLVM_ROOT/projects/libcxx"
get-sources-for libcxxabi "$LLVM_ROOT/projects/libcxxabi"

# Set up the installation dir
mkdir -p "$INSTALL_DIR"

# TODO: if you want to codesign debugserver, follow instructions here, and remove DLLDB_CODESIGN_IDENTITY from below.
# http://llvm.org/svn/llvm-project/lldb/trunk/docs/code-signing.txt

function build() {
	mkdir -p "$INSTALL_DIR/$1"
	chmod -R u+w "$INSTALL_DIR/$1"

	# Run the LLVM build and install
	BUILD_DIR="$HOME/var/tmp/llvm/$1"
	rm -rf "$BUILD_DIR"
	mkdir -p "$BUILD_DIR"
	cd "$BUILD_DIR"

	time cmake \
		-DLLDB_CODESIGN_IDENTITY='' \
		-DCMAKE_BUILD_TYPE="$1" \
		-DCMAKE_INSTALL_PREFIX="$INSTALL_DIR/$1" \
		-DLLVM_BUILD_LLVM_DYLIB=On \
		-DLLVM_DYLIB_EXPORT_ALL=On \
		-DLLVM_ENABLE_ASSERTIONS="$(if [ "$1" = Debug ]; then echo On; else echo Off; fi)" \
		-DLLVM_ENABLE_RTTI=On \
		"$LLVM_ROOT"
	time make -j8
	time make install

	# Just for good measure, let's mark this entire directory as unwriteable to avoid
	# accidental-edit headaches later.
	chmod -R a-w "$INSTALL_DIR/$1"
}

build Debug
# build MinSizeRel
