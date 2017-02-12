FROM ubuntu:latest
MAINTAINER William Bradley <williambbradley@gmail.com>

RUN apt-get update -y && apt-get install -y \
	wget \
	vim \
	time \
	ccache \
	exuberant-ctags \
	make

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

# RUN apt-get update -y && apt-get install -y \
	# libc++-dev \
	# libc++abi-dev \

WORKDIR /opt/zion
CMD bash
