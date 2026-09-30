# Phase 6–9 full-scope architecture audit — 2026-09-21

Verdict: **NOT ready for live trading.** The deterministic core is useful and
substantially tested; composition, authority, boundedness, recovery and venue
admission are not complete. This is a completed review of the Phase 6–9 scope,
not a proof that every defect has been found or an acceptance gate.

This is the pre-fix audit. Current remediation status and RED/GREEN evidence
are tracked separately in `V2_REMEDIATION_CHECKPOINTS.md`; historical failures
below are preserved rather than rewritten after a fix.

## Identity, scope and method

- Branch: `v2/phase-5-corrections`; HEAD `d44a4a964fc8c13279878e53d245ab56b921a824`.
- Reviewed the Phase 6–9 delta from `v2-phase5-corrected-accepted`, its production
  interfaces and callers, tests, ADR 0001 and ADRs 0005–0011 with amendments,
  architecture §§9–13, plan §§11–17, and G-035–G-092 dependencies.
- The prior turn's dirty tooling/docs and untracked `model/` were preserved.
  This turn changes audit documents/probes, not production C++ or contracts.
- No venue, authentication, NUC or Pi calls; no secrets or real orders.
- Earlier Phase 1–5 algorithms were not re-audited line by line. Their regression
  suites ran; Phase 8 changes crossing their boundaries were inspected.
- Reproduction sources are in `phase6_9_audit_2026-09-21/`. They assert desired
  properties and deliberately fail on this candidate. They are not included in
  the passing CTest count. Move each into its owning permanent suite before fixing.
- The main reviewer independently ran the prior replacement/authority findings
  and the new probes below. The earlier subagent reports remain supporting notes;
  this report supersedes their provisional status and narrower verdicts.

## Verification, including limitations

| Check | Result | What it does not prove |
|---|---|---|
| Fresh isolated Release build, then CTest | 36/36; 8.88 seconds | Target hardware, live process integration |
| Rebuilt isolated Debug ASan+UBSan, then CTest | 36/36; 36.29 seconds | Race freedom, physical persistence |
| Offline read-only tool regression | 9/9 | Real venue response schemas |
| Passive model tests, bytecode writes disabled | 109/109 | Strategy profitability or live readiness |
| Audit reproductions | Ten intentional RED runs below | A passing production acceptance suite |

Sanitizer build flags: `-fsanitize=address,undefined -fno-omit-frame-pointer`;
run environment `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`.
Probe translation units additionally use `-fno-sanitize-recover=undefined`.
Leak detection was explicitly disabled; TSan was not rerun in this audit.
Historical mutation tests are not rebranded as fresh mutation evidence.

The existing `build/` failed before testing because a generated build.make was
missing. No stale CTest result was used. A fresh isolated Release directory was
configured with the already-fetched pinned dependencies and no downloads.

Local raw build/test artifacts:

- `/private/tmp/aeron-phase67-audit-build/Testing/Temporary/LastTest.log`
- `/private/tmp/velocita-full-audit.lYB4P5/release/Testing/Temporary/LastTest.log`
- `/private/tmp/velocita-full-audit.lYB4P5/`: probe executables and retained dummy
  voter directories. Temporary artifacts may disappear; sources, commands and
  observed outputs are retained in the adjacent reproduction README.

## Findings, in fix-priority order

### A01 — HIGH: a stale leader can renew gateway authority after majority kill

`src/v2_replication.cpp:402` assigns `authoritative = raft_is_leader(...)`.
`src/v2_gateway.cpp:41` accepts that boolean and refreshes the lease at the
caller's current time. The authority probe starts with a materializable committed
effect, isolates the old leader, elects a majority leader and commits a new kill.

Observed: `majority_kill=2 stale_kill=1 old_authoritative=1 renewed=1 permitted=1`.

This composes existing public APIs; it does not pretend a live gateway exists.
Being in the same process as a voter does NOT repair the stale-role problem.
Shortening the lease also does not help if stale reads can continually renew it.
Violates the intended G-054/G-055 integration and ADR 0001 stale-authority rule.

Fix: quorum-confirmed, current-term read barrier plus applied-index coverage,
request correlation and bounded proof age, consumed by the final sender. Do not
stamp arbitrarily delayed responses fresh on arrival. Partition, delayed reply,
kill-after-read, restart and old-generation tests must exercise the composed path.
External fencing remains necessary before any gateway promotion; read-index
cannot retract an already-issued request. Material protocol choices need review.

### A02 — HIGH: delivery planner discards reserved emergency quota

`src/v2_gateway.cpp:399` initializes only the normal budget and stops iterating
when it is zero; its guard call constructs `SendQuotaView{budget}`, losing the
emergency field. Existing tests exercise the guard, not this composition.

Observed: `cancel_guard_permitted=1 planned_sends=0` with quota `{0,1}` and an
expired new-risk lease. A valid cancel is stranded exactly when its reserve is
needed (G-057/G-058). Fix the planner's two-budget consumption and test mixed
submit/cancel plans, zero normal quota, exhausted emergency quota and actual
transport sends. This enforces an already-approved contract; no new bytes needed.

### A03 — HIGH: venue rendering can change authorized economics

`src/v2_venue_wire.cpp:239` accepts a submit carrying a positive limit price and
required post-only, then emits `type=market` without either constraint. The
existing test at `tests/v2_venue_wire_tests.cpp:216` explicitly passes
`limit_submit()` and expects this body, so it rewards the violation.

Observed: post-only limit accepted as
`{"base_asset":"BTC","quote_asset":"USDC","qty":"0.003","side":"buy","qty_unit":"base","type":"market"}`.

Also, `begin_body` does not bind `spec` to the effect's venue/instrument. A spec
for instrument 999 encodes instrument 4's effect successfully. And
`spec_agrees_with_profile` only verifies tick×lot: both (10000,100000) and
(100000,10000), at 8 decimals, match the same profile while rendering the price
as 2.5 versus 25 and scaling base quantity oppositely. Notional agreement is
not economic-intent agreement.

These helpers have no live caller today; no live trade is alleged. They cannot
be used as a safe adapter boundary (G-062/G-063; ADR 0001 invariant 3).
Fix: reject market lowering of existing limit intents, bind exact immutable
instrument metadata and capabilities, check identities/versions at the encoder,
and reject unsupported post-only/TIF instead of dropping them. A swap contract,
if approved later, must originate in the reducer, not in a renderer.

### A04 — HIGH: persisted state does not bind quorum membership

`StoredSnapshot` has no membership; `ReplicatedKernel` rebuilds it from constructor
arguments, defaulting an empty list to self. The snapshot membership test passes
the original configuration again; it proves re-insertion, not durable identity.

Observed: compact a three-voter history, reopen the same directory with a
single-member vector, and it becomes leader while isolated. This is a
configuration-change trace, not a claim of spontaneous misconfiguration.
For the NUC/Pi pilot, an omitted startup argument must not silently convert
two-host durability to one-host authority.

Fix: persist/verify cluster ID, node identity, static membership and schema/config
identity before participation. Refuse mismatches; do not implement an implicit
membership migration. Recovery/peer-snapshot metadata must carry the same binding.
This storage/protocol contract needs approval. G-040/G-049 are not complete as worded.

### A05 — MEDIUM: malformed normal proposals can poison replay

`ReplicatedKernel::propose` validates the control lane only. One byte `ff` on the
normal lane is admitted and durably staged; apply fails to decode it. A restart
loads the bytes, then fails again when ticking applies the entry. The probe was
corrected to tick after restart: construction alone did not reproduce the failure.

Public API comments request canonical bytes, so a future trusted ingress could
prevent this. That ingress does not exist. Reject malformed/oversized envelopes
before admission and test healthy progress after refusal. Do not change replay
to skip a malformed committed entry; that would invent a different history.

### A06 — MEDIUM: a valid bootstrap can authorize an unmaterializable effect

The public-command replication fixture starts generation and kill epoch at zero,
then enables normal mode. Authorization succeeds, but `validate(OrderEffect)`
requires both identities nonzero and materialization returns absent.

Observed: `authorized_effects=1 materializable=0`. The gateway fails closed,
so this is stranded work/reservation, not an unreserved send. It also means the
existing replication fixture does not prove a deliverable end-to-end effect.
Require valid initial authority before authorization, and make integration
fixtures initialize it through commands and compare full materialized bytes.
Do not weaken effect validation or silently rewrite its identity.

### A07 — HIGH availability gap: bounded staging is not bounded replication

`include/velocita/v2/replication.hpp` owns unbounded outbound/result deques and
a map of every retained log payload. `apply_log` allocates a result on each
replica; send callbacks allocate payload copies. Only caller drains and explicit
compaction release these. A disk that keeps up replenishes the WAL arena and
therefore does not backpressure a stalled result consumer.

Observed: 2000 identical valid retries admitted, 2000 undrained results, 2000
retained log entries. The number 2000 is a witness, not a proposed capacity;
source has no fixed limit. G-088/G-089 and architecture §13 require explicit
capacities/full policies for the entire process. Preallocate/bound admission,
result retention and peer queues; schedule bounded replication batches and
durability/emergency work ahead of normal ingress. Define compaction and disk
retention watermarks. Never discard an effect/evidence obligation to free space.

### A08 — HIGH recovery gap: a fresh voter cannot fetch compacted history

The leader compacts through index 9; a fresh replacement with the same member ID
remains applied=0 after 5000 route/tick/flush rounds. PeerMessage has vote/append
messages but no snapshot transfer. Local snapshot+suffix restart is implemented;
replacement recovery (G-040) is not. Retained files were not deleted: the probe
uses a fresh replacement directory.

Fix: reviewed bounded snapshot transfer/install with cluster/membership binding,
checksum/length/version checks, crash-safe publication and suffix convergence.
A stopped, verified manual restoration can be an interim development runbook;
it does not close the original peer-install requirement or justify deleting WALs.

### A09 — MEDIUM design/evidence gap: hard state still blocks; cuts untested

`persist_term`, `persist_vote`, and `persist_commit_index` call
`MetadataStore::save` on the owner thread. Save performs write, file fsync,
rename and directory fsync. This includes steady-state commit updates, not only
maintenance. The storage benchmark measures the WAL path, not full owner-loop
latency. G-043 remains open; do not call this merely optional tuning.

The prior audit's "wrong file" accusation is corrected: the test deliberately
loads the intact file after creating a torn spare. It proves intact reads and
corrupt-file refusal, not crash cuts through the persistence protocol. Atomic
replacement can be correct; a literal two-slot scheme is not mandatory by Raft.
The implementation plan nevertheless names two slots, so either justify an
approved plan amendment with cut evidence or implement the specified protocol.

Fix: inject process/storage cuts at each write/sync/rename/directory-sync boundary,
verify durable term/vote consistency and acknowledged-prefix recovery, then move
hard-state I/O behind explicit protocol durability fences. Removing fsync or
acknowledging before the fence would be an unsafe "optimization".

### A10 — MEDIUM helper defect: multi-block synchronous flush duplicates a block

`WalSegment::flush` repeatedly calls `arena.next_sealed()` without advancing or
completing until after all writes. With two sealed blocks it writes the front
twice, syncs, then throws on the duplicate completion.

Observed: `sealed_before=2`, `WAL completion order violation`, stored indices
`1,1` instead of `1,2`. `ReplicatedKernel` uses WalWriter, not this helper;
current benchmark flushes one block at a time and hides the defect. Fix or
remove the unused multi-block path deliberately; test encoded records, multiple
blocks and failed sync retry before trusting it for performance comparisons.

### A11 — MEDIUM security defect: existing token file permissions survive mint

`tools/mint-token.py` uses `os.open(...O_TRUNC, 0600)`. That creation mode does
not chmod an existing file. Offline mocked auth succeeded while an existing
token destination remained 0644. No real token/key was used. It also follows
symlinks; a secure destination policy is missing.

Fix: validated private directory, no-follow/exclusive temporary creation, 0600
before secret bytes, atomic replace, and failure cleanup; test existing modes,
symlinks, malformed responses and partial failures. `probe-market-data.py` passes
the bearer token in curl argv, so process-argument visibility is another avoidable
credential exposure. Avoid argv secrets, arbitrary auth origins and raw auth
error output. ADR 0010's token-cannot-trade premise is not proven for the direct-
submission MCP surface; least-privilege transport remains mandatory.

### A12 — HIGH evidence gap: gate pins do not cover the running dependency closure

Parent-recomputed historical manifest root is
`c62e995439e12d97b1683849e2cf675055c3b27bbeaa981b48cb78340c8c02f5`.
The scripts pin `543eb468…`; the NUC tag at `29c0e96` records `b241ad40…` and
explicitly excludes later changes. No current-tree NUC/Pi acceptance exists.
The manifest excludes shared storage/wire sources/headers and gate/patch inputs
despite linking or relying on them. It is a partial fingerprint, not an image seal.

Fix: hash the full build/runtime dependency closure, pin dependencies/patches,
toolchain/image/config/gate inputs, and compare the result to an independently
held expected value. Require metadata keys and a target inventory, fail if a
required suite did not run. Cache keys must include these inputs. Test a changed
shared storage file and a changed gate script both invalidate evidence.

### A13 — KNOWN unfinished work: no V2 process/security/venue boundary

CMake builds a ReplicatedKernel library and tests, not a startable V2 node.
There is no authenticated V2 command edge, real final-send owner, complete
venue response decoder, running external fence or deployment lifecycle drill.
CallerId's operator band is an authorization convention, not authentication.
`SetExecutorGeneration` alone supplies no proof the previous sender was fenced.
G-052–G-068 correctly remain open. Keeping them open is not a defect; describing
Phase 8/9 as deployment-complete would be.

True Markets MCP evidence does not establish REST absence, executable slippage
limits, complete identity/retry/settlement semantics or comparable fee economics.
ADR 0011 remains proposed. Do not use the inapplicable inert-prepare helper to
paper over those unknowns. Shadow development can proceed without live admission.

### A14 — MEDIUM evidence gap: compaction removes the history called the audit

ADR 0007 rejects extra emergency provenance partly because "the log already is
that record". `ReplicatedKernel::compact` discards covered entries from the
segment and legacy WAL; no archival handoff gates reclamation. A current-state
snapshot is not a history of all earlier commands, caller actions or reclaimed
orders. Thus replay recovery can be correct while historical explanations have
been lost. This is a source-traced retention gap, not another RED run.

Define separate state-recovery and audit-retention watermarks, with an immutable
verified archive before deleting the only historical copy. Bound archive backlog
and stop admission rather than silently shedding evidence. A second live replica
that independently compacts is not an archive. G-060/G-072 require that distinction
before the final pilot; it is not a reason to keep unbounded log maps in RAM.

## Positive findings and corrections to the prior review

- No new reachable mass-cancel reducer counterexample was found: capacity
  preflight and selected-order cancellation were inspected. A02 is in delivery.
- Reducer apply remains separated from venue I/O; the Raft adapter invokes it
  through applylog. This alone does not validate ingress, delivery or authority.
- Snapshot save precedes prefix reclamation; failed sync/order tests and local
  restore/suffix replay exist. Missing peer install is a separate failure.
- The durable-quorum patch includes both single- and multi-voter periodic
  reconsideration. Callback-null/current-term/follower-clamp tests pass. No new
  quorum-safety counterexample was found. Independent mutations were not rerun,
  so this is not a new mutation-certified safety proof.
- Quorum durability need not imply every replica is locally durable. A leader
  with two durable peers can satisfy a three-voter quorum without its own copy;
  tests must distinguish global quorum safety from stronger local invariants.
- Atomic replace is not inherently inferior to two slots. Test its actual failure
  model rather than rejecting it solely for a different implementation shape.
- A market-order helper in isolation has not executed a trade. Its unsafe
  lowering is a pre-integration blocker, not evidence of money already lost.

## Guarantee disposition

| Area | Disposition for this candidate |
|---|---|
| G-035–039 | Host replication/retry/partition/convergence tests; authority reads and ingress remain separate gaps; no current target gate |
| G-040 | Open: A04/A08; local restart is not replacement install |
| G-041 | Correct two-host failure-domain limitation; no HA claim |
| G-042,044–046,048,050–051 | Host mechanisms exercised; current target acceptance absent; A10 narrows helper coverage |
| G-043 | Open: A09 and process-wide bounded work A07 |
| G-047 | Open: missing hard-state persistence cut proof |
| G-049 | Open for membership/config part: A04; economic state snapshot coverage remains useful |
| G-052–060 including 057a | Open process/fencing/kill acceptance; A01/A02 are concrete composition failures |
| G-061–068 | Open venue contracts, exact lowering, secrets, evidence and controlled lifecycle |
| G-069–077 incl.074a | Operations/alerts/replay/archive/recovery/rollback still required; A12 is gate infrastructure work |
| G-078–086 | Strategy/shadow/canary remain future work; passive model test pass grants no trading authority |
| G-087–092 | Actual two-host envelope, saturation, priority, retry, layout and source-specific claims required |

The checked historical items are not current-tree target acceptance. The original
guarantees must be implemented or explicitly amended by the owner, not silently
weakened to finish a phase. See `docs/V2_NUC_PI_TRADING_ROADMAP.md` for the sequence.
