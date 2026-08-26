#!/usr/bin/env bash
set -euo pipefail

image="${VELOCITA_BENCHMARK_IMAGE:-localhost/velocita-raft:bench}"
jobs="${VELOCITA_BUILD_JOBS:-2}"

podman build \
    --build-arg "BUILD_JOBS=${jobs}" \
    --build-arg VELOCITA_BENCHMARK_INSTRUMENTATION=ON \
    --tag "$image" \
    --file Containerfile \
    .

instrumentation="$(podman image inspect "$image" \
    --format '{{ index .Labels "io.velocita.benchmark-instrumentation" }}')"
if [[ "$instrumentation" != "ON" ]]; then
    echo "Benchmark image label does not say ON: ${instrumentation}" >&2
    exit 1
fi

podman run --rm --entrypoint /usr/local/bin/raft-bench "$image" --help
echo "Built benchmark-only image: $image"
echo "The production localhost/velocita-raft:v1 image was not changed."
