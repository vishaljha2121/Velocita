# 0011. Proposed swap execution model: unresolved admission and settlement contracts

Date: 2026-09-18; corrected after adversarial review 2026-09-21
Status: PROPOSED. No new operation, economic rule, or canonical bytes approved here.
This revision supersedes the original reasoning; Git retains the withdrawn text.

## 1. What the evidence establishes

The captured MCP input schemas advertise get_quote, trade_prepare ("Create a
market order"), and trade_execute. No captured tool advertises price, time in
force, cancellation, open orders, or a book. This does NOT establish that the
whole venue lacks those capabilities: MCP is not the REST API. Nor does it
establish an RFQ routing mechanism. Output schemas were not captured; absence
of a quote-id input is not proof that quote responses contain no identifier.

The schema describes chain values solana/base but declares a string, not an
enum. Additional properties are not explicitly forbidden. We must not infer
accepted undocumented fields, or assert their rejection. Keep the resting-order
mechanisms; do not claim True Markets market-making support. PURR admission
remains unverified, not proven impossible by the chain description alone.

## 2. Economics: illustrative arithmetic, not an execution guarantee

For equal-size buy/sell legs N, constant round-trip loss fraction c, and loss
budget B, approximate two-leg volume is 2B/c. Capital, time, inventory, fixed
fees, liquidity, rate limits and leaderboard eligibility remain constraints.
Selling the diminished proceeds also makes 2N an approximation.

The historical Jupiter samples are sequential indicative quotes, not executions,
not a proven lower bound on True Markets costs, and not evidence that a future
route will reproduce them. The sell quote does not include our own executed buy.
A cross-provider price difference also mixes route, timing, fees and precision;
it cannot uniquely identify a venue markup.

At B=$100, c=0.5 bps gives $4M; c=71.5 bps gives about $27,972;
c=771.8 bps gives about $2,591, NOT $28k. For fixed B, volume ratios are the
inverse of cost ratios. The former table's extra 10/30/100 bps must mean
ROUND-TRIP cost; a 10 bps per-leg charge contributes approximately 20 bps.
These are sensitivity examples, not feasible volumes or sizing advice.
Sparse samples do not establish a stable superlinear impact law.

## 3. Proposed shared effect operation; not yet an implementation decision

A swap operation might reuse identity, kill epoch and delivery fencing.
That avoids duplicating safety infrastructure, but it does not make economic
reservation or settlement interchangeable with limit orders. Reject adding a
gateway-only special case: the reducer must authorize the same economics sent.

EconomicActionProposal, authorization, reservations, risk valuation, snapshot
validation, canonical codecs and replay all need a coherent design before an
OrderEffect extension. Changing only the outbox would detach reserved exposure
from the actual spend. Existing RiskAccount identity is account+instrument,
not VenueId; distinct venue IDs do not automatically isolate balances or limits.

## 4. Proposed routing identity

Distinct chain-specific venue identities can select explicit encoders and reduce
accidental routing. They cannot make omission impossible: the encoder can still
omit chain. Require a tested mapping and fail-closed capability admission.
The schema describes omission as CeFi routing; do not silently default a DeFi
intent to that route. Custody/account segregation must be independently specified.

## 5. Quantity and bytes remain open

Explicit exact-input denomination is a promising proposal: quote spend for buys,
base spend for sells. However fees, gas, inventory units and worst-case input
must also be reserved. Exact input does not establish bounded output loss.

The previous 166 -> 176 "after padding" statement conflated canonical wire size
with C++ object layout. Adding one byte and eight bytes to a 166-byte canonical
encoding gives 175 bytes unless another byte is explicitly specified.
No size/version is frozen by this ADR. Validators must be operation-specific:
existing cancel payloads intentionally normalize economic quantities to zero,
so a global "exactly one quantity nonzero" rule would break accepted cancels.
A quantity-basis byte cannot itself prevent reducer/encoder disagreement.

## 6. Admission requires an enforceable execution bound

Expected output is a forecast, not an execution condition. Signing a transaction
after checking its forecast does not stop a worse fill. Before live admission,
verify a signed, execution-enforced minimum output or equivalent maximum debit,
including assets, recipient, network, fees, validity and settlement semantics.
The committed intent must carry the approved bound, and the gateway must verify
that the signed payload enforces it. Reject the former proposal to rely solely
on a pre-sign expected-output comparison.

The advertised prepare call may submit directly without returning payloads.
"Submitted" does not mean filled, successful, or final. An ambiguous prepare
therefore retains risk and requires reconciliation; it must not become a
retryable pre-send failure or a completed effect with a fabricated fill.
If this submission mode cannot be excluded before calling prepare, inspecting
its response is too late to establish pre-trade bounded-loss admission.

Balance differences alone cannot identify which operation settled: deposits,
withdrawals, fees and other activity can produce the same difference.
Reconciliation needs attributable execution evidence and a verified finality
rule. Do not translate aggregate balance movement into an order fill.

## 7. Measurement before strategy sizing

tools/measure-markup.py is now an owner-operated QUOTE CAPTURE tool. It validates
inputs, sends both required login fields, suppresses authentication responses,
refuses non-allowlisted calls and treats MCP tool errors as failures.
It does not guess quote units, size a sell from nested fields, or calculate markup.
Exit 2 means evidence captured but measurement incomplete, not a strategy signal.
Review captured data for privacy before sharing.

Next measurement work requires an actual response contract with verified units
and economically comparable two-leg quotes. No venue calls were made for this
review. The owner decides whether/when to authenticate; do not solicit the venue.

## 8. Preserved and blocked mechanisms

Keep the deterministic reducer, existing limit-order lifecycle and fences.
Do not claim resolve_two_step_send applies unchanged: it assumes prepare cannot
submit. ADR 0009 now scopes that premise explicitly.

A kill epoch can stop new admitted sends; it cannot undo an in-flight trade.
Absence of an advertised cancel is not evidence of completed settlement and
must never justify reporting successful cancellation or releasing uncertain risk.

S1-S3 process/observability work can proceed independently after the owner
resolves gateway topology. Market inputs belong in DecisionFrame; execution
evidence belongs in VenueObservation. No attribution to the external Velocita
book is justified without reading the relevant text.

## 9. Approval and live gate

Owner approval is required before encoding any proposed economic/byte contract.
Live trading additionally requires verified execution bounds, identity/retry,
settlement attribution, capability and fee contracts, and end-to-end target gates.
The system cannot presently promise an unbreachable loss budget on this surface.
Leaderboard eligibility and strategy choice are separate owner decisions.
