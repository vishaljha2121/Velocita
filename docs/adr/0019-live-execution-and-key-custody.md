# 0019. Live True Markets execution: an inspecting signer outside the gateway, a fail-closed adapter

Date: 2026-09-30
Status: PROPOSED. Nothing here approves live trading, a credential, a venue call or a byte change.
The mock-level adapter is implemented and tested; every live step is gated in
`docs/LIVE_TRADING_JOURNEY.md` and needs the owner's approval.

No kernel byte change. No reducer change. The adapter sits behind the existing
`sim::VenueTransport` seam (ADR 0017), like the paper venue (ADR 0018).

## Context

What the recorded evidence says about executing on True Markets (TM):

- The trading MCP server advertises three trading verbs: `get_quote`, `trade_prepare` ("Create a
  market order ... If no payloads are returned the order was submitted directly") and
  `trade_execute` (`order_id`, `signatures`: "JSON array of Turnkey stamps, one per payload").
  Source: `docs/evidence/V2_TRUEMARKETS_MCP_SCHEMA_2026-09-18.md` (input schemas only).
- A quote carries a `quote_id`, `qty_out`, a flat 1 % `fee` in USDC, `issues`, and payloads: an
  unsigned transaction, a signing organization id and a wallet address. Executing a quote means
  signing that payload. Source: `docs/evidence/truemarkets_markup_2026-09-28/README.md`.
- REST paths are confirmed for quotes (non-committal), orders and order status; their bodies and
  the status response are not recorded (`MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`).
- There is no advertised cancel, no resting order and no client order id (D-23, D-28). `trade_prepare`
  may itself submit (D-26), and an error or pacing label proves nothing about submission (ADR 0009
  amendment, D-27).

So a live order is: obtain a signable payload, sign it, submit it, learn the outcome from status.
The account's funds sit in a wallet whose signer is custodial at the venue side ("signing
organization id"); what the client holds is a credential that authorizes a signature ("stamp").
Whoever holds that credential can move the account's funds.

Book guidance followed (sections read for this ADR):

- ch57 §"Exchange API keys and the signing service": keep private material off the trading host;
  the host sends a narrow request (caller, strategy, operation, venue, canonical payload, nonce,
  timestamp, policy version) to an isolated signer that inspects the operation, enforces limits,
  rejects replay, and returns a signature and signature id, never the key; no "sign arbitrary
  bytes" endpoint; if a remote signer is impractical, a local key holder with a dedicated UID, no
  shell, strict socket permissions and narrow egress; separate read, trade and withdraw capability;
  an append-only audit joining request id, decision, key version, policy version, signature id and
  venue response.
- ch19 §"The external custodian integration surface": one interface per custodian, signing as the
  single extension point, rate limiting enforced before the syscall. §"Idempotency and
  deduplication": there is no exactly-once delivery; idempotency keys must survive the process.
  §"The effect is ambiguous": ambiguity is a third outcome, resolved by a patient reconciliation
  sweep, never a coin flip. §"The effect cannot be retracted": the quality bar moves to the decide
  stage, and the remaining lever is size and rate limits.
- ch46 §"The ambiguous submission algorithm" and §"Reconciliation invariants": mark
  `UnknownSubmission`, stop duplicate risk, query, adopt or retry with the same idempotency policy,
  remain non-quoting if inconclusive; typed outcomes Match / Adopt / Conflict / Incomplete /
  Unavailable. §"Rate limits and quota management": reserve before encoding; a venue's "too many
  requests" is not a local refusal.
- ch45 §"Execution gateways": the gateway must be able to say accepted, rejected, unknown or
  filled; an unknown outcome does not release the reservation.
- ch52 §"Forward testing wins": the live canary is an information purchase; keep the capital cap,
  kill path and reconciliation stricter than normal limits while learning.

## Decision

### 1. Key custody: the gateway never holds a signing credential

A separate signer process holds the TM signing credential. The gateway asks it to sign through the
`Signer` interface (`include/velocita/v2/venue_signer.hpp`) and receives signatures and a signature
id. ADR 0010 is kept: the operator holds the key; no long-lived component other than the signer
touches it; the collector and `model/` still hold nothing.

- **Where it runs.** On nuc1, as its own systemd unit and UID (ch57's local key holder), because the
  quote is short-lived and a remote HSM or cloud KMS is a new external dependency and credential for
  a $25 canary. **Owner-approved amendment, 2026-10-13:** the integrated signer independently
  checks and records authority through Raft/Aeron. Replace the original `PrivateNetwork=yes`
  requirement with `PrivateNetwork=no`, `IPAddressDeny=any` and only the three pinned voter
  IPv4 `/32` allow entries, with `RestrictAddressFamilies=AF_UNIX AF_INET`. The gateway transmits the
  signed venue payload; the signer answers signing requests on a Unix socket owned by its UID, group-readable by the
  gateway's UID, with `SO_PEERCRED` checked on every connection. No shell, `NoNewPrivileges`,
  `ProtectSystem=strict`, core dumps disabled, `MemoryDenyWriteExecute`.
  This is address-level filtering, not a port allowlist: all ports on the voter hosts remain
  reachable, and those hosts must not relay signer traffic to an outside destination. Target-host
  enforcement, permitted voter traffic and denied non-voter traffic must be demonstrated before
  activation; unavailable cgroup-BPF enforcement must not silently remove this boundary. New
  network sockets must originate in the restricted service, not be inherited from an unrestricted
  supervisor. This amendment does not approve live signing, deployment or money movement.
- **Who can use it.** Only the gateway UID, only for `SigningIntent`s it builds from a committed
  `OrderEffect`. The operator loads the credential at signer start (systemd credential or a 0600
  file readable only by the signer UID); rotation restarts the signer, not the gateway.
- **What it signs.** Only a venue-returned payload whose decoded effect is inside the intent: same
  side, same asset pair and wallet, exactly the authorized base quantity, and a quote bound no wider
  than the kernel's reservation (buy: maximum total debit = limit notional + maximum fee; sell:
  minimum net credit = limit notional - maximum fee), not after the quote's expiry. Anything it
  cannot decode it refuses. There is no sign-bytes operation.
- **Replay.** A monotone request sequence per caller (the gateway starts above a durable reservation,
  `first_signing_sequence`) and a bounded cache of signed venue handles; after a signer restart the
  sequence floor is persisted, the handle cache is not needed because quotes expire.
- **Audit.** Append-only records of request sequence, effect id, verdict, key version, policy
  version, signature id and a digest of the payload bytes signed; never the payload's secret inputs
  or the credential. The adapter keeps the signature id on its order record so the venue response
  joins to it.
- **Blast radius.** Until TM confirms a trade-only, withdrawal-disabled credential (open question
  C-2 in the journey), a leaked credential can move the whole account. The structural bound is
  therefore the account balance: fund the TM account with the canary capital and nothing else.

### 2. The adapter is a state machine that never guesses

`TrueMarketsVenue` (`include/velocita/v2/truemarkets_venue.hpp`) implements `sim::VenueTransport`.
The HTTP/MCP session, JWT, JSON-RPC id matching and MCP envelope are an injected `TrueMarketsApi`;
the clock and the signer are injected. The mapping onto ADR 0009 and ch46:

| Step and what happened | SendOutcome | Why |
|---|---|---|
| local rate bucket empty (2 tokens: step 1 and submit) | `rate_limited` | refused before any byte left |
| quote flow: `get_quote` error, timeout, undecodable, no payloads | `pre_send_failure` | quotes are documented non-committal |
| quote flow: `get_quote` paced by the venue | `rate_limited` | non-committal, so pacing proves nothing is at the venue |
| prepare flow: `trade_prepare` error, timeout, pacing, undecodable | `ambiguous` | prepare may submit directly (D-26); a label proves nothing |
| prepare flow: reply without payloads | `ambiguous` | submitted directly, with no bound we could inspect |
| reply for another side, asset, quantity or fee asset | `pre_send_failure` | nothing committal was made; never signed |
| reply with an issue, beyond the FOK limit, or fee above cap | `business_rejected` + local `order_rejected` | the FOK cannot fill on authorized terms |
| signer refuses or is unavailable | `pre_send_failure` | nothing signed, nothing submitted |
| quote older than `quote_ttl` after signing | `pre_send_failure` | a stale quote is never submitted |
| `trade_execute` accepted | `accepted` + `order_acknowledged` | the fill follows from status |
| `trade_execute` with a verified definitive rejection word | `business_rejected` | none is verified, so the list is empty |
| anything else from `trade_execute` | `ambiguous` | it may be executing |
| any later send for an order after a committal call | `venue_duplicate` | **no resend without proof** |

Reconciliation (`query`): an order this process has no record of, or one whose committal call left
no id the status endpoint is known to accept, answers `unavailable`, never `not_found`, because TM
takes no client order id. An order that never reached a committal call answers `not_found`.
A status of settled books identified evidence (execution id derived from the transaction signature,
so a restart derives the same id); failed or expired answers `not_found` and the kernel releases the
hold; pending answers `known` with nothing filled. A partial, over-fill, price beyond the limit or
fee above the cap is booked as reported where it fits the order and flagged `inconsistent`, which the
gateway treats as a reconciliation conflict and halts new risk (ADR 0017 §5).

Rate limits are local token buckets checked before encoding (ch19, ch46); the venue's limits are
unknown, so the defaults are conservative and are a configuration, not a claim.

### 3. Two flows are implemented; the quote-bound flow is the target

- `quote_then_execute`: step 1 is non-committal, and the signed payload is the priced transaction,
  so the bound the signer checks is the bound that executes. It needs TM to confirm the submit call
  for a quote (Q-1) and that the transaction enforces the quoted amounts (Q-3).
- `prepare_then_execute`: the recorded MCP sequence. Every uncertain prepare is unknown exposure, so
  it halts more often and a lost prepare reply cannot be reconciled at all (no id).

The configuration names the flow explicitly; there is no default.

### 4. Live is refused by construction in every build today

`--venue truemarkets` requires `--arm-live-venue` AND `--signer-endpoint`, and is then refused
because `kLiveVenueTransportCompiled` is `false`: no HTTP/MCP client and no signer client exist.
Enforced four ways, each under test:

1. `evaluate_live_arming` and the compiled-transport constant (unit tests; four mutants killed).
2. The gateway refuses in argument parsing, before any key file, Aeron directory or socket
   (four `v2-gateway-truemarkets-refuses-*` ctests run the binary; two mutants killed).
3. `v2-truemarkets-no-network-symbols` fails if the adapter library's objects reference any socket,
   name-resolution or TLS symbol (a mutant with one `socket()` call is caught).
4. The node interlock (no venue library in any voter or operator binary) now covers every non-gateway
   V2 binary.

Making live possible later is a reviewed change: a transport implementation, a signer client, the
constant flipped, and a new approval.

## Alternatives rejected

- **The gateway holds the credential.** Simplest, and the gateway already has every input. Rejected:
  it makes the process that talks to the venue and parses its replies also the key holder (ch57),
  and it breaks ADR 0010's single key-holder rule.
- **A generic "sign these bytes" service** (including a cloud KMS or HSM used directly). Rejected as
  the design, not as a key store: without inspection it is a remote copy of the key (ch57). A KMS or
  a hardware token may later hold the key behind the inspecting signer.
- **Operator approves each signature by hand.** Strongest custody, and it is the plan for the first
  controlled trade (journey step 9). Rejected for the canary: quote lifetimes are seconds-scale, so
  an automated strategy cannot wait for a person.
- **Let TM sign server-side ("submitted directly").** No key on our side at all. Rejected: there is
  then no point at which the transaction's bound is inspected before it can execute (D-28), and ADR
  0011 §6 requires an execution-enforced bound.
- **Retry an ambiguous submit in the adapter.** The signature is in hand. Rejected, as in ADR 0009:
  a second submission is a second swap; only reconciliation may conclude.
- **Report a lost prepare or submit as `pre_send_failure`.** Keeps the gateway running. Rejected: it
  is exactly the unsafe mapping the ADR 0009 amendment withdrew.

## Consequences

- The adapter's order table holds 64 orders and never forgets one that may have been submitted, so a
  gateway needs a restart (with reconciliation) after 64 committal orders. At canary cadence this
  does not bind; it is visible as `pre_send_failure` before any call.
- The swap's natural unit is exact input, the kernel's is lots at a limit price. The adapter asks for
  an exact base quantity (`qty_unit=base`) on both sides. For sells this matches the recorded
  semantics; for buys the reply semantics of `qty_unit=base` are unrecorded and assumed (Q-2). If TM
  cannot do exact-base buys, ADR 0011's swap operation (a kernel byte change) is needed first.
- The execute reply, status reply and their words (`submitted`, `filled`, `failed`, `expired`) are
  fixture shapes. They must be replaced from recorded replies before arming.
- The signer's payload decoder for a real Solana or Base transaction is the largest unbuilt piece.
  If a routed swap's instructions cannot be decoded well enough to prove the bound, the signer must
  refuse, and live stays blocked.
- **Start-of-process reconciliation is part of the live path, not an option.** The paper venue's
  balances live in process memory and reset on gateway restart; a real account does not. So every
  live gateway start must, before `allow_new_risk` can take effect, read the venue's balances
  (`get_balances`) and compare them with the kernel's position and the operator's recorded funding,
  at zero lot tolerance for the base asset and a documented atomic tolerance for USDC (ch50
  §"Cadence and zero tolerance"). A mismatch keeps new risk stopped. This is not yet implemented:
  the adapter has no balance call, and a balance difference alone can never become a fill (ADR 0011
  §6); it only blocks.
- **Elections halt the gateway today.** Every leader change stops new risk (ADR 0017 §5), and the
  wifi-only cluster sees election bursts (term 49 to 191 in six hours, both remote hosts dropping
  from nuc1's view at once). A live canary would spend most of its time halted, and each manual
  restart re-enters the startup path above. Live needs Raft pre-vote and an automatic re-arm that
  only fires after a reconciliation pass finds no unknown outcome and a matching balance.
- A failed on-chain settlement after acceptance is released through the resting-order reconciliation
  of ADR 0008 (`order_reconciled` with venue id 0), not through a rejection, because the kernel
  refuses a rejection for an acknowledged submit.

## Amendment 2026-10-09: the signer process exists, with a test key only

The owner approved building the signer with a test key, and the venue transport against a local
mock, with nothing linked into a release image and the compiled live switch left off. This
amendment records the signer. It approves nothing further.

What was built (`velocita-v2-signer`, library `velocita_v2_signer`):

- **The decision.** `SignerCore` signs a request only when the caller, venue, account and
  instrument are the configured ones, the request sequence is above the durable floor, the venue
  handle has not been signed before, the request has not expired, and every payload decodes to the
  same side, the same base quantity, the same handle and a quote bound no wider than the intent
  (buy: debit at most the bound; sell: credit at least the bound). Anything else is refused with a
  typed verdict.
- **Order of effects.** The sequence floor is made durable first, then the key is used, then the
  audit record is written, and only then does the signature leave. If the floor or the audit
  record cannot be written, no signature is returned.
- **Replay.** The floor is a 16-byte checksummed file replaced atomically. A restarted signer
  refuses every sequence it has used. The last 256 signed handles are remembered in memory.
- **Audit.** One line per decision, signed or refused, appended and synced: sequence, effect,
  verdict, key version, policy version, signature id, SHA-256 of the first payload.
- **Socket.** A Unix stream socket, mode 0660. The peer's user id is read from the kernel
  (`SO_PEERCRED` on Linux) and compared with the one allowed id before any byte is read. One
  request per connection, length-prefixed and checksummed.
- **Client.** `SocketSigner` implements the `Signer` interface the adapter already takes. Any
  failure, timeout or malformed reply is `unavailable`, which the adapter treats as nothing signed.

What keeps it inert:

- **Only a test key.** The one key option is `--test-key-file`, and the file must begin with the
  line `velocita-test-signing-key-v1`. The signature is an HMAC under that key. No venue accepts it.
- **Only mock payloads.** The one payload decoder reads the fixture format (`MOCKTX1;...`). A real
  venue payload does not decode, so it is refused. The decoder for a real transaction is still the
  largest unbuilt piece, as the Consequences section says.
- **Not shipped.** No gateway, voter or other binary links the signer library, no image copies the
  binary and no unit starts it. `v2-signer-not-in-release-images` fails if any of that changes.
- The gateway still cannot be started against True Markets: `kLiveVenueTransportCompiled` is false.

One limit to know: request expiry compares the gateway's monotonic clock with the signer's, so the
two must run on the same host, as section 1 already places them.

Tests: 14 in-process tests and one that runs the binary across a restart; a signature vector
computed independently; 69 mutants, 67 killed, 2 repeated by the payload comparison.

## Amendment 2026-10-09 (2): the venue transport exists, against a local mock only

Same approval as the signer amendment above. It approves nothing further.

What was built (library `velocita_v2_truemarkets_transport`):

- **HTTP.** One request per connection, `Connection: close`. Requests are refused before sending if
  the host, path or token contains a space or control character, so nothing can be injected into
  the header. Replies are read by content length, by chunks, or to the close of the stream, within
  fixed size limits.
- **The MCP envelope.** A reply is accepted only if it is JSON-RPC 2.0 with the request's own id.
  The tool's text is recovered exactly, from a plain JSON body or from an event stream. `isError`
  and a JSON-RPC `error` both arrive at the adapter as a tool error.
- **One meaning per failure**, in the adapter's terms:

  | What happened | The adapter is told |
  |---|---|
  | reply with the tool's text | completed |
  | reply marked as an error, or any HTTP status outside 2xx except 429 | tool error, with the body |
  | HTTP 429 | rate limited |
  | no connection, no reply in time, closed early, not HTTP, another request's id, an envelope it cannot read | no response |

  The adapter already maps "no response" on a committal call to unknown exposure and never resends
  (section 2). The transport does not retry.
- **Status reads** are a GET of the path the adapter gives.

What keeps it inert:

- **It can only reach this machine.** The one byte stream provided, `LoopbackStream`, takes a port
  and always connects to the loopback address. There is no name lookup and no TLS in the library,
  so it cannot open a connection to the venue.
- **Not shipped.** No gateway, voter or other binary links it and no image carries it. The same
  ctest that guards the signer now fails if that changes, if the stream can name another address,
  or if `kLiveVenueTransportCompiled` stops being false.

What is assumed, not recorded: the shape of the envelope (`result.content[0].text`, `isError`) is
the MCP convention and matches what `tools/mcp-read.py` reads from the public market-data server.
The trading server's replies to `get_quote`, `trade_prepare` and `trade_execute` have not been
recorded, so the fixture shapes in the adapter's tests remain fixtures. They must be replaced from
recorded replies before anything is armed.

End to end, at mock level: one test sends an order through the adapter, the transport, a local
mock venue speaking HTTP, and the signer over its Unix socket with the test key. The venue receives
the test signature of the quoted payload, the fill comes back from a status read, the signer's
audit log holds one line, and a second send of the same order is refused as a duplicate.

