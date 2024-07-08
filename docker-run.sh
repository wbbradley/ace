#!/bin/bash
set +x

./docker-build.sh

# Running docker tests by default, add a parameter to execute that command
# instead of the tests.

IMAGE="ciderlang/cider"
VERSION="$(cat VERSION)"
NAME="cider-build"

docker kill "${NAME}" 2>/dev/null
docker rm "${NAME}" 2>/dev/null

docker run \
	--rm \
	--name "${NAME}" \
	-v "$(pwd)/tests:/opt/cider/tests" \
	-v "$(pwd)/lib:/opt/cider/lib" \
	-t "${IMAGE}:${VERSION}" \
	"${1:-/opt/cider/docker-test.sh}"
