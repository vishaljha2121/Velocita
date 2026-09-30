"""A faithful mirror of the cluster's decision semantics.

This is the module that makes shadow research meaningful. Simulating fills
against an idealised strategy tells you what an idealised strategy would have
done. Simulating against *this* - the exact ordered first-match evaluator the
Raft reducer runs, plus the exact control-mode dominance table - tells you what
the deployed system would have done, which is the only number worth acting on.

Mirrored from:
  * `src/v2_rule_evaluator.cpp` (evaluation, validation order, visit counting)
  * `docs/V2_PHASE3_DECISION_EVALUATOR_DESIGN.md` section 3.4 (control authority)

Divergence between this file and the C++ evaluator is a defect in this file.
`tests/test_rule_evaluator_parity.py` pins the behaviour that is cheap to pin
from here; the honest statement of its limits is in that test's docstring.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from typing import Final

from ..contracts.codec import DecisionFrame, PolicyArtifact
from ..contracts.enums import (
    ContractError,
    ContractViolation,
    Directive,
    PredicateOperator,
    StrategyMode,
)

#: `kNoMatchedRuleIndex` from `rule_evaluator.hpp`.
NO_MATCHED_RULE_INDEX: Final = 0xFFFF


class RuleEvaluationCode(IntEnum):
    matched = 1
    no_rule_matched = 2
    invalid_policy = 3
    invalid_frame = 4
    feature_unavailable = 5


class ControlOutcome(IntEnum):
    """Why replicated control state permitted or suppressed the directive."""

    permitted = 1
    suppressed_no_new_risk = 2
    suppressed_cancel_only = 3
    suppressed_reduce_only = 4
    suppressed_halted = 5


@dataclass(frozen=True, slots=True)
class RuleEvaluation:
    code: RuleEvaluationCode
    contract_error: ContractError
    directive: Directive
    matched_rule_index: int
    rules_visited: int


@dataclass(frozen=True, slots=True)
class Decision:
    """What the cluster would retain: the policy result plus control authority."""

    evaluation: RuleEvaluation
    control: ControlOutcome
    effective_directive: Directive
    shadow: bool = True

    @property
    def directs_new_risk(self) -> bool:
        """True only for a directive that survives control authority AND is
        authoritative. A shadow decision never produces an effect, by contract:
        Phase 4 must reject a risk-increasing effect built from one.

        The name avoids the order-management vocabulary the safety audit bans on
        purpose. This property is the closest this package comes to describing an
        external effect, and it should not read like a verb."""
        return (
            not self.shadow
            and self.control == ControlOutcome.permitted
            and self.effective_directive in (Directive.place_limit, Directive.replace)
        )

    @property
    def directs_new_risk_if_authoritative(self) -> bool:
        """The research question: would this frame have directed new risk if the
        same policy were activated for real? This is the quantity the shadow
        simulator counts, and it must never be confused with the property above.
        """
        return self.control == ControlOutcome.permitted and self.effective_directive in (
            Directive.place_limit,
            Directive.replace,
        )


def _matches(feature_atoms: int, predicate: PredicateOperator, threshold_atoms: int) -> bool:
    """Exact integer comparison. No tolerance, no float, no locale."""
    match predicate:
        case PredicateOperator.less_than:
            return feature_atoms < threshold_atoms
        case PredicateOperator.less_than_or_equal:
            return feature_atoms <= threshold_atoms
        case PredicateOperator.equal:
            return feature_atoms == threshold_atoms
        case PredicateOperator.greater_than_or_equal:
            return feature_atoms >= threshold_atoms
        case PredicateOperator.greater_than:
            return feature_atoms > threshold_atoms
    return False


def evaluate_rules(policy: PolicyArtifact, frame: DecisionFrame) -> RuleEvaluation:
    """Ordered first-match evaluation, including a terminal `no_action` match."""
    try:
        policy.validate()
    except ContractViolation as violation:
        return RuleEvaluation(
            RuleEvaluationCode.invalid_policy,
            violation.error,
            Directive.no_action,
            NO_MATCHED_RULE_INDEX,
            0,
        )

    try:
        frame.validate()
    except ContractViolation as violation:
        return RuleEvaluation(
            RuleEvaluationCode.invalid_frame,
            violation.error,
            Directive.no_action,
            NO_MATCHED_RULE_INDEX,
            0,
        )

    # Availability is checked across the whole rule set before any economic
    # comparison, so a policy referencing a feature this frame does not carry
    # fails identically regardless of where that rule sits in the array.
    for rule in policy.rules:
        if rule.feature_index >= len(frame.features):
            return RuleEvaluation(
                RuleEvaluationCode.feature_unavailable,
                ContractError.invalid_feature_index,
                Directive.no_action,
                NO_MATCHED_RULE_INDEX,
                0,
            )

    for index, rule in enumerate(policy.rules):
        if _matches(frame.features[rule.feature_index], rule.predicate, rule.threshold_atoms):
            return RuleEvaluation(
                RuleEvaluationCode.matched,
                ContractError.none,
                rule.directive,
                index,
                index + 1,
            )

    return RuleEvaluation(
        RuleEvaluationCode.no_rule_matched,
        ContractError.none,
        Directive.no_action,
        NO_MATCHED_RULE_INDEX,
        len(policy.rules),
    )


#: Directives each strategy mode permits. Control authority dominates economic
#: policy: the policy proposes, replicated control state disposes.
_PERMITTED: Final[dict[StrategyMode, frozenset[Directive]]] = {
    StrategyMode.normal: frozenset(Directive),
    StrategyMode.no_new_risk: frozenset({Directive.no_action, Directive.cancel}),
    StrategyMode.cancel_only: frozenset({Directive.no_action, Directive.cancel}),
    # reduce_only is not "the same as cancel_only". It fails closed for
    # risk-increasing actions because Phase 3 cannot prove a replace or a place
    # reduces exposure - positions and reservations are Phase 4 state. Treating
    # an unproven action as safe would invert the authority hierarchy.
    StrategyMode.reduce_only: frozenset({Directive.no_action, Directive.cancel}),
    StrategyMode.halted: frozenset({Directive.no_action}),
}

_SUPPRESSION_REASON: Final[dict[StrategyMode, ControlOutcome]] = {
    StrategyMode.no_new_risk: ControlOutcome.suppressed_no_new_risk,
    StrategyMode.cancel_only: ControlOutcome.suppressed_cancel_only,
    StrategyMode.reduce_only: ControlOutcome.suppressed_reduce_only,
    StrategyMode.halted: ControlOutcome.suppressed_halted,
}


def apply_control_authority(
    directive: Directive, mode: StrategyMode
) -> tuple[Directive, ControlOutcome]:
    if directive in _PERMITTED[mode]:
        return directive, ControlOutcome.permitted
    return Directive.no_action, _SUPPRESSION_REASON[mode]


def decide(
    policy: PolicyArtifact,
    frame: DecisionFrame,
    mode: StrategyMode = StrategyMode.normal,
    *,
    shadow: bool = True,
) -> Decision:
    """Full mirror: evaluate, then apply control authority.

    `shadow` defaults to True because everything this package produces is shadow
    by construction. An authoritative decision is one the cluster made; this
    process cannot make one and the default must not suggest otherwise.
    """
    if mode == StrategyMode.halted:
        # `halted` is the categorical exception in section 3.4: it terminates
        # before source and rule work because no strategy directive is
        # permitted. Mirroring the short-circuit matters because `rules_visited`
        # is replay evidence and a bounded-cost metric, not a debug counter.
        return Decision(
            evaluation=RuleEvaluation(
                RuleEvaluationCode.no_rule_matched,
                ContractError.none,
                Directive.no_action,
                NO_MATCHED_RULE_INDEX,
                0,
            ),
            control=ControlOutcome.suppressed_halted,
            effective_directive=Directive.no_action,
            shadow=shadow,
        )

    evaluation = evaluate_rules(policy, frame)
    effective, control = apply_control_authority(evaluation.directive, mode)
    return Decision(evaluation=evaluation, control=control, effective_directive=effective, shadow=shadow)
