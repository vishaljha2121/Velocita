# M2 steps 5–6: Aeron transport, binaries, and the two-process exit test

2026-09-24, `v2/phase-5-corrections` / `d44a4a9`, uncommitted.

## Step 5 pieces (each RED-first; evidence in sibling folders)

- Config `caller.<id>.response_endpoint` (UDP has no reply address): required,
  may not collide with voter/ingress endpoints. RED `../m2_node_config_2026-09-24/red-response-endpoint.txt`.
- Strict secret/catalog loaders (`../m2_node_files_2026-09-24/`): 0600-style
  permissions, effective-user owner, no symlink, exact 32 bytes; catalog
  verified against the pinned root. 3 mutants killed.
- Seqlock health page (`../m2_node_health_2026-09-24/`): owner does atomic
  stores only; concurrent reader never torn; 2 mutants killed; TSan clean.
- `AeronNodeTransport`, `velocita-v2-node`, `velocita-v2-client`,
  `velocita-v2-status`. CMake refuses configuration if any node target links a
  library whose name contains "venue".

## Step 6: `tests/v2_node_process_tests.cpp` (the roadmap M2 exit)

Real processes on loopback, peer traffic through an in-test bidirectional UDP
relay that can drop everything. Run logs of one kept run: `run-logs/`.

| Scenario | Observed |
|---|---|
| Bootstrap both; client asks the follower first | COMMITTED kill_epoch=1. **Correction (2026-09-25):** the redirect was NOT exercised in this run (the client always asked the lowest id, here the leader). Fixed and asserted via the follower refusal counter; see `../../M3_DEPLOYMENT_LOG.md` finding 3 |
| SIGKILL follower | New command TIMEOUT (two voters fail closed) |
| Restart follower (recover) | READY epoch=2; retried command COMMITTED |
| Partition through relay | TIMEOUT |
| Heal | COMMITTED kill_epoch=3 |
| SIGKILL both, restart both from disk | epochs 2 and 3; COMMITTED kill_epoch=4 |
| Status tool | reads the leader's page |
| SIGTERM both | exit 0, `STOPPED quiescent=1` |

State versions are checked against the independent reducer.

Observations, not defects: each 500 ms client resend during a blocked window
appends a duplicate log entry (the reducer deduplicates; documented propose
behaviour; sequence 3 landed at index 9). The health page lags by up to its
100 ms publish interval (commit=15 while the client already held index 16),
which is why M3a must show telemetry age.

Not covered: a physical network, NUC/Pi, power loss, clock skew, long soak,
Aeron-level packet loss/reorder, whole-process TSan. Release 107/107; ASan+UBSan
107/107 run alone (`sanitized-tests.txt`).
