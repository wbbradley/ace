#!/bin/sh
set +x

echo "Run docker tests by default, add a parameter to execute that command instead of the tests."

IMAGE=zionlang/zion
VERSION=`cat VERSION`
NAME=zion-build

docker kill $NAME
docker rm $NAME

docker run \
	--rm \
	--name $NAME \
	-it $IMAGE:$VERSION \
	${1:-/opt/zion/docker-test.sh}
