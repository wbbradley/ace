#!/bin/bash
set +x

./docker-build.sh

# Running docker tests by default, add a parameter to execute that command
# instead of the tests.

IMAGE="wbbradley/ace"
VERSION="$(cat VERSION)"
NAME="ace-build"

docker kill "${NAME}" 2>/dev/null
docker rm "${NAME}" 2>/dev/null

docker run \
	--rm \
	--name "${NAME}" \
	-v "$(pwd)/tests:/opt/ace/tests" \
	-v "$(pwd)/lib:/opt/ace/lib" \
	-t "${IMAGE}:${VERSION}" \
	"${1:-/opt/ace/docker-test.sh}"
