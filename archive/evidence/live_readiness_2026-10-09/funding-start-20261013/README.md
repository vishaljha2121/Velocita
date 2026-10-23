# Reviewed funding/start ceremony completed

Verified at **2026-10-13 22:06:00 UTC (18:06 EDT)**. Both isolated production
services are active/running with zero restarts, executing the pinned native
binary. The single existing cluster remains globally halted. **No buy intent,
order preparation, signing or transaction execution occurred.** No funds moved.

## Committed state and running services

| Item | Verified result |
|---|---|
| Owner caller role | Request4 committed at index5/term5: caller22 is gateway. Signer23 remains a read-only Raft caller. |
| Canonical account opening | Request5 committed at index6/term5/state_version5; kill1/generation1, strategy_mode5 (halted). |
| Actual managed assets | 40.006989USDC and0.040764166SOL. Paper wallet excluded. |
| SOL opening mark | 4.353100USDC, from one unsigned whole-position quote. A quoted liquidation mark, not a sale or historical acquisition cost. |
| Bounded purchase scope | Total/per-buy debit cap1,000,000USDC atoms, fee cap2,000, realized-loss cap500,000. Conservative repayment cap2,039,280lamports; zero sponsor credit. No automatic sell or price-guaranteed stop-loss. |
| Services | signer PID298864 started22:03:37UTC; gateway PID298894 started22:04:08UTC. Both active/running, expected service UIDs, exact executable hash, zero restarts. |
| Recovery | Protected signer journal plus gateway prepared/submitted/signing-requests/results journals created once from committed index6. All ordinal1; order-bearing journals idle(phase1), effect0, no transaction or request payload. Checksums/private file metadata match. |
| First-use flag | Removed from future launches without restarting the running processes. Manifest now initialize_swap_recovery=false. Never delete/reinitialize these journals. |
| Cluster | All three exporters up, term5/leader2, halted. One voter container per host, no voter/exporter/monitoring restart. |

**Account review expires at22:17:27.845UTC (18:17:27EDT).** These supervised
services stop when their account review expires; this is not unattended strategy
operation. Later use requires a fresh review bound to this already-committed
opening and existing journals. Do not generate another Initialize or reuse the
initial-adoption helper to reset funding. The helper implemented here intentionally
refuses refreshing initialized runs; a post-funding review update is a distinct
recovery-aware operation. No trading authority survives stale evidence.

The next trading step is a separately reviewed fresh single-$1 buy intent and
normal-mode transition. The old captured buy is expired and must never execute.
A fresh positive conservative minimum receipt and matching current route remain
required. This ceremony does not prove end-to-end trade settlement or profitability.

## Evidence and implementation

Checkpoint **d63f93d** contains the narrowly scoped transition helper, its tests
and ceremony preparation. Installed service release remains5cdb5e1 and gateway
bytes remain the qualified13cc98b binary:
09f7c3ff0ea68db58cf55c0045e8fbf35c0dcffe1f01632a21cf77b2107b9d7c.
No C++ or native image rebuild was necessary. Shared dirty gateway/kernel/build
files were not staged.

Final manifestSHA:
910702fe85f8d5de6746d87c5961f6a48885b4a8bbdcc888fb11007059292947.
Runtime profileSHA:
f3197e572b0c4b615e9fd05a3e7e18590b18a35079ac7250641382d4701cd661.
Opening recordSHA:
10d9d43a3c571d7a9d25e4029342a898b2e27950f4dc81fc5357369a483c5fd3.

31 focused local tests passed(9transition,6adoption,16launcher). The9transition
tests passed on NUC before use. Three targeted mutations failed named assertions,
with zero test errors. The transition helper cannot start units or create orders;
it checks exact prior bytes, hashes, identities, credential bindings and review
freshness, preserves previous files, and refuses initialized journal replacement.

Fresh service-user reads before the ceremony matched the known unfunded state.
Two bounded20-request history cuts found unchanged balances, four transactions,
zero orders and no holds. The one finalized six-account route snapshot passed at
slot456598383. The single unsigned full-position valuation was followed by the
final history cut; the qualified C++ opening builder ran4.080seconds after the
quote. Existing evidence is retained, not overwritten or timestamp-extended.
Provider archival completeness and closed historical token accounts are not proved;
account snapshots do not guarantee arbitrary future program/execution state.

See startup-verified.json, voters-after.json, ceremony.log, opening-summary.txt,
profile-summary.json, transition-summary.json, route-summary.json and
valuation-summary.json. Raw responses and account bundles remain private.
No credentials or sudo password are included in this folder.

## Operational observations

- The older Mac operator binary prints `SWAP PAPER FUNDING`. This is a legacy
  presentation label; the canonical Initialize contains the actual TM balances
  and matches the account review accepted by the live executable. It does not
  perform a wallet transfer.
- Linux tar ignored macOS provenance extended headers during account-bundle
  transfer. Every operative file was checked by its pinned SHA256 before use.
  Use COPYFILE_DISABLE=1 for later tar transfers to avoid this harmless noise.
- The first read-only journal-inspection snippet expected idle enum0; the C++
  headers define idle=1. That diagnostic assertion failed without modifying
  files or services. After checking the actual codec/header, the corrected
  read-only check passed all five journal hashes, phases and metadata. No
  trading, funding, installation or network request was retried.
- The interactive sudo cache was invalidated and administrative SSH closed.
  Role credentials remain protected in their intended tmpfs locations.
