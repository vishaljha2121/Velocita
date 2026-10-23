# F4 proof composition — host evidence, 2026-09-22

CP-012 on branch `v2/phase-5-corrections`, base HEAD
`d44a4a964fc8c13279878e53d245ab56b921a824`; all remediation UNCOMMITTED.
Scope: approved ADR 0013 host-library composition, not a deployed live sender.
CP-011's canonical marker bytes remain unchanged. No economic schema, TradingState
or snapshot expansion, external contact, credential access, source freeze or tag.

## What changed, and why

The replication owner copies an exact historical BoundOrderEffect into a single
AuthorityProof only after a live matching current-term marker commits/applies.
Dequeue rechecks term, controls, effect deliverability and full binding. Replay
has no live subscriber and cannot produce proof. A full result slot refuses new
marker issuance but never blocks normal/control apply. Abandoning a subscriber
does not erase retained marker drain obligations or create an outcome receipt.

The gateway alone owns pending correlation and accepted permission. Its start
tick is local and retained across reply arrival; matching reply age is not a new
lease. A proof is an owned value with private construction; the noncopyable owner
consumes the private permission once. This avoids stale arena-reference identity
and accidental public-boolean authority, not malicious in-process callers.

Final dispatch checks delivery/retry/evidence/quota eligibility, exact prepared
value and known fences, samples time again after validation, consumes permission,
then records attempt uncertainty/quota before invoking transport. A thrown
transport error is not proof-unsent. A new proof cannot bypass an unknown outcome
or attempt budget. Committed cancels retain emergency capacity without a fresh
proof, but cannot bypass stop-all or conflicting observed controls.

`renew_authority` now always refuses without changing timestamps. The old pure
`evaluate_pre_send` API was renamed `evaluate_delivery_candidate`, with no old-name
alias: filtering reconstructs a candidate, not permission to emit bytes.

## RED provenance — not every increment was RED-first

| Evidence | Intended failure |
|---|---|
| `boolean-red.txt` | `local leader boolean still minted a fresh authority lease` on the old renewal implementation |
| `composition-stub-red.txt` | `fresh one-shot proof did not reach transport` with a fail-closed dispatch stub |
| First observed term during pending request | Console-only RED: `first observed term silently adopted during pending request`; permanent regression is in v2_gateway_authority_tests.cpp; no raw file was retained |

The initial producer and consumer implementations did not preserve a pre-fix
runtime RED. The mutation evidence below is **retrospective**, not relabeled as
original TDD. Existing CP-009 partition/delayed-boolean RED sources remain intact
as historical artifacts, not silently rewritten against the renamed API.

One agent temporarily mutated shared src/v2_replication.cpp while the integration
build was running. That created an intermediate `healthy committed marker yielded
no proof` result and stale object/source ordering. It is a verification race, not
an independently reproduced production defect. Source was restored and reviewed,
agents were frozen, main repeated mutations using isolated source copies/direct
compilation, then rebuilt the final tree. Only final logs here are acceptance
evidence; no concurrent-mutant build is counted as GREEN.

## Permanent observations and mutation sensitivity

The new composition suite reuses the replication suite's public-command cluster
fixture; it does not hand-edit balances, reservations or replicated state. The
positive oracle fixes price11, quantity2 and account5 and checks the callback's
canonical payload against the prepared bound intent. Existing payload golden
tests independently cover encoding; this composed comparison proves identity
preservation, not an independent proof that the encoder is correct.

Coverage includes one-voter lab and two-voter healthy send, no proof before durable commitment,
three-voter isolated old leader after majority kill, reply expiry, queued expiry,
expiry between first and final clock samples, inclusive bound, tick arithmetic
near UINT64_MAX, clock regression, wrong incarnation/sequence/historical binding,
known newer term/kill, direct double consumption, proven-unsent versus unknown
retry, exact attempt-budget boundary, emergency cancel and stop-all. Producer
tests cover multi-voter issuance, queued control/term invalidation, abandon,
restart/replay and result backpressure without blocking kill. Marker/storage
coverage and old-term drain availability remain in CP-011.

Direct source-copy mutation results are retained as `*-result.txt`. All compile
and run outside the repo against the restored Release archives; the corresponding
`.patch` files preserve each mutation. These patches are evidence, not changes
to apply to production source:

| Mutation | Required killed assertion |
|---|---|
| No producer issuance | committed marker did not yield one exact one-shot proof |
| Issue directly from admitted pending request | authority escaped before durable quorum apply |
| No dequeue revalidation | new controls failed to revoke the ready proof; separately higher term left queued authority consumable |
| No final clock sample | deadline edge/final sample regression test (second clock advances past bound) |
| Leave accepted permission reusable | second direct consumption reused the same proof |
| Bypass accept and consume freshness | late reply acquired fresh arrival-time authority |
| Bypass request equality | restart accepted another incarnation's proof |

This is targeted guard sensitivity, not exhaustive formal verification. Removing
one redundant quorum check is not necessarily observable because apply itself
requires commitment; no claim is made that every redundant branch was mutated.

Reproduction pattern (substitute one temporary mutated .cpp and suite):

```sh
c++ -std=c++20 -O1 -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  -Iinclude -Ibuild/_deps/willemt_raft-src/include \
  tests/v2_authority_composition_tests.cpp /tmp/mutant.cpp \
  /private/tmp/velocita-full-audit.lYB4P5/release/libvelocita_v2_replication.a \
  /private/tmp/velocita-full-audit.lYB4P5/release/libvelocita_v2_kernel.a \
  /private/tmp/velocita-full-audit.lYB4P5/release/libvelocita_v2_evaluator.a \
  /private/tmp/velocita-full-audit.lYB4P5/release/libvelocita_v2_contracts.a \
  /private/tmp/velocita-full-audit.lYB4P5/release/libvelocita_core.a \
  /private/tmp/velocita-full-audit.lYB4P5/release/libwillemt_raft.a -o /tmp/mutant-test
```

Producer mutations instead compile tests/v2_replication_tests.cpp and select
`authority-proof`, `authority-result-fence` or `authority-term-fence`. Use a unique
TMPDIR when running alongside regression: the replication fixture has a fixed
subdirectory. Never modify shared production sources for parallel mutations.

## Memory, complexity and availability

Measured host layout (cache-line configuration64): AuthorityProof416 bytes,
GatewayAuthority992, BoundOrderEffect224. Static ceilings512 and1536 bound the
first two independently of TradingState. Gateway state owns fixed slots and no
heap allocation. Producer retains one pending request and one result; no global
per-request map. Existing full binding comparison is fieldwise, not memcmp on
padding. Canonical bytes did not change.

Gateway correlation/age/consume are O(1). Final eligibility uses existing bounded
arena scans and planner; no new unbounded collection is added. Producer uses
existing outbox scans/materialization plus CP-011's O(retained suffix) drain scan.
Whole-node history/queue/work budgets remain F5. A 992-byte owner does not imply
that the whole running node is bounded.

Refused/expired final attempts drop permission; fresh authority costs another
durable round trip and marker. Lost results do not magically become deliverable.
Old-term drain may wait for an ordinary current-term command (no automatic no-op).
These reduce availability deliberately while preserving control service and
bounded outstanding F4 state. Do not retry unknown sends to improve throughput.

## Explicit limits tested or still requiring integration

- A proof is an ordered point, not a lock against future kill. The test issues a
  proof, partitions, commits kill on the majority, and allows one still-fresh send
  on the uninformed old side. Later observing kill stops new risk but cannot
  recall the callback or clear awaiting_response. This pins the approved bounded
  observation/in-flight limit rather than silently claiming immediate global kill.
- Dispatch inputs share one non-reentrant owner. Its clock callback may observe
  time but must not mutate authority/delivery/quota/state/control/catalog inputs.
  Arbitrary code can violate this contract; this API is not a sandbox.
- A callback that only enqueues unchecked work is not the final boundary. No real
  adapter, socket/IPC boundary or authentication is provided here. An arbitrary
  pause after final clock check prevents a hard venue-acceptance deadline claim.
- Tests inject incarnation and ticks. Production requires CSPRNG failure handling,
  suspend-aware monotonic time, measured nonzero bound, authenticated activation
  after fencing/reconciliation, membership-bound domain, and real transport.
- Zero configured freshness remains disabled by default. Neither a leader bit,
  telemetry page nor Grafana may enable trading. G-052..G-060 remain open for
  deployment; F5/F6, process/venue integration and NUC/Pi gates remain necessary.

## Final verification

Always rebuilt before CTest. Release **42/42**; Debug ASan+UBSan **42/42** with
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`. Existing Debug C++
flags are `-fsanitize=address,undefined -fno-omit-frame-pointer`; C flags are empty.
This is not a newly instrumented Raft C dependency or target-hardware gate.
Offline tools **40/40**, passive model **109/109** with bytecode writes disabled.
Independent required CTest inventory agrees: **42 total /39 V2**. Diff check clean.
The final inventory check parses the gate's explicit shell-array text and invokes
the validator with all 42 names. An earlier shell-sourcing attempt supplied only
the three non-V2 names and was correctly rejected; it is not counted as a pass.
Protected tracked paths remain clean and protected untracked model/cache paths
were preserved. Build/test logs are retained here; no source pin was regenerated.

Next: F5 identity/domain, bounded queues/history, safe snapshot catch-up and
archive-retention contract review before encoding. The roadmap now includes M3a
Grafana path diagnostics, subsystem/host health, freshness-aware alerts and
fault-drill evidence as a required pre-live gate, not dashboard-driven authority.
