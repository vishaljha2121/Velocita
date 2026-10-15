# Native qualification complete — 2026-10-15

**Executable source: `1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc`. Qualified on NUC1/x86_64 and Pi/aarch64. Not installed or enabled live.** A later documentation-only checkpoint does not change this executable identity.

| Check | NUC1 | Pi |
|---|---:|---:|
| Selected CTest targets | 59 passed | 59 passed |
| Python/CLI unittest checks | 383 passed | 383 passed |
| Existing process-drill cases | 8 passed | 8 passed |
| Fixed continuous-suite repetitions | 3 passed | 3 passed |
| Six-effect continuous cases | 9 passed | 9 passed |
| Continuous fixture executions / settlements | 54 / 54 | 54 / 54 |
| Retained artifact SHA-256 checks | 151 passed | 151 passed |

The 383 checks include the 104 focused model/staging/credential tests and 24 packaging-boundary
checks; do not add those again. The eight process cases are also exercised within CTest. Each
continuous repetition runs baseline, crashes at predecessor-journal retirement boundaries, and
crash after execution acceptance with recovery-only reconciliation before normal continuation.
Every case has exactly six creates, signatures, execution POSTs and settlements. All replicas
finish flat with no holds, $25 cash, $0.012 fees and $3 cumulative purchases. Fixture price moves
were scripted to offset fees; this is a correctness test, not strategy-return evidence.

[Machine-readable results and binary hashes](QUALIFICATION.json),
[NUC runner result](qualified-1f8d7b6/nuc1/status.json),
[Pi runner result](qualified-1f8d7b6/pi/status.json),
[commands](COMMANDS.md), [protocol](PROTOCOL.md).

## Corrections and preserved attempts

| Commit | Change / result |
|---|---|
| `c19a675` | F1–F5 implementation, tests and initial evidence. Both native CTest suites passed; packaging isolation correctly rejected the unlisted new test target. |
| `887d6f5` | Exact test-target allowlist plus four negative packaging checks. NUC qualified; Pi stopped after signature 4 before execution 4. |
| `26b1e51` | Queue the next Raft command until a fresh tick after blocking ports; four deterministic stale-time/expiry regressions. NUC qualified all repetitions; Pi exposed a separate signer-response failure after signature 2. |
| `1f8d7b6` | Start a fixed signer request deadline after accept, separate from idle connection waiting. Late-frame regression fails against old code; incomplete requests still close unsigned. Both native runs now qualify. |

Failed outputs are retained under `attempt-c19a675`, `attempt-887d6f5/pi` and
`attempt-26b1e51/pi`. Successful intermediate NUC results remain historical, superseded for
installation by `1f8d7b6`. These were code-corrected checkpoints, not blind retries until a test
passed. Timing defects were independently reproduced; the old native transcripts cannot prove
exclusive causal attribution. Details: [Raft timing](TIMING_CORRECTION.md),
[signer accept timing](SIGNER_ACCEPT_DEADLINE.md).

## Host changes and limits

Builds used the exact committed bundle in a clean dedicated cache, pinned TLS toolchain image,
network-disabled container, non-root user, core 3, one compiler and 2 GiB memory cap. Existing
incremental objects were reused. No production image, manifest, voter, exporter, funding,
credential or journal was changed. The only cleanup archived inactive Pi build artifacts with
per-file hash checks before removing their originals; the private archive remains on Pi.
[Capacity outcome](pi-capacity-archive.json) and [archival script](archive-pi-cache.py) are retained.

A read-only [production inventory](production-status-during-qualification.json) at 15:58 UTC
showed one existing voter/exporter pair per host on `qualified-54a5fcb`, NUC1 gateway inactive
and signer failed, both PID 0. This is service/container metadata, not a fresh authenticated
Raft state read. The last owner readback remains the authority for the funded/halted state.

No warnings were suppressed. Each final incremental build reports seven unique warning lines;
previous compilation diagnostics remain relevant. See [warning ledger](../BUILD_WARNINGS.md).
Fake HTTPS and same-UID fixture workers do not qualify external connectivity, deployed UID
separation or Aeron. Unfinished create/prepared crashes may still need supervised recovery.
A synchronous call can exceed its deadline before returning; this is not an asynchronous-I/O
rewrite or a guarantee of graceful shutdown. Journals preserve ambiguous effects.

## Next deployment boundary

1. Review and install the exact qualified gateway/runtime and corrected driver/tooling, preserving
   the single cluster, installed authority, funding and all journals. No voter-state reset is needed.
2. Owner sudo: use the committed create-only caller-key restoration helper; verify role access,
   protected metadata, stopped services and empty recovery directories. Retain retired originals.
3. Recheck fresh account/authorization state and expiry (recorded window ends October 16,
   13:48:34 UTC). Start while halted, verify authenticated reads and signer communication, then seal
   using reviewed current pins. Do not repeat arm, funding or retirement.
4. Stage the corrected driver once with a six-effect ceiling. Verify the next operator sequence
   before enabling the supervised run. Reconcile each settlement; do not reset driver state or
   automatically retry an ambiguous effect. Extension to 240 remains separately approved work.

These are remaining deployment actions, not actions performed by this qualification.
