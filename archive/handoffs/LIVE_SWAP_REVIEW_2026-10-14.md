# Claude takeover: bounded live SOL swap and route-independent authorization

Date: 2026-10-14. Repository: `/Users/vishaljha/aeron-raft-pi`.
Branch: `v2/phase-5-corrections`. Committed checkpoint: `2e5642a30da3c9cea7e094afc1cbd6a20b2d6e33`.

This is a review and continuation handoff, not a new grant of trading authority.
The owner requested a holistic C++/architecture review and further implementation,
with the smallest necessary scope. Read the [takeover prompt](CLAUDE_TAKEOVER_PROMPT_2026-10-14.md),
[implemented-decision record](../../docs/adr/0025-bounded-swap-verification-and-policy-bound-recovery.md)
and [proposed separation](../../docs/adr/0026-stable-execution-policy-and-per-attempt-route-evidence.md).

## 1. Executive state and the problem to review

The distributed system, isolated signer, authenticated transport, durable submission,
account opening and recovery machinery exist. The bounded integrated build is qualified
on both native architectures. **No real buy has yet been executed through this cluster.**
Earlier owner CLI trades are real fee/settlement evidence, but are not Raft end-to-end tests.

The immediate objective is one supervised SOL buy with **at most 1.000000 USDC total
debit**, then finalized reconciliation and stop. Continuous strategy execution is not
part of this checkpoint. The owner's later $25 reusable-capital preference does not
resize this one-buy implementation or authorize extra orders.

The latest obstacle is transaction compatibility. TM chooses a DEX route for each
unsigned quote. We added narrowly verified support for historical captures, then fresh
quotes selected other layouts. Quote creation does not let the current documented TM
API select a DEX. The next agent should review the architectural coupling before adding
another hard-coded layout. A repeated quote loop is not the solution.

The current `DirectBuyVerificationPolicy` pins one route, complete account arrangement,
lookup image and expiry. Its hash enters the protected signer journal binding; the
transaction-review digest also enters runtime identity/account review. That couples
per-order execution data to persistent authority. A route change therefore creates
renewal/adoption work even when the owner, instrument and economic limits are unchanged.

**Recommendation, not implementation:** separate stable account/execution authority from
per-attempt evidence; retain exact byte/intent/policy binding and durable replay protection.
Then assess whether a reviewed Jupiter contract can safely cover multiple DEX routes.
Removing identity churn and admitting arbitrary unknown programs are different decisions.
The latter has not been approved or proved. ADR0026 contains the review questions,
alternatives, trust boundary and acceptance tests.

## 2. What is actually running, installed, staged and committed

| Layer | Last verified state |
|---|---|
| Local committed source | `2e5642a`; large unrelated modified/untracked tree remains |
| Voters and paired exporters | Three existing voters on qualified `1a46fd8`; one cluster only |
| Incarnation | `b79e65be1ea14e219016ec48ea02efef` |
| Consensus | Leader3, term12, current/commit/applied/durable9; all up/ready, failed0; halted mode5 |
| Live installed manifest | `fe54001ce7bbd36c25947cc29ecf70833d161e37`, Byreal/schema2 |
| New live executable | Qualified `2e5642a` staged in immutable release directory, **not adopted** |
| Live services | `velocita-swap-gateway.service` and `velocita-swap-signer.service`: inactive, PID0, RefuseManualStart=yes |
| Funding | Already initialized from actual TM inventory; do not initialize again |
| Five recovery journals | Preserved; original idle/effect0 state, no cluster trade; protected journal generation has advanced through reviewed renewals |
| Paper producers | Inhibited; historical stochastic-named voter units are still the one current cluster, not an active paper strategy |
| Latest venue observation | 40.006989USDC and0.040764166SOL, no holds; historical observation, refresh before launch |

[Fresh host readback](../evidence/live_handoff_2026-10-14/host-readback.json) timestamps
read-only service/metric checks taken for this handoff. It is not an authenticated
Raft authority proof or a new venue balance capture. Account observation times/hashes
are in [latest quote check](../evidence/bounded_launch_review_2026-10-13/invariant/qualification/supported-route-recheck.json).
[Protected staging verification](../evidence/bounded_launch_review_2026-10-13/invariant/qualification/staged-verification.json)
confirmed all 11 protected documents/journals unchanged during staging.

Hosts: nuc1 `100.72.127.102` (bulbazar-NUC), Pi `100.84.151.116`, nuc2 `100.85.36.22`.
SSH build/runtime account `velocitas`; NUC1 sudo owner is `vishal`, not `velocitas`.
Never copy passwords, tokens, API keys or private capture bodies into this repository.

Pinned identities:

- Installed manifest SHA256 `896f404a4bc4914b02c2dae68539655a8b1b64de8eb39da3975e4e28f4fbe560`.
- New NUC gateway SHA256 `5ca983a71adf84dab1bb4b85cae894aa85f5d38de8b34b46d39fcfa3d495473e`.
- New Pi gateway SHA256 `8995eeb88248c1a3ff37bf9ce52eac987ae7ec058374f55361f299d48024be3d`.
- Original opening-record SHA256 `10d9d43a3c571d7a9d25e4029342a898b2e27950f4dc81fc5357369a483c5fd3`.
- Opening review digest `8daaf8ed69d67dff71b970d67bf682e9fafee19c0eb596c9ec87f38031a721f9`.
- Original authenticated swap-state digest `9cfaf73475bc0841411a069627f01b3b9adf779c07ca2969a59c4aed378f8d5b`.
- Gateway UID29001/caller22/key13; signer UID29002/caller23/key14; owner caller7/key9.
- Kill epoch1/executor generation1 at the retained opening; re-read before any command.

## 3. Work completed across the live-promotion continuation

This section reconstructs the preceding continuation from Git and retained evidence.
It is not a claim that every historical component was freshly re-audited in this handoff.
The initial October7 paper checkpoint is historical and must not be used as the current
live state. Milestones, source commits and deployment state are deliberately separated.

| Problem and why it mattered | Work and evidence | Boundary still open |
|---|---|---|
| A gateway could act on older state than it had already observed | Durable state-watermark work `2f5d405`; subsequent integrated read-barrier fix `a890307` | Ordinary status metrics do not substitute for fresh authenticated authority |
| Fixed-base paper orders did not describe TM quote-input swaps | Dedicated bounded swap intent/reducer, whole-transaction verification, submission and result recovery; `6349134`, `a890307`, `73e753d`; [integration](../evidence/live_readiness_2026-10-09/RUNTIME_INTEGRATION_2026-10-12.md) | Not a continuous strategy bridge or general sell implementation |
| Components passed separately but a leader-loss timing stalled recovery | Eight combined process drills exposed F07; committed current-term type103 read barrier with replication epoch2; [batch](../evidence/live_readiness_2026-10-09/ACCOUNT_PROCESS_BATCH_2026-10-13.md) | Does not claim every read is linearizable after a subsequent partition; existing authority/quorum fences remain |
| Native environments differed from local tests | Pinned x86/ARM builds, bounded CPU/memory/disk, fixture/header/teardown fixes; `ff72962`, `a890307` through `86f4408` | Qualification is scoped, not a full production funds-moving proof |
| Service users and signer network authority were unqualified | Separate permanent UIDs, Unix peer checks, protected runtime, real Aeron reads and network enforcement; `92abb5f`, `053fe43` | Signer egress is voter-address filtering, not a port allowlist; root/host remain trusted |
| Real credentials needed a protected path; delayed settlement must survive signer expiry | FD handoff, tmpfs receiver, hash-pinned bearer rotation, explicit reconciliation-only startup; `1c582e6`, `13cc98b`, `5cdb5e1`, `bb16daf`; [recovery](../evidence/live_readiness_2026-10-09/handoff-recovery-20261013/README.md) | No automatic retry or broad credential rotation; recovery must not prepare/sign/execute |
| Paper balance was not live funding | Actual TM identity/inventory review and once-only canonical opening; `04b641f`, `d63f93d`, `39bb333`; [funding ceremony](../evidence/live_readiness_2026-10-09/funding-start-20261013/README.md) | Fresh balance/history authorization expires; never reinitialize to renew it |
| Quoted fee alone hid a buy-output deduction | Reconciled owner buy, accepted conservative deduction model; `b1c6650`, `73e753d` | Observed extra deduction is not an all-route economic guarantee |
| Operator could not observe live state accurately | Read-only collector/dashboard, confirmed-fill telemetry, idle feed semantics; `19f67d2`, `c05dd8f`, `b85534d`, `039bdae`, `69679c0` | No cluster live fill yet; raw or quoted orders must not appear as confirmed fills |
| Expiry/release updates risked resetting journal identity | Locked idle renewal, session witness, crash-safe before/after archive and route transition; `fb5f413`, `dca6b9a`, `f6d2567`, `cc75e68` | Existing helper is for pristine/effect0 state, not general post-trade migration |
| Fresh TM payloads differed from historical topology | Byreal `fe54001`, Obsidian `8dde28e`, Bison and optional price amendment `1a46fd8`, Invariant `2e5642a` | Latest Meteora38 and Deriverse161 direct transactions remain unsupported |

The underlying research has not established that the stochastic strategy is profitable
at live all-in costs. Do not reinterpret engineering qualification or the owner's desire
for a POC as a positive alpha result. No new strategy/backtest claim is made here.

## 4. Exact latest checkpoint and how it was qualified

`2e5642a` committed 42 explicitly staged paths (13 production/tests plus 29 evidence files).
[Scope manifest](../evidence/bounded_launch_review_2026-10-13/invariant/CHECKPOINT_SCOPE.json).
No shared CMake/gateway-main/gateway-loop edits or mixed promotion tracker were staged.
No kernel, Raft, snapshot, catalog or funding change was needed for Invariant.

Implementation: direct Jupiter variant18 with a true direction flag; schema5; exactly 19
resolved accounts and five outer instructions; program/role/permission checks; bounded
USDC debit, explicit fee, repayment and net-receipt arithmetic; separate policy digest;
explicit Byreal/schema2→Invariant/schema5 locked transition preserving owner-facing roles.
Unknown layouts still refuse. Decoder support alone grants no signing authority.

Local validation: 10 decoder, 9 verifier, 8 review groups; 66 Python tests; 66 named mutation
kills (37 decoder, 19 verifier, 4 review, 6 Python). Decoder/verifier ASan/UBSan passed.
Actual retained capture passed offline with historical time and a synthetic intent at
6,910,173 lamports; adding one lamport to the required minimum refused. C++/Python policy,
signing and runtime identities agree; four older route-policy digests stayed identical.
Independent bounded review reported no blocking findings. These do not prove a fresh
live payload or equivalence of deployed program bytecode to published source.

Both native hosts: 58 CTests, 241 Python/CLI checks, eight combined process cases; exit 0;
106 exported artifact hashes verified per host. 46 unique warning lines per host retained
for the later warning-cleanup iteration. [Qualification](../evidence/bounded_launch_review_2026-10-13/invariant/qualification/README.md).
These are selected qualification suites; do not claim all repository tests passed.

Method:

1. Made a Git bundle from the approved exact commit. Extracted deployment helpers with
   `git show`/`git archive`, never built production from the dirty workspace.
2. Reused clean incremental source/build/dependency caches on each host. Used existing
   `native/launch.py`, `native-focused.py`, `collect.py` from committed source.
3. Built concurrently on NUC/Pi in network-disabled, unprivileged containers: CPU3 affinity,
   one-CPU quota, 2GB memory, 256MB shared memory, existing disk guards retained.
4. Pi lacked initial capacity. Archived three obsolete synthetic-test temporary trees
   on Mac, checked all 206 file hashes, then removed only those trees. Recovered 346MB;
   no runtime WAL, journal, image or rollback data removed.
5. Staged the qualified NUC executable with the existing `upgrade_byreal.py` helper,
   supplying new explicit commit/binary pins. Retained both start inhibitors, installed
   manifest and all 11 protected files. Independent verification passed.
6. Prepared but did not execute combined-renewal and ordered-start scripts. Five synthetic
   preparation tests and nine mocked start tests passed. The offline Mac intent builder
   was rebuilt from committed source and passed five test groups.

Qualification caches:
`~/.cache/velocita-build/bounded-native-20261013-2e5642a30da3c9cea7e094afc1cbd6a20b2d6e33/`
on NUC1/Pi; reusable cache is `~/.cache/velocita-build/integrated-a890307-20261013/`.
Mac source/bundle/preparation: `~/.cache/velocita-build/invariant-release-2e5642a/`.
Owner staging on NUC1: `/home/vishal/.cache/velocita-build/invariant-install-2e5642a/`.
Production staged release: `/opt/velocita/releases/2e5642a30da3c9cea7e094afc1cbd6a20b2d6e33/`.

Use the retained [NUC launch argv](../evidence/bounded_launch_review_2026-10-13/invariant/qualification/nuc1/launch-command.json),
[Pi launch argv](../evidence/bounded_launch_review_2026-10-13/invariant/qualification/pi/launch-command.json),
`*-command.json`, `stage-release.py`,
`verify-staged.py` and build logs as the exact command record. They are evidence;
do not rerun one-shot installers or the funding ceremony from a handoff.
The first Mac builder test invocation lacked its scratch argument; corrected invocation
passed. First cleanup inspection refused the node-exporter read-only rootfs mount;
inspection established that exception before deleting backed-up synthetic files.
Loopback exporter requests refused because exporters bind Tailscale addresses; subsequent
reads used those configured addresses. These were tooling errors, not traded failures.

## 5. Route chronology and economic guarantees

| Capture / checkpoint | Result | What it establishes |
|---|---|---|
| Historical Byreal / `fe54001` | Reviewed schema2 and installed profile | A particular reviewed topology; not TM route pinning |
| Historical Obsidian / `8dde28e` | Qualified exact five/six-instruction layouts | Supported layouts can vary without being the next returned route |
| Bison / `1a46fd8` | Retained conditional floor6,777,951 below original6,819,647 | Optional one-time price-cap amendment implemented/qualified; **not applied** |
| Invariant / `2e5642a` | Retained conditional floor6,910,173 above original floor | No price amendment needed for that capture |
| Fresh quote after qualification | MeteoraDlmm38,15 static accounts,5 outer instructions; refused | New layout absent from direct verifier; no retry/sign/execute |
| Owner-requested subsequent fresh check | Deriverse161,11 static accounts,6 outer instructions; refused | Route can change between adjacent checks; no retry/sign/execute |

Each fresh check had a separate explicit owner request, 20-request account/history capture,
zero unresolved orders and one unsigned quote. They were not an automatic quote-fishing loop.
Historical diagnostic admission cannot authorize a now-expired captured payload.

Economic terms retained:

- Purchase total cap1,000,000USDC atoms; principal998,000 plus explicit fee2,000 (20bp).
- Conservative maximum output deduction2,039,280lamports; guaranteed sponsor credit0.
- Observed October13 owner buy deducted550,840lamports beyond the quoted output; that
  measured result and the conservative bound are different quantities.
- Original max buy price146,635,156USDC atoms per SOL; for$1, minimum6,819,647lamports.
- Proposed higher cap147,537,213 was separately reviewed/implemented but not committed
  into the running Raft state. Do not apply it automatically.
- Realized-loss cap500,000USDC atoms. It is not a guaranteed mark-to-market stop, automatic
  sell, or protection against arbitrary wallet authority beyond the inspected transaction.
- Opening managed40.006989USDC and0.040764166SOL; SOL mark4.353100USDC was an unsigned
  liquidation quote, not historical acquisition cost. Never replace this with paper cash.

## 6. Guarantees, assumptions, deferrals and changes

| Guarantee / limitation | Status at handoff | Review obligation |
|---|---|---|
| One cluster, same incarnation/funding/WAL | Preserved; voters healthy | No reset, second cluster or re-funding |
| Deterministic kernel; external I/O outside apply | Preserved | Route discovery must remain gateway-side |
| Owner/operator/strategy/gateway roles | Explicit authenticated identities and command fences | Do not bypass via host-local direct calls |
| Exact payload/intent binding and signer inspection | Implemented for supported contracts; latest layouts refused | Unknown decoder output must never create a verified capability |
| No duplicate external send after uncertain response | Durable stores and recovery-only path tested in eight cases | Do not claim global exactly-once delivery; retain ambiguity and reservations |
| Original opening and journal lineage | Preserved through locked renewal | Per-attempt refactor needs explicit migration, never journal deletion |
| Native build provenance | Both architectures qualified at2e5642a | New code requires its own scoped checkpoint/qualification |
| Real integrated settlement | **Not yet demonstrated** | Owner CLI receipt is not cluster proof |
| Exact deployed Jupiter minimum-output rounding | **Deferred and not waived** | Revisit before broader aggregator trust; historical arithmetic is conditional |
| Program identity versus code provenance | Program IDs/public layouts checked; deployed-bytecode equivalence not proved | Define upgrade/version trust, refuse unknown change |
| Sponsor gas/fees | Owner says TM sponsors network/swap infrastructure costs; explicit20bp retained | Does not erase observed repayment/output deduction |
| Complete account history | Current owner/current classic-USDC account bounded captures pass | Closed historical accounts/provider archival completeness not proved |
| Signer isolation | Dedicated UID/Unix peer check; voter-address egress | Root trusted; all ports on allowed voter hosts reachable |
| Unknown transaction outcome | Stops new risk, holds exposure | Reconcile exact original operation; never resize or buy back automatically |
| External heartbeat | No Healthchecks URL/check created in last evidence | Open; not silently claimed delivered or waived for general operation |
| Production held-position failure drills | Synthetic/native combined drills done | Real deployed held-position qualification remains open |
| Continuous strategy / sells / multi-effect rollover | Deferred for bounded MVP | Single buy success does not enable full strategy |
| Warnings and networking teaching artifact | Deferred at owner request | Preserve46 warning lines/arch; post-live networking explainer remains requested |

Architectural change under review: move transient route evidence out of persistent
account authority. That is a proposal, not a guarantee already delivered. It must not
remove account freshness, program checks, replay protection or bounds. A short-lived
credential/review renewal is still required when expired; decoupling does not make
credentials or market evidence permanent.

## 7. Reviewer navigation and concrete acceptance targets

Read these implementation seams first:

- `src/v2_swap_transaction_verifier.cpp`: hard-coded topology, bounds and private verified result.
- `include/velocita/v2/swap_transaction_verifier.hpp`: fixed arrays and supported policy shape.
- `src/v2_jupiter_route_decode.cpp`: data-only decoding; direct and shared route support differ.
- `src/v2_swap_transaction_review.cpp`: exact review schemas and policy construction.
- `src/v2_swap_signer_session.cpp`: independent authority checks and protected journal binding.
- `src/v2_swap_signing_connection.cpp`, `src/v2_swap_runtime_production.cpp`: intent/credential/lookup ownership.
- `deploy/v2/swap-runtime/{idle_policy_renewal,review_transition,idle_policy_apply}.py`: migration and policy identity.
- `src/v2_gateway_main.cpp`: pending intent can select recovery-only mode at startup.
- `tools/prepare-swap-mvp.cpp`: offline canonical one-buy intent preparation.

Minimum review: verify resource bounds, integer overflow and rounding; account aliases,
privilege changes, unauthorized owner assets/recipients, extra instructions and token
extensions; payload substitution after verification; stale/forged lookup evidence;
policy changes during an outstanding operation; crashes before/after journal writes;
duplicate sign/submit; unknown outcomes and fresh authenticated read fences.

First RED targets for ADR0026: two valid supported routes under one stable authority
must not require account adoption; the exact first attempt must recover unchanged after
policy/route update. Mutate owner, mint, debit, minimum, fee, repayment, writable privilege,
payload digest or evidence provenance and require a named refusal. Do not count harness
errors as mutation kills. Specify registry bounds and old-policy retention before coding.

Do not expand to an arbitrary plugin engine, a second venue, new strategy, on-chain guard
or new wallet without establishing necessity and reviewing the changed trust boundary.
Book support is architectural, not a proof that arbitrary Jupiter CPI routes are safe.

## 8. Operational continuation after compatibility is established

These are dependency/order notes, not commands to run now:

1. Qualify any changed code from a scoped commit. Stage immutable artifacts, keep guards.
2. Refresh account/session and route evidence; compare against original funding. Prepare
   matching account/review/release witnesses with existing helpers only if their scope fits.
3. Verify C++/Python digest parity for exact proposed documents; use locked apply, fresh
   authenticated role reads and journal locks. Preserve four gateway journals; current
   pristine renewal advances protected journal ordinal only. Never mask a post-trade state
   as pristine. Before current locked apply, do not alter manifest independently or amend
   Raft state: helper checks the original state digest.
4. Refresh protected gateway bearer via the reviewed receiver if required. No secrets in
   argv, logs, repo or environment dumps. Keep real signing material in its protected path.
5. Keep global `/etc/velocita-idle-renewal-active` and all paper/edge guards. Only the two
   live units' reviewed drop-ins may be removed during an authorized start ceremony.
6. Start signer, then gateway **while halted and before any pending buy intent**. Starting
   after intent makes the current executable choose recovery-only, which cannot resume
   merely reserved/prepared work. Release journal locks before native processes open them.
7. From Mac, use verified unused operator sequences, current fences and exact bounded intent;
   stop on failed or ambiguous acknowledgment. Never replay old ceremony sequence numbers.
8. Reconcile finalized receipt, actual balance deltas and committed settlement; verify
   reservation release and one effect. Gateway completes after its one resolved settlement;
   explicitly halt and stop signer/gateway. An uncertain submission requires recovery-only,
   preserving the original identity and exposure, not another buy.

Existing prepared-only scripts are under
`docs/evidence/bounded_launch_review_2026-10-13/invariant/qualification/prepared-only/`.
They were not executed, are not a live launch bundle and contain pins for the old design.
Review them; do not infer approval from their existence.

## 9. Repo and authorization rules for the next agent

- Build/deploy committed source. Commit only on owner instruction; explicit paths only.
  No `git add .`, `git add -A`, reset, clean, stash or whole-file staging of shared edits.
  Owner-requested commit footer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Shared dirty files include CMake, gateway main/loop/tests, research, monitoring and docs.
  [Pre-documentation Git inventory](../evidence/live_handoff_2026-10-14/git-status-before-doc-edits.txt)
  is a snapshot, not proof of ownership. Recheck current status and diffs.
- Protected files: `docs/README.md`, `docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`,
  `docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`, `docs/adr/0002-*`; unchanged by this handoff.
- User allows POSTs for checks; that is distinct from money movement. Existing money scope
  is one conditional $1 buy, not arbitrary signing, repeated attempts or verifier bypass.
- No new implementation of ADR0026 has been approved through this documentation request.
  Claude should review and explain the concrete trust/compatibility decision before changing it.
- Use at most two bounded subagents only if explicitly requested. Keep shared architecture
  and signer/kernel edits with the main agent. This handoff was prepared inline.
- Use durable scratch under `~/.cache/velocita-build`, not `/tmp`; no Safari screen sharing.
  Read-only terminal checks for Grafana; owner checks browser personally.
- Raw venue/session/account evidence stays under `~/.velocita-credentials/truemarkets/`.
  Relevant private directories: `invariant-offline-candidate-20261014`,
  `invariant-launch-quote-2e5642a-20261014`, `invariant-adoption-2e5642a-20261014`,
  `supported-route-check-1791990276214`. Some are expired/preparatory and must not execute.

This handoff edits documentation only, starts no services, makes no venue calls, and
creates no commit. Historical evidence JSON is immutable: e.g. Invariant IMPLEMENTATION.json
records pre-commit/local status; later qualification supersedes its status, not its facts.
