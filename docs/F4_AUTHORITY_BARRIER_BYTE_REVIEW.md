# F4 authority barrier: exact durable bytes and bounded admission

2026-09-22. **APPROVED by owner, including the explicit availability trade-off.**
The semantic direction in ADR 0013 is already APPROVED. This review does not
reopen marker-per-attempt, request-start expiry, one-shot consumption, the cancel
exception, or disabled-by-default live freshness. Implementation status is tracked
in CURRENT_HANDOFF and remediation checkpoints. External IPC, authentication and
F5 membership remain separate.

## 1. Recommendation and actual compatibility boundary

Use V2-local Raft entry type **101**, magic **V2AB**, marker schema **1**, and a
fixed **186-byte** canonical frame. The pinned `raft.h` reserves custom types
strictly above `RAFT_LOGTYPE_NUM=100`; do not modify that shared enum or V1.
`WalRecord::entry_type` and peer log entries already carry the discriminator,
so no new WAL container version or peer message kind is needed for this body.

This is not business command kind 21. A barrier advances the Raft applied index
without calling the economic reducer: no StateVersion advance, request receipt,
order, balance, reservation or outbox change. Command/result/snapshot schema 8
and existing OrderEffect schema 2 stay unchanged. Consequently there is no
TradingState/snapshot budget increase. The frame is per attempt in retained log
history, not another 186 bytes embedded in every order.

Current constructor WAL preflight, `before_apply`, and `apply_log` in
`src/v2_replication.cpp` decode every payload as a kernel command. Each must
dispatch on the exact type; so must admission and any standalone replay path
that consumes typed log records. Unknown or malformed types fail closed before
applied-index advance. Never guess a type from whether command decoding fails.

Old V2 readers will refuse retained markers. After compaction, an economic
schema-8 snapshot alone might still be readable by old code: this frame is NOT
an anti-rollback guarantee. No mixed-version or old-authority deployment is
approved. F5 persisted protocol/identity admission and F7 deployment checks must
prevent an old executable serving after the new contract is enabled.

## 2. Canonical frame

Offsets are zero-based and inclusive. Integers are little-endian; digests and
incarnation bytes are copied verbatim. Never serialize a C++ struct or padding.
The body length excludes magic, length, schema and trailing digest.

| Offset | Bytes | Field |
|---|---:|---|
| 0–3 | 4 | ASCII `V2AB` |
| 4–7 | 4 | u32 body length = 144 |
| 8–9 | 2 | u16 marker schema = 1 |
| 10–25 | 16 | Gateway process incarnation |
| 26–33 | 8 | u64 request sequence |
| 34–41 | 8 | u64 EffectId |
| 42–73 | 32 | Expected existing OrderEffect payload digest |
| 74–81 | 8 | u64 expected KillEpoch |
| 82–89 | 8 | u64 expected ExecutorGeneration |
| 90–121 | 32 | Expected execution catalog root |
| 122–153 | 32 | Authority domain digest |
| 154–185 | 32 | SHA-256 of bytes 8–153, inclusive |

Every identity, expectation and domain field must be nonzero; opaque arrays
must not be all zero. Reject wrong type/magic/schema/length, truncation, trailing
bytes and digest mismatch. Failed encode leaves the destination unchanged;
failed decode leaves the output candidate unchanged. Independent golden bytes,
every truncation, wrong-field mutations and destination-preservation tests must
pin this rather than using encoder output as its own oracle.

The effect digest is the EXISTING `OrderEffect::payload_digest`, whose domain is
`velocita/order-effect-payload/v1`. It is not SHA-256 of the serialized 166-byte
effect. This extra 32 bytes binds exact intent, not merely an EffectId. Historical
specification/capability versions are outside that digest and must independently
match the copied BoundOrderEffect at gateway acceptance. The catalog root binds
the immutable catalog but does not excuse skipping those version checks.

The trailing SHA follows the existing schema-plus-body framing convention;
WAL CRC remains in place. Neither checksum authenticates a sender. They detect
corruption/noncanonical content at different framing layers, not malicious peers.

## 3. Domain, controls, and what deliberately is not durable

The authority domain is an explicit nonzero 32-byte value shared by the node
and gateway configuration; marker admission/recovery requires equality. Its
production derivation must bind the deployment/static configuration under F5.
This review allocates the equality field, NOT a membership serialization or an
assertion that such configuration is already persisted. Tests inject a fixed
synthetic domain. Missing domain means authority disabled, never a zero default
or inferred local-node identity. Live admission remains blocked on F5 and
authenticated bounded IPC.

Expected kill/generation/effect/root are preconditions, not permission to adopt
new values from a reply. At committed live apply, a changed control, missing or
non-deliverable effect, or mismatched intent produces NO grant. The well-formed
marker still advances the replication prefix as a no-op; it does not emit a
business ApplyCode or mutate economics.

Static type/frame/domain checks must not require an effect to exist while a
follower merely stages the entry: preceding commands may not yet have applied.
Historical replay likewise cannot require a reclaimed effect to rematerialize.
Replay validates the marker and advances the prefix but NEVER restores a grant.
Required catalog dependency failures remain local fail-closed node errors under
F3, not invented deterministic economic refusals.

Raft supplies term/index; the gateway owns its monotonic start clock. None is
caller-supplied in this durable body. A live, owned local result carries request
correlation, domain, issuer/term/barrier/applied coordinates, coherent controls
and StateVersion, and exact BoundOrderEffect. Its C++ representation is not an
IPC byte contract. It must be created and dequeued only while the issuer remains
leader in the barrier term, for the exact still-live request. Known newer fences
invalidate queued permission. Already transmitted IPC cannot be recalled.

Gateway acceptance compares the exact pending identity and expectations, verifies
applied coverage, and consumes permission once at final transport admission.
Use `now >= start && now - start <= bound`, with a nonzero bound, preserving
the existing inclusive age boundary without overflow-prone `start + bound`.
Response arrival cannot reset start. A result is not durable permission; restart
drops it. This alone cannot prevent arbitrary pause after the final clock check.

## 4. Bounded slot, retry pressure and the follower trap

MVP has one local outstanding authority request and one fixed result slot, not
a map keyed by all historical requests. Timeout, disconnect or cancellation can
detach the subscriber but cannot erase an unresolved marker from the Raft log.
An identical pending retry reuses its existing index; changed bytes under the
same correlation refuse. A new request while blocked is busy, not another append.
Markers use normal capacity, never the emergency kill reserve.

Also retain a conservative **drain watermark**: the highest retained marker
index above local applied, with its log identity. New local marker admission
requires no such unapplied marker. Startup/leadership acquisition reconstructs
this from retained history; incoming markers advance it. Applying earlier
markers does not clear a later watermark. If a conflicting suffix removes it,
recompute the highest remaining unapplied marker rather than blindly clearing.
Restart reconstructs only this drain obligation, never a subscriber or grant.

Important correction from review: a healthy lagging follower can hold markers
A, B and C above its applied index because the leader applied each before
issuing the next. More than one is NOT corruption and must not reject recovery
or AppendEntries. A one-slot local admission rule is not a one-marker-history
invariant. One watermark subsumes the backlog without a per-marker F4 allocation.

Term change invalidates local permission but does not clear retained obligations.
The pinned library does not append an automatic current-term leader no-op. An
old-term marker therefore may block authority issuance until an ordinary/control
current-term command commits the prefix, or conflict resolution overwrites it.
This is a real fail-closed availability cost, not an implementation detail to
hide. Do not silently introduce an extra bridge marker/no-op. A later command
drains old markers without retroactively authorizing their abandoned requests.

Result-slot saturation must never block Raft apply or emergency controls. New
request admission refuses while the result slot is occupied; the owner must
explicitly consume or discard an abandoned/invalidated result before reuse.
If publication unexpectedly encounters a full slot, withhold the new permission
rather than overwrite an unread result or block apply. Only the exact gateway
pending request can consume a returned result. Permission loss means another
fresh attempt, not restoration of the old permission.

The precise bound: repeated timeout/retry on an isolated leader cannot append
another authority marker while its first remains unapplied. This does NOT bound
total WAL history, follower lag, churn across divergent branches, or resolved
markers accumulated over time. Retention and suffix-scan cost remain F5. Fixed
auxiliary C++ slots live outside TradingState and need measured size assertions;
no existing replication queue is claimed newly bounded by this design.

## 5. Implementation and acceptance after byte approval

1. Preserve CP-009 RED evidence. Add independent codec REDs for type 101 and this
   frame; implement the codec without changing business schemas or V1.
2. Dispatch across peer/WAL/pre-apply/live apply/replay and snapshot restoration.
   Pin unchanged economic bytes and no grant on restart, malformed marker, stale
   controls, missing dependency or insufficient durable quorum.
3. Pin the slot traces: isolated leader plus repeated retries appends once;
   lagging follower accepts A/B/C; restart preserves watermark C without grants;
   removal of C leaves B blocking; old-term drain grants nothing. Full result
   slot must not obstruct kill application. No implicit leader no-op.
4. Replace boolean renewal with owned proof issuance/one-shot gateway state.
   Make partition/delayed-reply regressions GREEN while the healthy composed
   fixture still sends independently expected bytes. Mutate quorum, age,
   correlation, intent/version binding and consumption guards individually.
5. Build before focused/full Release and sanitizer tests. Keep IPC, F5/F6,
   real encoder/venue admission and target gates open. No live sender or arbitrary
   freshness duration is authorized by host GREEN.

**Owner approved:** type 101 / 186-byte frame, explicit domain equality
with production derivation deferred to F5, and conservative drain admission with
the stated old-term availability cost. Do not request these approvals again.
