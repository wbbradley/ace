#!/bin/bash

LLVM_VERSION="11"
if [ "$(uname -s)" = "Darwin" ]; then
  brew install \
    "llvm@$LLVM_VERSION" \
    bdw-gc \
    pkg-config \
    libsodium
else
  apt update -y
  apt install -y \
    apt-utils \
    cmake \
    shellcheck \
    gnupg2 \
    libgc-dev \
    libsodium-dev \
    lsb-release \
    make \
    pkg-config \
    software-properties-common \
    wget

  # Use the ordained script from https://apt.llvm.org/
  wget https://apt.llvm.org/llvm.sh
  chmod +x llvm.sh
  ./llvm.sh "$LLVM_VERSION"

  # Ensure the rest of the LLVM dependencies are available and mapped to sane
  # names.
  (apt-get install -y \
    clang-format-"${LLVM_VERSION}" \
    clang-tools-"${LLVM_VERSION}" \
    libclang-"${LLVM_VERSION}"-dev \
    libclang-common-"${LLVM_VERSION}"-dev \
    libllvm${LLVM_VERSION} \
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
fi
