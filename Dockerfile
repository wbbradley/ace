FROM ubuntu:latest
MAINTAINER William Bradley <williambbradley@gmail.com>

RUN apt-get update -y && apt-get install -y \
	wget \
	vim \
	time \
	ccache \
	exuberant-ctags \
	make \
	gdb \
	cmake \
	libedit-dev \
	build-essential

RUN \
	wget -O - http://llvm.org/apt/llvm-snapshot.gpg.key | apt-key add -

RUN \
	echo "deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial main" > /etc/apt/sources.list.d/llvm.list

RUN \
	echo "deb-src http://apt.llvm.org/xenial/ llvm-toolchain-xenial main" >> /etc/apt/sources.list.d/llvm.list

RUN \
	echo "deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-3.9 main" >> /etc/apt/sources.list.d/llvm.list

RUN \
	echo "deb-src http://apt.llvm.org/xenial/ llvm-toolchain-xenial-3.9 main" >> /etc/apt/sources.list.d/llvm.list
		
RUN apt-get update -y && apt-get install -y \
	clang-3.9 \
	lldb-3.9 \
	libstdc++6 \
	libz-dev

# Make sure llvm-link and clang are linked to be available without version numbers
RUN update-alternatives --install /usr/bin/llvm-link llvm-link /usr/bin/llvm-link-3.9 100 \
	&& update-alternatives --install /usr/bin/clang clang /usr/bin/clang-3.9 100 \
	&& update-alternatives --install /usr/bin/lldb lldb /usr/bin/lldb-3.9 100

ADD . /opt/zion
CMD bash
