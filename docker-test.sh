#!/bin/sh

IMAGE=zionlang/zion
VERSION=0.1

docker build -t $IMAGE:$VERSION .
docker tag $IMAGE:$VERSION $IMAGE:latest

docker kill zion-build
docker rm zion-build
docker run \
	--rm \
	--name zion-build \
	-e LLVM_LINK_BIN=$LLVM_LINK_BIN \
	-e CLANG_BIN=$CLANG_BIN \
	-e DEBUG=7 \
	-v `pwd`:/opt/zion \
	--privileged \
	-it $IMAGE:$VERSION \
	./build-and-run-tests.sh
