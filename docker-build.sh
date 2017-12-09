#!/bin/sh
IMAGE=zionlang/zion
VERSION=`cat VERSION`

echo Number of cores is `nproc`
docker build -t $IMAGE:$VERSION .
docker tag $IMAGE:$VERSION $IMAGE:latest
