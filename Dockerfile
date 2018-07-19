FROM ubuntu:xenial
MAINTAINER William Bradley <williambbradley@gmail.com>

RUN apt-get update -y && apt-get install -y \
	man \
	wget \
	vim \
	time \
	git \
	autogen \
	libtool \
	ccache \
	exuberant-ctags \
	autoconf \
	make \
	gdb \
	cmake \
	libedit-dev \
	build-essential \
	libsodium-dev \
	clang-5.0 \
	lldb-5.0 \
	lld-5.0 \
	libc++-dev \
	libc++abi1 \
	libc++abi-dev \
	libc++-helpers \
	libstdc++-4.8-dev \
	libz-dev \
	libncurses5-dev

# Make sure llvm-link and clang are linked to be available without version numbers
RUN update-alternatives --install /usr/bin/llvm-link llvm-link /usr/bin/llvm-link-5.0 100 \
	&& update-alternatives --install /usr/bin/clang clang /usr/bin/clang-5.0 100 \
	&& update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-5.0 100 \
	&& update-alternatives --install /usr/bin/lldb lldb /usr/bin/lldb-5.0 100 \
	&& update-alternatives --install /usr/bin/llvm-config llvm-config /usr/bin/llvm-config-5.0 100

RUN \
	mkdir -p /tmp/jansson && \
	cd /tmp/jansson && \
	git clone https://github.com/akheron/jansson /tmp/jansson && \
	autoreconf -i && \
	./configure && \
	make && \
	make install

ADD . /opt/zion
WORKDIR /opt/zion
CMD bash
