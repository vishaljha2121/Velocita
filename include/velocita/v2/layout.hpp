// Size budgets for the kernel's objects and messages.
#pragma once

#include <cstddef>

#ifndef VELOCITA_TARGET_CACHE_LINE_SIZE
#error "VELOCITA_TARGET_CACHE_LINE_SIZE must describe the deployment target"
#endif

namespace velocita::v2 {

inline constexpr std::size_t kTargetCacheLineBytes =
    VELOCITA_TARGET_CACHE_LINE_SIZE;

[[nodiscard]] constexpr bool is_power_of_two(const std::size_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] constexpr std::size_t
round_up_to_cache_line(const std::size_t bytes) noexcept {
    if (bytes == 0) return 0;
    return ((bytes - 1) / kTargetCacheLineBytes + 1) * kTargetCacheLineBytes;
}

inline constexpr std::size_t kDecisionFrameObjectBudgetBytes = 448;
inline constexpr std::size_t kPolicyArtifactObjectBudgetBytes = 1024;
inline constexpr std::size_t kRuleEvaluationObjectBudgetBytes = 16;
inline constexpr std::size_t kTradingStateObjectBudgetBytes = 80U * 1024U;
inline constexpr std::size_t kDecisionRecordHotBudgetBytes = 64;
inline constexpr std::size_t kDecisionRecordColdBudgetBytes = 640;
inline constexpr std::size_t kDecisionFramePayloadBudgetBytes = 448;
inline constexpr std::size_t kPolicyArtifactPayloadBudgetBytes = 576;
inline constexpr std::size_t kKernelCommandBudgetBytes = 1920;
inline constexpr std::size_t kKernelResultBudgetBytes = 128;
inline constexpr std::size_t kPriceSourceObjectBudgetBytes = 32;
inline constexpr std::size_t kQuantitySourceObjectBudgetBytes = 24;
inline constexpr std::size_t kActionSafetyCapsObjectBudgetBytes = 80;
inline constexpr std::size_t kActionTemplateObjectBudgetBytes = 160;
inline constexpr std::size_t kEconomicActionProposalObjectBudgetBytes = 128;
inline constexpr std::size_t kInstrumentExecutionProfileObjectBudgetBytes = 80;
inline constexpr std::size_t kVenueCapabilitiesObjectBudgetBytes = 32;
inline constexpr std::size_t kActionResolutionObjectBudgetBytes = 128;
inline constexpr std::size_t kFeatureDescriptorObjectBudgetBytes = 16;
inline constexpr std::size_t kFeatureSchemaArtifactObjectBudgetBytes = 576;
inline constexpr std::size_t kExecutablePolicyArtifactObjectBudgetBytes = 1408;
inline constexpr std::size_t kFeatureSchemaArtifactPayloadBudgetBytes = 384;
inline constexpr std::size_t kExecutablePolicyArtifactPayloadBudgetBytes = 1344;
inline constexpr std::size_t kRiskAccountObjectBudgetBytes = 192;
inline constexpr std::size_t kRiskReservationObjectBudgetBytes = 64;
inline constexpr std::size_t kClusterOrderObjectBudgetBytes = 64;
inline constexpr std::size_t kOutboxEffectRecordObjectBudgetBytes = 64;

static_assert(kTargetCacheLineBytes >= alignof(std::max_align_t));
static_assert(is_power_of_two(kTargetCacheLineBytes));

}
