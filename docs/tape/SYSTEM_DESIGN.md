# Verifiable tape: system design

UML views of the tape, reviewed before any code was written. Status: **locked 2026-11-11**. Changes need an
ADR 0030 amendment.
Decisions and paths are in [ADR 0030](../adr/0030-the-verifiable-tape-is-a-separate-raft-group.md); the
locked guarantees are in [GUARANTEES.md](GUARANTEES.md). Where a diagram shows behaviour, the guarantee
that requires it is named.

## 1. Context

Who and what the tape talks to.

```mermaid
flowchart LR
  TM(["True Markets<br/>public CeFi WebSocket"])
  OP(["Operator<br/>(owner)"])
  REV(["Reviewer<br/>(for example True Markets)"])
  subgraph Tape["Verifiable tape, three home machines"]
    REC["Recorders"]
    GRP["Tape Raft group"]
    PUB["Head publisher"]
  end
  TM -->|"public frames, read-only"| REC
  REC --> GRP
  GRP --> PUB
  OP -->|"start, stop, keys"| Tape
  GRP -->|"metrics"| MON["Prometheus, Grafana, Alertmanager"]
  MON -->|"alerts"| PHONE(["Owner's phone via ntfy"])
  GRP -->|"segments"| ARC[("Archive")]
  PUB -->|"signed tree heads"| REV
  ARC -->|"shared on request"| REV
  REV --> VER["Offline verifier"]
```

The only outside system the tape reads from is True Markets' public feed, and it never writes to it.
Reviewers get tree heads and, on request, archive segments; nothing is published without the owner's
approval.

## 2. Deployment

```mermaid
flowchart TB
  TM(["True Markets public WebSocket"])
  subgraph N1["NUC1, x86_64"]
    r1["tape-recorder@1"]
    v1["tape-voter@1"]
    e1["tape-exporter@1"]
    mon["Prometheus, Grafana,<br/>Alertmanager, ntfy"]
    t1["trading voter 1<br/>(separate cluster)"]
  end
  subgraph N2["NUC2, x86_64"]
    r2["tape-recorder@2"]
    v2["tape-voter@2"]
    e2["tape-exporter@2"]
    t2["trading voter 2<br/>(separate cluster)"]
  end
  subgraph P["Raspberry Pi 5, aarch64"]
    r3["tape-recorder@3"]
    v3["tape-voter@3"]
    e3["tape-exporter@3"]
    t3["trading voter 3<br/>(separate cluster)"]
  end
  TM --> r1
  TM --> r2
  TM --> r3
  r1 -->|"client protocol, HMAC"| v1
  r2 -->|"client protocol, HMAC"| v2
  r3 -->|"client protocol, HMAC"| v3
  v1 <-->|"Aeron UDP over Tailscale"| v2
  v2 <-->|"Aeron UDP"| v3
  v1 <-->|"Aeron UDP"| v3
  e1 --> mon
  e2 --> mon
  e3 --> mon
```

| Process | One per | Runs as | Talks to | Keys it holds |
|---|---|---|---|---|
| `tape-recorder@N` | machine | its own Unix user, rootless container | the venue (WebSocket out), its local voter (client protocol, routed to the leader) | its recorder HMAC key |
| `tape-voter@N` | machine | its own Unix user | the other two voters (Aeron UDP), recorders, the exporter's state page | peer HMAC keys, its ticker key, its tape signing key (public-key) |
| `tape-exporter@N` | machine | its own Unix user | the local voter's state page (read), Prometheus (scraped) | none |
| Head publisher | voter | inside `tape-voter` | committed signatures (read), the heads directory (write) | none, it only verifies |
| Offline verifier | anywhere | the reviewer | archive files and heads only | voters' public keys |

The tape group uses its own incarnation, key domain, Aeron ports and data directories, separate from the
trading cluster's (X-2). The trading cluster stays halted and unchanged.

## 3. Components

### 3.1 Tape voter

```mermaid
flowchart LR
  REC["Recorders"]
  subgraph Voter["tape-voter process"]
    ING["Ingress<br/>auth, session fence,<br/>request receipts"]
    TICK["Tick proposer<br/>leader only"]
    RAFT["Raft core<br/>willemt fork, Aeron"]
    PERS[("Persistence<br/>WAL, snapshots, archive")]
    subgraph RED["Tape reducer, pure and bounded"]
      DEC["Entry decoder<br/>part reassembly"]
      ATT["Attestation<br/>open verdicts"]
      BOOK["Book engine<br/>books, reorder buffers"]
      INT["Integrity checks<br/>observe only"]
      MRK["Merkle frontier"]
    end
    STATE["State page"]
    SIGN["Head signer"]
    PUB["Head publisher"]
  end
  REC --> ING
  ING --> RAFT
  TICK --> RAFT
  RAFT <--> PERS
  RAFT -->|"committed entries, log order"| DEC
  DEC --> ATT
  DEC --> BOOK
  BOOK --> INT
  ATT --> MRK
  BOOK --> MRK
  INT --> MRK
  MRK -->|"head at every K-th tick"| SIGN
  SIGN -->|"HeadSignature"| ING
  RAFT -->|"committed signatures"| PUB
  RED --> STATE
  STATE --> EXP["Exporter"]
```

The reducer is a pure library: no clock, file, socket, thread or heap after start-up (ATT-6, BOOK-8). Every
other component does I/O. Only the leader's tick proposer is active at a time; a deposed leader's tick is
refused by number (TICK-1).

### 3.2 Recorder

```mermaid
flowchart LR
  TM(["Venue WebSocket"])
  subgraph Recorder["tape-recorder process"]
    WS["WebSocket client<br/>subscribe, heartbeat"]
    CLS["Frame classifier<br/>stream, seqnum, kind"]
    SEQ["Sequence tracker<br/>gap, regression"]
    LIVE["Liveness monitor<br/>30 s rule, backoff"]
    REF["Snapshot refresh timer<br/>H-1"]
    CLK["Clock source<br/>UTC, monotonic, chrony error"]
    RING["Fixed ring"]
    SUB["Submitter<br/>split, sign, retry"]
  end
  TM --> WS
  WS --> CLS
  CLK --> CLS
  CLS --> SEQ
  SEQ --> RING
  SEQ -->|"gap: resubscribe"| WS
  LIVE --> WS
  REF --> WS
  RING --> SUB
  SUB -->|"client protocol"| V["Local tape voter"]
```

The classifier reads only the stream, `seqnum` and update kind; the bytes that go into the ring are the
frame as received (REC-1). The ring is allocated once and never grows (REC-5).

### 3.3 Offline verifier

```mermaid
flowchart LR
  ARC[("Archive segments")] --> DEC["Entry decoder"]
  DEC --> RED["Tape reducer<br/>same library, fresh state"]
  RED --> LEAF["Recompute leaves"]
  LEAF --> MRK["Merkle tree"]
  HEADS[("Published heads<br/>and public keys")] --> SIG["Signature check<br/>2 of 3"]
  MRK --> CMP["Compare roots,<br/>inclusion, consistency"]
  SIG --> CMP
  DEC --> TCK["Tick sequence<br/>and spacing"]
  RED --> MET["Recompute log metrics"]
  CMP --> REP["Report: first mismatch,<br/>or pass"]
  TCK --> REP
  MET --> REP
```

The verifier reuses the reducer library, so it proves the log and the heads were not altered. Whether the
reducer itself is correct is proven separately by the tests' independent reference book builder (BOOK-1).

## 4. Data model

### 4.1 Raft log entries

```mermaid
classDiagram
  class TapeEntry {
    <<variant>>
  }
  class Observation {
    +RecorderId recorder
    +SessionId session
    +u64 observation_no
    +StreamId stream
    +u64 seqnum
    +UpdateKind kind
    +Digest256 payload_hash
    +i64 utc_recv_ns
    +u64 mono_recv_ns
    +u32 clock_error_us
    +bytes payload
  }
  class ObservationPart {
    +RecorderId recorder
    +SessionId session
    +u64 observation_no
    +u16 part_index
    +u16 part_count
    +Digest256 whole_hash
    +bytes chunk
  }
  class RecorderEvent {
    +RecorderId recorder
    +SessionId session
    +u64 observation_no
    +EventKind kind
    +StreamId stream
    +u64 expected
    +u64 received
  }
  class Tick {
    +u64 number
    +VoterId proposer
    +i64 proposer_utc_ns
  }
  class HeadSignature {
    +u64 tick
    +u64 tree_size
    +Digest256 root
    +VoterId voter
    +Signature signature
  }
  class UpdateKind {
    <<enumeration>>
    SNAPSHOT
    UPDATE
    SNAPSHOT_COMPLETE
    HEARTBEAT
    UNPARSED
  }
  class EventKind {
    <<enumeration>>
    SESSION_START
    GAP
    REGRESSION
    OVERFLOW
    DEAD_CONNECTION
    RECONNECT
    RESUBSCRIBE
  }
  TapeEntry <|-- Observation
  TapeEntry <|-- ObservationPart
  TapeEntry <|-- RecorderEvent
  TapeEntry <|-- Tick
  TapeEntry <|-- HeadSignature
  Observation --> UpdateKind
  RecorderEvent --> EventKind
```

- `StreamId` is a channel plus a symbol index into a fixed table of at most 16 symbols.
- Prices and sizes stay inside `payload` as the venue's decimal strings; they become integers only in the
  book (BOOK-2).
- An observation whose payload exceeds the entry limit is sent as `ObservationPart`s with the hash of the
  whole payload (ING-4).
- For an OVERFLOW event, `expected` and `received` hold the first and last lost `seqnum`.
- HEARTBEAT and UNPARSED observations carry no usable `seqnum`, so they open no verdict.

### 4.2 Reducer state

```mermaid
classDiagram
  class TapeState {
    +u64 applied_index
    +u64 last_tick
    +StreamBook books_16
    +OpenVerdict verdicts_V
    +PartAssembly assemblies_P
    +RecorderSession sessions_3
    +MerkleFrontier frontier
  }
  class StreamBook {
    +StreamId stream
    +BookState state
    +Reason reason
    +u64 position
    +Level bids_L
    +Level asks_L
    +HeldMessage held_B
    +u64 hold_started_tick
    +Digest256 book_hash
    +IntegrityState integrity
  }
  class Level {
    +i64 price_atoms
    +i64 qty_atoms
    +u32 order_count
  }
  class HeldMessage {
    +u64 seqnum
    +u64 log_index
    +LevelChange changes_C
  }
  class OpenVerdict {
    +StreamId stream
    +u64 seqnum
    +u64 first_index
    +u64 first_tick
    +CopyRef copies_3
  }
  class CopyRef {
    +RecorderId recorder
    +Digest256 hash
    +i64 utc_recv_ns
    +u64 log_index
  }
  class MerkleFrontier {
    +u64 tree_size
    +Digest256 nodes_64
  }
  class BookState {
    <<enumeration>>
    VALID
    BUILDING
    UNKNOWN
  }
  TapeState *-- StreamBook
  TapeState *-- OpenVerdict
  TapeState *-- MerkleFrontier
  StreamBook *-- Level
  StreamBook *-- HeldMessage
  StreamBook --> BookState
  OpenVerdict *-- CopyRef
```

Suffixes such as `_16` and `_L` are fixed array sizes: L levels per side, B held messages per stream, C level
changes per held message, V open verdicts, P part assemblies. All are set from the day-one measurements. The
reducer cannot read the log, so a held message stores its parsed level changes, not a pointer to bytes.

### 4.3 Leaf and tree head

```mermaid
classDiagram
  class Leaf {
    +u64 log_index
    +Digest256 entry_digest
    +StreamId stream
    +BookState state
    +Digest256 book_hash
    +VerdictClose closed_at_this_index
    +u16 integrity_flags
  }
  class VerdictClose {
    +u64 seqnum
    +Outcome outcome
    +CloseReason reason
    +Digest256 hashes_3
  }
  class TreeHead {
    +u64 tick
    +u64 tree_size
    +Digest256 root
    +Signature signatures_3
  }
  class Outcome {
    <<enumeration>>
    AGREE_3_OF_3
    AGREE_2_OF_3
    ONLY_1_OF_3
    DIVERGENT
  }
  class CloseReason {
    <<enumeration>>
    ALL_REPORTED
    N_TICKS
    CAPACITY
  }
  Leaf *-- VerdictClose
  VerdictClose --> Outcome
  VerdictClose --> CloseReason
  TreeHead ..> Leaf : root over leaves 0..size-1
```

Every committed entry produces exactly one leaf. Leaves are hashed with RFC 9162's 0x00 prefix and inner
nodes with 0x01 (PROOF-1).

## 5. State machines

### 5.1 Book state, per stream

```mermaid
stateDiagram-v2
  [*] --> Unknown
  Unknown --> Building: new snapshot
  Building --> Valid: SNAPSHOT_COMPLETE
  Building --> Unknown: fault
  state Valid {
    [*] --> InOrder
    InOrder --> InOrder: next seqnum applied, new hash
    InOrder --> Holding: later seqnum before an earlier one
    Holding --> InOrder: missing seqnum arrives within N ticks
  }
  Valid --> Building: new snapshot
  Valid --> Unknown: fault
  Holding --> Unknown: N ticks or buffer full
```

"New snapshot" means a SNAPSHOT at or after the book's position. "Fault" means a divergent copy (BOOK-4), a
malformed, too-precise or out-of-range value (BOOK-2), or a capacity limit (BOOK-8). Only `Valid` has a book
hash (BOOK-7). `Holding` is part of `Valid`: the hash stays at the last applied
message while later ones wait (BOOK-5). A snapshot behind the book's position is recorded and ignored
(BOOK-6). Recovery from `Unknown` always goes through `Building`, so no book becomes valid without a complete
snapshot.

### 5.2 Verdict, per message

```mermaid
stateDiagram-v2
  [*] --> Open: first copy committed
  Open --> Open: copy from another recorder
  Open --> Closed: all three recorders reported
  Open --> Closed: Nth tick after the first copy
  Open --> Closed: table full, oldest closed by capacity
  Closed --> Closed: late copy recorded, verdict unchanged
```

The outcome is fixed at close: 3/3, 2/3, 1/3, or divergent if the copies hold more than one distinct hash
(ATT-1, ATT-2, ATT-4, ATT-5). A late copy never changes it (ATT-3); a late divergent copy still makes the
book unknown (BOOK-4).

### 5.3 Recorder connection

```mermaid
stateDiagram-v2
  [*] --> Connecting
  Connecting --> Subscribing: socket open
  Connecting --> Backoff: refused or failed
  Backoff --> Connecting: delay elapsed
  Subscribing --> Streaming: confirmation received
  Streaming --> Resyncing: gap, overflow or refresh timer
  Resyncing --> Streaming: new snapshot complete
  Streaming --> Dead: 30 s with no frame or heartbeat
  Resyncing --> Dead: 30 s with no frame or heartbeat
  Dead --> Backoff: socket closed, event recorded
```

Resyncing applies to one stream while the others keep streaming (REC-3). Backoff doubles up to its cap
(REC-4). Every transition except Connecting to Subscribing is also written to the tape as a recorder event.

## 6. Behaviour

### 6.1 Applying one committed entry

```mermaid
flowchart TD
  E["Committed entry"] --> T{"Entry type"}
  T -->|"Tick"| TK{"number is last + 1"}
  TK -->|"yes"| TA["Advance tick, close verdicts at N,<br/>end holds at N"]
  TK -->|"no"| TR["Record refused tick"]
  T -->|"ObservationPart"| PA["Add to assembly"]
  PA --> PC{"All parts present"}
  PC -->|"no"| PW["Keep waiting"]
  PC -->|"yes"| OB
  T -->|"Observation"| OB["Attestation: add copy"]
  OB --> FC{"First copy of this seqnum"}
  FC -->|"yes"| BK["Book: apply in order, or hold"]
  FC -->|"no"| SB{"Same bytes as the applied copy"}
  SB -->|"yes"| NC["Book unchanged"]
  SB -->|"no"| UK["Book unknown, BOOK-4"]
  T -->|"RecorderEvent"| RE["Record event, update session"]
  T -->|"HeadSignature"| HS["Store signature"]
  TA --> CK["Integrity checks, observe only"]
  TR --> CK
  PW --> CK
  BK --> CK
  NC --> CK
  UK --> CK
  RE --> CK
  HS --> CK
  CK --> LF["Append one leaf to the Merkle frontier"]
```

### 6.2 Tree head signing

```mermaid
sequenceDiagram
  participant L as Leader
  participant V1 as Voter 1
  participant V2 as Voter 2
  participant V3 as Voter 3
  participant P as Head publisher
  L->>L: commit tick k, a multiple of K
  Note over V1,V3: every voter applies tick k at the same index and computes the same head
  V1->>L: HeadSignature from voter 1
  V2->>L: HeadSignature from voter 2
  L->>V3: both signatures committed and replicated
  P->>P: two valid signatures, so publish head k
  V3->>L: HeadSignature from voter 3, added when committed
```

### 6.3 Leader failover

```mermaid
sequenceDiagram
  participant R as Recorder
  participant A as Old leader
  participant B as New leader
  participant G as Voters
  A->>G: tick 41 committed
  R->>A: observation, request sequence 900
  Note over A: crashes before replying
  G->>B: election, B leads, no ticks meanwhile
  R->>B: retry, request sequence 900
  B-->>R: committed once (ING-2)
  B->>G: tick 42
  Note over G: verdict windows count ticks, so the election costs wall time, not ticks
```

The ADR has sequence diagrams for an agreed update (HP-2) and for one recorder missing a message (FP-1).

## 7. Interfaces

| From | To | Protocol | Authentication | Carries |
|---|---|---|---|---|
| Venue | Recorder | WebSocket over TLS | none (no-login channels) | frames on DEPTH, EBBO, TRADE, INSTRUMENT, HEARTBEAT |
| Recorder | Venue | WebSocket | none | `SUBSCRIBE_NO_AUTH`, `UNSUBSCRIBE_NO_AUTH` only (REC-6) |
| Recorder | Voter | Velocita client protocol over Aeron | per-direction HMAC, session fence, request sequence | `Observation`, `ObservationPart`, `RecorderEvent` |
| Voter | Voter | Raft over Aeron UDP, Tailscale | per-direction HMAC, tape domain | Raft messages carrying `TapeEntry`s |
| Voter | Exporter | state page file | file permissions | committed state summary |
| Exporter | Prometheus | HTTP scrape | network allowlist | metrics with a `source` label |
| Head publisher | Reviewer | files | public-key signatures | tree heads |
| Archive | Verifier | files | public-key signatures on heads | log segments |

## 8. Guarantees by component

| Component | Guarantees |
|---|---|
| Recorder | REC-1 to REC-7, X-1, X-3 |
| Ingress | ING-1 to ING-4, X-2 |
| Raft core and persistence | RAFT-1 to RAFT-5 |
| Tick proposer | TICK-1 to TICK-3 |
| Attestation | ATT-1 to ATT-6 |
| Book engine and integrity checks | BOOK-1 to BOOK-9 |
| Merkle frontier, head signer, head publisher | PROOF-1 to PROOF-4 |
| Exporter | MET-1 to MET-4 |
| Offline verifier | VER-1 to VER-3 |

## 9. Review points, accepted

These came up while drawing the diagrams and were accepted with the design.

1. **A held message stores parsed level changes.** The reducer can't read the log, so the reorder buffer
   keeps each held message's parsed changes, bounded by C per message.
2. **The head publisher runs inside each voter.** Any voter can publish once two signatures are committed;
   the heads are identical, so duplicates are harmless.
3. **The verifier reuses the reducer library.** It proves the log and heads were not changed. The reducer's
   own correctness rests on the tests' independent reference builder.
4. **Heartbeats and unparsed frames open no verdict**, since they carry no usable `seqnum`. They are still
   recorded and still produce leaves.
5. **Recorders submit through their local voter**, which routes to the leader, so the deployment has no
   separate ingress process.
6. **Amendment 1** (ING-3 contiguity per recorder session) was approved and applied on 2026-11-11.
