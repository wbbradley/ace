FROM debian:latest

RUN \
	apt-get update \
	&& apt-get install -y \
		wget

RUN wget -O - http://llvm.org/apt/llvm-snapshot.gpg.key | apt-key add -
RUN echo "deb http://apt.llvm.org/jessie/ llvm-toolchain-jessie-4.0 main" > /etc/apt/sources.list.d/llvm.list
RUN echo "deb-src http://apt.llvm.org/jessie/ llvm-toolchain-jessie-4.0 main" >> /etc/apt/sources.list.d/llvm.list

RUN \
	apt-get update \
	&& apt-get install -y \
		ccache \
		stow \
        time \
		make \
		ccache \
		clang-4.0 \
		clang-4.0-doc \
		libclang-common-4.0-dev \
		libclang-4.0-dev \
		libclang1-4.0 \
		libclang1-4.0-dbg \
		libllvm-4.0-ocaml-dev \
		libllvm4.0 \
		libllvm4.0-dbg \
		lldb-4.0 \
		llvm-4.0 \
		llvm-4.0-dev \
		llvm-4.0-doc \
		llvm-4.0-examples \
		llvm-4.0-runtime \
		clang-format-4.0 \
		python-clang-4.0 \
		lld-4.0 \
		libc++-dev

COPY . /opt/zion
WORKDIR /opt/zion

CMD bash
