# F4 marker prerequisite — host evidence, 2026-09-22

Owner approved type 101, the 186-byte V2AB frame and conservative drain rule.
This implements the replication prerequisite, NOT live send authority. CP-009's
partition/delayed-reply send defects remain open until the gateway proof API
replaces boolean renewal. No targets, venues, credentials, commits or tags touched.

## Intended REDs and smallest GREEN

- Codec stub: `golden encode refused`. Fixed codec matches independent Python
  struct/hashlib golden bytes: body 144, frame 186. All short inputs/outputs and
  failed-output preservation are tested.
- Replication stubs: `old-term marker setup failed`, `authority marker was not
  admitted on the normal lane`, `isolated leader marker setup failed`. Typed
  dispatch now validates markers at WAL preflight, peer admission and pre-apply;
  apply is a replication-only no-op, never a reducer command/result.
- Zero-durable follower: `follower persisted committed index beyond zero local
  durability`. Fixed persisted commit to min(raw commit, local durable), including
  zero. A follower may apply known quorum commitment in volatile memory before
  local sync; it must not claim those bytes on restart.
- Truncation failure: `storage-failed node accepted a follow-on authority
  operation`. Old code released payload ownership before fallible truncation,
  while Raft still retained the entry. Ownership now survives until storage
  succeeds; failure latches the node closed. Public tests pin follow-on refusal,
  original persisted bytes and recovery. Allocation-lifetime ordering is reviewed,
  not directly observed through the private pointer in this test.

Transient compile errors during integration are not counted as RED evidence.
The initial snapshot fixture tried compacting one entry; the pinned library
requires at least two. Corrected the fixture, not the library.

## Availability proof and limits

The three-voter test appends a marker, delivers it but strands the replies, then
restarts voters. A higher-term leader retains that old marker. Heartbeats neither
commit it nor append an implicit no-op; 100 new correlations stay busy without
log growth. A real current-term kill commits through the control lane, carries
the old marker into the applied prefix, and unblocks admission. The same trace
runs through the ordinary proposal lane. No marker produces a business result.
No permission API exists yet: queued live-grant invalidation is not proven here.

The follower fixture stages A/B/C without local apply and recovers all three.
Replacing C retains B as the drain obligation. Applying A does not clear B;
applying B clears it. Removing the final unapplied marker also clears it. These
are typed AppendEntries fixtures, not one leader admitting all three at once:
a follower can receive history after the leader resolves markers sequentially.

Drain is derived at public owner boundaries by reverse scanning the existing
retained-payload index and checking actual Raft entry types. No second per-marker
map exists. Time O(retained unapplied suffix), auxiliary space O(1); fixed pending
storage has a 192-byte static ceiling. Total history/scan bounds remain F5.
Scanning inside log_pop would be wrong: Raft has not removed its entry yet.
Only new authority admission is blocked, never normal/control apply.

The one-voter case proves marker-only state and snapshot replay, not production
fallback. The multi-voter old-term stall is not universal: the pinned single-voter
durability path can commit its retained prefix directly. Snapshot-after-volatile-
apply was tested separately: compact flushes WAL and acknowledgements cover the
saved prefix. No additional snapshot fix was needed.

Default-zero domain disables marker admission; malformed/unknown/foreign-domain
peer entries throw before Raft/storage mutation. Wrong-domain recovery preserves
evidence. Replay need not rematerialize an already reclaimed effect. None of this
proves production domain provenance or authenticated IPC.

## Mutation evidence

Direct compilation avoids unreliable incremental mutation builds. Removing the
drain guard dies at `retry bypassed the old-term drain obligation`. Restoring
the commit clamp dies at the zero-durability assertion above. Codec mutations
disable nonzero validation, bypass digest rejection, remove exact-length refusal,
and bypass decoder-only validation on a zero field with recomputed valid SHA.
Each dies at its invalid-frame assertion. An earlier inverted digest comparison
only broke valid golden decoding; it is not counted as corruption-rejection proof.

## Verification and next boundary

Final build/test logs are retained here. UBSan runtime halt enabled, ASan leaks
disabled; this is the existing host C++ instrumentation configuration, not a new
cross-platform gate. Independent required-test inventory includes both new suites.
No source pin regenerated; TradingState, economic snapshots and business schema
unchanged. No live sender is enabled.

Final rebuilt Release **40/40** (2.73 s), Debug ASan+UBSan **40/40** (40.12 s),
offline tools **40/40**, passive model **109/109**. Main checked actual CTest
inventory against the independent gate list: **40 total / 37 V2**. Diff check
clean; protected tracked files clean, model still protected/untracked. Sanitized
and Release suites used separate temp roots to avoid fixture-directory races.
Independent full-frame SHA-256:
`08d9ab385b267bc1eac2b45c2edc34c13bfa8a0fb42d6357658ae66c8c52f163`.

Next: bounded live result slot and exact copied effect/control binding, followed
by gateway correlation, request-start expiry and one-shot transport consumption.
Pin term/kill changes at issue/dequeue; occupied/lost results cannot block kill
or regenerate permission. Make CP-009 composed positive/negative traces GREEN.
External IPC/authentication, F5/F6, real encoder admission and target gates remain
open. Broader callback failure handling (including log_offer staging errors) is
not proven by this targeted failed-truncation regression.
