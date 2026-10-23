# V2 remediation checkpoint audit

Updated: 2026-09-27. Scope: owner-approved NUC/Pi roadmap F0–F7.
This is progress evidence, not a deployment acceptance record. Historical RED
evidence remains in `phase6_9_audit_2026-09-21/`; do not rewrite past results when
a reproducer becomes GREEN. Preserve all pre-existing dirty paths.

## Current checkpoint

CP-059: three voters (owner decision 2026-09-27). RED-first node, mailbox and
real-process tests exposed a stall: one unreachable follower blocked all peer
traffic, so two of three voters committed nothing. Per-peer hold fix; five
mutants killed by their intended assertions (the first process-test version let
M0 survive inside Aeron's liveness window and was strengthened). Three-host
templates, quorum alerts, soak bots. Release 111/111; node suites ASan+UBSan
clean. Evidence `three_voter_2026-09-27/`. No compaction, so no installer needed yet.

Previous checkpoint:
CP-058: owner-approved secrets model: RAM-only (0600 tmpfs) secrets pushed from
the Mac Keychain, three-step SIGHUP overlap rotation. Node key sets, secret-set
loader, SIGHUP reload, client key-stdin and Mac tool; live two-process rotation
passes. Node/tool code preceded tests: coverage by 10 killed mutants (four tests
strengthened). Release 108/108. Evidence `m2_rotation_2026-09-24/`.

Previous checkpoint:

CP-057: M2 steps 5–6. Startable node, operator client and status tool over Aeron
UDP; two-process loopback exit test (redirect, fail-closed on voter loss and
partition, SIGKILL and full-restart recovery, clean SIGTERM) passes with an
independent reducer oracle. Release 107/107. Roadmap M2 exit met on one host;
not a NUC/Pi, network, power or soak result. Evidence `m2_node_process_2026-09-24/`.

Previous checkpoint:

CP-056: M2 step 4, V2Node owner loop over a fake transport. Found/fixed a busy-
frame drop that prevented worker-mode elections; parked-worker scenario and
frame conservation law cover it. Forgery, replay, impersonation, restart-fence,
partition and backpressure scenarios pass with independent reducer oracles.
5 mutants killed (two after test strengthening). Release/ASan+UBSan 104/104.
Evidence `m2_node_loop_2026-09-24/`. Next: step 5 Aeron transport + binaries.

Previous checkpoint:

CP-055: M2 decision-D ingress/response codec with exact layout documented in the
review; RED-first, Python goldens, 5 mutants killed; Release/ASan+UBSan 103/103.
Next: V2Node owner loop.

Previous checkpoint:

CP-054: M2 step 3, strict node config parser (decision E; identity/catalog
keys added as a documented clarification). RED-first; 10 mutants killed after
fixing one accidentally covered test. Release/ASan+UBSan 102/102 alone.
Evidence `m2_node_config_2026-09-24/`. Next: ingress/response body codec and
V2Node owner loop over a fake transport.

Previous checkpoint:

CP-053: owner approved M2 decisions C/D/E. Steps 1–2 of the M2 sequence landed
RED-first in new `velocita_v2_node`: authenticated envelope (RFC 4231 and Python
goldens, 6 mutants killed) and session fence with epoch file (Python goldens,
crash model over all 15 persistence cuts, 8 mutants killed incl. fsync
ordering). Release 101/101, ASan+UBSan 101/101 alone. Evidence
`m2_auth_envelope_2026-09-24/`, `m2_session_fence_2026-09-24/`. Next: config
parser, then V2Node owner loop. No kernel source change; no device/venue action.

Previous checkpoint:

CP-051: owner keeps one voter per host (NUC + Pi) for MVP and defers the online
snapshot installer to the three-voter release. Two-voter quorum intersection
means an intact voter never falls behind the compacted prefix; lost storage is
the offline lost-voter repair. New two-voter catch-up suite pins partitioned
and unsynced-acknowledgement power-loss traces with an independent reducer
oracle; it pins existing behavior, and the follower durable-ack clamp mutant
fails its exact intended assertion. Three-voter lagger RED freshly reproduced
and kept open for V2.1. Release 98/98; sanitizer in
`e1_two_voter_2026-09-24/verification.txt`. No production source change. Next:
M2 startable node. Pi deferred, not waived. Protected work untouched.

Previous checkpoint:

CP-050: documentation-only Claude handoff after CP-049. Verified branch/HEAD,
dirty workspace and saved evidence; no new runtime test/build or acceptance claim.
Top CURRENT_HANDOFF now carries approved decisions, exact real lagger RED, owned
generation/ledger/worker integration sequence, hazards, build paths and limits.
ADR0014 status now reflects native backup verification and the still-missing
online installer; ADR0006 supersedes stale synchronous/escaping-worker status
with CP-043/044/049 progress without erasing historical reasoning. No C++ edits,
staging/commit/tag/device/venue action. All existing dirty work remains intact.

Previous checkpoint:

CP-049: bounded serial writer target handoff for E1/E3. Main found draft
empty-pop/owner-publication race; worker must recheck submissions after acquiring
switch state and wait for old completion dequeue. Both borrowed targets remain
owned until exact completion and old arena accounting. No online installer or
ledger replacement; no E exit closed. Host Release97/97 and C++ ASan+UBSan97/97;
final strengthened focused rebuild/rerun details in
`f5_writer_handoff_2026-09-24/verification.txt`. Inventory97host/94V2,
gate-manifest16 PASS. Two independently forced guard mutants fail. Focused
TSan instruments test+actual WAL source, not support libraries. No target gates.
Initial implementation preceded new tests; later RED/mutations are labelled as
such, not a test-first claim. Next is actual stable-facade/generation integration,
not further disconnected acceptance counts. Protected work untouched.

Previous checkpoint:

CP-048: owner-approved fresh destination verification, persistent receipts
deferred. Native E2 compaction and reclaimed-history startup now require exact
move-only independent evidence. No new wire/durable format; private trusted
transport mint boundary. Actual WAL removal/restart and backup payload1..3,
missing/corrupt backup gating, file/directory fsync EIO and stale/duplicate checks.
Draft counter-reset replay RED fixed with fresh cold nonce+attempt challenge;
entropy EIO refuses unchanged. Three intended guard mutants fail. Inventory
95host/92V2; rebuilt Release95/95 and C++ ASan+UBSan95/95 (Raft C uninstrumented,
leaks off), gate-manifest16 PASS. Results/caveats in `f5_destination_verification_2026-09-24/`.
Same-host directories/simulated authentication are not independent hardware or
transport acceptance. E1/E3 online activation still next; original lagger RED
remains compacted12/applied0. All broad exits open; no new feature scope or device
actions. Protected paths preserved; main review plus two bounded Sol lanes.

Previous checkpoint:

CP-047: documentation/read-only parallel integration review; no new runtime
evidence. Owner locked work to E1/E2/E3 then startable node, nonessential findings
deferred. Two Sol lanes inspected E2 evidence and E1/E3 lifetime boundaries.
Main confirmed facade/core is already approved; actual old-worker drain and same
ledger are mandatory. Identified existing CP-038 independent-destination approval
boundary: live compaction intentionally refuses local-only evidence. Main proposes
fresh authenticated destination verification instead of new persistent receipt
bytes for MVP, with explicit restart/reclamation availability cost; OWNER REVIEW
pending in `../F5_ARCHIVE_PUBLICATION_REVIEW.md`. No live-mode gate was removed.
Host native exits do not replace later authenticated process/target acceptance.
CP-046 remains latest runtime evidence; E1/E2/E3/F5/F6/F7 stay open.

Previous checkpoint:

CP-046: owner-approved history bridge/candidate budget/recovery approach. Selected
generation's snapshot-covered WAL debt now archives on the serial worker while
the voter stays quarantined, without raising the live8192-entry bound. Whole-WAL
cap up to1GiB; exact full-plan archive preflight plus pending-suffix reservation;
acquired completion identity checks; no WAL deletion. Permanent8193-record,
overlap, paused/publication, worker failure/restart, quota and completion witnesses.
Three direct mutants fail intended assertions; draft overlap-accounting bug fixed.
Host Release93/93 and C++ ASan+UBSan93/93 (Raft C uninstrumented, leaks off),
inventory93host/90V2 and gate-manifest16 PASS; final checks and limitations in
`f5_history_recovery_2026-09-24/README.md`. Rebuilt actual lagger remains RED
compacted12/applied0/seven failures: online candidate/selection/ledger integration
still next. E1/E2/E3 and F5/F6/F7 remain OPEN. No target acceptance. Corrected the
linear-time claim: archive verification may reread growing prefixes per segment.
An initial parallel verification run collided in fixed-name test storage; final
Release and sanitizer must run sequentially. Protected paths preserved.

Previous checkpoint:

CP-045: E1 integration review, production unchanged. Fresh real lagger RED remains
compacted12/applied0/seven failed replies. Identified archive/selector two-head
publication mismatch; proposed a generation-local existing-WAL history bridge,
separate1-GiB lab candidate budget and accounted nonserving catch-up recovery.
Owner approval pending: `../F5_INSTALL_HISTORY_BRIDGE_REVIEW.md`. Three permanent
cold-layout contract witnesses and two intended guard mutations; Release92/92 and
C++ ASan+UBSan92/92. Inventory/evidence in `f5_install_bridge_review_2026-09-24/`.
No online installation/transport/acceptance claim. Exact same ledger, no stale
writes, archive provenance and independent-copy requirements remain mandatory.

Previous checkpoint:

CP-044: current-generation async metadata/WAL crash-image proof, test-only.
75 reached cuts over three name schedules, three committed-seed recoveries,
six nonvacuous baselines. Exact pre-recovery tuple/payload promises; typed temp
refusal separated from active metadata; clean images must recover. Two direct
mutants fail intended assertions. Release91/91 and C++ ASan+UBSan91/91; raw evidence
in `f6_async_crash_model_2026-09-24/`. Inventory91host/88V2, tools24+16.
No production/byte changes. Next is actual E1 transfer/install with stale-write
activation fencing, not another persistence helper. E2 independent copy and
target acceptance remain open, as do E1/E2/E3 and F5/F6/F7.

Previous checkpoint:

CP-043: owner-approved CP-042 fence implemented for actual hard-state and
whole-suffix persistence. One serial worker job, desired/durable separation,
owned bounded retry, optional library-selected whole-cut hook, exact completion
validation and unused-grant failure. No canonical-byte change. Actual metadata
thread/pause/EIO,16 suffix permutations+2 failures,9 completion cases, real
authority-marker checkpoint, and five intended guard mutations. Four suites
added; inventory90host/87V2. Verification/caveats and intermediate failed fixtures:
`f6_async_persistence_2026-09-23/README.md`. Host implementation is not target
acceptance; composed async crash coverage/E1 activation, E2 independent copy,
target durability/memory/timing still open. F5/F6/F7 not closed.

Previous checkpoint:

CP-042: documentation-only exact asynchronous continuation review, awaiting
owner approval of five choices in `../F6_ASYNC_DURABILITY_FENCE_REVIEW.md` §8.
Two read-only bounded Sol audits plus main call-site inspection; no new test or
production behavior claim. One paused turn/serial job, mailbox withholding,
before-apply pending status, library-selected whole-suffix preparation, one owned
32-KiB retry frame and cold-only clear scope. No new canonical bytes. Preserve
CP-041 REDs; implement hard state then suffix through permanent RED/GREEN and
mutation checks after approval. Broad exits remain OPEN; no target acceptance.

Previous checkpoint:

CP-041: completed the bounded COMPOSED archive/snapshot/WAL host-model step for
E2. Actual kernel/worker/recovery,243 cuts across first/replacement publication
and three independent name schedules. Before recovery, any reclaimed prefix
requires the new snapshot and full archive, with exact suffix preserved.
Accepted recovery checks full tuple/payload history. Three direct actual-source
mutants fail at intended ordering/data assertions; production unchanged.

Two bounded Sol workstreams plus main correction/review. Corrected draft
harness flaws rather than calling them product defects; evidence retains the
distinction. CP-040 mixed cache-line mutation builds were rerun with64 to match
their libraries; results reproduce. Rebuilt Release86/86, C++ ASan+UBSan86/86,
focused243 cuts, tools40/40, inventory86host/83V2. UB halt/nonrecovering, leaks
off, Raft C uninstrumented. Evidence: `f5_composed_persistence_2026-09-23/README.md`.

Next E3 failure freshly reproduced in existing hard-state diagnostic: election
and vote4/4, commit2/2 actual metadata fsyncs still execute on protocol owner.
No premature-output defect claimed. Adapt actual persistence callbacks and
suffix truncation under reviewed continuation/quarantine, then E1 actual lagger.
Local composed-model step complete, but broad E2 independent-copy/target gates
and E1/E2/E3/F5/F6/F7 stay OPEN. Protected paths untouched; no stage/commit or
device/venue action.

Previous checkpoint:

CP-040: strengthened E2 local fault evidence, no production/byte change. Three
permanent suites cover139 reached archive EIO sites/three successful retries,
124 archive-only durable-image cuts and nine real integrated snapshot/WAL errors
with a durable uncommitted suffix. Recovery checks actual command bytes/tuples
and preserves fault evidence. Two sync-class omission mutants die at the intended
durable-restart assertion; not a claim of individually mutating every guard.

Corrected worker harness flaws instead of misclassifying them as product REDs:
ordinal/retry/oracle mistakes and a fixed-yield timeout. Removed global libc
interposition from the final integrated harness; instrumented test-only production
objects are built by CMake. Rebuilt Release85/85, C++ ASan+UBSan85/85, focused3/3,
tools40/40, exact inventory85host/82V2; UB halt/nonrecovering, leaks off, Raft C
uninstrumented. Raw scope/results: `f5_archive_faults_2026-09-23/README.md`.

The complete archive/snapshot/WAL durable-image sweep remains NEXT for E2;
archive-only model plus integrated EIO is not composed power-loss proof. E3
hard-state/truncation and E1 actual installation still outstanding. Broad
E1/E2/E3/F5/F6/F7 OPEN. No target/acceptance, new contracts, protected-path changes
or stage/commit. A worker's unauthorized nested delegation was stopped/disclosed;
main completed final verification.

Previous checkpoint:

CP-039: approved CP-038 archive is wired into actual laboratory compaction and
restart. Exact command bytes survive removal from retained log/WAL in fully
verified catalog-named objects. The same WAL worker performs archive and snapshot
publication; owner revalidates/consumes coverage and polls Raft; WAL rewrite runs
last. Pending fence withholds authority/results without owner I/O/join fallback.
Local-only evidence still refuses pilot/live reclamation. Broad E1/E2/E3 OPEN.

Permanent REDs cover capacity-only cut deferral, corrupted cached coverage and
validly encoded inconsistent recovery artifacts. Full sanitizer found real
worker-stack overflow; fixed bounded codec workspaces moved to heap without
changing bytes/read limit. Full-catalog refusal distinguishes capacity from stale
generation. Gate inventory includes six new integrated/component suites.
Rebuilt Release82/82, C++ ASan+UBSan82/82 (nonrecovering UB, leaks off, Raft C
uninstrumented), focused7/7, tools40/40, inventory82 host/79 V2. Focused WAL-worker
TSan passed; not whole-replica coverage. Three intended direct-source mutant
kills (two consumer bypasses, one allocation regression). Exact commands and
failed-run distinctions: `f5_archive_integration_2026-09-23/README.md`.
Two bounded workers; main integration/review; no protected-path, commit or target
action. Next: systematic persistence fault sweep (E2), remaining actual async
hard-state/truncation (E3), actual lagger installation (E1).

Previous checkpoint:

CP-038: owner APPROVED all five archive catalog/publication/quota decisions;
implementation in progress. Enables E2 actual evidence-preserving compaction and E1 replay; E3 fence
is a mandatory integration dependency, not a background-thread label. One flat
catalog96/112 bytes, max458880; no chain/pages/extra selector; per-row source
generation; fixed existing dependencies; immutable segments versus explicitly
replaceable catalog metadata. Lab1GiB/4096objects and completion/backlog accounting
proposed with underfilled-cut reaccounting and no retention-duration claim.

Two Sol read-only reviews plus main final reasoning/arithmetic check. Destination
attestation/transport is deliberately not invented; local catalog grants no live
independent-copy authority. No runtime changes or new test results; CP-037 remains
the last verified checkpoint. All E1/E2/E3 exits OPEN. Exact proposal/reasons:
`../F5_ARCHIVE_CATALOG_BYTE_REVIEW.md`. No helper-only milestone authorized.

Previous checkpoint:

CP-037: owner approved CP-036's six install decisions and imposed three outcome
exits: E1 actual safe catch-up, E2 actual evidence-preserving compaction, E3 actual
nonblocking persistence/no early success. Every extra task must enable one or a
named separate launch requirement; otherwise defer. All three remain OPEN.

Added two E1 contract witnesses, not an installer: pinned-Raft overclaim policy
really commits/applies unverified suffix; correct boundary plus actual suffix
replacement converges and stale ACK does not regress. Valid cold disk recovery
applies committed suffix without notifications, while later uncommitted entry
publishes exact result once. Main corrected both fixtures, independently ran
the tests and two intended direct recovery-source mutations. Healthy public
commit>applied install reachability was NOT established; documented correction.

Release76/76, C++ sanitizer76/76 (UB halt/nonrecovering, leaks off, Raft C not
instrumented), focused2/2, tools40/40, independent inventory76 host/73 V2. Original
lagger/archive-loss REDs remain reproducible. No production change or acceptance
claim; complete raw evidence/gates: `f5_install_contract_2026-09-23/README.md`.
Next: E2 integrated archive publication/compaction after required concrete byte
review, shared F6 fence for E3, then complete E1 using those dependencies.
Protected paths untouched; no stage/commit/deployment. Pi deferred, not waived.

Previous checkpoint:

CP-036: documentation-only integrated install contract review, PROPOSED pending
owner approval. Two Sol read-only reviews, main reconciliation. ACK only S, not
unverified retained end; capture committed-suffix outcomes before replacing owner.
These are draft counterexamples, not executed REDs. Same-ledger/no-extra-receipt,
canonical transfer/wire deferral, async fence and finite pinned replay proposals
with costs: `../F5_INTEGRATED_INSTALL_CONTRACT_REVIEW.md`.
No code/build/test/byte change. Last verified tests CP-035; original integration
REDs and F5/F6/F7 remain open. Protected work unchanged. Next owner review then
permanent REDs and integrated implementation, with archive byte review still due.

Previous checkpoint:

CP-035: two Sol workers; main interface review, oracle corrections, extra tests/
validator fix, direct mutations and integration. Native-only suffix classifier
and value-only result-obligation visitor; no transfer/install publication or new
bytes. Full unknown suffix preserved only with exact boundary evidence; same
ledger/FIFO/capacity retained. Old snapshot is committed despite commit checkpoint
lag. Corrected wrong beyond-tail test oracle and superseded draft base/selector.

Release74/74; C++ sanitizer74/74 then rebuilt final focused checks after test-only
additions; tools40/40, inventory74 host/71 V2. Five intended direct guard mutants;
component REDs and main occupied-pool RED retained. Raw main original lagger RED
still fails at compacted12/lagger0 after seven retries. Evidence:
`f5_install_preflight_2026-09-23/README.md`. No target/model/TSan/latency claim.
Next is remaining material install/transfer/coverage/budget review then integrated
install, not more standalone helpers counted as closure. F5/F6/F7 and original
integration REDs stay open. Protected work untouched; no stage/commit/deployment.

Previous checkpoint:

CP-034: approved initial genesis publication and final-selector cold recovery
implemented. Root/generations/runtime ownership routes kernel stores; no raw path
startup or second root metadata authority. Durable dependency/seal/selector order,
snapshot floor, exact stale-token refusal and diagnostic-only temp inspection.
Legacy/nonempty partial bootstrap refuses unchanged; old mixed-root acceptance
removed deliberately. Full history validation still belongs to kernel admission.

Behavioral routing/stub diagnostic REDs retained, 26-cut/26-EIO actual-source
persistence model plus short/EINTR writes/four checked closes, four intended guard
mutants. Two bounded workers, main review/integration. Release 72/72; full C++
ASan+UBSan 72/72 (existing nonrecovering UB/leaks-off/uninstrumented Raft-C scope);
tooling 40/40; independent inventory 72 host / 69 V2. Evidence:
`f5_generation_startup_2026-09-23/README.md`. No target/model/TSan/latency claim.
Next actual snapshot installation must preserve unknown suffix/result obligations;
archive/repair/async/final gates and original integration REDs stay open. F5/F6/F7
NOT CLOSED. Protected paths preserved; no stage/commit/tag/device/venue action.

Previous checkpoint:

CP-033: owner approved the complete minimal generation-storage review. Implemented
exact 40-byte base seal/canonical snapshot+catalog validator and a read-only on-disk
final-selector/base verifier. No activation, publication, migration, mutable-tuple
validation or startup-authority capability; CP-032 guard remains active.
Component REDs retained; independent literal golden checked twice; main full-byte,
corruption/rehashed-header/filesystem tests and six intended guard mutations.
Release 70/70; C++ sanitizer 70/70 then rebuilt final focused 2/2 after test-only
additions; tooling 40/40, inventory 70 host / 67 V2. No target/model/TSan/power-cut
claim. Evidence: `f5_generation_base_2026-09-23/README.md`. Next already-approved
work is actual fresh bootstrap/selected recovery/publication integration, not
another byte review. Original integration REDs and F5/F6/F7 remain open.
Protected paths preserved; no stage/commit/tag/device/venue action.

Previous checkpoint:

CP-032: TDD guard prevents old-layout recovery/token revalidation from ignoring
generation artifacts. Pre-guard production RED retained; 3 names x 3 types,
unchanged evidence/zero checkpoints, existing-token refusal, three intended
direct guard mutants. Main corrected opaque-WAL fixture scope; independent review
confirmed ordering and documented bootstrap's unchanged generic refusal.
Fresh Release 68/68, startup 3/3, C++ sanitizer 68/68, tooling 40/40; independent
inventory 68 host / 65 V2. No new target/model/TSan/latency claim. Evidence:
`f5_generation_guard_2026-09-23/README.md`. No new persistent bytes encoded.
`../F5_GENERATION_STORAGE_REVIEW.md` consolidates the proposed minimal 40-byte
base seal, fixed layout/root ownership, snapshot floor and migration deferral;
OWNER APPROVAL REQUIRED before implementation. Existing CP-031 approval stands.
Original integration REDs and F5/F6/F7 stay open. Protected paths preserved;
no staging/commit/tag/device/venue action. Two bounded workers, main integration.

Previous checkpoint:

CP-031: owner approved exact160-byte local generation selector. Implemented pure
codec/value transition only; native56-byte value never directly serialized.
Worker component-stub RED and independent literal-golden RED retained, thenGREEN;
546 transition cells, truncation/corruption/resealed shape checks, output atomicity,
overlap, terminal counters, allocation checks and three main guard mutants pass.
Two bounded workers (implementation/read-only review), main integration/oracles.
Fresh Release67/67, C++ sanitizer67/67, tooling40/40, inventory67 host/64 V2.
Existing sanitizer scope; no target/model/TSan/latency claim. Evidence:
`f5_local_generation_2026-09-23/README.md`. Prepared-base/root-layout/migration and
publication integration still pending; no original integration RED repaired here.
Owner explicitly informed that contract approval does NOT close all F items.
Protected paths preserved; no staging, commit/tag, source freeze or deployment.

Previous checkpoint:

CP-030: four CP-029 directions owner-approved; exact new byte/budget/protocol
choices remain pending. Two bounded workers, main integration/review. Added
read-only archive file verifier using unchanged CP-028 codec and two permanent
test suites. Existing-byte component only: no publication, coverage, deletion,
install, repair or async callback behavior changed. Stub RED, smallest GREEN,
main injected I/O tests and three direct guard mutants retained. Main corrected
wrong-guard fixture, stat error classification and fd/path/aliasing proof limits.

Fresh Release65/65; full C++ sanitizer65/65, then rebuilt focused final2/2 after
test diagnostic wording; tooling40/40, independent inventory65 host/62 V2.
UBSan nonrecovering/halt, leaks off, Raft C uninstrumented. No target/model/TSan/
latency claim. Evidence: `f5_archive_file_2026-09-23/README.md`. Exact 160-byte
selector PROPOSED in `../F5_LOCAL_GENERATION_BYTE_REVIEW.md`; owner asked, not yet
approved. Original integration REDs remain open; F5/F6/F7 not closed. Protected
paths preserved; no staging/commit/tag or deployment.

Previous checkpoint:

CP-029: four concurrent lanes (main plus three workers), task-only exception to
normal two-worker limit. New publication/install/repair/async reviews PROPOSED:
`../F5_F6_FOUR_LANE_CLOSURE_REVIEW.md`. No production or CMake changes. Main
corrected unsafe draft suffix splicing, archive provenance, stale-generation
ownership and unnecessary durable-ledger/selector expansion before encoding.
Repair barrier capacity at saturation remains an explicit material decision.

Three actual-syscall owner-I/O REDs retained outside CTest; strict-built test shows
campaign/vote/commit4/4/2 metadata syncs on owner despite running WAL worker.
Lost-voter two-history characterization independently reproduced by main, not a
repair GREEN. Fresh build/Release63/63,11.16s; no fresh sanitizer/model/tool/target
claim. CP-026 integration REDs remain open. Protected paths preserved; no staging,
commit/tag or device action. Evidence: `f5_f6_parallel_contracts_2026-09-23/README.md`.

Previous checkpoint:

CP-028: exact portable archive bytes/caps approved and implemented as a standalone
codec. Incremental SHA preserves one-shot bytes; bounded pull-source encoding and
whole-input validation before visitors. Historical failed requests preserved;
no current-state filtering or gateway/cluster-incarnation confusion. Main supplied
independent full-byte golden/corruption/failure tests and seven intended archive
guard mutations; two SHA mutations also reproduced. Exact 4-MiB and 8,192-record
boundaries pass. API requires immutable contiguous caller buffers/nonaliasing;
no file-streaming, publication or compaction integration. Compile-only RED and
excluded pre-stable mutation provenance are documented, not upgraded to bug REDs.
Fresh Release63/63, C++ ASan+UBSan63/63, tooling40/40; inventory63 host/60 V2.
UBSan nonrecovering/halt, leaks off, Raft C uninstrumented; model not rerun.
Both original CP-026 integration failures freshly reproduced; F5/F6/F7 still OPEN.
Evidence: `f5_archive_codec_2026-09-23/README.md`. No target/live claim, staging,
commit or protected-path modification. Next: reviewed durable archive publication/
dependency coverage and reclamation, snapshot install/catch-up and fenced repair.

Previous checkpoint:

CP-027: owner approved CP-026 A/B policy. Actual BoundMetadataStore syscall model
is now integrated, with independently authored tuples, actual load cross-check,
cut/failure/short-write sweeps and main's two intended source-copy sync mutations.
No production format/order change; retrospective mutations, not an original bug
RED. Release60/60, C++ ASan+UBSan60/60; inventory60 host/57 V2; tooling40/40.
UBSan nonrecovering/halt, leaks off, Raft C uninstrumented; no target claim.
Evidence: `f6_bound_metadata_model_2026-09-23/README.md`.
Exact archive96/32-byte component layout and4-MiB/8192 caps are proposed in
`../F5_ARCHIVE_SEGMENT_BYTE_REVIEW.md`, NOT encoded/approved. Main corrected draft
filtering so failed authority attempts remain audit history. Catch-up/archive-loss
REDs and F5/F6/F7 remain open; no target/live acceptance or semantic guarantee cut.

Previous checkpoint:

CP-026: parallel completion plan plus two independently reproduced behavioral
REDs, not production fixes. Intact voter remains at0 after peers compact through12;
archive diagnostic loses exact raw command bytes despite a valid snapshot. Main
review added safe fixture roots and explicit semantic expectations; archive oracle
is explicitly negative-only until typed coverage verification exists. Fresh build
and Release59/59; no new sanitizer/target/performance claim. Evidence:
`f5_completion_review_2026-09-23/README.md`. Plan/approval boundary:
`../F5_COMPLETION_AND_NUC_PLAN.md`. Offline operator-fenced replacement and archive
policy are recommendations, not approved bytes. Snapshot integration must preserve
READY/RESERVED result obligations and respect the durable generation-selector
point of no return. No production behavior or acceptance scope changed; F5/F6/F7
remain open, NUC shadow distinct from live, Pi deferred not waived.

Previous checkpoint:

CP-025: all four CP-024 choices approved and implemented. Bounded coalescing
canonical-frame egress replaces deque/dynamic public take; fixed8192-cell log
replaces map/vector ownership.8128 ordinary/64 protected actual tokens; markers
ordinary; local/follower/startup admission and durable suffix release integrated.
Recovery/rewrite streams bounded blocks; no automatic last-copy deletion.
Three original behavioral growth REDs GREEN; four main/five worker direct mutants
fail intended assertions. Alias bug, error-cause reporting, fd ownership and native
mailbox accounting corrected during main review. Worker compile-only/unverified-
archive RED caveats preserved, not counted as original behavioral TDD evidence.
Fresh Release59/59, C++ ASan+UBSan59/59, tooling40/40; inventory59/56. Raft C not
instrumented; leaks off, UB nonrecovering/halt. No target/TSan/performance claim.
Evidence: `f5_capacity_integration_2026-09-23/README.md`. Full F5/F6/F7 still OPEN:
replacement/install/catch-up/archive, async fences and frozen target gates remain.

Previous checkpoint:

CP-024: outbound/log REVIEW ONLY, owner approval pending. Three permanent
standalone REDs reproduce reply accumulation, copied retry payload growth and
retained-log growth despite consumed results. No production behavior changed.
New proposal replaces old unapproved FIFO/byte candidates with four per-peer
coalescing packet mailboxes and8192 fixed log cells/8128+64 admission tokens.
Review includes measured prototype layout, actual Raft resize costs, bounded
recovery/rewrite requirement and explicit availability limits. Main recommends
no new Raft allocation fork for MVP. See ../F5_OUTBOUND_LOG_CAPACITY_REVIEW.md.
Fresh build/Release54/54; prior sanitizer/tooling evidence remains CP-023. REDs
are not registered GREEN tests; evidence: f5_capacity_review_2026-09-23/README.md.
F5/F6/F7 remain open, Pi deferred not waived. No freeze, commit or target action.

Previous checkpoint:

CP-023: owner-approved §8 fallback and §7 result ledger integrated across local/
follower admission, durable suffix replacement, pre-apply/publication and startup.
The four original REDs now run in CTest. New edge tests plus five intended mutants
verify ownership and failure boundaries; no post-apply result queue allocation.
Exact evidence/verification: `f5_result_integration_2026-09-23/README.md`.
Fresh Release54/54, ASan+UBSan54/54, tools40/40; model not rerun; inventory54/51.
Outbound/log quotas remain unapproved; full F5/F6/F7 OPEN. No target acceptance.

Previous checkpoint:

CP-022: approved standalone result ledger host GREEN, NOT kernel-integrated.
Generation/pool/ownership tests plus independent mutations; basic numeric peer
admission hardened. Fresh Release/sanitized52/52, tools40/40; model not rerun.
Four integration REDs remain outside CTest. §8 follower control-only saturation
protocol awaits separate approval;1024/64 budget already approved. Evidence and
the stale-library worker-verification correction:
`f5_result_ledger_2026-09-23/README.md`. Full F5/F6/F7 OPEN.

Previous checkpoint:

CP-021: V2 receive commit bounded to the message-verified prefix; signed prefix
arithmetic checked. Synthetic ingress REDs, repair/restart and lower/failed-prefix
cases, plus three intended mutants. Honest current-sender divergent-tail
reachability was NOT established; see explicit correction in
`f5_verified_prefix_2026-09-23/README.md`. Fresh Release/sanitized51/51; tools/model
not rerun. Result-ledger quota/layout/availability proposal awaits owner review in
F5_ADMISSION_RESULT_REVIEW §7; no bounded-container claim. Full F5/F6/F7 OPEN.
Owner subsequently approved that narrow result contract and1024/64 budget,
requesting a C++ explanation before implementation. This approval-only update
changes no code and adds no test result.

Previous checkpoint:

CP-020: approved local-history identity binding and metadata-first genesis host
GREEN. Release/sanitized51/51, tools40/40, model109/109. Missing/foreign history,
complete-block corruption, cross-artifact term and snapshot-covered ordering
refusals have permanent tests and retained REDs. F5 whole-node bounds, replacement/
install/archive and F6/F7 stay OPEN. New BoundMetadataStore needs its own port of
the old syscall ordering model; do not inherit that model's PASS by filename.
See `f5_local_history_2026-09-22/README.md` and CURRENT_HANDOFF's top section.

Earlier checkpoint summaries below are historical, not the current state:

CP-015: exact identity bytes approved; codec/standalone startup owner host GREEN.
Kernel still bypasses token via old raw constructor; A04 retained RED. Host46/46
Release/sanitized, tools40/40, model109/109. F5/F6 remain OPEN.
CP-014: ADR0014 direction approved; exact F5 identity bytes proposed, not encoded.
Actual metadata-save ordering model and two sync-only semantic mutants retained;
host44/44 Release/sanitized, tools40/40, model109/109. Full F5/F6 stay OPEN.
CP-013: F5 bounded snapshot load and F6 synchronous error-latch/metadata publication
fixes host GREEN; F5 recovery/identity/archive REDs retained. The owner subsequently
approved F5_F6_RECOVERY_DURABILITY_REVIEW / ADR0014 direction; exact new bytes,
budgets and async fences remain pending review. Both full items stay OPEN.
CP-012: F4 host-library proof issuance, one-shot gateway consumption and final
transport composition GREEN; deployed authority/timing/fencing/target gates OPEN.
CP-011: approved marker bytes/drain prerequisite, retained as historical evidence.
CP-010: historical byte proposal, superseded by approval/implementation in CP-011.
CP-009: reproduced partition/delayed-reply REDs retained; approval state superseded.
CP-008: immutable installation, mandatory row binding, schema 8 and artifact
readiness integrated; full F3 remains OPEN for real encoder/venue admission and targets.
CP-007: approved catalog-v1 codec/context prerequisite, retained as historical evidence.
CP-006: F6 process-crash coverage and F7 gate-tooling hardening; both full items
remain OPEN. Catalog row approval and CP-007 supersede the earlier pending review.
CP-005: F3 recovery compatibility preflight host GREEN; ADR 0012 records the
approved installation/recovery contract and fresh-lineage schema-8 boundary.
CP-003's immediate refusal increment remains host GREEN; full F3 remains OPEN.
CP-001/F1 and CP-002/F2 remain host GREEN. No target acceptance is implied.
Base HEAD d44a4a964fc8c13279878e53d245ab56b921a824 on
`v2/phase-5-corrections`; fixes and earlier audit work are UNCOMMITTED.
No hardware/venue run, credentials, trading or tag changes. No acceptance seal
was regenerated; the expanded manifest still needs frozen-candidate target proof.

| Step | Findings | Status / exit evidence |
|---|---|---|
| F0 | Baseline audit | Preserved; independent RED probes and roadmap present |
| F1 | A02, A06 | Host GREEN; mutation details below; target validation pending |
| F2 | A05, A10, A11 | Host GREEN; CP-002 below; target validation pending |
| F3 | A03 | Root/row binding, schema 8, recovery/readiness and bound gateway integrated; real encoder/venue admission and target gates pending |
| F4 | A01 | CP-012 host composition GREEN; deployment IPC/domain/clock/fencing and target proof remain open |
| F5 | A04, A07, A08, A14 | Bound history/genesis/prefix guards and CP-023 result ownership integrated; whole-node bounds, admitted catch-up and archive open |
| F6 | A09 | Failure latches host GREEN; CP-014 model covers the old generic store only; port to bound metadata, broader power-loss and async fences open |
| F7 | A12 | Gate-tooling hardening in CP-006; re-freeze and target gates open |

A13 (startable process and live integration) remains in downstream roadmap
milestones. A host test pass does not close the corresponding target guarantee.

## CP-001 — F1, emergency scheduling and bootstrap authority

### A02: planner discarded a resource the final guard understood

Permanent regression: `test_planner_preserves_emergency_cancel_capacity` in
`tests/v2_gateway_tests.cpp`. Before the fix, rebuilding and executing
`v2-gateway-tests` exited 1 with:

```text
V2 gateway test failed: planner lost emergency cancel capacity after the normal lane exhausted
```

The test uses a reducer-authorized cancel, quota `{0,1}` and an expired lease.
It asserts the planned identity and actual test-transport canonical bytes, not
only a permitted enum. Negative controls cover `{0,0}` and a submit with only
emergency quota. Mixed `{1,1}` schedules submit then cancel; `{1,0}` admits only
the submit, pinning normal-budget decrement and authorization ordering.

Smallest fix: replace the planner's single normal counter with a local copy of
both quota counts. Continue scanning while either is nonzero, pass both to the
guard, spend normal first, otherwise emergency. Guard permission guarantees the
latter case is a cancel. Keeping separate counters avoids sum overflow; checking
before decrement avoids underflow. No heap ownership, reference lifetime, public
layout, payload or replicated-state change. Existing bounded scan complexity is
unchanged; the counter state is two uint32 values, constant space.

Mutation check: directly compiled the permanent gateway suite against the HEAD
version of `src/v2_gateway.cpp`, not the fixed CMake object. It exited 1 at the
same emergency-planner assertion above. This proves reverting the fix is caught;
it does not claim exhaustive mutation coverage of every scheduler branch.

### A06: authorization accepted bytes the next boundary could not represent

Permanent reducer test:
`test_authorization_requires_initialized_execution_authority`. Initial RED exited
1 with `zero execution authority authorized an outbound effect`. Expanded cases
cover `(epoch,generation)` = `(0,0)`, `(0,3)`, `(2,0)` and assert refusal plus
unchanged reservation/order/outbox counts and reserved quantity/margin/fee.

`test_zero_authority_bootstrap_cannot_authorize_an_effect` adds the public
replicated-command trace: configuration, policy activation and actionable
evaluation succeed, but authorization refuses before creating economics. The
positive replicated fixture now advances both identities through committed
commands and asserts full effect materialization. It no longer silently relies
on the invalid bootstrap state.

Smallest fix: reuse `stale_control_generation` if either control identity is zero
before creating a reservation/order/effect. Canonical OrderEffect validation
already requires nonzero identities; weakening it would hide the mismatch.
Receipt/state-version bookkeeping on a refusal is intentional, so this does not
claim byte-identical whole state. Earlier safe sweep/dedup ordering is preserved.
Two scalar comparisons add constant work, no allocation and no layout changes.

A06 mutation verification: directly compiled the current risk suite with two
temporary kernel sources, one removing the guard and one replacing OR with AND.
Both exited 1 at `V2 risk/order/outbox test failed: incomplete execution authority
authorized an outbound effect`. The latter proves that checking only the
both-zero case would be insufficient. Production source was not mutated.
Executables: `/private/tmp/v2-risk-guard-removed` and
`/private/tmp/v2-risk-guard-and`; source copies have corresponding
`v2_kernel_guard_removed.cpp` and `v2_kernel_guard_and.cpp` names.

### Reproduction / verification

Both isolated builds use source `/Users/vishaljha/aeron-raft-pi`, cache-line size
64, and existing pinned dependency source directories from `build/_deps`.

```sh
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j2
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
```

Observed Release **36/36**, 6.30 s; Debug ASan+UBSan **36/36**, 42.09 s.
Leak detection explicitly disabled; UBSan runtime halts on first violation.
No new TSan or target measurement. Python tooling/model were not changed in F1;
their earlier audit results are not presented as new runs.

### Next checkpoint and approval boundaries

F2/A05: promote the audit's malformed normal-command ingress trace into the
replication suite. Prove rejection happens before log/WAL admission, then prove
restart still accepts and applies the next valid command. Inspect proposal,
receive and replay boundaries separately; do not silently drop committed bytes
or turn a malformed normal command into configuration.

After A05, tackle A10 multi-block synchronous flush, then A11 token handling.
Keep material byte/economic changes behind owner review. F1 fixes do not make
authority renewal (A01) or venue lowering (A03) safe; no live sender is authorized.

## CP-002 — F2, prevent poisoned history and credential exposure

### A05: reject malformed normal proposals before log admission

Permanent test `test_malformed_normal_commands_never_enter_durable_history`
failed before the fix with `V2 replication test failed: malformed normal command
entered the replicated log` (exit 1). Inputs are empty, one garbage byte,
truncated, trailing-byte and corrupted-digest frames. The test checks unchanged
log/commit/apply/durable indices and canonical state digest, no committed result,
then a valid command commits, survives restart, and the next valid command works.

Fix: decode on both proposal lanes before staging/Raft admission. Malformed
normal input returns existing `ProposeStatus::refused`; control retains existing
`not_a_control_command`. No enum ordinal or canonical bytes changed. The normal
path gains bounded O(command bytes) decode/hash work using owner-thread codec
scratch; no new allocation. Structural validation does not execute the reducer
or decide state-dependent economic applicability. Those decisions remain behind
commit and may produce normal typed refusals.

Focused verification: rebuilt Release `v2-replication-tests`, then executed it:
PASS. Directly compiled that suite against the HEAD replication source in
`/private/tmp/f2-replication-original.cpp`: exit 1 at the exact assertion above.
Executable `/private/tmp/f2-replication-mutant`; no CMake mutation cache involved.

Boundary review: follower AppendEntries still comes from configured consensus
peers, not client proposal ingress. This is not Byzantine peer validation. Replay
continues refusing undecodable committed history; it does not skip bytes or
repair an already poisoned directory. Existing invalid-history evidence remains
relevant. A05 prevents new poisoning through the public proposal API only.

### A10: distinct blocks, one durability boundary, retained ownership on failure

`WalSegment::flush` formerly peeked the sealed front repeatedly, wrote duplicate
records and then threw `WAL completion order violation`. The permanent suite
now stages three independently valued encoded records in distinct blocks and
asserts recovered payloads, order, record count and final durable index. A sync
observer checks that the index has not advanced at sync and that one batch uses
one sync. Further tests inject sync failure and failure before the second block
write; retries must recover exactly the original distinct records. A submitted
writer descriptor makes synchronous flush refuse before writing or crediting it.

Fix: snapshot each sealed descriptor once without advancing the async submission
cursor; write the batch, sync, then complete in order. Allocate the descriptor
vector before I/O (bounded by the arena capacity). Catch both write and sync
failure to roll back the append frontier and preserve retry. A private friend
relationship exposes arena ownership to the segment without adding object fields
or public mutation APIs. O(number of sealed blocks) work and temporary descriptor
space; no early recycle or added durable claim. The alternative `next_unsubmitted`
loop was rejected because failure would strand its advanced cursor.

Added an optional `before_block_write` fault checkpoint at the end of the local
checkpoint enum, preserving prior ordinal values. It is not a wire contract and
has no observer by default. Injected failures prove these program paths, not the
full physical power-cut/storage contract (still F6/F7).

Original RED and repeat-front mutation both exited 1 with
`V2 WAL arena test failed: WAL completion order violation`. Removing the
mixed-ownership guard exited 1 with `V2 WAL arena test failed: synchronous flush
silently mixed with writer-owned work`. Mutants were directly compiled with
ASan+UBSan outside CMake; production source was not mutated. Focused rebuilt WAL
suite passed before the full regressions below.

### A11: private atomic token output and no bearer in argv

Original-code mutation/reproduction against HEAD tools, using permanent tests
through the existing CLI/get entry points, failed for the intended reasons:

```text
test_mint_replaces_existing_file_with_private_mode: AssertionError: 420 != 384
test_probe_does_not_put_bearer_in_argv_and_validates_token:
  'aaa.bbb.ccc' unexpectedly found in curl argv
```

420 and 384 are decimal 0644 and 0600. Only dummy secrets were used. Earlier
delegated test errors about missing newly introduced helpers were NOT accepted
as RED evidence; the main reviewer replaced that evidence with these observable
failures. Review also reproduced invalid-expiry replacement of the old token and
newline-bearing credentials reaching the mocked transport, then fixed both.

Output uses no-follow, directory-descriptor-relative traversal, verified private
ownership/mode, exclusive 0600 temp creation and atomic rename. Parent traversal
and symlinks are refused (verified root-owned macOS system aliases are the narrow
exception). Ordinary relative paths still work. Validating JSON/token claims and
formatting expiry precede replacement, so malformed response data cannot destroy
the previous token. Partial-write, descriptor setup, sync and rename exceptions
clean up the temp file while preserving the old token.

Auth origin is fixed to canonical HTTPS; invalid origin is refused before key
access. Curl config and redirects are disabled, requests have a timeout, and
errors do not echo auth response bodies. Probe paths are exactly allowlisted,
token characters checked without whitespace normalization at the transport seam,
and the header is supplied through stdin config rather than process arguments.
Removing `O_NOFOLLOW` in an in-memory mutant fails the permanent parent-symlink
test at `OSError/RuntimeError not raised`, not an incidental helper/import error.

Limits: not a token-scope or cryptographic-signature proof; not protection against
same-user/root attackers or prior exposed copies; no directory-rename power-loss
guarantee. Raw probe response files may contain venue-private data. No real key,
auth request, network call or live order was used. `model/` remains untouched.

### CP-002 verification and next boundary

Offline tool regressions: **24/24** (15 token tests plus 9 existing tooling tests),
using `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_*tools.py'`.
Passive model: **109/109**, bytecode writes disabled. Python tests remain a
separate gate, not included in CTest totals; F7 must include them in its inventory.
Freshly rebuilt C++ suites: Release **36/36**, 6.77 s; Debug ASan+UBSan
**36/36**, 42.44 s. Same isolated build directories and build-before-CTest recipe
as CP-001, with separate test scratch roots to avoid concurrent mutation-suite
collisions:

```sh
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j2
TMPDIR=/private/tmp/velocita-f2-release.R2Ppr8 \
  ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j2
TMPDIR=/private/tmp/velocita-f2-sanitized.5G048P \
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
```

LeakSanitizer is explicitly disabled; runtime UB halts on the first violation.
No new TSan, NUC, ARM64 container or Pi run. `git diff --check` clean. No commits,
tag changes, protected-path edits, source-root re-pin or deployment.

Next after F2: F3/A03, fail closed when wire lowering cannot preserve the exact
economic intent and instrument metadata. The swap/effect schema remains proposed;
do not encode new venue economics without owner review. A01 authority renewal
also remains open, so no live sender or deployment is authorized.

## CP-003 — F3, quarantine unbound wire lowering; review the binding

Scope: fail-closed existing-contract correction only. Both body APIs lack the
committed metadata/capability context needed to prove the requested economics.
Market has no authorized operation in the present reducer; a zero-price fixture
must not invent one. Limit's caller-set verified flag cannot bind exact factors,
assets, routing, post-only or TIF. Quarantining them is safer than admitting a
subset on undocumented defaults. No live callers were found; decimal arithmetic,
profiles, order lifecycle, risk and reconciliation are retained.

`docs/F3_EXECUTION_METADATA_REVIEW.md` proposes committed content-addressed
catalog identity, with a single immutable root per deployment lineage as the
MVP candidate versus per-effect binding for dynamic catalog rotation. Neither
is approved or encoded. Fresh host layout report confirms TradingState 81360 of
81920 bytes and max snapshot 65451 of 65536: changes require measured sizing,
not an assumption that an extra digest fits every retained record.

Permanent tests cover ordinary/post-only/zero-price fabricated market intent,
wrong venue/instrument, same-product factor aliases, GTC/IOC/FOK/legacy post-only
and both values of the local schema flag. Every rejection checks zero returned
length and unchanged sentinel-filled output. Existing cancel refusal remains.
The arithmetic-only predicate deliberately still accepts both equal-product
specifications; the test proves that neither can enable an unbound body encoder.

Removed unreachable JSON assembly and body golden tests that rewarded unsafe
conversion, not reducer or order-management functionality. Public signatures and
error ordinals are retained. Quarantined functions are constant work, allocation
free, and do not inspect string lengths or copy any output. They are a safety
stop, not a performance claim about a future adapter.

Main reviewer rebuilt focused wire tests GREEN, then all-test targets before
CTest: Release **36/36**, 3.13 s; Debug ASan+UBSan **36/36**, 38.32 s, using
CP-002 build paths and isolated TMPDIRs. Leaks explicitly disabled, runtime UB
halts. Python/model were not changed or rerun in CP-003; their CP-002 results
are historical. No new target run, external call, gate pin or canonical bytes.
Permanent test-only RED (before implementation) exited 1 with:
`V2 venue wire test failed: existing limit intent was lowered to an unapproved
market order`. Main independently compiled the current suite with the HEAD
wire source (`/private/tmp/f3-original-venue-wire.cpp`) and observed the same
failure. Direct compilation avoids CMake mutation caching.

Independent market and limit guard mutants returning `{none,0}` were each killed
by their own expected-refusal assertion, including:
`V2 venue wire test failed: schema boolean authorized metadata or capability not
bound to the effect`. The main reviewer independently ran that limit mutation
(`/private/tmp/f3-limit-guard-mutant.cpp`, executable
`/private/tmp/f3-limit-guard-test`) with the market quarantine still intact.
The delegated direct mutation builds also used ASan+UBSan. Thus the limit result
is not merely the first market test masking a later missing check.
`git diff --check` is clean. Changes are uncommitted; protected paths preserved.

F3 cannot close merely because a quarantined encoder refuses everything. Next
step is owner review of the catalog direction, then an exact schema/retention/
layout plan before encoding any new material contract.

## CP-004 — F3 catalog direction approved; operational boundary review

Owner read and approved `docs/F3_EXECUTION_METADATA_REVIEW.md`. Record that as
approval of one immutable committed catalog identity per deployment lineage,
not of previously unspecified commands, migration or canonical row encoding.
The review now proposes install-once/bootstrap rules, a 140-byte root-install
command, one 32-byte snapshot root, explicit immutable reducer context, historical
binding materialization and fail-closed legacy persistence handling.

Main review verified current command kinds 1..19, codec version 7 shared by
commands/results, TradingState fields, historical proposal retention and the
snapshot-then-WAL recovery path. A bounded read-only subagent review independently
flagged artifact availability as a node barrier rather than an economic refusal.
Main review rejected silently adapting legacy snapshots to an uninstalled root:
replaying old successful authorizations under new prerequisites changes history.
The proposed fresh-lineage boundary preserves old data without claiming migration.

This checkpoint changes documentation only. No new production/test code, canonical
bytes, runtime gate or deployment. No build or test rerun for this checkpoint;
CP-003's test counts remain historical, not fresh evidence for catalog binding.
No tags, protected paths, budgets or venue capabilities changed. F3 remains OPEN;
the remaining material contract choices are explicitly labelled for owner review.

## CP-005 — F3 recovery compatibility before writable startup

Owner approved the installation/recovery proposal and fresh-lineage command,
result and snapshot schema 7 -> 8 transition. New ADR 0012 records what was
approved, rejected alternatives and costs. Exact catalog rows/venue mappings,
transport envelope and budget increases are still not approved. No schema
constant changes in this increment: switch versions atomically with root
semantics rather than create partially upgraded version-8 history.

### RED and smallest GREEN

Main independently reproduced the new test-only REDs before production edits:

```text
unsupported-snapshot: unsupported snapshot schema fell through to full-WAL replay
unsupported-segment-suffix: unsupported command schema in uncommitted segment suffix started
unsupported-legacy-suffix: unsupported command schema in uncommitted legacy suffix started
```

Each exited 1. Fixtures repair inner SHA-256/outer CRC after changing the schema,
so failure is not merely a broken checksum. Preflight now loads snapshots and
decodes all complete retained commands in both stores before opening the writable
segment/writer. Uncommitted commands are decoded, NOT applied. The read-only
segment helper shares parsing with the existing reader; I/O failures throw rather
than masquerade as an empty log. Existing torn-tail framing behavior is retained.
Unsupported snapshot frame/state versions now throw instead of returning absent.

Main review found a further counterexample: a larger future snapshot returned
`length_mismatch` before its unknown schema was recognized. Permanent test
`unsupported-oversized-snapshot` then went RED with:

```text
oversized future snapshot was classified as current-schema corruption
```

Moving schema recognition ahead of the version-specific size bound made it GREEN.
The snapshot-only test covers canonical and outer-frame mismatch and proves no
new WAL file is created. The segmented fixture additionally preserves a torn
three-byte tail. New preservation oracles compare exact relative filenames and
file bytes, not hashes. The older std::hash helper is not used for this guarantee.
Tests for supported-schema corrupt-snapshot replay and committed-loss refusal
remain in the passing suite; no blanket rejection of all corruption was added.

### Validation and cost

After rebuilding all-test targets: initial Release **36/36**, 6.63 s and Debug
ASan+UBSan **36/36**, 43.10 s. Main then strengthened the version matrix: the
full-WAL snapshot and legacy suffix use an older schema; the segmented suffix,
oversized and snapshot-only cases use newer schemas. This prevents testing only
one side of the incompatibility boundary. Final rebuilt Release **36/36**, 2.56 s;
Debug ASan+UBSan **36/36**, 37.16 s. Runtime `UBSAN_OPTIONS=halt_on_error=1`; leak detection
explicitly disabled. Compiler flags include `-fsanitize=address,undefined`,
not a claim of compile-time `-fno-sanitize-recover`. Builds are the CP-002 isolated
paths; TMPDIRs are `/private/tmp/velocita-f3-release.04kelR` and
`/private/tmp/velocita-f3-sanitized.34FaYW`. Five focused selectors pass.
Raw observed outputs: `f3_recovery_2026-09-21/red.txt`, `green.txt` and
`green-final.txt`. Initial RED captures precede these fixture refinements.

Main independently compiled five guard mutants directly, linking the final
rebuilt test object and unchanged static dependencies. Each compiled and exited 1
at its intended assertion: removed state-schema throw -> snapshot fell through;
removed WAL preflight calls -> uncommitted incompatible suffix started; early
writable segment construction -> directory inventory changed; removed outer-frame
version throw -> unsupported frame started; late schema recognition -> oversized
future snapshot misclassified. Exact commands/output are in `mutations.txt`.
No production workspace source was mutated for these runs. The delegated agent
provided permanent tests and a scoped no-blocker code review; its unfinished
mutation attempt is not evidence. Main mutation results are the evidence.

Startup adds a linear canonical-decode pass over retained WAL records; no new
per-order/reducer work or state/outbox bytes. Existing startup history allocations
remain unbounded (F5), and reading both layouts can retain both vectors until
startup completes. No throughput improvement is claimed. Replay still invokes
the reducer only through the existing committed apply callback.

No model/tool changes or reruns, target run, acceptance re-pin, external call,
trading action, commit or tag change. Protected paths and pre-existing edits
remain intact. F3 remains OPEN: exact catalog schema, root installation, immutable
context, historical binding, artifact-dependency recovery and coherent version-8
switch are not implemented by this prerequisite.

## CP-006 — parallel prerequisites, with main review corrections

Two scoped implementation workers were used; the main agent retained contract
design and reviewed their diffs, oracles and claims. No new persisted/economic
contract was encoded. F1/F2 remain host GREEN; F3–F7 are not declared complete.

### F6: useful coverage, deliberately limited claim

Added default-inert MetadataStore callbacks and a six-point fork/_exit matrix.
Before atomic rename the named metadata must be the independently specified old
tuple; afterwards it must be the complete new tuple. Exact temporary-file shapes
pin where the fault occurred. The null-callback production path keeps one write,
file fsync, rename and directory fsync; no wire/disk bytes change. Callback storage
adds a function pointer and context pointer to MetadataStore, not TradingState.
This is a coverage increment, not a reproduced production defect followed by GREEN.

Main rejected the agent's directory-sync-plus-callback-removal mutation as proof
of syncing. The corrected mutations remove ONLY the file or directory fsync and
both survive: process exit preserves kernel cache. Main inspected their diffs and
reran both, plus the rename-removal mutant which fails the intended wrong-generation
assertion. The final smoke test's name/comment/message were corrected to claim
callback observation only. ADR 0006 and `f6_metadata_2026-09-21/README.md` retain
the correction. G-047, power-loss modeling and asynchronous hard-state fencing
remain OPEN. Removing sync or returning from an async callback before term/vote
durability would be an unsafe shortcut, not this fix.

### F7: source identity and test inventory are separate checks

The new source helper covers CMakeLists, shared include/src, top-level tests,
optional cmake inputs and deploy scripts/configuration/patches. The old v2-only
recipe omitted shared storage implementation; hashing it could not identify the
code that actually ran. Sorted relative paths and explicit C collation remove
ambient locale dependence. Model/research/docs are excluded intentionally.
The Phase 7 wrappers no longer provide their own expected root; the operator must
supply the candidate from an independently retained review record. This does not
cryptographically enforce where an operator obtained that value.

Main review strengthened the initial submission in three areas:

- Nonempty inventory is insufficient. The gate now compares CTest JSON against
  an independent explicit list of 36 Release / 33 V2 instrumented/sanitized suites;
  omitted, unexpected or duplicate suites refuse. Disabled/skippable required
  suites must refuse too. `--no-tests=error` is retained as a separate guard.
- Shell `printf "$(hash_file ...)"` masked hash failures. The helper now checks
  subprocess status and exact lowercase 64-hex digest before publishing a root.
  A fake hasher previously exited 9 for file inputs yet the helper exited 0;
  the permanent test now requires failure and no root output.
- Symlinks could omit selected inputs, and output aliases could overwrite source
  bytes or become part of their own manifest. Selected input symlinks refuse;
  output under selected trees or to a directory refuses. Publishing by replacing
  a temporary pathname avoids writing through an existing output file's aliases.
  This assumes exclusive checkout ownership, not defense against a concurrent
  adversary changing the tree during hashing.

Agent-preserved RED observations: a selected symlink originally returned 0 rather
than 65; `cmake/toolchain.cmake` was absent; the failing file hasher returned 0;
an output inside deploy returned 0 rather than 65. Inventory-helper introduction
first failed because the helper was absent, which is implementation absence, NOT
a reproduced acceptance of a missing suite. Subsequent negative controls exercise
the implemented helper. Do not relabel all these cases as production defects.

Containerfile adds Python 3 for the inventory validator. This new container image
has NOT been built here. No container/NUC/Pi gate, frozen root, mixed replay or
acceptance re-pin is claimed. Legacy phase wrappers still carrying old recipe
roots will mismatch this new shared recipe; historical evidence remains valid
only for its original tree. Use the corrected Phase 7 entrypoints for the next
reviewed candidate, not a silently refreshed historical pin. Root identity does
not by itself prove dependency-image/toolchain integrity or live process readiness.

### F3 and next contract boundary

`docs/F3_CATALOG_V1_BYTE_REVIEW.md` proposes bounded canonical rows, exact asset/
precision identity, an account denomination rule and five appended result codes.
It remains awaiting owner approval, with no catalog or schema-8 bytes encoded.
The existing install/fresh-lineage decision is already approved; do not ask again.

Main and worker inspected pinned Raft: `raft_apply_entry` increments its index
before calling applylog. In addition to explicit wrapper apply calls, periodic
and begin-snapshot can apply internally; snapshot loading directly sets the index.
Therefore future missing-artifact checks must establish readiness before entering
those paths, not throw from the reducer after the index has moved. ADR 0012 records
the trap. Next approved-contract RED must assert catalog absence cannot advance
Raft index, produce results, mutate economics, publish snapshots or serve authority.

Remaining order: approve/catalog implementation and binding proofs; F4 authority
protocol review then stale-leader/delayed-proof tests; F5 identity/bounds/transfer/
archive contract review; F6 asynchronous fencing and stronger storage fault model;
then freeze one complete candidate and perform F7 target gates. Do not parallelize
unreviewed byte contracts merely to claim all roadmap boxes are finished.

### Final verification of this batch

Main rebuilt both isolated all-test targets before CTest. Release **36/36**,
3.19 s; Debug ASan+UBSan **36/36**, 40.18 s (`ASAN_OPTIONS=detect_leaks=0`,
`UBSAN_OPTIONS=halt_on_error=1`). The final replication binaries contain the
corrected completion-checkpoint assertion. No fresh TSan result is claimed.
Main reran final offline Python discovery **40/40**, including **16** gate-helper
tests, and passive model **109/109** with bytecode writes disabled. Owned shell
scripts pass bash syntax checks; git diff --check is clean.

Main also generated actual CTest JSON from the rebuilt Release tree and validated
it against the gate's independent list. A local expanded source-manifest run
succeeded, but its dirty-tree root is diagnostic only, NOT a deployment pin.
Temporary validation directory prefix: `/private/tmp/velocita-cp006-gate.*`.
No image build, target contact, network/venue call, order, commit or tag change.
Protected tracked paths remain clean; protected `model/` remains untracked.

Additional worker-observed review REDs retained here: top-level CMake symlink
returned 0 rather than 65; copying output through a symlink replaced its target's
`preserve me` bytes; inventories containing DISABLED, SKIP_RETURN_CODE or
SKIP_REGULAR_EXPRESSION each returned 0 instead of 1. Final tests cover these,
output hardlinks/directories, exact runnable inventory, missing/unexpected/
duplicate suites and failed hashing. These regressions verify host helpers;
they do not substitute for executing the revised gate in its target image.

## CP-007 — F3 approved catalog-v1 codec and owning context

Owner approved `F3_CATALOG_V1_BYTE_REVIEW.md` before encoding. Implemented only
the standalone prerequisite: canonical catalog schema 1, bounded encoder/decoder,
exact five-field lookup and owning verified context. Kernel/result/snapshot stay
schema 7; no persisted ApplyCode ordinal, OrderEffect or TradingState changed.
Full F3 remains OPEN, and both unbound sender APIs remain quarantined.

Initial positive-path RED: `valid catalog did not encode`, exit 1 against
compileable fail-closed stubs. Independent byte/digest oracle, actual REDs,
review corrections, reproducible commands and proof limits are recorded in
`f3_catalog_2026-09-21/README.md`. Main reviewed the implementation and independent
test worker, corrected draft row-size/ownership/testing issues, and expanded
coverage with every-prefix parsing, correctly hashed malformed structures,
every-short-output refusal and aliased-output tests.

All six scoped direct mutants fail at their intended assertions. One originally
survived: deleting exact divisibility. The new permanent case preserves the
integer quotient while leaving a remainder, independently killing that mutant.
Do not summarize the initial test suite as having proved this invariant.

Rebuilt final Release **37/37** (6.98 s), Debug ASan+UBSan **37/37** (43.93 s),
runtime UB halt and leaks disabled. Direct catalog suite at cache-line setting
128 also passes. Main checked actual CTest inventory against independent expected
37 total / 34 V2; offline Python tools **40/40**, passive model **109/109**.
Context **8488 bytes**, alignment 8, outside TradingState and below 16 KiB;
maximum artifact **8076** below 8192. Unchanged TradingState **81360** and max
snapshot **65451**. No target proof, performance claim, source freeze or tag change.

### Exact next RED and integration guardrails

Using an owned verified fixture, prove install kind 20 persists its approved
root once and round-trips through canonical snapshot; conflicting/zero root,
stale controls or economically nonempty installation cannot change that root.
Then prove authorization cannot use absent or mismatched exact catalog rows.
Missing local artifact is a NODE dependency failure, not an economic refusal:
prove no applied-index advance, reducer result, economic mutation, snapshot
publication or authority service on missing/mismatched required bytes.

Implement the approved command/result/snapshot schema-8 transition coherently,
not as a separately shipped version bump. Review every implicit Raft entry point:
constructor/recovery, periodic, proposal, peer receive, explicit apply,
begin-snapshot/compact and snapshot loading. The library advances its applied
index BEFORE the callback; checking artifact readiness there is too late.
Hold the catalog const and alive for all row borrowers. Keep within current
TradingState/snapshot ceilings and reject any proposal that silently widens them.
These approvals already exist; new F4/F5 transport/authority/storage contracts
still require separate review. Pi stays deferred to the final target stage.

## CP-008 — F3 immutable root, schema 8 and readiness integration

2026-09-22. Continues the owner's existing approval; no new economic/byte choice
was inferred. Kind 20, root32, codes 62..66 and command/result/snapshot schema 8
land together in the uncommitted working tree. OrderEffect bytes do not change.
Old lineage storage is retained, not converted or replayed with new semantics.

The replica owns a const catalog; apply/replay receive explicit context. Staging
and authorization bind exact rows; materialization copies historical versions
and root. Gateway pre-send/planner consume that binding, require runtime version
agreement and operation capability, and reject invalid fixed-array counts before
indexing. Real encoder/venue admission remains blocked; metadata is not authority.

Pinned Raft now has an optional pre-apply callback BEFORE index advancement;
NULL retains legacy behavior. A wrapper-only check misses indirect apply paths,
and applylog runs too late. Missing required artifact latches the node closed
without reducer result/state/index advance. Snapshot startup additionally checks
retained metadata against catalog rows before publication. First-install
dependency lookup respects preceding deterministic admission failures; this cold
path uses bounded identity scratch, not a TradingState copy or external I/O.

Evidence, actual RED messages, independent schema-8 oracle and proof limits:
`f3_binding_2026-09-22/README.md`. Main reviewed both workers, rejected unreachable
capacity fixtures, reproduced the snapshot bypass and directly killed its
root-only mutant. Public-command capacity traces now encode canonically; max
state-version and malformed-count cases are explicitly synthetic boundaries.

Final rebuilt host Release **38/38** (2.75 s), Debug ASan+UBSan **38/38**
(39.60 s), runtime UB halt and leaks disabled. Offline tools **40/40** and passive
model **109/109**. Actual CTest inventory matched independent **38 total / 35 V2**.
Direct host layout compile with cache-line setting 128: TradingState **81,392**
bytes, headroom **528**; max snapshot **65,483**, headroom **53**; separate context
**8,488** bytes. No hardware/latency inference. Raw final CTest logs and independent
golden reconstruction are retained in the evidence directory. Diff check clean;
protected tracked paths clean and protected model/ remains untracked.
No target, venue, commit, tag or source-freeze action. Full F3 remains OPEN for
real encoder/venue admission and target gates; F4/F5 require material contract
review before implementation, F6/F7 remain partial. Exact next step: review F4
authority/fencing against the current reducer/gateway/replica boundary. Keep
unbound encoders quarantined, and keep the book attribution/evidence limitations
in the roadmap. Pi stays deferred to the final target stage.

## CP-009 — F4 authority review and live-code REDs

2026-09-22. Read-only production review plus new retained diagnostic tests/docs.
`f4_authority_2026-09-22/authority_red.cpp` uses current committed installation and
explicit verified context. Partition and delayed-reply modes each exit 1 at the
intended send-permission assertion: majority kill=2 / stale kill=1 still permits;
a reply captured at tick1 and received at tick10000 still renews a 50-tick bound.
These audit REDs are intentionally not registered as passing CTest coverage.

Two independent bounded agent reviews covered quorum issuance and gateway
consumption; main reviewed code and timing/transport limits. Proposed ADR 0013
and `../F4_AUTHORITY_PROTOCOL_REVIEW.md` recommend one non-economic committed
barrier per new-risk attempt, request-start expiry and one-shot consumption.
Explicit costs are extra durable RTT/WAL pressure; exact bytes and the live
duration remain unapproved. Accepted cancel availability and external-fencing
limits are called out rather than silently tightened or waived.

Fresh build then focused existing regression **2/2** (replication + gateway,
1.24 s), diff check clean. No new full-suite/sanitizer/target claim; CP-008 remains
the last full host checkpoint. Protected tracked files remain clean; pre-existing
Python-cache/model paths were preserved. No production/schema/byte change, venue
or target call, credentials, commit or tag action. Next: owner semantic approval,
then exact barrier/IPC byte review before implementation. F4/A01 remain OPEN.

## CP-010 — F4 approval recorded and exact durable-frame review

2026-09-22. Owner explicitly approved marker per new-risk send attempt and the
semantic recommendations. ADR 0013, architecture and current routing now record
that approval; CP-009 remains the historical review/RED record.

Added `../F4_AUTHORITY_BARRIER_BYTE_REVIEW.md`: proposed custom type 101,
schema-1 V2AB frame, body 144 / total 186 bytes. Existing effect digest binds
intent; separate copied historical versions still require checks. Domain equality
is explicit, while production derivation/authenticated IPC remain F5/integration
dependencies. Kernel/result/snapshot schema 8 and V1 remain untouched. Old-reader
refusal of retained markers is not an anti-rollback guarantee after compaction.

Main inspected the pinned type namespace and actual persistence/apply dispatch;
two bounded independent reviews covered framing and slot/recovery semantics.
Main rejected a proposed one-marker-per-retained-branch invariant: a healthy
lagging follower can retain several. The corrected proposal reconstructs the
highest retained unapplied-marker watermark and blocks local issuance, not peer
history. It exposes the old-term availability cost without inventing a no-op.
F4 retry admission bounds must not be presented as total WAL/queue bounds.

Documentation only; exact bytes remain unapproved and unencoded. No production
change, new test run, target/venue contact, commit or tag. CP-008 remains the
last full host verification and CP-009 the last focused build/regression. Next:
owner exact-byte/domain/drain review, then codec and composed permanent RED/GREEN.

## CP-011 — approved F4 marker prerequisite and availability proof

2026-09-22. Owner approved exact bytes/domain/drain and requested extra care on
the explicit availability trade-off. Implemented type-101/schema-1 186-byte codec,
typed persistence/peer/apply dispatch, structural replay and bounded local marker
admission. One pending request is not a global history bound; derive the highest
unapplied marker from retained typed entries at owner boundaries, O(suffix),
O(1) extra space. No economic state/schema expansion or live permission API.

Public multi-voter tests pin the old-term stall, no automatic log growth, 100
blocked retries, and unblocking by a current-term command on either lane. Follower
A/B/C recovery, suffix replacement, progressive apply, snapshot and wrong-domain
refusal are covered. One-voter behavior is explicitly distinguished. Tests with
no actual effect prove structural no-op replay, not deliverability/authority.

Found/fixed zero-local-durability persisted commit and failed-truncation payload
ownership/latching defects. Permanent runtime REDs, direct drain/clamp mutants,
independent golden bytes and codec guard mutants are preserved in
`f4_marker_2026-09-22/`. Main corrected a one-entry snapshot fixture and strengthened
the agent's recovery oracle to compare original durable marker bytes, not just
its index. No claim of blanket storage-error coverage; log_offer paths remain
part of F6 follow-up. Standalone apply_encoded_kernel_command remains a business
frame API, not a typed WAL consumer, so it was not widened to accept markers.

Final rebuilt Release **40/40** (2.73 s); Debug ASan+UBSan **40/40** (40.12 s),
runtime UB halt, leaks disabled; tools **40/40**, model **109/109**. Independent
inventory **40 total / 37 V2** agrees. Protected paths preserved, diff check clean.
No deployment acceptance, source freeze, commit, tag or external contact.

Next RED: exactly one live owned proof after fresh current-term durable quorum
and apply, bound to actual historical effect/controls; none for stale controls,
term changes, replay, missing dependency or abandonment. Then wire gateway
request-start age/correlation/consumption and make CP-009 send regressions GREEN.
The legacy boolean renewal path remains; F4/A01 and target guarantees stay OPEN.

## CP-012 — F4 owned proofs and final transport composition

2026-09-22. Implemented the already-approved protocol without new canonical bytes.
One live owned producer result, no replay issuance, exact historical binding and
dequeue revalidation. One noncopyable gateway owner holds bounded pending/accepted
slots; correlation and original-start expiry prevent delayed renewal. Final
single-owner dispatch checks known term/controls, delivery/retry/evidence/quota,
prepared binding and a second clock sample; consumes permission and records
awaiting_response before callback. Transport exceptions cannot restore proof.

Removed leader-boolean renewal (`renew_authority` always refuses) and renamed
the old pre-send predicate to candidate filtering. No compatibility alias can
suggest that a pure predicate is final send permission. Retained committed-cancel
availability, explicit local stop latches and unknown-outcome retry refusal.

Public-command cluster fixtures prove healthy canonical bytes and refused sends
under partition, late reply/queue, changed term/kill/historical binding, restart,
sequence change, clock regression, proof reuse and attempt exhaustion. They also
pin the approved limit: a still-fresh prior proof can send inside an unseen remote
kill window, and a later local kill cannot recall bytes or erase uncertainty.

Main independently reran seven source-copy mutants: no issuance; premature
issuance directly from admission; no dequeue
revalidation (kill and term); no final clock; reusable proof; no age; no request
correlation. Each fails its intended permanent assertion. Initial producer and
consumer work missed preserved original RED evidence; this is not retrospectively
claimed as RED-first. Actual renewal and composition-stub REDs are retained.
An agent mutated shared source during a concurrent build; that intermediate
failure was quarantined, not called a product defect. Final builds followed
restoration and freeze; independent mutations never edited the repo.

Final rebuilt Release **42/42**, Debug ASan+UBSan **42/42** (runtime UB halt,
leaks disabled; existing C++ instrumentation), tooling **40/40**, model **109/109**.
Inventory **42 total / 39 V2**. Measured host sizes: AuthorityProof416,
GatewayAuthority992, BoundOrderEffect224 bytes; ceilings512/1536 on proof/owner.
No TradingState/snapshot expansion. All evidence and limits:
`f4_proof_2026-09-22/README.md`. Protected paths preserved; no external contact,
source pin, commit or tag. Roadmap M3a adds mandatory observability before live.

Next is F5 contract review (identity/domain, bounded queues/history, snapshot
catch-up and audit archival), then TDD. Keep deployment guarantees open until
authenticated process integration, measured timing/fencing and final target gates.

## CP-013 — parallel F5/F6 correction batch and contract review

2026-09-22. Main-owned bounded snapshot read removes proportional allocation
before canonical-size validation, with a genuine allocation-observing RED and
isolated-source mutant. A fixed65,522-byte cold-recovery buffer includes the
lookahead; schema-incompatibility refusal remains, no bytes/schema changed.

F6 worker added permanent flush/term/vote/commit failure cases and post-rename
visibility. Failures latch service closed; cache publication follows successful
save rather than preceding it. Main caught pinned Raft ignoring the self-vote
callback error, added its original RED and post-periodic latch check. A copied
metadata tuple is O(1) stack work; this is not asynchronous I/O or a new journal.

Main independently compiled four current-source latch mutants, plus snapshot
allocation mutant. Agent original flush/term/commit REDs were console-only; main
retained reruns of their old binaries rather than inventing original raw logs.
Vote-specific/post-rename tests are honestly post-fix coverage. Integration caught
an old-archive/new-constructor ABI mismatch in a diagnostic; it was never a RED.

F5 diagnostic source retains intended REDs: existing membership can be changed on
restart; compact12 cannot supply fresh voter applied0; exact audited command3
vanishes from both WAL stores after compact4 with no archive interface. Main
strengthened the archive oracle to check both formats and covered index. No new
identity/capacity/retention/install protocol was silently implemented.

Reviewed proposal: `../F5_F6_RECOVERY_DURABILITY_REVIEW.md` and ADR0014. Owner
response is not recorded yet. Parallelism does not waive material contract review.
Final rebuilt host Release43/43, ASan+UBSan43/43 (existing C++ flags, runtime UB
halt, leaks off), tools40/40, model109/109; independent inventory43/40 V2.
Details/raw logs: `f5_f6_2026-09-22/README.md`. Diff check clean; protected work
preserved; no targets, venues, credentials, freeze, commits or tags.

## CP-014 — approved direction, exact identity proposal and persistence model

Owner approved the F5/F6 recommendations after CP-013. ADR0014 now records
accepted direction, including provisional atomic replacement, while withholding
approval of unspecified byte layouts, numerical budgets, archive retention and
the concrete asynchronous Raft fence protocol. No repeated semantic approval is
needed. Main retained architecture/review ownership with at most two workers.

F5: `../F5_STATIC_IDENTITY_BYTE_REVIEW.md` proposes one atomic local identity
record with a shared manifest span and separate local-node field. Full-file
integrity and shared authority-domain digests have distinct inputs. Main and an
independent reviewer verified 116/120/124-byte arithmetic and current schemas;
review tightened validation-before-digest and exclusive-owner/preflight ordering.
Byte layout, three-voter ceiling and mode/epoch values remain PROPOSED, not encoded.
Bootstrap/recover and fresh-lineage refusal are already-approved direction.

The sidecar closes startup-argument drift only when integrated correctly. It does
not bind substituted same-schema history and cannot stop an old executable that
ignores it. Both limits are explicit next-stage storage/peer/envelope/admission
requirements. Two separate identity files would not fix either issue, but would
add a partial-bootstrap state; that alternative was rejected with reasoning.

F6: the new standalone persistence model executes actual save through test-only
POSIX shims and actual load on the materialized recovered image. Expected tuples
come from independent constants `(7,2,19)` and `(8,3,23)`, never the result under
test. Successful return requires recovered new; interruption permits only whole
old/new. Main review added requirements for a frozen cut during exception cleanup,
uncut short-write success and short-write-prefix cuts, plus the real-load check.

Both isolated production-call mutants were independently compiled by main:
file-sync removal => zero durable bytes; directory-sync removal => acknowledged
old tuple. No assertion about checkpoint counts is the killing oracle. This is
retrospective mutation evidence for newly added tests, not a production fix.
CMake and the independent gate inventory now include the standalone suite.

Full rebuilt host Release44/44, Debug ASan+UBSan44/44, tools40/40, model109/109;
inventory44 total/41 V2, diff check clean. Same sanitizer scope as CP-013: runtime
UB halt, leaks off, C++ instrumentation, no new Raft-C claim. Raw logs/patches and
scope: `f6_persistence_model_2026-09-22/README.md`. The model does not cover every
filesystem writeback/failure/sector history or physical device. F5/F6 remain OPEN;
no target, source-freeze, credential, venue, commit or tag action occurred.

## CP-015 — approved identity codec and standalone startup prerequisite

Owner approved exact V2ID bytes/digests and max3 voters after CP-014. Codec worker
implemented bounded canonical serialization and privately verified values; main
independently checked literal hashes and requested valid shared-field mutation,
signed-ID/mode cases and compile-time schema pins. Codec stub RED and isolated
count/domain/checksum mutations are retained. No live cluster/wire bytes changed.

Main implemented VerifiedIdentityStartup with explicit bootstrap/recover, retained
directory-flock ownership, mandatory matching catalog, descriptor-relative startup
I/O, bounded read and atomic publication/retry. A second worker reviewed failure
and ownership paths. Main added filetype, process-lock, unwind and permission
coverage; test inventory does not block on planted FIFO/symlink entries. Partial
temporary identities are preserved/refused, while complete exact temps can resume.

Final main review caught a new visibility/durability gap: matching recovery could
read a renamed final identity whose bootstrap never synced the directory. A
permanent interruption RED now pins that no owner is issued across a failed
success-path durability fence. Matching recovery resyncs the validated file,
directory and parent. That checkpoint/control-flow test is NOT sync-sensitive
power-loss evidence, and the new startup module still needs that stronger proof.

Original startup stub RED, world-writable-directory RED, recovery-fence RED and
three final-source guard mutants retained. Main compiled isolated copies removing
only flock, identity equality or catalog validation; each accepts exactly the
startup/owner that its permanent test forbids. No shared-source mutation.

CRITICAL remaining seam: ReplicatedKernel has not consumed this token; the raw
constructor still bypasses it. Rebuilt current-tree A04 confirms old three-voter
history becomes a one-voter leader. That failure is retained, not hidden behind
the two new GREEN suites. Next integrate the first-member owner, derive all static
configuration, remove raw/default args and migrate fixtures without making their
old corruption/schema/authority tests fail at an unrelated missing-identity check.

Final rebuilt host Release46/46, Debug ASan+UBSan46/46, tools40/40, model109/109;
inventory46 total/43 V2, diff check clean. Same sanitizer scope as CP-014. Evidence:
`f5_identity_2026-09-22/README.md`. F5/F6 and target acceptance remain OPEN. No
commit/tag/source freeze, credential, venue or target action; unrelated work kept.

## CP-016 — mandatory verified kernel startup; A04 startup drift corrected

The owner requested the already-approved token integration. Main removed the raw
constructor, derives every static configuration field from VerifiedIdentityStartup,
and keeps the validated owner first/destroyed last. Catalog is referenced from
owned storage, not caller memory or a second copy. Read-only revalidation catches
moved ownership, already-replaced directory names and changed identity before
stores construct. Stable trusted paths remain an explicit assumption for later
pathname-based store I/O; no hostile same-UID race protection is claimed.

Original constructor API RED and pre-fix A04 binary rerun retained. Snapshot-backed
three-to-one membership drift now has typed identity_mismatch, unchanged names/
bytes and healthy original-state recovery in normal CTest. New kernel-startup
suite covers self-quorum refusal, token/path/file misuse, live writer lock lifetime
and corrupt-metadata constructor unwind. Later coverage is not labeled original
RED-first. Independent inventory now has 47 total/44 V2 tests.

Two bounded agents migrated disjoint test suites; main reviewed and integrated.
Bootstrap/recover choices are explicit. Main caught a copied-history power-cut
fixture accidentally refusing at bootstrap rather than its intended history guard;
corrected recovery restores the original oracle. Missing catalog legitimately
fails earlier; valid alternate-catalog startup still exercises the true before-
apply dependency guard. Three isolated mutants fail intended observable effects:
storage created under replacement path, unauthorized self-election, and incorrect
apply/state advancement. No shared production source was mutated.

Final rebuilt host Release47/47 and Debug ASan+UBSan47/47 (runtime UB halt, leaks
off, existing C++ instrumentation, no new Raft-C instrumentation), tools40/40,
passive model109/109. Raw logs, exact patches, rationale and limits:
`f5_kernel_startup_2026-09-22/README.md`. Current-source replacement-catchup and
archive diagnostics remain RED outside passing CTest. No new persisted/economic
bytes, user-file deletion, target action, commit/tag or source freeze. F5/F6 stay
OPEN pending lineage/storage/peer/install/archive contracts, bounded budgets,
async fences and final target evidence; Pi remains deferred, not waived.

## CP-017 — identity-bound history and peer semantics review (PROPOSED)

Owner requested review, not encoding. Main inspected actual storage/peer/recovery
paths and one bounded agent independently challenged them. New proposal:
`../F5_HISTORY_PEER_IDENTITY_REVIEW.md`. Reuse manifest_digest; distinguish local
voter storage wrappers from shared portable content; bind every request/reply
before Raft; reject foreign/unsupported history without generic corruption fallback.
Record-level WAL binding is recommended over the cheaper block-only option for
independent record provenance. One active block-WAL format for fresh lineages is
proposed to eliminate implicit block-versus-flat recovery selection; compatibility
removal requires owner approval. No bytes frozen or behavior implemented.

Source review identified a critical scope distinction: missing/rolled-back voting
memory and fresh replacement under reused node ID are NOT solved by static identity.
Genesis and replacement admission must be separate; final catch-up cannot reuse
the current diagnostic's empty ordinary-voter shortcut. These are source-derived
risks with required future REDs, not newly executed reproductions. Identity also
does not authenticate a peer, establish sync durability or fence old executables.

Updated ADR0014 routing, handoff and changelog; corrected stale standalone-startup
status in earlier review introductions while retaining their audit history. No
source/tests/builds modified/run; CP-016 remains the latest executed baseline.
No target, venue, credentials, commit/tag or source-freeze action. F5/F6 remain OPEN.

Owner update after CP-017: all six history/peer semantic recommendations approved,
including per-record local binding and block-WAL-only fresh deployment. Recorded
approval in ADR0014, review and handoff; exact bytes and concrete recovery protocols
remain unapproved/unimplemented. Added F5 closure checklist (binding, whole-node
bounds, safe peer install, archive-gated reclamation, integrated evidence). No
source change or test rerun; CP-016 remains the latest executed host baseline.

## CP-018 — parallel F5 REDs/proposals and F6 worker-failure fix

Owner requested all remaining F fixes in parallel with MVP focus. Two workers
handled a bounded existing-contract fix and capacity review; main retained shared
source/build ownership, integrated and independently checked the results.

Worker exception capture now publishes a permanent atomic fault to kernel service
boundaries; stop joins a failed thread and restart refuses. Main caught an actual
draft bypass: direct polling could resume and publish durability after failure.
Permanent original RED/GREEN closes it. New real-thread tests and isolated latch/
owner-guard mutants exercise the fault path. Worker original RED was not captured;
do not pretend the retrospective mutants are original TDD evidence. Worker build
cache copy redirected into a shared root; main discarded that result, inspected
sanitizer flags and rebuilt before recording final evidence.

Five standalone F5 diagnostics now reproduce foreign metadata acceptance, foreign
WAL/snapshot economic-state substitution, foreign vote term mutation and lost
hard-state reset. Source is retained but not counted as GREEN CTest. Exact bytes
remain unapproved: `../F5_BOUND_FRAME_BYTE_REVIEW.md` proposes common framing,
limits and corrected metadata-first genesis ordering. Capacity/result ownership,
archive/install and async protocol details remain distinct approval gates.

Final host Release47/47, ASan+UBSan47/47 (existing C++ scope/UB halt/leaks off),
tools40/40, model109/109. Focused TSan passes new worker cases with test/WAL/
replication/storage TUs instrumented, not the full program. Evidence/provenance:
`f5_f6_parallel_2026-09-22/README.md`. Parallel plan/deferrals:
`../F5_F7_MVP_CLOSURE_PLAN.md`. No source freeze, target, venue, commit/tag or
credential action. F5/F6/F7 are NOT closed and physical Pi is not waived.

## CP-019 — approved bound framing and ordinary-peer admission

Owner approved CP-018's full exact byte sheet and metadata-first bootstrap retry
table before encoding. Two bounded workers handled common framing and fixture
migration/review; main owned typed peer codec, kernel integration, independent
review, builds and guard sensitivity. No commits/tags/source freeze/target action.

Common framing is explicit LE, length/kind/scope bounded, integrity checked before
identity classification, and publishes only a borrowed immutable-input view.
Main caught overlapping input/output corruption in the initial encoder draft;
scratch staging now preserves input until the complete frame is ready. This
codec does not validate local-storage body semantics or initialize voting memory.

Raw public peer ingress is gone. Every request/reply now passes identity/integrity,
complete shape preflight, route/member/candidate and canonical payload/domain
checks before Raft mutation. A malformed third entry allocates nothing before
refusal. Sixteen-entry outbound prefix batches deliver/commit all forty backlog
entries on three voters; pinned Raft acknowledgement/suffix resend behavior was
independently inspected. Whole-node queues/log retention remain OPEN.

Original stub REDs and independent golden precede codecs. Prior actual foreign
vote RED is now GREEN with exact manifest refusal and unchanged term/log/state.
Isolated direct-build mutants show observable Raft mutation without the manifest
guard, oversized outgoing batches without its cap, and premature allocations
without full preflight. These retrospective mutants are not original RED evidence
for the batch cap. Main also repaired a catalog latch test that initially failed
in helper construction instead of exercising the intended production guard.

Final Release49/49, Debug ASan+UBSan49/49 (existing C++ scope, UB halt, leaks off;
no new Raft-C or TSan claim), tools40/40, model109/109. Independent inventory49
(46 V2), diff check clean. Main integration/mutant builds use cacheline64. Evidence:
`f5_bound_peer_2026-09-22/README.md`. Four current-source local-history diagnostics
still RED and retained. Next already-approved work is typed local storage and
metadata-first genesis/recovery; F5/F6/F7 and live admission are NOT closed.
