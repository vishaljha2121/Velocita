"""Enumerations mirrored from `include/velocita/v2/contracts.hpp`.

Every value here is a wire-compatibility promise. Changing an integer requires
the same schema-revision discipline the C++ side uses: new schema version, new
golden corpus, and a documented compatibility outcome. Never renumber in place.
"""

from __future__ import annotations

from enum import IntEnum

# Independent per-family schema versions. These mirror the C++ constants and are
# asserted against the golden corpora in tests/test_codec_golden_parity.py.
POLICY_ARTIFACT_SCHEMA_VERSION = 1
DECISION_FRAME_SCHEMA_VERSION = 2
ORDER_EFFECT_SCHEMA_VERSION = 1
VENUE_OBSERVATION_SCHEMA_VERSION = 1
ACTION_TEMPLATE_SCHEMA_VERSION = 1

DIGEST_BYTES = 32
MAX_POLICY_RULES = 32
MAX_DECISION_FEATURES = 32


class PredicateOperator(IntEnum):
    less_than = 1
    less_than_or_equal = 2
    equal = 3
    greater_than_or_equal = 4
    greater_than = 5


class Directive(IntEnum):
    no_action = 1
    place_limit = 2
    cancel = 3
    replace = 4


class MarketDataStatus(IntEnum):
    ready = 1
    stale = 2
    recovering = 3
    crossed = 4
    incomplete = 5


class StrategyMode(IntEnum):
    normal = 1
    no_new_risk = 2
    cancel_only = 3
    reduce_only = 4
    halted = 5


class Side(IntEnum):
    buy = 1
    sell = 2


class TimeInForce(IntEnum):
    good_til_cancel = 1
    immediate_or_cancel = 2
    fill_or_kill = 3
    post_only = 4


class RoundingMode(IntEnum):
    toward_zero = 0
    away_from_zero = 1
    nearest_ties_away_from_zero = 2


class PostOnlyMode(IntEnum):
    prohibited = 1
    permitted = 2
    required = 3


class EconomicValueSourceKind(IntEnum):
    none = 0
    fixed = 1
    feature = 2


class ContractError(IntEnum):
    """Mirrors the C++ `ContractError`. Validation must fail with the same reason
    the reducer would report, so an edge-side rejection is diagnosable without
    round-tripping through the cluster."""

    none = 0
    invalid_identity = 1
    unsupported_schema = 2
    missing_digest = 3
    invalid_time_range = 4
    capacity_exceeded = 5
    empty_rules = 6
    empty_features = 7
    invalid_feature_index = 8
    invalid_age = 9
    invalid_enum = 10
    non_positive_price = 11
    non_positive_quantity = 12
    non_positive_notional = 13
    invalid_value_source = 14
    non_canonical_value = 15
    invalid_safety_cap = 16
    invalid_order_target = 17
    invalid_action_template = 18


class ContractViolation(Exception):
    """Raised instead of emitting a frame the cluster would reject."""

    def __init__(self, error: ContractError, detail: str = "") -> None:
        self.error = error
        super().__init__(f"{error.name}: {detail}" if detail else error.name)
