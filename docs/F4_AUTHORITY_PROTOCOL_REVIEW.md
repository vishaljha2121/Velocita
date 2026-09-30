# F4: quorum evidence at the final send boundary

2026-09-22. **Semantic direction and exact marker bytes APPROVED by the owner.**
Governing direction: ADR 0001; roadmap F4/A01; architecture sections 9 and 11;
G-052..G-060. CP-008 catalog binding remains intact. No sender is enabled here.

CP-012 status addendum: the reproduction/recommendation narrative below is the
historical review. Host proof issuance, consumption and final transport composition
are now implemented; boolean renewal always refuses. See ADR 0013 and
`evidence/f4_proof_2026-09-22/README.md` for tests and honest remaining deployment
limits. Production IPC/clock/domain/fencing and target approval are not implied.

## 1. What is reproduced, not merely suspected

Current `ReplicatedKernel::committed_outbox()` derives its `authoritative` flag
from the local leader role. `renew_authority()` accepts that flag and assigns
the caller's current tick to `confirmed_at`. The gateway checks the same stale
replica's controls, so all comparisons can agree while the majority disagrees.

The new CP-008-compatible reproducer builds the catalog, policy, reservation and
effect through committed commands and passes the verified catalog into pre-send.
Three-voter partition RED: `majority_kill=2 stale_kill=1 renewed=1 permitted=1`.
Two-voter delayed-reply RED: `captured_at=1 received_at=10000 bound=50 renewed=1
permitted=1`. Each exits 1 at its intended assertion. The second holds a reply
captured while both voters were healthy, then delivers it after partition.
It does not pretend the existing boolean already constitutes a quorum proof.
Evidence/source: `evidence/f4_authority_2026-09-22/`.

Consequently shorter timeouts, another comparison with the same stale state, or
co-location with a voter do not fix A01. The proof producer and consumer both
need a new contract. This is why no production patch is slipped into this review.

## 2. Recommended MVP primitive: one committed barrier per new-risk attempt

Recommend a small **replication-only, non-economic authority barrier** through
the existing durable Raft log. It names one pending gateway request and one
effect. Return a proof only after that newly appended current-term entry commits
and applies, and only to its still-matching live requester. At that point copy
the controls and exact catalog-bound effect from one owner-thread state view.
If the effect is no longer deliverable, return no send permission.

This is a proposed refinement of the audit's quorum-confirmed read barrier, not
a claim that a local read becomes authoritative. A committed marker establishes
the ordered point using the already-tested quorum/durability machinery. It does
not record an order claim, transport attempt or venue outcome, and cannot change
balances, reservations, reducer StateVersion, receipts or outbox state.

Cost: an additional quorum/durability round trip and one WAL entry per attempted
new-risk send. It is not the lowest-latency end state. This is recommended for
MVP correctness and implementation scope, not as a benchmark conclusion. F5's
retention/backpressure work remains a prerequisite to sustained deployment;
this design must not hide its additional log load. Idle gateways do not renew
by writing periodic barriers. One outstanding request/result per gateway domain
is proposed; full means refuse/defer, never overwrite or accumulate a map.

Alternatives:

| Option | Benefit | Cost / reason not recommended first |
|---|---|---|
| Genuine ReadIndex | Fresh quorum reads without a WAL write per attempt | This pinned library has no native correlated read API; requires current-term commit establishment, request-correlated quorum acknowledgements, applied-index coverage and a new reviewed peer protocol |
| Reusable timed proof | Amortizes quorum work across orders | Broader permission and consumption/late-effect reasoning; requires both time and count limits, not just a renamed lease |
| Existing no-change control command as marker | No new body type | Pollutes business receipts/StateVersion and can consume the emergency reserve; acknowledgement pinning can exhaust the receipt arena |
| Recent heartbeat / leader boolean | Cheap | Neither binds a fresh request to an ordered applied state; delayed replies reproduce A01 |

ReadIndex is a valid later optimization, not rejected as unsafe. The
[Raft paper, section 8](https://raft.github.io/raft.pdf) describes the two
requirements for log-free reads: establish commitment in the current term and
confirm leadership with a fresh majority exchange. Our current pinned
`raft_become_leader` sends AppendEntries but does not automatically append a
current-term no-op. Counting arbitrary heartbeat replies would omit a prerequisite.
This recommendation is our engineering judgment, not an attribution to the
unavailable Velocita book.

## 3. One-shot, request-start expiry; no remotely supplied freshness clock

Proposed gateway identity: fresh CSPRNG-generated 128-bit process incarnation
plus monotonically increasing u64 request sequence. Entropy failure or sequence
exhaustion stops new requests. Randomness is outside the deterministic reducer.
Restart invalidates every outstanding/accepted proof; it never restores a lease.
Authentication is still required: a random nonce is correlation, not a credential.

Record gateway-local start tick before requesting the barrier. A response must
match the exact pending incarnation/sequence/effect, be unused, and arrive with
age within the configured bound. Receiving it does NOT change the start tick.
Proof freshness is checked again at final transport admission. Accepting an old
reply twice cannot renew anything. A proof permits exactly one attempted send;
a proven-unsent retry needs a fresh proof and keeps the same stable EffectId.
Existing unknown-outcome reconciliation and attempt limits remain in force.
Track accepted term/log-index and observed control-version high watermarks within
the process; refuse regressions and invalidate outstanding work on an observed
newer fence. A delayed older proof cannot undo a separately observed kill.

Proposed clock: injected integer ticks in tests; suspend-aware monotonic elapsed
time in the Linux process integration. Never compare NUC and Pi clock readings.
Use subtraction after checking `now >= start`, reject zero bound and arithmetic
overflow, and invalidate on clock anomalies. Any reconnect/restart that cannot
preserve the exact local pending owner drops the proof and starts closed.

Do not choose a live millisecond value from intuition. Proposed default is zero
(disabled); simulations use explicit bounds, e.g. 50 ticks. NUC/Pi latency,
clock behavior, scheduling and the chosen venue must justify a separate live
bound before deployment. Protocol approval does not approve an unmeasured bound.

## 4. Coherent proof contents and C++ ownership

Required semantic fields (NOT yet frozen wire layout/ordinals):

- request incarnation and sequence; one requested EffectId;
- deployment/cluster and static membership identity (F5 must persist/validate it);
- issuer ID, current Raft term, barrier log index, applied log index;
- reducer StateVersion, generation, kill epoch, strategy mode;
- catalog root, historical spec/capability versions and exact OrderEffect;
- identity/digest of the coherent state/effect view where needed for validation.

`OrderEffect::committed_directive_index` currently holds reducer StateVersion,
NOT the Raft log index. The new barrier would advance only the latter. These
coordinates must remain separately typed/validated; comparing their numeric
values as proof of coverage is wrong. The proof's effect must actually come from
the applied view, not merely carry a small-enough integer.

The Raft owner alone produces the copied result; the gateway owner alone owns
the pending request and consumes permission. No TradingState reference or arena
index crosses a queue. Fixed pending/result slots bound F4-owned memory, with
no new TradingState or snapshot fields. Exact local sizes need static assertions
and layout tests after implementation; the existing 53-byte snapshot headroom
is not permission to expand its ceiling.
The issuer must still be leader in the barrier's term when publishing/dequeuing
permission; a term change invalidates its pending proof slot. Barrier admission
uses bounded non-emergency capacity and cannot spend the kill reserve. Capacity
pressure therefore stops new-risk permission while preserving control service.

The barrier needs an explicit typed discriminator and canonical body, validated
at admission, WAL preflight, before_apply and replay. Today these paths decode
all entries as business commands, so every path must change coherently. Unknown
types fail closed. Replay can advance the Raft prefix but never issue permission;
no gateway request survives a voter/gateway restart as a cached authorization.
V1 codecs, message types and behavior must remain unchanged. Full byte review is
required before assigning the new durable type/body or external IPC envelope.

The old `authoritative` boolean becomes diagnostic local leadership only.
Neither it nor a publicly constructible look-alike struct may be the live proof
issuer. Library type encapsulation prevents accidental misuse, not malicious
in-process code. The deployed separate-process path still requires authenticated,
bounded IPC/peer transport and static cluster identity; tests cannot stand in for
that integration. Do not treat F4 library GREEN as G-052..G-060 closure.

## 5. Kill, cancel and external effects: the limits must be explicit

Recommend preserving the accepted cancel exception: a known committed cancel
can use emergency quota without a fresh quorum proof. It remains bound to its
exact effect/catalog and known controls; any observed newer generation/epoch
invalidates conflicting cached authority. No new uncommitted cancel is invented
when quorum is absent. Unknown cancels retain existing bounded retry/evidence
rules. A disconnected gateway cannot know a remotely advanced generation; this
exception therefore permits an old, risk-reducing cancel until externally fenced.
It is not a promise that no old process can emit any byte.

Distinguish local `stop_new_risk` from `stop_all_transport`. The first must not
silently disable the emergency cancel path. The second blocks all venue writes
for shutdown/fencing. A stale reply cannot clear either local latch; re-enable
requires explicit authenticated action plus current controls/reconciliation.
No automatic gateway promotion, including when the Pi becomes Raft leader.
Manual replacement still requires stopping/fencing the old external sender and
reconciling ambiguous attempts. A new generation alone proves neither.

Final admission occurs in the transport owner AFTER quota wait, encoding and
other asynchronous work, immediately before bytes are entrusted to transport.
A callback that only enqueues work is not this boundary; queued work must be
checked at dequeue. Consume one-shot permission before the handoff. If anything
after that point may have sent bytes, the outcome is UNKNOWN until evidence says
otherwise; cancellation of a local task or a socket error is not proof-unsent.

A quorum proof is not a lock on the future. A remote kill can commit after proof
creation and before send; an unseen change may remain within the approved
freshness window. Observed changes/local latches refuse immediately at the next
admission check. Existing orders can still fill after kill. No Raft mechanism
retracts a request already accepted by TLS/kernel/network/venue.

There is also a clock-check-to-write scheduling gap: an arbitrary process pause
after the final time check cannot be ruled out by another C++ boolean. Tests
must inject pauses across the boundary, and deployment must state its timing
assumptions. A hard wall-clock send cutoff despite arbitrary pauses requires
enforcement at/beyond the transport (and may require venue expiry support).
Do not claim a finite venue-acceptance bound from an application timer alone.

## 6. TDD sequence after approval

1. Keep the two reproduced REDs; add them as permanent composed regressions when
   the approved proof API replaces boolean renewal. Do not hide them behind F3
   missing-context refusal. Positive fixtures must still send exact test bytes.
2. Review durable barrier bytes, type discriminator and recovery compatibility;
   implement the smallest commit/apply proof path with one bounded pending slot.
   Test refusal before durable quorum, stale term, replay and missing catalog.
3. Add the gateway one-shot pending state: delay, duplicate, wrong session,
   reordered/later request, restart, clock regression, overflow and expiry.
4. Bind controls/effect/catalog atomically; test kill before request, during
   commit, after response, before handoff and after handoff. Distinguish the
   permitted unseen-change window from forbidden stale renewal.
5. Compose a fake transport with observable bytes/venue effects, inject queue
   and encoding delays, test consumed-twice and proven-unsent versus unknown.
   Preserve cancel/emergency capacity and no-automatic-promotion behavior.
6. Mutation-check quorum/age/correlation/consumption/final-boundary guards,
   then full Release and sanitizer regression. Test 1 voter as a lab mode,
   2 voters for the pilot and 3 for majority-versus-isolated-old-leader traces.
   No one-voter production fallback. Target acceptance waits for F5/F6 and F7.

Owner approved: committed barrier first; one-shot request-start expiry with
disabled-by-default live bound; retained cancel exception and two local latches;
explicit bounded-observation/in-flight limits. The separate exact byte review is
also approved; do not request either approval again. Marker codec/replication
implementation begins at CP-011; CP-012 implements host send-proof issuance and
consumption. See `F4_AUTHORITY_BARRIER_BYTE_REVIEW.md`; that approval does not approve
external transport/authentication by itself.
