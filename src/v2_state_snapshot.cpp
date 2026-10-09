#include "velocita/v2/state_snapshot.hpp"

#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <type_traits>

namespace velocita::v2 {
namespace {

inline constexpr std::array kSnapshotMagic{
    std::byte{'V'}, std::byte{'2'}, std::byte{'S'}, std::byte{'T'}};

class Writer final {
public:
    explicit Writer(const std::span<std::byte> output) noexcept : output_(output) {}

    template <typename Value>
    void integral(const Value value) noexcept {
        static_assert(std::is_integral_v<Value>);
        using Unsigned = std::make_unsigned_t<Value>;
        const auto bits = static_cast<Unsigned>(value);
        for (std::size_t index = 0; index < sizeof(Value); ++index) {
            output_[offset_++] =
                static_cast<std::byte>((bits >> (index * 8U)) & 0xffU);
        }
    }

    void digest(const Digest256& value) noexcept {
        std::copy(value.bytes.begin(), value.bytes.end(),
                  output_.begin() + static_cast<std::ptrdiff_t>(offset_));
        offset_ += value.bytes.size();
    }

private:
    std::span<std::byte> output_;
    std::size_t offset_{};
};

class Reader final {
public:
    explicit Reader(const std::span<const std::byte> input) noexcept : input_(input) {}

    template <typename Value>
    [[nodiscard]] Value integral() noexcept {
        static_assert(std::is_integral_v<Value>);
        using Unsigned = std::make_unsigned_t<Value>;
        if (remaining() < sizeof(Value)) {
            valid_ = false;
            return {};
        }
        Unsigned bits{};
        for (std::size_t index = 0; index < sizeof(Value); ++index) {
            bits |= static_cast<Unsigned>(
                        std::to_integer<unsigned char>(input_[offset_ + index]))
                    << (index * 8U);
        }
        offset_ += sizeof(Value);
        if constexpr (std::is_signed_v<Value>) {
            return std::bit_cast<Value>(bits);
        }
        return bits;
    }

    [[nodiscard]] Digest256 digest() noexcept {
        Digest256 value;
        if (remaining() < value.bytes.size()) {
            valid_ = false;
            return value;
        }
        std::copy_n(input_.begin() + static_cast<std::ptrdiff_t>(offset_),
                    value.bytes.size(), value.bytes.begin());
        offset_ += value.bytes.size();
        return value;
    }

    [[nodiscard]] bool finished() const noexcept {
        return valid_ && offset_ == input_.size();
    }

private:
    [[nodiscard]] std::size_t remaining() const noexcept {
        return offset_ <= input_.size() ? input_.size() - offset_ : 0;
    }

    std::span<const std::byte> input_;
    std::size_t offset_{};
    bool valid_{true};
};

[[nodiscard]] constexpr bool known(const StrategyMode value) noexcept {
    switch (value) {
        case StrategyMode::normal:
        case StrategyMode::no_new_risk:
        case StrategyMode::cancel_only:
        case StrategyMode::reduce_only:
        case StrategyMode::halted:
        case StrategyMode::flatten:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool known(const PolicyLifecycle value,
                                   const bool allow_none) noexcept {
    switch (value) {
        case PolicyLifecycle::none:
            return allow_none;
        case PolicyLifecycle::proposed:
        case PolicyLifecycle::validated:
        case PolicyLifecycle::active:
        case PolicyLifecycle::superseded:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool known(const ApplyCode value) noexcept {
    switch (value) {
        case ApplyCode::applied:
        case ApplyCode::already_applied:
        case ApplyCode::invalid_command:
        case ApplyCode::invalid_policy_contract:
        case ApplyCode::duplicate_conflict:
        case ApplyCode::policy_not_found:
        case ApplyCode::immutable_policy_conflict:
        case ApplyCode::non_monotonic_policy_version:
        case ApplyCode::invalid_policy_transition:
        case ApplyCode::active_policy_scope_conflict:
        case ApplyCode::stale_control_generation:
        case ApplyCode::non_monotonic_kill_epoch:
        case ApplyCode::non_monotonic_executor_generation:
        case ApplyCode::source_event_conflict:
        case ApplyCode::source_generation_gap:
        case ApplyCode::source_generation_regression:
        case ApplyCode::source_sequence_gap:
        case ApplyCode::source_sequence_regression:
        case ApplyCode::source_capacity_exceeded:
        case ApplyCode::source_event_capacity_exceeded:
        case ApplyCode::decision_capacity_exceeded:
        case ApplyCode::policy_capacity_exceeded:
        case ApplyCode::receipt_capacity_exceeded:
        case ApplyCode::state_version_exhausted:
        case ApplyCode::decision_id_conflict:
        case ApplyCode::invalid_feature_schema_contract:
        case ApplyCode::invalid_executable_policy_contract:
        case ApplyCode::artifact_digest_mismatch:
        case ApplyCode::executable_policy_binding_failed:
        case ApplyCode::invalid_instrument_profile_contract:
        case ApplyCode::invalid_venue_capabilities_contract:
        case ApplyCode::executable_policy_not_staged:
        case ApplyCode::executable_policy_identity_conflict:
        case ApplyCode::decision_not_authorizable:
        case ApplyCode::risk_account_not_found:
        case ApplyCode::stale_risk_state:
        case ApplyCode::risk_arithmetic_overflow:
        case ApplyCode::insufficient_risk_capacity:
        case ApplyCode::reservation_capacity_exceeded:
        case ApplyCode::order_capacity_exceeded:
        case ApplyCode::outbox_capacity_exceeded:
        case ApplyCode::derived_identity_collision:
        case ApplyCode::venue_observation_not_applicable:
        case ApplyCode::fill_identity_conflict:
        case ApplyCode::fill_capacity_exceeded:
        case ApplyCode::fill_quantity_conflict:
        case ApplyCode::position_arithmetic_overflow:
        case ApplyCode::order_not_found:
        case ApplyCode::order_not_cancellable:
        case ApplyCode::venue_event_identity_conflict:
        case ApplyCode::venue_event_capacity_exceeded:
        case ApplyCode::replacement_not_authorizable:
        case ApplyCode::order_not_replaceable:
        case ApplyCode::replacement_not_supported:
        case ApplyCode::order_slot_occupied:
        case ApplyCode::risk_account_capacity_exceeded:
        case ApplyCode::risk_version_exhausted:
        case ApplyCode::risk_reconfiguration_blocked:
        case ApplyCode::request_expired:
        case ApplyCode::venue_event_expired:
        case ApplyCode::unauthorized_control:
        case ApplyCode::reconciliation_required:
        case ApplyCode::catalog_not_installed:
        case ApplyCode::catalog_installation_blocked:
        case ApplyCode::immutable_catalog_conflict:
        case ApplyCode::catalog_row_not_found:
        case ApplyCode::catalog_binding_mismatch:
        case ApplyCode::position_limit_exceeded:
        case ApplyCode::fill_price_conflict:
        case ApplyCode::fill_fee_conflict:
        case ApplyCode::loss_limit_reached:
        case ApplyCode::unauthorized_command:
        case ApplyCode::caller_role_conflict:
        case ApplyCode::order_not_in_conflict:
        case ApplyCode::order_not_reducing:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool known(const ContractError value) noexcept {
    switch (value) {
        case ContractError::none:
        case ContractError::invalid_identity:
        case ContractError::unsupported_schema:
        case ContractError::missing_digest:
        case ContractError::invalid_time_range:
        case ContractError::capacity_exceeded:
        case ContractError::empty_rules:
        case ContractError::empty_features:
        case ContractError::invalid_feature_index:
        case ContractError::invalid_age:
        case ContractError::invalid_enum:
        case ContractError::non_positive_price:
        case ContractError::non_positive_quantity:
        case ContractError::non_positive_notional:
        case ContractError::invalid_value_source:
        case ContractError::non_canonical_value:
        case ContractError::invalid_safety_cap:
        case ContractError::invalid_order_target:
        case ContractError::invalid_action_template:
        case ContractError::invalid_capability:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool known(const EvaluationMode value) noexcept {
    return value == EvaluationMode::authoritative ||
           value == EvaluationMode::shadow;
}

[[nodiscard]] constexpr bool known(const DecisionCode value) noexcept {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(DecisionCode::action_resolution_rejected);
}

[[nodiscard]] constexpr bool known(const ActionResolutionCode value) noexcept {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(ActionResolutionCode::notional_cap_exceeded);
}

[[nodiscard]] constexpr bool known(const Directive value) noexcept {
    switch (value) {
        case Directive::no_action:
        case Directive::place_limit:
        case Directive::cancel:
        case Directive::replace:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool known(const ApplyResultDetailKind value) noexcept {
    return value == ApplyResultDetailKind::none ||
           value == ApplyResultDetailKind::source ||
           value == ApplyResultDetailKind::authorization ||
           value == ApplyResultDetailKind::fill ||
           value == ApplyResultDetailKind::venue_event ||
           value == ApplyResultDetailKind::risk_configuration ||
           value == ApplyResultDetailKind::mass_cancel ||
           value == ApplyResultDetailKind::risk_state ||
           value == ApplyResultDetailKind::source_state;
}

[[nodiscard]] constexpr bool known(const ReservationState value) noexcept {
    return value == ReservationState::held ||
           value == ReservationState::partially_consumed ||
           value == ReservationState::released;
}

[[nodiscard]] constexpr bool known(const ClusterOrderState value) noexcept {
    return static_cast<std::uint8_t>(value) >=
               static_cast<std::uint8_t>(ClusterOrderState::dispatch_authorized) &&
           static_cast<std::uint8_t>(value) <=
               static_cast<std::uint8_t>(ClusterOrderState::conflict);
}

[[nodiscard]] constexpr bool known(const OutboxState value) noexcept {
    switch (value) {
        case OutboxState::pending:
        case OutboxState::acknowledged:
        case OutboxState::rejected:
        case OutboxState::unknown:
        case OutboxState::reconciling:
        case OutboxState::conflict:
        case OutboxState::incomplete:
            return true;
        case OutboxState::claimed:
        case OutboxState::attempted:
            return false;
    }
    return false;
}

[[nodiscard]] constexpr bool known(const EffectOperation value) noexcept {
    return value == EffectOperation::submit || value == EffectOperation::cancel ||
           value == EffectOperation::replace;
}

[[nodiscard]] constexpr bool valid_detail(const ApplyResult& value) noexcept {
    if (!known(value.detail_kind)) return false;
    if (value.detail_kind == ApplyResultDetailKind::none) {
        return value.detail == ApplyResultDetail{};
    }
    if (value.detail_kind == ApplyResultDetailKind::risk_configuration) {
        return value.detail.words[0] != 0 && value.detail.words[1] != 0 &&
               value.detail.words[2] != 0;
    }
    if (value.detail_kind == ApplyResultDetailKind::mass_cancel) {
        const auto in_scope = value.mass_in_scope();
        const auto cancelled = value.mass_cancelled();
        return value.code == ApplyCode::applied && in_scope <= kMaxClusterOrders &&
               cancelled <= in_scope && value.mass_skipped() == in_scope - cancelled;
    }
    if (value.detail_kind == ApplyResultDetailKind::source_state) {
        return value.code == ApplyCode::source_generation_gap &&
               value.source_cursor_id().value() != 0 &&
               (value.source_cursor_generation().value() == 0) ==
                   (value.source_cursor_sequence().value() == 0);
    }
    if (value.detail_kind == ApplyResultDetailKind::risk_state) {
        return value.code == ApplyCode::stale_risk_state &&
               value.current_risk_version().value() != 0 &&
               value.current_position_version().value() != 0 &&
               value.decision_id.value() != 0;
    }
    const bool accepted = value.code == ApplyCode::applied ||
                          value.code == ApplyCode::already_applied;
    if (value.detail_kind == ApplyResultDetailKind::source) {
        return accepted
                   ? value.source_id().value() != 0 &&
                         value.source_generation().value() != 0 &&
                         value.source_sequence().value() != 0
                   : value.detail != ApplyResultDetail{};
    }
    if (value.detail_kind == ApplyResultDetailKind::authorization) {
        return value.cluster_order_id().value() != 0 &&
               value.effect_id().value() != 0 &&
               value.reserved_exposure().atoms() > 0;
    }
    if (value.detail_kind == ApplyResultDetailKind::fill) {
        return accepted
                   ? value.fill_session_id().value() != 0 &&
                         value.fill_sequence().value() != 0 &&
                         value.fill_venue_id().value() != 0 &&
                         value.execution_id().value() != 0 &&
                         value.filled_quantity().atoms() > 0 &&
                         value.decision_id.value() != 0
                   : value.detail != ApplyResultDetail{};
    }
    const auto origin = value.metadata_words[1];
    const bool known_origin =
        origin == static_cast<std::uint64_t>(ObservationOrigin::native) ||
        origin == static_cast<std::uint64_t>(ObservationOrigin::synthetic);
    return accepted
               ? value.event_venue_id().value() != 0 &&
                     value.event_session_id().value() != 0 &&
                     value.event_sequence().value() != 0 &&
                     value.event_effect_id().value() != 0 &&
                     known_origin &&
                     value.decision_id.value() != 0
               : value.detail != ApplyResultDetail{};
}

[[nodiscard]] bool valid_state(const TradingState& state) noexcept {
    if (!known(state.strategy_mode) || state.policy_count > state.policies.size() ||
        state.source_count > state.sources.size() ||
        state.source_event_count > state.source_events.size() ||
        state.receipt_count > state.receipts.size() ||
        state.decision_count > state.decision_hot.size() ||
        state.decision_count > state.decision_cold.size() ||
        state.risk_account_count > state.risk_accounts.size() ||
        state.reservation_count > state.reservations.size() ||
        state.order_count > state.orders.size() ||
        state.outbox_effect_count > state.outbox_effects.size() ||
        state.caller_cursor_count > state.caller_cursors.size()) {
        return false;
    }

    const bool has_economic_state =
        state.policy_count != 0 || state.active_executable_policy.present ||
        state.staged_executable_policy.present || state.decision_count != 0 ||
        state.risk_account_count != 0 || state.reservation_count != 0 ||
        state.order_count != 0 || state.outbox_effect_count != 0;
    if (has_economic_state && state.execution_catalog_root.is_zero()) {
        return false;
    }

    const auto valid_bundle = [&](const ExecutablePolicyBundle& bundle,
                                  const bool active) noexcept {
        if (!bundle.present) return bundle == ExecutablePolicyBundle{};
        if (validate(bundle.feature_schema) != ContractError::none ||
            validate(bundle.executable_policy) != ContractError::none ||
            validate(bundle.instrument) != ContractError::none ||
            validate(bundle.venue) != ContractError::none ||
            verify_artifact_digest(bundle.feature_schema).error !=
                ArtifactDigestError::none ||
            verify_artifact_digest(bundle.executable_policy).error !=
                ArtifactDigestError::none ||
            bundle.staged_state_version == 0 ||
            bundle.staged_state_version > state.state_version ||
            (active && (bundle.activated_state_version == 0 ||
                        bundle.activated_state_version > state.state_version)) ||
            (!active && bundle.activated_state_version != 0)) {
            return false;
        }
        const auto* policy = find_policy(state, bundle.executable_policy.policy_id,
                                         bundle.executable_policy.policy_version);
        if (policy == nullptr ||
            bind_executable_policy(policy->artifact, bundle.feature_schema,
                                   bundle.executable_policy) !=
                ExecutablePolicyBindingCode::bound ||
            bundle.instrument.instrument_id != policy->artifact.instrument_id ||
            bundle.instrument.venue_id != bundle.venue.venue_id ||
            (active && policy->lifecycle != PolicyLifecycle::active) ||
            (!active && policy->lifecycle != PolicyLifecycle::validated &&
             policy->lifecycle != PolicyLifecycle::active)) {
            return false;
        }
        for (std::size_t index = 0;
             index < bundle.executable_policy.template_count; ++index) {
            const auto& action = bundle.executable_policy.templates[index];
            if (action.instrument_spec_version !=
                    bundle.instrument.instrument_spec_version ||
                action.venue_capabilities_version !=
                    bundle.venue.venue_capabilities_version) {
                return false;
            }
        }
        return true;
    };
    if (!valid_bundle(state.active_executable_policy, true) ||
        !valid_bundle(state.staged_executable_policy, false)) {
        return false;
    }
    if (state.active_executable_policy.present &&
        state.staged_executable_policy.present) {
        const auto* active = find_policy(
            state, state.active_executable_policy.executable_policy.policy_id,
            state.active_executable_policy.executable_policy.policy_version);
        const auto* staged = find_policy(
            state, state.staged_executable_policy.executable_policy.policy_id,
            state.staged_executable_policy.executable_policy.policy_version);
        if (active == nullptr || staged == nullptr ||
            active->artifact.account_id != staged->artifact.account_id ||
            active->artifact.instrument_id != staged->artifact.instrument_id) {
            return false;
        }
    }

    for (std::size_t index = 0; index < state.policy_count; ++index) {
        const auto& record = state.policies[index];
        if (validate(record.artifact) != ContractError::none ||
            !known(record.lifecycle, false)) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            const auto& previous = state.policies[prior];
            if (previous.artifact.policy_id == record.artifact.policy_id &&
                previous.artifact.policy_version == record.artifact.policy_version) {
                return false;
            }
            if (previous.lifecycle == PolicyLifecycle::active &&
                record.lifecycle == PolicyLifecycle::active &&
                previous.artifact.account_id == record.artifact.account_id &&
                previous.artifact.instrument_id == record.artifact.instrument_id) {
                return false;
            }
        }
    }

    for (std::size_t index = 0; index < state.source_count; ++index) {
        const auto& source = state.sources[index];
        if (source.source_id.value() == 0 || source.generation.value() == 0 ||
            source.sequence.value() == 0) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (state.sources[prior].source_id == source.source_id) return false;
        }
    }

    for (std::size_t index = 0; index < state.source_event_count; ++index) {
        const auto& event = state.source_events[index];
        if (event.source_id.value() == 0 || event.generation.value() == 0 ||
            event.sequence.value() == 0 || event.event_digest.is_zero()) {
            return false;
        }
        const auto* cursor = find_source_cursor(state, event.source_id);
        if (cursor == nullptr || event.generation.value() > cursor->generation.value() ||
            (event.generation == cursor->generation &&
             event.sequence.value() > cursor->sequence.value())) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            const auto& previous = state.source_events[prior];
            if (previous.source_id == event.source_id &&
                previous.generation == event.generation &&
                previous.sequence == event.sequence) {
                return false;
            }
        }
    }

    for (std::size_t index = 0; index < state.caller_cursor_count; ++index) {
        const auto& cursor = state.caller_cursors[index];
        if (cursor.caller_id.value() == 0) return false;
        if (cursor.role != CallerRole::none && cursor.role != CallerRole::strategy &&
            cursor.role != CallerRole::gateway) return false;
        if (cursor.role != CallerRole::none && is_operator_caller(cursor.caller_id)) return false;
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (state.caller_cursors[prior].caller_id == cursor.caller_id) {
                return false;
            }
        }
    }

    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        const auto& receipt = state.receipts[index];
        if (receipt.identity.caller_id.value() == 0 ||
            receipt.identity.request_sequence.value() == 0 ||
            receipt.identity.payload_digest.is_zero() ||
            !known(receipt.result.code) || !known(receipt.result.contract_error) ||
            !known(receipt.result.policy_lifecycle, true) ||
            !known(receipt.result.strategy_mode) ||
            !known(receipt.result.decision_code) ||
            !known(receipt.result.proposed_directive) ||
            !known(receipt.result.effective_directive) ||
            !known(receipt.result.evaluation_mode) ||
            !known(receipt.result.action_resolution_code) ||
            !valid_detail(receipt.result) ||
            (receipt.result.evaluation_mode == EvaluationMode::shadow &&
             receipt.result.effective_directive != Directive::no_action) ||
            receipt.result.rules_visited > kMaxPolicyRules ||
            (receipt.result.matched_rule_index !=
                 std::numeric_limits<std::uint16_t>::max() &&
             receipt.result.matched_rule_index >= kMaxPolicyRules) ||
            receipt.result.state_version > state.state_version) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (state.receipts[prior].identity.caller_id == receipt.identity.caller_id &&
                state.receipts[prior].identity.request_sequence ==
                    receipt.identity.request_sequence) {
                return false;
            }
        }
    }

    for (std::size_t index = 0; index < state.decision_count; ++index) {
        const auto& hot = state.decision_hot[index];
        const auto& cold = state.decision_cold[index];
        const auto frame_error = validate(cold.frame);
        if (hot.decision_id.value() == 0 || hot.decision_id != cold.decision_id ||
            cold.command_payload_digest.is_zero() ||
            cold.frame.feature_count > kMaxDecisionFeatures ||
            (frame_error != ContractError::none &&
             (hot.code != DecisionCode::invalid_frame ||
              hot.contract_error != frame_error)) ||
            (frame_error == ContractError::none &&
             hot.code == DecisionCode::invalid_frame) ||
            hot.policy_id != cold.frame.policy_id ||
            hot.policy_version != cold.frame.policy_version ||
            hot.state_version > state.state_version || !known(hot.code) ||
            !known(hot.contract_error) || !known(hot.proposed_directive) ||
            !known(hot.effective_directive) || !known(hot.evaluation_mode) ||
            !known(hot.strategy_mode) || !known(hot.action_resolution_code) ||
            hot.rules_visited > kMaxPolicyRules ||
            (hot.matched_rule_index != std::numeric_limits<std::uint16_t>::max() &&
             hot.matched_rule_index >= kMaxPolicyRules) ||
            (hot.evaluation_mode == EvaluationMode::shadow &&
             hot.effective_directive != Directive::no_action) ||
            (hot.evaluation_mode == EvaluationMode::authoritative &&
             hot.code == DecisionCode::shadow_recorded) ||
            (hot.code == DecisionCode::action &&
             hot.effective_directive != hot.proposed_directive) ||
            ((hot.code == DecisionCode::control_suppressed ||
              hot.code == DecisionCode::shadow_recorded) &&
             hot.effective_directive != Directive::no_action) ||
            hot.code == DecisionCode::none) {
            return false;
        }
        if ((hot.action_resolution_code == ActionResolutionCode::proposed &&
             (validate(cold.proposal) != ContractError::none ||
              cold.proposal.decision_id != hot.decision_id ||
              cold.proposal.policy_id != hot.policy_id ||
              cold.proposal.policy_version != hot.policy_version ||
              cold.proposal.account_id != cold.frame.account_id ||
              cold.proposal.instrument_id != cold.frame.instrument_id ||
              cold.proposal.venue_id != cold.frame.venue_id ||
              cold.proposal.directive != hot.proposed_directive)) ||
            (hot.action_resolution_code != ActionResolutionCode::proposed &&
             cold.proposal != EconomicActionProposal{}) ||
            (hot.code == DecisionCode::action &&
             hot.proposed_directive != Directive::no_action &&
             hot.action_resolution_code != ActionResolutionCode::proposed)) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (state.decision_hot[prior].decision_id == hot.decision_id) return false;
        }
    }

    for (std::size_t index = 0; index < state.risk_account_count; ++index) {
        const auto& risk = state.risk_accounts[index];
        if (risk.account_id.value() == 0 || risk.instrument_id.value() == 0 ||
            risk.risk_policy_version.value() == 0 || risk.risk_version.value() == 0 ||
            risk.position_version.value() == 0 ||
            risk.max_order_quantity.atoms() <= 0 ||
            risk.max_reserved_quantity.atoms() <= 0 ||
            risk.max_order_quantity > risk.max_reserved_quantity ||
            risk.reserved_quantity.atoms() < 0 ||
            risk.max_order_gross_notional.atoms() <= 0 ||
            risk.max_reserved_exposure.atoms() <= 0 ||
            risk.reserved_margin.atoms() < 0 || risk.reserved_fee.atoms() < 0 ||
            risk.initial_margin_ppm == 0 || risk.initial_margin_ppm > 1'000'000 ||
            risk.maximum_fee_ppm > 1'000'000 ||
            risk.max_position_quantity.atoms() < 0 ||
            risk.cost_basis.atoms() < 0 || risk.fees_paid.atoms() < 0 ||
            risk.max_window_loss.atoms() < 0 ||
            (risk.position_quantity.atoms() == 0 && risk.cost_basis.atoms() != 0)) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (state.risk_accounts[prior].account_id == risk.account_id &&
                state.risk_accounts[prior].instrument_id == risk.instrument_id) {
                return false;
            }
        }
    }

    for (std::size_t index = 0; index < state.reservation_count; ++index) {
        const auto& reservation = state.reservations[index];
        const auto quantities = reservation.filled_quantity.checked_add(
            reservation.remaining_quantity);
        if (reservation.cluster_order_id.value() == 0 ||
            reservation.original_quantity.atoms() <= 0 ||
            reservation.filled_quantity.atoms() < 0 ||
            reservation.remaining_quantity.atoms() < 0 || !quantities ||
            *quantities > reservation.original_quantity ||
            reservation.remaining_notional.atoms() < 0 ||
            reservation.margin_hold.atoms() < 0 ||
            reservation.fee_hold.atoms() < 0 || !known(reservation.state) ||
            (reservation.state != ReservationState::released &&
             *quantities != reservation.original_quantity) ||
            (reservation.state == ReservationState::released &&
             (reservation.remaining_quantity.atoms() != 0 ||
              reservation.remaining_notional.atoms() != 0 ||
              reservation.margin_hold.atoms() != 0 ||
              reservation.fee_hold.atoms() != 0))) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (state.reservations[prior].cluster_order_id ==
                reservation.cluster_order_id) {
                return false;
            }
        }
    }

    if (state.order_count != state.reservation_count) return false;
    struct ReservedTotals {
        QuantityLots quantity{};
        MoneyMicros margin{};
        MoneyMicros fee{};
    };
    std::array<ReservedTotals, kMaxRiskAccounts> totals{};
    for (std::size_t index = 0; index < state.order_count; ++index) {
        const auto& order = state.orders[index];
        if (order.cluster_order_id.value() == 0 || order.effect_id.value() == 0 ||
            order.decision_id.value() == 0 || order.authorization_state_version == 0 ||
            order.authorization_state_version > state.state_version ||
            order.last_evidence_state_version > state.state_version ||
            order.reservation_index >= state.reservation_count || !known(order.state) ||
            state.reservations[order.reservation_index].cluster_order_id !=
                order.cluster_order_id) {
            return false;
        }
        const EconomicActionProposal* proposal = nullptr;
        for (std::size_t decision = 0; decision < state.decision_count; ++decision) {
            if (state.decision_hot[decision].decision_id == order.decision_id) {
                proposal = &state.decision_cold[decision].proposal;
                break;
            }
        }
        bool current_effect_found = false;
        for (std::size_t effect = 0; effect < state.outbox_effect_count; ++effect) {
            current_effect_found = current_effect_found ||
                                   (state.outbox_effects[effect].effect_id ==
                                        order.effect_id &&
                                    state.outbox_effects[effect].cluster_order_id ==
                                        order.cluster_order_id);
        }
        if (proposal == nullptr || !current_effect_found) return false;
        if (validate(*proposal) != ContractError::none) return false;
        std::size_t account = 0;
        while (account < state.risk_account_count &&
               (state.risk_accounts[account].account_id != proposal->account_id ||
                state.risk_accounts[account].instrument_id != proposal->instrument_id)) ++account;
        if (account == state.risk_account_count) return false;
        const auto& reservation = state.reservations[order.reservation_index];
        const auto quantity = totals[account].quantity.checked_add(reservation.remaining_quantity);
        const auto margin = totals[account].margin.checked_add(reservation.margin_hold);
        const auto fee = totals[account].fee.checked_add(reservation.fee_hold);
        if (!quantity || !margin || !fee) return false;
        totals[account] = {*quantity, *margin, *fee};
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (state.orders[prior].cluster_order_id == order.cluster_order_id ||
                state.orders[prior].effect_id == order.effect_id ||
                state.orders[prior].decision_id == order.decision_id) {
                return false;
            }
        }
    }

    for (std::size_t account = 0; account < state.risk_account_count; ++account) {
        if (totals[account].quantity != state.risk_accounts[account].reserved_quantity ||
            totals[account].margin != state.risk_accounts[account].reserved_margin ||
            totals[account].fee != state.risk_accounts[account].reserved_fee) return false;
    }

    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        const auto& effect = state.outbox_effects[index];
        if (effect.effect_id.value() == 0 || effect.cluster_order_id.value() == 0 ||
            effect.decision_id.value() == 0 || effect.payload_digest.is_zero() ||
            effect.effect_generation == 0 || !known(effect.state) ||
            !known(effect.operation)) {
            return false;
        }
        bool linked = false;
        for (std::size_t order = 0; order < state.order_count; ++order) {
            const auto& candidate = state.orders[order];
            linked = linked ||
                     candidate.cluster_order_id == effect.cluster_order_id;
        }
        bool decision_found = false;
        for (std::size_t decision = 0; decision < state.decision_count; ++decision) {
            decision_found = decision_found ||
                             state.decision_hot[decision].decision_id ==
                                 effect.decision_id;
        }
        if (!linked || !decision_found) return false;
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (state.outbox_effects[prior].effect_id == effect.effect_id) return false;
        }
    }

    for (std::size_t receipt_index = 0; receipt_index < state.receipt_count;
         ++receipt_index) {
        const auto& result = state.receipts[receipt_index].result;
        const bool applied_observation =
            result.code == ApplyCode::applied &&
            (result.detail_kind == ApplyResultDetailKind::fill ||
             result.detail_kind == ApplyResultDetailKind::venue_event);
        const bool state_changing_fill_refusal =
            result.detail_kind == ApplyResultDetailKind::fill &&
            (result.code == ApplyCode::fill_capacity_exceeded ||
             result.code == ApplyCode::fill_quantity_conflict);
        if (!applied_observation && !state_changing_fill_refusal) {
            continue;
        }

        if (result.code == ApplyCode::applied &&
            result.detail_kind == ApplyResultDetailKind::venue_event) {
            for (std::size_t effect_index = 0;
                 effect_index < state.outbox_effect_count; ++effect_index) {
                const auto& effect = state.outbox_effects[effect_index];
                if (effect.effect_id == result.event_effect_id() &&
                    effect.decision_id != result.decision_id) {
                    return false;
                }
            }
        }

        for (std::size_t order_index = 0; order_index < state.order_count;
             ++order_index) {
            const auto& order = state.orders[order_index];
            if (order.decision_id == result.decision_id &&
                order.last_evidence_state_version < result.state_version) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] std::size_t payload_size(const TradingState& state) noexcept {
    auto size = kStateSnapshotEmptyPayloadBytes;
    for (std::size_t index = 0; index < state.policy_count; ++index) {
        size += kStateSnapshotPolicyFixedBytes +
                state.policies[index].artifact.rule_count *
                    kStateSnapshotPolicyRuleBytes;
    }
    const auto bundle_size = [](const ExecutablePolicyBundle& bundle) noexcept {
        if (!bundle.present) return std::size_t{0};
        return kFeatureSchemaPayloadPrefixBytes +
                   bundle.feature_schema.feature_count *
                       kFeatureDescriptorEncodedBytes +
               kExecutablePolicyPayloadPrefixBytes +
                   bundle.executable_policy.template_count *
                       kActionTemplateEncodedBytes +
                   sizeof(std::uint16_t) +
                   bundle.executable_policy.rule_binding_count +
               kStateSnapshotInstrumentBytes + kStateSnapshotVenueBytes +
               kStateSnapshotExecutableBundleMetadataBytes;
    };
    size += bundle_size(state.active_executable_policy);
    size += bundle_size(state.staged_executable_policy);
    size += state.source_count * kStateSnapshotSourceBytes;
    size += state.source_event_count * kStateSnapshotSourceEventBytes;
    size += state.receipt_count * kStateSnapshotReceiptBytes;
    for (std::size_t index = 0; index < state.decision_count; ++index) {
        size += kStateSnapshotDecisionFixedBytes +
                state.decision_cold[index].frame.feature_count *
                    kStateSnapshotDecisionFeatureBytes;
        if (state.decision_cold[index].proposal != EconomicActionProposal{}) {
            size += kStateSnapshotDecisionProposalBytes;
        }
    }
    size += state.risk_account_count * kStateSnapshotRiskAccountBytes;
    size += state.reservation_count * kStateSnapshotRiskReservationBytes;
    size += state.order_count * kStateSnapshotClusterOrderBytes;
    size += state.outbox_effect_count * kStateSnapshotOutboxEffectBytes;
    size += state.caller_cursor_count * kStateSnapshotCallerCursorBytes;
    return size;
}

void write_decision_frame(Writer& writer, const DecisionFrame& value) noexcept {
    writer.integral(kDecisionFrameSchemaVersion);
    writer.integral(value.policy_id.value());
    writer.integral(value.policy_version.value());
    writer.integral(value.feature_schema_id.value());
    writer.integral(value.venue_id.value());
    writer.integral(value.venue_session_id.value());
    writer.integral(value.venue_sequence.value());
    writer.integral(value.source_id.value());
    writer.integral(value.source_generation.value());
    writer.integral(value.source_sequence.value());
    writer.integral(value.event_time_ns);
    writer.integral(value.receive_time_ns);
    writer.integral(value.instrument_id.value());
    writer.integral(value.account_id.value());
    writer.integral(value.feature_count);
    for (std::size_t index = 0; index < value.feature_count; ++index) {
        writer.integral(value.features[index].atoms());
    }
    writer.integral(static_cast<std::uint8_t>(value.market_data_status));
    writer.integral(value.market_data_age_ns);
    writer.integral(value.position_version.value());
    writer.integral(value.risk_version.value());
    writer.digest(value.raw_evidence_digest);
}

void write_price_source(Writer& writer, const PriceSource& value) noexcept {
    writer.integral(static_cast<std::uint8_t>(value.kind));
    writer.integral(value.feature_index);
    writer.integral(static_cast<std::uint8_t>(value.rounding));
    writer.integral(value.fixed_value.atoms());
    writer.integral(value.feature_atoms_per_tick);
    writer.integral(value.tick_offset.atoms());
}

void write_quantity_source(Writer& writer, const QuantitySource& value) noexcept {
    writer.integral(static_cast<std::uint8_t>(value.kind));
    writer.integral(value.feature_index);
    writer.integral(static_cast<std::uint8_t>(value.rounding));
    writer.integral(value.fixed_value.atoms());
    writer.integral(value.feature_atoms_per_lot);
}

void write_action_template(Writer& writer, const ActionTemplate& value) noexcept {
    writer.integral(value.schema_version);
    writer.integral(static_cast<std::uint8_t>(value.directive));
    writer.integral(static_cast<std::uint8_t>(value.side));
    writer.integral(static_cast<std::uint8_t>(value.time_in_force));
    writer.integral(static_cast<std::uint8_t>(value.post_only));
    writer.integral(value.order_slot.value());
    writer.integral(value.instrument_spec_version.value());
    writer.integral(value.venue_capabilities_version.value());
    write_price_source(writer, value.price_source);
    write_quantity_source(writer, value.quantity_source);
    writer.integral(value.safety_caps.max_quantity.atoms());
    writer.integral(value.safety_caps.max_notional.atoms());
    writer.integral(value.safety_caps.max_abs_price_deviation.atoms());
    writer.integral(value.safety_caps.max_abs_tick_offset.atoms());
    writer.integral(value.safety_caps.max_market_data_age_ns);
    write_price_source(writer, value.safety_caps.reference_price);
}

void write_feature_schema(Writer& writer,
                          const FeatureSchemaArtifact& value) noexcept {
    writer.integral(value.schema_version);
    writer.integral(value.feature_schema_id.value());
    writer.digest(value.artifact_digest);
    writer.integral(value.feature_count);
    for (std::size_t index = 0; index < value.feature_count; ++index) {
        writer.integral(static_cast<std::uint8_t>(value.features[index].kind));
        writer.integral(value.features[index].atoms_per_output_unit);
    }
}

void write_executable_policy(Writer& writer,
                             const ExecutablePolicyArtifact& value) noexcept {
    writer.integral(value.schema_version);
    writer.integral(value.policy_id.value());
    writer.integral(value.policy_version.value());
    writer.digest(value.policy_artifact_digest);
    writer.integral(value.feature_schema_id.value());
    writer.digest(value.feature_schema_digest);
    writer.digest(value.artifact_digest);
    writer.integral(value.template_count);
    for (std::size_t index = 0; index < value.template_count; ++index) {
        write_action_template(writer, value.templates[index]);
    }
    writer.integral(value.rule_binding_count);
    for (std::size_t index = 0; index < value.rule_binding_count; ++index) {
        writer.integral(value.rule_template_slots[index].value());
    }
}

void write_instrument(Writer& writer,
                      const InstrumentExecutionProfile& value) noexcept {
    writer.integral(value.schema_version);
    writer.integral(value.venue_id.value());
    writer.integral(value.instrument_id.value());
    writer.integral(value.instrument_spec_version.value());
    writer.integral(value.quote_micros_per_tick_lot.atoms());
    writer.integral(value.minimum_price.atoms());
    writer.integral(value.maximum_price.atoms());
    writer.integral(value.minimum_quantity.atoms());
    writer.integral(value.maximum_quantity.atoms());
}

void write_venue(Writer& writer, const VenueCapabilities& value) noexcept {
    writer.integral(value.schema_version);
    writer.integral(value.venue_id.value());
    writer.integral(value.venue_capabilities_version.value());
    writer.integral(value.directive_mask);
    writer.integral(value.time_in_force_mask);
    writer.integral(value.supports_post_only);
}

void write_bundle(Writer& writer, const ExecutablePolicyBundle& value) noexcept {
    writer.integral(static_cast<std::uint8_t>(value.present));
    if (!value.present) return;
    write_feature_schema(writer, value.feature_schema);
    write_executable_policy(writer, value.executable_policy);
    write_instrument(writer, value.instrument);
    write_venue(writer, value.venue);
    writer.integral(value.staged_state_version);
    writer.integral(value.activated_state_version);
}

void write_decision_proposal(Writer& writer,
                             const EconomicActionProposal& value) noexcept {
    const auto present = value != EconomicActionProposal{};
    writer.integral(static_cast<std::uint8_t>(present));
    if (!present) return;
    writer.integral(value.risk_policy_version.value());
    writer.integral(value.instrument_spec_version.value());
    writer.integral(value.venue_capabilities_version.value());
    writer.integral(value.order_slot.value());
    writer.integral(static_cast<std::uint8_t>(value.directive));
    writer.integral(static_cast<std::uint8_t>(value.side));
    writer.integral(static_cast<std::uint8_t>(value.time_in_force));
    writer.integral(static_cast<std::uint8_t>(value.post_only));
    writer.integral(value.price.atoms());
    writer.integral(value.quantity.atoms());
    writer.integral(value.notional.atoms());
}

void write_policy(Writer& writer, const PolicyRecord& record) noexcept {
    const auto& value = record.artifact;
    writer.integral(value.schema_version);
    writer.integral(value.policy_id.value());
    writer.integral(value.policy_version.value());
    writer.integral(value.feature_schema_id.value());
    writer.integral(value.instrument_id.value());
    writer.integral(value.account_id.value());
    writer.digest(value.model_digest);
    writer.digest(value.artifact_digest);
    writer.integral(value.valid_from_event_time_ns);
    writer.integral(value.valid_until_event_time_ns);
    writer.integral(value.risk_policy_version.value());
    writer.integral(value.rule_count);
    for (std::size_t index = 0; index < value.rule_count; ++index) {
        const auto& rule = value.rules[index];
        writer.integral(rule.feature_index);
        writer.integral(static_cast<std::uint8_t>(rule.predicate));
        writer.integral(static_cast<std::uint8_t>(rule.directive));
        writer.integral(rule.threshold.atoms());
    }
    writer.integral(static_cast<std::uint8_t>(record.lifecycle));
}

void write_result(Writer& writer, const ApplyResult& value) noexcept {
    writer.integral(static_cast<std::uint8_t>(value.code));
    writer.integral(static_cast<std::uint8_t>(value.contract_error));
    writer.integral(value.metadata_words[0]);
    writer.integral(value.metadata_words[1]);
    const auto lifecycle_and_detail = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(value.policy_lifecycle) |
        (static_cast<std::uint8_t>(value.detail_kind) << 3U));
    writer.integral(lifecycle_and_detail);
    writer.integral(value.state_version);
    writer.integral(static_cast<std::uint8_t>(value.strategy_mode));
    writer.integral(value.kill_epoch.value());
    writer.integral(value.executor_generation.value());
    writer.integral(value.detail.words[0]);
    writer.integral(value.detail.words[1]);
    writer.integral(value.detail.words[2]);
    writer.integral(value.decision_id.value());
    writer.integral(static_cast<std::uint8_t>(value.decision_code));
    writer.integral(static_cast<std::uint8_t>(value.proposed_directive));
    writer.integral(static_cast<std::uint8_t>(value.effective_directive));
    writer.integral(static_cast<std::uint8_t>(value.evaluation_mode));
    writer.integral(value.matched_rule_index);
    writer.integral(value.rules_visited);
    writer.integral(static_cast<std::uint8_t>(value.action_resolution_code));
}

void write_payload(Writer& writer, const TradingState& state) noexcept {
    writer.integral(state.state_version);
    writer.integral(static_cast<std::uint8_t>(state.strategy_mode));
    writer.integral(state.kill_epoch.value());
    writer.integral(state.executor_generation.value());
    writer.integral(state.reclaimed_group_count);
    writer.integral(state.settled_incomplete_effect_count);
    writer.integral(static_cast<std::uint16_t>(state.caller_cursor_count));
    for (std::size_t index = 0; index < state.caller_cursor_count; ++index) {
        writer.integral(state.caller_cursors[index].caller_id.value());
        writer.integral(state.caller_cursors[index].expired_below.value());
        writer.integral(
            state.caller_cursors[index]
                .gateway_results_acknowledged_through.value());
        writer.integral(static_cast<std::uint8_t>(state.caller_cursors[index].role));
    }

    writer.integral(static_cast<std::uint16_t>(state.policy_count));
    for (std::size_t index = 0; index < state.policy_count; ++index) {
        write_policy(writer, state.policies[index]);
    }

    write_bundle(writer, state.active_executable_policy);
    write_bundle(writer, state.staged_executable_policy);

    writer.integral(static_cast<std::uint16_t>(state.source_count));
    for (std::size_t index = 0; index < state.source_count; ++index) {
        const auto& source = state.sources[index];
        writer.integral(source.source_id.value());
        writer.integral(source.generation.value());
        writer.integral(source.sequence.value());
    }

    writer.integral(static_cast<std::uint16_t>(state.source_event_count));
    for (std::size_t index = 0; index < state.source_event_count; ++index) {
        const auto& event = state.source_events[index];
        writer.integral(event.source_id.value());
        writer.integral(event.generation.value());
        writer.integral(event.sequence.value());
        writer.digest(event.event_digest);
    }

    writer.integral(static_cast<std::uint16_t>(state.receipt_count));
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        const auto& receipt = state.receipts[index];
        writer.integral(receipt.identity.caller_id.value());
        writer.integral(receipt.identity.request_sequence.value());
        writer.digest(receipt.identity.payload_digest);
        write_result(writer, receipt.result);
    }

    writer.integral(static_cast<std::uint16_t>(state.decision_count));
    for (std::size_t index = 0; index < state.decision_count; ++index) {
        const auto& hot = state.decision_hot[index];
        const auto& cold = state.decision_cold[index];
        writer.integral(hot.decision_id.value());
        writer.integral(hot.state_version);
        writer.integral(hot.kill_epoch.value());
        writer.integral(hot.executor_generation.value());
        writer.integral(hot.matched_rule_index);
        writer.integral(hot.rules_visited);
        writer.integral(static_cast<std::uint8_t>(hot.code));
        writer.integral(static_cast<std::uint8_t>(hot.contract_error));
        writer.integral(static_cast<std::uint8_t>(hot.proposed_directive));
        writer.integral(static_cast<std::uint8_t>(hot.effective_directive));
        writer.integral(static_cast<std::uint8_t>(hot.evaluation_mode));
        writer.integral(static_cast<std::uint8_t>(hot.strategy_mode));
        writer.integral(static_cast<std::uint8_t>(hot.action_resolution_code));
        writer.digest(cold.command_payload_digest);
        write_decision_frame(writer, cold.frame);
        write_decision_proposal(writer, cold.proposal);
    }

    writer.integral(static_cast<std::uint16_t>(state.risk_account_count));
    for (std::size_t index = 0; index < state.risk_account_count; ++index) {
        const auto& risk = state.risk_accounts[index];
        writer.integral(risk.account_id.value());
        writer.integral(risk.instrument_id.value());
        writer.integral(risk.risk_policy_version.value());
        writer.integral(risk.risk_version.value());
        writer.integral(risk.position_version.value());
        writer.integral(risk.max_order_quantity.atoms());
        writer.integral(risk.max_reserved_quantity.atoms());
        writer.integral(risk.reserved_quantity.atoms());
        writer.integral(risk.max_order_gross_notional.atoms());
        writer.integral(risk.max_reserved_exposure.atoms());
        writer.integral(risk.reserved_margin.atoms());
        writer.integral(risk.reserved_fee.atoms());
        writer.integral(risk.initial_margin_ppm);
        writer.integral(risk.maximum_fee_ppm);
        writer.integral(risk.position_quantity.atoms());
        writer.integral(risk.max_position_quantity.atoms());
        writer.integral(static_cast<std::uint8_t>(risk.allow_short));
        writer.integral(risk.cash.atoms());
        writer.integral(risk.cost_basis.atoms());
        writer.integral(risk.realized_pnl.atoms());
        writer.integral(risk.fees_paid.atoms());
        writer.integral(risk.window_realized_pnl.atoms());
        writer.integral(risk.max_window_loss.atoms());
    }

    writer.integral(static_cast<std::uint16_t>(state.reservation_count));
    for (std::size_t index = 0; index < state.reservation_count; ++index) {
        const auto& reservation = state.reservations[index];
        writer.integral(reservation.cluster_order_id.value());
        writer.integral(reservation.original_quantity.atoms());
        writer.integral(reservation.filled_quantity.atoms());
        writer.integral(reservation.remaining_quantity.atoms());
        writer.integral(reservation.remaining_notional.atoms());
        writer.integral(reservation.margin_hold.atoms());
        writer.integral(reservation.fee_hold.atoms());
        writer.integral(static_cast<std::uint8_t>(reservation.state));
    }

    writer.integral(static_cast<std::uint16_t>(state.order_count));
    for (std::size_t index = 0; index < state.order_count; ++index) {
        const auto& order = state.orders[index];
        writer.integral(order.cluster_order_id.value());
        writer.integral(order.effect_id.value());
        writer.integral(order.decision_id.value());
        writer.integral(order.authorization_state_version);
        writer.integral(order.reservation_index);
        writer.integral(static_cast<std::uint8_t>(order.state));
        writer.integral(order.last_evidence_state_version);
    }

    writer.integral(static_cast<std::uint16_t>(state.outbox_effect_count));
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        const auto& effect = state.outbox_effects[index];
        writer.integral(effect.effect_id.value());
        writer.integral(effect.cluster_order_id.value());
        writer.integral(effect.decision_id.value());
        writer.digest(effect.payload_digest);
        writer.integral(effect.effect_generation);
        writer.integral(static_cast<std::uint8_t>(effect.state));
        writer.integral(static_cast<std::uint8_t>(effect.operation));
    }
    writer.digest(state.execution_catalog_root);
}

[[nodiscard]] PolicyRecord read_policy(Reader& reader) noexcept {
    PolicyRecord record;
    auto& value = record.artifact;
    value.schema_version = reader.integral<std::uint16_t>();
    value.policy_id = PolicyId{reader.integral<std::uint64_t>()};
    value.policy_version = PolicyVersion{reader.integral<std::uint64_t>()};
    value.feature_schema_id = FeatureSchemaId{reader.integral<std::uint64_t>()};
    value.instrument_id = InstrumentId{reader.integral<std::uint64_t>()};
    value.account_id = AccountId{reader.integral<std::uint64_t>()};
    value.model_digest = reader.digest();
    value.artifact_digest = reader.digest();
    value.valid_from_event_time_ns = reader.integral<std::int64_t>();
    value.valid_until_event_time_ns = reader.integral<std::int64_t>();
    value.risk_policy_version = RiskPolicyVersion{reader.integral<std::uint64_t>()};
    value.rule_count = reader.integral<std::uint16_t>();
    if (value.rule_count > kMaxPolicyRules) return record;
    for (std::size_t index = 0; index < value.rule_count; ++index) {
        auto& rule = value.rules[index];
        rule.feature_index = reader.integral<std::uint16_t>();
        rule.predicate =
            static_cast<PredicateOperator>(reader.integral<std::uint8_t>());
        rule.directive = static_cast<Directive>(reader.integral<std::uint8_t>());
        rule.threshold = FeatureMicros::from_atoms(reader.integral<std::int64_t>());
    }
    record.lifecycle =
        static_cast<PolicyLifecycle>(reader.integral<std::uint8_t>());
    return record;
}

[[nodiscard]] bool read_decision_frame(Reader& reader,
                                       DecisionFrame& value) noexcept {
    if (reader.integral<std::uint16_t>() != kDecisionFrameSchemaVersion) {
        return false;
    }
    value.policy_id = PolicyId{reader.integral<std::uint64_t>()};
    value.policy_version = PolicyVersion{reader.integral<std::uint64_t>()};
    value.feature_schema_id = FeatureSchemaId{reader.integral<std::uint64_t>()};
    value.venue_id = VenueId{reader.integral<std::uint64_t>()};
    value.venue_session_id = VenueSessionId{reader.integral<std::uint64_t>()};
    value.venue_sequence = VenueSequence{reader.integral<std::uint64_t>()};
    value.source_id = SourceId{reader.integral<std::uint64_t>()};
    value.source_generation = SourceGeneration{reader.integral<std::uint64_t>()};
    value.source_sequence = SourceSequence{reader.integral<std::uint64_t>()};
    value.event_time_ns = reader.integral<std::int64_t>();
    value.receive_time_ns = reader.integral<std::int64_t>();
    value.instrument_id = InstrumentId{reader.integral<std::uint64_t>()};
    value.account_id = AccountId{reader.integral<std::uint64_t>()};
    value.feature_count = reader.integral<std::uint16_t>();
    if (value.feature_count > kMaxDecisionFeatures) return false;
    for (std::size_t index = 0; index < value.feature_count; ++index) {
        value.features[index] =
            FeatureMicros::from_atoms(reader.integral<std::int64_t>());
    }
    value.market_data_status =
        static_cast<MarketDataStatus>(reader.integral<std::uint8_t>());
    value.market_data_age_ns = reader.integral<std::int64_t>();
    value.position_version = PositionVersion{reader.integral<std::uint64_t>()};
    value.risk_version = RiskVersion{reader.integral<std::uint64_t>()};
    value.raw_evidence_digest = reader.digest();
    return true;
}

[[nodiscard]] PriceSource read_price_source(Reader& reader) noexcept {
    PriceSource value;
    value.kind = static_cast<EconomicValueSourceKind>(reader.integral<std::uint8_t>());
    value.feature_index = reader.integral<std::uint16_t>();
    value.rounding = static_cast<RoundingMode>(reader.integral<std::uint8_t>());
    value.fixed_value = PriceTicks::from_atoms(reader.integral<std::int64_t>());
    value.feature_atoms_per_tick = reader.integral<std::int64_t>();
    value.tick_offset = PriceTicks::from_atoms(reader.integral<std::int64_t>());
    return value;
}

[[nodiscard]] QuantitySource read_quantity_source(Reader& reader) noexcept {
    QuantitySource value;
    value.kind = static_cast<EconomicValueSourceKind>(reader.integral<std::uint8_t>());
    value.feature_index = reader.integral<std::uint16_t>();
    value.rounding = static_cast<RoundingMode>(reader.integral<std::uint8_t>());
    value.fixed_value = QuantityLots::from_atoms(reader.integral<std::int64_t>());
    value.feature_atoms_per_lot = reader.integral<std::int64_t>();
    return value;
}

[[nodiscard]] ActionTemplate read_action_template(Reader& reader) noexcept {
    ActionTemplate value;
    value.schema_version = reader.integral<std::uint16_t>();
    value.directive = static_cast<Directive>(reader.integral<std::uint8_t>());
    value.side = static_cast<Side>(reader.integral<std::uint8_t>());
    value.time_in_force = static_cast<TimeInForce>(reader.integral<std::uint8_t>());
    value.post_only = static_cast<PostOnlyMode>(reader.integral<std::uint8_t>());
    value.order_slot = LogicalOrderSlot{reader.integral<std::uint16_t>()};
    value.instrument_spec_version =
        InstrumentSpecVersion{reader.integral<std::uint64_t>()};
    value.venue_capabilities_version =
        VenueCapabilitiesVersion{reader.integral<std::uint64_t>()};
    value.price_source = read_price_source(reader);
    value.quantity_source = read_quantity_source(reader);
    value.safety_caps.max_quantity =
        QuantityLots::from_atoms(reader.integral<std::int64_t>());
    value.safety_caps.max_notional =
        MoneyMicros::from_atoms(reader.integral<std::int64_t>());
    value.safety_caps.max_abs_price_deviation =
        PriceTicks::from_atoms(reader.integral<std::int64_t>());
    value.safety_caps.max_abs_tick_offset =
        PriceTicks::from_atoms(reader.integral<std::int64_t>());
    value.safety_caps.max_market_data_age_ns = reader.integral<std::int64_t>();
    value.safety_caps.reference_price = read_price_source(reader);
    return value;
}

[[nodiscard]] bool read_feature_schema(Reader& reader,
                                       FeatureSchemaArtifact& value) noexcept {
    value.schema_version = reader.integral<std::uint16_t>();
    value.feature_schema_id = FeatureSchemaId{reader.integral<std::uint64_t>()};
    value.artifact_digest = reader.digest();
    value.feature_count = reader.integral<std::uint16_t>();
    if (value.feature_count > kMaxDecisionFeatures) return false;
    for (std::size_t index = 0; index < value.feature_count; ++index) {
        value.features[index].kind =
            static_cast<FeatureEconomicKind>(reader.integral<std::uint8_t>());
        value.features[index].atoms_per_output_unit =
            reader.integral<std::int64_t>();
    }
    return true;
}

[[nodiscard]] bool read_executable_policy(
    Reader& reader, ExecutablePolicyArtifact& value) noexcept {
    value.schema_version = reader.integral<std::uint16_t>();
    value.policy_id = PolicyId{reader.integral<std::uint64_t>()};
    value.policy_version = PolicyVersion{reader.integral<std::uint64_t>()};
    value.policy_artifact_digest = reader.digest();
    value.feature_schema_id = FeatureSchemaId{reader.integral<std::uint64_t>()};
    value.feature_schema_digest = reader.digest();
    value.artifact_digest = reader.digest();
    value.template_count = reader.integral<std::uint16_t>();
    if (value.template_count > kMaxExecutableActionTemplates) return false;
    for (std::size_t index = 0; index < value.template_count; ++index) {
        value.templates[index] = read_action_template(reader);
    }
    value.rule_binding_count = reader.integral<std::uint16_t>();
    if (value.rule_binding_count > kMaxPolicyRules) return false;
    for (std::size_t index = 0; index < value.rule_binding_count; ++index) {
        value.rule_template_slots[index] =
            ActionTemplateSlot{reader.integral<std::uint8_t>()};
    }
    return true;
}

[[nodiscard]] InstrumentExecutionProfile read_instrument(Reader& reader) noexcept {
    InstrumentExecutionProfile value;
    value.schema_version = reader.integral<std::uint16_t>();
    value.venue_id = VenueId{reader.integral<std::uint64_t>()};
    value.instrument_id = InstrumentId{reader.integral<std::uint64_t>()};
    value.instrument_spec_version =
        InstrumentSpecVersion{reader.integral<std::uint64_t>()};
    value.quote_micros_per_tick_lot =
        MoneyMicros::from_atoms(reader.integral<std::int64_t>());
    value.minimum_price = PriceTicks::from_atoms(reader.integral<std::int64_t>());
    value.maximum_price = PriceTicks::from_atoms(reader.integral<std::int64_t>());
    value.minimum_quantity =
        QuantityLots::from_atoms(reader.integral<std::int64_t>());
    value.maximum_quantity =
        QuantityLots::from_atoms(reader.integral<std::int64_t>());
    return value;
}

[[nodiscard]] VenueCapabilities read_venue(Reader& reader) noexcept {
    VenueCapabilities value;
    value.schema_version = reader.integral<std::uint16_t>();
    value.venue_id = VenueId{reader.integral<std::uint64_t>()};
    value.venue_capabilities_version =
        VenueCapabilitiesVersion{reader.integral<std::uint64_t>()};
    value.directive_mask = reader.integral<std::uint8_t>();
    value.time_in_force_mask = reader.integral<std::uint8_t>();
    value.supports_post_only = reader.integral<std::uint8_t>();
    return value;
}

[[nodiscard]] bool read_bundle(Reader& reader,
                               ExecutablePolicyBundle& value) noexcept {
    const auto present = reader.integral<std::uint8_t>();
    if (present > 1) return false;
    value.present = present == 1;
    if (!value.present) return true;
    if (!read_feature_schema(reader, value.feature_schema) ||
        !read_executable_policy(reader, value.executable_policy)) {
        return false;
    }
    value.instrument = read_instrument(reader);
    value.venue = read_venue(reader);
    value.staged_state_version = reader.integral<std::uint64_t>();
    value.activated_state_version = reader.integral<std::uint64_t>();
    return true;
}

[[nodiscard]] bool read_decision_proposal(
    Reader& reader,
    const DecisionRecordHot& hot,
    const DecisionFrame& frame,
    EconomicActionProposal& value) noexcept {
    const auto present = reader.integral<std::uint8_t>();
    if (present > 1) return false;
    if (present == 0) {
        value = {};
        return true;
    }
    value.schema_version = kEconomicActionProposalSchemaVersion;
    value.decision_id = hot.decision_id;
    value.policy_id = frame.policy_id;
    value.policy_version = frame.policy_version;
    value.risk_policy_version =
        RiskPolicyVersion{reader.integral<std::uint64_t>()};
    value.instrument_spec_version =
        InstrumentSpecVersion{reader.integral<std::uint64_t>()};
    value.venue_capabilities_version =
        VenueCapabilitiesVersion{reader.integral<std::uint64_t>()};
    value.account_id = frame.account_id;
    value.instrument_id = frame.instrument_id;
    value.venue_id = frame.venue_id;
    value.order_slot = LogicalOrderSlot{reader.integral<std::uint16_t>()};
    value.directive = static_cast<Directive>(reader.integral<std::uint8_t>());
    value.side = static_cast<Side>(reader.integral<std::uint8_t>());
    value.time_in_force = static_cast<TimeInForce>(reader.integral<std::uint8_t>());
    value.post_only = static_cast<PostOnlyMode>(reader.integral<std::uint8_t>());
    value.price = PriceTicks::from_atoms(reader.integral<std::int64_t>());
    value.quantity = QuantityLots::from_atoms(reader.integral<std::int64_t>());
    value.notional = MoneyMicros::from_atoms(reader.integral<std::int64_t>());
    return true;
}

[[nodiscard]] ApplyResult read_result(Reader& reader) noexcept {
    ApplyResult value;
    value.code = static_cast<ApplyCode>(reader.integral<std::uint8_t>());
    value.contract_error =
        static_cast<ContractError>(reader.integral<std::uint8_t>());
    value.metadata_words[0] = reader.integral<std::uint64_t>();
    value.metadata_words[1] = reader.integral<std::uint64_t>();
    const auto lifecycle_and_detail = reader.integral<std::uint8_t>();
    value.policy_lifecycle =
        static_cast<PolicyLifecycle>(lifecycle_and_detail & 0x07U);
    value.detail_kind =
        static_cast<ApplyResultDetailKind>(lifecycle_and_detail >> 3U);
    value.state_version = reader.integral<std::uint64_t>();
    value.strategy_mode =
        static_cast<StrategyMode>(reader.integral<std::uint8_t>());
    value.kill_epoch = KillEpoch{reader.integral<std::uint64_t>()};
    value.executor_generation =
        ExecutorGeneration{reader.integral<std::uint64_t>()};
    value.detail.words[0] = reader.integral<std::uint64_t>();
    value.detail.words[1] = reader.integral<std::uint64_t>();
    value.detail.words[2] = reader.integral<std::uint64_t>();
    value.decision_id = DecisionId{reader.integral<std::uint64_t>()};
    value.decision_code =
        static_cast<DecisionCode>(reader.integral<std::uint8_t>());
    value.proposed_directive =
        static_cast<Directive>(reader.integral<std::uint8_t>());
    value.effective_directive =
        static_cast<Directive>(reader.integral<std::uint8_t>());
    value.evaluation_mode =
        static_cast<EvaluationMode>(reader.integral<std::uint8_t>());
    value.matched_rule_index = reader.integral<std::uint16_t>();
    value.rules_visited = reader.integral<std::uint16_t>();
    value.action_resolution_code =
        static_cast<ActionResolutionCode>(reader.integral<std::uint8_t>());
    return value;
}

[[nodiscard]] bool read_payload(const std::span<const std::byte> payload,
                                TradingState& state) noexcept {
    Reader reader(payload);
    state = {};
    state.state_version = reader.integral<std::uint64_t>();
    state.strategy_mode =
        static_cast<StrategyMode>(reader.integral<std::uint8_t>());
    state.kill_epoch = KillEpoch{reader.integral<std::uint64_t>()};
    state.executor_generation =
        ExecutorGeneration{reader.integral<std::uint64_t>()};
    state.reclaimed_group_count = reader.integral<std::uint64_t>();
    state.settled_incomplete_effect_count = reader.integral<std::uint64_t>();
    state.caller_cursor_count = reader.integral<std::uint16_t>();
    if (state.caller_cursor_count > state.caller_cursors.size()) return false;
    for (std::size_t index = 0; index < state.caller_cursor_count; ++index) {
        state.caller_cursors[index].caller_id =
            CallerId{reader.integral<std::uint64_t>()};
        state.caller_cursors[index].expired_below =
            RequestSequence{reader.integral<std::uint64_t>()};
        state.caller_cursors[index].gateway_results_acknowledged_through =
            RequestSequence{reader.integral<std::uint64_t>()};
        const auto role = reader.integral<std::uint8_t>();
        if (role > static_cast<std::uint8_t>(CallerRole::gateway)) return false;
        state.caller_cursors[index].role = static_cast<CallerRole>(role);
    }

    state.policy_count = reader.integral<std::uint16_t>();
    if (state.policy_count > state.policies.size()) return false;
    for (std::size_t index = 0; index < state.policy_count; ++index) {
        state.policies[index] = read_policy(reader);
        if (state.policies[index].artifact.rule_count > kMaxPolicyRules) return false;
    }

    if (!read_bundle(reader, state.active_executable_policy) ||
        !read_bundle(reader, state.staged_executable_policy)) {
        return false;
    }

    state.source_count = reader.integral<std::uint16_t>();
    if (state.source_count > state.sources.size()) return false;
    for (std::size_t index = 0; index < state.source_count; ++index) {
        state.sources[index] =
            SourceCursor{SourceId{reader.integral<std::uint64_t>()},
                         SourceGeneration{reader.integral<std::uint64_t>()},
                         SourceSequence{reader.integral<std::uint64_t>()}};
    }

    state.source_event_count = reader.integral<std::uint16_t>();
    if (state.source_event_count > state.source_events.size()) return false;
    for (std::size_t index = 0; index < state.source_event_count; ++index) {
        state.source_events[index] =
            SourceEventReceipt{SourceId{reader.integral<std::uint64_t>()},
                               SourceGeneration{reader.integral<std::uint64_t>()},
                               SourceSequence{reader.integral<std::uint64_t>()},
                               reader.digest()};
    }

    state.receipt_count = reader.integral<std::uint16_t>();
    if (state.receipt_count > state.receipts.size()) return false;
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        state.receipts[index] =
            RequestReceipt{CommandIdentity{CallerId{reader.integral<std::uint64_t>()},
                                           RequestSequence{
                                               reader.integral<std::uint64_t>()},
                                           reader.digest()},
                           read_result(reader)};
    }

    state.decision_count = reader.integral<std::uint16_t>();
    if (state.decision_count > state.decision_hot.size() ||
        state.decision_count > state.decision_cold.size()) {
        return false;
    }
    for (std::size_t index = 0; index < state.decision_count; ++index) {
        auto& hot = state.decision_hot[index];
        auto& cold = state.decision_cold[index];
        hot.decision_id = DecisionId{reader.integral<std::uint64_t>()};
        hot.state_version = reader.integral<std::uint64_t>();
        hot.kill_epoch = KillEpoch{reader.integral<std::uint64_t>()};
        hot.executor_generation =
            ExecutorGeneration{reader.integral<std::uint64_t>()};
        hot.matched_rule_index = reader.integral<std::uint16_t>();
        hot.rules_visited = reader.integral<std::uint16_t>();
        hot.code = static_cast<DecisionCode>(reader.integral<std::uint8_t>());
        hot.contract_error =
            static_cast<ContractError>(reader.integral<std::uint8_t>());
        hot.proposed_directive =
            static_cast<Directive>(reader.integral<std::uint8_t>());
        hot.effective_directive =
            static_cast<Directive>(reader.integral<std::uint8_t>());
        hot.evaluation_mode =
            static_cast<EvaluationMode>(reader.integral<std::uint8_t>());
        hot.strategy_mode =
            static_cast<StrategyMode>(reader.integral<std::uint8_t>());
        hot.action_resolution_code =
            static_cast<ActionResolutionCode>(reader.integral<std::uint8_t>());
        cold.decision_id = hot.decision_id;
        cold.command_payload_digest = reader.digest();
        if (!read_decision_frame(reader, cold.frame)) return false;
        hot.policy_id = cold.frame.policy_id;
        hot.policy_version = cold.frame.policy_version;
        if (!read_decision_proposal(reader, hot, cold.frame, cold.proposal)) {
            return false;
        }
    }

    state.risk_account_count = reader.integral<std::uint16_t>();
    if (state.risk_account_count > state.risk_accounts.size()) return false;
    for (std::size_t index = 0; index < state.risk_account_count; ++index) {
        auto& risk = state.risk_accounts[index];
        risk.account_id = AccountId{reader.integral<std::uint64_t>()};
        risk.instrument_id = InstrumentId{reader.integral<std::uint64_t>()};
        risk.risk_policy_version =
            RiskPolicyVersion{reader.integral<std::uint64_t>()};
        risk.risk_version = RiskVersion{reader.integral<std::uint64_t>()};
        risk.position_version = PositionVersion{reader.integral<std::uint64_t>()};
        risk.max_order_quantity =
            QuantityLots::from_atoms(reader.integral<std::int64_t>());
        risk.max_reserved_quantity =
            QuantityLots::from_atoms(reader.integral<std::int64_t>());
        risk.reserved_quantity =
            QuantityLots::from_atoms(reader.integral<std::int64_t>());
        risk.max_order_gross_notional =
            MoneyMicros::from_atoms(reader.integral<std::int64_t>());
        risk.max_reserved_exposure =
            MoneyMicros::from_atoms(reader.integral<std::int64_t>());
        risk.reserved_margin =
            MoneyMicros::from_atoms(reader.integral<std::int64_t>());
        risk.reserved_fee =
            MoneyMicros::from_atoms(reader.integral<std::int64_t>());
        risk.initial_margin_ppm = reader.integral<std::uint32_t>();
        risk.maximum_fee_ppm = reader.integral<std::uint32_t>();
        risk.position_quantity =
            QuantityLots::from_atoms(reader.integral<std::int64_t>());
        risk.max_position_quantity =
            QuantityLots::from_atoms(reader.integral<std::int64_t>());
        const auto allow_short = reader.integral<std::uint8_t>();
        if (allow_short > 1) return false;
        risk.allow_short = allow_short == 1;
        risk.cash = MoneyMicros::from_atoms(reader.integral<std::int64_t>());
        risk.cost_basis = MoneyMicros::from_atoms(reader.integral<std::int64_t>());
        risk.realized_pnl = MoneyMicros::from_atoms(reader.integral<std::int64_t>());
        risk.fees_paid = MoneyMicros::from_atoms(reader.integral<std::int64_t>());
        risk.window_realized_pnl = MoneyMicros::from_atoms(reader.integral<std::int64_t>());
        risk.max_window_loss = MoneyMicros::from_atoms(reader.integral<std::int64_t>());
    }

    state.reservation_count = reader.integral<std::uint16_t>();
    if (state.reservation_count > state.reservations.size()) return false;
    for (std::size_t index = 0; index < state.reservation_count; ++index) {
        auto& reservation = state.reservations[index];
        reservation.cluster_order_id =
            ClusterOrderId{reader.integral<std::uint64_t>()};
        reservation.original_quantity =
            QuantityLots::from_atoms(reader.integral<std::int64_t>());
        reservation.filled_quantity =
            QuantityLots::from_atoms(reader.integral<std::int64_t>());
        reservation.remaining_quantity =
            QuantityLots::from_atoms(reader.integral<std::int64_t>());
        reservation.remaining_notional =
            MoneyMicros::from_atoms(reader.integral<std::int64_t>());
        reservation.margin_hold =
            MoneyMicros::from_atoms(reader.integral<std::int64_t>());
        reservation.fee_hold =
            MoneyMicros::from_atoms(reader.integral<std::int64_t>());
        reservation.state =
            static_cast<ReservationState>(reader.integral<std::uint8_t>());
    }

    state.order_count = reader.integral<std::uint16_t>();
    if (state.order_count > state.orders.size()) return false;
    for (std::size_t index = 0; index < state.order_count; ++index) {
        auto& order = state.orders[index];
        order.cluster_order_id = ClusterOrderId{reader.integral<std::uint64_t>()};
        order.effect_id = EffectId{reader.integral<std::uint64_t>()};
        order.decision_id = DecisionId{reader.integral<std::uint64_t>()};
        order.authorization_state_version = reader.integral<std::uint64_t>();
        order.reservation_index = reader.integral<std::uint16_t>();
        order.state =
            static_cast<ClusterOrderState>(reader.integral<std::uint8_t>());
        order.last_evidence_state_version = reader.integral<std::uint64_t>();
    }

    state.outbox_effect_count = reader.integral<std::uint16_t>();
    if (state.outbox_effect_count > state.outbox_effects.size()) return false;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        auto& effect = state.outbox_effects[index];
        effect.effect_id = EffectId{reader.integral<std::uint64_t>()};
        effect.cluster_order_id =
            ClusterOrderId{reader.integral<std::uint64_t>()};
        effect.decision_id = DecisionId{reader.integral<std::uint64_t>()};
        effect.payload_digest = reader.digest();
        effect.effect_generation = reader.integral<std::uint32_t>();
        effect.state = static_cast<OutboxState>(reader.integral<std::uint8_t>());
        effect.operation =
            static_cast<EffectOperation>(reader.integral<std::uint8_t>());
    }
    state.execution_catalog_root = reader.digest();
    return reader.finished() && valid_state(state);
}

[[nodiscard]] std::uint32_t read_u32(const std::span<const std::byte> bytes,
                                     const std::size_t offset) noexcept {
    std::uint32_t value{};
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<unsigned char>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint16_t read_u16(const std::span<const std::byte> bytes,
                                     const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        std::to_integer<unsigned char>(bytes[offset]) |
        (std::to_integer<unsigned char>(bytes[offset + 1]) << 8U));
}

}

EncodeStateSnapshotResult encode_state_snapshot(
    const TradingState& state, const std::span<std::byte> output) noexcept {
    if (!valid_state(state)) return {StateSnapshotError::invalid_state, 0, {}};

    const auto payload_bytes = payload_size(state);
    const auto frame_bytes = kStateSnapshotPayloadOffset + payload_bytes + kDigestBytes;
    if (output.size() < frame_bytes) {
        return {StateSnapshotError::output_too_small, 0, {}};
    }

    std::copy(kSnapshotMagic.begin(), kSnapshotMagic.end(), output.begin());
    Writer writer(output.subspan(kStateSnapshotMagicBytes));
    writer.integral(static_cast<std::uint32_t>(payload_bytes));
    writer.integral(kStateSnapshotSchemaVersion);
    write_payload(writer, state);

    const auto digest_end = kStateSnapshotPayloadOffset + payload_bytes;
    const auto state_digest = sha256_digest(output.subspan(
        kStateSnapshotDigestInputOffset,
        digest_end - kStateSnapshotDigestInputOffset));
    std::copy(state_digest.bytes.begin(), state_digest.bytes.end(),
              output.begin() + static_cast<std::ptrdiff_t>(digest_end));
    return {StateSnapshotError::none, frame_bytes, state_digest};
}

DecodeStateSnapshotResult decode_state_snapshot(
    const std::span<const std::byte> snapshot,
    TradingState& destination,
    StateSnapshotScratch& scratch) noexcept {
    constexpr auto minimum_frame = kStateSnapshotPayloadOffset + kDigestBytes;
    if (snapshot.size() < minimum_frame) {
        return {StateSnapshotError::truncated, {}};
    }
    if (!std::equal(kSnapshotMagic.begin(), kSnapshotMagic.end(), snapshot.begin())) {
        return {StateSnapshotError::invalid_magic, {}};
    }

    if (read_u16(snapshot, kStateSnapshotSchemaOffset) !=
        kStateSnapshotSchemaVersion) {
        return {StateSnapshotError::unsupported_schema, {}};
    }

    const auto payload_bytes = read_u32(snapshot, kStateSnapshotMagicBytes);
    if (payload_bytes > kMaxStateSnapshotPayloadBytes) {
        return {StateSnapshotError::length_mismatch, {}};
    }
    const auto expected_size =
        kStateSnapshotPayloadOffset + static_cast<std::size_t>(payload_bytes) +
        kDigestBytes;
    if (snapshot.size() < expected_size) {
        return {StateSnapshotError::truncated, {}};
    }
    if (snapshot.size() != expected_size) {
        return {StateSnapshotError::length_mismatch, {}};
    }

    const auto digest_offset = expected_size - kDigestBytes;
    const auto calculated = sha256_digest(snapshot.subspan(
        kStateSnapshotDigestInputOffset,
        digest_offset - kStateSnapshotDigestInputOffset));
    Digest256 retained;
    std::copy_n(snapshot.begin() + static_cast<std::ptrdiff_t>(digest_offset),
                retained.bytes.size(), retained.bytes.begin());
    if (calculated != retained) {
        return {StateSnapshotError::digest_mismatch, {}};
    }

    const auto payload = snapshot.subspan(kStateSnapshotPayloadOffset, payload_bytes);
    if (!read_payload(payload, scratch.candidate)) {
        return {StateSnapshotError::invalid_state, {}};
    }

    destination = scratch.candidate;
    return {StateSnapshotError::none, calculated};
}

}
