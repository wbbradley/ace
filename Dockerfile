FROM debian:stretch
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
	make \
	man \
	parallel \
	pkg-config \
	shellcheck \
	time \
	vim \
	libsodium-dev \
	wget


RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
RUN echo "deb http://apt.llvm.org/stretch/ llvm-toolchain-stretch-8 main" >> /etc/apt/sources.list
RUN echo "deb-src http://apt.llvm.org/stretch/ llvm-toolchain-stretch-8 main" >> /etc/apt/sources.list

RUN apt-get update -y && apt-get install -y \
	clang-8 \
	clang-tools-8 \
	libclang-8-dev \
	libclang-common-8-dev \
	libllvm8 \
	llvm-8 \
	llvm-8-dev \
	llvm-8-runtime \
	llvm-8-tools

RUN update-alternatives --install /usr/bin/llvm-link llvm-link /usr/bin/llvm-link-8 100 && \
	update-alternatives --install /usr/bin/clang clang /usr/bin/clang-8 100 && \
	update-alternatives --install /usr/bin/llvm-config llvm-config /usr/bin/llvm-config-8 100 && \
	update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-8 100

RUN echo "nmap ; :" >> /root/.vimrc
RUN echo "imap jk <Esc>" >> /root/.vimrc
RUN echo "imap kj <Esc>" >> /root/.vimrc

ADD . /opt/zion
WORKDIR /opt/zion

RUN make DEBUG=

CMD bash
