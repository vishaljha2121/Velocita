# F5 lost-voter repair: stopped two-voter admission

2026-09-23. **DIRECTION APPROVED after CP-029; exact protocol/bytes/budgets pending.**
CP-028 baseline, branch `v2/phase-5-corrections`, HEAD `d44a4a9`; remediation is
uncommitted. Choices A/B in `F5_COMPLETION_AND_NUC_PLAN.md` are already approved:
planned outage and real operator fencing; immutable archives without automatic
pruning, failing closed at capacity. This sheet does not reopen those choices.
No production file, shared handoff, CMake target, identity or economic byte is
changed by this lane. There is no repaired-node GREEN or F5/F6/F7 closure claim.

## 1. Smallest complete MVP proposal

Repair exactly one lost voter from one intact, non-rolled-back survivor, with
the same **two** voters throughout. Stop both voters and the gateway. Fence all
old processes and transport sessions. Seal one immutable donor bundle containing
its trusted committed base AND its entire durable suffix. Stage destination-local
generations for both participants, preserving shared identity. Persist a reviewed
new-term admission tuple, select both generations durably, then issue one paired
release certificate. Both startup paths require that certificate. Resume normal
two-voter consensus behind the fresh transport fence, and commit a current-term
barrier after the retained suffix before any gateway can obtain send authority.

This is a new **offline recovery admission protocol**. It is not normal recovery,
genesis, membership replacement, an economic rollback, or automatic failover.
Ordinary `recover()` must keep refusing missing term/vote metadata.

The proposal needs no new ordinary Raft message kind or shared cluster incarnation
IF the process and authenticated-transport fences below are enforced. The repair
certificate and generation records are new local control bytes requiring review.
Current bound-peer frames alone cannot supply the transport fence.

## 2. Current code: what exists and what does not

Source anchors refer to CP-028 and should be rechecked after integration:

| Anchor | Existing fact | Repair consequence |
|---|---|---|
| `include/velocita/v2/identity_startup.hpp`, `VerifiedIdentityStartup` | Only bootstrap and recover constructors; directory ownership token | No admitted replacement operation exists. |
| `src/v2_identity_startup.cpp:64`, `open_owner` | Exclusive `flock` on this directory inode | Another host or directory can still impersonate the same voter; not a remote fence. |
| `src/v2_identity_startup.cpp:96`, `bootstrap_contents`; `:187`, `require_zero` | Genesis rejects history and participating metadata | Do not bootstrap the old ID after removing its files. |
| `src/v2_identity_startup.cpp:214`, `validate_recovery_metadata`; `:298`, `recover` | Missing hard state refuses before serving | Correct fail-closed behavior, not the missing feature's bug. |
| `src/v2_static_identity.cpp:232` | Shared manifest excludes local voter ID | Local rebind can preserve cluster incarnation, voter set, catalog and authority domain. |
| `src/v2_bound_frame.cpp:22,89,131` | Local storage binds local owner; peers bind shared lineage | Donor local wrappers must be verified then re-encoded for the replacement, not copied verbatim. |
| `src/v2_replication.cpp:71`, constructor | Validates snapshot and all WAL, restores membership then metadata term/vote, checks contiguous suffix and committed coverage | Reuse validation semantics on fresh staged owners; do not invoke live snapshot load as repair. |
| `src/v2_replication.cpp:699`, `persist_commit_index` | Persisted commit is the locally durable known commit checkpoint | It is a lower bound, not an upper bound on decisions made by the old quorum. |
| `src/v2_replication.cpp:1022,1041`, persist callbacks | Term/vote persistence fences protocol callbacks | Repair must establish a new durable admission promise before voting. It cannot synthesize the lost promise from snapshot term. |
| `src/v2_bound_peer.cpp:166`, `decode_bound_peer` | Frame digest, manifest, route and typed-payload validation | No process/session replay fence or peer authentication. |
| `src/v2_replication.cpp:469,544`, authority proposal/currentness | Current-term, request-correlated marker and effect binding | Leadership, selected files, or a repair certificate alone cannot authorize a send. |

ADR 0001's committed outbox, unknown-risk retention, replay-without-delivery and
external sender-fencing invariants still govern. ADR 0005's fresh-server recovery
and ADR 0006's durable-voter counting still apply. ADR 0014 explicitly excludes
lost voting memory and complete same-node rollback from static identity's proof.

## 3. Why the full unknown-commit suffix is mandatory

Let donor D hold snapshot prefix S, durable log end L and persisted commit C.
Its trusted applied recovery prefix is `K = max(S, C)` after validation. Preserve
**every retained valid durable record**, including overlap below S for audit, and
the complete contiguous `(S, L]` suffix. Do not cut at C or K. Do not mark `(K,L]`
committed simply because it has been copied to both repair participants.

The permanent current-API characterization
`tests/lost_voter_repair_reachability.cpp` demonstrates two real executions:

| Trace | Lost old leader | Surviving donor |
|---|---|---|
| A | Durable index 1, actual quorum commit/apply 1, authored kill epoch 7 | term 1, vote 1, commit/applied 0, durable index 1 |
| B | Durable index 0, commit/apply 0 | term 1, vote 1, commit/applied 0, durable index 1 |

The two complete donor directory images are byte-identical. Their differing old
quorum outcomes are established by actual leader status/result and explicit
authored state expectations, not inferred from donor metadata. Thus no decoder
of that donor image can distinguish the histories. Discarding index 1 loses a
committed command in A; immediately applying index 1 claims nonexistent old
commitment in B. Retain it and let a renewed quorum legitimately commit it later.
A formerly uncommitted but admitted command may then acquire its first outcome;
clients must continue to treat uncertain admission as uncertain.

The fixture quarantines, rather than deletes, the lost leader's metadata. Normal
recovery returns exact `BoundMetadataErrorKind::absent` and leaves files unchanged.
Donor restart retains index 1 and one RESERVED result; it does not apply it or
produce authority. The characterization passes. It is **not** a behavioral repair
RED: no repair entrypoint exists to exercise. An unconditional failure or requiring
`recover()` to reconstruct missing votes would invent a test contract. The eventual
repair tests must call the approved entrypoint and retain this indistinguishability
case as an independent obligation oracle. Raw logs/provenance are in
`evidence/lost_voter_repair_2026-09-23/README.md`.

## 4. Preconditions: fences must act outside the lost process

Before sealing the donor:

1. Disable gateway delivery and both voter services, including automatic restart.
   Stop their process groups, drain/join disk workers on an intact process if
   possible, and acquire cold-start directory ownership. A process stop is not
   proof that earlier venue requests vanished; retain all ambiguous effects.
2. Fence the lost instance where it cannot undo the fence: power isolation or
   independent host/network policy denying its peer and venue egress. A timeout,
   missing ping, PID on the replacement, or lock file is insufficient. If the old
   host cannot be fenced, stay stopped. Do not ask the lost host to attest its own
   absence. Retain operator action, exact endpoint/host, and verification evidence.
3. Retire every old peer connection and transport replay queue on both endpoints.
   New authenticated sessions must bind each configured voter to its new process
   and connection instance. Old authenticated bytes cannot be replayed into the
   new connection. If old processes could reconnect with retained credentials,
   revoke those credentials or maintain the independent network/power fence until
   they are securely reprovisioned. A reconnect using the same unchecked static
   route is not fencing.
4. Keep the gateway egress fence until consensus recovery AND venue reconciliation
   are complete. Rotate/retire its old session and proof-consumer incarnation.
   Outstanding venue requests may still have taken effect; do not release risk
   or blindly resend on the basis of a stopped process.

These steps are requirements for a tested deployment runbook, not actions taken
by this review. There is no startable V2 production shell/authenticated transport
yet. The shell must refuse ordinary startup when a repair gate is present, absent
where required, corrupt, or mismatched; a bypassing old executable must also be
excluded by the service/transport admission boundary. Plain bound-frame SHA-256
is integrity, not authentication or authority to issue a repair certificate.

### Are new peer epoch bytes necessary?

Not for the scoped offline protocol if the fences above are real and survive
restart: no old participant can send, and no old-session frame is admitted to a
new session. Keep the existing peer payload bytes, immutable manifest and quorum.
Use the future transport's authenticated session binding rather than adding an
unreviewed global epoch to every log/command/peer object.

Without that boundary a concrete admission counterexample exists: D ended at term 5; lost X
self-voted in isolated term 6 and emitted a term-6 vote request. A repair that
sets both nodes to term 6/no-vote and feeds old queued frames to the current raw
decoder can grant that old request and mutate its admitted voting state. Current
frames have no connection instance to reject that old traffic. This is an
admission-fence violation, not a demonstrated committed-data-loss trace. Worse, a surviving
old X process can negotiate with D as the same static voter and form the only
quorum while the replacement also exists. The repair protocol cannot claim
voting-memory safety in that topology. Session replay rejection plus real
process fencing excludes it; a numeric peer epoch alone does not stop an old
gateway's already-authorized external request. If the transport cannot enforce
the fence, new peer admission/session bytes become a separate required owner
decision, not an implicit repair shortcut.

## 5. Immutable donor bundle and local rebind

Exactly one donor, one failed local ID and two unchanged voters per transaction.
Under exclusive cold ownership, seal and retain the original donor generation:

- Exact shared manifest and full verified execution catalog. Preserve all schema,
  replication/storage lineage and durability mode fields, including cluster
  incarnation and sorted voter set.
- Source-local identity, original term/vote/commit metadata, canonical snapshot
  with `(S, term[S], state digest)`, or an explicitly validated genesis base when
  S=0 and complete history begins at 1. Missing snapshot is not automatically
  genesis if prefix history was reclaimed.
- Complete original retained durable WAL with `(index, term, entry ID, type,
  payload)` through L, including authority markers, commands whose outcomes were
  refusals, and every unknown-commit entry. Preserve short-tail/corruption evidence;
  do not silently manufacture a missing complete record or discard invalid data.
- Exact immutable archive object and dependency-set digests needed for audit and
  any skipped-prefix/result reconstruction, plus validated coverage records.
  CP-028 archive codec validity alone certifies neither prior commit nor backup.
- Exact source generation, source-file inventory/digests, declared K/L and suffix
  root, plus source format/decoder versions. Once sealed, never edit this bundle
  to match a later donor state. An incomplete bundle is not usable admission.

The source snapshot proves only its covered committed base; metadata C contributes
only after the referenced history is validated. Archives can recover a missing
suffix only with independently verified lineage, exact continuity and appropriate
commit/coverage evidence. A newest-looking archive name or digest cannot repair
unknown lost tail. Refuse inconsistent/missing evidence and retain every version.

Validate each donor-local wrapper using donor identity, then re-encode only the
destination-local framing for the replacement's existing voter ID. Preserve
canonical commands, RequestSequence/CallerId, effects, executions, entry terms,
indices and payloads byte-for-byte. The shared manifest digest and F4 authority
domain remain unchanged. Never copy the donor's local ID, create a new shared
incarnation, change members, or relabel missing data as a new genesis.

Use the same `LocalStorageGeneration` selector contract proposed in
`F5_SNAPSHOT_INSTALL_REVIEW.md`: destination-owned, nonzero, nonwrapping u64,
immutable prepared base files and explicit durable selection. Once activated,
the generation's live WAL and hard-state metadata mutate under their normal
durability rules; it is the separately sealed ORIGINAL donor bundle that stays
immutable evidence. Never treat its inventory digest as a perpetual hash of the
later live generation. This review
does not approve that sheet's proposed byte offsets or invent a second selector.
Repair transaction ID, LocalStorageGeneration, ArchiveCatalogGeneration and
transport session instance are distinct identities. Copying a donor's generation
counter to the replacement is meaningless. A generation switch invalidates old
disk completions and reclamation capabilities on that destination.

Archive coverage is supplied by the archive-publication lane, not by repair.
Bundle pins name exact archive/dependency digests and the durable coverage-record
digest. Independent-copy receipts must bind a separate destination host/archive
identity; two directories on one disk are not two failure domains. A repair may
read that coverage but cannot mint it merely by copying records to a new voter.

## 6. Lost term/vote: the limited proof and its real cost

Let `T_D` be the **non-rolled-back** survivor's durable current term. Let T also
cover any higher authentic term recovered from the failed participant's retained
artifacts. The candidate repair floor is `R = max(T_D, T) + 1`, refusing arithmetic
exhaustion and reserving space for the subsequent ordinary election increment.
Both staged participants durably store `(term=R, voted_for=-1, commit=K)` under
their own local bindings. This clears the survivor's vote only in a strictly
higher term. Its original tuple and donor files remain in the repair evidence.
Staged metadata is not changed in the old serving directory.

**R is not proven greater than every term the failed process ever saw.** X may
have self-voted in arbitrarily many isolated election attempts while D was
unreachable. No snapshot hash or finite guess can recover that maximum. The
proposal deliberately makes an offline re-admission promise; it must not be
described as recovering the lost voter's original numerical term monotonicity or
proving that it never voted in a reused numeric term.

The narrower safety argument depends on all of these assumptions:

1. There have always been exactly these two voting participants for this history;
   every past quorum election/commit needed D. No older three-voter majority could
   have elected/committed without it.
2. D has not rolled back term/vote or lost acknowledged durable history. It records
   at least the term of every old quorum election it joined. R exceeds that term.
3. Every possibly old-committed log entry occurs in D's preserved durable history
   or covered snapshot. Standard durable two-voter quorum intersection establishes
   that fact, not the lagging local commit checkpoint.
4. Both admitted new participants start from that identical complete history and
   the reviewed new floor. Old processes, old-session votes, disk completions and
   transport frames cannot participate in this new election.

Thus no old quorum leader could have existed at/above R without D's term showing
it; forgotten higher private votes cannot constitute a quorum. After both durable
admissions, a new normal two-voter election can legitimately continue the retained
log. This is not a proof for three voters, a single surviving backup, changing
membership, unauthenticated transports, or a survivor with suspected rollback.

If the owner requires numerical monotonicity relative to every *forgotten* local
term rather than this fenced recovery argument, there is no safe finite term
choice from these inputs. Alternatives require an independently durable bound
on that voter's every term/vote, or a separately reviewed recovery epoch/migration
protocol. Neither exists now. Do not hide that choice behind `donor.term + 1`.

## 7. Both participants are gated: transaction and restart states

One offline coordinator is permitted for MVP; it is not a Raft voter. It consumes
the immutable bundle and actual fence evidence. Use fixed-size participant/control
records and a streamed immutable object inventory, with exact formats approved
before code. Each participant independently validates all named facts; a checksum
of an operator checklist is not a trusted grant.

Proposed logical records (not new encoded formats in this change):

| Record | Required bound fields |
|---|---|
| Repair descriptor | schema; unique transaction ID; prior repair certificate digest if any; shared manifest; exactly donor/replacement IDs; fence-evidence and retired/new transport-context digests; donor source generation/inventory root; original hard-state tuple; S/K/L and suffix root; archive/dependency coverage roots; R; both target local generations |
| Participant prepared receipt | descriptor digest; local ID; proposed local generation; sealed generation-manifest digest; resulting hard-state tuple and history/state roots; durable prepared status |
| Participant selected receipt | prepared receipt digest; exact durable selector identity/sequence; local ID; transaction ID; selected generation-manifest digest |
| Paired release certificate | descriptor digest; both selected receipts; exactly two participant IDs; both selected generation identities; authority-context/fence binding; release status; trusted coordinator authorization |

Identifiers never wrap or get silently reused. Repeating a request with the exact
same ID and all bytes is idempotent. A changed digest under the same ID quarantines
the transaction. Hashes bind content, not coordinator authentication. Files remain
owned by the trusted operator/service boundary; future remote coordination needs
authentication and explicit replay protection, not a self-signed hash assertion.

| Durable state after a cut | Required restart behavior |
|---|---|
| Stopped, neither prepared | Old storage remains evidence; no replacement may bootstrap. Resume repair only after fences revalidated. |
| One/both bundles partly staged | Ignore partial data for service; validate exact immutable inventory and resume bounded copy. No term/vote use or packet sends. |
| Both prepared, neither selected | Remain stopped; compare both receipts, bundle, generations and fence identity. |
| Only one selector durably advanced | Selected participant cannot vote yet; no paired certificate. Complete the other selection or remain stopped. Never fall back to an old generation. |
| Both selected, no certificate | Both remain quarantined; coordinator can reconstruct receipts from durable named artifacts and finish the same transaction. |
| Certificate delivered to only one | That node can start only the admitted repair-session consensus path; alone it cannot form quorum. Other node stays gated until it validates the same certificate. Gateway remains fenced. |
| Both released but barrier not committed | Election/replication may converge; no new economic proposals or external sends. Crash resumes this restricted state. |
| Barrier durably committed/applied, gateway not reconciled | Consensus recovery has finished; gateway sends remain disabled until recovery/authority requirements finish. |

Publishing both local selectors is not an atomic distributed rename. Safety comes
from both being selected before a release certificate can exist and from two-of-two
consensus after release. The coordinator may fail indefinitely; availability stops.
Missing/corrupt selector/certificate or an unknown generation never means “use the
old one.” A valid older certificate cannot release a later in-progress repair.
Trusted startup configuration must require the selected-generation scheme after
migration; removal of the marker cannot restore legacy startup implicitly.

During the outage donor files and READY/RESERVED ownership need deliberate handling.
Do not advertise graceful repair as making unread results disposable. Where an
intact process and consumer remain, drain READY results through the existing
consumer handoff before stopping, with acknowledgements established where the
existing protocol permits them. If quorum is already lost, do not pretend a new
replicated acknowledgement can be committed anyway. A crashed process's volatile
notifications cannot be invented; use existing restart retry/reconciliation
semantics, stable request receipts and gateway acknowledgement coordinates.
This sheet adds NO persisted ResultLedger image. The live snapshot installer
retains its existing in-memory ledger owner; a stopped-process repair cannot
claim the same ownership survives a crash. Recovery reconstructs RESERVED
obligations from the retained uncommitted suffix as current startup already does.
If a repair later attempts to skip a prefix containing live reserved obligations,
historical replay is required; applying their commands to final snapshot state
does not recreate original outcomes. No log deletion follows merely because a
repair was released.

## 8. Renewed quorum and gateway admission

Both selected logs agree through L. Permit only recovery consensus until a normal
new-term entry after L becomes durably committed by both voters and applied.
The smallest proposed barrier uses an existing operator `SetStrategyMode(halted)`
command under a fresh stable repair request identity. No new economic command
kind is needed. Separate the two conditions: commitment of that current-term
entry establishes the Raft prefix barrier even if its economic result refuses;
gateway release additionally requires an accepted halt and actual halted state.
The first command can use the known prefix's expected kill/executor generations.
Unknown suffix commands may change those generations before it applies, causing
`stale_control_generation`. After the suffix is committed/applied, issue the halt
again under a NEW stable request identity using the now-known generations. Accept
`applied` or `already_applied` with halted post-state; an already-halted state is
not a failed repair. Other results keep the gateway fenced. These are the current
`apply_set_strategy_mode` semantics (`src/v2_kernel.cpp:713`), not a new reducer
exception. Do not reuse an authority marker tied to a possibly nonexistent order
effect as a generic cluster-repair barrier.

Appending the current-term barrier after L prevents silently dropping the donor
suffix; commitment resolves previously unknown entries through L in log order.
Do not inject the halted state into a snapshot or skip suffix commands to reach
the barrier. Consumers must retain result notifications for newly resolved
commands. This may require bounded incremental drain before all suffix outcomes
can publish; do not defeat result quota to accelerate repair.

**Saturation requires a further concrete admission decision before this proposal
is complete.** Admission permits all 1,024 result reservations or all 8,192
retained cells to be occupied. In particular a result-full unknown suffix can
leave no result slot for L+1. The maximum all-unknown-marker log is a structural
recovery boundary, not an honest live two-voter trace established by this lane.
Any occupied committed prefix is reclaimable only with snapshot/archive coverage;
it is not automatically free. Result drain cannot free RESERVED slots before commitment,
and ordinary old-term entries cannot commit just because both repair copies exist.
In that state the proposed result-bearing halt/barrier cannot be appended at L+1.
Even a new result-free marker would not solve a completely full retained log.
Current finite operator reserves do not promise headroom after all controls use
them. Preserve the full suffix and stay stopped; do not truncate it or claim a
result-consumer drain solves this circular dependency. Owner review must choose
protected repair-only barrier headroom with an explicit migration/full-history
rule, or a separately bounded cold-recovery admission mechanism. This review
does not silently increase approved quotas or invent a new entry type.

Recommended new admission partition, **PROPOSED, not a change to CP-025**:
retain the physical 1,024-result/8,192-log capacities; reduce ordinary admission
to 959 result slots and 8,127 log tokens, keep the existing 64 protected control
slots/tokens, and dedicate one of each to the internal agreed repair barrier.
Both normal and ordinary operator-control admission must be unable to consume
that internal reserve. Current `ResultLedger::reserve`/`RetainedLog::append`
consume the protected pool FIRST for control-eligible commands, then ordinary;
`can_reserve`/`can_admit` simulate the same order. The 64 are therefore not a
perpetually spare emergency slot. Only operator callers carrying AdvanceKillEpoch,
SetExecutorGeneration, SetStrategyMode or RequestMassCancel are classified control
(`src/v2_replication.cpp:24`); changing the public lane label cannot establish
internal repair eligibility. A future repair admission token must bind the exact
certificate, request and current recovery state on both sender and receiver.

One reserved result-bearing halt entry can establish the new-term commitment
barrier. Drain its owned result and newly committed prefix results, then obtain
verified archive/snapshot coverage to reclaim cells before a required follow-up
halt; stop if consumers or archives cannot progress. A result-free new marker
avoids the result reservation but still needs log space and introduces new typed
log bytes, so it is an alternative requiring separate owner approval, not a
reason to overload the existing effect-correlated authority marker.

This changed partition protects FUTURE admissions only. Already-full histories
do not acquire a spare cell retroactively. They require an explicitly reviewed
bounded cold-rehydration/controlled scratch path, or remain safely stopped with
that availability limit named. Tests must exercise both full pools and the old-
to-new admission boundary before an end-to-end repair claim. No complete repair
availability guarantee is made until this choice and migration rule are settled.

Gateway restart is separate: retain unknown reservations, reconcile live orders
and lost submissions, renew the configured executor/gateway incarnation and
request correlation as required by the existing F4 contract, then obtain fresh
per-effect quorum authority. An old proof or metadata role sample cannot cross
the outage. No automatic new-risk resume and no claim that repairing Raft undoes
an HTTPS request already in flight. Any final strategy unhalt remains an explicit
operator action through the existing committed control command.

## 9. Limits, costs and alternatives requiring owner review

### Irrecoverable inputs

- **Both voting memories lost:** this protocol has no intact survivor term/vote
  anchor and refuses. Numeric monotonicity relative to both forgotten histories
  cannot be reconstructed from entry terms. Complete independently established
  histories plus stronger fences could support a DIFFERENT reviewed recovery
  proof; loss of votes alone is not claimed to prove economic data is gone.
- **Neither complete authoritative history nor independent latest commitment
  evidence survives:** current immutable archive
  segments do not certify a complete latest history or every forgotten vote.
  The same archive prefix may precede different acknowledged tails. No algorithm
  can select the correct continuation from indistinguishable inputs. Stay stopped;
  a later audited migration with independent authoritative evidence is a separate
  decision, not this repair protocol. External venue reconciliation cannot recreate
  all missing internal causal commands or Raft votes.
- **Donor restored/rolled back or suspected incomplete:** a locally consistent
  identity/snapshot/WAL can be an old valid copy. Checksums prove neither freshness
  nor absence of missing acknowledged data. Refuse known/suspected rollback.
  Undetected storage rollback is outside the durable-survivor assumption; this
  design does not detect it. External monotonic witnesses could strengthen that
  assumption only under a separately reviewed update/durability protocol.
- **No verifiable old-process/transport fence, missing archive audit coverage,
  ambiguous competing bundles, foreign lineage, or resource exhaustion:** remain
  quarantined and preserve risk/history. Operator approval cannot make absent bytes
  exist or convert a digest into proof of commitment.

### Cost and bounded ownership

One active repair transaction and exactly two participant receipts are sufficient;
no online membership protocol is required. While the services are stopped, prepare
one candidate owner at a time, avoiding simultaneous serving/candidate owner RAM.
Do not claim a small transfer buffer bounds total memory: the current retained log
alone owns 16 MiB of cells plus descriptors, result ledger and other owners.

Disk needs the retained original generation, immutable donor bundle, each selected
candidate generation, archive/dependency objects and control records. This can
temporarily require multiple full copies, including the complete unknown suffix;
deduplicating with mutable shared files would break independent crash recovery.
Pinned immutable verified objects may be shared only through the archive contract.
Quota accounting must include staging, failed/abandoned attempts and independent
export backlog before the repair starts. No auto-prune or history erasure to create
space. Exact control-byte layouts, inventory bounds, per-transaction disk/RAM/work
budgets, sync ordering and error codes still need the coordinated owner review.

### Decisions requested, in order

1. Approve the **limited two-voter fenced re-admission proof**, including the
   distinction between an intact survivor's monotonic term and unrecoverable
   numeric history of the lost voter. Or require an external term witness/epoch
   protocol instead, with its extra persistence and transport cost.
2. Approve using enforceable old-process and authenticated-session retirement
   instead of new ordinary-peer epoch bytes. Require a tested real fence runbook;
   fail if the transport cannot implement it.
3. Approve staged local generations on BOTH participants and a paired release
   certificate referencing two durable selection receipts. Choose exact new local
   control records alongside the common selector and archive contracts, not by
   adding ad-hoc files to the current bootstrap directory.
4. Approve the known-prefix K/full-suffix L distinction, repair term/vote tuples,
   obligation ownership and current-term `SetStrategyMode(halted)` barrier before
   gateway reconciliation/unhalt. No automatic economic state reset.
5. Approve measured bounded storage/transfer/control inventory budgets and recovery
   error handling. Existing 4-MiB/8,192 archive codec caps are not a global repair
   quota, and the 8,192-cell log cap is not proof all entries have result slots.

Rejected alternatives: old ID as genesis; live-directory copy; snapshot entry term
as vote memory; donor commit as deletion watermark; applying every copied record;
donor vote copied as the replacement's former promise; one-voter weakening; an
unbounded retry/copy loop; rollback to the old selected generation after publication.

## 10. Required implementation evidence after approval

Build the actual admitted repair API first with explicit state gates; retain
original behavioral REDs for missing steps before their fixes. Cover at least:

- both indistinguishable donor histories above, ending with retained bytes and
  exact newly committed outcomes; no pre-barrier send or auto-release of risk;
- wrong voter/manifest/catalog, private lost terms above R, stale old vote/append
  packets, old-process reconnect attempts, and failed fence verification;
- cuts after each write, file sync, rename, directory sync, selector and paired
  certificate publication; true modeled durable images separate from process exit;
- both partial-selection directions, only one delivered certificate, repeated
  transaction IDs, conflicting repeats, stale receipts and stale disk completions;
- donor rollback/dual-loss refusal, unavailable archives, capacity/full behavior,
  result READY/RESERVED preservation, unknown suffix at capacity, and second repair
  after a partially completed first transaction;
- independent exact tuple/byte/log-state expectations, mutations deleting each
  critical fence, and fresh complete host/target gates at a frozen accepted source.

This review and the characterization do not exercise any new bytes, cross-host
fencing, power loss, crash-resumable repair or gateway/venue behavior. They make
the remaining admission decisions concrete without claiming the feature exists.
