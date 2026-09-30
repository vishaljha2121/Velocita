"""The feature schema: what index N means, forever, under a given schema id.

`FeatureSchemaId` is the promise that binds a `PolicyArtifact` written today to
a `DecisionFrame` produced next month. A rule says "feature 6 >= 25_000_000".
Nothing in the canonical bytes explains that this means "quoted spread is at
least 25 basis points" - only this table does. So this table is versioned
content, not source-code convenience:

  * indices are append-only; a retired feature keeps its slot as `reserved`;
  * a unit or sign convention change is a NEW schema id, never an edit;
  * the id is derived from the schema's canonical digest, so a silent edit
    produces a different id and the cluster rejects the scope mismatch rather
    than evaluating an old rule against a new meaning.

That last property is the point. A feature-meaning drift is the most dangerous
class of bug in this design: every byte stays valid, every checksum passes, and
the strategy quietly does something else. Deriving the id from the content makes
that failure loud and immediate.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Final

from ..contracts.digest import sha256_digest
from ..contracts.enums import MAX_DECISION_FEATURES


@dataclass(frozen=True, slots=True)
class FeatureSpec:
    index: int
    name: str
    unit: str
    description: str
    signed: bool = False
    # Divisor that converts this feature's micros atoms into the unit an action
    # template needs. Only meaningful for features usable as a price or quantity
    # source; see `action_template.hpp` (`feature_atoms_per_tick` / `_per_lot`).
    atoms_per_tick: int | None = None
    atoms_per_lot: int | None = None


#: Market-making feature schema, revision 1.
#:
#: Units are chosen so a rule threshold is readable by a human reviewing a
#: policy diff: basis points and ratios rather than raw venue floats.
MARKET_MAKING_V1: Final[tuple[FeatureSpec, ...]] = (
    # --- price level, in ticks; usable as action-template price sources -------
    FeatureSpec(0, "mid_price_ticks", "ticks", "(best_bid + best_ask) / 2", atoms_per_tick=1_000_000),
    FeatureSpec(1, "best_bid_ticks", "ticks", "best displayed bid", atoms_per_tick=1_000_000),
    FeatureSpec(2, "best_ask_ticks", "ticks", "best displayed ask", atoms_per_tick=1_000_000),
    FeatureSpec(3, "microprice_ticks", "ticks", "size-weighted mid", atoms_per_tick=1_000_000),
    FeatureSpec(4, "fair_value_ticks", "ticks", "blended fair value estimate", atoms_per_tick=1_000_000),
    # --- spread and shape ----------------------------------------------------
    FeatureSpec(5, "spread_ticks", "ticks", "best_ask - best_bid"),
    FeatureSpec(6, "spread_bps", "bps", "spread relative to mid"),
    FeatureSpec(7, "book_imbalance", "ratio", "(bid_sz - ask_sz)/(bid_sz + ask_sz), -1..1", signed=True),
    FeatureSpec(8, "top_bid_size_lots", "lots", "size resting at best bid", atoms_per_lot=1_000_000),
    FeatureSpec(9, "top_ask_size_lots", "lots", "size resting at best ask", atoms_per_lot=1_000_000),
    FeatureSpec(10, "depth_bid_10bps_quote", "quote", "cumulative bid notional within 10bps of mid"),
    FeatureSpec(11, "depth_ask_10bps_quote", "quote", "cumulative ask notional within 10bps of mid"),
    # --- volatility ----------------------------------------------------------
    FeatureSpec(12, "realized_vol_1s_bps", "bps", "realized volatility, 1s horizon"),
    FeatureSpec(13, "realized_vol_5s_bps", "bps", "realized volatility, 5s horizon"),
    FeatureSpec(14, "realized_vol_30s_bps", "bps", "realized volatility, 30s horizon"),
    FeatureSpec(15, "realized_vol_300s_bps", "bps", "realized volatility, 5m horizon"),
    # --- external flow -------------------------------------------------------
    FeatureSpec(16, "trades_per_min", "count", "observed public prints per minute"),
    FeatureSpec(17, "notional_per_min_quote", "quote", "observed public traded notional per minute"),
    FeatureSpec(18, "reference_divergence_bps", "bps", "local mid vs reference price", signed=True),
    # --- inventory and capital ----------------------------------------------
    FeatureSpec(19, "inventory_base_lots", "lots", "signed base-asset inventory", signed=True, atoms_per_lot=1_000_000),
    FeatureSpec(20, "inventory_notional_quote", "quote", "signed inventory marked to fair value", signed=True),
    FeatureSpec(21, "inventory_skew", "ratio", "inventory / max inventory, -1..1", signed=True),
    FeatureSpec(22, "deployable_quote", "quote", "quote balance outside the safety reserve"),
    FeatureSpec(23, "target_quote_size_lots", "lots", "size the sizing model wants to show", atoms_per_lot=1_000_000),
    # --- measured strategy quality (fed back from the simulator) -------------
    FeatureSpec(24, "adverse_selection_30s_bps", "bps", "recent 30s fill-conditioned markout", signed=True),
    FeatureSpec(25, "fill_ratio_recent", "ratio", "recent simulated fill rate, 0..1"),
    FeatureSpec(26, "queue_ahead_lots", "lots", "estimated size ahead at the quoted level"),
    # --- costs ---------------------------------------------------------------
    FeatureSpec(27, "gas_cost_bps", "bps", "amortised per-fill on-chain cost"),
    FeatureSpec(28, "maker_fee_bps", "bps", "configured maker fee"),
    FeatureSpec(29, "taker_fee_bps", "bps", "configured taker fee"),
    # --- session and health --------------------------------------------------
    FeatureSpec(30, "utc_hour", "count", "UTC hour of day, 0..23"),
    FeatureSpec(31, "data_quality", "ratio", "0..1 composite feed-health score"),
)


class FeatureSchema:
    """An immutable, content-addressed feature layout."""

    __slots__ = ("_specs", "_by_name", "_revision", "_digest", "_schema_id")

    def __init__(self, revision: str, specs: tuple[FeatureSpec, ...]) -> None:
        if len(specs) > MAX_DECISION_FEATURES:
            raise ValueError(f"schema declares {len(specs)} features, cap is {MAX_DECISION_FEATURES}")
        if not specs:
            raise ValueError("schema declares no features")
        for position, spec in enumerate(specs):
            if spec.index != position:
                raise ValueError(f"feature {spec.name!r} declares index {spec.index} at position {position}")
        names = [spec.name for spec in specs]
        if len(set(names)) != len(names):
            raise ValueError("duplicate feature name in schema")

        self._specs = specs
        self._by_name = {spec.name: spec for spec in specs}
        self._revision = revision
        self._digest = sha256_digest(self._canonical_json().encode("utf-8"))
        # Derive a non-zero u64 identity from the digest. The cluster stores the
        # id; the digest stays in the model artifact as the human-auditable link.
        self._schema_id = int.from_bytes(self._digest[:8], "big") | 1

    def _canonical_json(self) -> str:
        return json.dumps(
            {
                "revision": self._revision,
                "features": [
                    {
                        "index": spec.index,
                        "name": spec.name,
                        "unit": spec.unit,
                        "signed": spec.signed,
                        "atoms_per_tick": spec.atoms_per_tick,
                        "atoms_per_lot": spec.atoms_per_lot,
                    }
                    for spec in self._specs
                ],
            },
            sort_keys=True,
            separators=(",", ":"),
        )

    @property
    def revision(self) -> str:
        return self._revision

    @property
    def digest(self) -> bytes:
        return self._digest

    @property
    def schema_id(self) -> int:
        """The `FeatureSchemaId` written into every frame and policy."""
        return self._schema_id

    @property
    def specs(self) -> tuple[FeatureSpec, ...]:
        return self._specs

    def __len__(self) -> int:
        return len(self._specs)

    def index_of(self, name: str) -> int:
        try:
            return self._by_name[name].index
        except KeyError:
            raise KeyError(f"unknown feature {name!r} in schema {self._revision}") from None

    def spec(self, name: str) -> FeatureSpec:
        return self._by_name[name]

    def describe(self) -> str:
        lines = [f"feature schema {self._revision} id={self._schema_id} digest={self._digest.hex()}"]
        lines += [f"  [{s.index:2d}] {s.name:<28} {s.unit:<6} {s.description}" for s in self._specs]
        return "\n".join(lines)


MARKET_MAKING_SCHEMA_V1: Final = FeatureSchema("market-making.v1", MARKET_MAKING_V1)
