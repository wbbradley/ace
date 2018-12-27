FROM ubuntu:xenial
MAINTAINER William Bradley <williambbradley@gmail.com>

RUN apt-get update -y && apt-get install -y \
	make \
	cmake \
	build-essential \
	libgc-dev

ADD . /opt/zion
WORKDIR /opt/zion
CMD bash
