# Velocita Raft benchmark protocol

This benchmark is an experiment harness, not a faster version of the client and
not a production service. It establishes how latency changes as offered load
approaches and exceeds the capacity of the three-voter Raft cluster.

## The latency contract

The primary result is `scheduled_to_response`:

```text
intended send ── scheduling delay ── actual Aeron offer ── Raft ── matching response
      │                                  │                              │
      └──────── scheduled_to_offer ──────┘                              │
                                         └──── offer_to_response ───────┘
      └──────────────── scheduled_to_response ──────────────────────────┘
```

The intended send time advances from an absolute monotonic clock at the
configured rate. It does not wait for an earlier response. This open-loop model
prevents coordinated omission: a stalled cluster creates many late observations
instead of one slow observation followed by an artificial pause in offered
load.

Every scheduled request becomes either:

- an exact successful sample in `steady-samples.csv` or
  `warmup-samples.csv`; or
- an explicit `timeout`, `capacity_rejected`, `invalid_request`, or
  `internal_error` row in `failures.csv`.

Aeron offer results and `not_leader` retries are also counted. Leader retries
reuse the same request ID, which preserves the existing idempotency contract.

## Production and benchmark builds

`VELOCITA_BENCHMARK_INSTRUMENTATION` defaults to `OFF`. The production image
therefore contains no benchmark trace table, timestamp calls, or expanded
response. `deploy/build-benchmark-image.sh` creates a separate
`localhost/velocita-raft:bench` image with the option enabled.

The benchmark-only node attributes successful commands at these ownership
boundaries:

| Stage | Meaning | Primary owner |
|---|---|---|
| `ingress_to_admission` | request delivery through local Raft admission | request decode / Raft owner |
| `local_wal_append` | leader's synchronous WAL append callback | storage |
| `quorum_append` | leader append publication to successful follower response | Aeron / follower / network |
| `admission_to_commit` | local admission until commit index advances | Raft quorum |
| `commit_to_apply` | commit observation until FSM application | Raft owner / FSM |

These spans diagnose ownership; they must not be added and presented as an
end-to-end number because some overlap. The client-clock end-to-end span remains
authoritative.

Build the benchmark image on the Pi:

```bash
cd ~/aeron-raft-pi
./deploy/build-benchmark-image.sh
```

Verify the isolation label:

```bash
podman image inspect localhost/velocita-raft:bench \
  --format '{{ index .Labels "io.velocita.benchmark-instrumentation" }}'
```

The answer must be `ON`. The production `:v1` image remains untouched.

## Run one command

The runner temporarily stops the production voter target because six Raft
processes would distort a 4 GB Pi and the UDP ports would conflict. Prometheus
and Grafana remain active. It then:

1. resets only `velocita-bench-node{1,2,3}-data` volumes;
2. starts three instrumented voters on separate benchmark WALs;
3. performs a repeated open-loop rate sweep;
4. writes immutable evidence and publishes the latest bounded metric snapshot;
5. merges exact samples rather than averaging percentiles;
6. stops benchmark voters and restores the production target through an exit
   trap, including after a failed run.

Start with the smoke mode:

```bash
./deploy/run-benchmark-suite.sh --yes --quick
```

Then run the standard repeated sweep:

```bash
./deploy/run-benchmark-suite.sh --yes
```

The default `current` CPU placement matches the current unpinned V1 deployment.
Run a separate controlled experiment with one container per Pi core and the
client on core 0:

```bash
./deploy/run-benchmark-suite.sh --yes --cpu-placement partitioned
```

Do not compare those results as though only an application optimization
changed; CPU placement is part of the experiment contract.

Use the long certification mode only after checking temperature, free disk,
CPU governor, and the quick results:

```bash
./deploy/run-benchmark-suite.sh --yes --certification
```

Prove that the harness exposes coordinated omission with the benchmark-only
10 ms stall mode:

```bash
./deploy/run-benchmark-suite.sh --yes --coordinated-omission-check
```

At 1,000 scheduled requests per second, a 10 ms owner-loop stall should create
a visible shoulder/population of delayed observations rather than only one
slow request. The mode is compiled out of the production image and is recorded
as bounded `run_mode="stall"` evidence.

The `--yes` is intentional. The runner deletes and recreates only the three
benchmark volumes named above. It never deletes the production volumes named
`velocita-raft-node{1,2,3}-data`.

Evidence is stored under:

```text
~/.local/share/velocita/benchmarks/
├── latest.prom
└── runs/<UTC-suite-id>/
    ├── metadata.txt
    ├── suite.tsv
    ├── rate-<rate>-aggregate.json
    ├── node-{1,2,3}.log
    └── rate-<rate>-rep-<n>/
        ├── summary.json
        ├── steady-samples.csv
        ├── warmup-samples.csv
        ├── failures.csv
        ├── steady-histogram.csv
        ├── warmup-histogram.csv
        ├── metrics.prom
        └── driver.log
```

Each `rate-<rate>-aggregate.json` is calculated from the union of exact samples
across repetitions at that same offered rate. Results from different rates are
never mixed, and the merger does not average p99 values. A p99.99 has little
statistical meaning unless the merged population contains enough observations
in the top 0.01%; always read the adjacent `count`.

## Grafana and Prometheus

The driver writes files only after the timed loop ends. A separate
`benchmark-metrics-exporter` serves `latest.prom` on loopback port 9300.
Prometheus scrapes it every five seconds, and Grafana provisions the dashboard
`Velocita — Raft Benchmark Evidence`.

Reinstall the observability configuration after building the benchmark image:

```bash
./deploy/install-observability.sh
systemctl --user is-active velocita-benchmark-metrics-exporter.service
curl -s localhost:9300/metrics | grep benchmark_metrics_file_up
```

The dashboard shows:

- latest-result age and whether instrumentation was enabled;
- completion ratio and correctness failures;
- p50, p99, p99.9, and p99.99 end-to-end history;
- p99 at each fixed ownership stage;
- offered versus completed throughput and outcome counts;
- the latest cumulative fixed-bucket histogram; and
- host CPU and temperature context.

Prometheus receives only bounded labels: `stage`, `quantile`, `outcome`,
`parameter`, `kind`, and the two-value operation class. Request IDs, suite IDs,
rates, and arbitrary profile names never become labels. Those belong in the
immutable evidence files.

The dashboard is not on the benchmark hot path. If Grafana, Prometheus, or the
snapshot exporter fails, the CSV and JSON result remains complete and can be
published later.

## How to read the result like a CTO

Do not ask only, "What is p99?" Ask:

1. What exact start and stop events define it?
2. At what offered rate and sample population?
3. Did completed throughput keep pace with offered throughput?
4. Were there timeouts, capacity rejects, Aeron backpressure, or leader retries?
5. Which ownership stage moved when the tail moved?
6. Were temperature, CPU policy, page faults, disk pressure, and competing
   processes comparable between runs?
7. Is this a production build result or an instrumented attribution result?

The safe operating point is below the knee where scheduling delay, tail
latency, or incomplete work begins to grow nonlinearly. Capacity planning then
reserves headroom below that point; it does not operate at the prettiest
throughput headline.

This implements the Velocitas book's benchmark protocol: open-loop intended
times, warmup separation, p50 through p99.99 plus maximum and count, bounded
outcome accounting, fixed dashboard buckets, exact offline evidence, metadata,
target-hardware Release builds, repeated load sweeps, and merging observations
instead of percentile values.

## What this does not prove yet

The V1 workload is a replicated counter command. It measures the current Raft,
WAL, Aeron, scheduling, and container topology. It does not prove matching-engine
performance or crypto cross-venue arbitrage viability.

V2 must replace the counter with representative operation classes and data
shapes: add, cancel, modify, aggressive match, risk decision, market-data
normalization, venue send, and acknowledgement. Each requires its own latency
contract and realistic occupancy/depth mix. V1 measurements tell us the
replication floor and where the Pi saturates; they are inputs to V2 budgets, not
the budgets themselves.
