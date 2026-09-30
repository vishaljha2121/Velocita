# F5–F7 MVP closure: dependencies, parallel work and pending contracts

2026-09-22. Owner requested completing the remaining fixes in parallel where safe,
with at most two implementation workers and main architecture/integration review.
This plan does not waive byte/protocol approvals or the deferred physical Pi gate.

## 1. Delivery sequence

| Increment | Main / worker lane A | Independent worker lane B | Exit |
|---|---|---|---|
| CP-018 | History/peer/missing-hard-state REDs; exact bound-frame review | Existing-contract worker-error capture and owner refusal | Honest RED/coverage evidence, full host regression; byte proposal ready |
| CP-019 | Approved common framing and typed peer admission | Fixture migration and independent peer review | Host peer ingress bound; raw bypass removed; local history still open |
| CP-020 | Approved typed local codecs, initialization/recovery integration | Independent storage review and capacity/result-ownership draft | Local binding implemented; host Release/sanitized51/51; full F5 remains open |
| CP-021 | Receiver verified-prefix guard with RED/GREEN/mutations | Independent reachability challenge and native result-budget proposal | Host51/51 both; synthetic trace explicitly qualified; ledger quota awaits review |
| CP-022 | Approved standalone result ledger, integration REDs and mutation review | Basic numeric peer validation | Host52/52 both; kernel still unbounded; follower saturation §8 awaits approval |
| CP-023 | Approved §8 plus kernel result integration, startup and suffix ownership | Preflight component and adversarial edge tests | Original REDs now in CTest; result bound implemented, other node bounds open |
| CP-024 | Outbound/log diagnostic REDs and exact capacity/ownership review | Two read-only callback/storage reviews | Fresh Release54/54; production unchanged; four decisions await approval |
| CP-025 | Approved mailbox/log admission and kernel integration; independent mutations/review | Components and streaming WAL with permanent tests | Fresh Release/sanitized59/59; outbound/log ownership slice closed on host, full F5 open |
| CP-026 | Intact-lagger catch-up RED and transactional/result-ownership review | Independent archive-loss RED and offline-repair challenge | Fresh Release59/59; both behavioral REDs reproduced; completion plan awaits material contract review |
| CP-027 | A/B policy approval recorded; exact portable archive byte sheet | Actual bound-metadata persistence model and independent sync mutations | Model-port gap covered on host; archive bytes remain proposed, not encoded |
| CP-028 | Approved archive codec; independent golden/corruption/lifetime/mutation review | Incremental SHA and bounded pull-source codec | Standalone component only; no publication, coverage token or reclamation integration |
| Next F5 | Admitted replacement + transactional snapshot install | Archive publication and verification using stable portable lineage | Safe catch-up; reclaim requires snapshot + verified archive + no live references |
| F6 lane | Compose CP-027 bound-metadata model with install/async cuts | Reviewed asynchronous persistence fence design and implementation | No volatile success; owner does not wait on routine hard-state sync |
| Integrate | Cross-subsystem cuts/saturation/mutations; acceptance record | Gate/source-manifest preparation and independent review | Frozen candidate ready for operator target gates |
| F7 | Host/NUC, then physical Pi/mixed validation at the agreed final stage | Review raw externally pinned evidence | No false closure while Pi or other required evidence is absent |

Shared-source/build ownership must be explicit. Codec, install, archive and
asynchronous Raft callbacks cannot be independently improvised and merged later:
they share identity, generations, buffers and durability boundaries.

Current next-step detail: `F5_COMPLETION_AND_NUC_PLAN.md`. Its offline repair mode
and archive policy are now approved. The narrow exact codec contract in
`F5_ARCHIVE_SEGMENT_BYTE_REVIEW.md` is also approved and implemented at CP-028;
publication/coverage/quota and snapshot-generation contracts remain pending. NUC shadow deployment and
live two-host trading have separate exits; no remaining F5 requirement is waived.

## 2. What is required versus deferred

Required: lineage/local identity; absence-of-hard-state refusal; bounded byte AND
entry/work admission; result obligations; reserved kill/control capacity; safe
replacement voting admission; crash-consistent snapshot/suffix selection; verified
audit before last-copy deletion; durable success/failure visibility; final gates.
These are original guarantees, not optimization or new strategy features.

Defer: compression, zero-copy/kernel bypass, generic plugin storage backends,
multiple simultaneous snapshot transfers, automatic archive pruning/tiering,
automatic gateway failover, dynamic membership, perpetuals/netting, and peak-
throughput tuning not necessary to meet the declared MVP workload. Keep one active
snapshot transfer, one storage format, explicit retention/full behavior. Detailed
Grafana/health observability remains required in M3a before live, not optional polish.

Do not introduce live trading, credentials, adapters or deployment actions as an
implicit way to finish this library work. Operator hardware authorization and
venue admission remain distinct. F7 target validation cannot be marked complete
while the operator's Pi deferral remains in effect.

## 3. Approved ownership budgets and remaining review candidates

Owner update after CP-021: the1024-slot result budget (960 ordinary/64 protected)
and the local contract in F5_ADMISSION_RESULT_REVIEW §7 are approved. CP-023
integrates both kernel admission paths after separate §8 approval. CP-025 follows
owner approval of all four outbound/log decisions and implements that slice.
Other candidates below remain unapproved.

Exception after CP-018: the owner approved the peer 16-entry / 32-KiB frame bounds
and the local frame maxima in the byte sheet. CP-019 implements the peer limits.
Outbound/log received separate CP-024 approval and CP-025 implementation; archive,
install and owner-turn candidates below remain unapproved.

Independent direct compilation measured current canonical maxima: command 1,789
bytes, result 128, state snapshot 65,483; native CommittedResult 176 bytes on this
host. A previously built layout report printed an older snapshot bound; generated
reports are not authoritative unless rebuilt. Proposed wrapper arithmetic is in
`F5_BOUND_FRAME_BYTE_REVIEW.md` and was independently recomputed by main.

| Resource | Candidate ceiling / reserve | Rationale and qualification |
|---|---|---|
| Peer AppendEntries (APPROVED) | 16 entries, 32 KiB entire bound frame | Exact maximum 29,084 bytes; reject size/count before allocation. |
| Outbound (APPROVED/INTEGRATED CP-025) | Four owned same-kind mailboxes per peer;32768-byte AppendEntries + three160-byte small cells | Actual owner66680 bytes/node always reserves both remote sets; egress/scratch extra. Fair draining and persistent lower-term watermark. |
| Result slots (APPROVED and INTEGRATED CP-023) | 1,024 total, 64 reserved control | Native ledger229,400 bytes; excludes24-byte optional handle per retained log entry and all other ownership. Target acceptance still required. |
| Retained log (APPROVED/INTEGRATED CP-025) |8192 entries,8128 ordinary+64 control;2048-byte cells =16 MiB backing | Markers ordinary; descriptors327680+owner64+bounded Raft resize extra. Streaming recovery/rewrites implemented; no unsafe automatic reclamation. |
| Snapshot staging | One transfer; 128 KiB scratch; <=66 KiB complete object | Current proposed local bound file is 65,587 bytes; portable format still pending. Do not conflate disk generations with one RAM buffer. |
| Snapshot chunks | <=16 KiB, <=5 per maximum object | Proposal only; replay/reorder IDs and transfer admission pending. |
| Archive segment (APPROVED/COMPONENT CP-028) | 4 MiB complete object; 8,192 records | Pure codec caps, not durable publication, retention duration or deletion authority. |
| Unexported archive backlog | 16 MiB; stop ordinary work at 12 MiB (still PROPOSED) | Must charge backlog consistently with retained-log ownership, not double-count or omit it. Emergency room is finite, not perpetual liveness. |
| Local archive quota | 1 GiB; warn 768 MiB; stop ordinary at 896 MiB | A proposed canary budget, not a guessed retention duration or loss-tolerance claim. No automatic deletion; destination/independent-copy policy need owner review. |
| Owner turn | <=8 frames, <=32 entries, <=64 KiB peer work; <=32 apply/completions/output items | Candidate count/byte budgets, not latency evidence; priority and coupled sum still require measured design. |

These are starting points, not a license to encode numbers. The exact frame
maxima support the peer proposal; traffic rates, run duration, consumer gaps and
target latency have NOT validated the queue/archive quotas. Approve limits with
explicit saturation behavior; tune later only through measured contract updates.

Approved CP-024 decisions and CP-025 evidence: `F5_OUTBOUND_LOG_CAPACITY_REVIEW.md`.
No automatic history deletion to escape fullness. Protected capacity is finite
and does not guarantee quorum, emergency deadlines or ordinary fill admission.

### The result-queue issue must be solved before rings are substituted

Current apply publishes results on every non-restoring replica. A leader-only
capacity check can still overrun a follower. Throwing/dropping when apply reaches
a full ring loses an already-committed obligation and may block a later kill.

Required invariant: reserve result capacity for each accepted obligation BEFORE
it can commit, including follower admission; retransmission is idempotent and
uncommitted suffix replacement releases exactly its own reservations. A slow
subscriber eventually prevents ordinary admission, not committed-result overwrite.
The result publication/retention promise must be defined across elections.

Worker suggested explicit cluster credit advertisement while retaining all-replica
publication. Main has NOT adopted that as necessary: it adds a distributed credit
protocol whose stale grants and failover need proof. First evaluate whether bounded
local pre-admission reservations plus Raft's existing acknowledgement/backpressure
paths preserve the same promise without new credit messages. Ordinary capacity
checks must not suppress higher-term processing or fail an already-reserved
committed kill at apply. A new kill cannot skip an unaccepted log prefix: reserved
capacity is not an unconditional liveness guarantee. CP-021's
`F5_ADMISSION_RESULT_REVIEW.md` records this distinction and the remaining
admission/callback questions; §7 is the narrow result-budget owner review, not
approval of other quotas/protocols. Full uncommitted suffixes require planned
reservation transfer after durable truncation; spare-capacity-only admission can
otherwise deadlock replacement with no ready result available to drain.
Changing to leader/request-origin-only publication is also not a free optimization;
it changes result/election/retry semantics and needs a separate reviewed contract.

## 4. Approval and acceptance boundaries

Already approved: static startup bytes/token and all six history/peer semantics,
including per-record local binding and no flat-WAL fallback for fresh deployment.

Also approved after CP-018: `F5_BOUND_FRAME_BYTE_REVIEW.md` exact local/peer bytes,
bounds and metadata-first bootstrap publication/retry table. Do not ask again.
Result ownership and outbound/log quotas are separately approved and integrated
through CP-025; do not reopen them. Still to finalize/review: remaining owner-work/
archive/install quotas; portable install/archive frames, coverage and
generation transaction; replacement fencing/admission after hard-state loss;
concrete asynchronous Raft continuations/output fences. No callbacks may return
early success merely because a worker accepted a request.

Tests must prove effects, not error labels, and use independent expected state.
Capture original RED before edits; keep retrospective mutants labeled separately.
Build before CTest. Keep known-red standalone diagnostics outside passing CTest
until the relevant contract/fix exists, and never call their exclusion acceptance.
Final source root is held outside the candidate tree during target verification.
