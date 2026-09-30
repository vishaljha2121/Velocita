"""Cross-language byte parity against the accepted C++ golden corpora.

These are the same fixtures and the same expected digests that
`tests/v2_payload_codec_tests.cpp` asserts. If the C++ side revises a schema,
this test fails first and loudly, which is the intended coupling: the model must
not be able to quietly emit bytes the reducer no longer accepts.
"""

from __future__ import annotations

import dataclasses
import unittest

from velocita_model.contracts.codec import (
    DECISION_FRAME_PAYLOAD_FIXED_BYTES,
    MAX_DECISION_FRAME_PAYLOAD_BYTES,
    MAX_POLICY_PAYLOAD_BYTES,
    POLICY_PAYLOAD_PREFIX_BYTES,
    DecisionFrame,
    PolicyArtifact,
    PolicyRule,
    decode_decision_frame,
    decode_policy_artifact,
)
from velocita_model.contracts.digest import sha256_digest, to_hex
from velocita_model.contracts.enums import (
    ContractError,
    ContractViolation,
    Directive,
    MarketDataStatus,
    PredicateOperator,
)

# tests/v2_payload_codec_tests.cpp: digest_with_first_byte
def digest_with_first_byte(value: int) -> bytes:
    return bytes([value]) + bytes(31)


GOLDEN_DECISION_FRAME_HEX = (
    "020001000000000000000200000000000000030000000000000004000000000000000500"
    "00000000000006000000000000000b000000000000000c000000000000000d0000000000"
    "000040420f0000000000a4420f0000000000070000000000000008000000000000000200"
    "e05ef8ffffffffff80841e000000000001e80300000000000009000000000000000a000000"
    "000000003300000000000000000000000000000000000000000000000000000000000000"
)
GOLDEN_DECISION_FRAME_SHA256 = (
    "9bdc490425ee07a28b4a0fd624d25ea5b167180dc01bae2080d54c0e4c05dcc3"
)
# CHANGELOG.md, Phase 1 candidate: the exact 144-byte policy corpus digest.
GOLDEN_POLICY_ARTIFACT_SHA256 = (
    "7cd115af41936eaaabbf53660d86e54d27c288c09f76f98efc1c2c75dd48ef30"
)


def frame_fixture() -> DecisionFrame:
    return DecisionFrame(
        policy_id=1,
        policy_version=2,
        feature_schema_id=3,
        venue_id=4,
        venue_session_id=5,
        venue_sequence=6,
        source_id=11,
        source_generation=12,
        source_sequence=13,
        event_time_ns=1_000_000,
        receive_time_ns=1_000_100,
        instrument_id=7,
        account_id=8,
        features=(-500_000, 2_000_000),
        market_data_status=MarketDataStatus.ready,
        market_data_age_ns=1_000,
        position_version=9,
        risk_version=10,
        raw_evidence_digest=digest_with_first_byte(0x33),
    )


def policy_fixture() -> PolicyArtifact:
    return PolicyArtifact(
        policy_id=1,
        policy_version=2,
        feature_schema_id=3,
        instrument_id=4,
        account_id=5,
        model_digest=digest_with_first_byte(0x11),
        artifact_digest=digest_with_first_byte(0x22),
        valid_from_event_time_ns=100,
        valid_until_event_time_ns=200,
        risk_policy_version=6,
        rules=(
            PolicyRule(
                feature_index=0,
                predicate=PredicateOperator.greater_than_or_equal,
                threshold_atoms=1_250_000,
                directive=Directive.place_limit,
            ),
        ),
    )


class DecisionFrameGoldenBytes(unittest.TestCase):
    def test_matches_cpp_golden_bytes(self) -> None:
        encoded = frame_fixture().encode()
        self.assertEqual(encoded.hex(), GOLDEN_DECISION_FRAME_HEX)

    def test_matches_cpp_corpus_digest(self) -> None:
        self.assertEqual(to_hex(frame_fixture().corpus_digest()), GOLDEN_DECISION_FRAME_SHA256)

    def test_declared_length(self) -> None:
        self.assertEqual(len(frame_fixture().encode()), 181)
        self.assertEqual(DECISION_FRAME_PAYLOAD_FIXED_BYTES, 165)
        self.assertEqual(MAX_DECISION_FRAME_PAYLOAD_BYTES, 421)

    def test_round_trips(self) -> None:
        frame = frame_fixture()
        self.assertEqual(decode_decision_frame(frame.encode()), frame)

    def test_trailing_bytes_rejected(self) -> None:
        with self.assertRaises(ContractViolation):
            decode_decision_frame(frame_fixture().encode() + b"\x00")

    def test_legacy_v1_schema_fails_closed(self) -> None:
        """A V1 frame must be refused by schema, not reinterpreted by length."""
        legacy = bytearray(frame_fixture().encode())
        legacy[0:2] = (1).to_bytes(2, "little")
        with self.assertRaises(ContractViolation) as caught:
            decode_decision_frame(bytes(legacy))
        self.assertEqual(caught.exception.error, ContractError.unsupported_schema)


class PolicyArtifactGoldenBytes(unittest.TestCase):
    def test_matches_cpp_corpus_digest(self) -> None:
        encoded = policy_fixture().encode()
        self.assertEqual(len(encoded), 144)
        self.assertEqual(to_hex(sha256_digest(encoded)), GOLDEN_POLICY_ARTIFACT_SHA256)

    def test_declared_lengths(self) -> None:
        self.assertEqual(POLICY_PAYLOAD_PREFIX_BYTES, 132)
        self.assertEqual(MAX_POLICY_PAYLOAD_BYTES, 516)

    def test_round_trips(self) -> None:
        policy = policy_fixture()
        self.assertEqual(decode_policy_artifact(policy.encode()), policy)


class ContractRejection(unittest.TestCase):
    """The edge must refuse to emit anything the reducer would reject, and must
    report the same typed reason so a failure is diagnosable locally."""

    def test_zero_identity_rejected(self) -> None:
        with self.assertRaises(ContractViolation) as caught:
            dataclasses.replace(frame_fixture(), venue_id=0).encode()
        self.assertEqual(caught.exception.error, ContractError.invalid_identity)

    def test_zero_evidence_digest_rejected(self) -> None:
        bad = dataclasses.replace(frame_fixture(), raw_evidence_digest=bytes(32))
        with self.assertRaises(ContractViolation) as caught:
            bad.encode()
        self.assertEqual(caught.exception.error, ContractError.missing_digest)

    def test_empty_features_rejected(self) -> None:
        bad = dataclasses.replace(frame_fixture(), features=())
        with self.assertRaises(ContractViolation) as caught:
            bad.encode()
        self.assertEqual(caught.exception.error, ContractError.empty_features)

    def test_too_many_features_rejected(self) -> None:
        bad = dataclasses.replace(frame_fixture(), features=tuple(range(33)))
        with self.assertRaises(ContractViolation) as caught:
            bad.encode()
        self.assertEqual(caught.exception.error, ContractError.capacity_exceeded)

    def test_negative_market_data_age_rejected(self) -> None:
        bad = dataclasses.replace(frame_fixture(), market_data_age_ns=-1)
        with self.assertRaises(ContractViolation) as caught:
            bad.encode()
        self.assertEqual(caught.exception.error, ContractError.invalid_age)

    def test_inverted_policy_validity_window_rejected(self) -> None:
        bad = dataclasses.replace(policy_fixture(), valid_until_event_time_ns=100)
        with self.assertRaises(ContractViolation) as caught:
            bad.encode()
        self.assertEqual(caught.exception.error, ContractError.invalid_time_range)

    def test_empty_rules_rejected(self) -> None:
        bad = dataclasses.replace(policy_fixture(), rules=())
        with self.assertRaises(ContractViolation) as caught:
            bad.encode()
        self.assertEqual(caught.exception.error, ContractError.empty_rules)


if __name__ == "__main__":
    unittest.main()
