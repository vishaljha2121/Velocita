# V2 message, context and memory atlas — 2026-09-22

Verified against the **dirty working tree**, branch `v2/phase-5-corrections`, base HEAD `d44a4a9`, through CP-025 outbound/log ownership and native size updates (2026-09-23). This is an explanatory snapshot, not a new contract, acceptance gate, or permission to deploy.

## Reading this atlas

- Scope: V2's operational message/artifact families, **all 20 kernel command variants**, all four ordinary Raft peer variants, local gateway/replication handoffs, retained-state records, and current versus historical storage. V1 application commands, test-only simulator scheduling objects, language/library objects, and codec error-return wrappers are not V2 trading messages.
- All **canonical** offsets start at zero in the table's explicitly named frame/body. Integers are little-endian. Digests are raw bytes. No native padding is serialized.
- **RAM offsets** in the final appendix are a different coordinate system: measured C++ object layout on this host/compiler with configured cache line64. They are NOT portable wire contracts.
- `N,F,R,T,B,P` are lengths/counts defined locally. Dynamic offsets are expressions rather than made-up fixed numbers.
- “Tag” means an actual magic, schema, kind, or enum discriminator. These are positional encodings, **not TLV**: individual fields generally have no numbered tag.
- “Sender → consumer” names logical ownership. It does not assert a deployed process/IPC route: the tree still has no `v2-node` executable, and several gateway exchanges are local value APIs.
- **Implemented** means present in the current code, not accepted on fresh NUC/Pi gates. **Historical** storage tables describe the removed V2 runtime formats, retained for comparison. Bound local storage is implemented at CP-020.

## 1. Memory budgets and headroom

The first table is **resident object size**, not encoded size. A per-type ceiling is not a complete process-memory budget. Headroom is ceiling minus current size; it is not permission to enlarge a schema.


| C++ object | Current RAM bytes | Object ceiling | Headroom | Owner / use |
|---|---|---|---|---|
| DecisionFrame | 432 | 448 | 16 | Producer → evaluator; 32 inline feature slots |
| PolicyArtifact | 912 | 1024 | 112 | Policy author/importer → reducer; 32 inline rules |
| RuleEvaluation | 8 | 16 | 8 | Rule evaluator → decision logic |
| PriceSource | 32 | 32 | 0 | Policy template → action resolver |
| QuantitySource | 24 | 24 | 0 | Policy template → action resolver |
| ActionSafetyCaps | 72 | 80 | 8 | Policy template → action resolver |
| ActionTemplate | 152 | 160 | 8 | Policy artifact → action resolver |
| EconomicActionProposal | 112 | 128 | 16 | Resolver → retained decision → authorization |
| InstrumentExecutionProfile | 72 | 80 | 8 | Catalog/policy staging → risk/resolver |
| VenueCapabilities | 32 | 32 | 0 | Catalog/policy staging → resolver/gateway |
| ActionResolution | 120 | 128 | 8 | Action resolver → reducer |
| FeatureDescriptor | 16 | 16 | 0 | Feature-schema artifact → resolver |
| FeatureSchemaArtifact | 568 | 576 | 8 | Policy importer → reducer |
| ExecutablePolicyArtifact | 1392 | 1408 | 16 | Policy importer → reducer |
| RiskAccount | 112 | 128 | 16 | Reducer-owned balances/limits |
| RiskReservation | 64 | 64 | 0 | Reducer-owned exposure hold |
| ClusterOrder | 48 | 64 | 16 | Reducer-owned lifecycle record |
| OutboxEffectRecord | 64 | 64 | 0 | Reducer-owned compact effect record |
| DecisionRecordHot | 64 | 64 | 0 | Reducer-owned frequently read decision metadata |
| DecisionRecordCold | 584 | 640 | 56 | Reducer-owned frame/proposal/digest |
| TradingState | 81392 | 81920 | 528 | One reducer state; excludes external contexts, queues and heaps |
| VerifiedExecutionCatalog | 8488 | 16384 | 7896 | Startup owns immutable context; reducer/gateway read |
| StaticIdentity | 80 | 96 | 16 | Bootstrap/recovery input; verifier consumes |
| AuthorityBarrier | 144 | 160 | 16 | Gateway → Raft authority barrier proposer |
| AuthorityProof | 416 | 512 | 96 | Live committed barrier apply → gateway |
| GatewayAuthority | 992 | 1536 | 544 | Gateway owner; pending and accepted one-shot permission |


**Other measured context sizes, without a separately declared object ceiling:** Command=2,120; CommandIdentity=48; ApplyResult=112; RequestReceipt=160; BoundOrderEffect=224; KernelCommandCodecScratch=2,120; StateSnapshotScratch=81,392; ApplyScratch=1 (empty C++ object); VerifiedStaticIdentity=148; VerifiedIdentityStartup=8,672; BoundFrameScratch=65,613. The kernel-command **1,920-byte budget is encoded**, so Command's 2,120-byte native size does not violate it.

ReplicatedKernel's inline sizeof is91,424 bytes at CP-025, **not its total footprint**: separate229400-byte result ledger,66680-byte mailbox owner,64-byte log owner with16777216-byte payload slab and327680-byte descriptors, heap scratch, WAL block storage and library allocations are additional. The24-byte optional result handle is INCLUDED in each40-byte log descriptor, not an extra allocation. Catalog resides in the startup owner, not a second duplicated kernel catalog. Snapshot scratch is a full candidate state because decode publishes transactionally.

### Canonical payload/frame budgets


| Canonical object | Current fixed/max bytes | Reviewed ceiling | Headroom | Scope/status |
|---|---|---|---|---|
| PolicyArtifact payload | 516 = 132 + 32×12 | 576 | 60 | Implemented; no transport header |
| DecisionFrame payload | 421 = 165 + 32×8 | 448 | 27 | Implemented; no transport header |
| FeatureSchemaArtifact payload | 332 = 44 + 32×9 | 384 | 52 | Implemented |
| ExecutablePolicyArtifact payload | 1,278 = 124 + 8×140 + 2 + 32 | 1344 | 66 | Implemented |
| OrderEffect payload | 166 | No separate payload ceiling beyond fixed schema | — | Implemented schema2; native object176 |
| VenueObservation payload | 116 | Fixed schema | — | Implemented schema2; native object128 |
| Kernel command V2KC | 1,789 max | 1920 | 131 | Implemented schema8; includes identity/header/digest |
| Kernel result V2KR | 128 | 128 | 0 | Implemented schema8; native ApplyResult112 |
| Canonical state snapshot | 65,483 max | 65,536 (must be strictly smaller) | 53 | Implemented; 52 bytes of growth would retain strict < |
| Execution catalog | 8,076 max | 8192 | 116 | Implemented; root digest is external, not a trailer |
| Static identity | 116 / 120 / 124 | 124 | 0 | Maximum three-voter frame; two-voter pilot120 |
| Authority barrier V2AB | 186 | Fixed approved frame | — | Implemented; native144 |
| VLT2 envelope payload | Variable ≤65,536 | 65536 | Depends on P | Generic envelope codec; whole frame84+P |
| Bound ordinary peer frame | 29,084 structural max | 32768 | 3684 | Implemented; max16 current canonical entries |
| Bound metadata | 112 | 112 | 0 | Implemented CP-020 local storage |
| Bound WAL record | 1,901 max | 1,901 | 0 | Implemented CP-020;112+canonical entry payload |
| Bound local snapshot | 65,587 max | 65,587 | 0 | Implemented CP-020; reader65,588 including oversize lookahead |


The **88-byte bound wrapper is not deducted from TradingState's 528 bytes**. Each has a separate allocation/contract. Nor can a full 65,587-byte local snapshot fit the generic envelope's 65,536-byte payload; portable/chunked transfer is a separate, unfinished contract.

### Array, arena and queue capacities


| Resource | Capacity / payload footprint | Meaning / status |
|---|---|---|
| Policies | 16 records | Unreclaimed lifetime bound remains a separate availability concern |
| Active + staged executable bundles | 2 × 2,088 = 4,176 RAM bytes | Includes profiles/capabilities and metadata |
| Source cursors / source-event receipts | 16 / 128 | Reducer-resident |
| Request receipts | 256 × 160 = 40,960 RAM bytes | Includes venue-event retention; not an extra independent receipt arena |
| Venue-event receipt quota | 64 | Subset budget within request receipts |
| Decision / reservation / order / outbox groups | 16 each | Concurrent retained groups; not just a transport queue |
| Risk accounts | 4 × 112 = 448 RAM bytes | Account/instrument scope |
| Caller cursors | 8 × 24 = 192 RAM bytes | Acknowledgement/identity retention tracking |
| Gateway delivery attempts | 16 × 24; state400 | Gateway-local, reconstructed after restart |
| Gateway evidence obligations | 64 × 208; state13,320 | Gateway-local; pending canonical identity+observation retained by value |
| Gateway reconciliation attempts | 16 × 32; state520 | Gateway-local query tracking |
| WAL staging blocks | 64 × 4,096 = 262,144 payload bytes | Additional arena metadata/allocations exist |
| WAL submission + completion rings | 64 descriptors each; sizeof ring2,688 at cacheline64 | 40-byte descriptors, no inline 4-KiB block copy |
| Control staging reserve | 4 blocks | Reserve inside64, not additional capacity |
| Disk preallocation increment | 32 × 4,096 = 131,072 bytes | Disk reservation, not RAM headroom |
| Whole-node outbound/result/log budgets | Not yet implemented/approved as a complete budget | Do not infer bounded process memory from the tables above |


Cache-line rounding: DecisionFrame is432 bytes with object ceiling448, while an independently cache-line-rounded slot is448 with cacheline64 and512 with cacheline128. Decision hot/cold sizes64/584 give rounded strides64/640 or128/640. **TradingState currently uses ordinary std::array elements**; its actual offsets in the appendix are authoritative for resident placement—do not multiply hypothetical rounded strides and call that its native size.

Sources: [layout constants](/Users/vishaljha/aeron-raft-pi/include/velocita/v2/layout.hpp), [payload bounds](/Users/vishaljha/aeron-raft-pi/include/velocita/v2/payload.hpp), [snapshot bounds](/Users/vishaljha/aeron-raft-pi/include/velocita/v2/state_snapshot.hpp), [rebuilt layout report source](/Users/vishaljha/aeron-raft-pi/src/v2_layout_report.cpp). RAM values additionally measured with an independently compiled offsetof/sizeof probe.

## 2. Family index: who sends and who consumes


| Family | Tag / schema | Encoded bytes | Producer → consumer | Status |
|---|---|---|---|---|
| Generic application envelope | VLT2 /1; kinds1–15 | 84+P | Client/ingress or result/effect publisher → generic boundary decoder | Codec exists; not a deployed command router |
| Canonical kernel command | V2KC /8; kinds1–20 | 92+B ≤1,789 | Authorized command producer → Raft proposal → reducer on committed apply | Implemented |
| Canonical kernel result | V2KR /8 | 128 | Reducer/replay codec → caller, gateway evidence tracker, tests/audit | Implemented; local CommittedResult adds correlation |
| PolicyArtifact | schema1 | 132+12R | Policy author/importer → policy reducer/evaluator | Implemented |
| FeatureSchemaArtifact | schema1 | 44+9F | Policy importer → staged executable bundle/resolver | Implemented |
| ExecutablePolicyArtifact | schema1 | 126+140T+R | Policy importer → staged executable bundle/resolver | Implemented |
| DecisionFrame | schema2 | 165+8F | Advisory feature producer → evaluation command/reducer | Implemented contract; producer has no trading authority |
| OrderEffect | schema2; operation1–3 | 166 | Committed outbox reconstruction → gateway/encoder | Implemented; not sufficient send authority alone |
| VenueObservation | schema2; kind1–7 | 116 | Venue adapter or gateway reconciliation → ObserveFill/ObserveOrderEvent command | Implemented contract; native vs synthetic identity domains |
| Authority barrier | V2AB /1; Raft entry101 | 186 | Gateway begin_request → leader barrier proposal → all voters | Implemented; replay never reissues grant |
| Authority proof/bound effect | No IPC schema | N/A | Live committed apply → GatewayAuthority → transport boundary | Local owned values only |
| Execution catalog | V2EC /1 | ≤8,076 | Verified configuration import → startup/reducer/gateway | Implemented immutable artifact |
| Static identity | V2ID /1 | 112+4N | Operator/bootstrap → startup verifier | Implemented local artifact |
| Bound ordinary peer | V2BF /1 kind4; inner PER1/1 | 88+inner | Voter → another configured voter | Implemented |
| Hard state / WAL / local snapshot | V2BF kinds1/2/3; MTA1/WAL1/VSN2 historical | See storage section | Persistence owner → startup recovery | Bound formats implemented CP-020; old V2 history refuses |
| Canonical state snapshot | V2ST /8 | 42+payload ≤65,483 | Reducer snapshot encoder → restore/replay verifier | Implemented; local file wrapper is separate |
| WAL block descriptor | No wire schema | 40 RAM bytes | Raft owner → writer; same descriptor returns after sync | Implemented local SPSC value |
| Gateway plans/query views/evidence obligations | No wire schema | Native appendix | Gateway planners/adapter → owner loop/reducer proposal | Implemented local APIs, not venue network encodings |

## 3. Generic application envelope VLT2

Producer → consumer: client/ingress, result or effect publisher → generic envelope decoder. This is a separate namespace from KernelCommandKind and BoundFrameKind. The codec carries opaque payload bytes: its enum alone does not install a command dispatcher or authorize trading. Maximum entire envelope =65,620.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 4 | VLT2 | Magic |
| 4 | 2 | 1 | Envelope version |
| 6 | 2 | 1–15 | MessageKind |
| 8 | 4 | 0 | Flags |
| 12 | 4 | ≤65,536 | Payload length P |
| 16 | 8 | — | CallerId |
| 24 | 8 | — | RequestSequence |
| 32 | 8 | — | CorrelationId |
| 40 | 8 | — | SourceId |
| 48 | 8 | — | SourceGeneration |
| 56 | 8 | — | SourceSequence |
| 64 | 8 | — | Event time ns (signed) |
| 72 | 8 | — | Receive time ns (signed) |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 80 | P | — | Opaque payload |
| 80+P | 4 | CRC32 | Checksum of header+payload |
| Tag at6 | MessageKind | Logical producer → consumer |
|---|---|---|
| 1 | publish_policy | Policy importer → command ingress |
| 2 | activate_policy | Operator/controller → ingress |
| 3 | supersede_policy | Operator/controller → ingress |
| 4 | evaluate_decision_frame | Advisory feature producer → ingress |
| 5 | set_strategy_mode | Operator → ingress |
| 6 | advance_kill_epoch | Operator → ingress |
| 7 | set_executor_generation | Operator → ingress |
| 8 | observe_order_ack | Gateway → evidence ingress |
| 9 | observe_order_reject | Gateway → evidence ingress |
| 10 | observe_fill | Gateway → evidence ingress |
| 11 | observe_cancel | Gateway → evidence ingress |
| 12 | observe_effect_unknown | Gateway → evidence ingress |
| 13 | apply_reconciliation | Gateway → evidence ingress |
| 14 | command_result | Result publisher → caller/gateway |
| 15 | order_effect | Outbox publisher → gateway |

Source: [wire.hpp](/Users/vishaljha/aeron-raft-pi/include/velocita/v2/wire.hpp), [encoder](/Users/vishaljha/aeron-raft-pi/src/v2_wire.cpp).

## 4. Canonical kernel commands — every variant

Producer → consumer: command producers below → leader proposal → follower replicated log → **reducer only after commit**. The sender roles are logical responsibilities, not a claim that transport authentication is implemented.

### Shared V2KC frame (all twenty kinds)

Body starts at **60**. Total=92+B. Length at4 is **48+B**, excluding the schema/kind header and final digest. The 32-byte command-identity payload digest is distinct from the trailing frame digest.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 4 | V2KC | Magic |
| 4 | 4 | 48+B | Identity+body length |
| 8 | 2 | 8 | Kernel codec schema |
| 10 | 2 | 1–20 | KernelCommandKind |
| 12 | 8 | — | CallerId |
| 20 | 8 | — | RequestSequence |
| 28 | 32 | — | CommandIdentity payload_digest |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 60 | B | Kind-dependent | Command body |
| 60+B | 32 | SHA-256 | Digest of bytes8 through end of body |


| Kind tag | Command | Body bytes B | Total fixed/max | Logical sender → consumer |
|---|---|---|---|---|
| 1 | PublishPolicy | 132+12R | 608 | Policy importer → replicated reducer |
| 2 | ValidatePolicy | 16 | 108 | Policy controller → replicated reducer |
| 3 | ActivatePolicy | 16 | 108 | Policy controller → replicated reducer |
| 4 | SupersedePolicy | 16 | 108 | Policy controller → replicated reducer |
| 5 | SetStrategyMode | 17 | 109 | Operator → replicated reducer |
| 6 | AdvanceKillEpoch | 8 | 100 | Operator → replicated reducer |
| 7 | SetExecutorGeneration | 8 | 100 | Operator → replicated reducer |
| 8 | RecordSourceEvent | 56 | 148 | Source-ingress producer → replicated reducer |
| 9 | EvaluateDecisionFrame | 174+8F | 522 | Advisory feature producer/controller → replicated reducer |
| 10 | StageExecutablePolicy | Fschema+Executable+87 | 1789 | Policy/configuration importer → replicated reducer |
| 11 | ActivateExecutablePolicy | 48 | 140 | Policy controller → replicated reducer |
| 12 | AuthorizeDecision | 8 | 100 | Execution controller → replicated reducer |
| 13 | ObserveFill | 116 | 208 | Gateway → replicated reducer |
| 14 | RequestCancel | 8 | 100 | Execution controller/operator → replicated reducer |
| 15 | ObserveOrderEvent | 116 | 208 | Gateway → replicated reducer |
| 16 | RequestReplace | 16 | 108 | Execution controller → replicated reducer |
| 17 | ConfigureRiskLimits | 88 | 180 | Operator → replicated reducer |
| 18 | AcknowledgeGatewayResults | 8 | 100 | Gateway → replicated reducer |
| 19 | RequestMassCancel | 33 | 125 | Operator → replicated reducer |
| 20 | InstallExecutionCatalog | 48 | 140 | Operator/configuration importer → replicated reducer |


### 4.1. PublishPolicy — tag 1

Offsets below are **relative to its body at frame60**. Policy importer → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 132+12R | PolicyArtifact schema1 | Entire nested policy payload; see §5 |


### 4.2. ValidatePolicy — tag 2

Offsets below are **relative to its body at frame60**. Policy controller → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | PolicyId |
| 8 | 8 | — | PolicyVersion |


### 4.3. ActivatePolicy — tag 3

Offsets below are **relative to its body at frame60**. Policy controller → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | PolicyId |
| 8 | 8 | — | PolicyVersion |


### 4.4. SupersedePolicy — tag 4

Offsets below are **relative to its body at frame60**. Policy controller → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | PolicyId |
| 8 | 8 | — | PolicyVersion |


### 4.5. SetStrategyMode — tag 5

Offsets below are **relative to its body at frame60**. Operator → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 1 | 1 normal;2 no_new_risk;3 cancel_only;4 reduce_only;5 halted | StrategyMode |
| 1 | 8 | — | Expected KillEpoch |
| 9 | 8 | — | Expected ExecutorGeneration |


### 4.6. AdvanceKillEpoch — tag 6

Offsets below are **relative to its body at frame60**. Operator → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | New KillEpoch |


### 4.7. SetExecutorGeneration — tag 7

Offsets below are **relative to its body at frame60**. Operator → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | New ExecutorGeneration |


### 4.8. RecordSourceEvent — tag 8

Offsets below are **relative to its body at frame60**. Source-ingress producer → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | SourceId |
| 8 | 8 | — | SourceGeneration |
| 16 | 8 | — | SourceSequence |
| 24 | 32 | — | Event digest |


### 4.9. EvaluateDecisionFrame — tag 9

Offsets below are **relative to its body at frame60**. Advisory feature producer/controller → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | DecisionId |
| 8 | 1 | 1 authoritative;2 shadow | EvaluationMode |

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 9 | 165+8F | DecisionFrame schema2 | Entire nested decision frame; see §5 |


### 4.10. StageExecutablePolicy — tag 10

Offsets below are **relative to its body at frame60**. Policy/configuration importer → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | S=44+9F | schema1 | FeatureSchemaArtifact |
| S | E=126+140T+R | schema1 | ExecutablePolicyArtifact |
| S+E | 66 | schema1 | InstrumentExecutionProfile |
| S+E+66 | 21 | schema1 | VenueCapabilities |


### 4.11. ActivateExecutablePolicy — tag 11

Offsets below are **relative to its body at frame60**. Policy controller → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | PolicyId |
| 8 | 8 | — | PolicyVersion |
| 16 | 32 | — | Executable policy digest |


### 4.12. AuthorizeDecision — tag 12

Offsets below are **relative to its body at frame60**. Execution controller → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | DecisionId |


### 4.13. ObserveFill — tag 13

Offsets below are **relative to its body at frame60**. Gateway → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 116 | VenueObservation schema2 | kind3 fill; entire nested observation |


### 4.14. RequestCancel — tag 14

Offsets below are **relative to its body at frame60**. Execution controller/operator → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | ClusterOrderId |


### 4.15. ObserveOrderEvent — tag 15

Offsets below are **relative to its body at frame60**. Gateway → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 116 | VenueObservation schema2 | Non-fill venue/gateway evidence kind; entire nested observation |


### 4.16. RequestReplace — tag 16

Offsets below are **relative to its body at frame60**. Execution controller → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | ClusterOrderId |
| 8 | 8 | — | Replacement DecisionId |


### 4.17. ConfigureRiskLimits — tag 17

Offsets below are **relative to its body at frame60**. Operator → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | AccountId |
| 8 | 8 | — | InstrumentId |
| 16 | 8 | — | RiskPolicyVersion |
| 24 | 8 | — | Expected RiskVersion |
| 32 | 8 | — | Expected KillEpoch |
| 40 | 8 | — | Expected ExecutorGeneration |
| 48 | 8 | — | Max order quantity (lots, i64) |
| 56 | 8 | — | Max reserved quantity (lots, i64) |
| 64 | 8 | — | Max order gross notional (micros, i64) |
| 72 | 8 | — | Max reserved exposure: margin+fee budget (micros, i64) |
| 80 | 4 | — | Initial margin ppm (u32) |
| 84 | 4 | — | Maximum fee ppm (u32) |


### 4.18. AcknowledgeGatewayResults — tag 18

Offsets below are **relative to its body at frame60**. Gateway → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | Acknowledged-through RequestSequence |


### 4.19. RequestMassCancel — tag 19

Offsets below are **relative to its body at frame60**. Operator → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 1 | 1 all;2 account;3 account+instrument | Scope |
| 1 | 8 | — | AccountId (scope-dependent) |
| 9 | 8 | — | InstrumentId (scope-dependent) |
| 17 | 8 | — | Expected KillEpoch |
| 25 | 8 | — | Expected ExecutorGeneration |


### 4.20. InstallExecutionCatalog — tag 20

Offsets below are **relative to its body at frame60**. Operator/configuration importer → replicated reducer.


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 32 | — | Catalog root |
| 32 | 8 | — | Expected KillEpoch |
| 40 | 8 | — | Expected ExecutorGeneration |


No separate command body exists for each ack/reject/cancel/reconciliation event: these use kind15 plus VenueObservation.kind. Serial replacement is kind16; it does not enable overlapping/native amend. Sources: [command kind/bounds](/Users/vishaljha/aeron-raft-pi/include/velocita/v2/kernel_codec.hpp), [actual write_body](/Users/vishaljha/aeron-raft-pi/src/v2_kernel_codec.cpp:690).

## 5. Economic/policy payloads and nested components

Offsets are relative to **the payload/component start**, not its transport or V2KC wrapper. Source: [v2_payload.cpp](/Users/vishaljha/aeron-raft-pi/src/v2_payload.cpp), mirrored allocation-free writers in [v2_kernel_codec.cpp](/Users/vishaljha/aeron-raft-pi/src/v2_kernel_codec.cpp).

### 5.1 PolicyArtifact — schema1; 132+12R bytes; R≤32

Policy author/importer → PublishPolicy → reducer/policy evaluator.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 2 | 1 | Schema |
| 2 | 8 | — | PolicyId |
| 10 | 8 | — | PolicyVersion |
| 18 | 8 | — | FeatureSchemaId |
| 26 | 8 | — | InstrumentId |
| 34 | 8 | — | AccountId |
| 42 | 32 | — | Model digest |
| 74 | 32 | — | Artifact digest |
| 106 | 8 | — | Valid-from event time ns (i64) |
| 114 | 8 | — | Valid-until event time ns (i64) |
| 122 | 8 | — | RiskPolicyVersion |
| 130 | 2 | 1–32 | Rule count R (u16) |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 132+12i | 2 | — | Rule i: feature_index |
| 134+12i | 1 | 1 <;2 ≤;3 =;4 ≥;5 > | Rule predicate |
| 135+12i | 1 | 1 no_action;2 place_limit;3 cancel;4 replace | Rule directive |
| 136+12i | 8 | i64 | Threshold feature atoms |


### 5.2 DecisionFrame — schema2; 165+8F bytes; F≤32

Advisory feature producer → EvaluateDecisionFrame → deterministic reducer/evaluator. Versions bind the view used for the decision; the producer does not reserve risk or submit orders.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 2 | 2 | Schema |
| 2 | 8 | — | PolicyId |
| 10 | 8 | — | PolicyVersion |
| 18 | 8 | — | FeatureSchemaId |
| 26 | 8 | — | VenueId |
| 34 | 8 | — | VenueSessionId |
| 42 | 8 | — | VenueSequence |
| 50 | 8 | — | SourceId |
| 58 | 8 | — | SourceGeneration |
| 66 | 8 | — | SourceSequence |
| 74 | 8 | — | Event time ns (i64) |
| 82 | 8 | — | Receive time ns (i64) |
| 90 | 8 | — | InstrumentId |
| 98 | 8 | — | AccountId |
| 106 | 2 | 1–32 | Feature count F |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 108 | 8F | i64 each | Feature values |
| 108+8F | 1 | 1 ready;2 stale;3 recovering;4 crossed;5 incomplete | MarketDataStatus |
| 109+8F | 8 | i64 | Market-data age ns |
| 117+8F | 8 | — | PositionVersion |
| 125+8F | 8 | — | RiskVersion |
| 133+8F | 32 | — | Raw evidence digest |


### 5.3 OrderEffect — schema2; 166 bytes

Committed outbox reconstruction → gateway candidate/authority checks → transport boundary. This is a reconstructable full payload; the compact64-byte outbox record is NOT this object. No swap/market-order schema is silently added.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 2 | 2 | Schema |
| 2 | 8 | — | EffectId |
| 10 | 8 | — | ClusterOrderId |
| 18 | 8 | — | Committed directive index (reducer intent linkage, not a fresh quorum proof) |
| 26 | 8 | — | ExecutorGeneration |
| 34 | 8 | — | KillEpoch |
| 42 | 8 | — | VenueId |
| 50 | 8 | — | AccountId |
| 58 | 8 | — | InstrumentId |
| 66 | 1 | 1 submit;2 cancel;3 replace | EffectOperation |
| 67 | 1 | 1 buy;2 sell | Side |
| 68 | 8 | — | Price ticks (i64) |
| 76 | 8 | — | Quantity lots (i64) |
| 84 | 1 | 1 GTC;2 IOC;3 FOK;4 legacy post_only enum | TimeInForce |
| 85 | 8 | — | PolicyId |
| 93 | 8 | — | PolicyVersion |
| 101 | 8 | — | DecisionId |
| 109 | 8 | — | RiskVersion |
| 117 | 8 | — | SourceGeneration |
| 125 | 8 | — | SourceSequence |
| 133 | 32 | Economic fingerprint, not SHA of this complete frame | Payload fingerprint |
| 165 | 1 | 1 prohibited;2 required | PostOnlyMode |

Cancel payload price and quantity are canonical zero; identity targets the existing order. The post-only byte is serialized LAST despite its earlier position in the native struct. An operation enum value does not mean every operation is enabled in the MVP gateway.


### 5.4 VenueObservation — schema2; 116 bytes

Native venue events or gateway-produced reconciliation/timeout evidence → canonical observation command → reducer. Kernel recomputes the evidence fingerprint; producer-supplied digest is not blindly trusted.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 2 | 2 | Schema |
| 2 | 1 | 1–7, table below | VenueObservationKind |
| 3 | 8 | — | VenueId |
| 11 | 8 | — | VenueSessionId |
| 19 | 8 | — | VenueSequence |
| 27 | 8 | — | ClusterOrderId |
| 35 | 8 | — | EffectId |
| 43 | 8 | — | VenueOrderId |
| 51 | 8 | — | ExecutionId |
| 59 | 1 | 1 buy;2 sell where meaningful | Side |
| 60 | 8 | — | Price ticks (i64) |
| 68 | 8 | — | Quantity lots (i64) |
| 76 | 8 | — | Fee micros (i64) |
| 84 | 32 | — | Evidence fingerprint |
| Kind tag at2 | Meaning | Identity origin | Quantity interpretation / consumer |
|---|---|---|---|
| 1 | order_acknowledged | Native venue | ObserveOrderEvent; acknowledgement evidence |
| 2 | order_rejected | Native venue | ObserveOrderEvent; rejection evidence |
| 3 | fill | Native venue | ObserveFill; incremental executed quantity |
| 4 | cancelled | Native venue | ObserveOrderEvent; cancellation evidence |
| 5 | effect_unknown | Synthetic gateway | ObserveOrderEvent; ambiguity, not rejection |
| 6 | cancel_reconciled | Synthetic gateway | Final cumulative filled quantity; price/fee/execution identity zero |
| 7 | order_reconciled | Synthetic gateway | Cumulative query evidence; absent order requires zero quantity; price/fee/execution identity zero |

There is **no separate origin byte**: origin derives from kind. The gateway must retain the same command identity across retries; a query response is not permission to invent an unrelated fill. Session/sequence and venue/execution identifiers serve different deduplication scopes.


### 5.5 FeatureSchemaArtifact — schema1; 44+9F bytes

Policy importer → StageExecutablePolicy → resolver's interpretation of features.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 2 | 1 | Schema |
| 2 | 8 | — | FeatureSchemaId |
| 10 | 32 | — | Artifact digest |
| 42 | 2 | 1–32 | Feature count F |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 44+9i | 1 | 1 generic;2 price;3 quantity | Feature i economic kind |
| 45+9i | 8 | i64 | Feature atoms per output unit |


### 5.6 ExecutablePolicyArtifact — schema1; 126+140T+R bytes

Policy importer → StageExecutablePolicy → action resolver. T≤8 templates, R≤32 rule bindings.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 2 | 1 | Schema |
| 2 | 8 | — | PolicyId |
| 10 | 8 | — | PolicyVersion |
| 18 | 32 | — | Policy artifact digest |
| 50 | 8 | — | FeatureSchemaId |
| 58 | 32 | — | Feature schema digest |
| 90 | 32 | — | Executable artifact digest |
| 122 | 2 | ≤8 | Template count T |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 124 | 140T | ActionTemplate schema1 | Templates in order |
| 124+140T | 2 | ≤32 | Rule-binding count R |
| 126+140T | R | u8 per slot | Rule-to-template slots; zero means no template, positive one-based template slot |


### 5.7 ActionTemplate — schema1; 140 bytes

Nested executable policy component → action resolver; not a separate transport message.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 2 | 1 | Schema |
| 2 | 1 | 1 no_action;2 place_limit;3 cancel;4 replace | Directive |
| 3 | 1 | 1 buy;2 sell | Side |
| 4 | 1 | GTC/IOC/FOK/post_only enum | TimeInForce |
| 5 | 1 | 1 prohibited;2 required | PostOnlyMode |
| 6 | 2 | — | LogicalOrderSlot |
| 8 | 8 | — | InstrumentSpecVersion |
| 16 | 8 | — | VenueCapabilitiesVersion |
| 24 | 28 | — | PriceSource |
| 52 | 20 | — | QuantitySource |
| 72 | 68 | — | ActionSafetyCaps |


### 5.8 PriceSource — 28 bytes

Nested template/reference-price component → action resolver. Inactive fields must be canonical; the source kind selects fixed versus feature-derived interpretation.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 1 | 0 none;1 fixed;2 feature | EconomicValueSourceKind |
| 1 | 2 | — | Feature index |
| 3 | 1 | Explicit fixed-point enum; see domain.hpp | RoundingMode |
| 4 | 8 | — | Fixed price ticks (i64) |
| 12 | 8 | — | Feature atoms per tick (i64) |
| 20 | 8 | — | Tick offset (i64) |

### 5.9 QuantitySource — 20 bytes

Nested template component → action resolver.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 1 | 0 none;1 fixed;2 feature | EconomicValueSourceKind |
| 1 | 2 | — | Feature index |
| 3 | 1 | Explicit fixed-point enum | RoundingMode |
| 4 | 8 | — | Fixed quantity lots (i64) |
| 12 | 8 | — | Feature atoms per lot (i64) |

### 5.10 ActionSafetyCaps — 68 bytes

Nested template component → action resolver; bounds proposals before authorization.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | Maximum quantity lots |
| 8 | 8 | — | Maximum notional micros |
| 16 | 8 | — | Maximum absolute price deviation ticks |
| 24 | 8 | — | Maximum absolute tick offset |
| 32 | 8 | — | Maximum market-data age ns |
| 40 | 28 | — | Reference PriceSource |


### 5.11 InstrumentExecutionProfile — schema1; 66 bytes

Catalog/configuration importer → staged policy, risk calculations and gateway binding.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 2 | 1 | Schema |
| 2 | 8 | — | VenueId |
| 10 | 8 | — | InstrumentId |
| 18 | 8 | — | InstrumentSpecVersion |
| 26 | 8 | — | Quote micros per tick×lot |
| 34 | 8 | — | Minimum price ticks |
| 42 | 8 | — | Maximum price ticks |
| 50 | 8 | — | Minimum quantity lots |
| 58 | 8 | — | Maximum quantity lots |

### 5.12 VenueCapabilities — schema1; 21 bytes

Catalog/configuration importer → resolver and gateway exact-intent checks.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 2 | 1 | Schema |
| 2 | 8 | — | VenueId |
| 10 | 8 | — | VenueCapabilitiesVersion |
| 18 | 1 | bits0/1/2: place_limit/cancel/replace | Directive mask |
| 19 | 1 | bits0/1/2: GTC/IOC/FOK | Time-in-force mask |
| 20 | 1 | 0/1 | Supports post-only |

## 6. Kernel result V2KR — 128 bytes, schema8

Reducer/replay → result codec → caller or gateway evidence tracker. Standalone V2KR does **not** carry caller/request identity; local CommittedResult wraps it with identity, term and log index. Future external correlation must preserve that distinction.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 4 | V2KR | Magic |
| 4 | 4 | 86 | Result payload length |
| 8 | 2 | 8 | Kernel result schema |
| 10 | 1 | ApplyCode enum | ApplyCode |
| 11 | 1 | ContractError enum | ContractError |
| 12 | 8 | Interpret through detail_kind | Metadata word0 |
| 20 | 8 | Interpret through detail_kind | Metadata word1 |
| 28 | 1 | 0 none;1 proposed;2 validated;3 active;4 superseded | PolicyLifecycle |
| 29 | 8 | — | StateVersion |
| 37 | 1 | 1–5 | StrategyMode |
| 38 | 8 | — | KillEpoch |
| 46 | 8 | — | ExecutorGeneration |
| 54 | 1 | 0–8 | ApplyResultDetailKind |
| 55 | 8 | — | Detail word0 |
| 63 | 8 | — | Detail word1 |
| 71 | 8 | — | Detail word2 |
| 79 | 8 | — | DecisionId |
| 87 | 1 | DecisionCode enum | DecisionCode |
| 88 | 1 | 1–4 | Proposed Directive |
| 89 | 1 | 1–4 | Effective Directive |
| 90 | 1 | 1 authoritative;2 shadow | EvaluationMode |
| 91 | 2 | u16; no-match sentinel65535 | Matched rule index |
| 93 | 2 | — | Rules visited |
| 95 | 1 | ActionResolutionCode enum | ActionResolutionCode |
| 96 | 32 | SHA-256 of bytes8–95 | Frame digest |

### Tagged result overlays

The three detail words share bytes; these are alternatives, not additional fields.

| detail_kind tag | Detail word0 | Detail word1 | Detail word2 | Metadata words0/1 |
|---|---|---|---|---|
| 0 | none | none | none | PolicyId/PolicyVersion where relevant |
| 1 | SourceId | SourceGeneration | SourceSequence | Policy identity where relevant |
| 2 | ClusterOrderId | EffectId | Reserved exposure micros | Policy identity where relevant |
| 3 | VenueId | ExecutionId | Filled quantity lots | VenueSessionId / VenueSequence |
| 4 | VenueId | VenueSessionId | VenueSequence | EffectId / ObservationOrigin (native1,synthetic2) |
| 5 | AccountId | InstrumentId | Resulting RiskVersion | No added risk balances |
| 6 | In-scope count | Cancelled count | Skipped count | No added per-order array |
| 7 | Account RiskVersion | Account PositionVersion | Position quantity lots | `stale_risk_state` only |
| 8 | SourceId | Cursor SourceGeneration | Cursor SourceSequence (both 0: no cursor) | `source_generation_gap` only |

Source: [write_result_payload](/Users/vishaljha/aeron-raft-pi/src/v2_kernel_codec.cpp:1109), [ApplyResult/accessors](/Users/vishaljha/aeron-raft-pi/include/velocita/v2/kernel.hpp:454).

## 7. Execution catalog V2EC — schema1

Configuration importer → verified startup context → reducer/resolver/gateway. At most16 rows. Entire artifact≤8,076 under8,192-byte ceiling. The SHA-256 root is computed externally with its domain separator; **no trailing root field** appears in the file.

### Header
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 4 | V2EC | Magic |
| 4 | 2 | 1 | Catalog schema |
| 6 | 2 | 1–16 | Row count |
| 8 | 4 | Exact full length | Total artifact length |

### Each row (offsets relative to row start)

Rows sorted by (account,venue,instrument,spec version,capability version).

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | AccountId |
| 8 | 66 | schema1; §5.11 | InstrumentExecutionProfile |
| 74 | 21 | schema1; §5.12 | VenueCapabilities |
| 95 | 8 | — | Tick quote micros |
| 103 | 8 | — | Lot base atoms |
| 111 | 1 | 0–18 | Base decimals |
| 112 | 2 | Nonzero; not a venue capability by itself | Encoder family ID |
| 114 | 2 | Nonzero | Encoder schema version |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 116 | 1 | n₁=1..96 | Network identity byte length |
| 117 | n₁ | Printable ASCII | Network identity |
| 117+n₁ | 1 | n₂=1..96 | Base-asset identity byte length |
| 118+n₁ | n₂ | Printable ASCII | Base-asset identity |
| 118+n₁+n₂ | 1 | n₃=1..96 | Quote-asset identity byte length |
| 119+n₁+n₂ | n₃ | Printable ASCII | Quote-asset identity |
| 119+n₁+n₂+n₃ | 1 | n₄=1..96 | Routing-key byte length |
| 120+n₁+n₂+n₃ | n₄ | Printable ASCII | Venue routing key |

Row total=120+n₁+n₂+n₃+n₄≤504. Exact immutable precision/asset/network facts are retained; identities/encoder IDs must not contain credentials. Catalog verification proves bytes, not actual venue support. Source: [catalog codec](/Users/vishaljha/aeron-raft-pi/src/v2_execution_catalog.cpp), [approved sheet](/Users/vishaljha/aeron-raft-pi/docs/F3_CATALOG_V1_BYTE_REVIEW.md).

## 8. Authority barrier and local proof

### 8.1 Durable barrier V2AB — 186 bytes

Gateway begin_request → leader → ordinary Raft replication as **entry_type101**, not a kernel command kind. Native barrier144 bytes; canonical frame186.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 4 | V2AB | Magic |
| 4 | 4 | 144 | Body length |
| 8 | 2 | 1 | Marker schema |
| 10 | 16 | — | Gateway process incarnation |
| 26 | 8 | — | Request sequence |
| 34 | 8 | — | EffectId |
| 42 | 32 | — | Expected OrderEffect payload fingerprint |
| 74 | 8 | — | Expected KillEpoch |
| 82 | 8 | — | Expected ExecutorGeneration |
| 90 | 32 | — | Execution catalog root |
| 122 | 32 | — | Derived authority domain |
| 154 | 32 | SHA-256 over bytes8–153 | Trailing digest |

### 8.2 Proof/grant is local, not persisted permission

Committed **live** barrier apply → AuthorityProof → GatewayAuthority.accept → one-shot consume → transport handoff. Replay validates historical barriers but never restores a usable grant.

| Local type | Wire offset | Encoded bytes | Tag | Native bytes | Components / meaning |
|---|---|---|---|---|---|
| BoundOrderEffect | Not defined | No approved IPC schema | — | 224 | OrderEffect + catalog root + exact spec/capability versions |
| AuthorityProofView / AuthorityProof | Not defined | No approved IPC schema | — | 416 | Barrier request + BoundOrderEffect + issuer/term/barrier index/applied index/state version/mode |
| GatewayAuthority | Not defined | Never a wire dump | — | 992 | Private pending/accepted slots, clocks, high-water marks and stop flags; budget1,536 |

All field-by-field **native** offsets for public proof/binding values are in §13. A bool labelled authoritative is not a quorum proof. Source: [barrier codec](/Users/vishaljha/aeron-raft-pi/src/v2_authority_barrier.cpp), [proof](/Users/vishaljha/aeron-raft-pi/include/velocita/v2/authority_proof.hpp), [gateway authority](/Users/vishaljha/aeron-raft-pi/include/velocita/v2/gateway_authority.hpp).

## 9. Static identity V2ID — 112+4N bytes

Operator/bootstrap → owned startup verifier. N=1..3 static voters; the two-voter pilot is120 bytes. The complete file is local-node-bound; manifest_digest intentionally excludes the local ID.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 4 | V2ID | Magic |
| 4 | 4 | 112+4N | Total file length |
| 8 | 2 | 1 | Identity frame schema |
| 10 | 2 | 0 | Reserved |
| 12 | 16 | — | Shared deployment incarnation |
| 28 | 32 | — | Intended immutable catalog root |
| 60 | 2 | 8 | State snapshot schema |
| 62 | 2 | 8 | Command/result schema |
| 64 | 2 | 1 | Authority marker schema |
| 66 | 2 | 1 | Execution catalog schema |
| 68 | 2 | 1 | Replication semantics epoch |
| 70 | 2 | 1 | Storage lineage epoch |
| 72 | 1 | 1 | Quorum durability mode |
| 73 | 1 | 1 laboratory;2 two-host pilot | Deployment mode |
| 74 | 2 | 1..3 | Voter count N |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 76 | 4N | Positive i32, strictly sorted | Shared voter IDs |
| 76+4N | 4 | Must be a configured voter | Local node ID |
| 80+4N | 32 | SHA-256 | Digest of all preceding file bytes |

Shared manifest and authority domain are derived with different domain separators. Stored identity is not evidence that a missing term/vote can safely be recreated. Implemented CP-020 bootstrap publishes bound zero hard state first and identity last; recovery requires both final files. Source: [identity codec](/Users/vishaljha/aeron-raft-pi/src/v2_static_identity.cpp), [approved framing/bootstrap](/Users/vishaljha/aeron-raft-pi/docs/F5_BOUND_FRAME_BYTE_REVIEW.md).

## 10. Bound framing and all Raft peer types

### 10.1 V2BF common frame — 88 bytes overhead

For ordinary peers: voter → another voter. For kinds1–3: future local persistence writer → local recovery verifier. Only common framing and typed peer integration are implemented so far.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 4 | V2BF | Magic |
| 4 | 2 | 1 | Bound-frame schema |
| 6 | 2 | 1 metadata;2 WAL record;3 snapshot;4 peer | BoundFrameKind |
| 8 | 4 | 88+P | Total frame length |
| 12 | 4 | Kind-specific maximum | Payload length P |
| 16 | 32 | — | Shared manifest digest |
| 48 | 4 | Positive local ID for1–3; exactly0 for4 | Owner local voter (i32) |
| 52 | 4 | 0 | Reserved |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 56 | P | Kind-specific | Payload |
| 56+P | 32 | SHA-256 | Hash of 26-byte ASCII velocita.v2.bound-frame.v1 + bytes[0,56+P) |

Hashing detects integrity/lineage equality, not malicious sender authentication.

### 10.2 Common inner PER1 peer header

Offsets here start at the **inner peer body**. Add56 for absolute offsets inside V2BF. Inner bytes must not enter Raft through a raw public bypass.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 4 | PER1 | Magic |
| 4 | 2 | 3 in V2 bound bodies (1 in V1) | Inner wire version; any other value is refused |
| 6 | 2 | 1 request_vote;2 vote_response;3 append_entries;4 append_response | PeerMessageType |
| 8 | 4 | — | Sender node ID (i32) |
| 12 | 4 | — | Recipient node ID (i32) |

### 10.3 RequestVote — inner49; complete bound137

Candidate → configured voter. Candidate ID must equal sender.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 16 | 8 | — | Term (i64) |
| 24 | 4 | — | Candidate ID (i32) |
| 28 | 4 | 0 | Reserved |
| 32 | 8 | — | Last log index (i64) |
| 40 | 8 | — | Last log term (i64) |
| 48 | 1 | Canonical0/1 | Pre-vote: term is the proposed term; no state changes at the receiver |

### 10.4 RequestVoteResponse — inner26; complete bound114

Voter → candidate; replies need the same lineage verification as requests.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 16 | 8 | — | Term (i64) |
| 24 | 1 | Canonical0/1 | Vote granted |
| 25 | 1 | Canonical0/1 | Pre-vote (echoes the request) |

### 10.5 AppendEntries — inner52+Σ(20+Lᵢ); bound140+Σ(20+Lᵢ)

Leader → follower. Empty heartbeat140 bytes. At16 maximum1,789-byte payloads, complete frame29,084 bytes≤32,768.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 16 | 8 | — | Term (i64) |
| 24 | 8 | — | Previous log index (i64) |
| 32 | 8 | — | Previous log term (i64) |
| 40 | 8 | — | Leader commit index (i64) |
| 48 | 4 | 0..16 | Entry count N (u32) |

Each entry begins at Eᵢ=52+Σⱼ<ᵢ(20+Lⱼ), relative to inner PER1 start:

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| Eᵢ | 8 | i64 | Entry term |
| Eᵢ+8 | 4 | i32 | Entry ID hint |
| Eᵢ+12 | 4 | 0 normal;101 authority barrier | Raft entry type |
| Eᵢ+16 | 4 | Lᵢ≤1,789 | Canonical payload length |
| Eᵢ+20 | Lᵢ | V2KC or V2AB | Typed canonical entry payload |

### 10.6 AppendEntriesResponse — inner41; complete bound129

Follower → leader. Successful reported current_index is clamped to local durability before emission.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 16 | 8 | — | Term (i64) |
| 24 | 1 | Canonical0/1 | Success |
| 25 | 8 | — | Current index (i64) |
| 33 | 8 | — | First index (i64) |

Bounds are checked before allocation and mutable Raft callbacks. They limit each message, not the total queue or retained log. Sources: [bound frame](/Users/vishaljha/aeron-raft-pi/src/v2_bound_frame.cpp), [typed peer](/Users/vishaljha/aeron-raft-pi/src/v2_bound_peer.cpp), [inner encoder](/Users/vishaljha/aeron-raft-pi/src/wire.cpp:186).

## 11. Storage frames — historical versus current

Persistence owner → durable files → startup recovery. **Do not mistake approval for implementation.** Current kernels still write the old local formats below; the bound variants are the next approved integration. The common framing codec alone does not make those stores identity-bound.

### 11.1 Historical hard state MTA1 — 36 bytes
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 4 | MTA1 | Magic |
| 4 | 2 | 1 | Storage version |
| 6 | 2 | 0 | Reserved |
| 8 | 8 | — | Current term (i64) |
| 16 | 4 | — | Voted-for node (i32) |
| 20 | 4 | 0 | Reserved |
| 24 | 8 | — | Commit index (i64) |
| 32 | 4 | CRC32 | Checksum of preceding32 bytes |

### 11.2 Current bound hard state — V2BF kind1; 112 bytes

Body offsets relative to outer frame56. Identity/owner come from the common wrapper.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | Current term (i64, nonnegative) |
| 8 | 4 | -1 or configured positive voter | Voted-for node (i32) |
| 12 | 4 | 0 | Reserved |
| 16 | 8 | — | Commit index (i64, nonnegative) |

Term0 requires voted_for=-1 and commit0. Bootstrap publication changes to metadata-FIRST/identity-LAST. Missing metadata in recovery is not permission to recreate a fresh tuple.

### 11.3 Historical WAL record WAL1 — 44+L bytes

Raft log staging → block writer; recovery → typed log replay. In V2, normal payload max1,789; authority marker186. The shared generic storage layer has a broader1-MiB payload limit, which is not the V2 command budget.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 4 | WAL1 | Magic |
| 4 | 2 | 1 | Storage version |
| 6 | 2 | 44 | Header length |
| 8 | 4 | — | Payload length L |
| 12 | 4 | 0 | Reserved |
| 16 | 8 | — | Log index (u64) |
| 24 | 8 | — | Term (i64) |
| 32 | 4 | — | Entry ID (i32) |
| 36 | 4 | 0 normal;101 authority marker | Entry type (i32) |
| 40 | 4 | CRC32 of header+payload with this field zeroed | Checksum |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 44 | L | V2KC / V2AB | Canonical entry bytes |

### 11.4 Current bound WAL record — V2BF kind2; 112+L bytes

Body offsets relative to outer frame56. Max1,901 bytes. No implicit flat-WAL adoption/fallback in the new bound lineage.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | Log index (u64, positive) |
| 8 | 8 | — | Term (i64, positive) |
| 16 | 4 | — | Entry ID hint (i32) |
| 20 | 4 | 0 normal;101 authority marker | Entry type (i32) |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 24 | L≤1,789 | V2KC / V2AB | Unchanged canonical command or marker; typed codec/domain must validate |

### 11.5 WAL block — 4,096 bytes

Historical version1 holds old WAL1 records. Current version2 holds only bound V2BF records; **same magic, explicit version change**. No record straddles blocks.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 4 | VWB2 | Magic |
| 4 | 2 | 1 historical;2 current | Block version |
| 6 | 2 | 0 | Reserved |
| 8 | 8 | — | Arena generation |
| 16 | 8 | — | Block sequence |
| 24 | 4 | ≤4,064 | Used payload length U |
| 28 | 4 | CRC32 of zeroed-checksum header + used payload | Checksum |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 32 | U | Records | Exactly consumed complete records |
| 32+U | 4,064−U | 0 | Padding excluded from checksum |

Two maximum new records use3,802 of4,064 payload bytes, leaving262 bytes. All-zero preallocation blocks are not valid historical frames. Version mismatch must not be treated as a torn tail in current recovery.

### 11.6 Historical local snapshot wrapper — 38+S bytes

SnapshotStore → local file → SnapshotStore.load. S is the canonical V2ST snapshot length; current maximum file65,521 bytes.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 4 | VSN2 | Magic |
| 4 | 2 | 1 | Wrapper version |
| 6 | 8 | — | Last included log index |
| 14 | 8 | — | Last included term |
| 22 | 8 | — | Canonical snapshot length S |
| 30 | 4 | 0 | Reserved |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 34 | S | V2ST /8 | Canonical state snapshot |
| 34+S | 4 | CRC32 | Checksum of wrapper header and payload |

### 11.7 Current bound local snapshot — V2BF kind3; 104+S bytes

Body offsets relative to outer frame56; maximum65,587 bytes, bounded reader65,588 with one lookahead byte.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | Last included index (u64, positive) |
| 8 | 8 | — | Last included term (i64, positive) |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 16 | S≤65,483 | V2ST /8 | Canonical state snapshot, unchanged |

A local wrapper includes local owner identity; it is NOT the portable snapshot-transfer contract. Peer install/archive generation/chunk bytes remain undefined. Sources: [storage.cpp](/Users/vishaljha/aeron-raft-pi/src/storage.cpp), [WAL blocks](/Users/vishaljha/aeron-raft-pi/src/v2_wal_arena.cpp), [snapshot store](/Users/vishaljha/aeron-raft-pi/src/v2_snapshot_store.cpp), [approved byte sheet](/Users/vishaljha/aeron-raft-pi/docs/F5_BOUND_FRAME_BYTE_REVIEW.md).

## 12. Canonical state snapshot V2ST — schema8; max65,483

Reducer snapshot encoder → recovery/replay decoder. Snapshots are state artifacts, not gateway send authority. Layout is variable-length because only retained counts and used features/rules are serialized.

### 12.1 Outer canonical frame
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 4 | V2ST | Magic |
| 4 | 4 | — | State payload length P |
| 8 | 2 | 8 | Snapshot schema |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 10 | P | State payload | Sections below |
| 10+P | 32 | SHA-256 | Hash of schema+state payload, bytes8 through payload end |

### 12.2 Fixed payload prefix (relative to frame10)

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | StateVersion |
| 8 | 1 | 1–5 | StrategyMode |
| 9 | 8 | — | KillEpoch |
| 17 | 8 | — | ExecutorGeneration |
| 25 | 8 | — | Reclaimed group count |
| 33 | 8 | — | Settled-incomplete effect count |
| 41 | 2 | ≤8 | Caller-cursor count C |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 43 | 24C | CallerCursor | Caller ID / expired-below / gateway-results-acknowledged-through;8 each |

### 12.3 Variable section order

Let X₀=43+24C, relative to payload start. In the following table each row begins at Xⱼ and Xⱼ₊₁=Xⱼ+that row's byte length. This recurrence gives exact offsets even when nested policy/frame sizes differ; a fixed absolute offset would be false.

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| X₀ | 2+Σ(133+12Rᵢ) | Policy count≤16 | u16 count, then PolicyArtifact +1-byte lifecycle per record |
| X₁ | 1 or 1+S+E+66+21+16 | Presence0/1 | Active executable bundle |
| X₂ | 1 or 1+S+E+66+21+16 | Presence0/1 | Staged executable bundle |
| X₃ | 2+24N | Source count≤16 | Source cursors |
| X₄ | 2+56N | Source-event count≤128 | Source event receipts |
| X₅ | 2+133N | Request-receipt count≤256 | Identity48 + packed result85 per receipt |
| X₆ | 2+Σ(241+8Fᵢ+54Qᵢ) | Decision count≤16; Qᵢ0/1 | Retained decisions; proposal-presence selects54-byte extension |
| X₇ | 2+112N | Risk-account count≤4 | Risk accounts |
| X₈ | 2+57N | Reservation count≤16 | Reservations |
| X₉ | 2+43N | Order count≤16 | Cluster orders |
| X₁₀ | 2+62N | Outbox count≤16 | Compact outbox effects |
| X₁₁ | 32 | — | Execution catalog root |

Bundle S=feature-schema bytes, E=executable-policy bytes. Its final16 bytes are staged_state_version and activated_state_version,8 each. Empty total state payload95 bytes; empty canonical frame137. Maximum formula sums independent arena ceilings; it is a bound, not a claim every maximal combination is reachable.

### 12.4 Source cursor (24) / source-event receipt (56)

Reducer → snapshot encoder → recovery. Both share this prefix:
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | SourceId |
| 8 | 8 | — | Generation |
| 16 | 8 | — | Sequence |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 24 | 32 | SourceEventReceipt only | Event digest |

### 12.5 Request receipt — 133 bytes

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | CallerId |
| 8 | 8 | — | RequestSequence |
| 16 | 32 | — | Command payload digest |
| 48 | 85 | Layout below | Packed result |

Packed result differs from standalone V2KR: it has no outer header/digest and combines lifecycle/detail_kind into one byte. Relative offsets:


| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 1 | ApplyCode enum | ApplyCode |
| 1 | 1 | ContractError enum | ContractError |
| 2 | 8 | Interpret through detail_kind | Metadata word0 |
| 10 | 8 | Interpret through detail_kind | Metadata word1 |
| 18 | 1 | lifecycle low3 bits; detail_kind shifted3 | Lifecycle + detail kind |
| 19 | 8 | — | StateVersion |
| 27 | 1 | 1–5 | StrategyMode |
| 28 | 8 | — | KillEpoch |
| 36 | 8 | — | ExecutorGeneration |
| 44 | 8 | — | Detail word0 |
| 52 | 8 | — | Detail word1 |
| 60 | 8 | — | Detail word2 |
| 68 | 8 | — | DecisionId |
| 76 | 1 | DecisionCode enum | DecisionCode |
| 77 | 1 | 1–4 | Proposed Directive |
| 78 | 1 | 1–4 | Effective Directive |
| 79 | 1 | 1 authoritative;2 shadow | EvaluationMode |
| 80 | 2 | u16; no-match sentinel65535 | Matched rule index |
| 82 | 2 | — | Rules visited |
| 84 | 1 | ActionResolutionCode enum | ActionResolutionCode |


### 12.6 Retained decision — 241+8F+54Q bytes

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | DecisionId |
| 8 | 8 | — | StateVersion |
| 16 | 8 | — | KillEpoch |
| 24 | 8 | — | ExecutorGeneration |
| 32 | 2 | — | Matched rule index |
| 34 | 2 | — | Rules visited |
| 36 | 1 | — | DecisionCode |
| 37 | 1 | — | ContractError |
| 38 | 1 | — | Proposed directive |
| 39 | 1 | — | Effective directive |
| 40 | 1 | — | EvaluationMode |
| 41 | 1 | — | StrategyMode |
| 42 | 1 | — | ActionResolutionCode |
| 43 | 32 | — | Command payload digest |
| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 75 | 165+8F | DecisionFrame schema2 | Original immutable evidence frame |
| 240+8F | 1 | Q=0/1 | Economic proposal presence |
| 241+8F | 54Q | Reduced proposal | Fields below; other identities reconstruct from the frame |

Present proposal extension, offsets relative to extension start:

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | RiskPolicyVersion |
| 8 | 8 | — | InstrumentSpecVersion |
| 16 | 8 | — | VenueCapabilitiesVersion |
| 24 | 2 | — | LogicalOrderSlot |
| 26 | 1 | — | Directive |
| 27 | 1 | — | Side |
| 28 | 1 | — | TimeInForce |
| 29 | 1 | — | PostOnlyMode |
| 30 | 8 | — | Price ticks |
| 38 | 8 | — | Quantity lots |
| 46 | 8 | — | Notional micros |

### 12.7 RiskAccount — 112 bytes

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | AccountId |
| 8 | 8 | — | InstrumentId |
| 16 | 8 | — | RiskPolicyVersion |
| 24 | 8 | — | RiskVersion |
| 32 | 8 | — | PositionVersion |
| 40 | 8 | — | Max order quantity |
| 48 | 8 | — | Max reserved quantity |
| 56 | 8 | — | Reserved quantity |
| 64 | 8 | — | Max order gross notional |
| 72 | 8 | — | Max reserved exposure (margin+fee budget) |
| 80 | 8 | — | Reserved margin |
| 88 | 8 | — | Reserved fee |
| 96 | 4 | — | Initial margin ppm |
| 100 | 4 | — | Maximum fee ppm |
| 104 | 8 | — | Position quantity |

### 12.8 RiskReservation — 57 bytes

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | ClusterOrderId |
| 8 | 8 | — | Original quantity |
| 16 | 8 | — | Filled quantity |
| 24 | 8 | — | Remaining quantity |
| 32 | 8 | — | Remaining notional |
| 40 | 8 | — | Margin hold |
| 48 | 8 | — | Fee hold |
| 56 | 1 | 1 held;2 partially_consumed;3 released | ReservationState |

### 12.9 ClusterOrder — 43 bytes

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | ClusterOrderId |
| 8 | 8 | — | Current EffectId |
| 16 | 8 | — | DecisionId |
| 24 | 8 | — | Authorization StateVersion |
| 32 | 2 | — | Reservation index |
| 34 | 1 | 1..12 | ClusterOrderState |
| 35 | 8 | — | Last evidence StateVersion |

State tags:1 dispatch_authorized;2 acknowledged;3 live;4 partially_filled;5 cancel_pending;6 replace_pending;7 submission_unknown;8 reconciling;9 filled;10 cancelled;11 rejected;12 conflict. Terminal is not necessarily risk-resolved.

### 12.10 Compact OutboxEffectRecord — 62 bytes

| Offset | Bytes | Tag / value | Meaning |
|---|---|---|---|
| 0 | 8 | — | EffectId |
| 8 | 8 | — | ClusterOrderId |
| 16 | 8 | — | DecisionId |
| 24 | 32 | — | Payload fingerprint |
| 56 | 4 | — | Effect generation |
| 60 | 1 | 1..9 | OutboxState |
| 61 | 1 | 1 submit;2 cancel;3 replace | EffectOperation |

Outbox tags:1 pending;2 claimed;3 attempted;4 acknowledged;5 rejected;6 unknown;7 reconciling;8 conflict;9 incomplete. The full166-byte OrderEffect is reconstructed from retained evidence, not stored in this compact record. Source for all snapshot layouts: [snapshot writer](/Users/vishaljha/aeron-raft-pi/src/v2_state_snapshot.cpp:927).

## 13. Local C++ handoffs and contexts: actual RAM offsets

**These tables are measured native offsets, NOT network/file layouts.** Compiled from current headers using C++20, configured cacheline64 on this development host. Bytes in gaps between members and at object ends are native padding. std::optional/std::variant/std::string_view internals are implementation-defined; their aggregate member size is shown, never promoted into a byte protocol.

Local-only types have **no wire tag, canonical length or wire offset** today. Canonically encoded types listed again here have deliberately different RAM and wire layouts. A private verified class is not a serializable struct; only its public value view is tabulated.

### Local-only family overview

| Type | Producer → consumer | RAM bytes | Wire tag / wire offsets |
|---|---|---|---|
| BoundOrderEffect | Committed-state+catalog materializer → gateway authority/transport boundary | 224 | Not defined |
| AuthorityProofView | Live committed barrier apply → gateway authority accept/consume | 416 | Not defined |
| GatewayControlView | Known committed control observations → gateway candidate planner; not fresh quorum authority | 48 | Not defined |
| VenueCapabilityView | Verified catalog/backend capability view → gateway candidate check | 24 | Not defined |
| AuthorityReading | Legacy diagnostic caller → disabled renew_authority shim; cannot mint permission | 32 | Not defined |
| SendQuotaView | Gateway rate/budget owner → candidate/dispatch; updated by owner | 8 | Not defined |
| PreSendDecision | Pure candidate evaluator → gateway planner; not send permission | 240 | Not defined |
| DeliveryAttempt | Gateway send/response bookkeeping → delivery/recovery planner | 24 | Not defined |
| GatewayDeliveryState | Gateway owner → delivery planner; local and non-durable | 400 | Not defined |
| GatewayTimeouts | Configuration → gateway delivery/evidence/reconciliation planners | 48 | Not defined |
| DeliveryPlan | Delivery planner → gateway owner loop | 272 | Not defined |
| EvidenceObligation | Gateway observation intake → evidence retry/proposal owner | 208 | Not defined |
| GatewayEvidenceState | Gateway owner → evidence planner and acknowledgement bookkeeping | 13320 | Not defined |
| EvidencePlan | Evidence planner → gateway proposal owner | 3080 | Not defined |
| VenueQueryView | Venue-query adapter/simulator → gateway reconciliation classifier | 48 | Not defined |
| ReconciliationTarget | Reconciliation planner → gateway query owner | 24 | Not defined |
| ReconciliationAttempt | Gateway query bookkeeping → reconciliation planner | 32 | Not defined |
| GatewayReconciliationState | Gateway owner → reconciliation planner | 520 | Not defined |
| ReconciliationPlan | Reconciliation planner → gateway query owner | 392 | Not defined |
| BlockDescriptor | Raft owner → WAL writer submission ring; writer → owner completion ring after sync | 40 | Not defined |
| WalMetrics | WAL/replication owner → diagnostics; no metrics IPC encoding established here | 40 | Not defined |
| ProposeOutcome | ReplicatedKernel.propose → local caller; admission is not committed success | 24 | Not defined |
| CommittedResult | Committed reducer apply → local caller/gateway result consumer | 176 | Not defined |
| ReplicaStatus | Replication owner → diagnostic reader; not quorum authority | 104 | Not defined |
| CommittedOutboxView | Replication owner → gateway discovery/diagnostics; not send permission | 1056 | Not defined |
| StateSnapshotScratch | Caller-owned decode scratch → transactional state-snapshot decoder | 81392 | Not defined |
| KernelCommandCodecScratch | Caller-owned command candidate → transactional command decoder | 2120 | Not defined |
| BoundFrameScratch | Caller/kernel-owned framing scratch → encode/decode; never returned as payload lifetime | 65613 | Not defined |


### Native OrderEffect

Committed outbox materializer → gateway; canonical payload in §5.3. sizeof=176; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | effect_id |
| 8 | 8 | No per-field wire tag | cluster_order_id |
| 16 | 8 | No per-field wire tag | committed_directive_index |
| 24 | 8 | No per-field wire tag | executor_generation |
| 32 | 8 | No per-field wire tag | kill_epoch |
| 40 | 8 | No per-field wire tag | venue_id |
| 48 | 8 | No per-field wire tag | account_id |
| 56 | 8 | No per-field wire tag | instrument_id |
| 64 | 1 | No per-field wire tag | operation |
| 65 | 1 | No per-field wire tag | side |
| 72 | 8 | No per-field wire tag | price |
| 80 | 8 | No per-field wire tag | quantity |
| 88 | 1 | No per-field wire tag | time_in_force |
| 89 | 1 | No per-field wire tag | post_only |
| 96 | 8 | No per-field wire tag | policy_id |
| 104 | 8 | No per-field wire tag | policy_version |
| 112 | 8 | No per-field wire tag | decision_id |
| 120 | 8 | No per-field wire tag | risk_version |
| 128 | 8 | No per-field wire tag | source_generation |
| 136 | 8 | No per-field wire tag | source_sequence |
| 144 | 32 | No per-field wire tag | payload_digest |


### Native VenueObservation

Venue/gateway evidence producer → reducer observation command; canonical payload in §5.4. sizeof=128; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 1 | No per-field wire tag | kind |
| 8 | 8 | No per-field wire tag | venue_id |
| 16 | 8 | No per-field wire tag | venue_session_id |
| 24 | 8 | No per-field wire tag | venue_sequence |
| 32 | 8 | No per-field wire tag | cluster_order_id |
| 40 | 8 | No per-field wire tag | effect_id |
| 48 | 8 | No per-field wire tag | venue_order_id |
| 56 | 8 | No per-field wire tag | execution_id |
| 64 | 1 | No per-field wire tag | side |
| 72 | 8 | No per-field wire tag | price |
| 80 | 8 | No per-field wire tag | quantity |
| 88 | 8 | No per-field wire tag | fee |
| 96 | 32 | No per-field wire tag | evidence_digest |


### Native BoundOrderEffect

Committed-state+catalog materializer → gateway authority/transport boundary. sizeof=224; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 176 | No per-field wire tag | effect |
| 176 | 32 | No per-field wire tag | catalog_root |
| 208 | 8 | No per-field wire tag | instrument_spec_version |
| 216 | 8 | No per-field wire tag | venue_capabilities_version |


### Native AuthorityBarrier

Gateway begin_request → ReplicatedKernel.propose_authority_barrier; canonical frame §8. sizeof=144; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 16 | No per-field wire tag | incarnation |
| 16 | 8 | No per-field wire tag | request_sequence |
| 24 | 8 | No per-field wire tag | effect_id |
| 32 | 32 | No per-field wire tag | expected_effect_payload_digest |
| 64 | 8 | No per-field wire tag | expected_kill_epoch |
| 72 | 8 | No per-field wire tag | expected_executor_generation |
| 80 | 32 | No per-field wire tag | catalog_root |
| 112 | 32 | No per-field wire tag | authority_domain |


### Native AuthorityProofView

Live committed barrier apply → gateway authority accept/consume. sizeof=416; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 144 | No per-field wire tag | request |
| 144 | 224 | No per-field wire tag | binding |
| 368 | 4 | No per-field wire tag | issuer_id |
| 376 | 8 | No per-field wire tag | term |
| 384 | 8 | No per-field wire tag | barrier_index |
| 392 | 8 | No per-field wire tag | applied_index |
| 400 | 8 | No per-field wire tag | state_version |
| 408 | 1 | No per-field wire tag | strategy_mode |


### Native GatewayControlView

Known committed control observations → gateway candidate planner; not fresh quorum authority. sizeof=48; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | executor_generation |
| 8 | 8 | No per-field wire tag | kill_epoch |
| 16 | 1 | No per-field wire tag | strategy_mode |
| 24 | 8 | No per-field wire tag | confirmed_at |
| 32 | 8 | No per-field wire tag | lease_bound |
| 40 | 8 | No per-field wire tag | now |


### Native VenueCapabilityView

Verified catalog/backend capability view → gateway candidate check. sizeof=24; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | venue_id |
| 8 | 8 | No per-field wire tag | capabilities_version |
| 16 | 1 | No per-field wire tag | supports_post_only |
| 17 | 1 | No per-field wire tag | supports_good_til_cancel |


### Native AuthorityReading

Legacy diagnostic caller → disabled renew_authority shim; cannot mint permission. sizeof=32; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 1 | No per-field wire tag | authoritative |
| 8 | 8 | No per-field wire tag | executor_generation |
| 16 | 8 | No per-field wire tag | kill_epoch |
| 24 | 1 | No per-field wire tag | strategy_mode |


### Native SendQuotaView

Gateway rate/budget owner → candidate/dispatch; updated by owner. sizeof=8; alignof=4.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 4 | No per-field wire tag | normal_lane_remaining |
| 4 | 4 | No per-field wire tag | emergency_lane_remaining |


### Native PreSendDecision

Pure candidate evaluator → gateway planner; not send permission. sizeof=240; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 1 | No per-field wire tag | refusal |
| 8 | 184 | No per-field wire tag | effect |
| 192 | 32 | No per-field wire tag | catalog_root |
| 224 | 8 | No per-field wire tag | instrument_spec_version |
| 232 | 8 | No per-field wire tag | venue_capabilities_version |


### Native DeliveryAttempt

Gateway send/response bookkeeping → delivery/recovery planner. sizeof=24; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | effect_id |
| 8 | 8 | No per-field wire tag | sent_at |
| 16 | 4 | No per-field wire tag | attempts |
| 20 | 1 | No per-field wire tag | awaiting_response |
| 21 | 1 | No per-field wire tag | resend_permitted |
| 22 | 1 | No per-field wire tag | from_recovery |


### Native GatewayDeliveryState

Gateway owner → delivery planner; local and non-durable. sizeof=400; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 1 | No per-field wire tag | started |
| 1 | 1 | No per-field wire tag | recovering |
| 8 | 8 | No per-field wire tag | attempt_count |
| 16 | 384 | No per-field wire tag | attempts |


### Native GatewayTimeouts

Configuration → gateway delivery/evidence/reconciliation planners. sizeof=48; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | response_deadline |
| 8 | 4 | No per-field wire tag | max_send_attempts |
| 16 | 8 | No per-field wire tag | query_interval |
| 24 | 4 | No per-field wire tag | max_query_attempts |
| 32 | 8 | No per-field wire tag | commit_deadline |
| 40 | 8 | No per-field wire tag | resting_confirm_interval |


### Native DeliveryPlan

Delivery planner → gateway owner loop. sizeof=272; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | send_count |
| 8 | 128 | No per-field wire tag | to_send |
| 136 | 8 | No per-field wire tag | ambiguous_count |
| 144 | 128 | No per-field wire tag | now_ambiguous |


### Native EvidenceObligation

Gateway observation intake → evidence retry/proposal owner. sizeof=208; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 48 | No per-field wire tag | identity |
| 48 | 128 | No per-field wire tag | observation |
| 176 | 8 | No per-field wire tag | effect_id |
| 184 | 8 | No per-field wire tag | first_recorded_at |
| 192 | 8 | No per-field wire tag | last_proposed_at |
| 200 | 4 | No per-field wire tag | attempts |
| 204 | 1 | No per-field wire tag | awaiting_commit |


### Native GatewayEvidenceState

Gateway owner → evidence planner and acknowledgement bookkeeping. sizeof=13320; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | obligation_count |
| 8 | 13312 | No per-field wire tag | obligations |


### Native EvidencePlan

Evidence planner → gateway proposal owner. sizeof=3080; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | proposal_count |
| 8 | 3072 | No per-field wire tag | to_propose |


### Native VenueQueryView

Venue-query adapter/simulator → gateway reconciliation classifier. sizeof=48; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 1 | No per-field wire tag | status |
| 8 | 8 | No per-field wire tag | venue_session_id |
| 16 | 8 | No per-field wire tag | venue_order_id |
| 24 | 8 | No per-field wire tag | filled_quantity |
| 32 | 8 | No per-field wire tag | remaining_quantity |
| 40 | 1 | No per-field wire tag | cancelled |
| 41 | 1 | No per-field wire tag | window_complete |
| 42 | 1 | No per-field wire tag | ordered_after_relevant_sends |


### Native ReconciliationTarget

Reconciliation planner → gateway query owner. sizeof=24; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | cluster_order_id |
| 8 | 8 | No per-field wire tag | effect_id |
| 16 | 1 | No per-field wire tag | need |


### Native ReconciliationAttempt

Gateway query bookkeeping → reconciliation planner. sizeof=32; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | cluster_order_id |
| 8 | 8 | No per-field wire tag | last_query_at |
| 16 | 4 | No per-field wire tag | attempts |
| 20 | 1 | No per-field wire tag | awaiting_answer |
| 24 | 8 | No per-field wire tag | evidence_at_query |


### Native GatewayReconciliationState

Gateway owner → reconciliation planner. sizeof=520; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | attempt_count |
| 8 | 512 | No per-field wire tag | attempts |


### Native ReconciliationPlan

Reconciliation planner → gateway query owner. sizeof=392; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | query_count |
| 8 | 384 | No per-field wire tag | to_query |


### Native BlockDescriptor

Raft owner → WAL writer submission ring; writer → owner completion ring after sync. sizeof=40; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | slot |
| 8 | 8 | No per-field wire tag | generation |
| 16 | 8 | No per-field wire tag | sequence |
| 24 | 8 | No per-field wire tag | highest_record_index |
| 32 | 8 | No per-field wire tag | byte_count |


### Native WalMetrics

WAL/replication owner → diagnostics; no metrics IPC encoding established here. sizeof=40; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | blocks_submitted |
| 8 | 8 | No per-field wire tag | blocks_durable |
| 16 | 8 | No per-field wire tag | backpressure_refusals |
| 24 | 8 | No per-field wire tag | highest_staged_index |
| 32 | 8 | No per-field wire tag | durable_index |


### Native ProposeOutcome

ReplicatedKernel.propose → local caller; admission is not committed success. sizeof=24; alignof=8.

CP-025 additionally appends local ProposeStatus::log_capacity_exhausted (value8).
CP-023 appended local ProposeStatus::result_capacity_exhausted (value7). No wire
schema changes: it means no new result obligation was accepted; retry depends on
consumer drain, except the practically unreachable nonwrapping generation limit.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 1 | No per-field wire tag | status |
| 4 | 4 | No per-field wire tag | leader_id |
| 8 | 8 | No per-field wire tag | term |
| 16 | 8 | No per-field wire tag | index |


### Native CommittedResult

Committed reducer apply → local caller/gateway result consumer. sizeof=176; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 48 | No per-field wire tag | identity |
| 48 | 112 | No per-field wire tag | result |
| 160 | 8 | No per-field wire tag | term |
| 168 | 8 | No per-field wire tag | index |


### Native ReplicaStatus

Replication owner → diagnostic reader; not quorum authority. sizeof=104; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 4 | No per-field wire tag | node_id |
| 4 | 4 | No per-field wire tag | leader_id |
| 8 | 16 | No per-field wire tag | role |
| 24 | 8 | No per-field wire tag | term |
| 32 | 8 | No per-field wire tag | current_index |
| 40 | 8 | No per-field wire tag | commit_index |
| 48 | 8 | No per-field wire tag | applied_index |
| 56 | 8 | No per-field wire tag | state_version |
| 64 | 8 | No per-field wire tag | outbox_effect_count |
| 72 | 32 | No per-field wire tag | state_digest |


### Native CommittedOutboxView

Replication owner → gateway discovery/diagnostics; not send permission. sizeof=1056; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 1 | No per-field wire tag | authoritative |
| 4 | 4 | No per-field wire tag | leader_id |
| 8 | 8 | No per-field wire tag | term |
| 16 | 8 | No per-field wire tag | applied_index |
| 24 | 8 | No per-field wire tag | effect_count |
| 32 | 1024 | No per-field wire tag | effects |


### Native TradingState

Single reducer owner → apply/snapshot/diagnostic readers. sizeof=81392; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | state_version |
| 8 | 1 | No per-field wire tag | strategy_mode |
| 16 | 8 | No per-field wire tag | kill_epoch |
| 24 | 8 | No per-field wire tag | executor_generation |
| 32 | 8 | No per-field wire tag | policy_count |
| 40 | 14720 | No per-field wire tag | policies |
| 14760 | 2088 | No per-field wire tag | active_executable_policy |
| 16848 | 2088 | No per-field wire tag | staged_executable_policy |
| 18936 | 8 | No per-field wire tag | source_count |
| 18944 | 384 | No per-field wire tag | sources |
| 19328 | 8 | No per-field wire tag | source_event_count |
| 19336 | 7168 | No per-field wire tag | source_events |
| 26504 | 8 | No per-field wire tag | receipt_count |
| 26512 | 40960 | No per-field wire tag | receipts |
| 67472 | 8 | No per-field wire tag | decision_count |
| 67480 | 1024 | No per-field wire tag | decision_hot |
| 68504 | 9344 | No per-field wire tag | decision_cold |
| 77848 | 8 | No per-field wire tag | risk_account_count |
| 77856 | 448 | No per-field wire tag | risk_accounts |
| 78304 | 8 | No per-field wire tag | reservation_count |
| 78312 | 1024 | No per-field wire tag | reservations |
| 79336 | 8 | No per-field wire tag | order_count |
| 79344 | 768 | No per-field wire tag | orders |
| 80112 | 8 | No per-field wire tag | outbox_effect_count |
| 80120 | 1024 | No per-field wire tag | outbox_effects |
| 81144 | 8 | No per-field wire tag | reclaimed_group_count |
| 81152 | 8 | No per-field wire tag | settled_incomplete_effect_count |
| 81160 | 8 | No per-field wire tag | caller_cursor_count |
| 81168 | 192 | No per-field wire tag | caller_cursors |
| 81360 | 32 | No per-field wire tag | execution_catalog_root |


### Native CommandIdentity

Command producer → deduplication/replay/result correlation. sizeof=48; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | caller_id |
| 8 | 8 | No per-field wire tag | request_sequence |
| 16 | 32 | No per-field wire tag | payload_digest |


### Native Command

Command decoder/producer → reducer; canonical V2KC layout differs. sizeof=2120; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 48 | No per-field wire tag | identity |
| 48 | 2072 | No per-field wire tag | body |


### Native ApplyResult

Reducer → local result publisher/snapshot receipt; canonical V2KR layout differs. sizeof=112; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 1 | No per-field wire tag | code |
| 1 | 1 | No per-field wire tag | contract_error |
| 8 | 16 | No per-field wire tag | metadata_words |
| 24 | 1 | No per-field wire tag | policy_lifecycle |
| 32 | 8 | No per-field wire tag | state_version |
| 40 | 1 | No per-field wire tag | strategy_mode |
| 48 | 8 | No per-field wire tag | kill_epoch |
| 56 | 8 | No per-field wire tag | executor_generation |
| 64 | 24 | No per-field wire tag | detail |
| 88 | 8 | No per-field wire tag | decision_id |
| 96 | 1 | No per-field wire tag | decision_code |
| 97 | 1 | No per-field wire tag | proposed_directive |
| 98 | 1 | No per-field wire tag | effective_directive |
| 99 | 1 | No per-field wire tag | evaluation_mode |
| 100 | 2 | No per-field wire tag | matched_rule_index |
| 102 | 2 | No per-field wire tag | rules_visited |
| 104 | 1 | No per-field wire tag | action_resolution_code |
| 105 | 1 | No per-field wire tag | detail_kind |


### Native DecisionFrame

Advisory feature producer → evaluator; canonical layout differs. sizeof=432; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | policy_id |
| 8 | 8 | No per-field wire tag | policy_version |
| 16 | 8 | No per-field wire tag | feature_schema_id |
| 24 | 8 | No per-field wire tag | venue_id |
| 32 | 8 | No per-field wire tag | venue_session_id |
| 40 | 8 | No per-field wire tag | venue_sequence |
| 48 | 8 | No per-field wire tag | source_id |
| 56 | 8 | No per-field wire tag | source_generation |
| 64 | 8 | No per-field wire tag | source_sequence |
| 72 | 8 | No per-field wire tag | event_time_ns |
| 80 | 8 | No per-field wire tag | receive_time_ns |
| 88 | 8 | No per-field wire tag | instrument_id |
| 96 | 8 | No per-field wire tag | account_id |
| 104 | 2 | No per-field wire tag | feature_count |
| 112 | 256 | No per-field wire tag | features |
| 368 | 1 | No per-field wire tag | market_data_status |
| 376 | 8 | No per-field wire tag | market_data_age_ns |
| 384 | 8 | No per-field wire tag | position_version |
| 392 | 8 | No per-field wire tag | risk_version |
| 400 | 32 | No per-field wire tag | raw_evidence_digest |


### Native RiskAccount

Reducer-owned → risk calculations/snapshot. sizeof=112; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | account_id |
| 8 | 8 | No per-field wire tag | instrument_id |
| 16 | 8 | No per-field wire tag | risk_policy_version |
| 24 | 8 | No per-field wire tag | risk_version |
| 32 | 8 | No per-field wire tag | position_version |
| 40 | 8 | No per-field wire tag | max_order_quantity |
| 48 | 8 | No per-field wire tag | max_reserved_quantity |
| 56 | 8 | No per-field wire tag | reserved_quantity |
| 64 | 8 | No per-field wire tag | max_order_gross_notional |
| 72 | 8 | No per-field wire tag | max_reserved_exposure |
| 80 | 8 | No per-field wire tag | reserved_margin |
| 88 | 8 | No per-field wire tag | reserved_fee |
| 96 | 4 | No per-field wire tag | initial_margin_ppm |
| 100 | 4 | No per-field wire tag | maximum_fee_ppm |
| 104 | 8 | No per-field wire tag | position_quantity |


### Native RiskReservation

Reducer-owned → fill/cancel settlement/snapshot. sizeof=64; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | cluster_order_id |
| 8 | 8 | No per-field wire tag | original_quantity |
| 16 | 8 | No per-field wire tag | filled_quantity |
| 24 | 8 | No per-field wire tag | remaining_quantity |
| 32 | 8 | No per-field wire tag | remaining_notional |
| 40 | 8 | No per-field wire tag | margin_hold |
| 48 | 8 | No per-field wire tag | fee_hold |
| 56 | 1 | No per-field wire tag | state |


### Native ClusterOrder

Reducer-owned → lifecycle/reconstruction/snapshot. sizeof=48; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | cluster_order_id |
| 8 | 8 | No per-field wire tag | effect_id |
| 16 | 8 | No per-field wire tag | decision_id |
| 24 | 8 | No per-field wire tag | authorization_state_version |
| 32 | 2 | No per-field wire tag | reservation_index |
| 34 | 1 | No per-field wire tag | state |
| 40 | 8 | No per-field wire tag | last_evidence_state_version |


### Native OutboxEffectRecord

Reducer-owned → full effect reconstruction/snapshot. sizeof=64; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 8 | No per-field wire tag | effect_id |
| 8 | 8 | No per-field wire tag | cluster_order_id |
| 16 | 8 | No per-field wire tag | decision_id |
| 24 | 32 | No per-field wire tag | payload_digest |
| 56 | 4 | No per-field wire tag | effect_generation |
| 60 | 1 | No per-field wire tag | state |
| 61 | 1 | No per-field wire tag | operation |


### Native StateSnapshotScratch

Caller-owned decode scratch → transactional state-snapshot decoder. sizeof=81392; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 81392 | No per-field wire tag | candidate |


### Native KernelCommandCodecScratch

Caller-owned command candidate → transactional command decoder. sizeof=2120; alignof=8.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 2120 | No per-field wire tag | candidate |


### Native BoundFrameScratch

Caller/kernel-owned framing scratch → encode/decode; never returned as payload lifetime. sizeof=65613; alignof=1.

| RAM offset | RAM bytes | Tag | Meaning / field |
|---|---|---|---|
| 0 | 65613 | No per-field wire tag | bytes |


### Private/opaque contexts — measured totals only

A wire layout cannot be inferred from sizeof. Private members are not exposed as an IPC protocol.

| Context | sizeof | alignof | Owner / consumer / caveat |
|---|---|---|---|
| VerifiedStaticIdentity | 148 | 4 | Startup verifier owns; kernel/framing read identity+derived digests |
| VerifiedIdentityStartup | 8672 | 8 | Move-only startup owner consumed by kernel; includes catalog/identity/path/ownership context; pathname allocations excluded |
| VerifiedExecutionCatalog | 8488 | 8 | Startup owns; bounded immutable rows read by reducer/gateway |
| GatewayAuthority | 992 | 8 | Gateway owner; two-slot request/proof and stop/high-water state |
| AuthorityProof | 416 | 8 | Restricted constructor; wraps AuthorityProofView |
| WalBlockArena | 96 | 8 | Inline object only; excludes block-vector allocations and262,144 block payload bytes |
| WalWriter | 5504 | 64 | Contains submission/completion SPSC rings; references shared arena/segment |
| DescriptorRing64 | 2688 | 64 | 64×40=2,560 descriptor payload bytes +128 for cacheline-separated counters |
| ReplicatedKernel | 124416 | 8 | CP-043 host measurement; includes32,768-byte deferred peer frame; excludes separately owned heaps/library memory |
| OutboundMailboxes | 66680 | 8 | Always two remote-peer sets, even for two voters; egress/scratch extra |
| RetainedLog | 64 | 8 | Owner only;16777216-byte cell slab and327680-byte descriptor array extra |
| RetainedLogDescriptor | 40 | 8 | Includes24-byte optional result handle; full index + actual pool token |

C++ ownership lesson: a span/string_view does not own its target. A std::vector's sizeof excludes its heap contents. std::array includes every slot even when count=0. Those are three different reasons not to equate native sizeof, encoded bytes and total resident memory.

CP-043 adds one native persistence job, measured168 bytes on this host and
compile-time capped at256 bytes. It is separately heap-owned for each active
fence; this is bounded storage, NOT an allocation-free owner path. It carries
operation/generation/nonwrapping sequence, captured durability/staging indices,
metadata or truncate target, store references, and an echoed completion. These
are native C++ fields, not serialized offsets/tags. One32-KiB owned retry frame
reuses the existing maximum peer-frame limit; the existing mailbox is withheld
rather than duplicated. No TradingState, command, peer, WAL or snapshot encoding
changed. These host sizeof values do not measure allocator overhead, total RSS
or NUC/Pi headroom. Other rows retain their earlier measurement dates.

## 14. Not-yet-frozen whole-node limits and missing message schemas

CP-023 addition: the approved kernel-owned ResultLedger occupies229400 native bytes
(24-byte header +1024×224-byte slots), with16-byte generation/slot handles.
It is allocated once by ReplicatedKernel, replacing its growable result deque.
Per-log optional handle storage occupies24 native bytes. Slot offsets and
ownership costs are tabulated in F5_ADMISSION_RESULT_REVIEW §7. Ledger operations
allocate nothing; owner allocation and retained-entry handle storage are
separate. No canonical message size changed.

Result/outbound/log budgets below are approved and host-integrated; other rows
remain **review candidates** from F5_F7_MVP_CLOSURE_PLAN.md. None is a measured
latency guarantee. Peer16-entry/32-KiB frame limits and local maxima are approved.

| Resource | Candidate bound | Candidate reserve / full behavior | Status |
|---|---|---|---|
| Outbound (CP-025) | Four coalescing mailboxes per peer;32768+3×160 payload backing | Actual fixed owner66680 bytes/node, both remote sets allocated | Approved/integrated; bounded copy-out; egress/scratch extra |
| Result slots | 1,024 total | 64 control; reserve before acceptance, never drop after apply | Approved and kernel-integrated CP-023; not fresh target acceptance |
| Retained log (CP-025) |8192×2048-byte cells =16 MiB backing |8128 ordinary+64 control tokens; markers ordinary | Approved/integrated; owner64+descriptors327680+Raft/scratch additional |
| Snapshot staging | One transfer;128 KiB scratch;≤66 KiB object | Bounded transactional install | Portable bytes/admission not frozen |
| Snapshot chunks | ≤16 KiB;≤5 per max object | Identity, replay/reorder and generation selection required | Not frozen |
| Archive segment/backlog | 4 MiB /16 MiB | Stop ordinary work at12 MiB backlog | Coverage/independent copy policy not frozen |
| Local archive disk quota | 1 GiB | Warn768 MiB; stop ordinary896 MiB | Proposed quota, not approved retention duration |
| Owner turn | ≤8 frames,32 entries,64 KiB peer work | ≤32 apply/completion/output items | Count/byte candidates; not a time guarantee |

Therefore there are **no honest offset tables yet** for portable snapshot chunks/install transactions, archive segments/coverage records, authenticated gateway IPC grants, asynchronous hard-state continuation/completion messages, or the planned observability shared page. They need contracts before encoding. The current BlockDescriptor completion is described above; it is not the future asynchronous term/vote fence protocol.

CP-025 implements the separately approved outbound/log choices. Detailed native
measurement and library resize arithmetic: [evidence](../archive/evidence/f5_capacity_integration_2026-09-23/README.md).
No wire bytes or whole-process ceiling changed. The following native descriptor
is written by the Raft owner during admission, read by callbacks/result publication,
and released only after successful poll/pop/clear ownership transfer:

| Offset | Bytes | Tag / component | Meaning |
|---:|---:|---|---|
|0|8|index|Full retained log index;0 marks an unused cell |
|8|2|length|Valid payload bytes within its2048-byte cell |
|10|1|RetainedLogPool|0 ordinary,1 protected-control ACTUAL token consumed |
|11|5|padding|Native alignment only; never serialized |
|16|24|optional ResultHandle|Reserved result ownership until publication or durable suffix removal |

Modulo8192 selects physical storage, not identity: lookup checks the full index.
Native ownership is owner-thread only. No descriptor or borrowed payload pointer
crosses the network; mailbox encoding copies canonical bytes before callback return.

The512-byte venue JSON request buffer is a buffer ceiling, not an approved live venue message. Both old market/limit body encoders are quarantined; proposed swap OrderEffect-v3 is not an implemented/approved schema here. No key, credential or signing material belongs in the replicated artifacts tabulated here.

## 15. Verification / limits

- Rebuilt the current v2-layout-report before reading numbers; independently compiled native offsetof/sizeof probe at cacheline64 and rebuilt layout report with128. This is host layout verification, not fresh target acceptance.
- Field order follows current encoder implementations, not declaration order or a historical chat table.
- Checked all20 KernelCommandKind variants,15 generic-envelope kinds,7 observation kinds,4 peer kinds and4 bound-frame kinds as distinct tag namespaces.
- Initially produced as a read-only CP-019 report, then placed in `docs/` at the owner's request. CP-020 refresh updates implemented storage status and freshly measured native sizes; its code/test evidence is separate in `evidence/f5_local_history_2026-09-22/`.
- No live venue access, credential access, hardware gate, source freeze, commit or tag.
- This catalog does not constitute a new adversarial correctness audit or imply pending F5/F6 work is closed.

Primary references: [CURRENT_HANDOFF](/Users/vishaljha/aeron-raft-pi/docs/CURRENT_HANDOFF.md), [architecture contracts](/Users/vishaljha/aeron-raft-pi/docs/V2_ARCHITECTURE_CONTRACTS.md), [kernel codec](/Users/vishaljha/aeron-raft-pi/src/v2_kernel_codec.cpp), [payload codec](/Users/vishaljha/aeron-raft-pi/src/v2_payload.cpp), [snapshot codec](/Users/vishaljha/aeron-raft-pi/src/v2_state_snapshot.cpp), [closure plan](/Users/vishaljha/aeron-raft-pi/docs/F5_F7_MVP_CLOSURE_PLAN.md).
