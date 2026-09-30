# M2: startable V2 node — design review

2026-09-24, CP-052. **Status: OWNER APPROVED decisions C, D and E as written
("approved C,D,E; proceed").** A, B, F and G are accepted recommendations.
Implementation follows §5, RED-first; evidence per step under `evidence/m2_*`.
Branch/HEAD `v2/phase-5-corrections` / `d44a4a9`; remediation uncommitted.

## 1. Problem and why it matters

The V2 library is a kernel with no process around it. CMake builds V2 tests,
reports and benchmarks, but no V2 node. Every remaining gate needs real
processes: the E1/E2/E3 machine runs (`V2_E_EXIT_SIGNOFF_PLAN.md`), NUC/Pi
deployment (M3), observability (M3a), shadow trading (M4). The book puts this
exact step first: "submit a command, kill the leader, retry, restore, replay,
and show the same position" (ch45, "Build the boring proof").

Roadmap M2 exit: two independent local processes start, restart, partition,
rejoin and replay; no venue send capability exists in the binary.

## 2. What already exists and is reused unchanged

| Piece | Source | Evidence |
|---|---|---|
| Embedded Aeron media driver + UDP publication/subscription | `aeron_runtime.hpp` (V1) | V1/V1.1 Pi gates |
| Owner-loop scheduler with bounded per-turn work budgets | `owner_loop_scheduler.hpp` (V1.1) | V1.1 Pi evidence |
| Seqlock shared metrics page (owner does atomic stores only) | `shared_metrics.hpp` (V1) pattern | V1 exporter |
| Kernel, WAL worker, recovery, compaction, authority proof | `ReplicatedKernel` | 98/98 host suites |
| Peer payloads: bound peer frames (integrity + lineage) | `bound_peer.hpp` | approved bytes |
| Command/result payloads: canonical kernel codec | `kernel_codec.hpp` | approved bytes |
| SHA-256 (one-shot + incremental) | `digest.hpp` | goldens |

No new command, result, peer-message, WAL, snapshot or economic bytes.

## 3. Decisions

### A. Process and thread model (no new bytes; recommended)

Threads in one node process:
- **owner**: the only thread that touches `ReplicatedKernel`, the Aeron
  subscriptions/publications, and the metrics page (atomic stores only);
- **WAL worker**: started at boot (`start_writer`), the first time worker mode
  is the production default;
- **Aeron driver threads**, embedded, as in V1.

A signal handler only sets an atomic flag. Nothing else calls into the kernel.
C++ lens: this keeps every existing single-owner assumption valid.
`ReplicatedKernel` is not thread-safe and does not need to be.

### B. Peer transport: Aeron UDP (no new bytes; recommended)

One subscription per node and one publication per remote voter. The payload is
the existing bound peer frame, wrapped in envelope C. The kernel's outbound
mailboxes are already bounded (66,680 bytes per node); the owner offers at most
`peer_offer_limit` frames per turn and keeps back-pressured frames in the
mailbox.

Alternative: TCP. It gives ordered bytes, but head-of-line blocking stalls
heartbeats behind a large AppendEntries, and Raft already tolerates loss.
Aeron is V1-proven on the Pi and is the book's choice for Raft traffic (ch45).

### C. Authenticated envelope — NEW BYTES, needs approval

**Problem.** Bound peer frames prove integrity and lineage, not that the sender
holds a voter's secret. Any process that can reach the UDP port (the gateway,
a stale old process, another host) could inject frames. The lost-voter review
also needs a transport fence: frames from an OLD process incarnation must be
refused after a restart or repair.

**Proposal.** One envelope for all three roles (peer, ingress, response):

| Field | Bytes | Meaning |
|---|---|---|
| magic `VLA1` | 4 | envelope kind |
| version | 2 | 1 |
| role | 2 | peer / ingress / response |
| sender id, recipient id | 4 + 4 | voter id or caller key id |
| session epoch | 8 | strictly increasing per sender process start |
| sequence | 8 | strictly increasing within one session and direction |
| body length | 4 | ≤ 32,768 (`kMaxBoundPeerBytes`, the largest body) |
| body | n | existing bound peer frame, or canonical command/result bytes |
| tag | 32 | HMAC-SHA256 over every byte above |

Fixed overhead is 36 header + 32 tag = 68 bytes, so the largest envelope is
32,836 bytes.

Keys: one 32-byte pre-shared secret per link or caller, in a 0600 file owned
by the voter's OS user, path in config, never argv or env. The per-direction
key is `HMAC(link_secret, "velocita.auth.v1" || role || sender || recipient ||
cluster incarnation)`, so a frame cannot be reflected back or across roles.

Replay fence: each node persists a **session-epoch counter** (new 16-byte local
file: magic, counter, CRC; written and fsynced ONCE at startup, before any
send, off the hot path). A receiver keeps the highest (epoch, sequence) per
sender and refuses anything lower. After a restart, the old process's frames
are refused as soon as the new one has spoken. This is the missing transport
fence named in `F5_LOST_VOTER_REPAIR_REVIEW.md`.

Verification happens BEFORE decoding the body and before any allocation.
Refusals are counted by reason and never reach the kernel.

Trade-offs:
- There is no confidentiality: frames are authenticated, not encrypted. Add
  WireGuard between NUC and Pi if privacy is wanted; safety does not depend on it.
- Symmetric keys: a key holder can forge frames on its own link. That is
  acceptable, because the key holder is the voter. The gateway runs as a
  different OS user and cannot read voter keys.
- Cost: one HMAC per frame (two SHA-256 passes). Tens of µs on the Pi for a
  32 KiB AppendEntries; to be measured, not assumed.

Alternatives: mTLS over TCP (confidentiality too, but an OpenSSL dependency,
certificate lifecycle and TCP head-of-line blocking); WireGuard alone (no app
bytes, but host-level only, so no process fence and any process on the host
can speak as the voter). Rejected: none, which leaves unauthenticated voters
and callers.

### D. Ingress and results — NEW BYTES, needs approval

**Problem.** Callers (operator CLI, later gateway and edge) must submit
commands to the leader, which may be either host, and get their exact result.
The roadmap requires: "Authenticate the caller before mapping it to an operator
CallerId; a numeric band is not authentication."

**Proposal.**
- Role `ingress`: body = one canonical kernel command (≤ 1,789 bytes) plus a
  1-byte lane (normal/control). The caller key id maps to exactly one `CallerId`
  in config. The node refuses a command whose embedded `CallerId` differs from
  the authenticated one, before proposing it.
- Role `response`: body = status (admitted / not_leader + leader id / refused
  reason / committed result using the existing 128-byte canonical result).
- Both voters listen. A non-leader answers `not_leader` with a leader hint.
  There is no forwarding in MVP.
- Correlation: each READY result from `take_committed_result` is sent to its
  caller. If the caller is gone, the result is dropped; a retry of the same
  request commits and the reducer returns the original result. That is the
  existing receipt semantics, not a new promise.
- Bounds: one in-flight admission per caller per owner turn;
  `client_fragment_limit` from the V1.1 scheduler; oversized frames refused
  before allocation.

#### D encoding (exact bytes, derived from the approved contents; CP-055)

Little-endian. Ingress body: `lane u8` (the existing `ProposeLane` value:
0 normal, 1 control) followed by one canonical kernel command (1..1,789 bytes).

Response body, first byte `kind`. Every kind echoes `request_sequence u64` and
`payload_digest[32]`, copied from the command's own identity, so a caller
matches a response to its request without relying on arrival order. They are
zero only when the command did not decode.

| kind | name | remaining fields | bytes |
|---|---|---|---|
| 1 | admitted | sequence, digest, term i64, index i64 | 57 |
| 2 | refused | sequence, digest, reason u8, leader_id i32 (0 = unknown) | 46 |
| 3 | committed | sequence, digest, term i64, index i64, canonical result [128] | 185 |

The refusal `reason` is the existing `ProposeStatus` value (2..9), or an
ingress-level code: 64 caller_mismatch (the command's CallerId is not the
authenticated key's), 65 malformed_command, 66 not_ready (recovery barrier),
67 busy (per-caller admission bound). `leader_id` is meaningful only for
`not_leader` (2).

### E. Node configuration — grammar needs approval (local file, not replicated)

A strict text file: `key = value` lines, `#` comments, 16 KiB cap. Unknown,
duplicate or missing keys refuse startup. Its SHA-256 is logged and exported.

Keys: `node_id`, `data_root`, `startup = bootstrap | recover` (explicit, never
inferred), `voter.<id>.endpoint`, `voter.<id>.key_file`, `ingress.endpoint`,
`caller.<key_id>.caller_id`, `caller.<key_id>.key_file`, `deployment_mode`,
`archive.destination_identity`, `history.max_wal_bytes`, owner-loop budgets.

The voter set must equal the persisted static identity; the existing startup
check already refuses drift.

### F. Startup, shutdown and faults (no new bytes; recommended)

- The recovery barrier refuses ingress until the kernel is constructed,
  history recovery is complete and (in `two_host_pilot`) archive verification
  is done. Peer traffic flows during the barrier; the kernel already gates it.
- SIGTERM: stop ingress, pump durability for a bounded time, exit. SIGKILL is
  the crash test.
- Any kernel storage/ownership/catalog latch means log the reason, publish
  `failed` to the metrics page, and exit non-zero. Fail-stop: restarting is
  the supervisor's job (systemd in M3); the node never continues.

### G. Minimum health for M2 (no new bytes; recommended)

The V1 seqlock metrics-page pattern with V2 cells: role, term, leader, commit,
durable, applied, retained log, ready/reserved results, WAL backlog, refusal
counts by reason, last-progress timestamps, failed flag. A separate read-only
`velocita-v2-status` tool prints it. The full M3a dashboard comes later.

## 4. Out of scope for M2

Gateway process and venue send (the binary must not link
`velocita_v2_venue_*`, asserted in CMake); market-data edge; dashboards; the
archive-copy transport to the backup host (M2b, reusing envelope C with an
`archive` role, and a separate approval if new bytes are needed); systemd
units (M3); automatic leader forwarding.

## 5. Implementation sequence, each step RED-first

1. Envelope codec + HMAC: pure functions. Goldens come from Python `hmac`/
   `hashlib`, an independent oracle. Adversarial: truncation, every-bit tag
   flip, role/direction reflection, length lies.
2. Session fence: pure state machine; epoch file create/advance/corrupt/torn.
3. Config parser: pure; adversarial grammar cases.
4. `V2Node` library class: owner loop over an injected transport interface,
   plus a deterministic in-process fake transport. Tests drive two nodes in
   one process with an independent reducer oracle.
5. Aeron transport adapter, `velocita-v2-node` binary, `velocita-v2-client`
   operator tool, `velocita-v2-status`.
6. Process tests on loopback: two real node processes, plus a small
   test-controlled UDP relay to partition and heal. Start, commit, SIGKILL,
   restart, partition, rejoin, replay, SIGTERM; digests compared with the
   independent reducer. Old-session frame replay refused. A CMake assertion
   that the node links no venue library.

Each guard gets a mutation check against its named assertion. Maximum two
helper agents on disjoint files (for example, step 1 and step 3 in parallel).

## 6. Approval requested

- **C**: envelope fields, HMAC-SHA256 tag and key derivation, per-link key files,
  and the 16-byte session-epoch file.
- **D**: ingress/response roles and body layouts, caller-key → CallerId mapping,
  and not-leader redirect without forwarding.
- **E**: the config grammar and key set.

A, B, F and G reuse existing mechanisms and add no bytes; they are recommendations,
listed for visibility.
