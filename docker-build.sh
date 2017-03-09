#!/bin/sh
IMAGE=zionlang/zion
VERSION=`cat VERSION`

docker build -t $IMAGE:$VERSION .
docker tag $IMAGE:$VERSION $IMAGE:latest
