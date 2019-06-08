FROM debian:stable
MAINTAINER William Bradley <williambbradley@gmail.com>

# Install ripgrep
RUN echo "deb http://ftp.us.debian.org/debian sid main" >> /etc/apt/sources.list
RUN apt-get update -y && apt-get install -y ripgrep
RUN cat /etc/apt/sources.list | grep -v ftp.us.debian.org/debian > /etc/apt/sources.list.new
RUN mv /etc/apt/sources.list.new /etc/apt/sources.list

RUN apt-get update -y && apt-get install -y \
	cmake \
	gnupg2 \
	less \
	libgc-dev \
	libgc-dev \
	make \
	man \
	ninja-build \
	pkg-config \
	shellcheck \
	time \
	vim \
	wget


RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
RUN echo "deb http://apt.llvm.org/stretch/ llvm-toolchain-stretch-7 main" >> /etc/apt/sources.list
RUN echo "deb-src http://apt.llvm.org/stretch/ llvm-toolchain-stretch-7 main" >> /etc/apt/sources.list

RUN apt-get update -y && apt-get install -y \
	clang-7 \
	clang-tools-7 \
	libclang-7-dev \
	libclang-common-7-dev \
	libllvm7 \
	llvm-7 \
	llvm-7-dev \
	llvm-7-runtime \
	llvm-7-tools

RUN update-alternatives --install /usr/bin/llvm-link llvm-link /usr/bin/llvm-link-7 100 && \
	update-alternatives --install /usr/bin/clang clang /usr/bin/clang-7 100 && \
	update-alternatives --install /usr/bin/llvm-config llvm-config /usr/bin/llvm-config-7 100 && \
	update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-7 100

ADD . /opt/zion
WORKDIR /opt/zion

ENV ZION_PATH=/opt/zion:/opt/zion/lib:/opt/zion/tests
ENV ZION_RT=/opt/zion/src
CMD bash
