#!/bin/sh
# Build a release image from the pinned toolchain (docs/TOOLCHAIN.md).
#
#   deploy/v2/build-image.sh voter  <TAG>   -> localhost/velocita-v2:<TAG>
#   deploy/v2/build-image.sh shadow <TAG>   -> localhost/velocita-v2-shadow:<TAG>
#
# Run from the shipped source tree (it reads SOURCE_COMMIT, written by the ship
# step; falls back to git). Refuses (exit 64) unless the local velocita-dev image
# is exactly the toolchain recorded for this architecture in TOOLCHAIN.env.
set -eu
[ $# -eq 2 ] || { echo "usage: $0 voter|shadow TAG" >&2; exit 64; }
role=$1; tag=$2
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
    echo "build-image: load or build the recorded toolchain (docs/TOOLCHAIN.md) before building a release" >&2
    exit 64
fi

if [ -f SOURCE_COMMIT ]; then commit=$(cat SOURCE_COMMIT); else commit=$(git rev-parse HEAD); fi
jobs=${BUILD_JOBS:-4}
[ "$arch" = arm64 ] && jobs=${BUILD_JOBS:-3}
case "$role" in
    voter)  target=""; image=localhost/velocita-v2:$tag ;;
    shadow) target="--target shadow"; image=localhost/velocita-v2-shadow:$tag ;;
    *) echo "usage: $0 voter|shadow TAG" >&2; exit 64 ;;
esac

mkdir -p "$HOME/.cache/velocita-ccache"
echo "build-image: $image from $commit with toolchain $actual ($arch), $jobs jobs"
# shellcheck disable=SC2086
podman build -v "$HOME/.cache/velocita-ccache:/ccache" \
    --build-arg BUILD_JOBS="$jobs" --build-arg DEV_IMAGE="$DEV_IMAGE" \
    --build-arg SOURCE_COMMIT="$commit" --build-arg TOOLCHAIN_ID="$actual" \
    $target -f deploy/v2/Containerfile -t "$image" .
echo "build-image: built $image = $(podman image inspect --format '{{.Id}}' "$image")"
