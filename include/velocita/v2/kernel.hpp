#pragma once

#include "velocita/v2/action_resolver.hpp"
#include "velocita/v2/artifact_digest.hpp"
#include "velocita/v2/contracts.hpp"
#include "velocita/v2/execution_catalog.hpp"

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

inline constexpr std::size_t kMaxPolicies = 16;
inline constexpr std::size_t kMaxSources = 16;
inline constexpr std::size_t kMaxSourceEventReceipts = 128;
inline constexpr std::size_t kMaxRequestReceipts = 256;
inline constexpr std::size_t kMaxDecisionRecords = 16;
inline constexpr std::size_t kMaxRiskAccounts = 4;
inline constexpr std::size_t kMaxRiskReservations = 16;
inline constexpr std::size_t kMaxClusterOrders = 16;
inline constexpr std::size_t kMaxOutboxEffects = 16;
inline constexpr std::size_t kMaxVenueEventReceipts = 64;
// Distinct logical clients of the cluster: the market-data edge, the authorizer,
// the execution gateway, and the operator console. CallerId must be a STABLE
// identity assigned by configuration - a caller returning from a restart with a
// fresh id would fill this arena with dead entries and rebuild the lifetime
// wedge one level up. See architecture 8.6.
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
    // Native amend and overlapping replacement are disabled. Safe serial
    // replacement remains required: cancel -> reconcile -> freshly authorize.
    replacement_not_supported,
    // At most one unresolved cluster order per (account, instrument, venue,
    // logical order slot), enforcing the selected no-overlap policy even when
    // a fresh DecisionId would derive a different ClusterOrderId.
    order_slot_occupied,
    risk_account_capacity_exceeded,
    risk_version_exhausted,
    risk_reconfiguration_blocked,
    // Architecture 8.6. The request identity is at or below its caller's
    // watermark but its receipt has been reclaimed, so the kernel can no longer
    // say what happened. It refuses rather than re-executing, because
    // re-execution would be a second economic mutation; the caller reconciles
    // against state, where almost every command's real protection already lives.
    request_expired,
    // Reserved for historical results from the withdrawn venue-sequence
    // watermark design. New reducers never emit this code.
    venue_event_expired,
    // A control action arrived from a caller outside the reserved operator
    // band. See `kMaxOperatorCallerId`. Appended at the END of this enum on
    // purpose: every existing code keeps its value, so no retained result and
    // no golden byte moves.
    unauthorized_control,
    // A resume into a risk-increasing mode was refused because the cluster
    // cannot currently say what its exposure is. See G-059.
    reconciliation_required,
    catalog_not_installed,
    catalog_installation_blocked,
    immutable_catalog_conflict,
    catalog_row_not_found,
    catalog_binding_mismatch,
    position_limit_exceeded,
};

// The highest RequestSequence whose receipt this caller has had RECLAIMED.
//
// Deliberately not "highest ever admitted". The kernel must refuse only what it
// actually forgot, so a caller sending a low sequence the cluster never held is
// treated as fresh rather than refused. Refusing on the high-water mark instead
// would enforce monotonicity as a side effect of expiry, which is a different
// contract and a stricter one than architecture 8.6 requires.
//
// This cursor cannot itself be forgotten: it is the only thing distinguishing an
// EXPIRED request from one never seen, and without it expiry would silently
// permit re-execution - a second economic mutation.
struct CallerCursor {
    CallerId caller_id{};
    RequestSequence expired_below{};
    RequestSequence gateway_results_acknowledged_through{};

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

// THE OPERATOR CALLER BAND. Control actions - kill epoch, executor generation,
// strategy mode, catalog installation, risk limits - are accepted only from a
// `CallerId` at or below this value.
//
// The reducer CANNOT verify a signature. G-066 keeps key material out of
// replicated state and `apply` performs no I/O, so authentication is
// necessarily a property of the edge that admits the command. What the cluster
// can do is refuse a control action from a caller the edge never authenticated,
// and record which operator issued the one it accepted - the existing
// `CommandIdentity` already carries both the caller and a digest binding the
// payload, so this needs no new bytes.
//
// ONE-DIRECTIONAL, deliberately: a control command must come from an operator
// caller, but an operator caller may also issue ordinary ones. Making it
// exclusive would buy nothing the edge does not already have to guarantee, and
// the guarantee it does have to make is the deployment obligation that gives
// this meaning - NEVER ISSUE A RESERVED CallerId TO A STRATEGY.
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

// WHAT A MASS CANCEL REACHES. Named rather than encoded as wildcard zeros,
// because an operator reading a log entry under pressure should not have to
// know that an empty account means "everything".
enum class MassCancelScope : std::uint8_t {
    all = 1,
    account = 2,
    account_instrument = 3,
};

// ADR 0007. ONE command, not N individual cancels.
//
// The N-cancel form needed no new bytes and fails twice. Sixteen entries
// against a four-block control reserve can drain the kill path it was meant to
// sit beside - the exact failure the reservation exists to prevent, arriving
// from inside the lane. And a mass cancel that stops half way leaves orders
// live with no record of the intent to cancel them, while the operator has been
// told the book is flat.
//
// Bounded by construction: the scope can name at most `kMaxClusterOrders`
// orders, and `kMaxOutboxEffects` is the same number by static assertion, so
// the request either fits or is refused. Neither outcome is partial.
//
// Carries the expected control values for the same reason `SetStrategyMode`
// does: an operator acting on a stale view of authority must not have that
// action silently applied to a newer one.
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

// Staging is deliberately atomic: no replica can observe rules from one
// revision with units or venue economics from another revision.
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
    // Bounds |position| after every open order on either side fills. Zero
    // denies new exposure; it never means unlimited.
    QuantityLots max_position_quantity{};
    // False is long-only: a sell may only reduce a long position.
    bool allow_short{};
    // Non-zero reference into the operator's audit record of why the limits
    // changed. Carried in the log only; the kernel does not interpret it.
    std::uint32_t reason_code{};
};

struct AcknowledgeGatewayResults {
    RequestSequence acknowledged_through{};
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
           value.max_position_quantity.atoms() >= 0 && value.reason_code != 0;
}

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
                                 InstallExecutionCatalog>;

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
    // Appended, so every existing kind keeps its value.
    mass_cancel = 6,
    // Carried by a `stale_risk_state` authorization refusal so the strategy can
    // bind the account's current versions on its next frame. Words are
    // {risk_version, position_version, position_quantity atoms}.
    risk_state = 7,
};

// Three canonical 64-bit words are interpreted only through the explicit tag.
// This overlays command-family-specific results without making every retained
// RequestReceipt pay for both source and Phase 4 authorization fields.
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

    // How much a mass cancel actually did. An operator who fired one needs to
    // know the difference between "nothing was open" and "nothing could be
    // cancelled", and those are the same `applied` result without this.
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
    // detail_kind tags these two words: fill stream identity for fills,
    // policy identity otherwise. Raw words are exposed for canonical codecs.
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

    [[nodiscard]] constexpr EffectId event_effect_id() const noexcept {
        return detail_kind == ApplyResultDetailKind::venue_event
                   ? EffectId{metadata_words[0]}
                   : EffectId{};
    }
    // Which identity domain this receipt belongs to. Retained because the kind
    // is not in the result, and a deduplication or reclamation pass must know
    // whether the sequence means anything without going to find the
    // observation.
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

// The hot record contains only fields used to find a decision and consume its
// authoritative outcome. Its independently retained cold partner contains the
// full input needed for audit and deterministic re-evaluation.
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
    // Gross notional ceiling for one proposed order.
    MoneyMicros max_order_gross_notional{};
    // Aggregate margin_hold + fee_hold budget, NOT gross open-order notional.
    MoneyMicros max_reserved_exposure{};
    MoneyMicros reserved_margin{};
    MoneyMicros reserved_fee{};
    std::uint32_t initial_margin_ppm{};
    std::uint32_t maximum_fee_ppm{};
    QuantityLots position_quantity{};
    QuantityLots max_position_quantity{};
    bool allow_short{};

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

// The exact economic payload is normalized through the retained immutable
// DecisionRecordCold. This record owns delivery identity and digest without
// duplicating the 176-byte boundary object in every bounded slot.
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

// TradingState has one mutable owner. References into its arrays must not be
// retained across apply(): later phases may reuse slots during replicated
// expiry even though std::array itself never reallocates.
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
    // Monotonic count of fully resolved order groups reclaimed from the bounded
    // arenas. Deliberately in state rather than in ApplyResult: it gives the
    // dashboard the "how much has ever been swept" signal without growing the
    // 128-byte canonical result or regenerating every existing golden result.
    std::uint64_t reclaimed_group_count{};
    // Monotonic count of effects settled to `incomplete` because their order
    // resolved before they ever received evidence. Operationally this is the
    // signal that the gateway is losing effect outcomes: reclamation stays
    // healthy, but a rising count means delivery bookkeeping is incomplete.
    std::uint64_t settled_incomplete_effect_count{};
    std::size_t caller_cursor_count{};
    std::array<CallerCursor, kMaxCallerCursors> caller_cursors{};
    Digest256 execution_catalog_root{};
};

// Reserved for bounded temporary indexes/encoding buffers in later reducer
// increments. It is caller-owned so apply never needs hidden thread-local or
// heap-backed scratch state.
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

// Recovery-only semantic preflight, in addition to canonical snapshot validation.
[[nodiscard]] bool execution_catalog_bindings_valid(
    const TradingState& state, const VerifiedExecutionCatalog& catalog) noexcept;

// Pure, bounded reconstruction from retained authorization evidence. Returns
// no value for missing/inconsistent evidence or unsupported operations.
// The returned value owns its data; no pointer into TradingState escapes.
[[nodiscard]] std::optional<OrderEffect> materialize_order_effect(
    const TradingState& state, EffectId effect_id) noexcept;

struct BoundOrderEffect {
    OrderEffect effect{};
    Digest256 catalog_root{};
    InstrumentSpecVersion instrument_spec_version{};
    VenueCapabilitiesVersion venue_capabilities_version{};

    constexpr auto operator<=>(const BoundOrderEffect&) const noexcept = default;
};

// A local copied value, not an authenticated cross-process authority envelope.
[[nodiscard]] std::optional<BoundOrderEffect> materialize_bound_order_effect(
    const TradingState& state, EffectId effect_id,
    const VerifiedExecutionCatalog& catalog) noexcept;

[[nodiscard]] ApplyResult apply(TradingState& state,
                                const Command& command,
                                ApplyScratch& scratch,
                                const VerifiedExecutionCatalog* catalog = nullptr);

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

}  // namespace velocita::v2
