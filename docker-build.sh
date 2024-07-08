#!/bin/bash
IMAGE="wbbradley/ace"
VERSION="$(cat VERSION)"

docker build -t "${IMAGE}:${VERSION}" .
docker tag "${IMAGE}:${VERSION}" "${IMAGE}:latest"
