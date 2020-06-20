#!/bin/bash
set +x

./docker-build.sh

# Running docker tests by default, add a parameter to execute that command
# instead of the tests.

IMAGE="zionlang/zion"
VERSION="$(cat VERSION)"
NAME="zion-build"

docker kill "${NAME}" 2>/dev/null
docker rm "${NAME}" 2>/dev/null

docker run \
	--rm \
	--name "${NAME}" \
	-v "$(pwd)/tests:/opt/zion/tests" \
	-v "$(pwd)/lib:/opt/zion/lib" \
	-it "${IMAGE}:${VERSION}" \
	"${1:-/opt/zion/docker-test.sh}"
