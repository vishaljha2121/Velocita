"""Edge-side frame construction: provenance, refusal, and sequence integrity."""

from __future__ import annotations

import unittest
from decimal import Decimal

from velocita_model.contracts.codec import decode_decision_frame
from velocita_model.contracts.enums import ContractError, MarketDataStatus
from velocita_model.features.book_metrics import (
    BookObservation,
    Level,
    classify_status,
    cumulative_depth_quote,
)
from velocita_model.features.frame_builder import (
    DecisionFrameBuilder,
    FrameIdentity,
    FrameRefused,
    evidence_digest,
)
from velocita_model.features.schema import MARKET_MAKING_SCHEMA_V1, FeatureSchema, FeatureSpec

D = Decimal

IDENTITY = FrameIdentity(
    policy_id=1,
    policy_version=1,
    venue_id=1,
    venue_session_id=1,
    source_id=1,
    source_generation=1,
    instrument_id=1,
    account_id=1,
)

SMALL_SCHEMA = FeatureSchema(
    "test.v1",
    (
        FeatureSpec(0, "spread_bps", "bps", "spread"),
        FeatureSpec(1, "imbalance", "ratio", "imbalance", signed=True),
    ),
)


def builder(**overrides: object) -> DecisionFrameBuilder:
    return DecisionFrameBuilder(SMALL_SCHEMA, IDENTITY, **overrides)  # type: ignore[arg-type]


def filled_vector(b: DecisionFrameBuilder):
    vector = b.new_vector()
    vector.set("spread_bps", D("25"))
    vector.set("imbalance", D("-0.5"))
    return vector


def build(b: DecisionFrameBuilder, **overrides: object):
    kwargs = {
        "vector": filled_vector(b),
        "event_time_ns": 1_000_000,
        "receive_time_ns": 1_000_100,
        "market_data_status": MarketDataStatus.ready,
        "market_data_age_ns": 1_000,
        "raw_payloads": (b"raw-book-bytes",),
    }
    kwargs.update(overrides)
    return b.build(**kwargs)  # type: ignore[arg-type]


class SchemaIdentity(unittest.TestCase):
    def test_schema_id_is_derived_from_content(self) -> None:
        """A silent edit to a feature's meaning must change the id, so the
        cluster rejects the scope mismatch instead of evaluating an old rule
        against a new meaning."""
        original = FeatureSchema("x", (FeatureSpec(0, "a", "bps", "d"),))
        renamed_unit = FeatureSchema("x", (FeatureSpec(0, "a", "ratio", "d"),))
        self.assertNotEqual(original.schema_id, renamed_unit.schema_id)

    def test_schema_id_is_never_zero(self) -> None:
        """Zero means 'absent' in every cluster identity check."""
        self.assertNotEqual(MARKET_MAKING_SCHEMA_V1.schema_id, 0)

    def test_index_must_match_position(self) -> None:
        with self.assertRaises(ValueError):
            FeatureSchema("x", (FeatureSpec(3, "a", "bps", "d"),))

    def test_schema_cannot_exceed_the_bounded_feature_cap(self) -> None:
        with self.assertRaises(ValueError):
            FeatureSchema("x", tuple(FeatureSpec(i, f"f{i}", "bps", "d") for i in range(33)))


class FeatureVectorDiscipline(unittest.TestCase):
    def test_unknown_feature_name_fails_at_the_call_site(self) -> None:
        with self.assertRaises(KeyError):
            builder().new_vector().set("not_a_feature", D(1))

    def test_a_hole_is_refused_rather_than_zero_filled(self) -> None:
        """The canonical encoding cannot say 'unknown', so a hole would assert
        the feature is exactly 0.0."""
        b = builder()
        vector = b.new_vector()
        vector.set("spread_bps", D("25"))
        with self.assertRaises(FrameRefused) as caught:
            b.build(
                vector=vector,
                event_time_ns=1,
                receive_time_ns=1,
                market_data_status=MarketDataStatus.ready,
                market_data_age_ns=0,
                raw_payloads=(b"x",),
            )
        self.assertIn("imbalance", str(caught.exception))

    def test_a_negative_value_in_an_unsigned_feature_is_refused(self) -> None:
        with self.assertRaises(FrameRefused):
            builder().new_vector().set("spread_bps", D("-1"))

    def test_setting_none_clears_rather_than_zeroes(self) -> None:
        b = builder()
        vector = filled_vector(b)
        vector.set("spread_bps", None)
        self.assertEqual(vector.missing(2), ["spread_bps"])


class EvidenceProvenance(unittest.TestCase):
    def test_frame_carries_a_digest_of_the_raw_bytes(self) -> None:
        frame = build(builder(), raw_payloads=(b"book", b"trades"))
        self.assertEqual(frame.raw_evidence_digest, evidence_digest((b"book", b"trades")))

    def test_length_prefixing_makes_concatenation_unambiguous(self) -> None:
        self.assertNotEqual(evidence_digest((b"ab", b"c")), evidence_digest((b"a", b"bc")))

    def test_a_frame_with_no_evidence_is_refused(self) -> None:
        with self.assertRaises(FrameRefused) as caught:
            build(builder(), raw_payloads=())
        self.assertEqual(caught.exception.contract_error, ContractError.missing_digest)


class SequenceIntegrity(unittest.TestCase):
    def test_source_sequence_advances_by_exactly_one(self) -> None:
        b = builder()
        self.assertEqual([build(b).source_sequence for _ in range(3)], [1, 2, 3])

    def test_a_refused_frame_leaves_no_hole_in_the_sequence(self) -> None:
        """A hole is indistinguishable from message loss downstream."""
        b = builder()
        build(b)
        with self.assertRaises(FrameRefused):
            build(b, event_time_ns=0)
        self.assertEqual(build(b).source_sequence, 2)

    def test_a_regressing_venue_sequence_is_refused(self) -> None:
        b = builder()
        build(b, venue_sequence=10)
        with self.assertRaises(FrameRefused) as caught:
            build(b, venue_sequence=5)
        self.assertEqual(caught.exception.contract_error, ContractError.invalid_identity)

    def test_a_repeated_venue_sequence_is_permitted_but_advances_the_source(self) -> None:
        """The venue may legitimately publish two updates with one sequence;
        our own source sequence still advances so the frames stay distinct."""
        b = builder()
        first = build(b, venue_sequence=7)
        second = build(b, venue_sequence=7)
        self.assertEqual(first.venue_sequence, second.venue_sequence)
        self.assertNotEqual(first.source_sequence, second.source_sequence)


class CanonicalOutput(unittest.TestCase):
    def test_the_built_frame_round_trips_through_the_canonical_codec(self) -> None:
        frame = build(builder())
        self.assertEqual(decode_decision_frame(frame.encode()), frame)

    def test_feature_atoms_use_the_declared_micros_scale(self) -> None:
        frame = build(builder())
        self.assertEqual(frame.features, (25_000_000, -500_000))

    def test_frame_length_matches_the_declared_layout(self) -> None:
        self.assertEqual(len(build(builder()).encode()), 165 + 2 * 8)


class StaleAndCrossedData(unittest.TestCase):
    def _book(self, bid: str, ask: str) -> BookObservation:
        return BookObservation(
            pair_id="X/USDC",
            source_sequence=1,
            venue_sequence=1,
            event_time_ns=1,
            receive_time_ns=1,
            bids=(Level(D(bid), D("10")),),
            asks=(Level(D(ask), D("10")),),
            status=MarketDataStatus.ready,
            raw_digest=bytes(32),
        )

    def test_fresh_two_sided_book_is_ready(self) -> None:
        status = classify_status(self._book("1.00", "1.01"), age_ns=0, max_age_ns=1_000_000_000)
        self.assertEqual(status, MarketDataStatus.ready)

    def test_old_book_is_stale(self) -> None:
        status = classify_status(self._book("1.00", "1.01"), age_ns=5_000_000_000, max_age_ns=1_000_000_000)
        self.assertEqual(status, MarketDataStatus.stale)

    def test_crossed_outranks_stale(self) -> None:
        """A crossed book says the feed is wrong, which is a stronger statement
        than an old book saying it is late."""
        status = classify_status(self._book("1.02", "1.01"), age_ns=5_000_000_000, max_age_ns=1_000_000_000)
        self.assertEqual(status, MarketDataStatus.crossed)

    def test_one_sided_book_is_incomplete(self) -> None:
        book = BookObservation(
            pair_id="X/USDC",
            source_sequence=1,
            venue_sequence=1,
            event_time_ns=1,
            receive_time_ns=1,
            bids=(Level(D("1.00"), D("10")),),
            asks=(),
            status=MarketDataStatus.ready,
            raw_digest=bytes(32),
        )
        self.assertEqual(classify_status(book, age_ns=0, max_age_ns=1_000_000_000), MarketDataStatus.incomplete)

    def test_recovering_outranks_everything(self) -> None:
        status = classify_status(
            self._book("1.00", "1.01"), age_ns=0, max_age_ns=1_000_000_000, recovering=True
        )
        self.assertEqual(status, MarketDataStatus.recovering)


class DepthMeasurement(unittest.TestCase):
    def test_depth_stops_at_the_distance_limit(self) -> None:
        levels = (Level(D("1.0000"), D("10")), Level(D("0.9990"), D("10")), Level(D("0.9000"), D("10")))
        # 10 bps of a 1.0 mid is 0.001, so only the first two levels qualify.
        depth = cumulative_depth_quote(levels, mid=D("1.0000"), distance_bps=D("10"))
        self.assertEqual(depth, D("1.0000") * D("10") + D("0.9990") * D("10"))


if __name__ == "__main__":
    unittest.main()
