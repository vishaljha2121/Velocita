# V2 model and market-data edge design

Status: proposed. The contract layer, feature schema, shadow simulator, and
safety interlock are implemented and tested on the development host. Live
collection is blocked on venue verification, and no pair has been studied.

Authority:

- [`ADR 0001`](adr/0001-v2-replicated-decision-oms-and-execution-boundary.md)
- [`V2 architecture contracts`](V2_ARCHITECTURE_CONTRACTS.md)
- [`V2 Phase 3 decision-evaluator design`](V2_PHASE3_DECISION_EVALUATOR_DESIGN.md)

Implementation: [`model/`](../model)

## 1. What "the model" is, precisely

ADR 0001 already answers this, and the answer is narrower than it first appears.
The model is not a trading brain that sits beside the cluster and decides
things. Under the accepted architecture it is two producers of immutable data
and nothing else:

```text
                 offline, slow, may use floats and judgement
   raw archive ------> calibration ------> PolicyArtifact  (schema V1, 132+12N bytes)
                                                 |
                                                 v
                                        published, then separately
                                        ACTIVATED by a cluster command
                                                 |
   venue feed ------> feature edge ----> DecisionFrame   (schema V2, 165+8N bytes)
                 online, fast, bounded, no judgement      |
                                                          v
                                            Raft Decision/OMS cluster
                                            performs the FINAL evaluation
```

The model proposes. The cluster decides. That separation is the whole reason
the architecture exists, and it means the model has exactly two output byte
contracts and zero authority.

Everything the research prompt asked for maps onto that shape without inventing
a new boundary:

| Research requirement | Where it lives | What it produces |
|---|---|---|
| Pair discovery, metadata with provenance | `adapters/` | metadata rows with per-field confidence |
| Book/trade/reference collection | `collectors/`, `storage/` | raw evidence rows, digested |
| Spread, depth, imbalance, volatility, flow | `features/` | `Decimal` metrics |
| Feature vector with pinned meaning | `features/schema.py` | `FeatureSchemaId` |
| Canonical frame emission | `features/frame_builder.py` | **`DecisionFrame` bytes** |
| Fair value, shadow quotes, fills, markouts | `simulation/` | model outputs, never orders |
| Policy fitting | `calibration/policy_builder.py` | **`PolicyArtifact` bytes** |
| Pair ranking and go/no-go gates | `calibration/pair_ranker.py` | REJECT / WATCHLIST / CANDIDATE |
| Daily reporting | `reporting/` | Markdown + CSV |

## 2. The plug-in point is a byte contract, not an API

The model attaches to the execution system at exactly two places, and both are
byte layouts already frozen and golden-tested in C++:

- `encode_payload(const DecisionFrame&)` in `src/v2_payload.cpp`
- `encode_payload(const PolicyArtifact&)` in the same file

`model/src/velocita_model/contracts/codec.py` is a byte-for-byte
reimplementation of both. `model/tests/test_codec_golden_parity.py` asserts the
Python encoder reproduces the exact C++ golden corpora:

```text
DecisionFrame V2 fixture -> 181 bytes,
    sha256 9bdc490425ee07a28b4a0fd624d25ea5b167180dc01bae2080d54c0e4c05dcc3
PolicyArtifact V1 fixture -> 144 bytes,
    sha256 7cd115af41936eaaabbf53660d86e54d27c288c09f76f98efc1c2c75dd48ef30
```

Those are the same digests recorded in `CHANGELOG.md` and asserted in
`tests/v2_payload_codec_tests.cpp`. If either side revises a schema, the Python
test fails first, which is the intended coupling: the model must not be able to
quietly emit bytes the reducer no longer accepts.

**Transport is deliberately unspecified.** The frames are bytes; how they reach
a voter - Aeron publication, a file the node replays, a Unix socket - is a
separate decision that does not affect the contract. Choosing it now would
couple the model to a transport before the reducer needs one.

### Why Python for the edge when the system is C++

The prompt's language choice is right for this component, for architectural
reasons rather than convenience:

- The edge is explicitly *not* on the authoritative path. ADR 0001 permits the
  model to use "floating point, workers, files, clocks, or external data"
  because the cluster re-validates everything and performs the final evaluation.
- It is a prefilter and a proposer. `EvaluateDecisionFrame` is where latency
  matters, and that is C++ in the reducer.
- Research code changes weekly; wire contracts change quarterly. Keeping them in
  different languages makes the boundary hard to blur by accident.

The one thing that must not drift is the canonical encoding, and that is pinned
by the golden-corpus test rather than by discipline.

## 3. Feature schema: the contract nobody thinks to write down

`FeatureSchemaId` is the promise binding a policy written today to a frame
produced next month. A rule says `feature 6 >= 25_000_000`. Nothing in the
canonical bytes explains that this means "quoted spread is at least 25 basis
points" - only the schema table does.

So the schema is content-addressed: `feature_schema_id` is derived from the
SHA-256 of its canonical description. Edit a unit, a sign convention, or an
index, and the id changes, and the cluster rejects the scope mismatch instead of
evaluating an old rule against a new meaning.

This defends against the most dangerous bug class in the whole design. A feature
meaning drift keeps every byte valid, every checksum passing, and every digest
verifying, while the strategy quietly does something else. Deriving identity
from content makes it loud.

`market-making.v1` declares 32 features (the bounded maximum): price levels in
ticks, spread and shape, four volatility horizons, external flow, inventory and
capital, measured strategy quality fed back from the simulator, costs, and
session/health. Indices are append-only; a retired feature keeps its slot.

## 4. Shadow evaluation reuses the cluster's semantics

`simulation/rule_evaluator.py` mirrors `src/v2_rule_evaluator.cpp`: the ordered
first-match evaluator, the whole-set feature-availability pre-pass, the visit
counting, and the section 3.4 control-mode dominance table including the
`halted` short-circuit.

This is what makes the shadow study worth anything. Simulating against an
idealised strategy tells you what an idealised strategy would do; simulating
against the exact evaluator the reducer runs tells you what the deployed system
would do.

**Stated limitation.** The parity tests pin the semantics this mirror claims to
reproduce; they do not execute the C++ evaluator. Cross-binary equivalence needs
a differential harness running both over one generated corpus, the way
`v2_model_replay_tests.cpp` already does for the reducer against its reference
model. That harness is the correct gate before any policy built here is
activated, and it is not yet built.

## 5. Safety: structural, not a flag

`LIVE_TRADING = False` is the weakest guarantee in the system, because a
constant can be edited. The real guarantees are structural:

1. The only path to a venue is a committed outbox effect consumed by the C++
   execution gateway. That gateway consumes `OrderEffect` records produced by
   the reducer, not input from this process on any transport.
2. **The model cannot author an `OrderEffect`.** `contracts/codec.py` implements
   no encoder for it, and `safety.assert_no_effect_encoder()` fails if one ever
   appears. No environment variable adds one.
3. Everything the model emits is advisory by contract. A frame is *proposed*; a
   published policy is not an activated policy.
4. Phase 3C retains an evaluation mode per decision, and Phase 4 must refuse to
   build a risk-increasing effect from a shadow decision. Shadow is an authority
   property, not a logging preference.

Defence in depth on top of that:

- a static AST audit refuses banned imports (`web3`, `eth_account`, `solders`,
  ...), banned definitions (`submit_order`, `sign_transaction`, ...), and banned
  attribute access, failing closed on an unreadable or empty tree;
- `ReadOnlyHttp` refuses to construct any non-GET request, and any path matching
  a state-changing fragment, *before* touching the network;
- the config loader refuses any key resembling a credential;
- the systemd unit runs the interlock as `ExecStartPre` and sandboxes the
  process, so "cannot trade" is an OS fact as well as a code-review conclusion.

The audit has already earned its place: it rejected this repo's own
`would_place_order` property name during development. The property was renamed
rather than the audit weakened.

## 6. Honest simulation: three decisions that determine whether this is useful

**Touching a price is not a fill.** A resting order fills only when opposing
volume clears the queue ahead of it. `fill_model.py` refuses to produce one fill
count; it produces three, under `optimistic` / `base` / `conservative` queue
assumptions, and `fill_probability_range()` returns the range. Where queue
position is unobservable, the range *is* the answer, and a report quoting only
the optimistic bound is misusing it.

**Markouts outrank spread.** The classic way to lose money making markets is to
be delighted by a wide quoted spread on an illiquid pair, then be adversely
selected on every fill. Percentiles are computed on the unfavourable tail, and
the percentile rank is deliberately conservative (`floor(f*n)+1`, not
`ceil(f*n)`) so a 20-sample p95 cannot exclude the single worst observation.
Bid and ask fills are reported separately, because a trending market makes one
side safe and the other lethal, and averaging produces a conclusion wrong for
both.

**Costs are a floor, not a term.** `cost_floor_bps` requires the round-trip
maker fee plus amortised gas, times a safety multiplier. No combination of
favourable adjustments may quote inside it. At a 20 USDC account size this is
the term that disqualifies most candidates, which is the point.

Missing data is never zero-filled anywhere: a markout horizon beyond the
collected data is omitted rather than counted as zero, because "we stopped
watching" must not read as "no adverse selection".

## 7. What is deliberately absent

- **No `OrderEffect` or `VenueObservation` encoder.** Those belong to the
  cluster and the gateway. Giving the model a vocabulary for describing an
  external effect would blur the boundary ADR 0001 draws.
- **No transport to the cluster.** Frames are bytes and rows; wiring is a later,
  separate decision.
- **No live collector.** Blocked on venue verification, not effort - see below.
- **No fitted policy.** `policy_builder.py` builds and self-hashes artifacts;
  what the rules *should be* is the next task, and it requires collected data
  that does not exist.

## 8. The finding that should change the plan

Verification against official documentation on 2026-09-04 established:

- API base `https://api.truemarkets.co`, ES256 JWT auth (**verified**);
- `GET /v1/conductor/assets` - asset catalog, filterable by venue (**verified**);
- `POST /v1/conductor/quotes` - non-committal quotes (**verified**);
- `POST /v1/conductor/orders` - places **market orders** (**verified**).

The documented retail DeFi path is quote-and-market-order, RFQ-shaped. It is not
a central limit order book with resting passive quotes. Separately, "Trading
Firms CeFi" access advertises FIX, low-latency quoting, order management, and
volume-tiered maker rebates - the maker surface appears to live on the KYC'd
institutional path.

If that reading holds, **a passive market-making strategy is not expressible on
the venue surface this account can reach**, and no amount of shadow simulation
changes it. The adapter therefore reports `supports_resting_limit_orders` as
unverified, `passive_strategy_is_expressible()` returns False, and the pair
ranker emits a venue-level NO-GO rather than a per-pair scorecard.

This is a reading of summarised documentation, not a confirmed fact, and it is
the single highest-value thing to resolve. See
[`MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`](MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md).

The architecture survives either answer. If resting quotes exist, the market-
making path proceeds as designed. If they do not, the pivot is to a taker-side
volume strategy, and the parts that change are the feature schema (new id), the
policy rules, and the quoting simulator. The contract layer, provenance
discipline, storage, evaluator mirror, safety interlock, and reporting are
unaffected, because none of them assume passivity.

## 9. Relationship to the V2 phase plan

This work sits alongside Phase 3, not inside it. It adds no C++ and changes no
accepted contract. It consumes the Phase 1 byte contracts and the Phase 3A/3C
evaluation semantics, and it produces the two inputs Phase 3 and Phase 4 need.

Nothing here is a Phase 3 acceptance claim. Phase 3D economic action templates
and Phase 3-wide measurement remain the blocking work on the C++ side.

## 10. Open questions

1. Does the DeFi surface support resting limit orders and post-only at all?
2. Is there a public order book, at what depth, with sequence numbers?
3. Which volume metric does the leaderboard rank, and is there a maker
   multiplier? Both readings are computed because the semantics are unverified.
4. What are the real maker fee and per-fill on-chain cost? A wrong fee inverts
   every conclusion in the report.
5. Transport from edge to cluster - Aeron, file replay, or socket?
6. When does the differential C++/Python evaluator harness get built? It gates
   activating any policy authored here.
