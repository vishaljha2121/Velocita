# Velocita model - passive market-data research and shadow quoting

The market-data/feature edge and offline calibration service for the Velocita V2
Decision/OMS cluster. It produces the two byte contracts the cluster consumes,
and it cannot trade.

Design: [`docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`](../docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md)
Blockers: [`docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`](../docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md)

## Safety statement

This package is passive by construction, not by configuration.

- It implements **no encoder for `OrderEffect`**, the only value the execution
  gateway acts on. There is no flag or environment variable that adds one.
- Its HTTP transport refuses any non-GET request, and any path matching a
  state-changing fragment, before opening a socket.
- A static AST audit rejects wallet/signing imports, order-management
  definitions, and signing attribute access anywhere in the tree, and fails
  closed on an unreadable or empty tree.
- The config loader refuses any key resembling a credential.
- `LIVE_TRADING` is `False` and is the *weakest* of these guarantees.

Everything this package emits - `DecisionFrame`, `PolicyArtifact` - is advisory.
The Raft cluster performs the final authoritative evaluation and may reject it.
Publication is not activation.

Verify it yourself:

```console
$ PYTHONPATH=src python3 -m velocita_model.cli verify
LIVE_TRADING = False
static audit: 0 finding(s)
codec exposes no OrderEffect encoder: ok
read-only transport refuses state-changing paths: ok
```

## Status

| Component | State |
|---|---|
| Canonical codec, byte-parity with C++ | implemented, golden-tested |
| Feature schema (content-addressed) | implemented |
| Decision-frame builder | implemented, tested |
| Policy artifact builder | implemented, self-hashing |
| Evaluator mirror (first-match + control authority) | implemented, tested |
| Shadow quoting, fill model, markouts, inventory, risk | implemented, tested |
| Pair ranker and stage gates | implemented |
| Storage (SQLite WAL) | implemented |
| Reporting | implemented, not yet fed by real data |
| Market-data stream recorder (`cli stream`, read-only WebSocket, raw evidence) | implemented, tested, live-read 2026-09-28 |
| Research lab (`research/`): daily bars (Binance archive, checksum-verified; True Markets history), Minara momentum rotation, 5-test permutation battery | implemented, tested, mutation-checked |
| **Venue adapter (collection)** | **blocked on verification** |
| Fitted policy rules | not started; needs collected data |

109 tests pass. No pair has been studied and no data has been collected.

## Requirements

Python 3.11+ (`tomllib`). The core is **stdlib-only** on purpose: a Pi should not
need a package index to collect data, and a smaller dependency surface is a
smaller audit surface.

## Quick start

```console
$ PYTHONPATH=src python3 -m unittest discover -s tests   # 109 tests
$ PYTHONPATH=src python3 -m velocita_model.cli verify
$ PYTHONPATH=src python3 -m velocita_model.cli capabilities
$ PYTHONPATH=src python3 -m velocita_model.cli schema
$ PYTHONPATH=src python3 -m velocita_model.cli config config/research.example.toml
$ PYTHONPATH=src python3 -m velocita_model.cli disk --book-rate 2 --trade-rate 30
$ PYTHONPATH=src python3 -m velocita_model.cli health
```

`capabilities` exits non-zero today, correctly: passive quoting has not been
shown to be possible on the venue surface this account can reach.

## Layout

```text
src/velocita_model/
  safety.py            structural interlock + static AST audit
  config.py            TOML config, content-hashed, credential-refusing
  cli.py               entry point; every command runs the interlock first
  contracts/           canonical codec - byte-identical to src/v2_payload.cpp
  features/            book metrics, volatility, flow, schema, frame builder
  simulation/          fair value, shadow quotes, fill model, markouts,
                       inventory, risk engine, evaluator mirror
  calibration/         policy builder, pair ranker and stage gates
  collectors/          host telemetry, clock and throttle checks
  adapters/            read-only transport guard, True Markets adapter
  storage/             SQLite WAL store, raw-evidence-first schema
  reporting/           daily Markdown/CSV report, competition worksheet
```

## The two byte contracts

```text
PolicyArtifact  schema V1   132 + 12N bytes   max 516
DecisionFrame   schema V2   165 +  8N bytes   max 421
```

`tests/test_codec_golden_parity.py` asserts these against the exact C++ golden
corpora and their SHA-256 digests. A schema revision on either side fails that
test first.

## Reading the output

Three habits are enforced and worth knowing before reading any report:

- **Fill counts are ranges.** Optimistic / base / conservative queue
  assumptions. Quoting only the optimistic bound misrepresents the study.
- **Missing is not zero.** A markout horizon beyond the collected data is
  omitted, not counted as zero. A gap is recorded as a gap and never backfilled.
- **Unverified is disqualifying.** A pair whose fee or tick size was inferred
  cannot become a RESEARCH-CANDIDATE, however good its spread looks.

## The blunt conclusion

A favourable shadow result is not evidence that a live strategy would be
profitable. It means the simulation did not find a disqualifying reason in the
data collected, under assumptions that have not been confirmed against live
venue behaviour - queue mechanics, fee schedule, gas cost, fill classification,
and post-only semantics chief among them.

Do not place a trade because a backtest looks positive. The default conclusion
of every report this system generates is **NO LIVE TRADING RECOMMENDED**, and
it is overturned only by gates that actually passed, never by the absence of a
failure.
