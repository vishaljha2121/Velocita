"""Canonical payload codec for the V2 boundary values the model produces.

This is a byte-for-byte reimplementation of `src/v2_payload.cpp`. It exists so
the model can construct exactly the bytes the Raft reducer will decode, and so a
cross-language golden test can prove the two encoders have not drifted. If this
module and the C++ encoder ever disagree, the C++ side is authoritative and this
file is the bug.

Only the two values the model is allowed to author are encodable here:

  * `PolicyArtifact` - offline calibration output, immutable and versioned;
  * `DecisionFrame`  - per-observation feature vector proposed to the cluster.

`OrderEffect` and `VenueObservation` are deliberately absent. They are authored
by the cluster and the execution gateway respectively. Giving the model an
encoder for them would hand it a vocabulary for describing an external effect,
which ADR 0001 reserves for the committed outbox path.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Final

from .digest import ZERO_DIGEST, sha256_digest
from .domain import check_int64, present
from .enums import (
    DECISION_FRAME_SCHEMA_VERSION,
    DIGEST_BYTES,
    MAX_DECISION_FEATURES,
    MAX_POLICY_RULES,
    POLICY_ARTIFACT_SCHEMA_VERSION,
    ContractError,
    ContractViolation,
    Directive,
    MarketDataStatus,
    PredicateOperator,
)

POLICY_PAYLOAD_PREFIX_BYTES: Final = 132
POLICY_RULE_ENCODED_BYTES: Final = 12
MAX_POLICY_PAYLOAD_BYTES: Final = POLICY_PAYLOAD_PREFIX_BYTES + MAX_POLICY_RULES * POLICY_RULE_ENCODED_BYTES
DECISION_FRAME_PAYLOAD_FIXED_BYTES: Final = 165
MAX_DECISION_FRAME_PAYLOAD_BYTES: Final = DECISION_FRAME_PAYLOAD_FIXED_BYTES + MAX_DECISION_FEATURES * 8

_U16 = struct.Struct("<H")
_U64 = struct.Struct("<Q")
_I64 = struct.Struct("<q")
_U8 = struct.Struct("<B")


def _digest_bytes(value: bytes, what: str) -> bytes:
    if len(value) != DIGEST_BYTES:
        raise ContractViolation(ContractError.missing_digest, f"{what} is not 32 bytes")
    if value == ZERO_DIGEST:
        raise ContractViolation(ContractError.missing_digest, f"{what} is zero")
    return value


@dataclass(frozen=True, slots=True)
class PolicyRule:
    """One ordered predicate. Array position is economically meaningful: the
    reducer stops at the first match, including a match selecting `no_action`."""

    feature_index: int
    predicate: PredicateOperator
    threshold_atoms: int
    directive: Directive

    def validate(self) -> None:
        if not (0 <= self.feature_index < MAX_DECISION_FEATURES):
            raise ContractViolation(ContractError.invalid_feature_index, str(self.feature_index))
        if self.predicate not in tuple(PredicateOperator):
            raise ContractViolation(ContractError.invalid_enum, "predicate")
        if self.directive not in tuple(Directive):
            raise ContractViolation(ContractError.invalid_enum, "directive")
        check_int64(self.threshold_atoms, "threshold")

    def encode(self) -> bytes:
        self.validate()
        return (
            _U16.pack(self.feature_index)
            + _U8.pack(int(self.predicate))
            + _U8.pack(int(self.directive))
            + _I64.pack(self.threshold_atoms)
        )


@dataclass(frozen=True, slots=True)
class PolicyArtifact:
    policy_id: int
    policy_version: int
    feature_schema_id: int
    instrument_id: int
    account_id: int
    model_digest: bytes
    artifact_digest: bytes
    valid_from_event_time_ns: int
    valid_until_event_time_ns: int
    risk_policy_version: int
    rules: tuple[PolicyRule, ...]
    schema_version: int = POLICY_ARTIFACT_SCHEMA_VERSION

    def validate(self) -> None:
        # Order mirrors the C++ `validate(const PolicyArtifact&)` exactly so the
        # reported reason matches what the reducer would report.
        present(self.policy_id, "policy_id")
        present(self.policy_version, "policy_version")
        present(self.feature_schema_id, "feature_schema_id")
        present(self.instrument_id, "instrument_id")
        present(self.account_id, "account_id")
        present(self.risk_policy_version, "risk_policy_version")
        if self.schema_version != POLICY_ARTIFACT_SCHEMA_VERSION:
            raise ContractViolation(ContractError.unsupported_schema, str(self.schema_version))
        _digest_bytes(self.model_digest, "model_digest")
        _digest_bytes(self.artifact_digest, "artifact_digest")
        if self.valid_until_event_time_ns <= self.valid_from_event_time_ns:
            raise ContractViolation(ContractError.invalid_time_range, "validity window")
        if len(self.rules) > MAX_POLICY_RULES:
            raise ContractViolation(ContractError.capacity_exceeded, "rule_count")
        if not self.rules:
            raise ContractViolation(ContractError.empty_rules, "rule_count is zero")
        for rule in self.rules:
            rule.validate()

    def encode(self) -> bytes:
        self.validate()
        out = bytearray()
        out += _U16.pack(self.schema_version)
        for value in (
            self.policy_id,
            self.policy_version,
            self.feature_schema_id,
            self.instrument_id,
            self.account_id,
        ):
            out += _U64.pack(value)
        out += self.model_digest
        out += self.artifact_digest
        out += _I64.pack(check_int64(self.valid_from_event_time_ns, "valid_from"))
        out += _I64.pack(check_int64(self.valid_until_event_time_ns, "valid_until"))
        out += _U64.pack(self.risk_policy_version)
        out += _U16.pack(len(self.rules))
        for rule in self.rules:
            out += rule.encode()
        assert len(out) == POLICY_PAYLOAD_PREFIX_BYTES + len(self.rules) * POLICY_RULE_ENCODED_BYTES
        return bytes(out)


@dataclass(frozen=True, slots=True)
class DecisionFrame:
    """The bounded causal evidence for exactly one authoritative evaluation.

    Every field is provenance the reducer checks. `features` is the only part
    the research pipeline is free to define, and even that is pinned by
    `feature_schema_id`: index N means one thing forever under a given schema.
    """

    policy_id: int
    policy_version: int
    feature_schema_id: int
    venue_id: int
    venue_session_id: int
    venue_sequence: int
    source_id: int
    source_generation: int
    source_sequence: int
    event_time_ns: int
    receive_time_ns: int
    instrument_id: int
    account_id: int
    features: tuple[int, ...]
    market_data_status: MarketDataStatus
    market_data_age_ns: int
    position_version: int
    risk_version: int
    raw_evidence_digest: bytes
    schema_version: int = DECISION_FRAME_SCHEMA_VERSION

    def validate(self) -> None:
        for name in (
            "policy_id",
            "policy_version",
            "feature_schema_id",
            "venue_id",
            "venue_session_id",
            "venue_sequence",
            "source_id",
            "source_generation",
            "source_sequence",
            "instrument_id",
            "account_id",
            "position_version",
            "risk_version",
        ):
            present(getattr(self, name), name)
        if self.event_time_ns <= 0 or self.receive_time_ns <= 0:
            raise ContractViolation(ContractError.invalid_time_range, "timestamps must be positive")
        check_int64(self.event_time_ns, "event_time_ns")
        check_int64(self.receive_time_ns, "receive_time_ns")
        if len(self.features) > MAX_DECISION_FEATURES:
            raise ContractViolation(ContractError.capacity_exceeded, "feature_count")
        if not self.features:
            raise ContractViolation(ContractError.empty_features, "feature_count is zero")
        if self.market_data_status not in tuple(MarketDataStatus):
            raise ContractViolation(ContractError.invalid_enum, "market_data_status")
        if self.market_data_age_ns < 0:
            raise ContractViolation(ContractError.invalid_age, "market_data_age_ns")
        _digest_bytes(self.raw_evidence_digest, "raw_evidence_digest")

    def encode(self) -> bytes:
        self.validate()
        out = bytearray()
        out += _U16.pack(self.schema_version)
        for value in (
            self.policy_id,
            self.policy_version,
            self.feature_schema_id,
            self.venue_id,
            self.venue_session_id,
            self.venue_sequence,
            self.source_id,
            self.source_generation,
            self.source_sequence,
        ):
            out += _U64.pack(value)
        out += _I64.pack(self.event_time_ns)
        out += _I64.pack(self.receive_time_ns)
        out += _U64.pack(self.instrument_id)
        out += _U64.pack(self.account_id)
        out += _U16.pack(len(self.features))
        for atoms in self.features:
            out += _I64.pack(check_int64(atoms, "feature"))
        out += _U8.pack(int(self.market_data_status))
        out += _I64.pack(check_int64(self.market_data_age_ns, "market_data_age_ns"))
        out += _U64.pack(self.position_version)
        out += _U64.pack(self.risk_version)
        out += self.raw_evidence_digest
        assert len(out) == DECISION_FRAME_PAYLOAD_FIXED_BYTES + len(self.features) * 8
        return bytes(out)

    def corpus_digest(self) -> bytes:
        """SHA-256 of the canonical bytes, matching the C++ corpus digests."""
        return sha256_digest(self.encode())


class _Reader:
    __slots__ = ("_buf", "_off")

    def __init__(self, buf: bytes) -> None:
        self._buf = buf
        self._off = 0

    def take(self, size: int) -> bytes:
        if self._off + size > len(self._buf):
            raise ContractViolation(ContractError.capacity_exceeded, "truncated payload")
        chunk = self._buf[self._off : self._off + size]
        self._off += size
        return chunk

    def u8(self) -> int:
        return _U8.unpack(self.take(1))[0]

    def u16(self) -> int:
        return _U16.unpack(self.take(2))[0]

    def u64(self) -> int:
        return _U64.unpack(self.take(8))[0]

    def i64(self) -> int:
        return _I64.unpack(self.take(8))[0]

    @property
    def remaining(self) -> int:
        return len(self._buf) - self._off


def decode_decision_frame(payload: bytes) -> DecisionFrame:
    """Decode is used by replay and by the parity tests, never on a hot path.

    Trailing undeclared bytes are rejected, as in the C++ decoder: a payload the
    encoder would not have produced is not a payload we agree to interpret.
    """
    if len(payload) < DECISION_FRAME_PAYLOAD_FIXED_BYTES:
        raise ContractViolation(ContractError.capacity_exceeded, "truncated decision frame")
    reader = _Reader(payload)
    schema_version = reader.u16()
    if schema_version != DECISION_FRAME_SCHEMA_VERSION:
        raise ContractViolation(ContractError.unsupported_schema, str(schema_version))
    ids = [reader.u64() for _ in range(9)]
    event_time_ns = reader.i64()
    receive_time_ns = reader.i64()
    instrument_id = reader.u64()
    account_id = reader.u64()
    feature_count = reader.u16()
    if feature_count > MAX_DECISION_FEATURES:
        raise ContractViolation(ContractError.capacity_exceeded, "feature_count")
    features = tuple(reader.i64() for _ in range(feature_count))
    status = reader.u8()
    market_data_age_ns = reader.i64()
    position_version = reader.u64()
    risk_version = reader.u64()
    raw_evidence_digest = reader.take(DIGEST_BYTES)
    if reader.remaining:
        raise ContractViolation(ContractError.capacity_exceeded, "trailing bytes")
    if status not in tuple(int(v) for v in MarketDataStatus):
        raise ContractViolation(ContractError.invalid_enum, "market_data_status")

    frame = DecisionFrame(
        policy_id=ids[0],
        policy_version=ids[1],
        feature_schema_id=ids[2],
        venue_id=ids[3],
        venue_session_id=ids[4],
        venue_sequence=ids[5],
        source_id=ids[6],
        source_generation=ids[7],
        source_sequence=ids[8],
        event_time_ns=event_time_ns,
        receive_time_ns=receive_time_ns,
        instrument_id=instrument_id,
        account_id=account_id,
        features=features,
        market_data_status=MarketDataStatus(status),
        market_data_age_ns=market_data_age_ns,
        position_version=position_version,
        risk_version=risk_version,
        raw_evidence_digest=raw_evidence_digest,
    )
    frame.validate()
    return frame


def decode_policy_artifact(payload: bytes) -> PolicyArtifact:
    if len(payload) < POLICY_PAYLOAD_PREFIX_BYTES:
        raise ContractViolation(ContractError.capacity_exceeded, "truncated policy artifact")
    reader = _Reader(payload)
    schema_version = reader.u16()
    if schema_version != POLICY_ARTIFACT_SCHEMA_VERSION:
        raise ContractViolation(ContractError.unsupported_schema, str(schema_version))
    policy_id = reader.u64()
    policy_version = reader.u64()
    feature_schema_id = reader.u64()
    instrument_id = reader.u64()
    account_id = reader.u64()
    model_digest = reader.take(DIGEST_BYTES)
    artifact_digest = reader.take(DIGEST_BYTES)
    valid_from = reader.i64()
    valid_until = reader.i64()
    risk_policy_version = reader.u64()
    rule_count = reader.u16()
    if rule_count > MAX_POLICY_RULES:
        raise ContractViolation(ContractError.capacity_exceeded, "rule_count")
    if reader.remaining != rule_count * POLICY_RULE_ENCODED_BYTES:
        raise ContractViolation(ContractError.capacity_exceeded, "rule region length mismatch")
    rules = []
    for _ in range(rule_count):
        feature_index = reader.u16()
        predicate = reader.u8()
        directive = reader.u8()
        threshold = reader.i64()
        if predicate not in tuple(int(v) for v in PredicateOperator):
            raise ContractViolation(ContractError.invalid_enum, "predicate")
        if directive not in tuple(int(v) for v in Directive):
            raise ContractViolation(ContractError.invalid_enum, "directive")
        rules.append(
            PolicyRule(
                feature_index=feature_index,
                predicate=PredicateOperator(predicate),
                threshold_atoms=threshold,
                directive=Directive(directive),
            )
        )
    artifact = PolicyArtifact(
        policy_id=policy_id,
        policy_version=policy_version,
        feature_schema_id=feature_schema_id,
        instrument_id=instrument_id,
        account_id=account_id,
        model_digest=model_digest,
        artifact_digest=artifact_digest,
        valid_from_event_time_ns=valid_from,
        valid_until_event_time_ns=valid_until,
        risk_policy_version=risk_policy_version,
        rules=tuple(rules),
    )
    artifact.validate()
    return artifact
