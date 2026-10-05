#include "velocita/v2/rule_evaluator.hpp"

#include <cstddef>
#include <cstdint>

namespace velocita::v2 {
namespace {

[[nodiscard]] bool matches(const FeatureMicros feature,
                           const PredicateOperator predicate,
                           const FeatureMicros threshold) noexcept {
    const auto left = feature.atoms();
    const auto right = threshold.atoms();
    switch (predicate) {
        case PredicateOperator::less_than:
            return left < right;
        case PredicateOperator::less_than_or_equal:
            return left <= right;
        case PredicateOperator::equal:
            return left == right;
        case PredicateOperator::greater_than_or_equal:
            return left >= right;
        case PredicateOperator::greater_than:
            return left > right;
    }
    return false;
}

}

RuleEvaluation evaluate_rules(const PolicyArtifact& policy,
                              const DecisionFrame& frame) noexcept {
    const auto policy_error = validate(policy);
    if (policy_error != ContractError::none) {
        return {RuleEvaluationCode::invalid_policy,
                policy_error,
                Directive::no_action,
                kNoMatchedRuleIndex,
                0};
    }

    const auto frame_error = validate(frame);
    if (frame_error != ContractError::none) {
        return {RuleEvaluationCode::invalid_frame,
                frame_error,
                Directive::no_action,
                kNoMatchedRuleIndex,
                0};
    }

    for (std::size_t index = 0; index < policy.rule_count; ++index) {
        if (policy.rules[index].feature_index >= frame.feature_count) {
            return {RuleEvaluationCode::feature_unavailable,
                    ContractError::invalid_feature_index,
                    Directive::no_action,
                    kNoMatchedRuleIndex,
                    0};
        }
    }

    for (std::size_t index = 0; index < policy.rule_count; ++index) {
        const auto& rule = policy.rules[index];
        if (matches(frame.features[rule.feature_index], rule.predicate, rule.threshold)) {
            return {RuleEvaluationCode::matched,
                    ContractError::none,
                    rule.directive,
                    static_cast<std::uint16_t>(index),
                    static_cast<std::uint16_t>(index + 1)};
        }
    }

    return {RuleEvaluationCode::no_rule_matched,
            ContractError::none,
            Directive::no_action,
            kNoMatchedRuleIndex,
            policy.rule_count};
}

}
