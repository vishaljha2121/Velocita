# Account evidence and crash recovery — October 13, 2026

The bounded account capture and local process harness are implemented. The harness
found a real leader-change recovery stall; the owner approved its repair and all
eight process cases now pass. This is local working-tree qualification, not a
release, deployment, funding adoption or authorization to trade.

## Real account observation and proposed allocation

The revised capture ran from 05:58:30.725 to 05:58:34.959 UTC, using 18 read-only
evidence requests. It found one current classic USDC token account, two finalized
on-chain records and zero listed TM orders. Authenticated balances and on-chain
native/token amounts matched, with no venue holds. Repeated account configuration,
balance, order-page and history-head checks matched within the observation window.

| Observed or proposed item | Exact amount |
|---|---:|
| Native SOL observed | 0.031896934 SOL |
| USDC observed | 1.006989 USDC |
| Proposed managed starting USDC | 1.006989 USDC |
| Proposed managed starting SOL and its cost basis | 0 |
| Existing SOL proposed outside the strategy | 0.031896934 SOL |
| Token-account rent excluded from both trading allocations | 1,488,440 lamports |
| Requested cumulative purchase cap, including fees | 5 USDC |
| Requested loss stop | 0.50 USDC |

The purchase cap does not create five dollars of cash. This proposal uses the
actual cash observed and avoids inventing an acquisition cost for existing SOL.
It remains **unapproved and non-executable**. No Initialize command was generated
or applied, and no paper-wallet funds were imported.

The two records show an incoming net native balance change of 40,296,934 lamports,
then the owner's previously reported October 5 sale: -8,400,000 lamports and
+1,006,989 USDC atoms. The second signature exactly matches the owner's retained
sale hash. These deltas reconcile arithmetically to the observed balances. The
collector labels unlinked observations as requiring review: an empty current TM
order list did not contain that historical sale, and is not treated as proof that
no historical trade occurred. Network fees reported for transactions are not
automatically charged to this owner's ledger.

The first capture stopped on the USDC account's alternate close authority and is
preserved. The revised protocol records and brackets this configuration: a classic
non-native token account must be empty before closure, and close authority is
distinct from transfer delegation. Delegated/frozen/native/unsupported token
accounts still refuse. See the [registered protocol](ACCOUNT_HISTORY_PROTOCOL_2026-10-13.md)
and [Solana's Close Account contract](https://solana.com/docs/tokens/basics/close-account).

Private raw responses, request trace, observations and the funding proposal remain
under `~/.velocita-credentials/truemarkets/`, mode 0700 directories/mode 0600 files.
The offline proposal builder verifies response hashes and replays every recorded
request against the exact retained bytes before producing the proposal.

- Capture: `account-history-revised-20261013/`
- Proposal: `account-funding-proposal-20261013/funding-proposal.private.json`
- Redacted proposal: `account-funding-proposal-20261013/summary.json`
- History-cut SHA-256: `b26dff2a4fed3aa6732ef71aa447595161bcc6c02c791448f98cb3df7a4da5da`
- Private proposal SHA-256: `d265444860d2c22f4c9c17efb358309d50e18ce0a75b74ffca32cf4b9e7dc80e`

The scope covers the current owner and currently existing USDC accounts. It does
not prove provider archival completeness, find all already-closed historical
accounts, prevent concurrent wallet use, or remain fresh indefinitely. A supervised
activation needs fresh observations, reviewed funding and real release/custody/
signer pins. The captured bytes are evidence, not a perpetual live permit.

## Startup integration

Both optional runtime processes now require `--swap-account-history` in addition
to the prior `--swap-account-review`. The review pins the exact history bytes.
The bounded C++ parser binds owner/user, balances, capture time/slots and evidence
manifest; requires eligible bracket/pagination/order flags; and refuses oversized,
malformed or over-five-minute captures. Funding checks require exact managed plus
reviewed outside amounts. Capture age is independently limited to 15 minutes, so
rewriting a review timestamp cannot revive stale observations. The signer uses
the earlier of the review and capture deadlines. Recovery-only processing retains
its prohibition on new create/sign/execute operations.

These declarations still require trusted operator review. A hash does not prove
the provider returned all history. The real transaction admission gate remains
closed, and a startup test verifies refusal before opening supplied resource paths.

## Leader-loss defect and repair

The first F07 drill captured this exact state:

| Replica state | Current log | Committed | Applied |
|---|---:|---:|---:|
| Old leader just before death | 10 | 10 | 10 |
| Each follower | 10 | 9 | 9 |
| Replacement leader, new term | 10 | 9 | 9 |

The gateway remembered committed result 10 and correctly refused state 9. Reads
alone could not commit the new leader's prior-term tail. This caused a recovery
stall without a duplicate send or release of held money.

`ReplicatedKernel::ensure_current_term_applied()` now gates gateway/signer state
reads. A current-term applied entry satisfies it. An existing pending current-term
entry is reused. Otherwise it appends a type 103 no-op with exactly 32 authority-domain
bytes and refuses the read until normal quorum commitment, durable WAL/metadata
and application complete. Committing the current-term marker commits the preceding
log prefix. There is no automatic retry of a venue request.

The check uses constant-time index/term lookups and bounded stack data, with no
new state allocation or arithmetic in the trading reducer. Retained-log ownership
copies the marker bytes through the existing path. The marker consumes protected
log/archive capacity, reserves no business-result slot, changes no economic state
or state version, and produces no signing proof. Exhausted capacity refuses;
repeated reads do not grow a pending term's log. WAL, both peer forms, archive
validation and replay reject a wrong domain/length/type. Compaction preserves its
replication coordinate and no-op semantics.

This establishes prior-log knowledge after election. It is not per-request
linearizable ReadIndex during a later partition; existing quorum-loss step-down,
fresh signing authority and durable result-watermark protections remain in place.

Replication semantics epoch changes from 1 to 2. Old identities refuse before use;
old/new replication binaries must not share a cluster. The economic command and
snapshot schemas are unchanged by this fix. A coordinated reviewed release with
compatible identity is required; no migration or reset was performed here.

## Verification and retained failures

See the [process report](PROCESS_DRILL_2026-10-13.md) for all eight cases and exact
fixture limitations. Gateway and signer really fork/exec and die; the three voters
are real V2Node objects in the parent. Venue/finality are synthetic, transport to
voters is a frame bridge, and peer-UID tests use the local test UID. This does not
qualify deployed Aeron/TLS transport or production distinct-UID separation.

- Eight process scenarios and eight independent artifact validations pass.
- Full gateway-node and replication suites pass; dedicated checks cover quorum
  refusal, one marker per term, no results/proofs, old authority drain, cold replay,
  compaction and epoch 1 refusal.
- 44 Python account checks, 20 build-boundary checks and the executable early
  refusal test pass.
- Named mutations: account-history C++ 10, collector 10, funding proposal 8,
  process boundaries 3, leader-barrier storage/read behavior 3. Compiler errors and
  timeouts are not counted as kills.
- The independent golden generator preserves 16 reviewed schema 8/epoch 1 vectors
  and rebinds current schema 12 fixtures to epoch 2 using Python struct/hashlib.
  Eleven dependent test files needed exact byte-literal updates.

The original F07 failure remains retained. An initial artifact writer streamed a
uint8 settlement count as a character; main review caught the invalid JSON. The
writer now emits an integer and independent parsing checks every financial field,
transcript count and process death. Old malformed files are preserved, superseded
by `process-drill-20261013/raft-fixed/`, and are not valid JSON evidence.

A broader regression also found an old truncation test expected only “committed
WAL history is missing.” The previously implemented strict scanner can refuse
earlier with “nonzero partial WAL block.” The test accepts those two specific
failures and now additionally asserts byte-for-byte preservation of damaged files.
Production WAL validation was not relaxed.

Exact final source/log hashes and check outcomes are in
`ACCOUNT_PROCESS_CHECKPOINT_2026-10-13.json`. Most build logs are under
`~/.cache/velocita-build/account-process-20261013/`; component mutation snapshots
retain their exact linked library hashes. These are local working-tree checks.

## Remaining release boundary

Review/isolate and commit the intended source, qualify that commit on NUC/Pi,
then perform the separately authorized single-cluster paper release and remaining
operational/native drills. Review actual funding and custody/signer configuration
at activation. The deferred TM sponsor-repayment discrepancy and exact enforced
minimum net receipt remain final live gates, alongside the exact approved test
and enforceable limits. No commit, host change, deployment, real signature or
money movement occurred in this batch.
