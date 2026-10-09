# Continuation from Claude's handoff — October 9

The owner asked to continue the promotion plan after the independent review. This step fixes
the identified cutover/signer defects and corrects the reference-price evidence. No live
orders, credentials access, service restarts, ledger reset, cluster change or image deployment
occurred. The owner subsequently approved committing the reviewed files; this checkpoint
records that implementation and its evidence. It is not a deployment.

## Changes

1. `stage-host.sh` now explicitly refuses anything other than an inactive service, including
   query failures. It checks the observer as well as edge/gateway/voter/exporter; verifies
   both images on nuc1, required units, catalog/config and observer input before moving the
   run; and refuses duplicate ingress/loss-limit settings. It no longer relies on `! command`
   to stop `set -e`. Unit rewrites use a temporary file and rename, portable across the test
   Mac and Linux. This is not a transactional rollback mechanism: a disk error during the
   mutation phase still requires inspecting the preserved old directory and unit backups.
2. `FloorFile` owns a non-copyable RAII file descriptor with an exclusive advisory lock on a
   stable `.lock` sidecar. It acquires that lock before reading the floor, retains it across
   floor-file replacement, and releases it on object destruction/process exit. A second
   owner is refused. The lock path is deliberately not deleted on shutdown. All cooperating
   instances must use the same canonical floor path in a trusted directory; this is not a
   distributed lock or protection against a same-UID attacker deleting the lock file.
3. The signer socket has its own lifetime lock. Startup does not replace a live listener;
   stale sockets can be reclaimed, and shutdown removes only the inode it bound. These are
   constant-time startup/teardown operations outside the Raft reducer. No kernel wire format
   changes or additional hot-path Raft work were introduced.
4. Client socket connect/write/read use nonblocking I/O and a single monotonic deadline.
   Server socket reads/writes are bounded too. Socket writes suppress SIGPIPE locally. Disk
   fsync in the signer still depends on OS/storage completion: these deadlines bound socket
   waits, not storage latency. A full queue returns unavailable; it does not trigger a retry
   or grant authority to trade.
5. The basis study labels a close at `(close_time_ms + 1) // 1000`. Versioned caches reject old
   timestamp conventions and mismatched intervals. The same venue history was rechecked
   against public Binance GET data, with inputs cached and hashed. The corrected correlation
   peaks at **zero shift, 0.551**, rather than establishing a 1–2 second venue lag. Equal-time
   basis is **-9.39 bp**, standard deviation **2.43 bp**. Original output is preserved.
6. The promotion plan now separates the measured 20 bp CLI sell from assumed buy cost and
   historical 100/98 bp quotes, corrects the status count/runtime snapshot, and labels the
   replay as an unconstrained signal diagnostic rather than feasible $25-account turnover.

## Tests and mutation checks

- Signer suite passed on macOS, including test-key process startup, simultaneous-process
  refusal, graceful restart, persisted floor and socket tests.
- Signer suite passed on Linux/nuc2 in `$HOME/velocita-review-20261009/signer`, built at
  `nice -n 10`. This is an isolated scratch build, not a deployed binary or container image.
- The Linux mutation removing `flock` fails the named floor-owner assertion. Removing
  nonblocking socket mode fails the named full-accept-queue deadline assertion. The queue
  test did not expose the original blocking behavior on macOS; Linux evidence is retained
  rather than claiming the Mac run established that regression.
- Existing local mock venue transport integration tests pass with the changed signer.
- Six staging tests pass (including each of five active services, query failure, missing
  shadow image/unit, existing ingress, and successful staging with old-state preservation).
  Three staged-script mutants fail their intended assertions: absent stop check, late
  image check, and absent duplicate-ingress check. These use fake systemctl/podman commands
  and disposable directories; no production script was run.
- Two timing/cache tests pass. The opening-time mutant fails the worked-example assertion:
  the close of [1000 ms, 1999 ms] is available at second 2, not second 1. Original tests also
  rejected silent reuse of a legacy timestamp cache.
- Release-image exclusion check still passes. Signer and transport remain mock-only and
  unshipped. No full repository/ARM/sanitizer certification is claimed.

Logs are in `continuation-tests/`; `changed_paths.json` records hashes of the exact patch
files. Early tests ran before the fixes. The initial staging red run produced missing-path
errors, so it was not counted as mutation coverage; later checks assert path existence first
and fail on the intended invariant. The timestamp red run and signer red run failed named
assertions. Shared dirty CMake/gateway/research work was not modified.

## Host state and remaining work

At 19:14 UTC the original paper cluster still had current/durable index 20880 and
commit/applied 20876, with nuc1 leader in term 13. Paper cash/equity was $22.314889, flat.
The edge remained failed. Later metadata-only inspection found caller-11 and caller-12 key
files present. Missing keys were an earlier post-reboot failure, not a confirmed continuing
blocker. The later `reason=source code=-1` is a source-command submission without an outcome;
it is not itself a market-data error. The four-entry tail's precise cause remains undiagnosed.

The owner approved this source checkpoint after reviewing the implementation. Only the
reviewed paths and their supporting evidence are included; shared changes remain outside it.
Next, build production images from the approved commit using `deploy/v2/build-image.sh` on hosts, and update the prepared
image identities consistently. The current staging script still names `2f5d405`; it must not
be mistaken for an image containing this follow-up patch. Remaining prepared activation,
profile and ceremony files are another session's untracked work and are not silently included
in this patch's commit scope.

Before replacing the incarnation, confirm a flat/quiescent paper account, stop the single
existing cluster, archive its state and units, then stage the replacement. The ceremony
remains owner-run unless the owner explicitly delegates it. Restart voters with exporters
and refresh monitoring mounts after a source-tree swap. Keep one cluster per host and one
ledger writer. Verify all three committed/applied indices converge, the edge advances, and
the new observer selects the correct run. Then perform the pending position-held paper drills.

This does not close real payload/TLS integration, venue fee/order/reconciliation contracts,
outside heartbeat, crash-recovery decisions, or the funded-account strategy-validation gate.

## Local test commands

```sh
cd "$HOME/aeron-raft-pi"
python3 -B -m unittest discover -s tests -p test_roles_stage.py
python3 -B -m unittest discover -s tests -p test_venue_basis_timing.py
python3 -B tests/test_signer_not_shipped.py "$PWD"
```

The C++ tests were compiled directly from the selected test, signer sources and isolated
committed contract libraries under `$HOME/.cache/velocita-review/continuation`. Each mutant
was a separate source file/binary; no temporary broken source was left in the shared checkout.
Use `TMPDIR=$HOME/.cache/vr-review-tmp` for short, durable local test-socket paths.
