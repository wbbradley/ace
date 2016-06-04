FROM ubuntu:14.04

RUN \
	apt-get update \
	&& apt-get install -y \
		ccache \
		stow \
		clang-3.6 \
		llvm-3.6-tools \
		llvm-3.6 \
		lldb-3.6-dev \
		llvm-3.6-runtime \
		libllvm3.6-dbg \
		libc++1 \
		libc++-dev \
		libstdc++-4.8-dev \
		python \
		python-dev \
		python-distribute \
		python-pip

COPY . /usr/local/zion
