// The deterministic trading state machine: commands, state and apply.
#pragma once

#include "velocita/v2/action_resolver.hpp"
#include "velocita/v2/artifact_digest.hpp"
#include "velocita/v2/contracts.hpp"
#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/swap_codec.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>

namespace velocita::v2 {

inline constexpr std::size_t kMaxPolicies = 8;
inline constexpr std::size_t kMaxSources = 16;
inline constexpr std::size_t kMaxSourceEventReceipts = 128;
inline constexpr std::size_t kMaxRequestReceipts = 256;
inline constexpr std::size_t kMaxDecisionRecords = 16;
inline constexpr std::size_t kMaxRiskAccounts = 4;
inline constexpr std::size_t kMaxRiskReservations = 16;
inline constexpr std::size_t kMaxClusterOrders = 16;
inline constexpr std::size_t kMaxOutboxEffects = 16;
inline constexpr std::size_t kMaxVenueEventReceipts = 64;
inline constexpr std::size_t kMaxCallerCursors = 8;
static_assert(kMaxPolicies > 0);
static_assert(kMaxSources > 0);
static_assert(kMaxSourceEventReceipts > kMaxSources);
static_assert(kMaxRequestReceipts > kMaxSourceEventReceipts);
static_assert(kMaxRequestReceipts > kMaxPolicies);
static_assert(kMaxDecisionRecords > 0);
static_assert(kMaxRiskAccounts > 0);
static_assert(kMaxRiskReservations == kMaxClusterOrders);
static_assert(kMaxClusterOrders == kMaxOutboxEffects);

enum class EvaluationMode : std::uint8_t {
    authoritative = 1,
    shadow = 2,
};

enum class DecisionCode : std::uint8_t {
    none = 0,
    action = 1,
    no_rule_matched = 2,
    invalid_frame = 3,
    policy_not_found = 4,
    policy_not_active = 5,
    account_scope_mismatch = 6,
    instrument_scope_mismatch = 7,
    feature_schema_mismatch = 8,
    source_not_admitted = 9,
    source_evidence_mismatch = 10,
    policy_not_yet_valid = 11,
    policy_expired = 12,
    market_data_not_ready = 13,
    feature_unavailable = 14,
    control_halted = 15,
    control_suppressed = 16,
    shadow_recorded = 17,
    executable_policy_not_active = 18,
    action_resolution_rejected = 19,
};

enum class PolicyLifecycle : std::uint8_t {
    none = 0,
    proposed = 1,
    validated = 2,
    active = 3,
    superseded = 4,
};

enum class ApplyCode : std::uint8_t {
    applied = 0,
    already_applied,
    invalid_command,
    invalid_policy_contract,
    duplicate_conflict,
    policy_not_found,
    immutable_policy_conflict,
    non_monotonic_policy_version,
    invalid_policy_transition,
    active_policy_scope_conflict,
    stale_control_generation,
    non_monotonic_kill_epoch,
    non_monotonic_executor_generation,
    source_event_conflict,
    source_generation_gap,
    source_generation_regression,
    source_sequence_gap,
    source_sequence_regression,
    source_capacity_exceeded,
    source_event_capacity_exceeded,
    decision_capacity_exceeded,
    policy_capacity_exceeded,
    receipt_capacity_exceeded,
    state_version_exhausted,
    decision_id_conflict,
    invalid_feature_schema_contract,
    invalid_executable_policy_contract,
    artifact_digest_mismatch,
    executable_policy_binding_failed,
    invalid_instrument_profile_contract,
    invalid_venue_capabilities_contract,
    executable_policy_not_staged,
    executable_policy_identity_conflict,
    decision_not_authorizable,
    risk_account_not_found,
    stale_risk_state,
    risk_arithmetic_overflow,
    insufficient_risk_capacity,
    reservation_capacity_exceeded,
    order_capacity_exceeded,
    outbox_capacity_exceeded,
    derived_identity_collision,
    venue_observation_not_applicable,
    fill_identity_conflict,
    fill_capacity_exceeded,
    fill_quantity_conflict,
    position_arithmetic_overflow,
    order_not_found,
    order_not_cancellable,
    venue_event_identity_conflict,
    venue_event_capacity_exceeded,
    replacement_not_authorizable,
    order_not_replaceable,
    replacement_not_supported,
    order_slot_occupied,
    risk_account_capacity_exceeded,
    risk_version_exhausted,
    risk_reconfiguration_blocked,
    request_expired,
    venue_event_expired,
    unauthorized_control,
    reconciliation_required,
    catalog_not_installed,
    catalog_installation_blocked,
    immutable_catalog_conflict,
    catalog_row_not_found,
    catalog_binding_mismatch,
    position_limit_exceeded,
    fill_price_conflict,
    fill_fee_conflict,
    loss_limit_reached,
    unauthorized_command,
    caller_role_conflict,
    order_not_in_conflict,
    order_not_reducing,
};

enum class CallerRole : std::uint8_t {
    none = 0,
    strategy = 1,
    gateway = 2,
};

struct CallerCursor {
    CallerId caller_id{};
    RequestSequence expired_below{};
    RequestSequence gateway_results_acknowledged_through{};
    CallerRole role{CallerRole::none};

    constexpr auto operator<=>(const CallerCursor&) const noexcept = default;
};

struct CommandIdentity {
    CallerId caller_id{};
    RequestSequence request_sequence{};
    Digest256 payload_digest{};

    constexpr auto operator<=>(const CommandIdentity&) const noexcept = default;
};

struct PublishPolicy {
    PolicyArtifact artifact{};
};

struct ValidatePolicy {
    PolicyId policy_id{};
    PolicyVersion policy_version{};
};

struct ActivatePolicy {
    PolicyId policy_id{};
    PolicyVersion policy_version{};
};

struct SupersedePolicy {
    PolicyId policy_id{};
    PolicyVersion policy_version{};
};

inline constexpr std::uint64_t kMaxOperatorCallerId = 15;

[[nodiscard]] constexpr bool is_operator_caller(const CallerId caller) noexcept {
    return caller.value() != 0 && caller.value() <= kMaxOperatorCallerId;
}

struct SetStrategyMode {
    StrategyMode mode{StrategyMode::halted};
    KillEpoch expected_kill_epoch{};
    ExecutorGeneration expected_executor_generation{};
};

struct AdvanceKillEpoch {
    KillEpoch new_kill_epoch{};
};

struct SetExecutorGeneration {
    ExecutorGeneration new_generation{};
};

enum class MassCancelScope : std::uint8_t {
    all = 1,
    account = 2,
    account_instrument = 3,
};

struct RequestMassCancel {
    MassCancelScope scope{MassCancelScope::all};
    AccountId account_id{};
    InstrumentId instrument_id{};
    KillEpoch expected_kill_epoch{};
    ExecutorGeneration expected_executor_generation{};
};

struct RecordSourceEvent {
    SourceId source_id{};
    SourceGeneration source_generation{};
    SourceSequence source_sequence{};
    Digest256 event_digest{};
};

struct EvaluateDecisionFrame {
    DecisionId decision_id{};
    EvaluationMode evaluation_mode{EvaluationMode::authoritative};
    DecisionFrame frame{};
};

struct StageExecutablePolicy {
    FeatureSchemaArtifact feature_schema{};
    ExecutablePolicyArtifact executable_policy{};
    InstrumentExecutionProfile instrument{};
    VenueCapabilities venue{};
};

struct ActivateExecutablePolicy {
    PolicyId policy_id{};
    PolicyVersion policy_version{};
    Digest256 executable_policy_digest{};
};

struct AuthorizeDecision {
    DecisionId decision_id{};
};

struct ObserveFill {
    VenueObservation observation{};
};

struct RequestCancel {
    ClusterOrderId cluster_order_id{};
};

struct ObserveOrderEvent {
    VenueObservation observation{};
};

struct RequestReplace {
    ClusterOrderId cluster_order_id{};
    DecisionId replacement_decision_id{};
};

struct ConfigureRiskLimits {
    AccountId account_id{};
    InstrumentId instrument_id{};
    RiskPolicyVersion risk_policy_version{};
    RiskVersion expected_risk_version{};
    KillEpoch expected_kill_epoch{};
    ExecutorGeneration expected_executor_generation{};
    QuantityLots max_order_quantity{};
    QuantityLots max_reserved_quantity{};
    MoneyMicros max_order_gross_notional{};
    MoneyMicros max_reserved_exposure{};
    std::uint32_t initial_margin_ppm{};
    std::uint32_t maximum_fee_ppm{};
    QuantityLots max_position_quantity{};
    bool allow_short{};
    std::uint32_t reason_code{};
    MoneyMicros max_window_loss{};
    bool reset_loss_window{};
};

struct AcknowledgeGatewayResults {
    RequestSequence acknowledged_through{};
};

struct AssignCallerRole {
    CallerId caller_id{};
    CallerRole role{CallerRole::none};
};

struct ResolveOrderConflict {
    ClusterOrderId cluster_order_id{};
    KillEpoch expected_kill_epoch{};
    ExecutorGeneration expected_executor_generation{};
    QuantityLots unbooked_quantity{};
    PriceTicks unbooked_price{};
    FeeMicros unbooked_fee{};
    Digest256 evidence_digest{};
};

struct InstallExecutionCatalog {
    Digest256 catalog_root{};
    KillEpoch expected_kill_epoch{};
    ExecutorGeneration expected_executor_generation{};
};

[[nodiscard]] constexpr bool valid_risk_limits(const ConfigureRiskLimits& value) noexcept {
    return value.account_id.value() != 0 && value.instrument_id.value() != 0 &&
           value.risk_policy_version.value() != 0 && value.max_order_quantity.atoms() > 0 &&
           value.max_reserved_quantity >= value.max_order_quantity &&
           value.max_order_gross_notional.atoms() > 0 && value.max_reserved_exposure.atoms() > 0 &&
           value.initial_margin_ppm > 0 && value.initial_margin_ppm <= 1'000'000 &&
           value.maximum_fee_ppm <= 1'000'000 &&
           value.max_position_quantity.atoms() >= 0 && value.reason_code != 0 &&
           value.max_window_loss.atoms() >= 0;
}

struct ApplySpotSwap {
    swap::Record record{};
};

using CommandBody = std::variant<PublishPolicy,
                                 ValidatePolicy,
                                 ActivatePolicy,
                                 SupersedePolicy,
                                 SetStrategyMode,
                                 AdvanceKillEpoch,
                                 SetExecutorGeneration,
                                 RecordSourceEvent,
                                 EvaluateDecisionFrame,
                                 StageExecutablePolicy,
                                 ActivateExecutablePolicy,
                                 AuthorizeDecision,
                                 ObserveFill,
                                 RequestCancel,
                                 RequestMassCancel,
                                 ObserveOrderEvent,
                                 RequestReplace,
                                 ConfigureRiskLimits,
                                 AcknowledgeGatewayResults,
                                 InstallExecutionCatalog,
                                 AssignCallerRole,
                                 ResolveOrderConflict,
                                 ApplySpotSwap>;

struct Command {
    CommandIdentity identity{};
    CommandBody body{};
};

enum class ApplyResultDetailKind : std::uint8_t {
    none = 0,
    source = 1,
    authorization = 2,
    fill = 3,
    venue_event = 4,
    risk_configuration = 5,
    mass_cancel = 6,
    risk_state = 7,
    source_state = 8,
};

struct ApplyResultDetail {
    std::array<std::uint64_t, 3> words{};

    [[nodiscard]] static constexpr ApplyResultDetail source(
        const SourceId id,
        const SourceGeneration generation,
        const SourceSequence sequence) noexcept {
        return {{{id.value(), generation.value(), sequence.value()}}};
    }

    [[nodiscard]] static constexpr ApplyResultDetail authorization(
        const ClusterOrderId order_id,
        const EffectId effect_id,
        const MoneyMicros reserved_exposure) noexcept {
        return {{{order_id.value(), effect_id.value(),
                  static_cast<std::uint64_t>(reserved_exposure.atoms())}}};
    }

    [[nodiscard]] static constexpr ApplyResultDetail fill(
        const VenueId venue_id,
        const ExecutionId execution_id,
        const QuantityLots filled_quantity) noexcept {
        return {{{venue_id.value(), execution_id.value(),
                  static_cast<std::uint64_t>(filled_quantity.atoms())}}};
    }

    [[nodiscard]] static constexpr ApplyResultDetail mass_cancel(
        const std::uint64_t in_scope,
        const std::uint64_t cancelled,
        const std::uint64_t skipped) noexcept {
        return {{{in_scope, cancelled, skipped}}};
    }

    [[nodiscard]] static constexpr ApplyResultDetail risk_state(
        const RiskVersion risk_version,
        const PositionVersion position_version,
        const QuantityLots position_quantity) noexcept {
        return {{{risk_version.value(), position_version.value(),
                  static_cast<std::uint64_t>(position_quantity.atoms())}}};
    }

    [[nodiscard]] static constexpr ApplyResultDetail source_state(
        const SourceId id,
        const SourceGeneration cursor_generation,
        const SourceSequence cursor_sequence) noexcept {
        return {{{id.value(), cursor_generation.value(), cursor_sequence.value()}}};
    }

    [[nodiscard]] static constexpr ApplyResultDetail venue_event(
        const VenueId venue_id,
        const VenueSessionId session_id,
        const VenueSequence sequence) noexcept {
        return {{{venue_id.value(), session_id.value(), sequence.value()}}};
    }

    constexpr auto operator<=>(const ApplyResultDetail&) const noexcept = default;
};

struct ApplyResult {
    ApplyCode code{ApplyCode::invalid_command};
    ContractError contract_error{ContractError::none};
    std::array<std::uint64_t, 2> metadata_words{};
    PolicyLifecycle policy_lifecycle{PolicyLifecycle::none};
    std::uint64_t state_version{};
    StrategyMode strategy_mode{StrategyMode::halted};
    KillEpoch kill_epoch{};
    ExecutorGeneration executor_generation{};
    ApplyResultDetail detail{};
    DecisionId decision_id{};
    DecisionCode decision_code{DecisionCode::none};
    Directive proposed_directive{Directive::no_action};
    Directive effective_directive{Directive::no_action};
    EvaluationMode evaluation_mode{EvaluationMode::authoritative};
    std::uint16_t matched_rule_index{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t rules_visited{};
    ActionResolutionCode action_resolution_code{ActionResolutionCode::none};
    ApplyResultDetailKind detail_kind{ApplyResultDetailKind::none};

    [[nodiscard]] constexpr PolicyId policy_id() const noexcept {
        return detail_kind == ApplyResultDetailKind::fill ||
                       detail_kind == ApplyResultDetailKind::venue_event
                   ? PolicyId{}
                   : PolicyId{metadata_words[0]};
    }
    [[nodiscard]] constexpr PolicyVersion policy_version() const noexcept {
        return detail_kind == ApplyResultDetailKind::fill ||
                       detail_kind == ApplyResultDetailKind::venue_event
                   ? PolicyVersion{}
                   : PolicyVersion{metadata_words[1]};
    }
    [[nodiscard]] constexpr VenueSessionId fill_session_id() const noexcept {
        return detail_kind == ApplyResultDetailKind::fill ? VenueSessionId{metadata_words[0]} : VenueSessionId{};
    }
    [[nodiscard]] constexpr VenueSequence fill_sequence() const noexcept {
        return detail_kind == ApplyResultDetailKind::fill ? VenueSequence{metadata_words[1]} : VenueSequence{};
    }

    [[nodiscard]] constexpr SourceId source_id() const noexcept {
        return detail_kind == ApplyResultDetailKind::source ? SourceId{detail.words[0]}
                                                            : SourceId{};
    }
    [[nodiscard]] constexpr SourceGeneration source_generation() const noexcept {
        return detail_kind == ApplyResultDetailKind::source
                   ? SourceGeneration{detail.words[1]}
                   : SourceGeneration{};
    }
    [[nodiscard]] constexpr SourceSequence source_sequence() const noexcept {
        return detail_kind == ApplyResultDetailKind::source
                   ? SourceSequence{detail.words[2]}
                   : SourceSequence{};
    }
    [[nodiscard]] constexpr ClusterOrderId cluster_order_id() const noexcept {
        return detail_kind == ApplyResultDetailKind::authorization
                   ? ClusterOrderId{detail.words[0]}
                   : ClusterOrderId{};
    }
    [[nodiscard]] constexpr EffectId effect_id() const noexcept {
        return detail_kind == ApplyResultDetailKind::authorization
                   ? EffectId{detail.words[1]}
                   : EffectId{};
    }
    [[nodiscard]] constexpr MoneyMicros reserved_exposure() const noexcept {
        return detail_kind == ApplyResultDetailKind::authorization
                   ? MoneyMicros::from_atoms(static_cast<std::int64_t>(detail.words[2]))
                   : MoneyMicros{};
    }
    [[nodiscard]] constexpr VenueId fill_venue_id() const noexcept {
        return detail_kind == ApplyResultDetailKind::fill ? VenueId{detail.words[0]}
                                                          : VenueId{};
    }
    [[nodiscard]] constexpr ExecutionId execution_id() const noexcept {
        return detail_kind == ApplyResultDetailKind::fill
                   ? ExecutionId{detail.words[1]}
                   : ExecutionId{};
    }
    [[nodiscard]] constexpr QuantityLots filled_quantity() const noexcept {
        return detail_kind == ApplyResultDetailKind::fill
                   ? QuantityLots::from_atoms(
                         static_cast<std::int64_t>(detail.words[2]))
                   : QuantityLots{};
    }
    [[nodiscard]] constexpr VenueId event_venue_id() const noexcept {
        return detail_kind == ApplyResultDetailKind::venue_event
                   ? VenueId{detail.words[0]}
                   : VenueId{};
    }
    [[nodiscard]] constexpr VenueSessionId event_session_id() const noexcept {
        return detail_kind == ApplyResultDetailKind::venue_event
                   ? VenueSessionId{detail.words[1]}
                   : VenueSessionId{};
    }
    [[nodiscard]] constexpr VenueSequence event_sequence() const noexcept {
        return detail_kind == ApplyResultDetailKind::venue_event
                   ? VenueSequence{detail.words[2]}
                   : VenueSequence{};
    }
    [[nodiscard]] constexpr std::uint64_t mass_in_scope() const noexcept {
        return detail_kind == ApplyResultDetailKind::mass_cancel ? detail.words[0] : 0;
    }
    [[nodiscard]] constexpr std::uint64_t mass_cancelled() const noexcept {
        return detail_kind == ApplyResultDetailKind::mass_cancel ? detail.words[1] : 0;
    }
    [[nodiscard]] constexpr std::uint64_t mass_skipped() const noexcept {
        return detail_kind == ApplyResultDetailKind::mass_cancel ? detail.words[2] : 0;
    }

    [[nodiscard]] constexpr RiskVersion current_risk_version() const noexcept {
        return detail_kind == ApplyResultDetailKind::risk_state
                   ? RiskVersion{detail.words[0]}
                   : RiskVersion{};
    }
    [[nodiscard]] constexpr PositionVersion current_position_version() const noexcept {
        return detail_kind == ApplyResultDetailKind::risk_state
                   ? PositionVersion{detail.words[1]}
                   : PositionVersion{};
    }
    [[nodiscard]] constexpr QuantityLots current_position_quantity() const noexcept {
        return detail_kind == ApplyResultDetailKind::risk_state
                   ? QuantityLots::from_atoms(static_cast<std::int64_t>(detail.words[2]))
                   : QuantityLots{};
    }

    [[nodiscard]] constexpr SourceId source_cursor_id() const noexcept {
        return detail_kind == ApplyResultDetailKind::source_state ? SourceId{detail.words[0]} : SourceId{};
    }
    [[nodiscard]] constexpr SourceGeneration source_cursor_generation() const noexcept {
        return detail_kind == ApplyResultDetailKind::source_state ? SourceGeneration{detail.words[1]}
                                                                  : SourceGeneration{};
    }
    [[nodiscard]] constexpr SourceSequence source_cursor_sequence() const noexcept {
        return detail_kind == ApplyResultDetailKind::source_state ? SourceSequence{detail.words[2]}
                                                                  : SourceSequence{};
    }

    [[nodiscard]] constexpr EffectId event_effect_id() const noexcept {
        return detail_kind == ApplyResultDetailKind::venue_event
                   ? EffectId{metadata_words[0]}
                   : EffectId{};
    }
    [[nodiscard]] constexpr ObservationOrigin event_origin() const noexcept {
        return detail_kind == ApplyResultDetailKind::venue_event &&
                       metadata_words[1] ==
                           static_cast<std::uint64_t>(ObservationOrigin::synthetic)
                   ? ObservationOrigin::synthetic
                   : ObservationOrigin::native;
    }

    constexpr auto operator<=>(const ApplyResult&) const noexcept = default;
};

struct PolicyRecord {
    PolicyArtifact artifact{};
    PolicyLifecycle lifecycle{PolicyLifecycle::none};
};

struct RequestReceipt {
    CommandIdentity identity{};
    ApplyResult result{};
};

struct SourceCursor {
    SourceId source_id{};
    SourceGeneration generation{};
    SourceSequence sequence{};
};

struct SourceEventReceipt {
    SourceId source_id{};
    SourceGeneration generation{};
    SourceSequence sequence{};
    Digest256 event_digest{};
};

struct ExecutablePolicyBundle {
    bool present{};
    FeatureSchemaArtifact feature_schema{};
    ExecutablePolicyArtifact executable_policy{};
    InstrumentExecutionProfile instrument{};
    VenueCapabilities venue{};
    std::uint64_t staged_state_version{};
    std::uint64_t activated_state_version{};

    constexpr auto operator<=>(const ExecutablePolicyBundle&) const noexcept = default;
};

struct DecisionRecordHot {
    DecisionId decision_id{};
    PolicyId policy_id{};
    PolicyVersion policy_version{};
    std::uint64_t state_version{};
    KillEpoch kill_epoch{};
    ExecutorGeneration executor_generation{};
    std::uint16_t matched_rule_index{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t rules_visited{};
    DecisionCode code{DecisionCode::none};
    ContractError contract_error{ContractError::none};
    Directive proposed_directive{Directive::no_action};
    Directive effective_directive{Directive::no_action};
    EvaluationMode evaluation_mode{EvaluationMode::authoritative};
    StrategyMode strategy_mode{StrategyMode::halted};
    ActionResolutionCode action_resolution_code{ActionResolutionCode::none};

    constexpr auto operator<=>(const DecisionRecordHot&) const noexcept = default;
};

struct DecisionRecordCold {
    DecisionId decision_id{};
    Digest256 command_payload_digest{};
    DecisionFrame frame{};
    EconomicActionProposal proposal{};
};

enum class ReservationState : std::uint8_t {
    held = 1,
    partially_consumed = 2,
    released = 3,
};

enum class ClusterOrderState : std::uint8_t {
    dispatch_authorized = 1,
    acknowledged = 2,
    live = 3,
    partially_filled = 4,
    cancel_pending = 5,
    replace_pending = 6,
    submission_unknown = 7,
    reconciling = 8,
    filled = 9,
    cancelled = 10,
    rejected = 11,
    conflict = 12,
};

enum class OutboxState : std::uint8_t {
    pending = 1,
    claimed = 2,
    attempted = 3,
    acknowledged = 4,
    rejected = 5,
    unknown = 6,
    reconciling = 7,
    conflict = 8,
    incomplete = 9,
};

struct RiskAccount {
    AccountId account_id{};
    InstrumentId instrument_id{};
    RiskPolicyVersion risk_policy_version{};
    RiskVersion risk_version{};
    PositionVersion position_version{};
    QuantityLots max_order_quantity{};
    QuantityLots max_reserved_quantity{};
    QuantityLots reserved_quantity{};
    MoneyMicros max_order_gross_notional{};
    MoneyMicros max_reserved_exposure{};
    MoneyMicros reserved_margin{};
    MoneyMicros reserved_fee{};
    std::uint32_t initial_margin_ppm{};
    std::uint32_t maximum_fee_ppm{};
    QuantityLots position_quantity{};
    QuantityLots max_position_quantity{};
    bool allow_short{};
    MoneyMicros cash{};
    MoneyMicros cost_basis{};
    MoneyMicros realized_pnl{};
    MoneyMicros fees_paid{};
    MoneyMicros window_realized_pnl{};
    MoneyMicros max_window_loss{};

    constexpr auto operator<=>(const RiskAccount&) const noexcept = default;
};

struct RiskReservation {
    ClusterOrderId cluster_order_id{};
    QuantityLots original_quantity{};
    QuantityLots filled_quantity{};
    QuantityLots remaining_quantity{};
    MoneyMicros remaining_notional{};
    MoneyMicros margin_hold{};
    MoneyMicros fee_hold{};
    ReservationState state{ReservationState::held};

    constexpr auto operator<=>(const RiskReservation&) const noexcept = default;
};

struct ClusterOrder {
    ClusterOrderId cluster_order_id{};
    EffectId effect_id{};
    DecisionId decision_id{};
    std::uint64_t authorization_state_version{};
    std::uint16_t reservation_index{};
    ClusterOrderState state{ClusterOrderState::dispatch_authorized};
    std::uint64_t last_evidence_state_version{};

    constexpr auto operator<=>(const ClusterOrder&) const noexcept = default;
};

struct OutboxEffectRecord {
    EffectId effect_id{};
    ClusterOrderId cluster_order_id{};
    DecisionId decision_id{};
    Digest256 payload_digest{};
    std::uint32_t effect_generation{};
    OutboxState state{OutboxState::pending};
    EffectOperation operation{EffectOperation::submit};

    constexpr auto operator<=>(const OutboxEffectRecord&) const noexcept = default;
};

inline constexpr std::size_t kDecisionRecordHotArenaSlotStrideBytes =
    round_up_to_cache_line(sizeof(DecisionRecordHot));
inline constexpr std::size_t kDecisionRecordColdArenaSlotStrideBytes =
    round_up_to_cache_line(sizeof(DecisionRecordCold));

static_assert(std::is_trivially_copyable_v<DecisionRecordHot>);
static_assert(std::is_trivially_copyable_v<DecisionRecordCold>);
static_assert(sizeof(DecisionRecordHot) <= kDecisionRecordHotBudgetBytes,
              "hot decision record exceeded its reviewed object budget");
static_assert(sizeof(DecisionRecordCold) <= kDecisionRecordColdBudgetBytes,
              "cold decision record exceeded its reviewed object budget");
static_assert(kDecisionRecordHotArenaSlotStrideBytes == kTargetCacheLineBytes,
              "hot decision record no longer fits one target cache-line slot");

struct TradingState {
    std::uint64_t state_version{};
    StrategyMode strategy_mode{StrategyMode::halted};
    KillEpoch kill_epoch{};
    ExecutorGeneration executor_generation{};
    std::size_t policy_count{};
    std::array<PolicyRecord, kMaxPolicies> policies{};
    ExecutablePolicyBundle active_executable_policy{};
    ExecutablePolicyBundle staged_executable_policy{};
    std::size_t source_count{};
    std::array<SourceCursor, kMaxSources> sources{};
    std::size_t source_event_count{};
    std::array<SourceEventReceipt, kMaxSourceEventReceipts> source_events{};
    std::size_t receipt_count{};
    std::array<RequestReceipt, kMaxRequestReceipts> receipts{};
    std::size_t decision_count{};
    std::array<DecisionRecordHot, kMaxDecisionRecords> decision_hot{};
    std::array<DecisionRecordCold, kMaxDecisionRecords> decision_cold{};
    std::size_t risk_account_count{};
    std::array<RiskAccount, kMaxRiskAccounts> risk_accounts{};
    std::size_t reservation_count{};
    std::array<RiskReservation, kMaxRiskReservations> reservations{};
    std::size_t order_count{};
    std::array<ClusterOrder, kMaxClusterOrders> orders{};
    std::size_t outbox_effect_count{};
    std::array<OutboxEffectRecord, kMaxOutboxEffects> outbox_effects{};
    std::uint64_t reclaimed_group_count{};
    std::uint64_t settled_incomplete_effect_count{};
    std::size_t caller_cursor_count{};
    std::array<CallerCursor, kMaxCallerCursors> caller_cursors{};
    Digest256 execution_catalog_root{};
    swap::State spot_swap{};
};

struct ApplyScratch final {};

class CatalogDependencyUnavailable final : public std::exception {
public:
    const char* what() const noexcept override {
        return "required execution catalog is unavailable";
    }
};

[[nodiscard]] Digest256 required_execution_catalog(
    const TradingState& state, const Command& command) noexcept;

[[nodiscard]] bool execution_catalog_ready(
    const Digest256& required_root, const VerifiedExecutionCatalog* catalog) noexcept;

[[nodiscard]] bool execution_catalog_bindings_valid(
    const TradingState& state, const VerifiedExecutionCatalog& catalog) noexcept;

[[nodiscard]] std::optional<OrderEffect> materialize_order_effect(
    const TradingState& state, EffectId effect_id) noexcept;

struct BoundOrderEffect {
    OrderEffect effect{};
    Digest256 catalog_root{};
    InstrumentSpecVersion instrument_spec_version{};
    VenueCapabilitiesVersion venue_capabilities_version{};

    constexpr auto operator<=>(const BoundOrderEffect&) const noexcept = default;
};

[[nodiscard]] std::optional<BoundOrderEffect> materialize_bound_order_effect(
    const TradingState& state, EffectId effect_id,
    const VerifiedExecutionCatalog& catalog) noexcept;

[[nodiscard]] ApplyResult apply(TradingState& state,
                                const Command& command,
                                ApplyScratch& scratch,
                                const VerifiedExecutionCatalog* catalog = nullptr);

[[nodiscard]] std::size_t pinned_receipt_count(const TradingState& state) noexcept;

[[nodiscard]] bool submit_permitted(const TradingState& state, DecisionId decision_id) noexcept;

[[nodiscard]] const PolicyRecord* find_policy(const TradingState& state,
                                              PolicyId policy_id,
                                              PolicyVersion policy_version) noexcept;

[[nodiscard]] const SourceCursor* find_source_cursor(const TradingState& state,
                                                     SourceId source_id) noexcept;

static_assert(std::is_nothrow_copy_assignable_v<PolicyArtifact>);
static_assert(std::is_nothrow_copy_assignable_v<CommandBody>);
static_assert(std::is_nothrow_copy_assignable_v<ApplyResult>);
static_assert(std::is_trivially_copyable_v<RiskAccount>);
static_assert(std::is_trivially_copyable_v<RiskReservation>);
static_assert(std::is_trivially_copyable_v<ClusterOrder>);
static_assert(std::is_trivially_copyable_v<OutboxEffectRecord>);
static_assert(sizeof(RiskAccount) <= kRiskAccountObjectBudgetBytes);
static_assert(sizeof(RiskReservation) <= kRiskReservationObjectBudgetBytes);
static_assert(sizeof(ClusterOrder) <= kClusterOrderObjectBudgetBytes);
static_assert(sizeof(OutboxEffectRecord) <=
              kOutboxEffectRecordObjectBudgetBytes);
static_assert(sizeof(TradingState) <= kTradingStateObjectBudgetBytes,
              "TradingState exceeded its reviewed in-memory object budget");

}
