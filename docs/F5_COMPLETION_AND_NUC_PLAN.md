# F5 completion and the shortest safe NUC launch path

2026-09-23, CP-026. **Choices A/B APPROVED after CP-026; NOT new byte-contract
approval.** Branch `v2/phase-5-corrections`, HEAD `d44a4a9`; remediation remains
uncommitted. ADR 0014's accepted direction still governs. No new production
protocol, capacity, retention period, deployment or live-trading permission is
implied by the request to parallelize implementation.

CP-028 update: the owner subsequently approved the exact standalone archive
codec in `F5_ARCHIVE_SEGMENT_BYTE_REVIEW.md`; that component is implemented.
Publication, durable coverage/dependency ownership, global quota accounting,
snapshot transfer/selection and repair admission remain separate review slices.
Neither original integration RED below is fixed by a pure byte codec. Evidence:
`evidence/f5_archive_codec_2026-09-23/README.md`.

## 1. What remains, and why three items is not three small patches

| Workstream | Required outcome | Why it matters |
|---|---|---|
| Archive-backed reclamation | Delete covered working history only after verified recovery and audit coverage, with no outstanding borrow | A snapshot retains state, not the ordered commands explaining that state. |
| Transactional peer catch-up | An intact but lagging voter crosses a compacted prefix, preserves obligations, and resumes bounded replication | Retrying AppendEntries cannot supply entries that no longer exist in the sender's active log. |
| Lost-hard-state replacement admission | A replacement cannot vote by merely presenting the old node ID and a snapshot | State recovery does not recover forgotten term/vote promises or fence another process using the same identity. |

Cross-cutting F5 work remains: transfer/archive and owner-work bounds, saturation,
failure/cut tests, independent oracles, mutations and integration. CP-027 ports
the actual bound-metadata persistence model; F6's async fences, composed protocol
cuts and target validation remain open;
F7 still requires an externally pinned target candidate. These are not hidden
inside a claim that only three tasks remain before trading.

## 2. Work already started in parallel

Two bounded implementation workers added separate permanent current-API REDs;
main reviewed their fixtures and independently compiled/ran both against the
fresh Release archives. No shared production file or CMake target was changed.

- `tests/v2_snapshot_catchup_red.cpp`: three voters; isolate one, commit twelve
  authored commands on the other two, compact both healthy replicas, reconnect,
  route 2,000 rounds. The lagger remains at applied/commit 0 instead of 12.
  Expected kill epoch and state version are explicitly 12; an independent reducer
  fixture also supplies the expected canonical state digest. This tests replication,
  not independent correctness of the reducer implementation.
- `tests/v2_archive_reclamation_red.cpp`: commit an independently pinned canonical
  command, verify its raw bytes exist, then compact through three committed entries.
  Snapshot state is valid but those raw bytes are absent from every fixture file.
  This proves loss, NOT that finding bytes in an arbitrary file would prove a
  verified archive. The eventual GREEN must check typed durable coverage, not
  bless a stray temporary file. The old diagnostics remain as historical evidence.

Both fail at the intended behavioral assertion, not compilation/setup. Both
refuse an existing fixture root. They remain outside passing CTest until their
fixes exist; the fresh 59/59 baseline does not cover away these failures.
Evidence: `evidence/f5_completion_review_2026-09-23/README.md`.

## 3. Recommended implementation order and ownership

| Step | Worker A | Worker B | Main integration/review exit |
|---|---|---|---|
| Now | Catch-up RED and Raft-load hazards | Archive-loss RED and repair challenge | Current failures reproduced; this plan recorded. |
| After contracts | Bounded immutable archive publication, verification and coverage tests | Bounded snapshot staging and generation-selection transaction tests | Components share reviewed lineage/range/digest semantics; no kernel deletion yet. |
| Integrate F5 | Archive-assisted replay and reclamation preconditions | Snapshot transfer/restart/cut matrix | Main owns shared replication callbacks, result ledger and active-generation swap. |
| Repair | Offline replacement bundle/admission checks | Adversarial duplicate-node, partial-install and unknown-suffix tests | No voter starts from partial repair; restart/convergence and authority barrier proven. |
| F6 | Compose CP-027 bound-metadata model with install/async cuts | Reviewed async fence/completion work | No early vote/ack/proof, no routine owner-thread fsync, stale completions rejected. |
| Verify | Cross-subsystem saturation/cuts/mutations | Gate inventory/manifest and independent review | Build then full host tests; freeze only after all required REDs are meaningful GREENs. |

At most two workers. They may own distinct files/components, not competing edits
to the replication owner or the same build directory. Main validates each result
and integrates once shared ownership is settled. F6 model work may overlap once
a worker is free: it changes no byte contract. Async protocol changes cannot.

## 4. Review choice A: planned outage for lost voting memory

**Recommend an explicit operator-fenced, stopped-cluster repair for this two-voter
MVP**, not automatic online replacement/membership negotiation. Ordinary catch-up
with intact local hard state remains automatic and is a different operation.

Required repair preconditions and proof obligations:

1. Stop both voters and gateway. Fence the lost/old instance and its transport so
   it cannot reappear as the replacement's identity. A directory lock on one host
   is not a cross-host fence. An operator statement must correspond to a tested
   runbook action; a checkbox is not proof of a dead remote process.
2. Verify the surviving identity, immutable catalog, snapshot, complete retained
   suffix and archive coverage. Preserve the original storage; refuse incomplete
   or conflicting evidence. This is not a license to pick the newest-looking file.
3. Carry canonical command/effect/request identities unchanged. Rebind only the
   approved local storage framing to the destination voter. Copying the donor's
   local-owner frame verbatim, or declaring a new cluster incarnation to bypass
   validation, is not a repair.
4. Preserve the entire durable suffix, including entries above the survivor's
   persisted commit checkpoint. That checkpoint can lag a real quorum decision.
   Do not discard those entries as definitely uncommitted, nor apply all of them
   as definitely committed merely because they were copied. Normal renewed Raft
   commitment must resolve that uncertainty under the reviewed admission rules.
5. Publish resumable repair/install state and establish a safe voting admission
   barrier before either repaired participant serves. Precisely how term/vote and
   fencing evidence establish that barrier is a required protocol proof, not
   something solved by matching a snapshot hash. Partial repair stays quarantined.
6. Resume with the unchanged two-voter configuration, converge through a fresh
   quorum barrier, and re-establish gateway authority/reconciliation before sends.

Why this is MVP-focused: two voters already lose quorum when one is unavailable;
an automatic membership/replacement epoch protocol buys less availability than
its implementation and proof cost here. Planned downtime is explicit. It cannot
repair loss of both authoritative histories or make an incomplete archive whole.

Rejected: fresh genesis under the same ID; copy a live directory; synthesize lost
hard state from the snapshot's entry term; automatically weaken to one voter.
An online replacement protocol remains a possible later iteration, not a silent
deletion of the required replacement recovery path.

**Owner APPROVED after CP-026:** planned-outage repair is the MVP mode.
This selects the mode; it does not by itself approve missing admission bytes.

## 5. Review choice B: archive retention and availability

**Recommend immutable local archives with no automatic pruning for MVP**, plus a
verified independent copy before claiming host-loss audit protection/live two-host
acceptance. A local archive is useful for NUC development and working-log deletion
ordering; it does not survive loss of that host. Another voter's active WAL is not
an archive because it may compact the same history.

Coverage must include the exact committed typed records, including authority
markers (which emit no business result), causal commands producing refusals,
index/term/type/identity coordinates, and required immutable catalog/policy
dependencies. Portable lineage must be separate from source-local provenance.
A current state digest or retained result notification is not a substitute.

Before reclaiming working entries: snapshot coverage durable; contiguous archive
coverage verified under the declared failure model; no writer/Raft/payload borrow
still naming the reclaimed cells. READY result values are independently owned:
they must survive, but do not pin raw log cells merely because they remain unread.
This corrects an overbroad worker suggestion that every unread result must block
all reclamation. Only actual dependencies should pin storage.

Publication must fail closed on short writes, missing dependencies, checksum or
lineage mismatch, interrupted sync/rename and stale coverage acknowledgements.
No source deletion merely because a copy operation returned successfully.

The previous candidate budgets remain PROPOSALS: 4 MiB segments, 16 MiB unexported
backlog with ordinary admission stopped at 12 MiB; 1 GiB local archive quota with
768 MiB warning and 896 MiB ordinary-stop thresholds. They are not workload-derived
retention durations and are not encoded. Count physical staging/generations and
in-flight worst cases; reserve is finite. An archive/export outage can eventually
stop ordinary fills as well as new orders. Keep unknown exposure held and make
that stop visible. Never silently erase history to recover availability.

**Owner APPROVED after CP-026:** no automatic archive pruning and fail-closed full
behavior, with local-only development clearly distinguished from independently
copied live audit. Export destination, measured sizing and the exact quota ledger
must be settled in the byte/budget review before integration, not inferred here.

## 6. Snapshot installation: important integration constraints

The pinned `raft_begin_load_snapshot` is not a live transactional install API.
In `build/_deps/willemt_raft-src/src/raft_server.c` it immediately resets term and
vote, removes remote membership, alters log/applied/commit coordinates, and rejects
a snapshot behind the current log end. There is no rollback-load API. Current
startup works around this by loading on a fresh server, restoring membership,
then restoring durable hard state (`src/v2_replication.cpp` constructor).

Recommendation: stage and validate a replacement Raft owner/generation, rather
than call that routine on the serving owner and hope later steps cannot fail.
Keep unchanged members, current hard state, compatible suffix and owned result
obligations explicit. Never run external delivery during restoration. Review the
temporary RAM/storage peak, including any second library server and state object;
128 KiB transfer scratch is not the entire installation footprint.

There are two distinct publication points:

1. Write/sync immutable staged files; they are not serving authority.
2. After complete preparation, quiesce and revalidate active generation, sender/
   leader term, hard state and relevant log/commit/applied coordinates. Atomically
   publish and sync the active-generation selector, then activate/ack the new
   generation. This durable selector is the point of no return: on later failure,
   recover/fail closed on the selected generation, never resume the old owner.

Higher-term traffic or a changed suffix during staging invalidates the prepared
transaction; stale completion is not permission to overwrite newer state. Exact
selector/chunk/ack bytes, nonwrapping identities and retry rules need review.

### Result obligations cannot be skipped with a snapshot

CP-023 reserves a result before accepting each result-bearing log obligation.
READY notifications already owed must remain in the existing ledger/FIFO across
install. RESERVED entries covered by the incoming snapshot need special care:
snapshot receipts may have expired, and applying the command to the final snapshot
does not recreate its original historical outcome.

Recommend archive-assisted contiguous catch-up for that covered prefix, using
existing command bytes, reservations and apply/publication semantics. Matching
entries reuse their slots. Conflicting uncommitted entries may release slots only
after validated durable truncation; committed conflicts refuse and preserve data.
Replay remains bounded and stops on consumer pressure. After those obligations
are settled, snapshot installation can skip an obligation-free missing prefix.
This may be slower than a pure snapshot leap but preserves the already approved
result contract without adding an economic-result schema. Missing verified history
must refuse rather than fabricate outcomes. No per-entry result can be emitted
twice merely because an install retry occurred.

Required tests include READY plus RESERVED plus conflicting suffix together,
snapshot index behind a compatible suffix, chunk replay/reorder, wrong lineage,
higher term during preparation, stale install ACK and cuts around every publication
point. A valid final state digest alone is insufficient evidence.

## 7. NUC launch critical path

**NUC shadow first is the nearest deployable milestone, not permission to trade.**

1. Close F5 and F6 library correctness, including the cross-cutting work above.
2. Build M2's startable V2 node/process shell and bounded authenticated transport.
   CMake currently has V2 tests/reports/benchmarks, not a V2 node binary.
3. Run an externally pinned candidate on NUC, then deploy credentials-free shadow
   services with operator authorization. A laboratory topology used for testing
   on NUC must not be represented as two-host quorum durability.
4. M3a: subsystem/machine health and stage-specific failure dashboards, exported
   from bounded counters/pages; formatting/scraping stays off the Raft owner.
5. Complete the deferred Pi/mixed-host gates before the approved two-host live
   pilot. Separately close venue/adapter/economic contracts, shadow/failure drills
   and the explicit live-canary admission checklist. No perpetuals or assumed swap
   capability is introduced to accelerate the platform work.

Do not promise a date or a percentage from the count of remaining rows. The next
measurable exits are permanent GREEN catch-up/archive/repair traces, durable-fence
proof, then a startable non-trading process on NUC. No OS reinstall is implied.

## 8. Approval boundary and exact next action

Choices A and B are approved. Finalize a coherent byte/budget sheet for portable
archive/install content, destination-local generation selection, bounded transfer
and repair admission. Reuse approved codecs where valid; do not overload an
existing kind or silently migrate the startup directory. That sheet must precede
encoding. The exact async-Raft fence remains a separate F6 review.

CP-027's first concrete component sheet is `F5_ARCHIVE_SEGMENT_BYTE_REVIEW.md`.
It allows review/implementation of immutable bytes independently of live deletion,
without pretending the harder publication/admission transactions are already set.

The request to finish faster authorizes parallel work, not weaker guarantees.
No archive/install/repair production fix is claimed by CP-026. This checkpoint
turns the remaining work into independently reproduced failures and a dependency-
ordered implementation plan rather than prematurely freezing an unsafe protocol.
