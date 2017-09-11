#!/bin/sh

echo "MAINTAINER William Bradley <williambbradley@gmail.com>"

set -ex
apt-get update -y
apt-get install -y \
	wget \
	vim \
	time \
	ccache \
	exuberant-ctags \
	make \
	gdb \
	cmake \
	htop \
	build-essential \
	libedit-dev

wget -O - http://llvm.org/apt/llvm-snapshot.gpg.key | apt-key add -

cp llvm.list /etc/apt/sources.list.d/llvm.list
		
apt-get install -y clang-4.0 lldb-4.0 libstdc++6 libz-dev

# Make sure llvm-link and clang are linked to be available without version numbers
update-alternatives --install /usr/bin/llvm-link llvm-link /usr/bin/llvm-link-4.0 100
update-alternatives --install /usr/bin/clang clang /usr/bin/clang-4.0 100
update-alternatives --install /usr/bin/lldb lldb /usr/bin/lldb-4.0 100
