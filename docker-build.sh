#!/bin/bash
IMAGE="ciderlang/cider"
VERSION="$(cat VERSION)"

docker build -t "${IMAGE}:${VERSION}" .
docker tag "${IMAGE}:${VERSION}" "${IMAGE}:latest"
