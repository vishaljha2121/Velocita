# Codex to Claude: automated MVP review and continuation

Updated 2026-10-15 17:42 UTC. This supersedes startup/activation instructions in the earlier October 15 handoff. It does not authorize new trading scope. Current branch is `v2/phase-5-corrections`; HEAD is `0fed1b012b8b0b1b551ed269014008bc9209ab1a`. This handoff and the final attempt evidence are uncommitted. The shared tree contains substantial unrelated work.

## Start here

1. Read this file and [current status evidence](../evidence/automated_launch_2026-10-15/focused-deploy/six-effect/HALTED_HANDOFF_STATUS.json).
2. Review [Codex findings](../evidence/automated_launch_2026-10-15/CODEX_REVIEW.md), [focused implementation](../evidence/automated_launch_2026-10-15/focused-mvp/README.md), and [native qualification](../evidence/automated_launch_2026-10-15/focused-mvp/native/RESULTS.md).
3. Read the [startup/seal evidence](../evidence/automated_launch_2026-10-15/focused-deploy/START_SEAL_RESULT.json), then the [feed incident](../evidence/automated_launch_2026-10-15/focused-deploy/six-effect/FEED_DIAGNOSTIC.json) and unfinished tests below.
4. Use [live tracker](../LIVE_PROMOTION_PLAN_2026-10-07.md) for remaining work. Older dated sections are historical, not instructions to repeat ceremonies.

## Current runtime: verified, stopped driver, no strategy trades

Read-only checks at 17:42 UTC confirmed:

| Item | State |
|---|---|
| Cluster incarnation | `aae119b95ddcea5a206f03ab5ca12c15`; one existing cluster |
| Voters | All three up/ready, failed=0, term 1, leader 3, commit/applied/durable index 9 |
| Trading mode | Halted (5); owner sequence 8 applied at index 9 |
| Next operator sequence | 9 **only if no intervening command**; recheck history |
| Driver | `velocita-strategy-driver.service` inactive/dead, PID 0 |
| Gateway / signer | Active/running, PIDs 467333 / 467321; `RefuseManualStart=yes` retained |
| Managed account | 25,000,000 USDC atoms ($25), zero managed SOL, fees, realized P&L, effects and pending effects; no settlements |
| Driver/client state | Existing directories contain no files; no request was allocated |
| Review expires | 2026-10-16 13:48:34.459 UTC (`1792158514459`) |
| Sealed manifest | `f3df32529bc2e3bfd08cb4a82ddecd8fe9b128160b2417ecb7850252cd0668c4` |

Service/Prometheus reads are operational evidence, not independent authenticated Raft proof. The producer feed is written from authenticated runtime observations. This handoff did not request a fresh TM wallet balance; do not claim one. No host services or credentials were changed while writing the handoff.

NUC1: `velocitas@100.72.127.102`; sudo owner is `vishal` on that host. Pi: `velocitas@100.84.151.116`; NUC2: `velocitas@100.85.36.22`.

Installed gateway/signer executable source is `1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc`, SHA-256 `8d0fab33246d85628cac6471c3926205fd5710f96a55731106324a101cc9abcf`, under `/opt/velocita/releases/<full-commit>/velocita-v2-gateway`. Voters and driver client retain `localhost/velocita-v2:qualified-54a5fcb`; do not mistake the newer runtime binary for a voter-image replacement.

Existing driver root: `/home/velocitas/velocita-strategy/aae119b95ddcea5a206f03ab5ca12c15/`. Preserve `driver.json`, opening record, `state/`, `client/` and unit. Model archive currently matches `1f8d7b6` (76 files). Opening SHA: `2f329e441584fbda843187ffd251f8ac000ea383506b0ba4623586ef604d9b95`.

## What Codex completed after Claude's handoff, and why

| Problem | Implemented correction | Evidence / limit |
|---|---|---|
| Runtime and journals only supported one settled effect, so a buy could strand a position | Advance per-effect gateway/signing/signer journals only after matching authenticated settlement; preserve prior replay evidence and obtain new independent signing authority | `c19a675`; six-effect production-composition fake-venue cases, including crashes and lost responses |
| Retirement moved valid caller keys away; verifier missed them | Preserve caller keys in retirement, validate metadata/role access, create-only restoration from retained tmpfs sources | Tested restoration installed by owner; originals retained, no key content logged |
| Old candle could be relabeled with current time | Keep actual candle source time; enforce 2-second age and future rejection; recheck immediately before client submission | Driver/rule tests and mutations; the new guard exposed the bootstrap backlog below |
| Last available effect could open a position with no exit slot | Reserve exit capacity; count attempted effects, not successful-fill parity; persist ambiguous submissions | Six is an attempted-effect ceiling, not a promised three round trips |
| Review expiry could leave exposure or pending work | Block entries/drain 420 seconds before expiry; stop starting exits 210 seconds before expiry; explicit durable stop reason | No guaranteed flatness, automatic renewal, or unlimited recovery authority |
| Blocking calls consumed a previously computed Raft deadline | Begin the subsequent operation with a fresh tick/deadline | `26b1e51`, deterministic regression tests |
| Waiting for a signer connection consumed its request deadline | Start the fixed request deadline after accept; incomplete frames still time out unsigned | `1f8d7b6`, late-frame regression and dual-architecture qualification |
| Installation/first startup could repeat destructive lifecycle steps | Guarded, pinned installer and first-start/seal helper; observer review lifetime aligned to 24-hour contract | `24ecbec`, `f30e6cc`; owner installation and startup completed |

Commit chain: `c19a675` implementation; `887d6f5` test packaging isolation; `26b1e51` Raft timing; `1f8d7b6` signer timing; `fa495a1` native evidence; `24ecbec` install/observer tools; `51c7d47` staging evidence; `f30e6cc` startup/seal; `0fed1b0` native startup-tool qualification/staging.

Each architecture passed 59 selected CTest targets, 383 Python/CLI checks, eight process-drill cases and three repetitions of three six-effect cases (54 fixture executions/settlements per architecture). Each effect had one create/sign/execute/settlement. All 151 retained hashes per host verified. Local 33 named mutation kills are separate. Counts overlap as described in native results; do not sum them as independent tests. Failed attempts remain preserved. The startup Python bundle passed 64 checks on NUC1.

Fixtures use real production codecs/verifier/reconciliation with fake HTTPS transport, real Raft implementations and separate gateway/signer processes. They do not demonstrate alpha or qualify all live service-user/network behavior. Automatic continuation is qualified at completed settlements; unfinished current create/prepared crashes can require supervised recovery. Do not convert this into a universal exactly-once network claim.

Owner installed the qualified runtime, restored both caller keys, then started signer followed by gateway while halted. Both initialized; five journal stores were created (`prepared`, `protected-signing`, `results`, `signing-requests`, `submitted`), then the installation was sealed. **Do not rerun first-use startup, arm, funding, retirement or key restoration.** The journals are now initialized even though no effect exists.

## Live attempt and newly discovered blocker

Owner sequence 7 set normal mode at index 8. After pinned-model/client/opening, public-feed and account checks, Codex started the existing six-effect driver once at 13:34:03 EDT (17:34:03 UTC). It repeatedly logged `feed restart: ProtocolError`, before allocating any request. Codex stopped the driver. Owner then set halted mode using sequence 8, committed at index 9. The live six-effect run **has not completed; zero effects occurred**.

The bootstrap opens the live WebSocket first, then fetches three REST pages (3000 one-second history bars). This avoids losing live coverage while fetching history, but messages accumulate in the socket. A no-submitter diagnostic measured bootstrap at 4.413633 seconds. The first consecutive queued candle was already 3615 ms old; the new 2000 ms guard rejected it and restarted bootstrap. This created a restart loop. The next queued candles were aged 2615, 1615 and 615 ms.

The diagnostic's later `gap=true` flags compare every row with the unchanged bootstrap endpoint. The four source timestamps are consecutive: those flags do not establish missing feed seconds. The initial separate REST/WebSocket checks passed because they did not test the combined open-before-bootstrap sequence. Add that integration coverage.

This is an availability bug in Python feed initialization. The freshness protection prevented trading; do not fix availability by widening freshness or labeling old candles as current. No C++/Raft schema or strategy/economic change is proposed.

## Unfinished code: deliberately RED, not implemented or deployed

Only `model/tests/test_live_swap_driver.py` has new catch-up work. Two methods in `GuardRegressions` specify:

- Old contiguous bootstrap backlog warms indicators without orders, sequence allocation or state-file creation; a subsequent fresh candle may trade with its original source timestamp/deadline.
- Catch-up still rejects future candles and missing seconds.

`Driver.on_bar` currently has no `catching_up` parameter. Last run: 59 unittest executions, two errors (`unexpected keyword argument 'catching_up'`). This is an API-level RED starting point, not completed behavioral proof. See [retained RED output](../evidence/automated_launch_2026-10-15/focused-deploy/six-effect/CATCHUP_RED.log). Production `live_swap_driver.py` has no catch-up edits. Do not deploy the test-only working tree.

Recommended minimal implementation, subject to review:

1. Introduce explicit bootstrap/reconnect catch-up state. Keep the ordinary `on_bar` default strict.
2. During catch-up, validate future timestamps and continuity, then warm old consecutive candles using the existing non-trading `warm` path. Never allocate a request or call submit on these candles. Keep account/review/inflight checks active.
3. Transition out of catch-up when fresh data resumes. Keep source-time preservation and the independent client-submit freshness check. Test the main loop transition, duplicates, genuine gaps, reconnect and stalled socket behavior.
4. Prove stale catch-up never submits and normal-mode stale data still refuses. Mutation-test skipped continuity/future checks, stale emission and a catch-up phase incorrectly left enabled.
5. Rehearse the exact open-before-bootstrap sequence on public data with isolated candidate code and **no real submitter/client construction**. Stop at a fixed bound; no quote/order activity.

The tests propose keyword `catching_up=True`, not an obligation to keep that API if review finds a smaller correct design. Determine a bounded catch-up/rebootstrap policy without weakening expiry or freshness.

## Next milestones and acceptance boundary

| Next | Completion condition |
|---|---|
| Review this checkpoint | Inspect lifecycle/journal ordering, C++ ownership and deadlines, driver caps/drain, source timestamp handling, tooling metadata checks; distinguish existing native proof from remaining integration coverage |
| Finish Python catch-up | RED/GREEN + named mutations + main-loop coverage + bounded no-submit public-data rehearsal |
| Scoped checkpoint | Owner approval before committing explicit owned paths; preserve unrelated dirty changes and partial-stage shared tracker sections |
| Qualify/deploy corrected model | Test committed Python on NUC1; replace only existing driver model source while inactive, retaining an archive and all driver/client/config/history state. No C++ rebuild if C++ is unchanged |
| Resume supervised six-effect run | Fresh review/account/feed/pins and sequence checks; owner mode-normal command with verified next sequence (currently 9), then start the existing driver once; no forced trades or repeated order retries |
| Report actual outcome | Follow every intent/sign/submit/settlement, reconcile wallet versus Raft, stop safely with explicit exposure/pending status. Six remains a ceiling |
| Unattended extension | Separate owner approval for `--extend 240`; never restage/reset sequence state |

Current supervised configuration is $25 managed, 0.12 SOL target buys, six attempted effects, $10 realized-loss stop, 24-hour review. The older $0.50/single-$1 plan is historical; source these later settings from the approved automated-launch documents and installed profile. Do not silently change economics. A realized-loss stop does not guarantee a mark-to-market drawdown ceiling. Do not sell outside inventory.

Review expiry is a real cutoff. If the window expires, refresh account/history and reviewed documents through the reviewed procedure before resumption; do not edit timestamps or assume old authority carries forward.

## Design decisions, preserved guarantees and deferrals

The focused correction batch did not alter the kernel reducer, command schema, replicated layout, journal format, account scope or verifier economic caps. Gateway coordination remains single-threaded; network work stays outside apply. Journal records are owned copies, independent authenticated reads gate reuse, and no snapshot pointer should survive a replacement read. Review these mechanics in the actual committed sources, not this prose alone.

Read ADR0025 (bounded verification/recovery), ADR0026 (narrowed stable policy/per-attempt route checks), ADR0027 (unsent release/reviewed baseline), ADR0028 (recent-settlement window/lifetime count), ADR0029 (signer-authenticated gateway session). Their headers retain older local/proposed deployment language; verify implementation through commits/native evidence rather than assuming every header is current. This handoff does not accept a new ADR or broaden route trust. ADR0029 already has the focused implementation correction. A feed catch-up repair within the same freshness contract need not create an ADR.

Unknown transaction layouts remain fail-closed. Exact minimum-output rounding and sponsor/rent accounting history are not waived by this feed repair; consult current adopted transaction review before changing any economics. No claim that a 20 bp displayed fee alone proves all wallet deltas. No automatic daily renewal, no automatic replay of ambiguous create/execute, no journal deletion. Performance/profitability and full six-effect live execution remain unproven by fake-venue tests.

Deferred owner artifacts: explain kill epoch/executor generation/modes/start guards/review window and stopping; end-to-end networking stack explainer after live milestone. Compiler warning cleanup is tracked in `focused-mvp/BUILD_WARNINGS.md`. These are not newly invented live gates.

## Commands and evidence for continuation

Safe read-only commands:

```sh
ssh velocitas@100.72.127.102 'systemctl --user status velocita-strategy-driver.service --no-pager; journalctl --user -u velocita-strategy-driver.service -n 40 --no-pager -o cat'
ssh velocitas@100.72.127.102 'cat "$HOME/velocita-v2/textfile/strategy-settlements.json"'
```

Local focused tests (currently two expected RED errors):

```sh
cd "$HOME/aeron-raft-pi/model"
PYTHONPATH=src python3 -B -m unittest discover -s tests -p test_live_swap_driver.py
```

Relevant implementation: `model/src/velocita_model/shadow/live_swap_driver.py` (`bootstrap`, `main`, `Driver.on_bar`, `client_submitter`); `live_swap.py`; `src/v2_swap_gateway_runtime.cpp`, `v2_swap_runtime_production.cpp`, `v2_swap_signing_connection.cpp`, `v2_swap_signer_session.cpp`; focused tests and native evidence. Read committed versions for deployed C++: shared `src/v2_gateway_main.cpp`, `src/v2_gateway_loop.cpp`, CMake and gateway tests still contain unrelated edits.

Earlier owner install/start commands are retained in `focused-deploy/owner-install.sh` and `owner-start.sh`; **historical audit only, do not rerun**. Startup tools at `f30e6cc`/`0fed1b0` document the exact ceremony. Current raw no-order supervision is on NUC1 in `~/.cache/velocita-build/six-effect-supervision-20261015/observations.jsonl`. Local raw WAL at `~/.cache/velocita-build/six-effect-20261015/nuc1-wal.bin` stays private. Sequence audit is summarized in `six-effect/SEQUENCE_AUDIT.json`; it predates normal7/halt8.

Preserve the entire shared tree. No reset/clean/stash, broad staging, or committing shared files whole. No new commit is authorized by this handoff request. Commit footer remains `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Sudo, Keychain and TM session remain owner-run; never use chat passwords or log credentials. No Safari screen sharing. Only one cluster. Archive instead of deleting, and never rerun a partial ceremony blindly.
