# F3 canonical execution catalog v1 — material contract review

2026-09-21. APPROVED by the owner; implementation in progress. Extends the accepted direction
and recovery boundary in ADR 0012; does not reopen those decisions. The two
unbound request-body APIs stay quarantined. No venue claims are inferred here.

Implementation: CP-007 delivered catalog schema 1; CP-008 integrates immutable
installation and mandatory binding with command/result/snapshot schema 8.
Approved choices below are unchanged. Evidence is in the remediation checkpoints.

## Recommendation in operational terms

One immutable artifact, at most **16 account/instrument/spec/capability rows**,
loaded once per process. This is a metadata/version limit, NOT a new limit of
sixteen live orders. All historical row versions used by this deployment must
already exist in the installed artifact. Changed metadata outside it stops new
work until a separately reviewed lineage transition; it is not silently appended.

Every row names the account as well as the instrument. Current risk amounts are
MoneyMicros without a currency field. Therefore require every row for an account
to agree on its quote-asset and network identity: comparing quote micros from two
different assets as one budget would otherwise be dimensionally meaningless.
This is an approved preventive constraint, not a reproduced live-trading loss.
No FX conversion or cross-currency netting is added. This extra account binding is
a material choice explicitly approved, not a consequence hidden in implementation.

## Canonical bytes

Integers are little-endian, no host padding or native struct hashing. Header:

| Field | Bytes | Rule |
|---|---:|---|
| Magic | 4 | ASCII `V2EC` |
| Catalog schema | 2 | u16, exactly 1 |
| Row count | 2 | u16, 1..16 |
| Total artifact length | 4 | u32, exact full byte count |

Rows follow in strict lexicographic numeric order by
`(account_id, venue_id, instrument_id, instrument_spec_version, capabilities_version)`.
Duplicate keys and trailing bytes refuse; the decoder does not sort or normalize
untrusted input into a different artifact.

| Per-row field, in order | Bytes | Rule |
|---|---:|---|
| AccountId | 8 | nonzero u64 |
| InstrumentExecutionProfile | 66 | existing canonical field order/schema 1, validated |
| VenueCapabilities | 21 | existing canonical field order/schema 1, validated |
| tick_quote_micros | 8 | positive i64, quote micros per tick per whole base unit |
| lot_base_atoms | 8 | positive i64, base atoms per quantity lot |
| base_decimals | 1 | u8, 0..18; checked exact product agrees with profile |
| encoder family ID | 2 | nonzero u16, no venue is assigned by this proposal |
| encoder schema version | 2 | nonzero u16, no implicit latest/default |
| network identity | 1 + n | n=1..96, exact opaque printable ASCII bytes |
| base-asset identity | 1 + n | n=1..96, exact opaque printable ASCII bytes |
| quote-asset identity | 1 + n | n=1..96, exact opaque printable ASCII bytes |
| venue instrument routing key | 1 + n | n=1..96, exact opaque printable ASCII bytes |

Text is length-prefixed, not NUL-terminated on wire. No trimming, case folding,
Unicode conversion or symbol-to-address guessing. Printable ASCII means bytes
0x20..0x7e inclusive. The canonical decoder validates bytes, not venue existence;
the gateway refuses identities unsupported by its independently verified backend.
An adapter must still encode/escape routing bytes correctly; opaque identity
strings are not pre-escaped JSON fragments. Neither strings nor encoder IDs may
carry credentials, private keys or authorization tokens.

The profile and capabilities must name the same venue. A given
`(venue,instrument,spec_version)` must name identical profile, precision, network,
asset, routing and encoder facts wherever repeated across accounts or capability
versions; a given `(venue,capabilities_version)` must name
identical capability facts. Account-scoped row selection does not permit one
version to mean two different economic contracts. Base and quote identities must
differ. Post-only, GTC/IOC/FOK and operations are explicit existing masks; unknown
bits refuse. No market/swap operation or native/overlapping replacement is enabled.

Maximum row = 8+66+21+8+8+1+2+2+4*(1+96) = **504 bytes**.
Maximum artifact = 12+16*504 = **8076 bytes**, within an **8192-byte wire bound**.
Approved decoded immutable-context ceiling: **16 KiB**, separate from TradingState.
Measure sizeof/alignment on 64- and 128-byte cache-line builds; no TradingState
or snapshot budget increase. The already-approved single root costs 32 snapshot
bytes. Lookup is a bounded linear scan of at most sixteen rows; no hashing table,
per-order allocation, or file/network access during apply.

## Digest and trust boundary

Root = SHA-256 of the ASCII bytes `velocita.execution-catalog.v1`, one zero byte,
then the complete canonical header and rows. The root is not embedded in its own
preimage. Decode bounds and validates canonical form, recomputes the root, and
matches the committed/expected root before exposing an immutable context.

Catalog integrity is NOT venue verification. An unsupported encoder family or
unverified real venue mapping must still refuse at the gateway; no generic
caller-selected JSON schema or `verified=true` escape hatch. Tests may use
explicit synthetic identities and a test-only encoder, not pretend a real venue
supports limit/post-only behavior. Live adapter admission remains a later gate.

## Reducer/result and recovery details

Use existing request deduplication and control checks for the approved install
command. Approved appended ApplyCode names, in this order after the current last
code `reconciliation_required`: `catalog_not_installed`,
`catalog_installation_blocked`, `immutable_catalog_conflict`,
`catalog_row_not_found`, `catalog_binding_mismatch`. Existing ordinals stay fixed;
append exact-value golden tests before encoding. These describe deterministic
state/row refusals, never a locally missing artifact: that remains a node failure
before apply/receipt/index advancement under ADR 0012.

Catalog context must be explicit for stage, authorization and historical binding.
No context-free overload may quietly fall back to the old authorization behavior.
Recovery validates both snapshot state and replay dependencies before publication.
An incompatible or missing historical artifact never falls back to current files.
The snapshot root alone is not an installation/readiness protocol.

## Alternatives and remaining review boundary

- Unbounded strings/rows: simpler schema, but violates bounded parsing and startup
  memory. The fixed limits have an explicit availability cost: oversize artifacts
  are rejected, not truncated.
- Display symbols only: shorter, but cannot disambiguate assets across networks.
- Floats or inferred precision: can change prices/quantities; keep exact integers.
- Drop AccountId/denomination consistency: saves eight bytes per row but leaves
  the risk-unit assumption outside the immutable binding. Recommended against.
- Freeze a real venue encoder ID now: no verified mapping supports that claim.
  Keep the production send path closed until its own evidence exists.

Owner approval covers this complete catalog schema, bounds, account-denomination
constraint and appended deterministic result codes. A separate-process transport
envelope, authority proof protocol and static cluster identity remain separate
material contracts; this approval does not encode or approve them.
