# Live promotion review — 2026-10-09

Reviewed committed source `2f5d405`, the local untracked schema-11 staging scripts, and live hosts. This is a review, not approval to deploy or trade. No services were changed, no secrets were read, and no venue calls were made. All findings below are independent of profitability claims.

## Findings, in priority order

### 1. P1: the draft cutover's stop guards do not stop execution

`docs/evidence/roles_incarnation_2026-10-09/stage-host.sh:13–18` uses `! systemctl ... is-active` under `set -eu`, then moves the live state directory at line 24. Shell `errexit` does not exit for a command whose status is inverted with `!`. An active voter therefore does not abort this script. Several later `! grep` assertions have the same defect. This is a prepared, untracked script, not evidence that a cutover has occurred.

Minimal local reproduction:

```sh
/bin/sh -ec '! true; printf "continued after failed negated guard\n"'
```

It prints the message and exits successfully. The intended check has failed, but the script continues. If invoked before services are stopped, the script can move state and replace configuration while processes still own the old state.

Use explicit conditional failures; distinguish systemd's inactive result from query/connection errors. Verify the observer is stopped too, and check all images, configurations, unit files and backup inputs before moving anything. Currently the shadow-image check is line 42, after the move and unit edits, leaving a partially staged host on failure. Exercise the script against stub commands and disposable directories for active services, missing images, and partial failure. Do not run the prepared script unchanged.

### 2. P1 before live integration: signer instances can overwrite one another's replay floor

`src/v2_signer_socket.cpp:147–193` loads a sequence floor into each `FloorFile` object without holding an exclusive process lock. Persist compares against that object's cached floor. `SignerServer::listen` at lines 241–260 unlinks an existing socket without checking whether a live server owns it. Its destructor also unlinks by pathname.

The attached `signer_singleton.cpp`, compiled against the exact committed libraries, reproduces:

```text
first_write_100=1 stale_second_write_1=1 durable_floor=1
first_listener=1 second_listener_same_path=1
```

Two independently opened stores allow the durable sequence to fall from 100 to 1, and two server objects accept the same socket path. Overlapping processes can consequently weaken replay protection; old-server destruction can also remove the replacement's socket. This is an undeployed mock signer, not an observed real-key incident.

Acquire a stable exclusive lock for the floor/key before reading the floor or modifying the socket; retain ownership for the process lifetime. Do not lock only the file inode that rename replaces. Test overlapping startup, restart, floor monotonicity, and socket cleanup ownership. Keep real credentials gated until this is fixed.

### 3. P2: signer timeout does not cover the whole request

`src/v2_signer_socket.cpp:302–314` performs blocking `connect` and `write_all` before starting the response-read timeout. A full accept queue or a peer that stops reading can therefore delay completion beyond the configured timeout. This is a static code finding; the pathological blocking case was not reproduced here.

Use nonblocking I/O with one monotonic deadline for connect, request transmission, and response receipt. Test accept/write backpressure, not just a silent response. This matters before a synchronous signer call becomes part of live execution and quote-expiry handling.

### 4. P2: the plan conflates historical quote costs with the later executed route

`docs/LIVE_PROMOTION_PLAN_2026-10-07.md:14–16,43` describes approximately 100/98 bp as the applicable measured cost, without reconciling `docs/evidence/venue_contract_2026-10-05/SETTLED_SOL_SELL_FEE.md`.

That recorded finalized sale reports 0.002018 USDC fee on 1.009007 gross USDC, approximately 20 bp. It explicitly limits this observation to the executed CLI/API-key SOL sell route. Historical 1% quote observations remain valid for their route/time. Neither establishes the current buy fee, all-in round-trip cost, or passive maker fees.

Separate route/account/side/time evidence in the capability matrix. Keep the old 100/98 bp case as a labeled historical stress scenario; label the 20/20 bp paper scenario as measured sell plus assumed buy. Do not change production fees or request new trading on the strength of this review.

### 5. P2: price-basis timing uses candle opening time for a closing price

`docs/evidence/venue_price_basis_2026-10-09/basis.py:39` stores a candle's close under `row[0] // 1000`, its opening second. The deployed strategy's own `candle` contract in `model/src/velocita_model/shadow/stochastic_second.py:18–26` requires `end=start+999` and timestamps availability at `(end+1)//1000`.

The basis study is therefore shifted by one second relative to the actual strategy timestamp. Its claimed 1–2 second relative lag cannot be promoted unchanged into feed/execution assumptions. Timestamp closes at availability, retain the venue event-time definition, and regenerate the lag comparison with a hand-worked known-offset fixture. Reference prices still do not establish executable fills.

### 6. P2 for promotion evidence: the replay is a signal diagnostic, not a funded-account simulation

`docs/evidence/stochastic_rule_replay_2026-10-09/replay.py:28–82` reuses live indicators, delays the signal, and reports closed round trips. It does not enforce cash availability, the deployed loss stop or capacity gates, nor mark open inventory throughout the path. Final open inventory is reported as a Boolean but excluded from monetary results. Its reported worst drawdown is based on realized closed-trade P&L.

The accompanying README discloses omitted gates, which is good. Nevertheless, the 30-day roughly $80 loss at 20 bp per side cannot represent an executable run funded with $25 and stopped at $10 loss. The volume is unconstrained diagnostic turnover. This replay also does not complete the requested research-code review or supply a new preregistered null/walk-forward battery.

Before using it for promotion, carry cash/inventory continuously, apply the actual gates, report marked equity and terminal inventory, and label diagnostic turnover separately from feasible account turnover. Preserve the negative observed result without claiming it proves absence of every possible edge.

## Claude's progress and where work stopped

The committed tree progressed from the Oct-7 checkpoint to `2f5d405`. Host images still identify `f55209e`. Built does not mean deployed.

| Work | Reviewed status |
|---|---|
| Edge retry/bootstrap, market-data allowlist, fewer idle frames | Included in deployed source lineage |
| Kernel cash/P&L, fill checks, loss limit; archive usage alerts; smaller policy table | Deployed schema 10 |
| Caller roles, operator flatten/conflict resolution | Committed; schema-11 cutover pending |
| Quorum-loss step-down, release assertions, ingress rate limit, idle-writer sleeping | Committed after deployed image |
| Redundant terminal reconciliation and stale gateway state protection | Committed after deployed image; focused tests pass |
| Paper fault flags and required live loss limit | Committed; new gateway deployment pending |
| Signer and transport | Test-key/mock-only; excluded from release images; real payload/TLS integration absent |
| Next-incarnation scripts | Prepared locally, untracked; unsafe guard noted above |
| Venue contract, balance reconciliation, outside heartbeat, position-held drills, crash recovery | Still open |

The stale-state repair is sound in its stated scope: the voter refuses state reads while application is pending, and the gateway rejects a state older than its observed result/proof index. The three-voter slow-leader test covers the follower-quorum case. This does not certify every failure mode of the whole system.

Claude's local status note records a nuc1 connectivity/reboot interruption during preparation. The running cluster and image identities confirm that the schema-11 replacement has not happened. Its handoff note and the plan contain stale bookkeeping: the plan lists 26 items but its count totals 25, classifies item 20 differently in the count, and still describes the current account as fresh $25 with no orders. Treat those as historical snapshots.

## Live snapshot: 2026-10-09 19:03–19:11 UTC

Read through SSH/host HTTP, not Safari. One voter cluster per host was observed.

| Host | Role | Term | Current/durable index | Commit/applied index | Image |
|---|---|---:|---:|---:|---|
| nuc1 | leader | 13 | 20880 | 20876 | `ledger-f55209e` |
| Pi | follower | 13 | 20880 | 20876 | `ledger-f55209e` |
| nuc2 | follower | 13 | 20880 | 20876 | `ledger-f55209e` |

All three voter/exporter pairs were running; voter failed metrics were zero. The four-entry uncommitted tail persisted across checks. Equal durable indices alone do not prove current-term commitment eligibility; the precise stall cause was not established. Do not delete or truncate this tail manually.

The paper pipeline is **not healthy**: nuc1's edge service is failed and its container is absent. After the reboot, its journal reported a missing caller-12 runtime key, later a source error and submitter exit, and then an explicit systemd stop with eventual exit 137. This is not evidence of an OOM kill. Restarting blindly would not resolve all of those stages.

The observer reports readable, reconciled paper state: **$22.314889 cash/equity, flat position**, initial cash $25. Kernel realized P&L is **-$2.685111**, including **$2.533911 fees**. The negative kernel cash metric is the trading cash delta, not a negative funded account balance. The configured loss limit is $10.

At 19:10 UTC alerts included edge stalled, commit stalled, recurring elections, no reference-price recorder updates, and an oversized recorder database (~20.79 GB). The commit alert's text speculates that another voter is unreachable; the simultaneous three-host checks do not support presenting that explanation as established. Archive usage itself was low (~5.24 MB / 1 GiB on nuc1), and ordinary retained slots were 377 / 8128 there. Pi disk use was ~81% with ~5.2 GB free at the earlier check.

## Validation and limits

- Built from a `git archive` of committed `2f5d405`, isolated from the dirty/shared working tree. Dependencies and build outputs were in a separate review cache. No production images were built or shipped.
- Passed `v2-replication-tests`, `v2-gateway-node-tests`, `v2-node-tests`, `v2-risk-order-outbox-tests`, and `v2-signer-not-in-release-images`.
- Signer and transport suites initially failed because the sandbox disallowed local socket creation. Both passed on a rerun permitting Unix/loopback sockets. The failed sandbox log is retained rather than hidden.
- Seven selected CTest targets passed in aggregate. This is not a full repository test run, ARM run, sanitizer run, mutation campaign, or production fault drill.
- Independently reproduced the signer overlap/floor regression and the shell-negation guard behavior. The signer regression is missing from the passing existing suite.
- Shared uncommitted CMake/gateway/research/REST work was left untouched. Committed `truemarkets_transport` and untracked `truemarkets_rest` are separate work and must not be conflated.

## Recommended order

1. Repair and test the staging guards and signer ownership before using those artifacts.
2. Diagnose/recover the stopped paper edge and four-entry tail under the existing operational procedure; establish why the reboot lost runtime credentials and why the later source failure occurred. This review performed no recovery.
3. Deploy the reviewed paper fixes from an exact committed revision, with one cluster only, a coordinated voter/exporter change, and explicit old-state archival/new-incarnation ceremony if choosing schema 11. Do not claim a runtime backup from a source tag.
4. Run position-held failure drills with marked account accounting and bounded faults; correct the route-specific cost and price timing evidence.
5. Keep real trading gated on actual venue payload/order/reconciliation contracts, signer fixes, credentials policy and the owner's bounded live approval. Mock tests and strategy paper activity do not close these gates.

## Read-only inspection commands

From the Mac, SSH opens a shell on the remote machine; it does not change Raft state:

```sh
ssh velocitas@nuc1
curl -fsS http://100.72.127.102:11410/metrics |
  grep '^velocita_v2_log_index'
systemctl --user status velocita-paper-stochastic-edge.service --no-pager
```

`current - commit` counts the uncommitted log tail. `durable` records local persistence; `applied` records kernel application. Neither a process being up nor an entry being on disk alone means the entry is committed.

For uncommitted **source edits**, use `git status --short` and `git diff` in the actual Git checkout on the Mac (`~/aeron-raft-pi`). The deployed host source is shipped as an archive; do not assume it has `.git` or infer deployed source identity from the Mac's HEAD.
