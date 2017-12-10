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
	libbsd-dev \
	build-essential

RUN \
	wget -O - http://llvm.org/apt/llvm-snapshot.gpg.key | apt-key add -

RUN \
	echo "deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial main" > /etc/apt/sources.list.d/llvm.list

RUN \
	echo "deb-src http://apt.llvm.org/xenial/ llvm-toolchain-xenial main" >> /etc/apt/sources.list.d/llvm.list

RUN \
	echo "deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-4.0 main" >> /etc/apt/sources.list.d/llvm.list

RUN \
	echo "deb-src http://apt.llvm.org/xenial/ llvm-toolchain-xenial-4.0 main" >> /etc/apt/sources.list.d/llvm.list
		
RUN apt-get update -y && apt-get install -y \
	clang-4.0 \
	lldb-4.0 \
	libstdc++6 \
	libz-dev

# Make sure llvm-link and clang are linked to be available without version numbers
RUN update-alternatives --install /usr/bin/llvm-link llvm-link /usr/bin/llvm-link-4.0 100 \
	&& update-alternatives --install /usr/bin/clang clang /usr/bin/clang-4.0 100 \
	&& update-alternatives --install /usr/bin/lldb lldb /usr/bin/lldb-4.0 100

ENV ARC4RANDOM_LIB bsd
ADD . /opt/zion
WORKDIR /opt/zion
CMD bash
