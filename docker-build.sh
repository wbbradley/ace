#!/bin/bash
IMAGE="acelang/ace"
VERSION="$(cat VERSION)"

docker build -t "${IMAGE}:${VERSION}" .
docker tag "${IMAGE}:${VERSION}" "${IMAGE}:latest"
