# Storage: databases, logs and caches

Velocita uses no general-purpose database for anything that moves money. Trading state is a replicated log
plus snapshots in purpose-built, checksummed files, because the kernel needs exact bytes, an fsync it
controls, and recovery that refuses rather than guesses. SQLite and Prometheus are used only beside the money
path, for market data and monitoring.

## Durable stores

| Store | Where | Format | Written by | Why this shape |
|---|---|---|---|---|
| Raft WAL | each voter's `data/` | 4 KiB checksummed blocks, preallocated, `fsync`ed off the consensus thread | voter WAL writer | a record never straddles blocks, so a torn write is always a bad checksum ([memory.md](memory.md#wal-blocks)) |
| Raft metadata | each voter | term, vote and commit index, replaced atomically | voter | a vote must survive a crash or a node could vote twice in a term |
| Snapshot | each voter | canonical little-endian encoding of the whole state, ≤ 61,586 B, digest-checked | compaction | identical bytes on x86_64 and aarch64; recovery replays it before trusting it ([kernel.md](kernel.md#snapshots-prove-themselves)) |
| Archive segments + catalog | each voter | immutable segments, SHA-256 per record and per segment; catalog ≤ 4,096 rows, ≤ 1 GiB | compaction worker | compacted history stays verifiable and can repair another voter |
| Generation selector, identity | each voter | small files naming the current base and the cluster's static identity | startup / install | a voter refuses to start against the wrong cluster or a half-installed snapshot |
| Gateway journals | `/var/lib/velocita-swap-gateway` | prepare, submission and reconcile stores, checksummed and `fsync`ed | gateway | written *before* each external act ([gateway.md](gateway.md#journal-before-every-external-act)) |
| Signer journal | `/var/lib/velocita-swap-signer` | sequence floor, request identity, signed transaction | signer | written before the key is used ([signer.md](signer.md#journal-before-using-the-key)) |
| Session epoch files | each process | one counter, increased durably at every start | auth layer | retires every frame from an earlier process life ([secrets.md](secrets.md#authenticated-messages)) |
| Driver state | NUC1 | `driver-state.json`: sequence, in-flight effect, stop reason; temp file, fsync, rename | driver | a crash can skip a sequence number but never reuse one |
| Paper venue | paper mode only | durable fill state | paper gateway | paper fills survive restarts like real ones |

### How compaction keeps the log small without losing evidence

```mermaid
flowchart LR
  A["2,048 retained entries"] --> B["copy history into an<br/>immutable archive segment"]
  B --> C["read it back in full,<br/>publish a catalog row"]
  C --> D["save snapshot, reload it,<br/>check cut, term, digest"]
  D --> E["release the Raft prefix"]
  E --> F["rewrite the WAL prefix last"]
```

Every step is checked before the next one destroys anything. Compaction used to re-read every archive
segment, and its pause grew past the election timeout (term 1 → 65 in 48 hours). It now re-reads two segments
in rotation, with pauses of 0.1–0.4 s.

## Beside the money path

| Store | What | Retention |
|---|---|---|
| `stream.db` (SQLite, NUC1) | every market-data message the recorder sees | archived nightly to the owner's Mac as `stream-YYYY-MM-DD.sqlite.gz`; pruned only after the Mac holds a verified copy ([runbook](../MD_ARCHIVE_RUNBOOK.md)) |
| Prometheus TSDB (NUC1) | voter, gateway, signer and strategy metrics | 1 week or 1 GiB. Too short: the live run's series had expired before its write-up |
| Configuration and reviews | `/etc/velocita-swap/public` | manifest, runtime profile, account review, opening record, each pinned by SHA-256 |

## Caches, and what keeps each one honest

| Cache | Holds | Freshness rule |
|---|---|---|
| Retained Raft log (16 MiB per voter) | every entry not yet compacted | the source of truth until compaction; lets the leader serve a lagging follower without reading disk |
| Result ledger (1,024 slots) | committed answers waiting for their caller | reserved when the command is proposed, so a committed result is never dropped |
| Gateway's copy of the state | the latest committed reading | a reading older than the highest index the gateway has caused is discarded ([stale-read fence](fix-read-outage-retry.md#the-related-stale-read-fence)) |
| Gateway bearer token | the venue session token | memory only; re-minted through the signer when two minutes remain |
| `strategy-settlements.json` | the account as the driver sees it | published every 5 s by the observer from the gateway's validated snapshot; ignored when older than 12 s |
| Driver indicators | 900–1,200 s of bar history | rebuilt from a 3,000 s REST warm-up after any feed gap |

The pattern is the same everywhere: a cache can only make the system slower or more cautious when it is
stale, never make it act on old information.
