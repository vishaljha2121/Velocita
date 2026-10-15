# Promoting the stochastic paper strategy to live: plan and status

## Strategy permission correction implemented locally — 2026-10-15 21:06 UTC

The approved explicit operator grant is implemented and tested; it is not committed, native-qualified
or deployed. [Evidence and deployment proposal](evidence/automated_launch_2026-10-15/strategy-grant/README.md).
The grant binds the opening, strategy caller, minimum sequence and control epochs; default absence
preserves old refusals and snapshot bytes. It changes only direct swap-intent admission, retaining
the existing risk and protected-signing checks. ADR0016 records this direct-intent exception.

Eleven CTest targets and eight CLI tests pass; 15 freshly compiled mutants fail at their named
assertions. Three six-trade fake-venue cases now use authenticated strategy caller21, including
crash recovery. An old/new fixture replay has identical code71 and snapshot hash for requests1/2.
The grant adds 72 fixed state bytes; the full state stays within its unchanged 80KiB budget.

Fresh readback21:02:26UTC: driver inactive, gateway/signer active, account index14 flat$25,
zero effects/pending/fees. No venue wallet read or live changes during this implementation.
Next: scoped checkpoint, exact-build NUC/Pi qualification, then coordinated single-cluster upgrade
while halted before owner grant/resume. Preserve strategy sequence2(next3), funding/WAL/journals.
Expected owner sequence10 must be reverified. Old readers cannot process a granted state;
no gateway-only upgrade or rollback to old readers after activation. Epoch advancement invalidates
this one-time grant; renewal is not implemented. Review drains October16 13:41:34UTC.

## Focused correction batch natively qualified — 2026-10-15

**Executable checkpoint `1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc` is qualified on NUC1/x86_64
and Pi/aarch64; now owner-installed, with trading still blocked.** Read the
[batch report](evidence/automated_launch_2026-10-15/focused-mvp/README.md) and
[native evidence](evidence/automated_launch_2026-10-15/focused-mvp/native/RESULTS.md).
Each architecture passed 59 CTest targets, 383 Python/CLI checks, eight process-drill cases,
and three fixed repetitions of three six-effect continuous cases: 54 fixture trades, exactly
one create/sign/execute/settlement per effect. All 151 retained artifact hashes per host verified.
Local 33 named mutation kills remain recorded separately. Fake economics do not show alpha.

Commits: `c19a675` fixes F1–F5; `887d6f5` corrects the test packaging boundary; `26b1e51`
uses fresh Raft deadlines after blocking ports; `1f8d7b6` separates signer connection wait
from the accepted request deadline. Failed native attempts and independently reproduced
regressions are retained. Compiler warnings are recorded for the cleanup iteration.

Owner installation succeeded after the scoped `24ecbec` tool qualification: exact `1f8d7b6`
binary/manifest installed, missing caller keys restored create-only, retired originals retained,
observer updated. No funding/journal reset or live startup. Gateway inactive, signer retains prior
failed state, both PID 0 and RefuseManualStart=yes; driver inactive with no sequence state.
See [owner installation evidence](evidence/automated_launch_2026-10-15/focused-deploy/OWNER_INSTALL_RESULT.json).
Observer timer is active; collector still exits 1 before first-start journals exist, which its
snapshot requires. Recheck after startup rather than claiming monitoring is fully operational.

Tools `24ecbec` passed 13 installer and 24 observer tests on NUC1. Executable remains `1f8d7b6`;
one production cluster and all funding/history are preserved. Sudo remains owner-only.
The six-effect driver is already staged: never stage it again or reset its sequences.

Prepared [first-start/seal wrapper](evidence/automated_launch_2026-10-15/focused-deploy/START_AND_SEAL.md):
8 new tests and 6 mutation kills; 13 installer and 19 seal-dependency checks pass.
Fresh voter health agrees on halted mode 5, term 1, leader 3, index 7. Protected owner
preflight and native startup admission are still pending; no services have started.
The helper is committed as `f30e6cc`; all 64 Python checks passed on NUC1. Its exact
source and owner command are staged and hash-verified. Await owner execution; services
remain guarded/stopped and the driver remains inactive.

Next: verify protected resources and fresh authenticated halted state/authorization, then start halted services
and run the supervised six-effect drill. Do not repeat arm, funding or retirement; never reset
driver sequences. Recheck recorded expiry (2026-10-16 13:48:34 UTC). The 240-effect extension
remains separate. Current unfinished create/prepared crashes may require supervised recovery;
automatic continuation is qualified at completed settlements, not every partial prepare.
Earlier review sections below describe historical states, not current deployment instructions.

## Post-Claude review — 2026-10-15

The [Codex review](evidence/automated_launch_2026-10-15/CODEX_REVIEW.md) supersedes older
next-step advice to restore keys and immediately run the automated drill. New cluster
`aae119b9` is funded and halted; the signer failed64 and the gateway remains inactive.
Owner readback confirms both caller keys are in the retired tmpfs directory, absent from
the active paths, with no recovery files printed. More materially, the production gateway
still completes after one settled effect. The review also reproduces stale-candle trading
and an order-cap exit conflict, and identifies the missing pre-expiry drain guarantee.
Resolve these correctness issues and qualify the multi-effect lifecycle before the live
drill. No services, funding, credentials or production code changed in this review.
See [Claude's handoff](handoffs/AUTOMATED_LAUNCH_HANDOFF_2026-10-15.md) for the recorded
owner decisions and deployment pins; its startup continuation is subject to this review.

Written 2026-10-07 after the owner said the paper strategy looks good and asked for a plan to promote
it. **Live trading is not approved by this document.** It orders the work, says what is done, and
names the owner decisions. `LIVE_TRADING_JOURNEY.md` remains the step-by-step gate list; this file
maps the checkpoint review (`docs/evidence/stochastic_cash_2026-10-07/CHECKPOINT_REVIEW.md`) onto it.

## Where the strategy stands

**Post-live learning artifact requested by the owner (2026-10-13):** after the
live-trading milestone, create an end-to-end networking explainer of the actual
deployed system. Cover physical hosts/processes, Tailscale/IPs/ports, Aeron data
and control paths, Raft replication and acknowledgments, gateway/venue HTTPS,
gateway/signer Unix IPC, Linux users/permissions/cgroup network filtering,
packet/decision/order/settlement flow, and failure/recovery/troubleshooting.
Explain the problem, intuition and tradeoffs in developer-friendly terms with
diagrams and relevant C++ ownership/concurrency details. Verify the deployed
configuration when writing it. This is deferred documentation, **not a new live
gate**; do not create the full artifact during current implementation work.

`stochastic_1s_cash_v1`: long-only SOL, 0.12 SOL clips, one-second bars, paper venue. Its first
session made 29 round trips in 17 hours and was flat before fees (`RESULTS.md`). The system around
it is working; the economics are not proven:

- The paper fee is 20 bp a side. The finalized October 5 CLI/API-key SOL sell verified about
  20 bp on that route. The October 9 unsigned 1 USDC buy quote also quoted 20 bp;
  no buy was executed, so the settled buy fee and complete round-trip cost remain unverified.
  Historical 100/98 bp quotes describe a different route/time and remain a stress scenario,
  not the proven fee for the intended execution route. See
  `evidence/venue_contract_2026-10-05/SETTLED_SOL_SELL_FEE.md`.
- The paper price is the Binance SOLUSDT close. A live order fills at True Markets' price.
- The paper venue always fills completely at the limit.

So the first thing "live" buys is evidence about real fills and real cost, at the smallest size
(the book, ch. 52: a small live test is an information purchase). It is not expected to make money
at 100 bp a side.

## What must be true before the first real order

**Byreal route qualification (October 13, local implementation):** enum156 is
identified and the separate schema2 decoder/verifier/review policy passes focused
local checks, including the retained real envelope under synthetic offline intent
and time. Conservative capture-specific floor:6,838,907lamports after the full
2,039,280-lamport repayment, zero guaranteed sponsor credit, $1 inclusive debit.
Both successful simulation balances and the adjacent rounding boundary reconcile.
Twenty C++ test groups,52Python tests and29named mutation kills pass; native/Python
journal-domain parity passes. A pinned route-transition proposal preserves owner
accounts, all financial limits and the five-journal recovery rules. No host changes,
commit, service activation, signing or execution occurred. Next: scoped checkpoint,
exact NUC/Pi qualification, stopped upgrade/locked route adoption and fresh launch
review. Installed69679c0 still rejects the new route; continuous strategy trading
is outside the bounded test. See [Byreal evidence](evidence/bounded_launch_review_2026-10-13/BYREAL_RESULT.md).

**Confirmed-fill wiring and locked renewal (October 13, deployed):** qualified
C++ producer 69679c0, collector and 39-panel live dashboard are installed. NUC/Pi
qualification passed 52 native tests,193 Python tests and eight process-drill cases
per host. Locked renewal helper f6d2567 passed 30 renewal/application and 19 credential
checks on NUC1. Both protected callers freshly read the original funded Raft
state at index 6; four gateway journals remain byte-identical and the idle
protected signer journal advanced from ordinal 1 to 2. Same-account bearer handoff
completed and its hash/lifetime verified. Both trading services remain inactive
with persistent startup guards. No service activation, signing or order occurred.

The review expires 23:50:24.625UTC onOctober 13; expiry requires another fresh
capture/locked renewal before launch. Monitoring is healthy, but no live
settlement snapshot/fill has been produced while the gateway is stopped. The
next bounded launch still needs a fresh positive executable receipt floor and
reviewed removal of both inhibition layers. Continuous stochastic trading remains
beyond the one-effect/$1 runtime and needs a reviewed signal-to-swap bridge and
repeated-order recovery/budget controls. See
[renewal deployment evidence](evidence/locked_review_renewal_2026-10-13/DEPLOYED.md),
[telemetry deployment](evidence/live_fill_wiring_2026-10-13/TELEMETRY_DEPLOYMENT.md)
and [native qualification](evidence/live_fill_wiring_2026-10-13/QUALIFICATION.md).

**Live observability and cost review (October 13):** the supervised account review
expired at22:17:27UTC and both services stopped as designed; journals remain idle.
A dedicated Live trading dashboard and read-only local collector are prepared and
validated (26tests,26PromQL queries,3mutation kills, real-file diagnostic). They
separate reviewed snapshot balances from current service status and do not infer
fills/P&L from quotes. Monitoring checkpoint19f67d2 is deployed; Grafana loaded the tab, Prometheus
scrapes it without errors, and all existing dashboard panels were preserved. Next
trading step requires refreshing against the existing opening/journals, a fresh
positive executable receipt floor and the separately reviewed one-$1buy. The
strategy's fee-only results must be re-evaluated with the observed native-output
deduction; the conservative MVP cap is not an expected fee. See
[live observability and economics](evidence/live_observability_2026-10-13/README.md).

**Owner-requested live entry/exit graph:** the Live tab contains a prepared
green-buy/red-sell chart with an explicit NOT CONNECTED indicator. Finalized,
Raft-committed settlement amount/time telemetry still needs wiring from the
production gateway. The local lifecycle journals cannot supply fill prices.
Do not treat this chart as operational until that producer is qualified; no
quotes, deposits or paper fills may be substituted. This is an observability
deliverable, not an extra live-trading gate.

**Reviewed funding/start completed (October13,18:06 EDT):**
Owner request4 assigned caller22 gateway at index5; request5 initialized the real
account opening at index6/term5/state_version5. Managed balances40.006989USDC and
0.040764166SOL, opening quote mark4.353100USDC, total purchase cap$1, explicit fee
cap$0.002 and realized-loss cap$0.50. Trading stays globally halted; no Intent,
preparation, signing or execution occurred. All three voters are up, one cluster.

Signer and gateway are active/running under their separate UIDs, with exact
qualified native bytes and zero restarts. All five journals are initialized once,
idle/effect0 at index6. The first-use flag has been removed from future launches.
Ceremony helper checkpointd63f93d passed31local tests,9NUC tests and3named mutation
checks. No C++ image rebuild, voter restart or journal reset. **Review expires
22:17:27.845UTC(18:17EDT): services stop then; a later review must bind the existing
opening/journals.** Next is a separately reviewed fresh single-$1 buy, not a
strategy launch. [Evidence and exact command record](evidence/live_readiness_2026-10-09/funding-start-20261013/README.md).


**Fresh account/profile adoption completed (October 13, 17:41 EDT):**
The account/route review, canonical offline opening and protected credential
adoption are complete. Balances remain40.006989 USDC and 0.040764166 SOL; final
history reports no blocking orders. The whole-position unsigned sell quote marked
opening SOL at 4.351947 USDC; no sale occurred. The fresh captured $1 buy passed
the production verifier offline with a synthetic intent: 1,000,000 USDC atoms debit
including 2,000 fee atoms, conservative net minimum 6,819,647 lamports, zero sponsor
credit. That captured payload is not a current execution instruction.

Installed NUC release 5cdb5e1 fixes the Python wallet/API-key and incarnation/digest
mismatches and adds dormant account adoption. Its gateway bytes are unchanged from
qualified 13cc98b (NUC/Pi 52 selected C++ tests plus prior native component/drill suites).
The local credential exporter is bb16daf, fixing a CLI argument typo discovered
before SSH; the initial failed transfer moved no credential. 42 service, 6 adoption
and 8 exporter tests pass on Mac/NUC; 11 local handoff tests and 6 named mutations pass.

Both service principals completed authenticated voter reads: term 5, leader 2,
applied index 4, state version 3, unchanged reviewed halted state digest. Independent
post-adoption checks confirm root-owned account hashes, exact bearer/public-key
pins, role-owned 0600/tmpfs credentials and denied cross-role reads. Both services
remain inactive/dead/static; launch_authorized=false, recovery directories empty.
No ledger funding, service activation, signing or fund movement occurred.

| Remaining step | Current evidence/status |
|---|---|
| Qualified runtime and dormant installation | Complete. Installed release 5cdb5e1 uses the unchanged qualified 13cc98b gateway binary; no C++ rebuild claimed. No voter, exporter or monitoring restart. |
| Protected credential handoff and delayed recovery | Real credentials installed and verified in isolated service-user tmpfs. Temporary owner copies removed. Gateway-only reconciliation and bearer rotation were qualified in 13cc98b. |
| Fresh account/route review and canonical opening | Refreshed and committed at index6. Review expires2026-10-13 22:17:27.845UTC. Later review must preserve this opening and journals. |
| Replicated opening and startup ceremony | Complete: both services verified active/running, five idle journals, first-use flag removed. Trading remains halted; no Intent or order. |
| Supervised single buy | Not started. Exactly $1 total USDC debit, effect 1, repayment ≤ 2,039,280 lamports, zero sponsor credit. Obtain a fresh positive net floor; no automatic retry, resize or second trade. |

[Fresh verification, installation evidence and commands](evidence/live_readiness_2026-10-09/fresh-adoption-20261013/README.md).
Earlier status paragraphs below are historical where superseded by this table.

Last updated 2026-10-13: **single-cluster cutover and authenticated production-user
Raft reads passed**. New incarnation `b79e65be1ea14e219016ec48ea02efef`, qualified
production C++ `86f4408`, all three voters ready at term1/index3, leader3. Both
UID29001/caller22 and UID29002/caller23 verified the same state digest using
separate keys and durable sessions. Production services are installed but inactive;
launch authorization false. Cluster is halted, catalog-only and unfunded. The
archived paper strategy is stopped; historical funding, role assignments and loss
limits were not copied. See [cutover result and commands](evidence/live_readiness_2026-10-09/service-account-20261013/setup-r6/README.md).

Historical native qualification: `86f4408` passed NUC1/Pi qualification (199
unchanged cases retained, two corrected fixtures each passing three repetitions,
44 account checks and schema/process oracles per architecture). Production ELF
hashes remain unchanged. Details below describe earlier dated states unless
explicitly superseded by the current paragraph above.
Owner authorization update: POST methods are approved; moving money still requires
explicit approval. This supersedes the original GET-only method restriction and
does not authorize live signing, execution, broadcast or automatic promotion.

October 13 service/account batch: **preparation verified, deployment still open**.
Added disabled distinct-UID service review profiles and tested protected
credential-descriptor mechanics (8 tests; 5 profile mutation checks), and reran
all 44 account tests. A fresh authenticated setup/balance read and an 18-request
bounded history capture reconcile 0.031896934 SOL and 1.006989 USDC, with no
holds or listed unresolved orders. The new unapproved opening proposes managing
the USDC and leaving existing SOL outside; no ledger was initialized. ADR 0019's
original `PrivateNetwork=yes` conflicted with the signer's independent Raft transport;
the owner subsequently approved voter-address-only connectivity and ADR 0019 is
amended. Fourteen service/network-probe tests pass. The original native enforcement test was
partially observed, not qualified: the owner-run test under verified administrator
`vishal` reached all three voters and received EPERM for both excluded UDP
destinations; excluded TCP timed out instead of the probe's required permission
error. That original run remains recorded as failed; the corrected successful run
is documented below. The earlier attempt under
`velocitas` was denied by sudo policy before the probe started; that account's
permissions remain unchanged.
Supervisor mapping is now implemented: hash-pinned root-owned configuration,
derived argv, UID/GID/groups, private directories, account links and protected FD
handoff. The revised TCP test uses a controlled loopback listener and before/after
control services, so a timeout alone cannot pass. **26 synthetic tests pass on
Mac and NUC1; 5 mutations fail named assertions.** The owner ran the revised privileged probe under `vishal` from
`swap-services-20261013-r3`: **network_policy_passed=true**, run
`1791885226758329135`. Saved phase records and matching source hashes were
independently checked: both controls succeeded, restricted voter connections
succeeded, excluded TCP produced no controlled-listener connection, and excluded
UDP returned EPERM. Aeron, production UIDs and live admission remain false. Actual fixed UID/custody, native Aeron/IPC and deployed isolation qualification
remain in items 9–10. See [current wiring and owner command](evidence/live_readiness_2026-10-09/service-account-20261013/setup-r3/README.md).
No host services or credentials were installed; no live
gate changed. See [service/account evidence](evidence/live_readiness_2026-10-09/service-account-20261013/README.md).

October 13 production-user follow-up: **host setup and bounded communication
qualification passed**. Permanent nologin users 29001 (gateway) and 29002
(signer/socket group) and private state/credential directories are provisioned.
The owner authorized sudo for this work; no credential value is retained in this
tracker or evidence. The pinned image userspace is installed root-owned after a
22,392-entry content-manifest check. This resolves the observed Ubuntu 26.04
binary versus Ubuntu 22.04 host GLIBC/GLIBCXX mismatch without a host upgrade.
Both bounded transient services passed actual Aeron UDP delivery and native
signer IPC under those real host UIDs, including synthetic sign-once/retrieve
and wrong-user/key refusal; run `21a47de2dd7626ee`. Both services exited and were
collected. **64 checks pass on Mac and NUC1.** Existing qualified C++ objects
were reused; the only new compiled artifact is the isolated synthetic helper.
Authenticated reads from deployed Raft voters are not covered by this local
transport drill. Production units, real custody, reviewed live account/config
artifacts and live activation remain open; no credentials, journals or funds
were adopted, no production binary/image was replaced, and the existing cluster
was untouched. The service helpers and retained host evidence are checkpointed at `92abb5f`. See
[evidence and exact commands](evidence/live_readiness_2026-10-09/service-account-20261013/setup-r4/README.md).

October 13 installation/read follow-up: the account/caller review found that the
currently deployed `884816d` voters accept reads only from gateway caller 22 and
use snapshot/command schema 11, replication epoch 1. The qualified `86f4408`
release uses schema 12, epoch 2 and supports independent signer caller 23.
Persisted static identity rejects an in-place image restart; no supported state
migrator exists. Authenticated production reads remain **blocked on a reviewed
single-cluster replacement**, preserving/archiving old paper state before a fresh
incarnation. The bounded read helper never borrows gateway identity for the
signer. Dormant installation supports explicit null account pins only while
launch authorization is false, and role credential binds source private tmpfs.
Production trading and any paper-state reset remain unauthorized by this batch.
See [installation and read review](evidence/live_readiness_2026-10-09/service-account-20261013/setup-r5/ACCOUNT_CALLER_REVIEW.md).

October 13 dormant installation completed at **45db6a6**: both production units
are loaded/inactive/static, manifest admission false, role tmpfs directories empty.
Exact qualified gateway ELF reused; no production C++/image rebuild. Native test
review exposed and fixed FD3 close-on-exec handoff; **38 focused tests pass on Mac
and NUC1**, with retained earlier failures and named mutation proof. A separate
schema12 read helper compiled cleanly. Existing three paper voters/exporters and
gateway remain active; edge remains previously inactive. Account opening/custody
and authenticated deployed reads remain open. Read verification requires owner
approval of the [single-cluster replacement scope](evidence/live_readiness_2026-10-09/service-account-20261013/setup-r5/CUTOVER_SCOPE.md).
See [final installation evidence](evidence/live_readiness_2026-10-09/service-account-20261013/setup-r5/README.md).

October 13 cutover authorization: owner approved archiving the current paper
incarnation and replacing it with **one** schema12 cluster for bounded read
qualification. New incarnation `b79e65be1ea14e219016ec48ea02efef`; qualified
production C++86f4408. Replacement images package exact previously qualified
binaries, with no C++ rebuild. No funding or strategy restart is part of this
step. Old edge/gateway/observer will be persistently masked during qualification;
all old state and unit bytes remain archived for rollback. See
[approved scope](evidence/live_readiness_2026-10-09/service-account-20261013/setup-r6/APPROVAL.md).

October 13 cutover completed: old state and units are hash-verified and archived
on all three hosts; startup is now `recover`. Mac ceremony committed only kill
epoch1, executor generation1 and the existing catalog. Authenticated read run
`782b102342c52aa6` passed under both service UIDs: epoch2, term1, leader3,
index3/state-version3 and identical decoded state digest. Two earlier attempts
refused unsafe sandbox `/run` ancestry before session advancement; both are
retained. Explicit root-owned read-only runtime-parent binds fix the host's
legacy API-VFS behavior, preserving tmpfs Aeron children and all C++ guards.
21 focused tests and three named mutation kills cover the final correction
`ffbb71a`. Dormant production units use the same corrected mount arrangement.
Monitoring uses the new read-qualification identity and `strategy=none`; trading
panels explicitly describe the inactive, unfunded state. No venue credentials,
live opening, policy, risk limits or trading role assignments were installed.

Archived paper deployment: `884816deae1c960c9bd6fca18e7a5ff26b15359f`; dated runtime observations appear below.
The owner explicitly authorized this deployment and execution of the ceremony from the Mac.
Status words below refer to implementation milestones. Historical paper deployment
claims do not imply activation in the current unfunded cluster. **Done** (verified
implementation; see the current paragraph for what runs now),
**Built** (tested but not deployed or not enabled for the intended check), **Open**.

| # | Requirement | Status | Where |
|---|---|---|---|
| 1 | The edge cannot freeze or go blind (review A1, A2) | **Implemented/previously deployed**; current edge is masked/inactive for read qualification | `0063c86` |
| 2 | The market-data allowlist is enforced (A3) | **Done** 2026-10-08 | `0063c86` |
| 3 | The archive cannot silently fill (B1) | **Done** 2026-10-08: fewer commands, usage metric, alerts at 70% and 90% | `d542601`, `9dcca2c`, `3264351` |
| 4 | The kernel checks every fill's price and fee and owns cash, P&L and a loss limit (ADR 0021) | **Implemented/previously deployed**; old paper run used a $10 loss limit. Current fresh cluster has no account or installed limit | `4c04f75`, `f55209e` |
| 5 | Snapshot room (ADR 0022) | **Partly done**: policy table cut to 8 and running. Receipt table change waits for the week of measurement (from 2026-10-07) | `d8ad46d` |
| 6 | Callers are authorized by role (ADR 0020) | **Schema12 code deployed**; gateway22/signer23 registered for authenticated reads. No trading role assignments installed in the new cluster | `11ad1ae` |
| 7 | An operator flatten path | **Done** 2026-10-10: reduce-only/flatten capability deployed; held-position host drill remains item 16 | `04b474b` |
| 8 | An operator command to resolve an order in conflict | **Done** 2026-10-10: operator conflict-resolution capability deployed (ADR 0024) | `9f96cf3` |
| 9 | The signer (ADR 0019 section 1) | **Host-user communication qualified; open for live**: permanent separate UIDs and pinned runtime installed; native Aeron and protected signer IPC pass under these UIDs with synthetic sign/retrieve and wrong-user/key refusal. Protected descriptor startup is wired and tested. Real payloads still refuse. Production units/release installed inactive at45db6a6; authenticated deployed-Raft reads now pass under both UIDs. Real custody/account pins and multi-trade rearming remain open | `evidence/live_readiness_2026-10-09/RUNTIME_INTEGRATION_2026-10-12.md` |
| 10 | The venue transport | **Native-qualified components; production services inactive**: bounded prepare/sign/execute/recovery coordinator, concrete one-shot execution HTTPS and finalized RPC adapters compile in the optional gateway build. Thirty selected Release checks pass. Real-contract startup gate stays closed; no fixture macro enters that library. Integrated86f4408 native qualification is recorded; no complete admitted live route or real-money execution is claimed | `evidence/live_readiness_2026-10-09/RUNTIME_INTEGRATION_2026-10-12.md` |
| 11 | The venue contract questions | **Fee inclusion observed; net-receipt admission open**: two unsigned simulations completed. The captured buy debits 1 USDC total (0.998 principal + 0.002 fee). Raising quoted output triggers Jupiter's slippage rejection, but exact floor rounding is not exposed. Original simulation credits 0.009410957 SOL from swap yet only 0.008860117 SOL net to owner: fixed sponsor return exceeds actual account funding by 550,840 lamports. This captured transaction does not meet the owner-directed sponsorship/net-receipt requirement; no limits relaxed or transaction admitted | journey step 3; `evidence/live_readiness_2026-10-09/ROUTE_A_SIMULATION_RESULT_2026-10-12.md` |
| 12 | The paper venue can reject, partly fill and time out | **Built and deployed**, faults off: reject/partial-fill/timeout flags available; explicit fault rates and drills still pending | runbook 5.2.1 |
| 13 | Decide and fill on the venue's own price, with cost measured on both sides | **Partly done** 2026-10-09: the one-second timestamp error is corrected locally; the same history window now peaks at zero shift (correlation 0.551), not a verified 1–2 s lag. Open: executable prices, measured buy fee, and route-specific reconciliation. A 20 bp executed sell does not validate the buy side | `docs/evidence/venue_price_basis_2026-10-09/` |
| 14 | Startup balance reconciliation against the venue | **Local recovery/accounting extended; live open**: native and classic-token lifecycle replay accounts for temporary WSOL and deposit control. Authenticated order lookup and finalized RPC are composed. Supported successful receipts now produce a private proof in local tests; the fresh TM buy remains economically ineligible. Supported finalized sponsor-paid failures now admit zero-owner-charge settlement locally using an explicit sponsor pin. Charged/unsupported failures retain exposure. Authenticated main-wallet/user checks and a separately hash-pinned opening/account review are wired locally. Fresh exact-atom balances must match managed funds plus reviewed outside activity; startup never imports cash. Recovery-only mode cannot create/sign/execute. Bounded current-owner/current-USDC-account history capture now passed; closed historical accounts and independent provider completeness are not claimed. Actual owner-reviewed live artifacts remain open. Verified live opening balances must come from TM, never paper cash | `evidence/live_readiness_2026-10-09/SETTLEMENT_ADMISSION_LIFECYCLE_2026-10-12.md` |
| 15 | Outside heartbeat armed | **Open**: October 13 read-only check under velocitas on nuc1 confirms timer installed/active, but `/home/velocitas/.config/velocita/heartbeat-url` absent. Owner confirmed October 13 that no Healthchecks check has been created; therefore no URL exists to install. No external delivery verified | `evidence/live_readiness_2026-10-09/OPERATIONAL_RECOVERY_2026-10-13.md` |
| 16 | Drills while holding a position: kill, leader loss, gateway restart | **Eight separate-process cases and native checkpoint qualification recorded; deployed held-position qualification open**: actual gateway/signer process deaths preserve holds and prevent duplicate sends. New F07 initially exposed a prior-term commit gap after leader loss; a type103 current-term read barrier now resolves it without weakening the durable watermark. Epoch2 prevents older replication binaries from sharing this identity. Earlier combined tests missed this timing | `evidence/live_readiness_2026-10-09/PROCESS_DRILL_2026-10-13.md` |
| 17 | A live gateway refuses an account with no loss limit | **Implemented/previously deployed** with --require-loss-limit in paper. Current fresh cluster has no funded account or loss limit; this is not live-venue validation | `989fe04` |
| 18 | Leader steps down when it cannot reach a quorum | **Done** 2026-10-10: quorum-loss step-down implementation deployed; held-position drill remains item 16 | `20329b6` |
| 19 | Raft library assertions active in release builds | **Done** 2026-10-10: release images include active Raft library assertions | `83a103e` |
| 20 | Review of the files the kernel review did not read (node loop, write-ahead log, archive store, codecs, authentication) | **Done** 2026-10-09: node loop, authentication, write-ahead log, archive store, protocol, codecs, Aeron transport and the skipped kernel ranges read. The second pass's findings are plan items 22 to 24; the third pass found no defect | `REVIEW_2_NODE_AND_STORAGE.md`, `REVIEW_3_CODECS_AND_REMAINDER.md` |
| 21 | Review of the research and backtest code | **Partly done** 2026-10-09: the running rule replayed on a week and on 30 days of data with the live code. The diagnostic observed near-zero gross P&L; it omits funded-cash/loss gates and marked inventory, so its turnover is not feasible $25-account turnover. Full research review and promotion validation remain open | `docs/evidence/stochastic_rule_replay_2026-10-09/` |
| 22 | A leaked or faulty key cannot fill the log: per-caller rate limit at ingress | **Done** 2026-10-10: ingress rate 6/s and burst 60 configured on all three voters | review 2, finding 1 |
| 23 | A voter survives a crash or power cut without a person: archive leftovers and a torn last log block | **Open; bounded recovery built locally**: verified publication temporaries are preserved in quarantine; nonzero partial WAL tails and data after zero padding now refuse. Arbitrary torn records and unreferenced final segments still require operator recovery; no guessed truncation | `evidence/live_readiness_2026-10-09/OPERATIONAL_RECOVERY_2026-10-13.md` |
| 24 | The log writer thread sleeps when idle | **Done** 2026-10-10: idle log-writer sleep implementation deployed | review 2, finding 3 |
| 25 | A reconciliation that agrees with a finished order does not stop the gateway | **Done** 2026-10-10: gateway reconciliation correction deployed; current paper book reconciles | `docs/evidence/gateway_stop_2026-10-09/` |
| 26 | The gateway never acts on a state older than a result it has seen committed (the cause of the 2026-10-09 stop: a leader with a slow disk served a state read from before a fill its followers had already answered) | **Done** 2026-10-10: committed-result read fence deployed in voter and gateway; regression tests passed on all three hosts | `docs/evidence/gateway_stop_2026-10-09/` |

Count of the 26 table entries: 15 done (1–4, 6–8, 17–20, 22, 24–26),
4 built or not enabled (9, 10, 12, 14), 3 partly done (5, 13, 21), and 4 open
(11, 15, 16, 23). Deployed capabilities do not close the live contract,
signer/TLS, venue reconciliation or held-position drill gates.

**Next iteration — compiler warnings (owner requested October 13):** retain a deduplicated inventory from both native builds, then fix all reported warnings so future build logs are clear. Keep project and dependency/toolchain warnings distinct; do not suppress diagnostics or change the checkpoint during qualification. Inventory: `evidence/live_readiness_2026-10-09/native-integrated-a890307-20261013/WARNINGS.md`. This is a cleanup task, not an added live gate.

### Remaining work, in practical order (October 13)

**Owner-requested deferral, October 13:** move the fresh TM transaction economic
check to the end of engineering/paper qualification. This includes the observed
550,840-lamport sponsor-repayment discrepancy and the exact enforced minimum
net receipt/rounding. Neither is verified, waived, or accepted as a cost. The
production live-contract gate remains closed; resolve both before real signing
or a money-moving test. This supersedes the October 12 investigation priority,
not its retained evidence. The established $1 order-size minimum is separate.

The groups below reorganize existing tracker items, rather than adding gates.

| Priority | Tracker items | Remaining work / current boundary |
|---|---|---|
| 1 | 9, 10, 14: account and recovery completion | Qualify protected real account/session custody and exact-atom opening balances from TM. Supported sponsor-paid failed-transaction accounting now passes locally; charged/unsupported failures retain holds. Local startup consumes exact opening/account pins and bounded attributed outside records. The bounded current-account capture has passed; finish actual owner-reviewed funding/release artifacts and retain its explicit history/exclusivity limitations. A balance match alone is not proof of absent offsetting activity. Successful receipt admission, signer recovery and setup checks already pass locally; do not rebuild those components. |
| 2 | 9–10: review and native qualification | **Native/offline qualification complete** at `86f4408` on NUC1/Pi. The full-suite baseline covers 199 unchanged tests; two corrected native fixtures each pass three repetitions, with 44 account checks and independent oracles. Hash comparison confirms only the two test executables changed. Earlier failures remain preserved. Owner-authorized maintenance released sufficient memory; the original cluster has recovered without deployment/reset. Distinct production UID/custody and deployed whole-route qualification remain in the existing groups below. See `native-integrated-a890307-20261013/RESULT.md`. |
| 3 | 12, 16: whole-executable failure drills | On the exact release, test lost responses, crashes, leader/quorum loss, restart and held-position kill/flatten; test paper fault modes within their supported semantics. Verify retained exposure, no duplicate sends, and correct settlement/recovery. All eight local separate-process scenarios now pass, including F07 after the approved current-term read-barrier fix. Preserve the exact source/evidence checkpoint; native transport, distinct-UID process isolation and deployed held-position drills remain. |
| 4 | 9–10, 12–16: paper qualification | Deploy the integrated executable in paper mode under separate deployment authorization, one cluster only. Verify opening balance, decisions, fills, recovery, Grafana and alerts. Paper qualification must not bypass the real-signing gate. |
| 5 | 15, 23: operations | Heartbeat URL is absent on the verified deployment account and must be supplied. Bounded temp quarantine/strict WAL recovery now pass locally; ambiguous torn blocks and unreferenced final segments still need operator recovery. October 13 restoration verified all three voters ready, one leader and matching durable/commit/applied state. Strategy edge already stopped after feed timeouts on October 12; decisions have not resumed. |
| 6 | 5, 13, 21: capacity, price basis and strategy review | Complete receipt/snapshot capacity measurement, executable-price integration and funded-account/backtester review. Review ongoing strategy sizing/rearming before unattended trading. Profitability is unproven; engineering test approval is distinct from strategy promotion. |
| Final live gate | 11, 13–14 and journey gates | Return to TM-supported funding/repayment and exact minimum net receipt. Review exact enforceable purchase/loss bounds, current account funds and recovery; obtain explicit approval for the specific small live test. Settled buy and round-trip cost measurement require an approved money-moving test and cannot be claimed from unsigned simulations. |

After the applicable engineering gates, prepare the exact owner-reviewed live
test, build/release from committed source and verify its runtime configuration.
The owner's later engineering-test bounds are **$5 maximum total purchases
including fees and a $0.50 loss stop**; they supersede older illustrative $10
trade sizing in the journey document. They do not authorize a trade, import the
virtual $25 balance, or permit the current 0.12 SOL paper sizing automatically.
The stop's enforceable behavior and limitations must be reviewed in that test.
Strategy-driven unattended live promotion additionally needs its own reviewed
economic results, sizing and operating window.

Header fix status: Python probe and experimental C++ HTTPS reader now identify
the real Velocita client explicitly. Local verification is complete; no venue
order retry, commit, deployment or new live capability followed. The order
endpoint's response after the fix remains unverified.

Next contract check prepared October 11: one unsigned SOL fixed-base limit
request, with eleven offline tests and five named mutation kills. Its purpose
is to determine whether the existing kernel intent can map to this venue route
before proposing different quote-input semantics. The owner approved it; the
tool initially stopped locally on an expired session. The separately approved
refresh and balance GET succeeded October 12 at 02:31 UTC. The resumed check
then stopped at its catalog GET: captured response reports status 403,
Cloudflare error 1010 (`browser_signature_banned`). No order POST, signing,
execution or retry occurred. The attempt marker is retained. This is an access
blocker, not a rejection of SOL limit orders; capability remains unknown.
Result: `evidence/live_readiness_2026-10-09/SOL_LIMIT_RESULT_2026-10-12.md`.
The owner then selected reuse of the verified SOL mapping. A revision now pins
the October 9 catalog hash and checks asset identity locally, removing the
catalog GET. It preserves the same fixed unsigned order body and adds actual
HTTP-status capture. Fifteen offline tests and four named mutation checks pass.
The owner approved the revision. Its single authenticated create-order POST
at October 12 02:54 UTC also returned HTTP 403, Cloudflare error 1010
(`browser_signature_banned`), with no order ID or unsigned payload. No signing,
execution, refresh or retry occurred. SOL limit support remains unknown;
supported API access must be resolved before another network attempt.
Evidence: `evidence/live_readiness_2026-10-09/SOL_LIMIT_PINNED_RESULT_2026-10-12.md`.
Follow-up diagnosis found a probe regression: the new helper omitted the
explicit Velocita User-Agent and sent Python's default, unlike both previously
working helpers and the official CLI. The truthful client header is restored
locally; 16 probe tests and the named header-removal mutation pass. A fresh
balance GET through the unchanged account helper succeeded at 03:14 UTC.
The header omission is a likely 1010 trigger, not a remotely proven cause;
the order endpoint has not been retested. Details:
`evidence/live_readiness_2026-10-09/CONNECTION_DIAGNOSIS_2026-10-12.md`.
The owner approved the corrected-header batch; it ran once October 12 at
14:25 UTC. Session renewal and the balance GET succeeded (0.031896934 SOL,
1.006989 USDC, no holds). The unchanged unsigned SOL limit POST reached the
order handler and returned HTTP 400, `invalid_argument`: `order type not
supported for this asset`. No order ID or payload was returned; no signing,
execution or retry. Access is verified for this request, while the submitted
limit type is rejected. Item 11 remains open for a reviewed execution contract,
payload economics and settlement. Approval is consumed and markers retained.
Evidence: `evidence/live_readiness_2026-10-09/SOL_LIMIT_HEADER_RECHECK_RESULT.md`.
The owner agreed to design around the swap flow. The source-reviewed draft is
`evidence/live_readiness_2026-10-09/SWAP_EXECUTION_DESIGN.md`: a distinct
exact-input operation, funded exact-atom balances, maximum debits/minimum net
receipts and one unresolved swap. It explicitly identifies the change from a
hard lot-position cap to funded-spend control, independent signer authority,
durable effect replay protection and rent/expiry semantics. The owner subsequently
approved the economics and local paper implementation. A test-only
deterministic swap reducer, exact-atom simulated venue and bounded journal/replay
drill are implemented. Tests cover funding, cumulative purchases, net receipt,
fee and inventory bounds, unknown reservations and replay. At that earlier checkpoint, production command/snapshot integration and actual
transaction inspection remained open. The later local integration is recorded below;
the deployed strategy and live capability have not changed. See
`evidence/live_readiness_2026-10-09/SWAP_PAPER_IMPLEMENTATION.md` for scope and checks.
Owner-authorized production integration is implemented locally (October 12):
schema-12 command/snapshot encoding, exclusive exact-atom funding, global controls,
operator-only explicit intents and a paper-only gateway startup path. The local
three-voter drill recovers all voters and the gateway after Attempt and books
one simulated settlement; stale reads cannot repeat it. Full validation evidence
and limitations: `evidence/live_readiness_2026-10-09/SWAP_KERNEL_GATEWAY_INTEGRATION.md`.
Validation: all 173 registered checks passed; integration ASan/UBSan passed;
30 named-assertion mutations were killed across core, integration and balance work.
The retained evidence is `swap-integration-checkpoint-20261012.json` in that folder.
This does not enable autonomous strategy swaps or live sends. No deployment,
commit or host change occurred; schema 11 cannot be mixed with this build.

Parallel item 14 work added a tested exact-atom balance projection with explicit
identity mapping and refusal of pending/conflicted state. See
`evidence/live_readiness_2026-10-09/SWAP_BALANCE_ADAPTER.md`. Authenticated startup
admission, external activity and attributed settlement remain open. Parallel
items 9/11 review identified that idempotent ATA reuse followed by a fixed sponsor
reimbursement can consume preexisting owner rent. The unsupported Jupiter variant
and zero auxiliary SOL cost remain unproven; see
`evidence/live_readiness_2026-10-09/SWAP_TRANSACTION_CONTRACT_REVIEW_2026-10-12.md`.

The next owner-requested parallel batch advanced the existing items 9/11 only:
owned account resolution, exact instruction-shape decoding and their combined
six-instruction synthetic fixture. Nine standalone sanitizer CTest groups and
seven selected root Release groups passed; 30 additional named-assertion
mutations were killed (14 resolver, 16 decoder). No new tracker entries were
added. The source recheck did not obtain an authoritative definition for Jupiter
variant 161; whole-transaction economics and rent attribution remain unresolved.
This code is not linked into production or the signer. No commit, deployment,
host change, account/venue/RPC request, signing or trade occurred. See
`evidence/live_readiness_2026-10-09/SOLANA_INSPECTION_CHECKPOINT.md` and its pinned
`solana-inspection-checkpoint-20261012.json` evidence. Item 14's authenticated
integration remains open and was not modified in this batch.

The owner-requested A/B/C foundations batch is now implemented and reviewed
locally. A single specifically approved public metadata read returned the
program-owned Jupiter IDL (197 variants), resolving the missing route layouts.
An initial wrong Anchor discriminator in the Python reader was fixed and the
saved response decoded offline; no second request occurred. A bounded C++
data-only decoder, exact balance/read-context reconciler and opt-in HTTPS
client are registered in the small standalone test build. Thirteen standalone
CTest groups passed across initial/targeted runs; five selected root Release
groups passed; forty named mutations were killed. Fake lost-response,
restart/late-settlement and inflight-stale-read cases, plus native dependency
verification preparation, are reusable for the later batches. No production
kernel/gateway changes, commit, deployment, host change, signing or trading
occurred in this batch. Existing items 9–11/14 remain open at the integration
and economic-authority boundaries, not because the local helpers are absent.
Evidence and remaining work: `evidence/live_readiness_2026-10-09/ABC_FOUNDATIONS_2026-10-12.md`.

Original protocol and exact body:
`evidence/live_readiness_2026-10-09/SOL_LIMIT_PROBE_PROTOCOL.md`.
Repeatable local build, tests, commands and current host verification:
`evidence/live_readiness_2026-10-09/READINESS_BUILD_2026-10-11.md`.

The schema-11 deployment and six-command ceremony completed on October 10.
All six commands applied once and were verified replicated before paper activation.

## What is running now

Latest read-only check: **2026-10-11 05:25 UTC** (~24.3 hours into the run).
All three voters ready, failed=0, Pi/node 2 leader at term 5; all agree on
current/commit/applied/durable 6672. Paper services active, deployed image IDs
unchanged, submitter errors 0, reconciliation matching, new-risk stop 0.
89 fills / 44 completed round trips; open position 0.12 SOL, cash $9.509094,
charged fees $2.350906 and gross executed volume $1,175.4336. At the sampled
$109.66 mark, equity is approximately $22.668294 before a future exit fee.
Fees remain the dominant loss; this is not evidence of alpha.
Prometheus reports **no active alerts at 05:29 UTC**. The four earlier warnings
were repaired in the preceding continuation (details below); the silent HYPE
order-book subscription remains a separate unresolved feed capability.
No service/control changes made in this continuation. Evidence:
`evidence/live_readiness_2026-10-09/readiness-build-status-{nuc1,pi,nuc2}-20261011.json`
and `readiness-alerts-20261011.json` in the same folder.

Incarnation `348e0b2cd4f639ff404df4eeadee0ee5`; fresh paper account started
2026-10-10 05:07:06 UTC (01:07:06 EDT).

- Source commit `884816d`. Voters, exporters and gateway use
  `localhost/velocita-v2:roles-884816d`; edge and observer use
  `localhost/velocita-v2-shadow:paper-roles-884816d`. Running image IDs match the manifest.
- Exactly one voter/cluster per host. At initial activation, all voters were
  ready, failed=0, term 1, leader 1 (nuc1); initial verification snapshot
  current/commit/applied/durable 19. Current state is reported above.
  All paper services and exporters active. Future voter starts use recover.
- Same stochastic_1s_cash_v1 strategy, one-second bars, 12-lot (0.12 SOL)
  clips, fresh virtual $25, actual paper fees 20 bp/side, risk fee cap 30 bp,
  $10 kernel loss limit, no timer deadline. Existing cash/capacity/risk stops apply.
- Feed connected, decisions committing, submitter errors 0, ledger readable,
  reconciled=1, gateway new-risk-stopped=0. Initial state was flat with $25. The first natural paper buy subsequently
  filled: 0.12 SOL at $109.91, fee $0.026379, cash $11.784421. Reconciliation
  matches and no new-run alerts are active. Observer ledger mount is read-only.
- Prometheus and Grafana restarted with new-run selectors. All five relevant
  scrape targets are up; Grafana database health is OK. Verified through host
  terminals, without browser sharing. No live venue was contacted.
- Old run archived on each host at
  `~/velocita-indicator-paper/retired/stochastic-abd1e149-20261009`, including
  data, configuration and old units. Old account: $22.314889 cash, flat,
  $2.533911 fees; current/durable 20880 versus commit/applied 20876. Its four-entry
  uncommitted tail is preserved. No old/new ledger overlap.

Deployment evidence: `evidence/live_readiness_2026-10-09/cutover-20261010/`;
current operational record: `release-884816d/deployment.json` in that evidence folder.

## Order of work

Current release checkpoint (updated as milestones complete):

| Step | Verified state | Next action |
|---|---|---|
| Committed release source | `884816d`, two isolated test-harness fixes; production code unchanged. Shared dirty hunks excluded | Deployed from this committed archive |
| nuc1 native release | 884816d complete: 145/145 passed, 372.42 s; voter and shadow image/source/toolchain identities verified | Deployed; running image verified |
| nuc2 native release | 884816d complete: 145/145 passed, 293.76 s; source/toolchain/image verified | Deployed; running image verified |
| Pi native release | 884816d complete: 145/145 passed, 453.10 s; image 6624394a0083, source/toolchain verified, both exits 0. Build recovery verified; new voter/exporter subsequently deployed | Deployed; running image verified. Preexisting tagged images preserved |
| Deployment bundle | Previous a073998 bundle blocked. Replacement in `evidence/live_readiness_2026-10-09/release-884816d/` pins all native images, source, Mac client and 12 prepared deployment files | Staged and activated; hashes and runtime identities verified |
| Monitoring preflight | Real promtool validates config and 57 rules. Eighteen macOS metadata files per host were preserved outside the mounts after one matched the YAML glob | Prometheus/Grafana restarted; all five paper/cluster targets up |
| Fresh paper cluster | Incarnation `348e0b2cd4f639ff404df4eeadee0ee5` running on all three hosts | Complete; old state archived, recovery mode set |
| Owner ceremony and paper strategy | Six commands applied once from Mac under explicit owner authorization; fresh $25 paper strategy active at 20 bp per side | Observe natural trades; held-position drills remain |
| Live trading | Disabled; no new live trade | Complete contract, inspecting signer/TLS, balance reconciliation, held-position drills and owner live gates |

Exact image identities, checks and commands are in
`evidence/live_readiness_2026-10-09/release-884816d/build_status.json`,
`evidence/live_readiness_2026-10-09/CUTOVER_STATUS.md` and
`evidence/live_readiness_2026-10-09/COMMANDS.md`.

Failure investigation: the signer test's concurrent reads of plain counters
are confirmed by TSan. Joining before inspection passes both isolated TSan
checks and the complete Pi signer suite; removing the joins restores the races.
The unchanged gateway binary also reproduces alone. Instrumentation identifies
twenty immediate `persistence_busy` refusals; the test-only 500 ms retry backoff,
matching the production client's pacing, now passes the full forty-trip case.
Kernel OOM logs identify the process timeout: the 1.5 GiB build/test cgroup
killed a test voter. Two CPUs did not help; 2 GiB progressed further but also
caused an OOM kill. A bounded maintenance test temporarily stopped only the
Pi production voter/exporter, with both NUC voters retaining quorum. With
3,345,824 KiB available, tests received 2.75 GiB and the full process suite
passed without new OOM kills. The Pi voter/exporter restarted and rejoined
term 49, leader 1, epoch 2; the build cap is restored to 1.5 GiB. No incarnation,
ledger or protocol deadline changed. The complete gateway suite with the
backoff fix passed all 28 cases. The full signer and process suites also pass.
After the owner's continuation, the two test-only fixes were committed as
884816d, staging only the prepared isolated patches. Unrelated gateway test
hunks remain unstaged. New release source hashes match on all three hosts:
1,565 tracked files plus SOURCE_COMMIT. Those rebuilds and the paper cutover
subsequently completed; the verified release results are in the table above.
These earlier isolated diagnostics did not by themselves certify the release.
Details and limits: `evidence/live_readiness_2026-10-09/ARM_RELEASE_FAILURES.md`.

1. ~~Finish and deploy the kernel change (items 3, 4, 5).~~ Done 2026-10-08.
2. ~~Roles, flatten, conflict resolution, leader step-down, assertions (items 6, 7, 8, 18, 19).~~
   Built 2026-10-09 as one format change (schema 11); deployed 2026-10-10.
3. **Make paper honest about the venue (items 12, 13).** A paper venue that can refuse and partly
   fill, a True Markets reference price for the decision, and the measured fee. This is where the
   strategy's real economics will show, still with no money at risk.
4. **Signer and transport against mocks (items 9, 10).** Everything that can be built without a
   credential or a venue call.
5. **The remaining reviews (items 20, 21).**
6. **Venue contract and balance reconciliation (items 11, 14).** Needs recorded venue replies.
7. **Drills (item 16), then the journey's steps 9 to 11.** First controlled trade, canary, review.

Steps 2 to 5 need no credential and no venue call. Step 6 onward does, and each needs its own
explicit approval.

## Owner decisions that block progress

- The Healthchecks URL for the outside heartbeat.
- Whether the walk-forward research result is accepted as the basis for a canary
  (`STRATEGY_PROMOTION_GATE.md`, Stage 1).
- Where the signing credential lives at rest and who may start the signer (ADR 0019).
- Whether True Markets offers a trade-only, no-withdrawal credential. Until it does, a leaked key
  can move the whole account, so the account should hold only the canary capital.
- Wired Ethernet for the three hosts (journey step 2.3). On 2026-10-08 nuc2 dropped off the network
  for about 20 seconds and leadership moved.

## Continuation after the independent review

See `evidence/live_promotion_review_2026-10-09/CONTINUATION.md` for exact changed paths,
checks and remaining boundaries. This checkpoint fixes the cutover's ineffective negated-shell
checks, enforce signer floor/socket ownership, bound socket I/O under one deadline, and
correct close-availability timestamps. These changes are committed in this checkpoint and are not deployed.

The mock signer remains excluded from release images. No real key decoder, live TLS route,
venue contract, new loss budget, or real trading is authorized by this continuation. The
prepared schema-11 ceremony stays owner-run. Build production images from the final committed
revision; do not build the shared dirty working tree. Preserve and archive the existing run
before any approved fresh-incarnation change.


Release supervisor correction (22:18 UTC): the first 884816d nuc1 attempt
reached the process suite after passing 132/145 suites. Its external 1,800-second
wrapper expired (supervisor exit 124, guard exit 143); this is not a failed test
assertion and not a completed release. Full log is preserved. The same committed
source is rebuilding with a 3,600-second outer cap and unchanged CPU, memory,
compiler settings and protocol/test deadlines. Future native build wrappers,
including the prepared Pi maintenance window, use that bounded allowance.

Status check at 22:29 UTC: nuc1 rebuild is active, last observed at compile/link
step 774/1075; this attempt has not entered CTest. No build OOM event. All three
existing voters are ready, failed=0, term 49, leader 1, commit 20876. The paper
edge remains failed/stopped. nuc2/Pi replacement builds have not started; no
cutover or live trading has occurred.


Parallel build update (2026-10-09T22:57:49.207263+00:00): owner explicitly requested the Pi build
alongside nuc2. Both NUC voters were healthy before and after Pi maintenance
started, term 49, leader 1, commit 20876. Pi had 4,221,268 KiB disk free;
stopping its voter/exporter yielded 3,342,504 KiB available memory. The actual
Ninja process is pinned to CPU 3 in the build slice, memory.max=2,952,790,016.
The tested wrapper bounds the build to 3,600 seconds, keeps the disk guard,
and restores the 1.5 GiB build cap plus both old services on exit. The Pi voter
is intentionally down during this window; Grafana may alert. No second
production cluster, cutover, ledger reset or live order was started.


Status check at 2026-10-09T23:32:54.445061+00:00: nuc2 release is complete. Pi is stopped, not
hung: every native suite passed, then the 3,000,000 KiB free-disk guard fired
at 23:22:51 UTC (2,927,744 KiB remaining). No ARM release image exists. The
maintenance handler restored both old Pi services; voter ready=1, failed=0,
epoch=3, term=49, leader=1. All three voters are healthy. No OOM event occurred.
Disk cleanup has not been performed and cutover remains blocked on the image.
Logs are preserved in tests/pi-884816d-capacity-stop.log and
tests/nuc2-884816d-release-build.log under the live-readiness evidence folder.


Pi disk recovery: owner explicitly authorized freeing space and finishing the
build. Four selected untagged intermediate build images were removed after
checking they were neither tagged, ancestors of tagged images, nor referenced
as container base images. Every tagged-image-to-ID mapping is unchanged.
Initial Podman removal also tried pruning an untagged parent; that parent was
referenced by a stopped build container, so Podman refused it. No force was
used. The selected image itself was removed; remaining removals used --no-prune.
Free space rose from 2,917,876 to 5,401,212 KiB. Inventory and exact removal
outputs are in evidence/live_readiness_2026-10-09/pi-cleanup-20261009/.
The old stopped-at-capacity logs and exit files were renamed into
~/velocita-release-884816d/capacity-stop-attempt on the Pi. Retry unit:
velocita-build-884816d-maintenance-r2.service. It retains CPU 3, 2.75 GiB,
60-minute build timeout, disk guard, and voter/exporter restoration on exit.

Cleanup audit clarification: the first removal also pruned one unused untagged
parent, so five image records were removed in total. The referenced parent that
Podman refused was retained. All tagged image identities remain unchanged.
Exact records: pi-cleanup-20261009/removal-audit.json in the evidence folder.


The first cleanup did not provide enough packaging headroom: retry r2 again
passed all 145 suites (444.26 s) but the guard stopped image export at 2,920,800
KiB free. Runtime restoration was delayed while container storage was busy;
then the old Pi voter/exporter were verified active, epoch 4, ready=1, failed=0,
term 49, leader 1. The full log is tests/pi-884816d-capacity-stop-r2.log.
Read-only inspection found an ordinary 632,644 KiB /build tree, no file over
32 MiB in build/tmp/src, and five inactive image-export files in /var/tmp
using about 2.7 GiB. Those files survived stopped builds. After owner-authorized
cleanup of those exact owned files and three failed builders whose source
commits were checked, free space is 7,473,228 KiB. All tagged images are
unchanged. An older 8a15b98 builder was deliberately retained after the source
preflight refused it. Failed preflights performed no deletion.
Inventory: evidence/live_readiness_2026-10-09/pi-export-cleanup-20261010/.
Retry r3 uses the same committed source and standard build-image.sh; launch
required at least 7,000,000 KiB free. CPU, memory, disk and time guards remain.


Pi release completed at approximately 00:45 UTC on October 10 (October 9 EDT).
The r3 build passed 145/145 suites in 453.10 s and produced ARM64 image
6624394a00837ede82cba5d8d83ae08fe5b73ad33f1b4c64bed0fb569b8514e2.
Both build and maintenance exit markers are zero; source 884816d and pinned
ARM toolchain labels match. OOM counters stayed zero; observed build memory
peak was 2,733,084,672 bytes. Packaging briefly delayed an SSH observation;
the independent build continued and completed normally. The old Pi voter and
exporter are active, epoch 5, ready=1, failed=0, term 49, leader 1. The build
memory cap is restored to 1,610,612,736 bytes. Final free disk: 6,823,624 KiB.
Every preexisting tagged image mapping remains unchanged. NUC health checks
at 00:46 UTC also show ready=1, failed=0 and the same leader/term/commit.
The paper edge remains failed/stopped. No cutover, owner ceremony or live
trade occurred. All three native releases are now built; the next deployment
step is the fresh-paper cutover preflight described in CUTOVER_STATUS.md.
Build logs, final verification, image IDs and bundle hashes are preserved in
`evidence/live_readiness_2026-10-09/release-884816d/` and its sibling `tests/`.


Runtime status refresh 2026-10-10T03:51:48.915170+00:00: all three voters ready=1, failed=0,
all exporters active. They agree on term 50, leader 3 (nuc2), commit/applied
20876 and current/durable 20880. Leadership has changed since the previous
snapshot; this check does not establish why. Gateway active; paper edge still
failed/stopped. Pi free disk 6,823,324 KiB. Release build evidence remains
145/145 on each host, all images complete; paper cutover remains pending.


October 10 release cutover: owner explicitly authorized deployment, running
the ceremony from the Mac, and restarting paper trading. Preflight verified
all image and bundle hashes, key-file metadata, flat kernel/venue positions,
no pending reports, matching reconciliation and 57 monitoring rules. The
replacement bundle is staged at ~/roles-348e0b2c-884816d on all hosts. Old
services are being stopped before preserving state and starting the new
incarnation. No live venue is involved. Evidence: cutover-20261010/.


October 10 cutover completed: see `cutover-20261010/DEPLOYED.md` for commands,
verification, archived-state paths and startup observations. The Pi container
creation took about 90 seconds; an early health read timed out. No ceremony
command was issued until all three voters were ready at commit zero. No
voter restart was needed. The owner-authorized ceremony applied sequences
1–6 once, then activation initialized $25, installed the program and started
paper services. No credentials were printed, uploaded or persisted by this
cutover; existing host key files and the Mac Keychain pipe were used.


First natural paper fill: 2026-10-10T05:10:16.396611+00:00; buy 0.12 SOL at
$109.91, fee $0.026379 (20 bp rounded up), remaining cash $11.784421,
position 12 lots, pending reports 0. Subsequent observer checks show matching
reconciliation, ledger readable, no submitter errors and no active new-run
alerts. This validates the paper path only; no live venue trade was sent.

## 2026-10-11 warning repair and live-readiness continuation

**Warning repair complete:** recorder DNS fixed with the active host resolver;
Oct 8/9 Mac archives copied, hash-verified and acknowledged; guarded SQLite
compaction reduced the file from 20.8 GB to 7.36 GB. A tested 15-minute completion
window now bounds the slow-compaction alert without changing its 0.5 s threshold.
Final Prometheus observation has no active alerts. The HYPE orderbook topic is
still silent; recovered reference prices/candles do not qualify that feed.

All three paper voters are ready on unchanged release 884816d, term 5, leader 2.
Paper ledger reconciles, submitter is ready, and no new-risk stop is set. One
leader change was observed during maintenance; its cause was not established.
No voter/gateway/edge/observer restart or ledger reset occurred.

**Local live-readiness progress:** signer refuses multiple payloads before key
use to prevent each spending a full intent; REST parsing can bind an omitted
asset class to explicit catalog identity; exact balances retain residual atoms;
a read-only libcurl HTTPS component passes certificate/redirect/timeout tests.
C++ sanitizer checks and targeted mutations pass. The new HTTPS/balance components
are not in CMake, the gateway or an image. They require dependency review and
integration; none of these checks is live admission or settlement proof.

No commit, private venue read, authenticated venue write, signature, order or key
deployment occurred. Fixed-base versus quote-input execution, route-161 decoding,
real signing and finalized ledger reconciliation remain open. Details, evidence,
exact operational commands and next gates:
[evidence/live_readiness_2026-10-09/CONTINUATION_2026-10-11.md](evidence/live_readiness_2026-10-09/CONTINUATION_2026-10-11.md).

## October 12 settlement and integration checkpoint

The sole newly approved historical transaction read succeeded once; its approval
is consumed. Observed sell accounting agrees with the reported owner balances,
20 bp platform fee after rounding and sponsor-paid network charge. Sponsor close
authority over the new USDC account means its deposit is not owner trading profit.
This does not validate the current buy route or authorize further requests.

Local declared-account arithmetic now distinguishes backing, owner/external rent
and base excluding deposits. Five sanitizer groups and fourteen targeted mutation
checks pass. The authenticated reconciliation session passes seven groups and ten
mutations. Seven selected CTest entries pass, including the real local TLS plus
three-voter WAL-recovery drill and release-boundary guards. Its settlement is
synthetic and its watermark seed is fixture input, not durable production recovery.

No new tracker requirement was added. Existing items 9–11/14 remain open for live;
item 16 has additional local evidence only. Native host qualification is prepared
but not run: checkpoint the relevant source, verify the build-image curl/TLS
dependencies, and run the bounded native qualifier. No deployment, service restart,
new trade, signature or commit occurred in this batch. Commands, exact evidence,
private-response hash and remaining scope are in
[evidence/live_readiness_2026-10-09/SETTLEMENT_INTEGRATION_2026-10-12.md](evidence/live_readiness_2026-10-09/SETTLEMENT_INTEGRATION_2026-10-12.md).


## October 12 native qualification and production recovery continuation

Native HTTPS component qualification is complete on nuc1 and Pi from commit
5131340: ten TLS fixture tests each, with sanitizers and leak checking enabled.
Release images, runtime services and the existing single cluster were unchanged.
See [native qualification result](evidence/live_readiness_2026-10-09/NATIVE_QUALIFICATION_RESULT_2026-10-12.md).

Production recovery integration has passed its local verification: the executable swap
paper branch requires an explicit durable journal; command acknowledgements are
saved before the next action, and the recovered index fences stale initial reads.
Initialization is explicit and restricted to an unused funded allocation. Crash,
write-failure and startup checks pass; nine gateway integration mutations, ten
session mutations and sixteen store mutations fail at their intended assertions.
The standalone recovery core is committed as 6349134 and passes seven native
sanitizer test groups on each architecture. Shared dirty
kernel/gateway files have not been committed wholesale. Live signing, attributed
final settlement and authenticated production reconciliation remain open; a
capture-shape match is not transaction approval. No new tracker gate was added.

Full results and the remaining production boundary are in
[production recovery integration](evidence/live_readiness_2026-10-09/PRODUCTION_RECOVERY_INTEGRATION_2026-10-12.md).
Native component qualification is complete; complete live production integration
is not. The current captured swap still lacks proven transaction-wide admission
bounds, and the real exact-input signer/submission/final-settlement path remains
unconnected. No permission to sign, trade or deploy follows from these tests.

## October 12 parallel production-integration continuation

The owner's original five-step plan is unchanged. Step 1's native component
qualification remains complete for TLS and the earlier result-recovery core.
Step 2 (complete live integration) remains **in progress**, not complete.

Parallel work added an exact external-submission identity journal and tightened
transaction-profile checks. The main agent connected the journal to a test-only
coordinator using the real authenticated `GatewayLink` state reader. It checks
the committed attempted swap, stores the exact execution identity before a
single fixture-port call, and requires reconciliation after every return or
exception. Neither an ambiguous response nor restart releases a hold or resends.
The store's six sanitizer groups and twenty named mutations pass; the session's
nine groups and sixteen named mutations pass. Review found and fixed late queued
state replies bypassing the local timeout, owned-link cleanup on early exit,
and a test assertion that could be swallowed by a port exception handler.

The combined three-voter lost-response/leader-loss/gateway-restart drill passed:
exactly one request was sent and the 1,002,000 quote-atom hold remained intact.
Existing paper WAL-restart and journal-write-failure regressions also pass.
These are test components and synthetic venue behavior, not
step 3 qualification of a committed production live build. No live signer,
credentials, venue POST, deployment or active-cluster change was made.

The unresolved contract is now more specific: TM's documented request fields
do not let us rewrite the returned rent lifecycle or transaction output bound.
The actual shared-route principal/fee/debit and net-output equations must be
verified before the kernel mapping or signer can be enabled. A current account
snapshot cannot prove an idempotent account was absent at transaction execution.
The current captured buy remains refused; neither limits nor source bytes were
relaxed to manufacture admission. See
[transaction bounds and minimum next evidence](evidence/live_readiness_2026-10-09/TRANSACTION_BOUNDS_CONTINUATION_2026-10-12.md)
and [submission recovery](evidence/live_readiness_2026-10-09/SWAP_SUBMISSION_RECOVERY_2026-10-12.md).

Next remains within step 2: close those transaction semantics, implement the
independent exact-input signer and earlier prepare/stamp recovery, and wire
verified account/terminal-settlement evidence. Only then qualify the complete
committed executable, do its failure drills, and deploy one paper cluster.
The later live-test bounds and separate execution approval remain unchanged.

Combined source hashes, logs and scope: [submission integration](evidence/live_readiness_2026-10-09/SUBMISSION_INTEGRATION_2026-10-12.md).

## October 12 production 2a–2d parallel batch

Step 2 remains **in progress**. Two agents and main completed the local stamp,
REST codec and committed-settlement resolution connections. The stamp now owns
the required maximum 375 characters; the REST codec verifies the saved quote's
original payload hash. Independent review exposed and fixed ambiguous nested
payload fields. Historical audited ceiling rounding replaces the old nominal
floor without granting any transaction authority.

The known-signature mapping is now defined internally as SHA-256 of exactly
the ASCII domain `velocita.solana.signature.v1` followed by the 64 raw signature
bytes. The resolver checks this digest against the exact committed settlement;
it rejects an inconsistent legacy/arbitrary digest. A future external settlement
producer must use the same helper. This removes an internal convention gap; it
does not supply external finality or prove the signature cryptographically.

The combined drill now formats a real REST execute body with a synthetic stamp,
then covers lost response, leader change, gateway restart, retained exposure,
known-signature settlement and durable journal resolution with only one fixture
send. Eight selected CTest entries, fifteen build-boundary regressions, and the
four components' 67 named mutation checks pass. Native component results from
earlier commits do not qualify this new uncommitted integrated test build.

Still open within the same 2a–2d scope: current route debit/net-receipt/rent proof;
independent protected signing and replay state; durable prepare/stamp and real
authenticated transport/status orchestration; externally attributed finality
and account-property evidence. The current captured buy remains refused.
No commit, deployment, live request, key use or active-cluster check occurred.

Exact changes, tests, source hashes and commands:
[production 2a–2d checkpoint](evidence/live_readiness_2026-10-09/PRODUCTION_ABCD_2026-10-12.md).

## October 12 signer, prepare and status continuation

The existing step 2 scope remains open, with three lanes advanced in parallel.
The independent signer component now checks authenticated Prepared state and
signs exact owned bytes with a pinned synthetic P256 key, after a separate
durable one-use fence. It is not a production process-isolated signer or a
current-quorum authorization proof. Without the synthetic fixture macro it
refuses before crypto. A lost stamp remains fenced rather than re-signed.

Prepare recovery now saves the exact original unsigned payload and order mapping
before exposing it. The status reader derives its expected quantity from that
saved request, checks both journals and the session across one GET, and durably
retains a canonical transaction signature. `complete` does not resolve exposure.
The extended three-voter drill passed create persistence, one execute, lost reply,
leader loss, restart, status observation with hold retained, and later synthetic
committed settlement. Signer cryptography is tested separately from this drill.

The current buy's rent counterexample is now an exact-atom accounting regression:
9,191,912 quoted output minus 2,039,280 existing owner rent leaves 7,152,632
lamports of net new owner property. This conditional branch invalidates an
unconditional zero-extra-SOL claim; it is not an observed new trade or verified
deployed output guarantee. Debit mapping and enforceable net receipt still need
the authoritative route contract. No limits were relaxed to enable signing.

Component mutation results: prepare 28, signer 19, order status 21, accounting
14; all fail at designated named assertions. Prepare's thirteen groups include
ten actual abrupt process-exit cases. The new code remains local/uncommitted and
outside production targets; no deployment or active-cluster change was made.
Evidence, scope and commands: [parallel continuation](evidence/live_readiness_2026-10-09/LIVE_COMPLETION_BATCH_2026-10-12.md).


## October 12 fee-inclusive preparation wiring

[Preparation wiring evidence](evidence/live_readiness_2026-10-09/PREPARE_WIRING_2026-10-12.md)
records the new gross-input mapping, journal identity/recovery checks and bounded,
one-shot unsigned HTTPS port. The three-voter failure drill now uses 0.998 USDC
principal plus 0.002 fee, preserving exactly 1 USDC exposure until matching settlement.
Twelve affected Release checks and 62 named mutation checks pass locally; the new
HTTPS port and fixture signing remain excluded from release deployment.

Items 9–11 remain incomplete at the production-executable boundary. Next is durable
signer-request orchestration (including lost-stamp retrieval), verified account-session
custody and protected signer IPC, then execution/finality integration. The exact net
receipt check is still deferred to the next iteration and must return at live-test review;
the observed repayment discrepancy is not admitted. No commit, deployment, venue request
or money movement occurred in this batch, and no runtime health claim was refreshed.


## October 12 protected-signer and settlement connection continuation

[Connection evidence](evidence/live_readiness_2026-10-09/SIGNER_SETTLEMENT_CONNECTIONS_2026-10-12.md)
records implementation of the previously pending durable gateway signing request,
protected IPC retrieval after restart, and canonical execute identity. The settlement
connection now starts from retained preparation/submission journals and collects only
the matching finalized transaction, rejecting changed journals during RPC.

Ten affected Release checks and eight targeted mutation checks pass. These are local
components, not complete executable deployment wiring. Remaining: production gateway
phase orchestration/account-session custody and signer process setup, then attributed
account-lifecycle evidence before emitting a kernel Settlement. Until then, finalized
observation alone does not release funds. Native qualification follows the reviewed,
committed executable integration. No live admission, money movement, host change or
new runtime-health assertion occurred. The receipt deferral remains unchanged.

## October 12 parallel runtime integration — local checkpoint

The owner approved parallel work on gateway orchestration, protected startup and
settlement completion. Main owns shared gateway/CMake integration; two agents own
startup/HTTPS and finalized lifecycle accounting respectively.

- The new bounded gateway coordinator passed five three-voter local scenarios:
  joined synthetic settlement, ambiguous execute/restart, ambiguous create/restart,
  signer refusal, and malformed signed-request length. Unknown outcomes retain the
  full reservation and neither ambiguous POST is repeated.
- Protected startup passed seven sanitizer groups and sixteen named mutation checks.
  Gateway session-token ownership and signer private-key ownership are separate;
  configured account/session pins are not proof of a live authenticated account.
- Concrete execution and finalized-RPC HTTPS adapters passed local TLS checks;
  production executable composition now builds locally behind its closed live gate.
- Finalized lifecycle replay now accounts for temporary wrapped-SOL creation,
  transfer and closure. Admission remains unresolved for opaque route control,
  fee attribution and enforceable net receipt; an observation is not a Settlement.

`VELOCITA_BUILD_SWAP_RUNTIME` is an opt-in build configuration, default OFF.
The runtime's independent real-contract gate remains false even when compiled:
startup refuses before credentials or network. No fixture signing or settlement
macro belongs in this library. This is not live promotion, native qualification,
or completion of the outstanding transaction-contract work. The exact minimum
net receipt deferral and unaccepted 550,840-lamport discrepancy remain open.
No host, deployment, live account, funding or runtime health was changed here.

Final verification for this checkpoint: **30/30 selected Release CTest entries,
20/20 build-boundary tests, 47/47 targeted named mutation checks**. The coordinator
restart fixtures close/recover journals from disk and restore a fresh in-memory
watermark. The compiled runtime library has no local-fixture or synthetic-settlement
factory symbols. Current source is uncommitted; no native/deployed readiness is implied.
Exact scope, remaining proof requirements, commands and source hashes are in
[runtime integration checkpoint](evidence/live_readiness_2026-10-09/RUNTIME_INTEGRATION_2026-10-12.md).

## October 12 real transaction proof and parallel recovery batch

The owner authorized non-moving POSTs and parallel work. No live signing,
execution, submission, deployment or commit is authorized by this batch.

| Work | Current verified state |
|---|---|
| Fresh TM unsigned buy | One refresh + one fixed 1-USDC preparation succeeded. One metadata batch and one unsigned simulation succeeded. The $0.002 fee is included in $1; the sponsor repayment again exceeds the new account deposit by **550,840 lamports**. This real payload remains ineligible for signing. |
| Current route decoding | Added fieldless Jupiter RaydiumClmm variant 26 from the retained program-owned IDL; seven sanitizer groups and 25 named mutation kills pass. Decoding alone does not authorize signing. |
| Protected signer and recovery | Eight local sanitizer groups and four named mutation kills pass, including pre-IPC crash, result-persistence failure, expiry and missing-journal recovery. No additional production signer defect found. |
| Finalized settlement admission | Implemented and locally verified for supported successful receipts: ABI-bound fee roles, complete relevant lifecycle replay and exact net owner changes construct the private proof. Canonical transaction identity is shared with recovery. Sixteen sanitizer groups and 26 named mutation kills pass; shared/direct buys and a shared sell compose through the kernel reducer. Failed/unproven receipts retain exposure; failed-transaction charge attribution remains open. |
| Release / native hosts | Not changed. Local verification is complete for this batch. Latest edits still need a reviewed committed checkpoint before native qualification and deployment. |

Evidence: [fresh transaction proof](evidence/live_readiness_2026-10-09/REAL_TRANSACTION_PROOF_RESULT_2026-10-12.md),
[protected signing drills](evidence/live_readiness_2026-10-09/SWAP_SIGNING_RECOVERY_DRILLS_2026-10-12.md).
The exact enforced minimum net receipt remains unverified and reopened for this
iteration. The discrepancy was reproduced across two routes; it is not accepted
as an extra fee or resolved by weakening the minimum. No funds moved.

Final local verification: **11/11 selected integrated Release CTests**, followed by
one successful rebuild/rerun of the expanded settlement-admission composition test;
**74/74 named mutation kills** across route decoding (25), canonical resolution (19),
settlement (26), and signer recovery (4). The final signer rerun includes the shared
identity-helper relocation and reports unchanged sources. Twenty build-boundary
tests, six non-moving request-boundary tests and the signer-exclusion guard pass.
Production archive inspection found no prohibited synthetic/fixture factory symbols.
These are local results, not native/deployed qualification or permission to sign.
See [settlement evidence](evidence/live_readiness_2026-10-09/SETTLEMENT_ADMISSION_LIFECYCLE_2026-10-12.md)
and [batch verification manifest](evidence/live_readiness_2026-10-09/REAL_PROOF_VERIFICATION_2026-10-12.json).


## October 13 owner deferral and work ordering

The owner requested deferring the fresh-transaction economic mismatch until the
end. The practical-order table above now places it at the final live gate and
reflects completed local successful-settlement admission. Remaining work is drawn
from existing items 5, 9–16, 21 and 23; no new implementation, venue requests,
commit, host change or deployment occurred in this status update. Earlier dated
entries preserve their historical state and are superseded by the current table.


## October 13 account, operational recovery and drill preparation

Two parallel implementation agents plus main integration completed this bounded
local batch. [Account evidence](evidence/live_readiness_2026-10-09/ACCOUNT_RECONCILIATION_2026-10-13.md)
records the authenticated setup read, exact balances and the fix for a reconciliation
session reporting completion despite an unexplained delta. Fresh read-only results:
0.031896934 SOL and 1.006989 USDC, zero reported holds; main wallet matches the
retained quote owner. No ledger funding/import or real signing occurred.

[Failed settlement](evidence/live_readiness_2026-10-09/FAILED_SETTLEMENT_2026-10-13.md)
now supports a private zero-owner-charge proof for exact finalized sponsor-paid
failures, with an explicit profile fee-payer pin. Owner-paid or unsupported failures
remain unresolved. The actual user/sponsor pins and external-activity boundary
remain unqualified for live operation; a snapshot does not prove account inactivity.

[Operational recovery](evidence/live_readiness_2026-10-09/OPERATIONAL_RECOVERY_2026-10-13.md)
preserves known temporary archive files after verifying published history and
refuses previously silently skipped nonzero WAL tails/gaps. It does not erase or
repair ambiguous committed history. The persistence-model oracle now permits only
these verified quarantine renames while preserving byte equality. Another stale
adversarial test used type 102 as unknown even though it is now the swap barrier;
it now uses explicitly unsupported -1, without relaxing production validation.

The [failure drill plan](evidence/live_readiness_2026-10-09/FAILURE_DRILL_PLAN_2026-10-13.md)
fixes 15 scenarios, financial oracles, crash points, request-count invariants,
artifact provenance and stop conditions. Separate-process/native/deployed drills
are prepared, not executed or replaced by component tests.

Read-only nuc1 inspection found the external-heartbeat URL missing under the actual
`velocitas` deployment account. No heartbeat was sent and no host state changed.
Source review/commit and native qualification remain next release steps; no commit
or deployment occurred. Final test counts/hashes are in
[batch manifest](evidence/live_readiness_2026-10-09/ACCOUNT_RECOVERY_CHECKPOINT_2026-10-13.json).

### October 13: reviewed startup admission integration

Local source now requires `--swap-account-review` plus `account_review_digest` in
the runtime profile. It binds the complete operator opening (including limits,
cost basis and funding evidence), runtime account/wallet identity and user ID.
At most 16 individually identified outside events adjust only the declared
outside partition; checked arithmetic rejects duplicates, negatives and overflow.
An authenticated state read after journal recovery and fresh TM balance GET must
match exact managed-plus-outside atoms before new work. The prepare port repeats
the balance check. No wallet balance is adopted as kernel funding.

Unresolved startup state enters reconciliation-only mode: no create, signing or
submission. Supported retained submissions can still settle. Expired account
reviews cannot authorize a new signature, including expiry during an authority
read. This is local, gated executable wiring, not deployment or live approval.
Complete external-history coverage and the actual owner-reviewed artifact remain
open. The existing paper allocation must never become the live opening.
See `evidence/live_readiness_2026-10-09/STARTUP_ACCOUNT_REVIEW_2026-10-13.md`
for format, limits, verification and remaining boundaries.

The owner confirmed no Healthchecks check exists yet. Bounded metadata searches
on nuc1, documented local stores and named Keychain entries found no URL.
Creation of the external check is the remaining heartbeat setup step; no secret
was printed, heartbeat sent or host configuration changed.

Verification for this startup batch: eight related C++ suites, one early-refusal
Python test, 20 build-boundary tests and the deployment guard pass. Account review
7/7, coordinator 4/4 and signer 31/31 designated mutation kills pass; signer
sanitizer baseline covers 18 groups plus production refusal. Exact source/log
hashes are in `evidence/live_readiness_2026-10-09/STARTUP_ACCOUNT_CHECKPOINT_2026-10-13.json`.
No native host qualification, release commit, deployment or money movement is
implied by those local checks.


### October 13: bounded account cut and separate-process drill batch

The revised read-only capture used 18 evidence requests and found one current
legacy USDC account, two finalized transaction records, zero listed TM orders,
31,896,934 native SOL atoms and 1,006,989 USDC atoms. Discovery, balances,
configuration, order pages and history heads were checked again. The first
capture stopped on an alternate close authority and remains preserved; the
revised protocol distinguishes closing an empty token account from spending
its USDC and excludes token-account rent from funding. No new order, signature
or funds movement occurred. Provider history and current account enumeration
are bounded observations, not lifetime completeness or future exclusivity.

Startup now also requires a hash-pinned `--swap-account-history` artifact; its
owner/user, exact managed-plus-outside funding and age are checked independently
of the review timestamp. A new review cannot revive a stale capture. The cash-only
funding proposal is offline and non-executable; owner review and real release/
custody/signer pins are still required. The existing SOL is proposed outside
the strategy, avoiding invented acquisition cost.

The separate-process test uses independently exec'd gateway and generated-key
signer workers, real authenticated frames into three in-process V2Node replicas,
and a synthetic venue/settlement authority. It is not the shipped gateway binary,
Aeron transport, a distinct-UID deployment or a native-host qualification. F07
now exposes leader-change recovery stalling below the durable result watermark.
No signing/send is repeated and no held cash is released. The correct fix must
establish a committed current-term barrier before accepting recovery reads;
weakening the watermark or forcing follower apply in the drill would hide the
failure. This finding belongs to existing item 16, not a new checklist item.


### October 13: owner-approved election read-barrier fix

The owner explicitly requested fixing the F07 finding. A new bounded type103
record contains exactly the cluster authority-domain digest. Before serving a
gateway/signer state read, a leader must have applied a current-term log entry.
If none exists, it proposes this no-op; a pending current-term entry is reused,
so repeated reads cannot append duplicate barriers. The normal quorum, durable
WAL and durable commit-metadata gates apply. The marker uses protected capacity
and creates no business result, balance change, state-version change or signing
proof. An exhausted reserve refuses safely.

Both peer encoders/decoders, WAL and archive validation, retained-log ownership
and cold replay recognize the exact domain-bound record. Replication semantics
epoch is now 2; epoch1 manifests refuse before use, rather than admitting a
mixed cluster that cannot decode type103. This is an incompatible release boundary,
not an authorization to migrate/reset the running paper cluster. Kernel economic
commands and state snapshot schema remain unchanged by this fix.

The exact failed leader-loss timing now passes; all eight process cases and
independent JSON/economic/send-count/PID checks pass. The full gateway-node suite
also passes. Bypassing the node gate reproduces the named F07 failure in an
isolated mutation. Additional storage/replay/quorum checks and final hashes are
recorded in `evidence/live_readiness_2026-10-09/ACCOUNT_PROCESS_CHECKPOINT_2026-10-13.json`.
This marker establishes current-term prior-log knowledge; it is not a claim of
per-request linearizable reads under subsequent partition. Existing quorum-loss
step-down, signing authority and durable result fences remain required.


Account/process batch detail: [report](evidence/live_readiness_2026-10-09/ACCOUNT_PROCESS_BATCH_2026-10-13.md).
The actual private funding proposal now exists: 1.006989 USDC managed, zero
managed SOL/cost basis, 0.031896934 SOL outside, token-account rent excluded.
It is not an executable review or approved funding command. The raw observation
window was 05:58:30–05:58:34 UTC; activation requires a fresh cut and actual
release/custody/signer pins. No money moved. The independent compatibility goldens
were updated for replication epoch 2 while retaining the prior reviewed oracle;
final verification evidence is pinned in the batch checkpoint.
