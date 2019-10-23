#!/bin/bash

# wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
# echo "deb http://apt.llvm.org/stretch/ llvm-toolchain-stretch-8 main" >> /etc/apt/sources.list
# echo "deb-src http://apt.llvm.org/stretch/ llvm-toolchain-stretch-8 main" >> /etc/apt/sources.list

LLVM_VERSION="9"
(apt-get update -y && apt-get install -y \
	clang-"${LLVM_VERSION}" \
	clang-format-"${LLVM_VERSION}" \
	clang-tools-"${LLVM_VERSION}" \
	libclang-"${LLVM_VERSION}"-dev \
	libclang-common-"${LLVM_VERSION}"-dev \
	libllvm${LLVM_VERSION} \
	lldb-9 \
	llvm-"${LLVM_VERSION}" \
	llvm-"${LLVM_VERSION}"-dev \
	llvm-"${LLVM_VERSION}"-runtime \
	llvm-"${LLVM_VERSION}"-tools) || exit 1

update-alternatives --install /usr/bin/llvm-link llvm-link /usr/bin/llvm-link-"${LLVM_VERSION}" 100 && \
	update-alternatives --install /usr/bin/clang clang /usr/bin/clang-"${LLVM_VERSION}" 100 && \
	update-alternatives --install /usr/bin/llvm-config llvm-config /usr/bin/llvm-config-"${LLVM_VERSION}" 100 && \
	update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-"${LLVM_VERSION}" 100 && \
	update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-"${LLVM_VERSION}" 100 && \
	update-alternatives --install /usr/bin/lldb lldb /usr/bin/lldb-"${LLVM_VERSION}" 100
