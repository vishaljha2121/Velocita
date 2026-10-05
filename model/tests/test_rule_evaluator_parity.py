"""Semantic parity with `src/v2_rule_evaluator.cpp`.

Scope, stated honestly: these tests pin the semantics this Python mirror is
*claimed* to reproduce - operator boundaries, first-match ordering, terminal
`no_action`, the whole-set feature-availability pre-pass, visit counting, and
the section 3.4 control table. They do not execute the C++ evaluator, so they
prove self-consistency plus adherence to the written contract, not
cross-binary equivalence.

Cross-binary equivalence needs a differential harness that runs both evaluators
over one generated corpus, the way `v2_model_replay_tests.cpp` already does for
the reducer against its reference model. That harness is the correct next gate
before any policy built here is activated, and it is not yet built.
"""

from __future__ import annotations

import dataclasses
import unittest

from velocita_model.contracts.codec import DecisionFrame, PolicyArtifact, PolicyRule
from velocita_model.contracts.enums import (
    ContractError,
    Directive,
    MarketDataStatus,
    PredicateOperator,
    StrategyMode,
)
from velocita_model.simulation.rule_evaluator import (
    NO_MATCHED_RULE_INDEX,
    ControlOutcome,
    RuleEvaluationCode,
    decide,
    evaluate_rules,
)


def frame(features: tuple[int, ...]) -> DecisionFrame:
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
        features=features,
        market_data_status=MarketDataStatus.ready,
        market_data_age_ns=1_000,
        position_version=9,
        risk_version=10,
        raw_evidence_digest=bytes([0x33]) + bytes(31),
    )


def policy(*rules: PolicyRule) -> PolicyArtifact:
    return PolicyArtifact(
        policy_id=1,
        policy_version=2,
        feature_schema_id=3,
        instrument_id=4,
        account_id=5,
        model_digest=bytes([0x11]) + bytes(31),
        artifact_digest=bytes([0x22]) + bytes(31),
        valid_from_event_time_ns=100,
        valid_until_event_time_ns=200,
        risk_policy_version=6,
        rules=rules,
    )


def rule(index: int, op: PredicateOperator, threshold: int, directive: Directive) -> PolicyRule:
    return PolicyRule(feature_index=index, predicate=op, threshold_atoms=threshold, directive=directive)


class PredicateBoundaries(unittest.TestCase):
    def _directive_for(self, op: PredicateOperator, value: int) -> Directive:
        result = evaluate_rules(policy(rule(0, op, 1_000_000, Directive.place_limit)), frame((value,)))
        return result.directive

    def test_less_than(self) -> None:
        self.assertEqual(self._directive_for(PredicateOperator.less_than, 999_999), Directive.place_limit)
        self.assertEqual(self._directive_for(PredicateOperator.less_than, 1_000_000), Directive.no_action)

    def test_less_than_or_equal(self) -> None:
        op = PredicateOperator.less_than_or_equal
        self.assertEqual(self._directive_for(op, 1_000_000), Directive.place_limit)
        self.assertEqual(self._directive_for(op, 1_000_001), Directive.no_action)

    def test_equal(self) -> None:
        op = PredicateOperator.equal
        self.assertEqual(self._directive_for(op, 1_000_000), Directive.place_limit)
        self.assertEqual(self._directive_for(op, 999_999), Directive.no_action)

    def test_greater_than_or_equal(self) -> None:
        op = PredicateOperator.greater_than_or_equal
        self.assertEqual(self._directive_for(op, 1_000_000), Directive.place_limit)
        self.assertEqual(self._directive_for(op, 999_999), Directive.no_action)

    def test_greater_than(self) -> None:
        op = PredicateOperator.greater_than
        self.assertEqual(self._directive_for(op, 1_000_001), Directive.place_limit)
        self.assertEqual(self._directive_for(op, 1_000_000), Directive.no_action)

    def test_negative_thresholds_compare_by_sign(self) -> None:
        result = evaluate_rules(
            policy(rule(0, PredicateOperator.less_than, -500_000, Directive.cancel)),
            frame((-500_001,)),
        )
        self.assertEqual(result.directive, Directive.cancel)


class OrderingSemantics(unittest.TestCase):
    def test_first_match_wins_even_when_a_later_rule_also_matches(self) -> None:
        result = evaluate_rules(
            policy(
                rule(0, PredicateOperator.greater_than, 0, Directive.cancel),
                rule(0, PredicateOperator.greater_than, 0, Directive.place_limit),
            ),
            frame((1,)),
        )
        self.assertEqual(result.directive, Directive.cancel)
        self.assertEqual(result.matched_rule_index, 0)
        self.assertEqual(result.rules_visited, 1)

    def test_matching_no_action_rule_is_terminal(self) -> None:
        result = evaluate_rules(
            policy(
                rule(0, PredicateOperator.greater_than, 0, Directive.no_action),
                rule(0, PredicateOperator.greater_than, 0, Directive.place_limit),
            ),
            frame((1,)),
        )
        self.assertEqual(result.code, RuleEvaluationCode.matched)
        self.assertEqual(result.directive, Directive.no_action)
        self.assertEqual(result.matched_rule_index, 0)

    def test_no_match_reports_full_visit_count(self) -> None:
        result = evaluate_rules(
            policy(
                rule(0, PredicateOperator.greater_than, 10, Directive.place_limit),
                rule(0, PredicateOperator.greater_than, 20, Directive.place_limit),
            ),
            frame((1,)),
        )
        self.assertEqual(result.code, RuleEvaluationCode.no_rule_matched)
        self.assertEqual(result.matched_rule_index, NO_MATCHED_RULE_INDEX)
        self.assertEqual(result.rules_visited, 2)

    def test_visit_count_is_one_based_at_the_match(self) -> None:
        result = evaluate_rules(
            policy(
                rule(0, PredicateOperator.greater_than, 100, Directive.place_limit),
                rule(0, PredicateOperator.greater_than, 0, Directive.cancel),
            ),
            frame((1,)),
        )
        self.assertEqual(result.matched_rule_index, 1)
        self.assertEqual(result.rules_visited, 2)


class FailClosedBehaviour(unittest.TestCase):
    def test_feature_availability_is_checked_across_the_whole_rule_set(self) -> None:
        result = evaluate_rules(
            policy(
                rule(0, PredicateOperator.greater_than, 0, Directive.place_limit),
                rule(5, PredicateOperator.greater_than, 0, Directive.cancel),
            ),
            frame((1, 2)),
        )
        self.assertEqual(result.code, RuleEvaluationCode.feature_unavailable)
        self.assertEqual(result.contract_error, ContractError.invalid_feature_index)
        self.assertEqual(result.rules_visited, 0)
        self.assertEqual(result.directive, Directive.no_action)

    def test_invalid_policy_is_reported_before_the_frame_is_examined(self) -> None:
        bad = dataclasses.replace(policy(rule(0, PredicateOperator.equal, 0, Directive.cancel)), rules=())
        result = evaluate_rules(bad, frame((1,)))
        self.assertEqual(result.code, RuleEvaluationCode.invalid_policy)
        self.assertEqual(result.contract_error, ContractError.empty_rules)

    def test_invalid_frame_is_reported_with_its_typed_reason(self) -> None:
        bad = dataclasses.replace(frame((1,)), market_data_age_ns=-1)
        result = evaluate_rules(policy(rule(0, PredicateOperator.equal, 1, Directive.cancel)), bad)
        self.assertEqual(result.code, RuleEvaluationCode.invalid_frame)
        self.assertEqual(result.contract_error, ContractError.invalid_age)


class ControlAuthorityDominance(unittest.TestCase):
    def _decide(self, directive: Directive, mode: StrategyMode):
        return decide(policy(rule(0, PredicateOperator.greater_than, 0, directive)), frame((1,)), mode)

    def test_normal_permits_every_directive(self) -> None:
        for directive in Directive:
            outcome = self._decide(directive, StrategyMode.normal)
            self.assertEqual(outcome.control, ControlOutcome.permitted)
            self.assertEqual(outcome.effective_directive, directive)

    def test_no_new_risk_suppresses_place_and_replace_only(self) -> None:
        for directive in (Directive.place_limit, Directive.replace):
            outcome = self._decide(directive, StrategyMode.no_new_risk)
            self.assertEqual(outcome.control, ControlOutcome.suppressed_no_new_risk)
            self.assertEqual(outcome.effective_directive, Directive.no_action)
        outcome = self._decide(Directive.cancel, StrategyMode.no_new_risk)
        self.assertEqual(outcome.effective_directive, Directive.cancel)

    def test_reduce_only_fails_closed_rather_than_assuming_reduction(self) -> None:
        outcome = self._decide(Directive.replace, StrategyMode.reduce_only)
        self.assertEqual(outcome.control, ControlOutcome.suppressed_reduce_only)
        self.assertEqual(outcome.effective_directive, Directive.no_action)

    def test_halted_short_circuits_before_rule_work(self) -> None:
        outcome = self._decide(Directive.cancel, StrategyMode.halted)
        self.assertEqual(outcome.control, ControlOutcome.suppressed_halted)
        self.assertEqual(outcome.effective_directive, Directive.no_action)
        self.assertEqual(outcome.evaluation.rules_visited, 0)


class ShadowAuthority(unittest.TestCase):
    def test_a_shadow_decision_never_reports_an_order(self) -> None:
        outcome = decide(
            policy(rule(0, PredicateOperator.greater_than, 0, Directive.place_limit)),
            frame((1,)),
            StrategyMode.normal,
        )
        self.assertTrue(outcome.shadow)
        self.assertFalse(outcome.directs_new_risk)
        self.assertTrue(outcome.directs_new_risk_if_authoritative)

    def test_shadow_is_the_default(self) -> None:
        outcome = decide(policy(rule(0, PredicateOperator.equal, 1, Directive.cancel)), frame((1,)))
        self.assertTrue(outcome.shadow)


if __name__ == "__main__":
    unittest.main()
