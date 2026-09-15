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
    // Expiry is generation/kill-epoch staleness; effects have no clock deadline.
    stale_executor_generation,
    stale_kill_epoch,
    control_forbids_new_risk,
    unsupported_operation,
    venue_cannot_represent_intent,
    quota_exhausted,
    payload_unreconstructable,
};

// Venue lowering must preserve the exact economic intent.
struct VenueCapabilityView {
    VenueId venue_id{};
    VenueCapabilitiesVersion capabilities_version{};
    bool supports_post_only{};
    bool supports_good_til_cancel{};

    constexpr auto operator<=>(const VenueCapabilityView&) const noexcept = default;
};

// Disposable control cache; evaluate_pre_send verifies it against replicated state.
struct GatewayControlView {
    ExecutorGeneration executor_generation{};
    KillEpoch kill_epoch{};
    StrategyMode strategy_mode{StrategyMode::halted};

    constexpr auto operator<=>(const GatewayControlView&) const noexcept = default;
};

struct SendQuotaView {
    std::uint32_t normal_lane_remaining{};

    constexpr auto operator<=>(const SendQuotaView&) const noexcept = default;
};

// Refusal carries no payload; permission carries the reconstructed canonical effect.
struct PreSendDecision {
    PreSendRefusal refusal{PreSendRefusal::none};
    std::optional<OrderEffect> effect{};

    [[nodiscard]] constexpr bool permitted() const noexcept {
        return refusal == PreSendRefusal::none && effect.has_value();
    }
};

// Pure, bounded, allocation-free. Refusal precedence: identity/deliverability,
// canonical payload, current authority, venue capability, quota.
[[nodiscard]] PreSendDecision evaluate_pre_send(
    const TradingState& state,
    EffectId effect_id,
    const GatewayControlView& control,
    const VenueCapabilityView& venue,
    const SendQuotaView& quota) noexcept;

using Tick = std::uint64_t;

// Gateway-local and non-durable; restart rebuilds through reconciliation.
struct DeliveryAttempt {
    EffectId effect_id{};
    Tick sent_at{};
    std::uint32_t attempts{};
    // An unanswered send is ambiguous, never proof of acceptance or rejection.
    bool awaiting_response{};
    // Set only with proof the send never reached the venue. Defaults false to
    // prevent an unproven submit retry from creating a duplicate order.
    bool resend_permitted{};
    // Seeded at startup; this process cannot know whether the prior gateway sent it.
    bool from_recovery{};

    constexpr auto operator<=>(const DeliveryAttempt&) const noexcept = default;
};

inline constexpr std::size_t kMaxDeliveryAttempts = kMaxOutboxEffects;

struct GatewayDeliveryState {
    // No delivery until begin_gateway_recovery scans replicated state.
    bool started{false};
    // Blocks new risk during recovery; cancels remain eligible.
    bool recovering{false};
    std::size_t attempt_count{};
    std::array<DeliveryAttempt, kMaxDeliveryAttempts> attempts{};

    constexpr auto operator<=>(const GatewayDeliveryState&) const noexcept = default;
};

struct GatewayTimeouts {
    // Timeout produces effect_unknown, never a rejection.
    Tick response_deadline{10};
    // Exhausted sends remain unsettled and require reconciliation.
    std::uint32_t max_send_attempts{3};
    Tick query_interval{6};
    // Exhausted queries leave the order unresolved and its slot blocked.
    std::uint32_t max_query_attempts{4};
    // Re-propose overdue evidence under the same command identity.
    Tick commit_deadline{4};
};

struct DeliveryPlan {
    std::size_t send_count{};
    std::array<EffectId, kMaxDeliveryAttempts> to_send{};
    // The shell reports these as effect_unknown; submit retries require reconciliation.
    std::size_t ambiguous_count{};
    std::array<EffectId, kMaxDeliveryAttempts> now_ambiguous{};

    constexpr auto operator<=>(const DeliveryPlan&) const noexcept = default;
};

struct EvidenceObligation {
    // Preserve this identity and observation across retries for canonical deduplication.
    CommandIdentity identity{};
    // Retained by value; retry bytes must not depend on later state changes.
    VenueObservation observation{};
    EffectId effect_id{};
    Tick first_recorded_at{};
    Tick last_proposed_at{};
    std::uint32_t attempts{};
    bool awaiting_commit{};
};

// Acknowledgements and partial fills can create multiple obligations per effect.
inline constexpr std::size_t kMaxEvidenceObligations = kMaxOutboxEffects * 4;

// Gateway-local; restart must recover uncommitted evidence through reconciliation.
struct GatewayEvidenceState {
    std::size_t obligation_count{};
    std::array<EvidenceObligation, kMaxEvidenceObligations> obligations{};
};

struct EvidencePlan {
    std::size_t proposal_count{};
    std::array<CommandIdentity, kMaxEvidenceObligations> to_propose{};

    constexpr auto operator<=>(const EvidencePlan&) const noexcept = default;
};

// Pair with record_response: venue receipt ends transport, not the commit obligation.
// False means full; the caller must retain the observation and apply backpressure.
[[nodiscard]] bool record_evidence_obligation(GatewayEvidenceState& evidence,
                                              const CommandIdentity& identity,
                                              const VenueObservation& observation,
                                              EffectId effect_id, Tick now) noexcept;

[[nodiscard]] EvidencePlan plan_evidence(const GatewayEvidenceState& evidence,
                                         const GatewayTimeouts& timeouts,
                                         Tick now) noexcept;

// Proposal alone does not discharge the evidence obligation.
[[nodiscard]] bool record_proposal(GatewayEvidenceState& evidence,
                                   const CommandIdentity& identity,
                                   Tick now) noexcept;

// Discharge only after observing the committed result, including a replayed result.
[[nodiscard]] bool discharge_evidence(GatewayEvidenceState& evidence,
                                      const CommandIdentity& identity) noexcept;

[[nodiscard]] bool evidence_queue_saturated(
    const GatewayEvidenceState& evidence) noexcept;

[[nodiscard]] const EvidenceObligation* find_obligation(
    const GatewayEvidenceState& evidence, const CommandIdentity& identity) noexcept;

// Pure; time is injected. Decide ambiguity before sends to prevent same-tick retries.
// A full evidence queue blocks work that would generate further evidence.
[[nodiscard]] DeliveryPlan plan_delivery(const TradingState& state,
                                         const GatewayDeliveryState& delivery,
                                         const GatewayEvidenceState& evidence,
                                         const GatewayControlView& control,
                                         const VenueCapabilityView& venue,
                                         const SendQuotaView& quota,
                                         const GatewayTimeouts& timeouts,
                                         Tick now) noexcept;

[[nodiscard]] bool record_send(GatewayDeliveryState& delivery, EffectId effect_id,
                               Tick now) noexcept;
// Closes transport only; an answered submit still requires reconciliation before resend.
[[nodiscard]] bool record_response(GatewayDeliveryState& delivery,
                                   EffectId effect_id) noexcept;
// Only for proven-unsent outcomes (rate_limited or pre_send_failure),
// never ambiguous, accepted, or business-rejected outcomes.
[[nodiscard]] bool record_unsent(GatewayDeliveryState& delivery,
                                 EffectId effect_id) noexcept;
[[nodiscard]] const DeliveryAttempt* find_attempt(
    const GatewayDeliveryState& delivery, EffectId effect_id) noexcept;

// Run before first delivery. Treat existing unsettled effects as possibly sent;
// returns the number marked suspect for reconciliation.
std::size_t begin_gateway_recovery(GatewayDeliveryState& delivery,
                                   const TradingState& state, Tick now) noexcept;

// The shell may clear recovery once suspects settle or pass to reconciliation.
// Unresolved suspects retain their slot and submit-resend restrictions.
[[nodiscard]] bool recovery_complete(const GatewayDeliveryState& delivery,
                                     const TradingState& state) noexcept;

// Drop records only for swept effects, whose groups were fully resolved.
// Mutates/compacts the arena; reacquire borrowed attempt pointers afterward.
std::size_t reclaim_delivery_attempts(GatewayDeliveryState& delivery,
                                      const TradingState& state) noexcept;

enum class VenueQueryStatus : std::uint8_t {
    known = 1,
    // An absence answer still requires complete, ordered evidence before submission.
    not_found,
    // No answer; nothing may be concluded about the order.
    unavailable,
};

struct VenueQueryView {
    VenueQueryStatus status{VenueQueryStatus::unavailable};
    VenueSessionId venue_session_id{};
    VenueOrderId venue_order_id{};
    QuantityLots filled_quantity{};
    QuantityLots remaining_quantity{};
    bool cancelled{};
    // Window coverage alone does not prove ordering after sends or concurrent fills.
    bool window_complete{};
    bool ordered_after_relevant_sends{};

    constexpr auto operator<=>(const VenueQueryView&) const noexcept = default;
};

// Gateway classifies answer trustworthiness; the reducer owns economic adjudication.
enum class QueryDisposition : std::uint8_t {
    submit = 1,
    incomplete,
    unavailable,
    // Stops querying; the order remains unresolved and its slot blocked.
    exhausted,
};

enum class ReconciliationNeed : std::uint8_t {
    none = 0,
    submission_unknown,
    cancel_unconfirmed,
    delivery_exhausted,
};

struct ReconciliationTarget {
    ClusterOrderId cluster_order_id{};
    EffectId effect_id{};
    ReconciliationNeed need{ReconciliationNeed::none};

    constexpr auto operator<=>(const ReconciliationTarget&) const noexcept = default;
};

// Gateway-local; restart rebuilds by querying again.
struct ReconciliationAttempt {
    ClusterOrderId cluster_order_id{};
    Tick last_query_at{};
    std::uint32_t attempts{};
    bool awaiting_answer{};
    // Accepted-evidence watermark at issue time. A changed watermark at answer
    // arrival makes the snapshot incomparable with current cluster evidence.
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

// Pure; time is injected. Generation fences reconciliation; kill epoch and
// strategy mode do not, because querying existing exposure creates no new risk.
[[nodiscard]] ReconciliationPlan plan_reconciliation(
    const TradingState& state,
    const GatewayReconciliationState& reconciliation,
    const GatewayDeliveryState& delivery,
    const GatewayControlView& control,
    const VenueCapabilityView& venue,
    const GatewayTimeouts& timeouts,
    Tick now) noexcept;

// Last state version that changed this order's evidence. Monotonic across
// receipt sweeps, avoiding count-based ABA.
[[nodiscard]] std::uint64_t accepted_evidence_watermark(
    const TradingState& state, ClusterOrderId cluster_order_id) noexcept;

// A changed evidence watermark makes the answer incomplete and requires a new query.
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

// Compacts the arena; reacquire borrowed reconciliation pointers afterward.
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

}  // namespace velocita::v2
