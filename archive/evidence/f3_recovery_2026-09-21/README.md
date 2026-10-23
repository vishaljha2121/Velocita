# F3 recovery compatibility prerequisite — host evidence

Base: d44a4a9, branch v2/phase-5-corrections, plus uncommitted audit/F1/F2/F3
work. This is a development checkpoint, not a frozen source-root acceptance.
See ADR 0012 and CP-005 in `../V2_REMEDIATION_CHECKPOINTS.md`.

- `red.txt`: main-observed failures before each production correction.
- `green.txt`: first focused and full host GREEN.
- `green-final.txt`: final full host runs after older/newer schema coverage.
- `mutations.txt`: five main-agent direct-compiled guard mutants and exact kills.

Tests live in `tests/v2_replication_tests.cpp`. Focused selectors:

```text
unsupported-snapshot
unsupported-oversized-snapshot
unsupported-segment-suffix
unsupported-legacy-suffix
snapshot-preflight-readonly
```

Always build first. Use a fresh TMPDIR for parallel test/mutation runs: the suite
owns a fixed test-directory name underneath it. Direct mutants link the rebuilt
test object against one replacement source translation unit before the current
static archives; workspace production files are not changed. Temporary mutation
sources/binaries are not durable evidence; the transformation descriptions,
commands, permanent tests and observed assertion output are recorded here.

Scope: compatible framing's complete reachable records, exclusive storage
ownership, host cache line 64. Persisted schema constants remain 7. No catalog
installation, artifact availability proof, schema-8 rollout, target acceptance,
Python/model rerun, live transport, or real-money activity is claimed.
