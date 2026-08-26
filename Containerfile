FROM ubuntu:26.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG BUILD_JOBS=2
ARG VELOCITA_BENCHMARK_INSTRUMENTATION=OFF
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates cmake g++ git libbsd-dev ninja-build pkg-config uuid-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DAERON_HIDE_DEPRECATION_MESSAGE=ON \
        -DVELOCITA_BENCHMARK_INSTRUMENTATION="${VELOCITA_BENCHMARK_INSTRUMENTATION}" \
    && cmake --build /build --parallel "${BUILD_JOBS}" \
    && ctest --test-dir /build --output-on-failure

FROM ubuntu:26.04

ARG DEBIAN_FRONTEND=noninteractive
ARG VELOCITA_BENCHMARK_INSTRUMENTATION=OFF
LABEL io.velocita.benchmark-instrumentation="${VELOCITA_BENCHMARK_INSTRUMENTATION}"
RUN apt-get update \
    && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && install -d -o 1000 -g 1000 /var/lib/velocita-raft

COPY --from=builder /build/raft-node /usr/local/bin/raft-node
COPY --from=builder /build/raft-client /usr/local/bin/raft-client
COPY --from=builder /build/raft-bench /usr/local/bin/raft-bench
COPY --from=builder /build/raft-bench-merge /usr/local/bin/raft-bench-merge
COPY --from=builder /build/raft-dump /usr/local/bin/raft-dump
COPY --from=builder /build/raft-metrics-exporter /usr/local/bin/raft-metrics-exporter
COPY --from=builder /build/benchmark-metrics-exporter /usr/local/bin/benchmark-metrics-exporter
COPY --from=builder /build/_deps/aeron-src/LICENSE /usr/share/licenses/aeron/LICENSE
COPY --from=builder /build/_deps/willemt_raft-src/LICENSE /usr/share/licenses/willemt-raft/LICENSE

USER 1000:1000
VOLUME ["/var/lib/velocita-raft"]
ENTRYPOINT ["/usr/local/bin/raft-node"]
