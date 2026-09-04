#include "velocita/v2/rule_evaluator.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

velocita::v2::Digest256 digest(const std::byte marker) {
    velocita::v2::Digest256 value;
    value.bytes[0] = marker;
    return value;
}

velocita::v2::PolicyArtifact policy_with_rule(
    const velocita::v2::PredicateOperator predicate,
    const std::int64_t threshold_atoms,
    const velocita::v2::Directive directive = velocita::v2::Directive::place_limit) {
    using namespace velocita::v2;
    PolicyArtifact policy;
    policy.policy_id = PolicyId{1};
    policy.policy_version = PolicyVersion{2};
    policy.schema_version = kTradingContractSchemaVersion;
    policy.feature_schema_id = FeatureSchemaId{3};
    policy.instrument_id = InstrumentId{4};
    policy.account_id = AccountId{5};
    policy.model_digest = digest(std::byte{0x11});
    policy.artifact_digest = digest(std::byte{0x22});
    policy.valid_from_event_time_ns = 100;
    policy.valid_until_event_time_ns = 200;
    policy.risk_policy_version = RiskPolicyVersion{6};
    policy.rule_count = 1;
    policy.rules[0] = PolicyRule{0,
                                 predicate,
                                 FeatureMicros::from_atoms(threshold_atoms),
                                 directive};
    return policy;
}

velocita::v2::DecisionFrame frame_with_feature(const std::int64_t atoms) {
    using namespace velocita::v2;
    DecisionFrame frame;
    frame.policy_id = PolicyId{1};
    frame.policy_version = PolicyVersion{2};
    frame.feature_schema_id = FeatureSchemaId{3};
    frame.venue_id = VenueId{4};
    frame.venue_session_id = VenueSessionId{5};
    frame.venue_sequence = VenueSequence{6};
    frame.instrument_id = InstrumentId{4};
    frame.account_id = AccountId{5};
    frame.feature_count = 1;
    frame.features[0] = FeatureMicros::from_atoms(atoms);
    frame.market_data_status = MarketDataStatus::ready;
    frame.market_data_age_ns = 7;
    frame.position_version = PositionVersion{8};
    frame.risk_version = RiskVersion{9};
    frame.raw_evidence_digest = digest(std::byte{0x33});
    return frame;
}

void expect_match(const velocita::v2::PredicateOperator predicate,
                  const std::int64_t feature,
                  const std::int64_t threshold,
                  const bool expected,
                  const std::string& context) {
    using namespace velocita::v2;
    const auto evaluation =
        evaluate_rules(policy_with_rule(predicate, threshold), frame_with_feature(feature));
    require((evaluation.code == RuleEvaluationCode::matched) == expected, context);
    if (expected) {
        require(evaluation.directive == Directive::place_limit &&
                    evaluation.matched_rule_index == 0 &&
                    evaluation.rules_visited == 1,
                context + ": match metadata was incorrect");
    } else {
        require(evaluation.code == RuleEvaluationCode::no_rule_matched &&
                    evaluation.directive == Directive::no_action &&
                    evaluation.matched_rule_index == kNoMatchedRuleIndex &&
                    evaluation.rules_visited == 1,
                context + ": no-match metadata was incorrect");
    }
}

void test_all_predicates_and_exact_boundaries() {
    using velocita::v2::PredicateOperator;

    expect_match(PredicateOperator::less_than, 9, 10, true, "less-than true");
    expect_match(PredicateOperator::less_than, 10, 10, false, "less-than boundary");
    expect_match(PredicateOperator::less_than_or_equal, 10, 10, true,
                 "less-than-or-equal boundary");
    expect_match(PredicateOperator::less_than_or_equal, 11, 10, false,
                 "less-than-or-equal false");
    expect_match(PredicateOperator::equal, -10, -10, true, "equal negative boundary");
    expect_match(PredicateOperator::equal, -9, -10, false, "equal false");
    expect_match(PredicateOperator::greater_than_or_equal, 10, 10, true,
                 "greater-than-or-equal boundary");
    expect_match(PredicateOperator::greater_than_or_equal, 9, 10, false,
                 "greater-than-or-equal false");
    expect_match(PredicateOperator::greater_than, 11, 10, true, "greater-than true");
    expect_match(PredicateOperator::greater_than, 10, 10, false,
                 "greater-than boundary");
}

void test_first_match_and_terminal_no_action() {
    using namespace velocita::v2;
    auto policy = policy_with_rule(PredicateOperator::greater_than_or_equal,
                                   10,
                                   Directive::no_action);
    policy.rule_count = 3;
    policy.rules[1] = PolicyRule{0,
                                 PredicateOperator::greater_than_or_equal,
                                 FeatureMicros::from_atoms(0),
                                 Directive::place_limit};
    policy.rules[2] = PolicyRule{0,
                                 PredicateOperator::greater_than_or_equal,
                                 FeatureMicros::from_atoms(-10),
                                 Directive::replace};

    const auto evaluation = evaluate_rules(policy, frame_with_feature(20));
    require(evaluation.code == RuleEvaluationCode::matched &&
                evaluation.directive == Directive::no_action &&
                evaluation.matched_rule_index == 0 &&
                evaluation.rules_visited == 1,
            "first matching no-action rule was not terminal");

    policy.rules[0].threshold = FeatureMicros::from_atoms(30);
    const auto second = evaluate_rules(policy, frame_with_feature(20));
    require(second.code == RuleEvaluationCode::matched &&
                second.directive == Directive::place_limit &&
                second.matched_rule_index == 1 &&
                second.rules_visited == 2,
            "ordered evaluation did not select the first matching array index");
}

void test_no_match_visits_the_bounded_rule_set() {
    using namespace velocita::v2;
    auto policy = policy_with_rule(PredicateOperator::greater_than, 100);
    policy.rule_count = 2;
    policy.rules[1] = PolicyRule{0,
                                 PredicateOperator::less_than,
                                 FeatureMicros::from_atoms(-100),
                                 Directive::cancel};

    const auto evaluation = evaluate_rules(policy, frame_with_feature(0));
    require(evaluation.code == RuleEvaluationCode::no_rule_matched &&
                evaluation.directive == Directive::no_action &&
                evaluation.matched_rule_index == kNoMatchedRuleIndex &&
                evaluation.rules_visited == 2,
            "no-match did not report a complete bounded scan");
}

void test_invalid_values_fail_closed_before_economic_evaluation() {
    using namespace velocita::v2;
    const auto frame = frame_with_feature(20);

    auto unknown_predicate = policy_with_rule(PredicateOperator::greater_than, 10);
    unknown_predicate.rules[0].predicate = static_cast<PredicateOperator>(255);
    const auto invalid_policy = evaluate_rules(unknown_predicate, frame);
    require(invalid_policy.code == RuleEvaluationCode::invalid_policy &&
                invalid_policy.contract_error == ContractError::invalid_enum &&
                invalid_policy.rules_visited == 0,
            "unknown predicate did not fail closed as an invalid policy");

    auto unknown_directive = policy_with_rule(PredicateOperator::greater_than, 10);
    unknown_directive.rules[0].directive = static_cast<Directive>(255);
    const auto directive_result = evaluate_rules(unknown_directive, frame);
    require(directive_result.code == RuleEvaluationCode::invalid_policy &&
                directive_result.contract_error == ContractError::invalid_enum &&
                directive_result.rules_visited == 0,
            "unknown directive did not fail closed as an invalid policy");

    auto invalid_frame = frame;
    invalid_frame.market_data_status = static_cast<MarketDataStatus>(255);
    const auto frame_result =
        evaluate_rules(policy_with_rule(PredicateOperator::greater_than, 10), invalid_frame);
    require(frame_result.code == RuleEvaluationCode::invalid_frame &&
                frame_result.contract_error == ContractError::invalid_enum &&
                frame_result.rules_visited == 0,
            "invalid frame reached economic evaluation");

    auto unavailable_feature = policy_with_rule(PredicateOperator::greater_than, 10);
    unavailable_feature.rule_count = 2;
    unavailable_feature.rules[1] = PolicyRule{1,
                                               PredicateOperator::equal,
                                               FeatureMicros::from_atoms(0),
                                               Directive::cancel};
    const auto unavailable = evaluate_rules(unavailable_feature, frame);
    require(unavailable.code == RuleEvaluationCode::feature_unavailable &&
                unavailable.contract_error == ContractError::invalid_feature_index &&
                unavailable.rules_visited == 0,
            "early match hid a later unavailable feature reference");
}

void test_extreme_integer_atoms_do_not_overflow() {
    using namespace velocita::v2;
    expect_match(PredicateOperator::less_than,
                 std::numeric_limits<std::int64_t>::min(),
                 std::numeric_limits<std::int64_t>::max(),
                 true,
                 "minimum versus maximum");
    expect_match(PredicateOperator::greater_than,
                 std::numeric_limits<std::int64_t>::max(),
                 std::numeric_limits<std::int64_t>::min(),
                 true,
                 "maximum versus minimum");
}

}  // namespace

int main() {
    try {
        test_all_predicates_and_exact_boundaries();
        test_first_match_and_terminal_no_action();
        test_no_match_visits_the_bounded_rule_set();
        test_invalid_values_fail_closed_before_economic_evaluation();
        test_extreme_integer_atoms_do_not_overflow();
    } catch (const std::exception& error) {
        std::cerr << "v2 rule evaluator test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
