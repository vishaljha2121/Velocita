# Velocita native Raft + Aeron v1

An educational, native C++20 Raft service for a Raspberry Pi 5. It supports a
single voter or three logical voters connected through Aeron UDP. It uses:

- `willemt/raft` for the Raft protocol engine;
- Aeron's C media driver and C++ client wrapper for UDP messaging;
- a versioned, checksummed append-only WAL and atomically replaced metadata file;
- a deterministic counter state machine with replicated request deduplication; and
- Podman for an ARM64-compatible deployment.

Release scope, validated strengths, measured failures, and the planned V1.1
acceptance gates are maintained in [`CHANGELOG.md`](CHANGELOG.md).
The repository is intentionally maintained with local-only Git; branching,
tagging, safe rollback, and offline backup are documented in
[`docs/LOCAL_GIT_WORKFLOW.md`](docs/LOCAL_GIT_WORKFLOW.md).

There is no Java runtime in the build or final image. The code in this repository is C++20; the two pinned upstream dependencies contain their native C/C++ implementations.

## Architecture and ownership

```text
raft-client
    |  Aeron UDP: versioned Command
    v
embedded C media driver -> C++ owner loop -> willemt/raft
                                           | append + fsync
                                           v
                                      versioned WAL
                                           | commit
                                           v
                                     deterministic FSM
    ^                                      |
    |  Aeron UDP: versioned Response       |
    +--------------------------------------+
```

The split follows the Velocitas book:

- Chapter 5, “Raft in Practice”: command identity is replicated, durability callbacks are protocol boundaries, and one thread owns Raft.
- Chapter 6, “Aeron Transport”: this is Raft **over** Aeron, not Aeron Cluster; transport and consensus have different ownership.
- Chapter 13, “The Cluster Runtime”: version the wire/log formats, own storage outside the C engine, expose term/index/apply evidence, and provide an offline dump tool.
- Chapter 54, “Homelab Hardware and Network”: one authority node per physical server; multiple containers on one Pi are not high availability.

## What v1 proves—and what it does not

It proves the native toolchain, single-voter election, durable append, commit, deterministic apply, restart replay, request deduplication, Aeron UDP ingress/response, and Podman packaging.

Three voters on one Pi tolerate one **process** stopping, but are not machine-level
high availability: power, storage, kernel, and network-interface failures still
affect every voter. Aeron carries client traffic plus versioned `RequestVote` and
`AppendEntries` traffic. V1 still has no snapshot/compaction, so its WAL grows
indefinitely. A torn final WAL record fails startup safely and currently requires
operator repair tooling.

## Build and test on macOS or Linux

Prerequisites are CMake 3.30+, a C/C++ compiler, Git, and Ninja or Make.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DAERON_HIDE_DEPRECATION_MESSAGE=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Dependencies are fetched from their official repositories and pinned to exact commits in `CMakeLists.txt`.

## Build on the Raspberry Pi with Podman

SSH to the Pi, copy or clone this directory, then:

```bash
cd aeron-raft-pi
podman build --tag localhost/velocita-raft:v0.1 -f Containerfile .
podman volume create velocita-raft-data
```

The build runs the core tests inside the ARM64 image. The final image contains only the native executables and the runtime C++ library.

## Run the node

```bash
podman run --detach \
  --name velocita-raft \
  --network host \
  --shm-size 256m \
  --volume velocita-raft-data:/var/lib/velocita-raft:U \
  localhost/velocita-raft:v0.1
```

Why these flags:

- `--network host` preserves Aeron's real UDP endpoint semantics; container NAT would add an address-mapping failure surface.
- `--shm-size 256m` gives Aeron room for its command-and-control and term buffers.
- the named volume keeps Raft decisions across container replacement.
- `:U` initializes ownership of this dedicated volume for the non-root UID inside the image.

Watch leadership and indexes:

```bash
podman logs --follow velocita-raft
```

Healthy output converges on `role=leader` with `current_index >= commit_index >= applied_index`. In this synchronous v1, committed and applied should normally be equal.

## Send commands on the Pi

The easiest first test runs a disposable native client container on the same host network:

```bash
podman run --rm --network host --shm-size 128m \
  --entrypoint /usr/local/bin/raft-client \
  localhost/velocita-raft:v0.1 increment 5

podman run --rm --network host --shm-size 128m \
  --entrypoint /usr/local/bin/raft-client \
  localhost/velocita-raft:v0.1 get
```

Expected responses include `status=ok value=5` and increasing log indexes. Reads are replicated in v1. That is deliberately simple and linearizable, although a production system would use a ReadIndex/lease strategy to avoid filling the log with reads.

To prove idempotency, reuse a request ID:

```bash
podman run --rm --network host --shm-size 128m \
  --entrypoint /usr/local/bin/raft-client \
  localhost/velocita-raft:v0.1 increment 5 --request-id 7001

podman run --rm --network host --shm-size 128m \
  --entrypoint /usr/local/bin/raft-client \
  localhost/velocita-raft:v0.1 increment 5 --request-id 7001
```

Both calls return the same value; the second does not append or apply another increment.

## Prove restart recovery

```bash
podman restart velocita-raft
podman logs --since 30s velocita-raft

podman run --rm --network host --shm-size 128m \
  --entrypoint /usr/local/bin/raft-client \
  localhost/velocita-raft:v0.1 get
```

The value must match the value before restart. If it does not, stop: a green process with lost committed state is not a healthy service.

## Inspect the durable log offline

Stop the writer before using the v1 dump tool:

```bash
podman stop velocita-raft
podman run --rm \
  --volume velocita-raft-data:/var/lib/velocita-raft \
  --entrypoint /usr/local/bin/raft-dump \
  localhost/velocita-raft:v0.1 /var/lib/velocita-raft
podman start velocita-raft
```

The dump shows term, vote, commit index, and every versioned command. This is recovery evidence, not merely debugging output.

## Run three logical voters on one Pi

For the command-by-command Linux and production learning path, use
[`docs/PI_OPERATOR_RUNBOOK.md`](docs/PI_OPERATOR_RUNBOOK.md). It explains the
host checks, rootless Podman model, storage, ports, health invariants, debugging
order, and failure drills behind the commands below.

Build the cluster image and start three isolated nodes:

```bash
podman build --tag localhost/velocita-raft:v1 -f Containerfile .
./deploy/start-pi-cluster.sh
sleep 5
podman ps --filter name=velocita-raft
```

Each node has a different node ID, client port, peer port, Aeron media driver,
and named WAL volume. All three use the host network so their loopback UDP
endpoints preserve Aeron's native addressing.

Check the five-second status lines:

```bash
for node in 1 2 3; do
  podman logs --tail 20 "velocita-raft-$node"
done
```

Exactly one node should report `role=leader`; two should report `role=follower`,
and all three should agree on the term. Send a command without manually finding
the leader:

```bash
./deploy/cluster-client.sh increment 5
./deploy/cluster-client.sh get
```

The helper tries each client endpoint with one stable request ID. Followers
return `not_leader`; the leader accepts the proposal. Reusing the same ID makes
those retries safe.

Stop the processes while preserving all three WAL volumes:

```bash
./deploy/stop-pi-cluster.sh
```

## Manage the cluster with rootless systemd and Quadlet

The manual `podman run` scripts are the learning/bootstrap path. For boot-time
and crash lifecycle management, install the rootless Quadlet definitions under
`deploy/quadlet/` and the grouping target under `deploy/systemd-user/`. Follow
the migration and verification procedure in
[`docs/PI_OPERATOR_RUNBOOK.md`](docs/PI_OPERATOR_RUNBOOK.md).

## Book-aligned shared-memory observability

Each voter publishes a versioned, fixed-layout metrics page in tmpfs. Hot-path
updates are cache-line-separated, lock-free 64-bit atomics with relaxed memory
ordering; status gauges use a bounded seqlock snapshot. There is no formatting,
allocation, filesystem write, or HTTP work in the Raft owner loop.

`raft-metrics-exporter` is a separate process that maps the three pages
read-only, validates magic/version/size/node identity, formats Prometheus text,
and serves it on `127.0.0.1:9200`. Host, process, disk, temperature, and the
full Aeron CnC catalogue remain external collection concerns.

## Local Prometheus and Grafana

The host-level layer uses Ubuntu's native `prometheus-node-exporter` because it
needs an honest view of the Pi's `/proc`, `/sys`, filesystems, disks, network,
and thermal zones. Keep it bound to `127.0.0.1:9100`. Ubuntu's native
`prometheus-process-exporter` reads `/proc` externally for per-voter CPU,
resident memory, faults, context switches, and file descriptors on
`127.0.0.1:9256`; its repository-owned matcher groups by stable node ID rather
than PID.

`deploy/install-observability.sh` installs a separate rootless Quadlet target
containing Prometheus and Grafana. Prometheus is pinned to the 3.13.2 LTS
release, keeps at most seven days and 1 GiB of TSDB blocks, and applies explicit
sample/label limits. Grafana is provisioned from repository-controlled files
with a Prometheus data source and the initial cluster/host health dashboard.
Both web endpoints are loopback-only.

The dashboard includes live state timelines for voter liveness and leadership.

The separate native open-loop benchmark driver, repeated rate-sweep runner,
exact artifact format, and Grafana benchmark dashboard are documented in
[`docs/BENCHMARKING.md`](docs/BENCHMARKING.md). Benchmark-only stage timestamps
are compile-time disabled in the production image.
Liveness means that the expected metrics page is valid, its writer is active,
and the Raft owner loop has ticked within two seconds. It does not claim quorum
or committed progress; those remain separate panels and alerts.

Run all bounded online failure drills and retain their evidence with:

```bash
./deploy/run-failure-drills.sh --yes
```

The runner restores supervised services through an exit trap and writes its
timestamped log, summary, final metrics, alerts, and unit status under
`~/.local/share/velocita/failure-drills/`. Physical reboot validation remains
a separate drill because an on-host script cannot observe its own host outage.
The first complete V1 acceptance result and its interpretation are recorded in
[`docs/V1_FAILURE_DRILL_RESULTS.md`](docs/V1_FAILURE_DRILL_RESULTS.md).

The observability target is deliberately independent of the Raft target:
stopping dashboards must not stop consensus, and a broken dashboard is not
proof that the cluster is broken. Follow the installation, SSH tunnel, alert
validation, and triage procedure in
[`docs/PI_OPERATOR_RUNBOOK.md`](docs/PI_OPERATOR_RUNBOOK.md).

## Next milestone: three physical voters

The next safe increment is:

1. Give each Pi a stable node ID and LAN endpoint; keep Tailscale on the management plane, not the Aeron data plane.
2. Replace the loopback peer endpoints with the three LAN addresses.
3. Add snapshot schema, snapshot transfer, WAL compaction, and a dedup-table snapshot.
4. Test leader loss, follower loss, packet loss, restart, and state digests across all three physical machines.

Only after three independent voters and failure tests should this be described as highly available.
