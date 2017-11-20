#!/bin/sh
set -e

function require() {
	$@ || (echo "You must have $1 installed." && exit 1)
}

require cmake --version
require make --version
require git --version

# Specify which version of LLVM you'd like to use (corresponds to branch names in
# https://github.com/llvm-mirror/... repos.)
RELEASE=release_40

# We need to make sure you have the LLVM sources.
mkdir -p $HOME/src
LLVM_ROOT=$HOME/src/llvm

# The built LLVM will be installed here:
INSTALL_DIR=$HOME/opt/llvm/$RELEASE

function enlist() {
	# Try to make sure we have this LLVM repo, synced to the right branch.
	if [ ! -d $2 ]; then
		git clone git@github.com:llvm-mirror/$1 $2
	else
		echo $2 already exists, skipping cloning $1 into it...
	fi

	cd $2
	git checkout $RELEASE
}

# Get the sources
enlist llvm $LLVM_ROOT
enlist clang $LLVM_ROOT/tools/clang
enlist libcxx $LLVM_ROOT/projects/libcxx
enlist libcxxabi $LLVM_ROOT/projects/libcxxabi

# Set up the installation dir
mkdir -p $INSTALL_DIR

function build() {
	# Run the LLVM build and install
	BUILD_DIR=$HOME/var/tmp/llvm/$1
	mkdir -p $BUILD_DIR
	cd $BUILD_DIR

	time cmake \
		-DCMAKE_INSTALL_PREFIX=$INSTALL_DIR/$1 \
		-DLLVM_ENABLE_RTTI=On \
		-DCMAKE_BUILD_TYPE=$1 \
		-DLLVM_ENABLE_ASSERTIONS=$(if [ $1 = Debug ]; then echo On; else echo Off; fi) \
		$LLVM_ROOT

	time make -j8
	time make install
}

build Debug
build MinSizeRel
