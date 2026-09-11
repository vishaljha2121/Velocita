# Current development handoff

Updated: 2026-09-10

Purpose: compact working memory for the next agent session. Verify this file
against Git before acting; do not reconstruct the project from chat history.

## V2 PHASE 5 ACCEPTED ON ALL FOUR ENVIRONMENTS — 2026-09-11 (read this first)

| environment | Release | instrumented | ASan+UBSan |
|---|---|---|---|
| development host | 29/29 | - | 29/29 |
| ARM64 Linux container | 29/29 | 26/26 | 26/26 |
| physical Raspberry Pi 5 | 29/29 | 26/26 | 26/26 |
| x86-64 NUC | 29/29 | 26/26 | 26/26 |

Source root `b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4`
reproduced exactly on every one. Checkpoint `v2-phase5-accepted`. Record:
`docs/evidence/V2_PHASE5_GATEWAY_SIMULATOR_ACCEPTANCE.md`.

THE PHASE 4 PROVISIONAL-CAPACITY CAVEAT IS DISCHARGED. The NUC reproduced the
bounded layout byte-for-byte against ARM64 - `TradingState` 81,168 of 81,920, 752
bytes headroom, snapshot 65,259 of 65,536 - so the 80-KiB ceiling holds on both
deployment architectures, and the sixteen-conflict saturation bound of §10.5.3 is
architecture-independent. Neither machine in the ADR 0002 topology binds tighter.

FIVE GATE-LEVEL DEFECTS were found across the campaign and none were in the code:
a locale-dependent source root, a build target list drifted from `CMakeLists.txt`,
two gates using different container engines, a container image pinned to ARM64
that made the NUC gate unrunnable since Phase 3D, and a sanitizer whose coverage
silently differed by architecture. All are recorded with cause and structural fix.

NUC setup is documented in `docs/NUC_OPERATOR_RUNBOOK.md`, including the three
host-level prerequisites that are not obvious: cgroup `cpu` delegation for
rootless podman, system-scoped NetworkManager credentials for headless boot, and
Tailscale key-expiry disablement.

NEXT:

1. Phase 6 Raft integration planning. Read architecture §9.1.1 first: "committed"
   currently collapses to "applied", and Phase 6 must re-tighten it to genuine
   quorum commitment AND re-run the Phase 5 delivery gates against the stricter
   definition. Phase 5's guarantees are conditional on that until it happens.
2. Deferred functional work in `docs/V2_DEFERRED_FUNCTIONAL_SCOPE.md`, each item
   with a blast radius and a trigger. The one with a realistic trigger: policies,
   sources and caller cursors are never reclaimed, so sixteen `PublishPolicy`
   commands is a cluster lifetime budget.

## V2 Phase 5 acceptance detail — 2026-09-11

All three required gates passed at checkout
`04329fad05837e7f2c6e9d8d075ba8e5ede90700`, source root
`b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4`. The annotated
checkpoint is `v2-phase5-accepted`. Record:
`docs/evidence/V2_PHASE5_GATEWAY_SIMULATOR_ACCEPTANCE.md`.

| environment | Release | instrumented | ASan+UBSan |
|---|---|---|---|
| development host | 29/29 | - | 29/29 |
| ARM64 Linux container | 29/29 | 26/26 | 26/26 |
| physical Raspberry Pi 5 | 29/29 | 26/26 | 26/26 |

Pi power evidence: `throttled=0x0` before and after, 46.30 C to 50.70 C, governor
`ondemand`. Bounded layout reproduced byte-for-byte on ARM64/GCC 15.2:
`TradingState` 81,168 of 81,920, snapshot 65,259 of 65,536.

G-025 and G-026 are closed. G-074a remains open BY DESIGN and is now measured
rather than assumed: architecture §10.5.3 records that sixteen simultaneous
unresolved conflicts saturate the cluster, which makes operator escalation
load-bearing rather than a convenience.

NEXT, in order:

1. NUC validation, outstanding since Phase 4. It is the last environment the
   80-KiB state ceiling has not been checked on, and that ceiling is what makes
   the sixteen-conflict bound immovable - so this is a layout-and-capacity gate
   more than a functional one.
2. Phase 6 Raft integration planning. Before writing code, re-read architecture
   §9.1.1: "committed" currently collapses to "applied", and Phase 6 must
   re-tighten it to genuine quorum commitment and re-run the Phase 5 delivery
   gates against the stricter definition.
3. Deferred functional work in `docs/V2_DEFERRED_FUNCTIONAL_SCOPE.md`, each item
   carrying a blast radius and a trigger. The one with a realistic trigger:
   policies, sources and caller cursors are never reclaimed, so sixteen
   `PublishPolicy` commands is a cluster lifetime budget.

Three gate-level defects were found and fixed along the way, none of them in the
candidate: a source root that depended on ambient locale, a build target list that
had drifted from `CMakeLists.txt`, and two gates attesting the same candidate
through different container engines. All three are recorded in the acceptance
document, because a gate that only records its successes teaches nothing about its
own blind spots.

## Phase 5 physical Pi gate detail — 2026-09-11

`BUILD_JOBS=2 ./deploy/run-v2-phase5-pi-gate.sh --yes` passed on the physical
Raspberry Pi 5 at checkout `04329fad05837e7f2c6e9d8d075ba8e5ede90700`, source root
`b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4`. Release 29/29,
benchmark-instrumented 26/26, Debug ASan+UBSan 26/26, `throttled=0x0` before and
after, 46.30 C to 50.70 C. Full record in
`docs/evidence/V2_PHASE5_GATEWAY_SIMULATOR_ACCEPTANCE.md`.

The bounded layout reproduced byte-for-byte on ARM64/GCC 15.2: `TradingState`
81,168 of 81,920, snapshot 65,259 of 65,536. The sixteen-conflict saturation bound
is now a hardware fact, not a host measurement.

STILL OUTSTANDING before Phase 5 can be called accepted:

1. the standalone ARM64 container gate, for parity with the Phase 4 promotion
   rule - `BUILD_JOBS=2 ./deploy/run-v2-phase5-container-gate.sh --yes`. The Pi
   gate runs the same container and adds hardware evidence, so this is a parity
   run rather than new information, but the rule asks for three passes and this
   candidate has two;
2. the annotated checkpoint tag, once that passes; and
3. NUC validation, still outstanding from Phase 4 and now load-bearing, since the
   80-KiB ceiling is what makes the conflict bound immovable.

Two earlier gate attempts failed in the GATE rather than the code, and both fixes
were structural: a source root that depended on ambient locale (now `LC_ALL=C`
inside the recipe) and a hand-maintained build target list that had drifted from
`CMakeLists.txt` (now derived from `BUILDSYSTEM_TARGETS`). Both are recorded in
the acceptance document, because a gate that only records its successes teaches
nothing about its own blind spots.

## Phase 5 review outcome — 2026-09-10 (superseded by the section above)

Supersedes the review-request section below, which is retained for its risk
inventory. Codex was unavailable, so the review named there was performed in
session.

THE REVIEW FOUND THREE CORRECTNESS DEFECTS, all fixed in `c13c014`. They were
found by ENUMERATION, not by reading: `v2-lifecycle-reachability-tests` drives the
kernel breadth-first through every command it accepts and converges at 27
reachable lifecycle positions. Details in architecture §10.8.

1. A fill could erase a conflict. `apply_observe_order_event` refused everything
   from `conflict`; `apply_observe_fill` did not, so a later fill carried a
   conflicted order to `filled`, released its reservation, and removed it from the
   conflict projection.
2. A pending cancel could be reconciled away, marking it rejected and returning
   the order to live - silently abandoning an operator's intent.
3. A cancel could become permanently unsettleable when its order filled while it
   was in flight, pinning one slot in each of four arenas forever.

The tool now asserts the inventory permanently, asserts that `conflict` is a sink,
and asserts that no unsettled effect is a dead end.

GATE READINESS. Host evidence is recorded in
`docs/evidence/V2_PHASE5_GATEWAY_SIMULATOR_ACCEPTANCE.md`: Release 29/29, full
host Debug ASan+UBSan 29/29, model corpus 109/109. Both gate scripts exist and are
pinned to source root
`b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4`:

```sh
BUILD_JOBS=2 ./deploy/run-v2-phase5-container-gate.sh --yes   # ARM64 container
BUILD_JOBS=2 ./deploy/run-v2-phase5-pi-gate.sh --yes          # physical Pi
```

Neither has run. Phase 5 is NOT accepted and cannot be on host evidence alone.

DEFERRED WORK is now collected in `docs/V2_DEFERRED_FUNCTIONAL_SCOPE.md`, with a
blast radius and a TRIGGER for each item. The one to know about: policies,
sources and caller cursors are never reclaimed, so they are lifetime ceilings -
sixteen `PublishPolicy` commands per cluster lifetime, ever. Not a correctness
issue and not an optimisation either; it is the same availability defect §8.6
fixed for receipts, still open for three arenas. Parked for the MVP deliberately.

A capacity rebalance was investigated and REJECTED. The arenas look independent
in a size table and are not: venue event receipts live inside the request receipt
arena, that sub-budget rather than `kMaxClusterOrders` is the real limit on
concurrent orders, and the policy arena that looks like free space is an
unreclaimed lifetime budget. Reasoning in `V2_DEFERRED_FUNCTIONAL_SCOPE.md` §3.

## Phase 5 review handoff — 2026-09-10 (risk inventory, superseded above)

This section supersedes the Phase 4 "next task" text below for anything about
what to do NEXT. The Phase 4 acceptance record itself is unchanged and still
authoritative for Phase 4.

Purpose of this handoff: Phase 5 is CODE COMPLETE ON THE HOST AND UNREVIEWED. It
has never run on the target hardware. What is wanted is adversarial review and
analysis, not more features.

### Identity

- branch: `v2/phase-4-risk-orders-outbox` (Phase 5 work continues on it; a
  rename or a fresh branch is a reviewer call)
- Phase 5 base: `dd77e61420c4ff5428fcd409cebdfebbe545cc77` (Phase 4 accepted)
- HEAD at handoff: `fc21b17e00de6bf9a25399325f0f58cd49340cba`
- source root at HEAD, computed with the Phase 4 gate recipe:
  `d68f23ef5f9ab1ab594db3dec4cb2e0a2c2e654651c6ef75fba550d392479144`
  (computed on the development host with `shasum -a 256`; the gate recomputes it
  with `sha256sum` inside the container and the two must agree — treat a mismatch
  as unverified, not as a defect, until the gate has run once)
- no Phase 5 gate script exists yet. `deploy/run-v2-phase4-pi-gate.sh` is the
  template; a Phase 5 script needs the new root and its own evidence directory.

### Verification status

Host only. Release 28/28 and full host Debug AddressSanitizer + UndefinedBehavior
Sanitizer 28/28 at HEAD. Model corpus 109/109, untouched by Phase 5.

NOT DONE: ARM64 container gate, physical Pi gate, NUC validation. Phase 5 cannot
be accepted on this evidence.

### The nine commits, in order

| commit | what it settled |
|---|---|
| `4409ea3` | resolved five blocking contract contradictions in §9; added the pre-send guard |
| `2b0e847` | deterministic adversarial venue simulator, test-only by CMake construction |
| `4a4ee1a` | delivery planner and the Phase 5 gate; resend requires proof (§9.2.1) |
| `bd39053` | reviewed §10, built the gateway half of reconciliation |
| `f08bac0` | `order_reconciled` as the seventh observation kind (approved byte contract) |
| `c7b1034` | cancel path through reconciliation; four findings |
| `da87f62` | cancel redelivery without proof (approved amendment to §9.2.1) |
| `f482bfa` | measured retention horizons; discharged §9.1.7 |
| `fc21b17` | snapshot coverage for Phase 5 states; corrected two impossible fixtures |

All reasoning is in `docs/V2_ARCHITECTURE_CONTRACTS.md` §§9.1–9.3 and §§10.1–10.7.
Read those before the code; every non-obvious choice is argued there with the
alternative that was rejected and why.

### WHAT TO REVIEW HARDEST

Ordered by how likely I think they are to be wrong.

**1. The applicability matrix in `apply_observe_order_event`.** This is the single
highest-risk area. It is now a large interacting set of booleans —
`reconciled_submit`, `reconciled_cancel`, `cancel_reconcilable`, `cancel_agrees`,
`cancel_contradicts`, `late_cancel_completion`, `reconciliation`, `terminal`,
`effect_resolved` — feeding a six-term guard disjunction with negations. I believe
each term individually; I am not confident about every interaction. The review
that would actually settle it is a full truth table over (observation kind) ×
(order state) × (effect state) × (effect operation), checking each cell against
the contract rather than against the code. If a cell is wrong, this is where.

**2. Three accepted Phase 4 behaviours changed.** Each needs an independent "is
this right?" pass, not just "is it consistent?":
  - a rejected cancel now moves the order to `reconciling`, not `live` (§10.3.6).
    Justification: a venue refuses a cancel for "already cancelled", "already
    filled" and "no such order" as readily as for "still working", and once
    cancels may be redelivered a rejection of the duplicate is the common case.
  - `fill_quantity_conflict` now marks the order `conflict` instead of refusing
    silently (§10.5.5). It mutates on a refusal path, which has precedent in the
    same function but is still worth challenging.
  - Phase 4's outbox-reuse test now reconciles between a rejected cancel and a
    fresh one. The arena-pressure guarantee is unchanged; the path is longer.

**3. The asymmetric disagreement rule (§10.3.4).** I got this wrong first — both
directions as conflict — and Phase 4's cancel/fill/final permutation test caught
it. The corrected rule: a venue reporting MORE than the cluster booked is missing
evidence (refuse, let it settle); reporting LESS is a conflict (no future evidence
reduces a booked fill). Challenge the "above" case specifically: if the fills are
genuinely lost the order stays unresolved forever rather than becoming a conflict,
and I argued unresolved-and-recoverable beats terminal. That argument deserves a
second opinion.

**4. `order_reconciled` is overloaded.** The same kind means "the submit arrived"
against a submit effect and "the cancel did not take" against a cancel effect. I
think this is defensible — the observation states one fact and what it resolves
depends on what was in doubt — but it is the kind of overload that reads fine
until someone adds a third case.

**5. The harness is unverified test code that found three bugs in itself.** It
leaked the simulator's ground truth into the gateway's `authoritative` flag;
it conflated "the venue answered" with "the cluster accepted the answer"; and its
identity gate was scoped wrongly once cancels existed. Treat the ~900 lines of
`tests/v2_gateway_delivery_tests.cpp` as suspect in the same way as the code it
tests. Two fixtures in it built states the kernel cannot reach, and passed.

**6. The gateway is not a process.** Everything is pure functions plus an
in-process shell. No transport, no socket, no credential isolation, no Raft.
§9.1.5 records that as the plan; nothing has been proven about the real boundary.

### Known and deliberate gaps

- **The simulator's faults do not compose.** `choose_fault` returns ONE fault per
  operation by first match, so partial-fill-plus-loss, reordered fills, and late
  duplicates are all unreachable. Consequence: fills never arrive out of order
  end to end, and the matrix cannot produce a reconciliation conflict. Covered by
  direct unit tests instead. Closing it means splitting `Fault` into its three
  actual axes — transport per message, venue behaviour per operation, integrity
  per answer — and re-measuring §10.5, because every seed changes.
- **Sixteen simultaneous unresolved conflicts saturate the cluster** (§10.5.3).
  Correct and fail-closed, but it makes the G-074a operator path load-bearing.
  The capacity cannot be raised: `TradingState` is 81,168 of 81,920 bytes and one
  more decision record costs 704 of the 752 remaining.
- Native amend and overlapping replacement remain disabled, as in Phase 4.
- A venue-initiated cancellation still cannot be reported: the existing
  `cancelled` kind requires a cancel effect the cluster may never have sent. This
  is pre-existing, not introduced by Phase 5.

### Reproduction

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
ctest --test-dir build --output-on-failure          # expect 28/28

cmake -S . -B build-v2-sanitized -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-v2-sanitized -j8
ctest --test-dir build-v2-sanitized --output-on-failure   # expect 28/28

VELOCITA_HORIZON_REPORT=1 ./build/v2-gateway-delivery-tests   # §10.5 tables
VELOCITA_MATRIX_TRACE=1   ./build/v2-gateway-delivery-tests   # per-seed matrix

cd model && PYTHONPATH=src python3 -m unittest discover -s tests  # expect 109
```

Always build before `ctest`: `ctest` does not build, and this project has already
produced one false pass from a stale binary.

### Next tasks, in the order I would do them

1. **Review and analysis** — this handoff's purpose. Nothing below should start
   until the items above have had an adversarial pass.
2. **Phase 5 gate scripts and target validation** — ARM64 container, then the
   physical Pi, using `deploy/run-v2-phase4-pi-gate.sh` as the template with the
   new source root. This is what Phase 5 acceptance requires and it is the largest
   remaining unknown.
3. **NUC validation**, still outstanding from Phase 4 and now more load-bearing,
   because the 80-KiB state ceiling is what makes the sixteen-conflict bound
   immovable.
4. **Compose the simulator's faults**, then re-measure §10.5.

### Protected uncommitted paths

`model/`, `docs/README.md`, `docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`,
`docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`, and
`docs/adr/0002-two-machine-pi-nuc-deployment-topology.md` are intentionally
uncommitted. Do not `git add .`; commit by explicit path only. This rule exists
because a directory-wide add already swept these into a Phase 4 commit once.

## Phase 4 accepted checkpoint — 2026-09-10 (authoritative)

Phase 4 is accepted. The annotated checkpoint is `v2-phase4-accepted`; the
tested checkout was `9f61247645140a544a4db6bba1ba1d62dcacb080`, and the
accepted source root is
`a84609b54b2efe408f34151b91bbd10a217d8cdadbd7b18aba6a54c6183ac853`.
The acceptance record is
`docs/evidence/V2_PHASE4_RISK_ORDERS_OUTBOX_ACCEPTANCE.md`.

Host Release and full ASan+UBSan passed 25/25. ARM64 Ubuntu passed Release 25/25
and both V2 matrices 22/22. The physical Pi gate passed the same matrices with
the exact source root, `throttled=0x0` before and after, and 44.10 C to 50.15 C
temperature evidence at
`~/.local/share/velocita/v2-phase4-acceptance-pi-gates/20260910T152924Z`.

G-010 through G-024 are closed. G-025/G-026 remain Phase 5 transport work, and
G-074a remains the explicitly accepted fail-closed operational gap owned by
Phases 8 and 10. No Phase 5 implementation has started. Before Phase 5 code,
review its simulator/gateway command, delivery, retry, reconciliation, fencing,
and emergency-lane contracts; do not infer them from Phase 4.

## Codex acceptance-correction checkpoint — 2026-09-10 (read this first)

This section and architecture §§8.4-8.7 supersede the earlier §8.7 review pause
and older next-task text below. The user approved all ten audit corrections. The
working parent was `365b570` on `v2/phase-4-risk-orders-outbox`; verify current
HEAD and status because this checkpoint may be committed after it is written.

Implemented by permanent RED-to-GREEN tests:

1. authorization requires the current active policy/executable/instrument/venue
   contract and rejects an older same-slot proposal once a newer actionable
   decision exists;
2. cancel effects refuse submit-only `order_acknowledged`, and a repeated
   semantic `effect_unknown` is a non-mutating `already_applied` result;
3. applied fill/event results populate the existing candidate-V5 `DecisionId`.
   This zero-byte owner link makes their receipts reclaimable with a resolved
   order group; 119 completed cycles cross the old 64-event lifetime ceiling;
4. authorization reserves worst finite observation capacity as remaining
   one-atom fills plus state-sensitive submit/cancel/reconciliation transitions.
   It also reserves one cancel outbox slot per unresolved order, limiting the
   current 16-effect arena to at most eight newly live orders;
5. a definitively rejected cancel record can be reused at a strictly increasing
   effect generation. Pending/unknown/reconciling/conflict/incomplete records are
   never overwritten;
6. caller cursors register atomically on first admission with a zero expired
   prefix. Snapshot validation bounds the arena and rejects zero/duplicate caller
   IDs. The contract now states the real conservative-prefix semantics and the
   monotonic per-stable-caller requirement;
7. successful observation results require complete owner/stream identity, while
   malformed typed refusals may retain partial non-empty diagnostic detail. An
   admitted invalid fill is now result-encodable and snapshotable;
8. metrics use the exact effect authorization receipt for age, explicitly name
   reconciliation age as since authorization, expose venue/caller identity
   headroom, and classify derived/fill-quantity collisions as conflicts;
9. the Phase 4 property corpus now executes sell accounting, same- and
   cross-request duplicates, conflicting observation identities, seed/step
   diagnostics, and a minimal independent signed-position ledger; and
10. fill evidence is scoped to a submit effect. A partial fill resolves an
    unknown submit, but a historical-submit fill does not erase an unknown
    cancel; a fill that names a cancel effect is refused before economics move.

Candidate V5 bytes did not grow. Canonical VALUES changed because caller cursors
are present from first admission and applied observation results now carry their
owning DecisionId. Old unaccepted development V5 snapshots that omitted that
owner are unsupported. Reviewed replay goldens were updated only after direct,
snapshot-suffix, and independent-model bytes agreed.

Candidate source commit `db13a9c19caf7d5ac67ce1117b78f6302b6bcd3e`
has Phase 4 source root
`a84609b54b2efe408f34151b91bbd10a217d8cdadbd7b18aba6a54c6183ac853`.
Its full Release build and 25/25 CTest suites pass; its full Debug ASan+UBSan
build and 25/25 suites also pass; `git diff --check` passes. Reviewed Phase 4
ARM64-container and physical-Pi wrappers now pin that root, and
`docs/evidence/V2_PHASE4_RISK_ORDERS_OUTBOX_ACCEPTANCE.md` records the promotion
contract without claiming pending target results. The ARM64 container gate is
now PASS at
`~/.local/share/velocita/v2-phase4-container-gates/20260910T151843Z`:
Release 25/25, instrumented V2 22/22, and Debug ASan+UBSan V2 22/22 on Ubuntu
26.04 ARM64/GCC 15.2 with the expected source root. The physical Pi gate also
passed at
`~/.local/share/velocita/v2-phase4-acceptance-pi-gates/20260910T152924Z`:
Release 25/25, instrumented V2 22/22, Debug ASan+UBSan V2 22/22, exact source
root, and `throttled=0x0` before/after. The authoritative accepted checkpoint
above supersedes this development checkpoint.

Preserve the same protected unrelated paths listed near the end of this file:
`docs/README.md`, `docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`,
`docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`,
`docs/adr/0002-two-machine-pi-nuc-deployment-topology.md`, and `model/`.

## Phase 5 started — 2026-09-10 (read after the Phase 4 sections)

Phase 4 is accepted (`v2-phase4-accepted`). Phase 5 is venue simulator and
execution gateway.

### Contracts were reviewed BEFORE code, and that was necessary

Sections 9 through 15 are the primary gateway contract and, unlike 8.1 to 8.7,
had never passed an approval gate - the document header still reads "proposed".
A review found thirteen contradictions or gaps in them. Five blocked the first
line of code and are now resolved and recorded in architecture §9.1, with
reasoning:

1. §9.1.1 "committed" collapses to "applied" until Phase 6, because Raft
   integration is Phase 6 and the phrase had no referent in the interim.
2. §9.1.2 `claimed` and `attempted` are gateway-local, resolving a direct
   contradiction between §9's lifecycle diagram and §8.2. No command writes them;
   putting them in consensus would add a quorum round trip to every send.
3. §9.1.3 expiry IS control-generation staleness. §9 and G-029 required checking
   an "expiry" that `OrderEffect` has no field for. Defining it this way deletes
   a phantom field rather than adding a real one, and keeps wall-clock out of the
   authority path.
4. §9.1.4 lowering is representation only, never economics. This was the largest
   under-specified permission in the contract: nothing distinguished permitted
   "lowering" from the forbidden "economically modify". Rounding, TIF
   substitution, and post-only downgrades are now explicitly forbidden; a venue
   that cannot represent exact intent gets a refusal, not an approximation.
5. §9.1.5 the gateway is a separate process in deployment, tested in-process with
   the transport interface as the seam, because G-066 credential isolation is not
   achievable inside a process that also runs the reducer and its fixtures.

§9.1.6 records the shaping decisions that did not block: emergency lane built but
unclaimed until Phase 8, authority-lease deferred to Phase 8, send-then-reconcile
rather than record-before-send, crash scoped to GATEWAY crash since nothing is
durable until Phase 7, simulator test-only, virtual time everywhere, and the five
parked retention horizons not blocking Phase 5.

§9.1.7 records an open obligation inherited from Phase 4: §3 requires retention
to exceed the retry and reconciliation horizons, §8.6/§8.7 made retention finite,
and §8.4 says those horizons are unknown until Phase 5. Phase 4 was accepted with
a retention policy that cannot yet be SHOWN to satisfy §3. Discharge it by
measuring what the simulator produces; if the numbers do not fit, the capacities
move, not the invariant.

§9.1.8 records a trap: `include/velocita/v2/wire.hpp` has a `VLT2` envelope with
`MessageKind::order_effect`, `observe_fill`, `apply_reconciliation` and friends.
Nothing maps them onto `KernelCommandKind`, the numbering does not correspond,
and the only callers are Phase 1 tests and a benchmark. It is not a transport.

### First increment: the pre-send guard (G-029), complete

`include/velocita/v2/gateway.hpp`, `src/v2_gateway.cpp`, target
`v2-gateway-tests`. Release 26/26 and full host Debug ASan+UBSan 26/26.

`evaluate_pre_send` is a PURE FUNCTION: reads no clock, performs no I/O, mutates
nothing, and is deterministic on repeated calls. This follows
`OwnerLoopScheduler` from V1.1, which already takes `now` as a parameter rather
than reading a clock - that precedent is the whole reason Phase 5 can keep the
project's testing discipline while owning a clock and a transport.

A permitted decision CARRIES the materialized effect; a refused one carries
nothing. A caller therefore cannot send a refused effect: the payload is simply
absent rather than present-but-forbidden.

Refusal ordering is fixed and the reasons are documented in `v2_gateway.cpp`:
identity and deliverability, then payload reconstruction, then control authority
(generation before kill epoch), then venue capability, then quota. Control
precedes capability so a stale effect reports as stale even on a venue that could
not have taken it. Quota is last because it is the only "not yet" refusal; every
other one means "never".

Only `pending` is deliverable. `unknown` and `reconciling` are deliberately NOT,
because ADR 0001 requires reconciliation before resending an ambiguous effect and
delivering one straight from this guard would be the blind retry G-026 forbids.

Next: the venue simulator's deterministic core - virtual clock, seeded fault
model covering the nine G-033 classes, and the crash cut points TDD §4.5 names.
Then the delivery loop shell, then reconciliation.

## Claude takeover checkpoint — 2026-09-09 (historical)

This section and architecture contract §8.1 supersede older next-task/scope/schema
statements later in this file. Later dated sections retain development history;
do not restart B1 or restore the removed native-replacement implementation.

The CURRENT WORKING TREE, not HEAD alone, is the development checkpoint.
HEAD remains `aedd658ee0a7fe4e30a2338d2f953fc659306e29` on
`v2/phase-4-risk-orders-outbox`. No takeover commit/tag was created. Preserve and
inspect the existing diff; resetting to HEAD would lose the completed increments.

Completed after HEAD, all uncommitted:

1. Approved scope correction: safe SERIAL replacement is a Phase 4 acceptance
   requirement. Native amend and overlapping replacement remain disabled. Refusal
   alone does not satisfy G-018; the full cancel/reconcile/fresh-authorize lifecycle
   needs proof. Covering two real exposures is not erroneous double-counting.
2. Reclamation identity fix: retained evaluation/authorization receipts prevent
   decision reuse and recover the original submit result after payload compaction.
3. Snapshot accounting fix: checked per-account sums, reservation ownership and
   transactional rejection of inconsistent snapshots; permanent regressions.
4. Approved names: `max_order_gross_notional` (single-order gross notional) and
   `max_reserved_exposure` (aggregate margin + fee hold budget).
5. B1 implemented: full `ConfigureRiskLimits`, command kind 17, 88-byte body,
   strict expected RiskVersion and kernel-selected +1, matching kill/executor
   generations, no economic-balance overwrite, prospective caps, existing-hold
   coverage and quiescent rate/policy updates. See architecture §8.1 for the
   complete approved contract and future user-doc reference.
6. Approved kernel command/result and snapshot schemas V5; V4 rejected, no
   implicit migration. Result detail tag 5 retains scope and resulting RiskVersion.
   OrderEffect unchanged; state 81,024 bytes, max snapshot 65,121 bytes.

Phase 4 paths with existing uncommitted work (preserve all; distinguish from the
unrelated protected list below):

```text
CHANGELOG.md
docs/CURRENT_HANDOFF.md
docs/V2_ARCHITECTURE_CONTRACTS.md
docs/V2_END_STATE_GUARANTEE_CHECKLIST.md
include/velocita/v2/kernel.hpp
include/velocita/v2/kernel_codec.hpp
include/velocita/v2/state_snapshot.hpp
src/v2_kernel.cpp
src/v2_kernel_codec.cpp
src/v2_state_snapshot.cpp
tests/v2_kernel_codec_tests.cpp
tests/v2_model_replay_tests.cpp
tests/v2_replay_tests.cpp
tests/v2_risk_order_outbox_tests.cpp
tests/v2_state_snapshot_tests.cpp
```

Last verified after final source/test edits: Release 23/23; focused host Debug
ASan+UBSan 5/5; `git diff --check` clean. Subsequent takeover edits are docs-only.
Reproduce with:

```sh
cmake -S . -B build-phase4 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-phase4 -j2
ctest --test-dir build-phase4 --output-on-failure
cmake --build build-phase4-review-sanitized --target v2-risk-order-outbox-tests v2-state-snapshot-tests v2-kernel-codec-tests v2-replay-tests v2-model-replay-tests -j2
ctest --test-dir build-phase4-review-sanitized -R 'v2-(risk-order-outbox|state-snapshot|kernel-codec|replay|model-replay)-tests' --output-on-failure
```

The existing sanitizer build is Debug with `-fsanitize=address,undefined
-fno-omit-frame-pointer` in CXX flags and `-fsanitize=address,undefined` in linker
flags. It reuses `build-phase4/_deps/aeron-src` and
`build-phase4/_deps/willemt_raft-src` through CMake FetchContent source overrides.
These are focused host checks, NOT full Phase 4 acceptance or pinned ARM64/Pi.

### Serial-replacement acceptance progress — 2026-09-09 (Claude)

`test_serial_replacement_across_a_risk_limit_update` implements the first
approved slice of the serial-replacement acceptance matrix, in eight legs:
rate/policy change refused while the cancelled order still holds risk; aggregate
cap below the retained hold refused; prospective per-order cap accepted
mid-cancellation without rewriting balances, position version, order state, or
historical effect bytes; stale-version successor refused; venue cancellation not
releasing risk; final reconciliation releasing the residual; freshly evaluated
successor authorizing against the new configuration; and every effect in the
lifecycle reconstructing byte-for-byte across BOTH snapshot restore and canonical
command replay, with the replayed snapshot byte-identical to the direct one.

Legs 1-7 passed on first run. They are acceptance coverage for behavior already
implemented correctly, not bug fixes, and are reported as such rather than as
fabricated RED.

Leg 8 produced a GENUINE RED: `canonical replay changed historical effect bytes`.
The cause is a replay-fidelity property worth stating explicitly, because it will
recur in the Phase 4 model corpus and in any future replay harness:

  `materialize_order_effect` sets `committed_directive_index` from the retained
  authorization receipt's `state_version`. Per architecture §8.1 an admitted
  REFUSAL also retains a receipt and advances the global StateVersion. A replay
  that omits refused commands therefore reaches authorization at a different
  StateVersion and reproduces different canonical effect bytes.

  Consequence: a faithful replay must re-drive every admitted command, refusals
  included. Refusals are part of history, not filler between the interesting
  commands. The original defect was in the test harness, not the kernel, but a
  Phase 4 model corpus that replays only successful commands would report a false
  divergence - or worse, mask a real one.

`evaluate_new_decision` now takes an optional command sink so a caller can
replay a lifecycle faithfully; it previously applied two commands invisibly.

Verified after this increment: Release 23/23; focused host Debug ASan+UBSan 5/5.

### Reclamation inside the cancel/reconcile window — 2026-09-09 (Claude)

`test_reclamation_during_cancellation_preserves_the_recovery_path` produced a
GENUINE RED and exposed an availability defect, now fixed.

An order can be terminal AND risk-resolved while one of its own effects never
received evidence. Observed state: order `cancelled`, reservation `released`,
cancel effect `acknowledged`, submit effect still `pending`. Once the order is
closed no future observation can be scoped to that submit effect, so it was
ORPHANED - yet reclamation required every effect settled, making the group
permanently unreclaimable. With the decision arena full of unauthorized
decisions, evaluation failed closed and could not recover even after the stuck
slot reconciled. Wedged, not degraded.

Fix, approved as option A and specified in architecture §8.2: on DEFINITIVE
evidence only, orphaned effects settle to `OutboxState::incomplete`, an enum
value that already existed and was never set. `incomplete` counts as settled for
reclamation. It is deliberately not `acknowledged`, which would claim evidence
the venue never gave.

Deliberately excluded: the full-fill path. A cancel in flight when a fill lands
is still owed a venue response, so that effect is awaiting evidence rather than
orphaned. Settlement runs only where `releases` is already true - final cancel
reconciliation, or a rejected submit the venue never accepted.

`effect_is_settled` and `order_is_risk_resolved` are now shared helpers, because
reclamation and settlement must agree on what "resolved" means or one will free
capacity the other still considers live.

`TradingState::settled_incomplete_effect_count` is a new monotonic counter in the
snapshot header. It grew the V5 empty payload by eight bytes, which falsified the
guard asserting V5's empty payload matched V4's; that guard is re-purposed to pin
the empty-payload SIZE, which is what unreviewed header growth actually moves.
Empty-state, replay and generated-model goldens were re-pinned for development V5.

Layout: `TradingState` 81,032 bytes (888 headroom), maximum canonical snapshot
65,129 bytes (407 headroom). Release 23/23; focused host Debug ASan+UBSan 5/5.

Phase 8 now carries G-057a: reclaiming an `incomplete` effect makes its bytes
unreconstructable, so a late gateway send against a closed order must be stopped
by executor-generation and kill-epoch fencing plus stable `EffectId` idempotency.
Phase 4 relies on that and it is a Phase 8 acceptance requirement.

### Serial-replacement matrix completed — 2026-09-09 (Claude)

`test_risk_configuration_respects_every_unresolved_order_state` pins the §8.1
rule against all four unresolved states: `cancel_pending`, `submission_unknown`,
`reconciling`, and `conflict`. For each it proves a rate change and a bare
policy-version bump are refused with `risk_reconfiguration_blocked` and mutate
nothing, a prospective per-order cap change is accepted without revaluing the
live hold, and an aggregate cap below the retained hold is refused rather than
clamped. It then proves rates become configurable once the hold is released,
without disturbing the filled position. `submission_unknown` is driven by a real
`effect_unknown` observation; `conflict` still has no producer and is constructed
directly, so the rule is pinned before B4 supplies one.

Why this needed pinning even though the behavior was already correct: the
implementing predicate keys on the reservation not being released, which is the
right axis because risk is what the rule protects, but it means the states §8.1
names are only covered transitively. A future refactor to a lifecycle-state list
would silently drop whichever state it forgot.

`test_two_sided_slots_requote_independently` is the first fixture in the suite
that quotes two sides. `two_sided_policy` and `two_sided_executable_policy` add a
second ordered rule bound to a second template on slot 2 with the sell side, so
first-match steers a rich feature to the buy template on slot 1 and anything
above the floor to the sell template on slot 2. Prices are kept small
deliberately: the shared instrument profile caps price at 100 ticks and per-order
gross notional at 4,000, and notional is price * quantity * 100.

It proves the two slots never contend, each slot's exclusivity is enforced
independently, both holds are summed rather than netted, cancelling and
reconciling one side leaves the other's state, pairing and effect bytes
untouched, only the reconciled side's risk is released, the freed slot requotes
while the other still rests, and the whole interleaving survives snapshot
restore. It also separates the two idempotency paths: a same-identity retry
returns the original result verbatim through receipt deduplication, while a
different request re-authorizing a consumed decision returns `already_applied`
with the retained order and effect identities.

#### Fixture hazard for review: a retained decision outlives its policy

Flagged for Codex review because it produced a convincing false positive and the
general shape will recur.

`ready_state()` EVALUATES `DecisionId{20}` as part of building the fixture. Any
helper that layers a different policy on top of `ready_state()` - as
`two_sided_state()` does - leaves that decision resolved against the OLD policy.
Its retained proposal is frozen at evaluation time, so it keeps the old policy's
side, slot, price and quantity no matter what the new bindings say.

What it cost here: the first draft of `test_two_sided_slots_requote_independently`
authorized the inherited `DecisionId{20}` as its "ask". That decision had been
resolved against the single-rule policy and targeted slot 1 buy, so both quotes
landed on slot 1 and the second was refused `order_slot_occupied`. The failure
was indistinguishable from a genuine slot-exclusivity defect - two orders that
should occupy different slots contending for one - and the obvious next step
would have been to "fix" the exclusivity predicate. There was nothing wrong with
it. Printing the resolved `order_slot` and `side` of both proposals is what
settled it: both read slot 1, side buy.

What it could cost elsewhere, and why this is not merely a test-hygiene note:

- a test that swaps policy mid-fixture is exercising YESTERDAY's policy while
  reading today's, so it can report a pass for behavior that was never executed,
  which is the more dangerous direction;
- the same shape appears in any future fixture that stages or activates a policy
  after `ready_state()`, including executable-policy rebinding tests; and
- a generated model corpus that mixes policy activation with decision evaluation
  must order them deliberately, or the model and the kernel will disagree about
  which policy a decision was resolved under and the divergence will be blamed on
  the reducer.

This is correct kernel behavior, not a bug: a decision record must retain the
proposal it was evaluated with, because that is what makes decisions replayable
and attributable. The hazard is entirely in fixture construction.

Mitigations in place: `two_sided_state()` documents the constraint at its return
point, and `evaluate_at_feature()` exists so a caller can evaluate its own
decisions after the swap at a chosen feature value. A stronger guard - having
`ready_state()` not evaluate a decision at all, or having policy-swapping helpers
assert `decision_count == 0` - was NOT implemented, because changing
`ready_state()` touches every test in the suite. Worth considering when the Phase
4 model corpus lands, since that is the next place the ordering will matter.

Both tests are acceptance coverage: no kernel defect was found by either.
Release 23/23; focused host Debug ASan+UBSan 5/5.

### Independent Phase 4 property corpus — 2026-09-09 (Claude)

`tests/v2_phase4_property_tests.cpp`, target `v2-phase4-property-tests`. 24
seeds, 220 steps each, currently 2,312 of 5,264 generated commands admitted.

Approach and why. The Phase 4 gate is stated as a PROPERTY - fault/property tests
must not be able to produce an unreserved effect, a duplicate economic mutation,
a negative reservation, or a replay mismatch - so this corpus checks properties
derived from the CONTRACTS after every command rather than agreeing with a second
implementation. A hand-written mirror catches wrong-but-self-consistent behavior
that invariants miss, which is real, but it is also the approach most at risk of
transcribing the reducer including its bugs. The Phase 1-3 mirror in
`v2_model_replay_tests.cpp` remains the reference for that style; this file
deliberately takes the other side so the two fail differently. A Phase 4 mirror
model is still worth adding later and is NOT superseded by this.

Leverage worth knowing: `encode_state_snapshot` runs the full structural
validator, which already enforces per-account conservation - reserved totals must
equal the sum over that account's reservations - plus reservation ownership,
non-negativity and index bounds. Snapshotting after every command is therefore
itself a dense invariant check. The corpus adds what the validator cannot see:
no effect outliving its cluster order, per-record reservation arithmetic,
cross-command monotonicity of both accounting counters, reclamation being
economically inert, and byte-identical canonical replay of the entire history.

Phase 4 state is created only by commands - the risk account through B1
`ConfigureRiskLimits` creation, decisions through real source admission and
evaluation - so the corpus cannot hand-seed a state the kernel would never
produce. Only Phase 1-3 scaffolding is built directly.

Coverage is asserted, not hoped for: the run fails unless it exercised
authorization, partial fill, full fill, cancel, reconciliation, unknown
submission, configuration, reclamation, orphan settlement and refusals. A vacuity
guard additionally requires at least a third of commands to be admitted, so a
future change that exhausts an arena early cannot silently turn later steps into
no-ops. The threshold is deliberately loose rather than tuned to the current
ratio, which would make it a change detector instead of a vacuity guard. A high
refusal rate is wanted: the generator aims commands at stale decisions, released
reservations, occupied slots and closed orders on purpose.

Three fixture defects were found and fixed while building it, all in the corpus
rather than the kernel, and each is a hazard for any future hand-built state: a
source cursor with a zero sequence is invalid, so the corpus lets the first
`RecordSourceEvent` establish it; an executable-policy bundle records the
StateVersions at which it was staged and activated and both must be at or below
the current version, so a bootstrap sitting at version zero describes a history
that never happened; and asserting `replacement_not_supported` specifically is
wrong once the 256-receipt arena fills, because admission then refuses on
capacity before dispatch reaches the replacement handler - the property is that
replacement never succeeds, not that it always fails the same way.

No kernel defect was found by the corpus. Release 24/24; focused host Debug
ASan+UBSan 6/6, the sanitized build reconfigured to include the new target.

### Unauthorized-decision retention — 2026-09-09 (Claude)

Implemented per architecture §8.4. Two rules now make retained decisions
reclaimable: structurally non-authorizable records, whose authorizability is
decidable from immutable fields alone and therefore needs no horizon; and
superseded records, where a newer actionable proposal exists for the same
account, instrument, venue and logical order slot. Ordering is by array position,
never `DecisionId`, because position is arrival order and is canonical under
stable compaction.

The blocker recorded in an earlier session - that reclaiming a decision would
narrow `DecisionId` conflict detection - was ALREADY SOLVED by the
reclamation-identity fix: `apply_evaluate_decision` falls back to scanning
applied evaluation receipts, which outlive the decision arena and retain the
DecisionId, digest and outcome. No new contract was needed for it.

A structural change to the sweep was: it previously returned early when no order
GROUP was reclaimable, which is exactly the case that matters here - an arena
full of no_action records has zero reclaimable groups. Decisions are now assessed
independently of order-group reclamation.

Knock-on effect worth understanding, because it invalidated two existing tests'
premises rather than their intent: the decision arena can no longer be filled by
unauthorized decisions at all, since at most one survives per slot plus those
referenced by live orders. The wedge scenario that
`test_reclamation_during_cancellation_preserves_the_recovery_path` was built to
exercise is now structurally unreachable. That test was rewritten to assert the
STRONGER property - a stuck order cannot wedge evaluation - rather than the
earlier "evaluation halts but can still recover".
`test_conflicted_orders_are_never_swept` was rebuilt to take sweep pressure from
the ORDER arena using the two-sided fixture, which is the more realistic source
anyway.

`observe_fill` gained side and price parameters. A fill whose side disagrees with
the resolved proposal is refused as not applicable, which is correct but reads as
a mysterious fixture failure on a two-sided book.

Implementation hazard for review: `decision_is_authorizable` mirrors the
predicate in `apply_authorize_decision`. If that gains a condition and the mirror
does not, the sweep will reclaim records the authorizer would still have
accepted. There is no compiler check; they must change together.

Deferred to a later version and recorded in §8.4: a state-version horizon for any
unreferenced decision. Deterministic and strictly more general than supersession,
but N cannot be justified until Phase 5 supplies a venue simulator and the
gateway retry horizon becomes measurable. Revisit alongside the reference-horizon
question already deferred from the reclamation contract.

No schema or layout change was required: `TradingState` remains 81,032 bytes and
the maximum canonical snapshot 65,129. Release 24/24; focused host Debug
ASan+UBSan 6/6.

### Phase 4 metrics — 2026-09-09 (Claude)

`include/velocita/v2/metrics.hpp`, `src/v2_metrics.cpp`, target
`v2-metrics-tests`. Specified in architecture §8.5.

`collect_metrics` is a pure function of `TradingState`: no new field, no digest
participation, no clock. Ages are STATE-VERSION distances, not durations, because
apply may not read a clock; the exporter converts to wall-clock on its own clock,
outside the authority boundary. `OutcomeCounters` is separate and deliberately
not in state, because duplicate and refusal counts describe command outcomes
rather than retained records - a duplicate leaves no trace in state, which is
what makes it a duplicate - and putting them in state would drag an observability
counter into the state digest and therefore into consensus.

G-074a now has its signal: `oldest_reconciling_order_age` plus
`oldest_conflicted_order_age` and `unresolved_orders`. A stuck slot is no longer
indistinguishable from a quiet market.

REGRESSION FOUND, AND A PROCESS LESSON. Building only the target under test and
then running `ctest` gives a false pass: `ctest` does not build, so untouched
suites keep running STALE binaries. `v2-decision-kernel-tests` had been passing
against a binary predating the unauthorized-decision reclamation. A full
`cmake --build build-phase4` exposed it immediately. Always build ALL targets
before trusting a regression run.

The regression itself was a premise change, not a defect: that test filled the
decision arena with sixteen same-slot proposals, which supersession now sweeps
down to one so the arena never reaches its bound. The capacity-atomicity
invariant it protects is still worth testing, so the fixture now assigns each
retained proposal a distinct logical slot, making the arena genuinely full and
genuinely unsweepable. That is the third test whose premise this phase's
reclamation work invalidated while leaving its intent intact.

Release 25/25; focused host Debug ASan+UBSan 8/8, the sanitized build
reconfigured to include the metrics and decision-kernel targets.

### Finite identity horizons — decisions approved 2026-09-09 (Claude)

Specified in architecture §8.6; G-023 amended in the guarantee checklist.
Decisions recorded BEFORE implementation so the contract is reviewable
independently of the code that follows.

The defect being fixed: nothing ever reclaimed a request receipt, so the kernel
accepted exactly `kMaxRequestReceipts` commands in its lifetime and then refused
everything permanently. A second ceiling inside the same arena stopped fills
being accepted at `kMaxVenueEventReceipts`, which strands existing risk rather
than merely stopping new work. Both are availability defects.

Approved:

1. G-023 weakened to "within the retention window". Outside it a duplicate
   returns the typed refusal `request_expired`. The ANSWER is weakened, never the
   safety: G-022 holds because an expired request is refused rather than
   re-executed.
2. A new bounded `CallerCursor` arena plus `ApplyCode::request_expired`. Receipts
   may be forgotten freely; a request at or below its caller's watermark with no
   retained receipt is refused.
3. Caller capacity 8, WITH a new deployment requirement: `CallerId` must be a
   stable logical identity assigned by configuration, and `RequestSequence`
   monotonic per caller and non-regressing across restarts. A caller returning
   from a restart with a fresh `CallerId` would fill the cursor arena with dead
   entries and rebuild the wedge one level up. The alternative, caller eviction,
   needs its own horizon and reintroduces the problem it was meant to solve.
4. REVISED from the original proposal. Venue-event identities are reclaimed with
   their order group rather than pinned against expiry. A duplicate fill whose
   order is gone fails to resolve an order and is refused before touching
   economics, so the absence of the order IS the deduplication. This preserves
   the same safety at strictly lower cost AND removes the venue-event wedge,
   which pinning would have left in place.

Parked for Phase 5 review: caller-driven `ExpireIdentity`, already named in the
§7 candidate command family. It is the safest possible expiry because the party
that would retry declares it is finished, but it cannot be the only mechanism
since an uncooperative or crashed caller would leave the wedge. It belongs
alongside the gateway that would send it, and should be reviewed together with
the state-version horizon deferred from §8.4 and the reference horizon deferred
from the reclamation contract - all three are the same question asked about
different arenas, and Phase 5 is where the venue simulator finally makes the
numbers derivable.

The 64-entry venue-event horizon is a reviewed capacity, not a derived one. No
derivation is claimed, and none should be implied in later evidence.

### Finite identity horizons implemented — 2026-09-09 (Claude)

Implements §8.6. Release 25/25 and the FULL host Debug ASan+UBSan suite 25/25,
not the focused subset. `TradingState` 81,168 bytes (752 headroom), maximum
canonical snapshot 65,259 bytes (277 headroom).

`CallerCursor` records the highest `RequestSequence` whose receipt was actually
RECLAIMED for that caller. The first implementation used highest-observed and was
rejected during the work: it refuses a low sequence the cluster never held, which
enforces per-caller monotonicity as a side effect of expiry - a stricter and
different contract than approved, and it broke fixtures with every right to
number their requests freely. The kernel must refuse only what it actually forgot.

Source-event receipts turned out to be a FOURTH arena with the same defect, found
only because the lifetime test drove past 128. `SourceCursor` already holds the
watermark and admission independently refuses a regression, so they are reclaimed
on the same pattern with no new state.

Receipt pinning: authorization receipts while their order group lives, since
`materialize_order_effect` needs them; fill and venue-event receipts
unconditionally, since a redelivered PARTIAL fill would book twice and
reservation arithmetic does not catch it. Everything else, including every
refusal, is reclaimable.

DECISION 4 IS NOT IMPLEMENTED and is pinned by a failing-loud test. A fill
receipt has all five of its slots used and none names the cluster order, so
tying its expiry to order-group reclamation needs the canonical result to carry a
cluster order id - and the result is at 126 of 128 bytes. That is a byte-contract
change awaiting review. Until then fills still stop at `kMaxVenueEventReceipts`
and still strand existing risk.
`test_venue_event_horizon_still_bounds_order_completion` asserts the bound and
instructs its own deletion once lifted.

Suite-wide impact worth expecting again. Five suites needed fixture repair, in
two recurring shapes: fixtures that filled an arena with records that are now
reclaimable, which needed PINNED fillers to keep testing the capacity path rather
than the sweep; and fixtures that reused request identities in permuted or
descending order, which the watermark correctly refuses. Both are premise changes
rather than defects, and this is now the fifth and sixth time reclamation work
has invalidated an arena-filling fixture. A shared `unsweepable_full_arena()`
helper is overdue.

Two self-inflicted test bugs are worth recording because both read exactly like
kernel defects. A diagnostic message that re-ran the command under test corrupted
the run it was measuring, because `require` builds its message argument EAGERLY.
And `digest(static_cast<std::uint8_t>(decision_id))` makes every 256th DecisionId
hash to the all-zero digest, which `apply` correctly refuses as a missing digest -
it surfaced 120 cycles into a loop and looked like an arena bound.

### Historical pause — §8.7 resolved on 2026-09-10

Work stopped here on 2026-09-09 rather than taking the then-open decision
unilaterally. The resolution and implementation are recorded in the first
checkpoint and architecture §8.7; the analysis below is retained as history.

The open question: `apply_observe_fill` refuses at `kMaxVenueEventReceipts`, and
that bound fails in the WRONG DIRECTION. Every other bounded arena stops NEW work
when full; this one stops EXISTING orders completing, stranding live risk with no
resolution path.

The §8.6-approved fix - reclaim a fill identity when its order group is reclaimed
- turned out to be unimplementable as written. A fill receipt cannot be traced to
its order: five slots, all used, none naming the cluster order, and the canonical
result is at 126 of 128 bytes. Repurposing `venue_id` by making the dedup key
session-scoped was examined and rejected as a SAFETY REGRESSION, because a
redelivery after a gateway reconnect carries a new session id and would escape a
session-scoped key.

Recommendation in §8.7, costing zero bytes: keep the horizon but move the
pressure from fills to AUTHORIZATION, refusing new orders when remaining
venue-event capacity would not cover completing the orders already live. The
failure becomes "stops quoting" rather than "cannot get out". This is not a new
idea - §11 already reserves bounded capacity for risk-reducing operations, and
this applies the same principle one layer down. Pair it with a free self-heal:
fill receipts are reclaimable once no reservation is unreleased, because with
everything released a redelivery meets `fill_quantity_conflict` anyway.

Deferred alternative, also costing no result change: a per-`(venue, session)`
watermark on `venue_sequence`, the same pattern as the caller cursor. It needs a
small arena and depends on whether the venue supplies monotonic sequences, which
is unknown until a venue exists to ask. Phase 5, alongside the three horizon
questions already parked there.

A reviewer should decide three things, listed at the end of §8.7: whether the
stop-quoting trade is right, how much completion capacity to reserve (two venue
events per live order is a defensible start), and whether the flat-state reclaim
earns its place given how rarely a quoting maker is genuinely flat.

State at the pause: Release 25/25, full host Debug ASan+UBSan 25/25,
`git diff --check` clean, HEAD `8e8f661`. `TradingState` 81,168 bytes (752
headroom), maximum canonical snapshot 65,259 bytes (277 headroom).

Remaining Phase 4 work: broader accounting and reconciliation evidence, metrics and
age/latency evidence, an optional Phase 4 mirror model to complement this corpus,
the complete sanitizer gate, pinned ARM64 and physical Pi gates, and a Phase 4
acceptance evidence document.

Next bounded work: finish the approved serial-replacement acceptance matrix.
Start with a cancel/reconcile cycle containing a risk-limit update: reject a
stale-version successor proposal; freshly evaluate and authorize; verify full
effect bytes and economic/identity invariants across snapshot restore/replay.
Audit existing coverage first. Add a genuinely failing test for any uncovered
defect before fixing it; do not fabricate RED for behavior already implemented.
Continue the remaining accounting/capacity, metrics and independent Phase 4
model/property corpus work afterward, within the agreed phase ownership.

Standing invariants: sweep BEFORE every arena lookup/borrow; terminal is not
necessarily risk-resolved; payload reclamation must not erase retry/conflict
identity; configuration is validated before its non-throwing commit and cannot
rewrite balances. No wall-clock risk release or blind retry of unknown exposure.

Still open: unauthorized-decision retention, finite request/venue-event horizons,
broader accounting and reconciliation evidence, serial-replacement completion,
metrics/age/latency evidence, independent Phase 4 model, complete sanitizer gate,
pinned ARM64 and physical Pi gates, and a Phase 4 acceptance evidence document.
The Phase 1-3 generated model does not cover B1/Phase 4. No new decisions are
awaiting approval for the implemented B1 contract; review any NEW material byte
or economic contract before encoding. Keep educating the user at CTO/expert-C++
level, and do not pull gateway, Raft, WAL, venue adapter, strategy, netting or
optional V2.2 work forward. No live-trading readiness claim.

## Current branch and HEAD

- Branch: `v2/phase-4-risk-orders-outbox`
- Verified development HEAD: `aedd658ee0a7fe4e30a2338d2f953fc659306e29`
  (`Update Phase 4 handoff and changelog for takeover`)
- Working-tree increments after HEAD: approved serial-replacement contract,
  reclamation-identity/snapshot-accounting corrections, and B1 V5 configuration;
  see the takeover checkpoint above for the full uncommitted file inventory.
- Phase 4 development commits, in order:
  - `401c861` Add V2 Phase 4 risk reservation, orders, and outbox
  - `0491584` Reclaim resolved order groups from bounded arenas
- Phase 4 base commit: `c25c45fe0d05978b9855685622b39fccc1481b84`
- Handoff checkpoint: annotated tag `v2-phase4-handoff-start` (`330cfe1`),
  now an ancestor rather than the tip
- Accepted Phase 3 checkpoint: annotated tag `v2-phase3-accepted`, commit
  `8438d0d`

Run `git rev-parse HEAD` and `git show --no-patch --decorate HEAD` before
acting. Rollback point for all Phase 4 work is `330cfe1`.

## Dirty-worktree status

The following pre-existing work is intentionally uncommitted and must be
preserved. It belongs to passive model/market-data research and the proposed
Pi/NUC topology, not to Phase 4:

```text
 M docs/README.md
?? docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md
?? docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md
?? docs/adr/0002-two-machine-pi-nuc-deployment-topology.md
?? model/
```

`CHANGELOG.md` is no longer in this protected list: it carries committed Phase 4
entries plus current Phase 4 working-tree updates. Preserve its earlier
model/market-data section, which rode along in the committed handoff.

Do not delete, reset, stage, or include these paths in Phase 4 commits. Inspect
`git status --short` at the start and before every commit. Commit Phase 4 files
by explicit path rather than using `git add .`.

## Last accepted phase

Phase 3—policy artifacts and authoritative decision evaluation—is accepted on
the host and physical Raspberry Pi 5.

Accepted behavior:

- an admitted canonical `DecisionFrame` is evaluated deterministically against
  an atomically activated feature-schema/executable-policy/instrument/venue
  bundle;
- bounded rules use reviewed array-order, first-match precedence;
- strategy control modes dominate economic policy;
- a matched template resolves to an exact `EconomicActionProposal` using
  checked fixed-point values and explicit venue/instrument constraints;
- authoritative and shadow outcomes, provenance, and exact proposals are
  retained in bounded hot/cold decision records;
- canonical command/result/snapshot schema V3, direct apply, snapshot replay,
  and generated-model replay agree;
- authoritative apply remains pure, bounded, transport-free, storage-free, and
  venue-free; and
- cache-line/layout ceilings are enforced, including a 64-byte hot decision
  record.

Acceptance evidence:

- Git tag: `v2-phase3-accepted`
- Evidence document:
  `docs/evidence/V2_PHASE3_EXECUTABLE_POLICY_AND_MEASUREMENT.md`
- Physical Pi evidence directory:
  `~/.local/share/velocita/v2-phase3-acceptance-pi-gates/20260907T212930Z`
- Accepted source root:
  `f2d350757c9f2f71f0eab4e2600af982bea44d56027082fafc70764c77073f5b`
- Pi result: all Phase 3 suites passed; `throttled=0x0` before/after;
  43.55 C -> 49.60 C.
- In-memory Pi measurements only—no Raft/WAL/network:
  32-rule worst case p50/p99 `222/241 ns`, action resolution `185/204 ns`,
  authoritative reducer `740/778 ns`.

The measurements above are development signals, not end-to-end throughput or
venue-latency claims.

## Current phase

Phase 4—risk reservation, orders, and transactional outbox.

Objective: make an accepted authoritative economic decision atomically create
all four of the following, or none of them:

1. risk reservation;
2. cluster-owned order intent;
3. retained idempotency receipt/result; and
4. committed outbound effect with deterministic `EffectId` and payload digest.

Phase 4 owns end-state guarantees G-010 through G-020 and contributes to
G-021 through G-026 in `docs/V2_END_STATE_GUARANTEE_CHECKLIST.md`.

## Implemented behavior relevant to Phase 4

Already available as inputs/foundations:

- strong IDs including `DecisionId`, `ClusterOrderId`, and `EffectId`;
- bounded `EconomicActionProposal` created by Phase 3;
- canonical `OrderEffect` contract and payload codec as a boundary type;
- active policy, executor generation, kill epoch, and strategy mode;
- request deduplication and bounded decision hot/cold records;
- canonical kernel command/result/snapshot encoding and deterministic replay;
- compile-time object/payload ceilings and target cache-line reporting.

Phase 4 authorization, canonical V4 state, fill accounting, the cancel/unknown
lifecycle, slot exclusivity, and bounded arena reclamation are GREEN on the host
and COMMITTED (`401c861`, `0491584`). This is a development checkpoint, not a
completed Phase 4 acceptance claim - no sanitizer, ARM64, or physical Pi gate has
been run and no Phase 4 evidence document exists:

- direct amend/replace is refused with `replacement_not_supported`; wire kind 16
  stays allocated. Native amend and overlapping replacement remain disabled.
  Safe serial replacement is a REQUIRED Phase 4 acceptance item: cancel, final
  reconciliation, fresh authoritative decision, fresh authorization. Refusal
  alone does not satisfy it; see the approved scope and acceptance matrix below;
- `AuthorizeDecision` enforces at most one unresolved cluster order per account,
  instrument, venue, and logical order slot, keyed on the reservation not being
  released;
- `sweep_resolved_order_groups` reclaims fully resolved order groups at both
  admission points, and `TradingState::reclaimed_group_count` records how many
  have ever been swept;

- `AuthorizeDecision` accepts only a retained authoritative Phase 3
  `place_limit` action with an exact valid proposal;
- current risk-policy, risk-state, and position versions must match;
- checked gross quantity, full-margin, and ceiling-rounded fee holds are
  validated before mutation;
- one reservation, cluster order, and pending normalized outbox record are
  created in one non-throwing commit section;
- domain-separated SHA-256 derivation supplies stable 64-bit order/effect IDs,
  with zero/collision failure closed;
- request replay returns the retained identities and exposure, while a
  same-request/different-payload retry remains a typed conflict; and
- risk, staleness, capacity, shadow, no-action, and rejected-decision failures
  leave Phase 4 counts and economic balances unchanged.
- kernel command/result schema V4 explicitly encodes `AuthorizeDecision`,
  `ObserveFill`, `RequestCancel`, `RequestReplace`, and `ObserveOrderEvent`; V3 is not
  reinterpreted;
- snapshot schema V4 retains risk accounts, reservations, orders, outbox
  operation/state, position quantity, and fill/venue-event result identities;
- snapshot decision records omit only identities proven equal to the retained
  frame/hot record, keeping the current worst case at 65,121 bytes under 64 KiB;
- accepted partial fills atomically move signed quantity into position,
  proportionally and conservatively reduce the remaining margin/fee/notional
  holds, and increment `PositionVersion`;
- `(VenueId, ExecutionId)` suppresses duplicate fill economics and conflicts
  on a different retained command digest; accepted fill and venue-event
  identities share a reviewed 64-entry logical horizon inside the existing
  request-receipt arena;
- cancel intent appends a distinct generation-2 cancel effect and remains fully
  risk-bearing; `effect_unknown` also retains the hold; only accepted cancel or
  fill evidence releases residual economics; and
- the tested fill/cancel race preserves a filled terminal result even when
  cancel evidence arrives later.

The focused target is `v2-risk-order-outbox-tests`. Intended compile/runtime
REDs were observed for authorization, V4 codec/snapshot state, fill contracts,
cancel/unknown contracts, and cancelled-quantity snapshot conservation. The
focused suites and all 23 Release tests pass. Current host layout is
81,024-byte `TradingState` under the unchanged 81,920-byte ceiling, with
112-byte risk accounts, 64-byte reservations, 40-byte orders, 64-byte
normalized outbox records, and 896 bytes of remaining provisional headroom.
`ApplyResult` remains 112 bytes, canonical results remain 128 bytes, and the
command ceiling remains 1,920 bytes.

Historical review corrections before `401c861` on 2026-09-08 (replacement
branches described below were subsequently removed; these are not current
replacement capabilities):

- New submit/replace authorization rechecks current strategy mode, kill epoch,
  and executor generation. Historical retries still return historical outcomes.
- Delayed acknowledgements/unknown/rejections cannot regress tested terminal
  orders, partial-fill knowledge, or newer cancel intent.
- Provisional replacement keeps old active terms until acknowledgement and
  holds componentwise maximum old/new exposure; acknowledgement/rejection
  collapses that hold. This is NOT a proven venue-neutral safe replacement:
  maximum is sufficient only if old/new executions are mutually exclusive.
- A fill during pending/unknown replacement preserves risk and marks the
  order/current effect reconciling. Ordinary acknowledgement cannot clear it.
  This defers real fill accounting; reconciliation itself remains unimplemented.
- Submit decision retries after full fill return the retained original receipt,
  not the consumed reservation. Replacement decision retries after ack/reject
  likewise return the original effect/exposure without another mutation.
  Receipt lookup is bounded O(receipts * effects), allocation-free, with no
  added resident bytes or canonical schema changes in these retry fixes.

Latest host validation: full `build-phase4` Release build and 23/23 CTest suites
pass. This is development evidence, not Phase 4 acceptance. Existing generated
model coverage is Phase 1–3, not a Phase 4 reference model. Resolved order groups
are reclaimed from the 16-slot arenas; unauthorized decisions and the separate
receipt capacities still bound continued admission. Snapshot headroom is only
415 bytes; do not silently expand capacity.

Not implemented yet:

- complete Phase 4 acceptance evidence for command-configured risk limits (B1
  reducer/codec implementation is now present; see the checkpoint below);
- final review of the normalized `OrderEffect` boundary produced from retained
  outbox/decision state;
- reclamation of decisions that were never authorized (no-action and rejected
  evaluations retain a record no order will ever reference; in a live market
  maker most frames are no-action, so the decision arena can still fill). Needs
  the `DecisionId` conflict-detection horizon settled in Phase 5;
- complete safe serial replacement lifecycle/economic acceptance evidence
  (REQUIRED by the revised 2026-09-08 user approval; native amend and overlapping
  replacement remain disabled);
- ordered venue evidence across fills/order events and complete reconciliation;
- position/margin/fee accounting beyond signed filled position quantity and
  remaining open-order holds;
- broader acknowledgement/reject/reconciliation transition coverage;
- Phase 4 model/property/replay corpus and target gate.

An existing `OrderEffect` value/codec is not evidence that an effect can yet be
created authoritatively. It is only a validated boundary contract.

## Standing invariants added during Phase 4

Break either of these and the failure is silent.

1. **Sweep before you borrow.** `sweep_resolved_order_groups` compacts the
   decision, order, reservation, and outbox arenas. Any index resolved or
   reference borrowed BEFORE it runs stays in bounds afterwards while naming a
   different, possibly cleared, slot. Both call sites therefore sweep before any
   arena lookup, and a new lookup must not be inserted above them. This was a
   real defect: `apply_authorize_decision` borrowed
   `const auto& cold = state.decision_cold[decision_index]` before the capacity
   check, and an order reserved from a zeroed proposal. No crash, no sanitizer
   report; it surfaced fifteen requote cycles later as `fill_quantity_conflict`.
2. **Terminal is not resolved.** Reclamation and any future cleanup must test
   whether a record's RISK is resolved, not whether its state machine stopped.
   `conflict` and `reconciling` are terminal-or-stuck but hold exposure of
   unknown size. Prefer that formulation over enumerating states, so future
   terminal-but-unresolved states inherit the correct behavior.

## Decisions that must be reviewed before broad implementation

Keep the Phase 4 design minimal, bounded, and MVP-focused. Present the proposed
contracts to the user before locking them into canonical bytes:

1. exact risk-account dimensions and conservative reservation formula for
   submit, cancel, and replace;
2. minimum cluster order and reservation lifecycle states;
3. deterministic derivation and collision handling for `ClusterOrderId` and
   `EffectId`;
4. bounded capacities for orders, reservations, fill/event receipts, and
   outbox effects, including full behavior and provisional Pi/NUC headroom;
5. replacement semantics that cannot double reserve or briefly under-reserve;
6. partial-fill, cancel/fill-race, and unknown-submission accounting rules;
7. exact additions to `Command`, `ApplyResult`, canonical schema, snapshot, and
   state digest; and
8. which Phase 4 state is hot versus cold and its cache-line/arena budgets.

Prefer the conservative fail-closed choice when evidence is incomplete. Do not
silently clamp an unsafe proposal into an accepted order; return a typed reason.

## Exact next RED test

MVP replacement scope REVISED AND APPROVED by the user on 2026-09-08:
native amend and overlapping replacement remain disabled, but safe SERIAL
replacement remains a Phase 4 acceptance requirement. This supersedes the
earlier blanket deferral and the claim that refusal alone satisfies G-018.

Rationale: if old and new orders can both execute, covering both exposures is
legitimate risk coverage, not erroneous double-counting. `max(old, new)` can
under-reserve that case. G-011 and safe replacement are not inherently
incompatible. This MVP chooses serialization to avoid overlap and dependence on
native-amend semantics; this decision does not rely on an unverified venue API
claim. No gateway, venue adapter, or external execution is pulled into Phase 4.

Required serial-replacement acceptance matrix (coverage to be audited and
completed by TDD, NOT claimed GREEN by this documentation change):

1. Cancel-pending, unknown, reconciling, and conflict states retain unresolved
   risk and block a fresh authorization on the same logical slot.
2. Late/partial fills update position exactly once. Accepted final reconciliation
   resolves the old exposure before its remaining reservation is released;
   missing or contradictory evidence never causes a timeout-based release.
3. Only after resolution may a fresh authoritative decision create a new order,
   reservation, receipt, and submit effect atomically. Recheck current policy,
   strategy mode, kill epoch, executor generation, risk and position versions,
   and available limits. Do not auto-submit a proposal queued before the wait.
4. Exercise fill/cancel/final-evidence permutations and duplicates under fresh
   request IDs. Assert conservation and identity continuity, not only return codes.
5. Repeat across compaction and snapshot restore/replay. Compare complete
   canonical bytes for every retained pending/unknown effect against independently
   constructed expected payloads; a historical DecisionId must not gain a second
   authorization or alias a new order/effect after reclamation.
6. Keep direct `RequestReplace` refusal tests: no economic mutation or effect.
   These are negative capability tests, not proof of the serial lifecycle.

Execution uses successive reducer transitions, never a blocking wait inside
`apply`. No new canonical command or schema is approved by this scope decision.

Implemented (host GREEN, all 23 CTest suites pass):

- `ApplyCode::replacement_not_supported`: `apply_request_replace` refuses before
  touching economics. Wire kind 16 stays allocated so reintroducing replacement
  needs no renumbering.
- `ApplyCode::order_slot_occupied`: at most one unresolved cluster order per
  `(account, instrument, venue, logical order slot)`, enforced in
  `apply_authorize_decision` before any mutation. Occupancy is defined by a
  reservation that is not `released`, because retained risk is what G-018
  constrains. This closes a real defect: `ClusterOrderId` is derived from the
  varying `DecisionId`, so cancel-then-authorize previously created a second
  order and a second reservation on the same slot - violating the chosen
  no-overlap policy. The RED for
  `test_fresh_submit_cannot_bypass_unresolved_cancelled_slot` observed exactly
  that.
- Deleted: the `max(old, new)` hold in `apply_request_replace` (276 lines) and
  the now-unreachable replace acknowledgement/rejection branch in the observation
  handler (140 lines).
- Deleted: `ClusterOrder::active_filled_baseline`, whose only purpose was
  reconstructing an old residual after a rejected replacement. Every read lived
  in the deleted branch. Removed from the struct, snapshot validation, encode,
  and decode; `kStateSnapshotClusterOrderBytes` 43 -> 35.
- Removed five replacement-economics tests; converted the outbox materialization
  test to a submit/cancel effect pair, and the replacement control-recheck
  assertion to an unconditional-refusal assertion. This proves the disabled
  capability, not safe serial replacement or its control rechecks.

Measured layout after the increment:

```text
ClusterOrder            48 -> 40 bytes
TradingState        81,144 -> 81,016 bytes   (headroom 776 -> 904)
max canonical snapshot 65,241 -> 65,113 bytes (headroom 295 -> 423)
```

The snapshot headroom increase is deliberate and should be spent on B2 below,
not on new capacity.

Two new tests in `v2-risk-order-outbox-tests`:
`test_request_replace_fails_closed_without_economics` and
`test_fresh_submit_cannot_bypass_unresolved_cancelled_slot`. Both assert
economics are byte-unchanged on refusal and hold across snapshot restore. The
second also proves the slot becomes reusable only after `cancel_reconciled`
final evidence releases the residual.

This is unaccepted host development evidence. No sanitizer, ARM64, or physical
Pi gate has been run for it, and no Phase 4 evidence document exists yet.

B2 contract decisions approved on 2026-09-08:

1. reclamation unit is an order and its reservation as a PAIR, using dense
   compaction with one index-fixup pass, not a free list. `ClusterOrder`
   references its reservation by ARRAY INDEX, so compacting reservations
   independently would silently repoint orders at another order's risk. The
   existing `static_assert(kMaxRiskReservations == kMaxClusterOrders)` already
   encodes the 1:1 relationship;
2. reclaimable requires terminal AND unreferenced. Terminality alone is never
   sufficient;
3. the reference check is a bounded graph traversal evaluated across all arenas
   BEFORE anything is reclaimed, never reclaim-then-check. An order,
   reservation, or decision may not be reclaimed while any outbox effect in
   `pending`, `claimed`, `attempted`, `unknown`, or `reconciling` still needs it
   for `materialize_order_effect`;
4. the trigger is an implicit sweep on admission, only when the target arena is
   full, failing closed with the existing typed capacity code if the sweep frees
   nothing. Deterministic: no clock, no randomness, no allocation, one bounded
   pass;
5. the reclaimed count is reported in `ApplyResult` and exposed as a metric, so
   a sweep is an observable event rather than a silent state-digest change;
6. the horizon is REFERENCE-based, not time-based. The arena being full is the
   trigger and "nothing can reference this" is the safety condition, so no time
   horizon is required. An optional state-version guard exists as a configured
   parameter defaulting to unrestricted. It is measured in state versions, never
   seconds, because the reducer may not read a clock. REVISIT IN PHASE 5: a
   defensible bound cannot be derived today because the gateway retry horizon
   and the venue reconciliation window are both unknown until the venue
   simulator exists; and
7. scope is orders, reservations, decisions, and outbox effects. Request
   receipts (256) and venue-event receipts (64) keep their existing horizons and
   are a separate question.

Open question before the RED: whether B2 lands before or after the Phase 7 WAL.
Compaction removes records from the bounded working set, not from history - but
Phase 7 does not exist yet, so today there is no durable log behind the working
set and reclamation genuinely loses data. Audit and analysis are served by the
WAL plus Phase 10 projections, never by `TradingState`, which is deliberately
bounded and is not the archive.

B2 is GREEN on the host (23/23 CTest suites). `sweep_resolved_order_groups`
reclaims fully resolved order groups at both admission points, and
`TradingState::reclaimed_group_count` records how many have ever been swept.

One defect was found by the B2 REDs and fixed: `apply_authorize_decision`
borrowed a reference into `decision_cold` before the capacity check, so running
compaction there left the reference in bounds while naming a different, cleared
slot, and the order reserved from a zeroed proposal. Silent - no crash, no
sanitizer report. Both sweep call sites now run before any index is resolved or
any arena reference is borrowed. Preserve that ordering: adding a lookup above
the sweep reintroduces the hazard.

Still open in B2 scope: decisions that are never authorized (no-action and
rejected evaluations) are retained, because they might still be authorized. In a
live market maker most frames produce no-action, so the decision arena can fill
with records no order will ever reference. Reclaiming them needs the DecisionId
conflict-detection horizon settled, which is the Phase 5 revisit above.

Layout after B2: `TradingState` 81,024 bytes (896 headroom), maximum canonical
snapshot 65,121 bytes (415 headroom).

### Review corrections: reclamation identity and snapshot accounting

Implemented in the working tree on 2026-09-08; not a Phase 4 acceptance claim.
Both permanent regressions were run RED before production edits:

- `test_reclaimed_decision_retains_identity_and_authorization` failed with
  "reclaimed decision retry lost its original authorization receipt" after 20
  real evaluation/authorization/fill cycles forced reclamation.
- `test_snapshot_rejects_unbacked_risk_aggregates` failed with
  "snapshot accepted an aggregate hold inconsistent with live reservations".
  The test repairs the altered snapshot's integrity digest, so rejection must
  come from economic validation rather than checksum failure.

GREEN corrections:

- Applied evaluation receipts retain the decision digest and outcome after its
  hot/cold records are swept. New-request retries return `already_applied` without
  recreating a proposal; different retained digests return `decision_id_conflict`.
- Authorization retries recognize the original submit receipt using the existing
  generation-1 submit EffectId derivation, independently of reclaimed outbox
  records. Cancel receipts share the result tag but are not submit outcomes.
- Snapshot validation joins reservation → order → immutable decision proposal →
  account/instrument. Every reservation must have one owner, and checked sums of
  remaining quantity, margin and fee must exactly match each risk account.
  Missing accounts, orphan reservations, mismatched totals and overflow fail
  `invalid_state`; rejected decoding leaves destination state unchanged.
- Tests cover both direct and restored identity retries, conflicting evaluation,
  fresh authorization with a distinct materializable effect, full canonical effect
  and final snapshot byte equality across restore, all three aggregate fields,
  per-account ownership, orphan holds, and all three accumulator overflows.
- Corrected the maximum-size snapshot fixture's accounting: its orders all belong
  to account zero, so its holds do too. No capacity or encoded size was reduced.

Validation: full Release 23/23; focused Release 2/2; focused host Debug ASan+UBSan
2/2 using `build-phase4-review-sanitized`, `-fsanitize=address,undefined` and
`-fno-omit-frame-pointer`. This is NOT the complete Phase 4 sanitizer acceptance
gate; pinned ARM64, physical Pi, and Phase 4 evidence remain outstanding.

No canonical schema, field, ID derivation, or resident capacity changed.
Evaluation/authorization receipt scans are bounded O(request receipts); snapshot
ownership joins are bounded O(orders * (decisions + risk accounts)), in addition
to existing uniqueness/link checks. Four temporary totals use 96 stack bytes.
State remains 81,024 bytes; maximum snapshot remains 65,121 bytes.
Identity safety still depends on retaining receipts: future receipt reclamation
must preserve the declared duplicate/conflict horizon. No receipt expiry or
indefinite-operation claim was introduced. Sweep-before-borrow remains mandatory.

### B1 checkpoint: replicated risk-limit configuration (2026-09-09)

The eight B1 choices were approved, including full-config replacement after
explaining that masked patches can also be atomic but add unnecessary partial-
update semantics. Authoritative operator/user-doc reference:
`docs/V2_ARCHITECTURE_CONTRACTS.md`, section 8.1. Do not reconstruct those rules
from this handoff's historical text.

Implemented: `ConfigureRiskLimits`, kind 17, 88-byte body/180-byte framed command;
kernel command/result and snapshot schemas V5, with risk-configuration receipt
tag 5 and unchanged resident sizes. Creation, strict compare-and-set updates,
kernel-selected next RiskVersion, control-generation checks, preservation of
balances, prospective per-order caps, aggregate hold coverage, quiescent rate/
policy changes, checked overflow and bounded refusal behavior are implemented.
No native amend, overlapping replacement, gateway, Raft or operator authentication
was added. V4 input is rejected rather than migrated; OrderEffect is unchanged.

Observed REDs, in order: undeclared ConfigureRiskLimits; runtime update failed
to advance exactly one version; runtime command lacked its 88-byte canonical
body. Each was followed by GREEN. Additional coverage checks stale decision
authorization, fresh authorization using command-created risk (not injected
balances), encoded replay, full-body byte expectations, result/snapshot receipt
round trips, old-schema refusal, malformed/truncated input, duplicate conflicts,
account/receipt capacity and invalid-rate/control/version boundaries.

V5 intentionally changes command/result/snapshot framing digests and the replay
goldens derived from them. Legacy source-command and empty-snapshot payload
projections retain their V4 golden checks. The Phase 1-3 generated model explicitly
does not model B1; the independent full Phase 4 model remains outstanding.

Next work after B1 validation: complete the approved safe serial-replacement
acceptance matrix. Start with a cancel/reconcile cycle containing a risk-limit
update, prove an old-version proposal cannot authorize the successor, then
evaluate/authorize freshly and compare full effect bytes through restore/replay.
Audit existing GREEN coverage before selecting the next genuinely failing case;
do not claim an already-passing test was observed RED. Capacity horizons, complete
accounting, metrics/model corpus and Phase 4 target gates remain open.

B1 validation: full Release 23/23; focused host ASan+UBSan 5/5 (risk/outbox,
snapshot, command/result codec, replay, and the Phase 1-3 generated model).
Builds: `build-phase4`, `build-phase4-review-sanitized`. This is not the complete
Phase 4 acceptance gate. TradingState remains 81,024 bytes; maximum snapshot
65,121 bytes; command and result ceilings unchanged. Reducer work is bounded by
four account entries and at most sixteen orders/decision lookups, plus existing
request-receipt handling. No allocation or new resident balances are introduced.

B1 naming/semantics decision approved and implemented on 2026-09-08:
`max_order_notional` is renamed `max_order_gross_notional` (one order's gross
notional ceiling); `max_reserved_notional` is renamed `max_reserved_exposure`
(aggregate reserved margin plus reserved fee budget). This is a behavior-preserving
source rename, not a new aggregate gross-notional limit. Member types/order,
snapshot field widths/order, schemas and risk comparisons remain unchanged.
The later B1 command/schema implementation above preserves these limit meanings.

Reconciliation contract approved and implemented. The runtime RED
`test_cancel_before_earlier_fill_does_not_lose_fill_evidence` failed with
"earlier venue fill delivered after cancel was lost from accounting".
Venue sequence 1601 is a one-lot fill, 1602 is cancellation; delivery is 1602
then 1601. Cancellation currently releases all remaining reservation; the late
fill is refused with fill_quantity_conflict because the reservation is released.
It is now GREEN: cancellation retains the hold and marks order/cancel effect
reconciling; a valid delayed fill consumes only its quantity and does not reopen
the order. Full fill may finish the order because no unfilled residual remains.

Approved economic contract: distinguish stopped
order from reconciled accounting. Cancellation without evidence of fill
completeness retains unresolved hold and enters reconciling; valid delayed fills
must be applied exactly once without reopening the order. Release unused residual
only after scoped final-order evidence establishes final cumulative filled quantity
and agrees with the locally accepted fills. Implemented `cancel_reconciled` kind
6 in VenueObservation schema V2, still 116 bytes: quantity is explicitly final
cumulative filled quantity for this kind; price/fee/execution ID must be zero,
quantity nonnegative, and venue order identity present. The observation is scoped
by venue, cluster order and current cancel effect. It requires matching locally
filled quantity and a reconciling cancellation (or matching already-full fill).
No position is inferred from a cumulative total. Mismatch returns
venue_observation_not_applicable, retaining risk. Evidence received too early is
not buffered for automatic completion: after missing fills arrive, the caller
must submit a new request; retrying the old request preserves its refusal.

Both plain cancellation and final reconciliation remain venue evidence, whose
authenticity/finality must be established upstream. No contiguous stream watermark
is implemented; a larger sequence alone is NOT proof of completeness. This does
not solve arbitrary contradictory terminal evidence or replacement reconciliation.

Observation boundary/fingerprint explicitly advance to V2: SHA-256 over ASCII
`velocita/venue-observation/v2` without NUL plus canonical V2 bytes. V1 observation
payloads are rejected. Kernel command/result/snapshot remain development V4, and
no fields/capacity were added. Tests prove hold preservation before delayed fills,
snapshot restore while pending, matching/mismatched final totals, request/event
dedup, and byte-identical direct vs encoded reconciliation replay snapshots.
Negative final quantity exposed an additional runtime RED: the typed refusal
could not serialize because kernel codec's ContractError range stopped at an old
enum member. Its range now includes all existing declared ContractError values.
No new error enum was introduced. All current changes remain unaccepted Phase 4
development, not sanitizer/ARM64/Pi acceptance.
Fresh full Release build and 23/23 CTest suites pass after reconciliation and
refusal-codec fixes. Layout report remains 112-byte result, 160-byte receipt,
128-byte wire result, 81,144-byte state and 65,241-byte maximum snapshot;
`git diff --check` is clean. Unrelated pre-existing dirty paths remain untouched.

Latest cross-kind identity checkpoint:
- `test_fill_cannot_reuse_an_order_event_identity` observed runtime RED: an
  acknowledgement and fill reused one venue/session/sequence. GREEN adds an
  existing event-receipt lookup before fill deduplication/economics, returning
  venue_event_identity_conflict. The test also verifies the guard after snapshot
  restore. All 23 Release suites passed before the reverse-direction test below.
- The reverse runtime RED: `test_order_event_cannot_reuse_a_fill_identity` failed
  with "cancel evidence reused a fill venue/session/sequence and released risk".
  A partial fill was accepted, then cancel confirmation reused that fill's stream
  identity and incorrectly released residual risk. This is now GREEN using the
  approved tagged metadata implementation below.
- Approved and implemented result/snapshot contract: use explicitly tagged
  16-byte result metadata for policy ID/version on policy-related results, and
  venue session/sequence on fill results. Preserve strongly typed accessors; do
  not silently put venue IDs in fields still named policy IDs. Existing fill
  results do not use those policy fields. `ApplyResult::metadata_words` stores
  two uint64 words, discriminated by existing detail_kind. Typed policy_id(),
  policy_version(), fill_session_id(), and fill_sequence() accessors return an
  absent ID for the wrong tag. No C++ union aliasing or new tag allocation.
  Canonical codecs read/write the same two positions; successful/duplicate fill
  results now require nonzero session/sequence. A separate codec runtime RED
  proved that missing stream metadata needed rejecting before this validation.
  Old unaccepted V4 fill results without stream identity fail validation; this
  amends development V4 semantics, not accepted V3 bytes. Phase 1–3 regression
  goldens remain unchanged.
- Shared retained-event lookup now covers applied fills and order events.
  Cross-kind conflicts and different executions reusing the same stream identity
  return venue_event_identity_conflict before economics. Execution-ID dedup remains
  independently enforced. Tests prove both directions across snapshot restore and
  distinguish identical sequence numbers in different sessions. One legacy race
  fixture incorrectly reused fill sequence 501 for cancel confirmation; it now
  uses distinct sequence 503 so it tests the intended race, not identity conflict.
- Full Release build and 23/23 suites pass. Measured result/receipt/wire-result
  sizes remain 112/160/128 bytes; resident state remains 81,144 bytes; max snapshot
  remains 65,241 bytes. No added resident capacity, sanitizer or target-gate claim.

Cancel contract approved and GREEN: whole-order cancel uses zero price/quantity,
retains scope/provenance and authorization-time controls, and hashes the full
20-word instruction preimage instead of eight identity/control words. Existing
proposal side/TIF/post-only are retained as metadata, not new trading parameters.
Cancel intent does not release risk. Full-byte tests cover partial fill, cancel
completion, control changes and snapshot restore; boundary validation rejects
nonzero cancel price/quantity. All 23 Release suites passed after this increment.
No additional resident fields or payload bytes were added. Cancel digests (and
snapshots containing them) change from the earlier unaccepted development draft;
old draft cancel digests fail reconstruction rather than being silently rewritten.

Observation fingerprint contract approved and GREEN. The copied-digest RED above
was observed, then fixed by `observation_fingerprint` in digest.hpp/v2_digest.cpp.
Current preimage (advanced for reconciliation): ASCII `velocita/venue-observation/v2`
without NUL, followed by the 116 canonical VenueObservation V2 bytes, including schema, all IDs/economic
fields, and evidence_digest. SHA-256 covers the entire preimage. Caller/request
identity is excluded so genuine cross-request retries have the same fingerprint.
The allocation-free implementation uses a fixed stack buffer and explicit LE
encoding, tested against the independent existing payload encoder plus SHA-256.

ObserveFill/ObserveOrderEvent producers must set CommandIdentity.payload_digest
to that fingerprint. Kernel apply verifies it before receipt lookup or economic
deduplication; mismatches return invalid_command without advancing state_version
or retaining a receipt. A separate runtime RED exposed wrong command-wrapper
reuse of a valid observation; wrapper/kind compatibility is now checked before
deduplication too. All other command digest semantics remain unchanged.
Tests cover same/cross-request alteration, correctly fingerprinted execution/event
conflicts, valid duplicate receipts after snapshot restore, and existing fill replay.
Full Release build plus all 23 CTest suites pass after this increment.
Resident layout and serialized field layouts are unchanged; observation digest
values now follow this contract rather than arbitrary caller values. Old unaccepted
development snapshots with unchecked observation digests are not migration-tested.
This proves content consistency, not venue authenticity or ordered evidence.

User approved OrderEffect schema V2 on 2026-09-08. The missing-post-only compile
RED is now GREEN: V2 is 166 bytes with the explicit one-byte PostOnlyMode appended
at offset 165. V1 is rejected, not reinterpreted. Unknown enum values and required
post-only with incompatible time-in-force fail validation. The enum moved from
action_template.hpp to contracts.hpp to share one definition without a circular
include; the in-memory field occupies padding beside time-in-force. This boundary
change does not change kernel V4 command/result/snapshot bytes or resident state.

`test_outbox_reconstructs_full_immutable_payload` first failed compilation for
the missing materialization API, then passed with `materialize_order_effect`.
It independently constructs expected submit/replace payloads and compares every
encoded byte in pending, unknown, and acknowledged/control-changed states, also
after snapshot restore at each stage. Corrupt digests, changed proposal post-only,
changed authorization generation, missing identity, and absent authorization
receipts fail closed. The API returns an owning optional value, is noexcept and
allocation-free, and scans O(effects + receipts + decisions), all bounded.
Canonical boundary encoding remains the existing vector-based outer-layer API;
it is not called by the reducer or materializer.

Authorization-time metadata comes from the original applied receipt, not mutable
order/current controls; economics come from the effect's own retained proposal.
The digest is recomputed using the existing 20-word domain-separated semantic
preimage (including post-only), not recursively hashing the serialized digest
field. Full-byte comparisons are additional evidence, not replaced by hash checks.
No retained receipts/decisions may be reclaimed while their effects need them.
Cancel materialization has subsequently been implemented as recorded above.
No sanitizer/ARM64/Pi claim is made.
After this schema/materialization increment, a fresh full Release rebuild and
all 23 CTest suites passed; `git diff --check` was clean. Layout remains
81,144 bytes for TradingState and 65,241 maximum canonical snapshot bytes.

The eight contract decisions were reviewed and approved on 2026-09-07.
User authorized continued development and corrective review. Earlier replacement
wording claiming old-plus-new exposure must never be held was too strong:
coexistence may require both holds. Do not freeze that assumption into canonical
contracts. Historical replacement fork below is now resolved by the approved
serial-replacement scope above; it is not a separate next-RED instruction:

```text
Keep RequestReplace fail-closed and unresolved-slot refusal tests. Complete the
serial-replacement acceptance matrix above using cancel/reconciliation and fresh
authorization; do not restore native amend or overlapping replacement branches.
```

Fingerprint, cross-kind identity, and first delayed-cancel reconciliation REDs
above are GREEN. Fill receipts retain session/sequence and execution identity.
Tested terminal evidence permutations are GREEN; replacement scope is resolved,
but the full serial-replacement acceptance matrix is not yet proven;
no extra state-capacity budget is approved.
Review any material
new byte-contract choice with the user before freezing it. Then prioritize
observation-content identity, safe replacement/reconciliation, snapshot invariants,
and a real Phase 4 model/replay corpus. No gateway, venue adapter, Raft, WAL,
netting, strategy, or optional V2.2 work belongs in these increments.

## Acceptance evidence required for Phase 4

The phase cannot close until retained evidence proves:

- no effect without reservation and order intent;
- duplicate request/decision returns the same effect, never a second mutation;
- insufficient or stale risk creates no order/effect;
- reservation/filled/remaining quantities conserve under partial fills;
- unknown submission and cancel-pending remain risk-bearing;
- safe serial replacement meets the approved acceptance matrix above: retain old
  risk through cancellation, book late fills exactly once, reconcile, then freshly
  decide and authorize under current controls; native amend and overlap remain
  disabled. Refusal and slot exclusivity alone are insufficient evidence;
- replay reconstructs byte-identical pending/unknown effects;
- snapshot retains receipts/effects through the declared retry horizon;
- bounded full policies fail atomically; and
- fault/property/model tests cannot create an unreserved effect, negative
  reservation, duplicate economics, or replay mismatch.

Evidence must identify commit/source root, canonical schema/digest changes,
Release and sanitizer results, layout/capacity report, ARM64 container result,
physical Pi result, power/temperature state, limitations, and rollback point.

## Unresolved failures and provisional limits

- No known failing accepted Phase 3 test or physical Pi gate.
- Phase 4 is in progress; the host RED/GREEN evidence above is not yet the
  sanitizer/ARM64/physical-Pi acceptance gate.
- The `TradingState` 80-KiB ceiling and current capacities are provisional until
  deferred NUC capacity validation; do not increase them silently.
- Phase 3's benchmark excludes Raft, WAL, networking, and venue I/O.
- TrueMarkets resting-order/post-only/order-book capability remains unconfirmed;
  that blocks adapter/live claims, not Phase 4's venue-neutral OMS work.
- The proposed two-machine topology is not accepted high availability.
- A logical order slot held by an order stuck in `reconciling` is not
  recoverable without operator action, and no operator action exists yet. There
  is no replicated timeout, no escalation, and no override command. The slot
  silently stops quoting and is indistinguishable from a quiet market until the
  Phase 4 metrics in C4 exist. Tracked as G-074a in
  `docs/V2_END_STATE_GUARANTEE_CHECKLIST.md`; the resolution is an operator-
  driven replicated transition plus a runbook entry, owned by Phases 8 and 10.
  Do not "fix" this in Phase 4 with a wall-clock timeout in the reducer: that
  breaks determinism, and releasing a hold without venue evidence of final
  cumulative filled quantity is the G-011 violation reconciliation exists to
  prevent.

## Important ADRs and contracts

Read only these first; chase additional references only when the current task
requires them:

1. `docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md`, especially sections 2,
   3, 4, and 9 (Phase 4).
2. `docs/adr/0001-v2-replicated-decision-oms-and-execution-boundary.md`, especially
   transactional outbox, authority, persistence, and non-negotiable invariants.
3. `docs/V2_ARCHITECTURE_CONTRACTS.md`, especially sections 3 and 7–10.
4. `docs/V2_END_STATE_GUARANTEE_CHECKLIST.md`, especially G-010–G-026 and
   deviation tripwires.
5. `docs/V2_PHASE3_DECISION_EVALUATOR_DESIGN.md` for the accepted input boundary.
6. `docs/evidence/V2_PHASE3_EXECUTABLE_POLICY_AND_MEASUREMENT.md` for the exact
   accepted checkpoint and limitations.
7. `docs/V2_1_PERFORMANCE_OPTIMIZATION_SCOPE.md` only to avoid pulling deferred
   work into V2.

Primary C++ starting points:

- `include/velocita/v2/kernel.hpp`
- `src/v2_kernel.cpp`
- `include/velocita/v2/action_template.hpp`
- `include/velocita/v2/contracts.hpp`
- `include/velocita/v2/kernel_codec.hpp`
- `src/v2_kernel_codec.cpp`
- `include/velocita/v2/state_snapshot.hpp`
- `src/v2_state_snapshot.cpp`
- `tests/v2_decision_kernel_tests.cpp`
- `tests/v2_replay_tests.cpp`
- `tests/v2_model_replay_tests.cpp`
- `tests/v2_layout_tests.cpp`

## Commands needed to continue

Verify the handoff:

```bash
cd /Users/vishaljha/aeron-raft-pi
git status --short
git branch --show-current
git rev-parse HEAD
git rev-parse v2-phase4-handoff-start
git log --oneline --decorate -8
git diff --check
```

Read the bounded context:

```bash
sed -n '1,260p' docs/CURRENT_HANDOFF.md
sed -n '442,484p' docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md
sed -n '1,240p' docs/adr/0001-v2-replicated-decision-oms-and-execution-boundary.md
sed -n '1,340p' docs/V2_ARCHITECTURE_CONTRACTS.md
sed -n '45,115p' docs/V2_END_STATE_GUARANTEE_CHECKLIST.md
```

Run the existing host regression baseline using an out-of-tree build directory
(do not overwrite an existing user build):

```bash
cmake -S . -B build-phase4 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-phase4 -j2
ctest --test-dir build-phase4 --output-on-failure
```

Before a physical Pi gate, synchronize explicitly while preserving Pi-local
Git/build/evidence state:

```bash
rsync -av --delete \
  --exclude '.git/' \
  --exclude 'build/' \
  --exclude 'build-*/' \
  ./ bhaikapi@100.84.151.116:~/aeron-raft-pi/
```

Do not invent a Phase 4 Pi acceptance command until its gate script and
evidence contract exist. The previous accepted Phase 3 gate is
`deploy/run-v2-phase3-acceptance-pi-gate.sh`; it is a regression reference, not
a Phase 4 acceptance gate.

## Explicit non-goals for Phase 4

- no execution gateway or external venue calls (Phase 5);
- no generic V2 Raft integration (Phase 6);
- no asynchronous WAL, block arena, snapshot publication, or compaction work
  beyond extending deterministic in-memory snapshot bytes (Phase 7);
- no executor fencing, automatic promotion, kill transport, or emergency lane
  implementation (Phase 8);
- no TrueMarkets order adapter, credentials, or live/paper order submission
  (Phase 9);
- no Grafana/release-operating expansion beyond metrics contracts (Phase 10);
- no strategy fitting, profitability claim, leaderboard claim, or real-money
  activation (Phase 11);
- no cross-strategy portfolio netting; it is explicitly deferred to V2.1;
- no `valid_state` performance rewrite without measurement; deferred to V2.1;
- no good-to-have feature that is not necessary for the Phase 4 invariants;
  defer such work to V2.2.

## New-session bootstrap prompt

Copy the prompt below into the new session:

```text
Work in /Users/vishaljha/aeron-raft-pi on Phase 4: risk reservation, cluster
orders, and the transactional outbox.

Read completely:
- AGENTS.md, if present
- docs/CURRENT_HANDOFF.md
- sections 2, 3, 4, and 9 of docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md
- the directly referenced ADR/contracts listed in the handoff

Inspect Git status, branch, HEAD, the v2-phase4-handoff-start tag, and recent
commits to verify the handoff. Preserve every pre-existing dirty path listed in
the handoff. Never use git add . and do not reset or delete unrelated work.

Do not reconstruct the project from historical chat. Git, tests, ADRs,
contracts, evidence, and docs/CURRENT_HANDOFF.md are authoritative memory.

First explain the eight Phase 4 contract decisions in CTO-level and deep C++
terms, including rejected alternatives, and ask me to review material choices
before freezing canonical bytes. Then continue by TDD from the exact next RED
test in docs/CURRENT_HANDOFF.md. Show RED for the intended reason, implement the
smallest GREEN increment, run focused and regression tests, and keep me educated
on what, why, how, ownership, memory layout, complexity, failure behavior, and
trade-offs.

Maintain MVP focus. Phase 4 must not pull gateway, Raft, WAL, venue adapter,
strategy, netting, or optional V2.2 work forward. Update CURRENT_HANDOFF.md as
the phase advances.
```
