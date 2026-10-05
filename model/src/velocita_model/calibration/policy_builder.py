"""Build immutable `PolicyArtifact` values from named, reviewable rules.

Calibration may use whatever it likes - floats, fitting, judgement. Its output
is an immutable artifact whose rules are exact integers. That boundary is the
whole point: a model can be wrong, but it cannot be *nondeterministic* once
frozen into an artifact.

Two digest conventions are fixed here. Neither is enforced by the C++ contract,
which only requires both digests to be non-zero, so they are stated explicitly
rather than assumed:

  * `artifact_digest` = SHA-256 of the canonical payload with the
    `artifact_digest` field itself zeroed. Self-referential hashing needs a
    declared hole, and this is where it is declared.
  * `model_digest` = SHA-256 of the canonical description of the calibration
    inputs - the dataset window, the schema, and the parameters that produced
    these rules. It answers "what produced this policy?", which is a different
    question from "what are these bytes?".

Publication is not activation. Building an artifact here has no effect on any
cluster. Activation is a separate committed command that requires every voter to
possess the exact artifact named by its digest.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from decimal import Decimal

from ..contracts.codec import PolicyArtifact, PolicyRule
from ..contracts.digest import sha256_digest
from ..contracts.domain import feature_atoms
from ..contracts.enums import MAX_POLICY_RULES, Directive, PredicateOperator
from ..features.schema import FeatureSchema

_ARTIFACT_DIGEST_OFFSET = 2 + 5 * 8 + 32
_DIGEST_LENGTH = 32


@dataclass(frozen=True, slots=True)
class NamedRule:
    feature: str
    operator: PredicateOperator
    threshold: str
    directive: Directive
    rationale: str = ""

    def describe(self) -> str:
        symbol = {
            PredicateOperator.less_than: "<",
            PredicateOperator.less_than_or_equal: "<=",
            PredicateOperator.equal: "==",
            PredicateOperator.greater_than_or_equal: ">=",
            PredicateOperator.greater_than: ">",
        }[self.operator]
        text = f"{self.feature} {symbol} {self.threshold} -> {self.directive.name}"
        return f"{text}  # {self.rationale}" if self.rationale else text


@dataclass(frozen=True, slots=True)
class CalibrationInputs:
    dataset_start_ns: int
    dataset_end_ns: int
    pair_id: str
    sample_count: int
    method: str
    parameters: dict[str, str]

    def digest(self, schema: FeatureSchema) -> bytes:
        canonical = json.dumps(
            {
                "dataset_start_ns": self.dataset_start_ns,
                "dataset_end_ns": self.dataset_end_ns,
                "pair_id": self.pair_id,
                "sample_count": self.sample_count,
                "method": self.method,
                "parameters": self.parameters,
                "feature_schema": schema.digest.hex(),
            },
            sort_keys=True,
            separators=(",", ":"),
        )
        return sha256_digest(canonical.encode("utf-8"))


class PolicyBuildError(ValueError):
    pass


def compute_artifact_digest(artifact: PolicyArtifact) -> bytes:
    payload = bytearray(artifact.encode())
    start, end = _ARTIFACT_DIGEST_OFFSET, _ARTIFACT_DIGEST_OFFSET + _DIGEST_LENGTH
    payload[start:end] = bytes(_DIGEST_LENGTH)
    return sha256_digest(bytes(payload))


def build_policy(
    *,
    schema: FeatureSchema,
    rules: tuple[NamedRule, ...],
    policy_id: int,
    policy_version: int,
    instrument_id: int,
    account_id: int,
    risk_policy_version: int,
    valid_from_event_time_ns: int,
    valid_until_event_time_ns: int,
    calibration: CalibrationInputs,
) -> PolicyArtifact:
    if not rules:
        raise PolicyBuildError("a policy must declare at least one rule")
    if len(rules) > MAX_POLICY_RULES:
        raise PolicyBuildError(f"{len(rules)} rules exceeds the bounded maximum of {MAX_POLICY_RULES}")

    encoded_rules = []
    for position, rule in enumerate(rules):
        try:
            index = schema.index_of(rule.feature)
        except KeyError as exc:
            raise PolicyBuildError(f"rule {position}: {exc}") from None
        encoded_rules.append(
            PolicyRule(
                feature_index=index,
                predicate=rule.operator,
                threshold_atoms=feature_atoms(rule.threshold),
                directive=rule.directive,
            )
        )

    model_digest = calibration.digest(schema)
    draft = PolicyArtifact(
        policy_id=policy_id,
        policy_version=policy_version,
        feature_schema_id=schema.schema_id,
        instrument_id=instrument_id,
        account_id=account_id,
        model_digest=model_digest,
        artifact_digest=bytes([1]) + bytes(31),
        valid_from_event_time_ns=valid_from_event_time_ns,
        valid_until_event_time_ns=valid_until_event_time_ns,
        risk_policy_version=risk_policy_version,
        rules=tuple(encoded_rules),
    )
    artifact_digest = compute_artifact_digest(draft)

    from dataclasses import replace

    final = replace(draft, artifact_digest=artifact_digest)
    final.validate()
    return final


def verify_artifact_digest(artifact: PolicyArtifact) -> bool:
    return compute_artifact_digest(artifact) == artifact.artifact_digest


def describe_policy(artifact: PolicyArtifact, schema: FeatureSchema, rules: tuple[NamedRule, ...]) -> str:
    lines = [
        f"policy {artifact.policy_id} version {artifact.policy_version}",
        f"  feature schema : {schema.revision} (id {schema.schema_id})",
        f"  instrument     : {artifact.instrument_id}   account: {artifact.account_id}",
        f"  risk policy    : v{artifact.risk_policy_version}",
        f"  validity       : [{artifact.valid_from_event_time_ns}, {artifact.valid_until_event_time_ns})",
        f"  model digest   : {artifact.model_digest.hex()}",
        f"  artifact digest: {artifact.artifact_digest.hex()}",
        f"  canonical bytes: {len(artifact.encode())}",
        "  rules (first match wins; order is part of the policy):",
    ]
    lines += [f"    [{index}] {rule.describe()}" for index, rule in enumerate(rules)]
    return "\n".join(lines)
