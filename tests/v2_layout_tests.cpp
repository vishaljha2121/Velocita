#include "velocita/v2/layout.hpp"

#include "velocita/v2/action_template.hpp"
#include "velocita/v2/action_resolver.hpp"
#include "velocita/v2/contracts.hpp"
#include "velocita/v2/executable_policy.hpp"
#include "velocita/v2/kernel.hpp"
#include "velocita/v2/payload.hpp"
#include "velocita/v2/rule_evaluator.hpp"
#include "velocita/v2/state_snapshot.hpp"

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
    static_assert(sizeof(PriceSource) <= kPriceSourceObjectBudgetBytes);
    static_assert(sizeof(QuantitySource) <= kQuantitySourceObjectBudgetBytes);
    static_assert(sizeof(ActionSafetyCaps) <= kActionSafetyCapsObjectBudgetBytes);
    static_assert(sizeof(ActionTemplate) <= kActionTemplateObjectBudgetBytes);
    static_assert(sizeof(EconomicActionProposal) <=
                  kEconomicActionProposalObjectBudgetBytes);
    static_assert(sizeof(InstrumentExecutionProfile) <=
                  kInstrumentExecutionProfileObjectBudgetBytes);
    static_assert(sizeof(VenueCapabilities) <=
                  kVenueCapabilitiesObjectBudgetBytes);
    static_assert(sizeof(ActionResolution) <=
                  kActionResolutionObjectBudgetBytes);
    static_assert(sizeof(FeatureDescriptor) <=
                  kFeatureDescriptorObjectBudgetBytes);
    static_assert(sizeof(FeatureSchemaArtifact) <=
                  kFeatureSchemaArtifactObjectBudgetBytes);
    static_assert(sizeof(ExecutablePolicyArtifact) <=
                  kExecutablePolicyArtifactObjectBudgetBytes);
    static_assert(sizeof(TradingState) <= kTradingStateObjectBudgetBytes);
    static_assert(kMaxStateSnapshotBytes < 64U * 1024U);

    require(kDecisionFrameArenaSlotStrideBytes >= sizeof(DecisionFrame),
            "decision-frame arena stride is smaller than its object");
    require((kDecisionFrameArenaSlotStrideBytes % kTargetCacheLineBytes) == 0,
            "decision-frame arena stride is not cache-line rounded");
}

void test_integrated_executable_policy_capacity_is_bounded() {
    using namespace velocita::v2;
    static_assert(kMaxExecutablePolicyArtifacts == 2);
    static_assert(sizeof(ExecutablePolicyBundle) *
                      kMaxExecutablePolicyArtifacts <=
                  5U * 1024U);
    require(sizeof(TradingState) <= kTradingStateObjectBudgetBytes,
            "integrated active/staged policy state exceeded its reviewed "
            "provisional ceiling");
    // Phase 4 deliberately consumes the Phase 3 reserve without raising the
    // provisional 80-KiB ceiling. Keep enough explicit slack to make padding
    // or an accidental field addition visible before canonical integration.
    // 448, not 512: the P2.1 risk fields (max_position_quantity, allow_short)
    // grew RiskAccount 112 -> 128 bytes x 4 accounts, leaving 464 free.
    require(kTradingStateObjectBudgetBytes - sizeof(TradingState) >= 448,
            "Phase 4 in-memory authorization exhausted state headroom");
    static_assert(kMaxFeatureSchemaPayloadBytes <=
                  kFeatureSchemaArtifactPayloadBudgetBytes);
    static_assert(kMaxExecutablePolicyPayloadBytes <=
                  kExecutablePolicyArtifactPayloadBudgetBytes);
    static_assert(kMaxExecutablePolicyArtifacts *
                      (kMaxFeatureSchemaPayloadBytes +
                       kMaxExecutablePolicyPayloadBytes) ==
                  3220);
}

void test_phase4_authorization_regions_are_bounded() {
    using namespace velocita::v2;
    static_assert(kMaxRiskAccounts == 4);
    static_assert(kMaxRiskReservations == 16);
    static_assert(kMaxClusterOrders == 16);
    static_assert(kMaxOutboxEffects == 16);
    static_assert(sizeof(ApplyResult) == 112);
    static_assert(sizeof(RequestReceipt) == 160);
    static_assert(sizeof(RiskAccount) <= kRiskAccountObjectBudgetBytes);
    static_assert(sizeof(RiskReservation) <=
                  kRiskReservationObjectBudgetBytes);
    static_assert(sizeof(ClusterOrder) <= kClusterOrderObjectBudgetBytes);
    static_assert(sizeof(OutboxEffectRecord) <=
                  kOutboxEffectRecordObjectBudgetBytes);
    require(sizeof(RiskReservation) == kTargetCacheLineBytes,
            "reservation no longer fits its one-line MVP record");
    require(sizeof(OutboxEffectRecord) == kTargetCacheLineBytes,
            "outbox identity record no longer fits its one-line MVP record");
}

void test_decision_records_have_separate_hot_and_cold_layouts() {
    using namespace velocita::v2;
    static_assert(kMaxDecisionRecords == 16);
    static_assert(std::is_trivially_copyable_v<DecisionRecordHot>);
    static_assert(std::is_trivially_copyable_v<DecisionRecordCold>);
    static_assert(sizeof(DecisionRecordHot) <= kDecisionRecordHotBudgetBytes);
    static_assert(sizeof(DecisionRecordCold) <= kDecisionRecordColdBudgetBytes);

    require(kDecisionRecordHotArenaSlotStrideBytes == kTargetCacheLineBytes,
            "hot decision record does not fit one cache-line slot");
    require(kDecisionRecordColdArenaSlotStrideBytes >=
                sizeof(DecisionRecordCold),
            "cold decision-record stride is smaller than its object");
    require((kDecisionRecordColdArenaSlotStrideBytes %
             kTargetCacheLineBytes) == 0,
            "cold decision-record stride is not cache-line rounded");
    require(kDecisionRecordColdArenaSlotStrideBytes !=
                kMaxDecisionFramePayloadBytes,
            "cold arena stride was coupled to canonical frame bytes");
}

}  // namespace

int main() {
    try {
        test_target_cache_line_contract();
        test_current_object_and_slot_budgets();
        test_integrated_executable_policy_capacity_is_bounded();
        test_decision_records_have_separate_hot_and_cold_layouts();
        test_phase4_authorization_regions_are_bounded();
        std::cout << "all V2 layout tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 layout test failed: " << error.what() << '\n';
        return 1;
    }
}
