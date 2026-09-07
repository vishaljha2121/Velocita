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

// Object ceilings are review gates, not serialized sizes. Raising one requires
// an explicit working-set, arena-capacity, and snapshot-impact review.
inline constexpr std::size_t kDecisionFrameObjectBudgetBytes = 448;
inline constexpr std::size_t kPolicyArtifactObjectBudgetBytes = 1024;
inline constexpr std::size_t kRuleEvaluationObjectBudgetBytes = 16;
// Phase 3D adds one active and one staged executable-policy bundle plus exact
// proposals in cold decision records. The 80-KiB ceiling remains provisional
// until the deferred NUC gate confirms the same capacity on both targets.
inline constexpr std::size_t kTradingStateObjectBudgetBytes = 80U * 1024U;
inline constexpr std::size_t kDecisionRecordHotBudgetBytes = 64;
inline constexpr std::size_t kDecisionRecordColdBudgetBytes = 640;
inline constexpr std::size_t kDecisionFramePayloadBudgetBytes = 448;
inline constexpr std::size_t kPolicyArtifactPayloadBudgetBytes = 576;
// An atomic StageExecutablePolicy command carries every immutable fact needed
// to resolve an action identically on every replica.
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
// Candidate wire ceilings for Phase 3D hardware validation. These are distinct
// from object budgets because canonical payloads omit padding and unused slots.
inline constexpr std::size_t kFeatureSchemaArtifactPayloadBudgetBytes = 384;
inline constexpr std::size_t kExecutablePolicyArtifactPayloadBudgetBytes = 1344;

static_assert(kTargetCacheLineBytes >= alignof(std::max_align_t));
static_assert(is_power_of_two(kTargetCacheLineBytes));

}  // namespace velocita::v2
