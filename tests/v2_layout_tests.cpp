#include "velocita/v2/layout.hpp"

#include "velocita/v2/contracts.hpp"
#include "velocita/v2/kernel.hpp"
#include "velocita/v2/rule_evaluator.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_target_cache_line_contract() {
    using namespace velocita::v2;
    static_assert(kTargetCacheLineBytes >= alignof(std::max_align_t));
    static_assert(is_power_of_two(kTargetCacheLineBytes));
    static_assert(round_up_to_cache_line(1) == kTargetCacheLineBytes);
    static_assert(round_up_to_cache_line(kTargetCacheLineBytes) ==
                  kTargetCacheLineBytes);
    static_assert(round_up_to_cache_line(kTargetCacheLineBytes + 1) ==
                  2 * kTargetCacheLineBytes);
}

void test_current_object_and_slot_budgets() {
    using namespace velocita::v2;
    static_assert(std::is_trivially_copyable_v<DecisionFrame>);
    static_assert(sizeof(DecisionFrame) <= kDecisionFrameObjectBudgetBytes);
    static_assert(sizeof(PolicyArtifact) <= kPolicyArtifactObjectBudgetBytes);
    static_assert(sizeof(RuleEvaluation) <= kRuleEvaluationObjectBudgetBytes);
    static_assert(sizeof(TradingState) <= kTradingStateObjectBudgetBytes);

    require(kDecisionFrameArenaSlotStrideBytes >= sizeof(DecisionFrame),
            "decision-frame arena stride is smaller than its object");
    require((kDecisionFrameArenaSlotStrideBytes % kTargetCacheLineBytes) == 0,
            "decision-frame arena stride is not cache-line rounded");
}

}  // namespace

int main() {
    try {
        test_target_cache_line_contract();
        test_current_object_and_slot_budgets();
        std::cout << "all V2 layout tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 layout test failed: " << error.what() << '\n';
        return 1;
    }
}
