#!/bin/bash

wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
echo "deb http://apt.llvm.org/stretch/ llvm-toolchain-stretch-8 main" >> /etc/apt/sources.list
echo "deb-src http://apt.llvm.org/stretch/ llvm-toolchain-stretch-8 main" >> /etc/apt/sources.list

apt-get update -y && apt-get install -y \
	clang-8 \
	clang-format-8 \
	clang-tools-8 \
	libclang-8-dev \
	libclang-common-8-dev \
	libllvm8 \
	llvm-8 \
	llvm-8-dev \
	llvm-8-runtime \
	llvm-8-tools

update-alternatives --install /usr/bin/llvm-link llvm-link /usr/bin/llvm-link-8 100 && \
	update-alternatives --install /usr/bin/clang clang /usr/bin/clang-8 100 && \
	update-alternatives --install /usr/bin/llvm-config llvm-config /usr/bin/llvm-config-8 100 && \
	update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-8 100 && \
	update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-8 100
