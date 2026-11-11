#!/bin/sh
set -eu
[ $# -eq 1 ] || { echo "usage: $0 TAG" >&2; exit 64; }
tag=$1
cd "$(dirname "$0")/../.."
. deploy/v2/TOOLCHAIN.env

arch=$(podman info --format '{{.Host.Arch}}')
case "$arch" in
    amd64) expected=$DEV_ID_amd64 ;;
    arm64) expected=$DEV_ID_arm64 ;;
    *) echo "build-image: no toolchain is recorded for architecture $arch" >&2; exit 64 ;;
esac
actual=$(podman image inspect --format '{{.Id}}' "$DEV_IMAGE" 2>/dev/null || true)
if [ -z "$expected" ] || [ "$actual" != "$expected" ]; then
    echo "build-image: $DEV_IMAGE is '${actual:-absent}', not the recorded $arch toolchain '${expected:-unset}'" >&2
    exit 64
fi
openssl=$(podman run --rm --network none "$DEV_IMAGE" dpkg-query -W -f='${Version}' libssl3t64)
if [ -f SOURCE_COMMIT ]; then commit=$(cat SOURCE_COMMIT); else commit=$(git rev-parse HEAD); fi
jobs=${BUILD_JOBS:-3}
[ "$arch" = arm64 ] && jobs=${BUILD_JOBS:-2}
image=localhost/velocita-tape:$tag

mkdir -p "$HOME/.cache/velocita-ccache"
echo "build-image: $image from $commit with toolchain $actual ($arch), OpenSSL $openssl, $jobs jobs"
podman build -v "$HOME/.cache/velocita-ccache:/ccache" \
    --build-arg BUILD_JOBS="$jobs" --build-arg DEV_IMAGE="$DEV_IMAGE" --build-arg OPENSSL_VERSION="$openssl" \
    --build-arg SOURCE_COMMIT="$commit" --build-arg TOOLCHAIN_ID="$actual" \
    -f deploy/tape/Containerfile -t "$image" .
echo "build-image: built $image = $(podman image inspect --format '{{.Id}}' "$image")"
