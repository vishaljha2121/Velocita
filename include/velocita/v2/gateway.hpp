// Gateway planning: which effects to deliver, reconcile or report, and the pre-send checks.
#pragma once

#include "velocita/v2/kernel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace velocita::v2 {

enum class PreSendRefusal : std::uint8_t {
    none = 0,
    effect_not_found,
    effect_not_deliverable,
    stale_executor_generation,
    stale_kill_epoch,
    control_forbids_new_risk,
    authority_lease_expired,
    unsupported_operation,
    venue_cannot_represent_intent,
    quota_exhausted,
    payload_unreconstructable,
};

struct VenueCapabilityView {
    VenueId venue_id{};
    VenueCapabilitiesVersion capabilities_version{};
    bool supports_post_only{};
    bool supports_good_til_cancel{};

    constexpr auto operator<=>(const VenueCapabilityView&) const noexcept = default;
};

using Tick = std::uint64_t;

struct GatewayControlView {
    ExecutorGeneration executor_generation{};
    KillEpoch kill_epoch{};
    StrategyMode strategy_mode{StrategyMode::halted};
    Tick confirmed_at{};
    Tick lease_bound{};
    Tick now{};

    [[nodiscard]] constexpr bool lease_is_live() const noexcept {
        if (lease_bound == 0) return false;
        if (confirmed_at > now) return false;
        return now - confirmed_at <= lease_bound;
    }

    constexpr auto operator<=>(const GatewayControlView&) const noexcept = default;
};

struct AuthorityReading {
    bool authoritative{};
    ExecutorGeneration executor_generation{};
    KillEpoch kill_epoch{};
    StrategyMode strategy_mode{StrategyMode::halted};

    constexpr auto operator<=>(const AuthorityReading&) const noexcept = default;
};

[[nodiscard]] bool renew_authority(GatewayControlView& control,
                                   const AuthorityReading& reading,
                                   Tick now) noexcept;

struct SendQuotaView {
    std::uint32_t normal_lane_remaining{};
    std::uint32_t emergency_lane_remaining{};

    constexpr auto operator<=>(const SendQuotaView&) const noexcept = default;
};

[[nodiscard]] constexpr bool reduces_risk(const EffectOperation operation) noexcept {
    switch (operation) {
        case EffectOperation::cancel:
            return true;
        case EffectOperation::submit:
        case EffectOperation::replace:
            return false;
    }
    return false;
}

struct PreSendDecision {
    PreSendRefusal refusal{PreSendRefusal::none};
    std::optional<OrderEffect> effect{};
    Digest256 catalog_root{};
    InstrumentSpecVersion instrument_spec_version{};
    VenueCapabilitiesVersion venue_capabilities_version{};

    [[nodiscard]] constexpr bool permitted() const noexcept {
        return refusal == PreSendRefusal::none && effect.has_value();
    }
};

[[nodiscard]] PreSendDecision evaluate_delivery_candidate(
    const TradingState& state,
    EffectId effect_id,
    const GatewayControlView& control,
    const VenueCapabilityView& venue,
    const SendQuotaView& quota,
    const VerifiedExecutionCatalog* catalog = nullptr) noexcept;

struct DeliveryAttempt {
    EffectId effect_id{};
    Tick sent_at{};
    std::uint32_t attempts{};
    bool awaiting_response{};
    bool resend_permitted{};
    bool from_recovery{};

    constexpr auto operator<=>(const DeliveryAttempt&) const noexcept = default;
};

inline constexpr std::size_t kMaxDeliveryAttempts = kMaxOutboxEffects;

struct GatewayDeliveryState {
    bool started{false};
    bool recovering{false};
    std::size_t attempt_count{};
    std::array<DeliveryAttempt, kMaxDeliveryAttempts> attempts{};

    constexpr auto operator<=>(const GatewayDeliveryState&) const noexcept = default;
};

struct GatewayTimeouts {
    Tick response_deadline{10};
    std::uint32_t max_send_attempts{3};
    Tick query_interval{6};
    std::uint32_t max_query_attempts{4};
    Tick commit_deadline{4};
    Tick resting_confirm_interval{600};
};

struct DeliveryPlan {
    std::size_t send_count{};
    std::array<EffectId, kMaxDeliveryAttempts> to_send{};
    std::size_t ambiguous_count{};
    std::array<EffectId, kMaxDeliveryAttempts> now_ambiguous{};

    constexpr auto operator<=>(const DeliveryPlan&) const noexcept = default;
};

struct EvidenceObligation {
    CommandIdentity identity{};
    VenueObservation observation{};
    EffectId effect_id{};
    Tick first_recorded_at{};
    Tick last_proposed_at{};
    std::uint32_t attempts{};
    bool awaiting_commit{};
};

inline constexpr std::size_t kMaxEvidenceObligations = kMaxOutboxEffects * 4;

struct GatewayEvidenceState {
    std::size_t obligation_count{};
    std::array<EvidenceObligation, kMaxEvidenceObligations> obligations{};
};

struct EvidencePlan {
    std::size_t proposal_count{};
    std::array<CommandIdentity, kMaxEvidenceObligations> to_propose{};

    constexpr auto operator<=>(const EvidencePlan&) const noexcept = default;
};

[[nodiscard]] bool record_evidence_obligation(GatewayEvidenceState& evidence,
                                              const CommandIdentity& identity,
                                              const VenueObservation& observation,
                                              EffectId effect_id, Tick now) noexcept;

[[nodiscard]] EvidencePlan plan_evidence(const GatewayEvidenceState& evidence,
                                         const GatewayTimeouts& timeouts,
                                         Tick now) noexcept;

[[nodiscard]] bool record_proposal(GatewayEvidenceState& evidence,
                                   const CommandIdentity& identity,
                                   Tick now) noexcept;

[[nodiscard]] bool discharge_evidence(GatewayEvidenceState& evidence,
                                      const CommandIdentity& identity) noexcept;

[[nodiscard]] bool evidence_queue_saturated(
    const GatewayEvidenceState& evidence) noexcept;

[[nodiscard]] const EvidenceObligation* find_obligation(
    const GatewayEvidenceState& evidence, const CommandIdentity& identity) noexcept;

[[nodiscard]] DeliveryPlan plan_delivery(const TradingState& state,
                                         const GatewayDeliveryState& delivery,
                                         const GatewayEvidenceState& evidence,
                                         const GatewayControlView& control,
                                         const VenueCapabilityView& venue,
                                         const SendQuotaView& quota,
                                         const GatewayTimeouts& timeouts,
                                         Tick now,
                                         const VerifiedExecutionCatalog* catalog = nullptr) noexcept;

[[nodiscard]] bool record_send(GatewayDeliveryState& delivery, EffectId effect_id,
                               Tick now) noexcept;
[[nodiscard]] bool record_response(GatewayDeliveryState& delivery,
                                   EffectId effect_id) noexcept;
[[nodiscard]] bool record_unsent(GatewayDeliveryState& delivery,
                                 EffectId effect_id) noexcept;
[[nodiscard]] const DeliveryAttempt* find_attempt(
    const GatewayDeliveryState& delivery, EffectId effect_id) noexcept;

std::size_t begin_gateway_recovery(GatewayDeliveryState& delivery,
                                   const TradingState& state, Tick now) noexcept;

[[nodiscard]] bool recovery_complete(const GatewayDeliveryState& delivery,
                                     const TradingState& state) noexcept;

std::size_t reclaim_delivery_attempts(GatewayDeliveryState& delivery,
                                      const TradingState& state) noexcept;

enum class VenueQueryStatus : std::uint8_t {
    known = 1,
    not_found,
    unavailable,
};

struct VenueQueryView {
    VenueQueryStatus status{VenueQueryStatus::unavailable};
    VenueSessionId venue_session_id{};
    VenueOrderId venue_order_id{};
    QuantityLots filled_quantity{};
    QuantityLots remaining_quantity{};
    bool cancelled{};
    bool window_complete{};
    bool ordered_after_relevant_sends{};

    constexpr auto operator<=>(const VenueQueryView&) const noexcept = default;
};

enum class QueryDisposition : std::uint8_t {
    submit = 1,
    incomplete,
    unavailable,
    exhausted,
};

enum class ReconciliationNeed : std::uint8_t {
    none = 0,
    submission_unknown,
    cancel_unconfirmed,
    delivery_exhausted,
    unconfirmed_resting,
};

struct ReconciliationTarget {
    ClusterOrderId cluster_order_id{};
    EffectId effect_id{};
    ReconciliationNeed need{ReconciliationNeed::none};

    constexpr auto operator<=>(const ReconciliationTarget&) const noexcept = default;
};

struct ReconciliationAttempt {
    ClusterOrderId cluster_order_id{};
    Tick last_query_at{};
    std::uint32_t attempts{};
    bool awaiting_answer{};
    std::uint64_t evidence_at_query{};

    constexpr auto operator<=>(const ReconciliationAttempt&) const noexcept = default;
};

inline constexpr std::size_t kMaxReconciliationAttempts = kMaxClusterOrders;

struct GatewayReconciliationState {
    std::size_t attempt_count{};
    std::array<ReconciliationAttempt, kMaxReconciliationAttempts> attempts{};

    constexpr auto operator<=>(const GatewayReconciliationState&) const noexcept =
        default;
};

struct ReconciliationPlan {
    std::size_t query_count{};
    std::array<ReconciliationTarget, kMaxReconciliationAttempts> to_query{};

    constexpr auto operator<=>(const ReconciliationPlan&) const noexcept = default;
};

[[nodiscard]] ReconciliationPlan plan_reconciliation(
    const TradingState& state,
    const GatewayReconciliationState& reconciliation,
    const GatewayDeliveryState& delivery,
    const GatewayControlView& control,
    const VenueCapabilityView& venue,
    const GatewayTimeouts& timeouts,
    Tick now) noexcept;

[[nodiscard]] std::uint64_t accepted_evidence_watermark(
    const TradingState& state, ClusterOrderId cluster_order_id) noexcept;

[[nodiscard]] QueryDisposition classify_query(
    const VenueQueryView& answer,
    const ReconciliationAttempt* attempt,
    std::uint64_t evidence_now,
    const GatewayTimeouts& timeouts) noexcept;

[[nodiscard]] bool record_query(GatewayReconciliationState& reconciliation,
                                ClusterOrderId cluster_order_id, Tick now,
                                std::uint64_t evidence_at_query) noexcept;
[[nodiscard]] bool record_answer(GatewayReconciliationState& reconciliation,
                                 ClusterOrderId cluster_order_id) noexcept;
[[nodiscard]] const ReconciliationAttempt* find_reconciliation(
    const GatewayReconciliationState& reconciliation,
    ClusterOrderId cluster_order_id) noexcept;

std::size_t reclaim_reconciliation_attempts(
    GatewayReconciliationState& reconciliation, const TradingState& state) noexcept;

static_assert(std::is_trivially_copyable_v<GatewayDeliveryState>);
static_assert(std::is_trivially_copyable_v<DeliveryPlan>);
static_assert(std::is_trivially_copyable_v<VenueCapabilityView>);
static_assert(std::is_trivially_copyable_v<GatewayControlView>);
static_assert(std::is_trivially_copyable_v<SendQuotaView>);
static_assert(std::is_trivially_copyable_v<GatewayReconciliationState>);
static_assert(std::is_trivially_copyable_v<ReconciliationPlan>);
static_assert(std::is_trivially_copyable_v<VenueQueryView>);

}
