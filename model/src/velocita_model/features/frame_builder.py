"""Assemble a canonical `DecisionFrame` from observed market state.

This is the boundary. Everything upstream of this module is ordinary research
code that may use floats, heuristics, and judgement. Everything downstream is a
byte contract the Raft reducer will re-validate and may reject.

Three properties are enforced here and nowhere else:

  * **Monotonic source provenance.** `source_sequence` advances by exactly one
    per emitted frame and never regresses. A regression without a new
    `source_generation` is refused locally, because the cluster's source-
    admission logic would refuse it anyway and finding out here is cheaper.

  * **Evidence digest over raw bytes.** `raw_evidence_digest` names the exact
    upstream payloads this frame was derived from, not a re-serialisation of the
    parsed values. That is what makes a frame auditable after the fact: you can
    prove which bytes produced it.

  * **Refusal, not repair.** A frame that would fail `validate()` is never
    emitted with a substituted value. Missing data produces a typed refusal and
    a recorded gap.
"""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal

from ..contracts.codec import DecisionFrame
from ..contracts.digest import sha256_digest
from ..contracts.domain import feature_atoms
from ..contracts.enums import ContractError, ContractViolation, MarketDataStatus
from .schema import FeatureSchema


class FrameRefused(Exception):
    def __init__(self, reason: str, contract_error: ContractError | None = None) -> None:
        self.reason = reason
        self.contract_error = contract_error
        super().__init__(reason)


@dataclass(frozen=True, slots=True)
class FrameIdentity:
    policy_id: int
    policy_version: int
    venue_id: int
    venue_session_id: int
    source_id: int
    source_generation: int
    instrument_id: int
    account_id: int
    position_version: int = 1
    risk_version: int = 1


class FeatureVector:
    __slots__ = ("_schema", "_values")

    def __init__(self, schema: FeatureSchema) -> None:
        self._schema = schema
        self._values: dict[int, int] = {}

    def set(self, name: str, value: Decimal | int | str | None) -> None:
        index = self._schema.index_of(name)
        if value is None:
            self._values.pop(index, None)
            return
        spec = self._schema.spec(name)
        atoms = feature_atoms(value)
        if not spec.signed and atoms < 0:
            raise FrameRefused(
                f"feature {name!r} is declared unsigned but received {value}",
                ContractError.non_canonical_value,
            )
        self._values[index] = atoms

    def missing(self, upto: int) -> list[str]:
        return [
            self._schema.specs[index].name
            for index in range(upto)
            if index not in self._values
        ]

    def atoms(self, count: int) -> tuple[int, ...]:
        holes = self.missing(count)
        if holes:
            raise FrameRefused(
                f"feature vector has unset values and will not be zero-filled: {', '.join(holes)}",
                ContractError.non_canonical_value,
            )
        return tuple(self._values[index] for index in range(count))


class DecisionFrameBuilder:
    __slots__ = ("_schema", "_identity", "_feature_count", "_source_sequence", "_venue_sequence")

    def __init__(
        self,
        schema: FeatureSchema,
        identity: FrameIdentity,
        *,
        feature_count: int | None = None,
        initial_source_sequence: int = 0,
    ) -> None:
        self._schema = schema
        self._identity = identity
        self._feature_count = feature_count if feature_count is not None else len(schema)
        if not (0 < self._feature_count <= len(schema)):
            raise ValueError(f"feature_count {self._feature_count} outside schema of {len(schema)}")
        self._source_sequence = initial_source_sequence
        self._venue_sequence = 0

    @property
    def source_sequence(self) -> int:
        return self._source_sequence

    def new_vector(self) -> FeatureVector:
        return FeatureVector(self._schema)

    def build(
        self,
        *,
        vector: FeatureVector,
        event_time_ns: int,
        receive_time_ns: int,
        market_data_status: MarketDataStatus,
        market_data_age_ns: int,
        raw_payloads: tuple[bytes, ...],
        venue_sequence: int | None = None,
    ) -> DecisionFrame:
        if not raw_payloads:
            raise FrameRefused("no raw evidence supplied", ContractError.missing_digest)
        if event_time_ns <= 0 or receive_time_ns <= 0:
            raise FrameRefused("non-positive timestamp", ContractError.invalid_time_range)
        if market_data_age_ns < 0:
            raise FrameRefused("negative market-data age", ContractError.invalid_age)

        if venue_sequence is None:
            self._venue_sequence += 1
            venue_sequence = self._venue_sequence
        else:
            if venue_sequence <= 0:
                raise FrameRefused("venue sequence must be positive", ContractError.invalid_identity)
            if venue_sequence < self._venue_sequence:
                raise FrameRefused(
                    f"venue sequence regressed {self._venue_sequence} -> {venue_sequence}",
                    ContractError.invalid_identity,
                )
            self._venue_sequence = venue_sequence

        self._source_sequence += 1
        frame = DecisionFrame(
            policy_id=self._identity.policy_id,
            policy_version=self._identity.policy_version,
            feature_schema_id=self._schema.schema_id,
            venue_id=self._identity.venue_id,
            venue_session_id=self._identity.venue_session_id,
            venue_sequence=venue_sequence,
            source_id=self._identity.source_id,
            source_generation=self._identity.source_generation,
            source_sequence=self._source_sequence,
            event_time_ns=event_time_ns,
            receive_time_ns=receive_time_ns,
            instrument_id=self._identity.instrument_id,
            account_id=self._identity.account_id,
            features=vector.atoms(self._feature_count),
            market_data_status=market_data_status,
            market_data_age_ns=market_data_age_ns,
            position_version=self._identity.position_version,
            risk_version=self._identity.risk_version,
            raw_evidence_digest=evidence_digest(raw_payloads),
        )
        try:
            frame.validate()
        except ContractViolation as violation:
            self._source_sequence -= 1
            raise FrameRefused(str(violation), violation.error) from violation
        return frame


def evidence_digest(payloads: tuple[bytes, ...]) -> bytes:
    buffer = bytearray()
    for payload in payloads:
        buffer += len(payload).to_bytes(8, "little")
        buffer += payload
    digest = sha256_digest(bytes(buffer))
    if digest == bytes(32):  # pragma: no cover - cannot occur for SHA-256
        raise FrameRefused("evidence digest is zero", ContractError.missing_digest)
    return digest
