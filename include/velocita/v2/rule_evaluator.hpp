// Evaluates a policy's ordered rules against a decision frame.
#pragma once

#include "velocita/v2/contracts.hpp"

#include <compare>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace velocita::v2 {

inline constexpr std::uint16_t kNoMatchedRuleIndex =
    std::numeric_limits<std::uint16_t>::max();
static_assert(kMaxPolicyRules < kNoMatchedRuleIndex);

enum class RuleEvaluationCode : std::uint8_t {
    matched = 1,
    no_rule_matched = 2,
    invalid_policy = 3,
    invalid_frame = 4,
    feature_unavailable = 5,
};

struct RuleEvaluation {
    RuleEvaluationCode code{RuleEvaluationCode::invalid_policy};
    ContractError contract_error{ContractError::none};
    Directive directive{Directive::no_action};
    std::uint16_t matched_rule_index{kNoMatchedRuleIndex};
    std::uint16_t rules_visited{};

    constexpr auto operator<=>(const RuleEvaluation&) const noexcept = default;
};

[[nodiscard]] RuleEvaluation
evaluate_rules(const PolicyArtifact& policy,
               const DecisionFrame& frame) noexcept;

static_assert(std::is_trivially_copyable_v<RuleEvaluation>);
static_assert(sizeof(RuleEvaluation) <= kRuleEvaluationObjectBudgetBytes,
              "RuleEvaluation exceeded its reviewed hot-result budget");

}
