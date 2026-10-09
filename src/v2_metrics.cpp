#include "velocita/v2/metrics.hpp"

#include <algorithm>

namespace velocita::v2 {
namespace {

[[nodiscard]] bool order_holds_unresolved_risk(const TradingState& state,
                                               const ClusterOrder& order) noexcept {
    if (order.reservation_index >= state.reservation_count) return true;
    return state.reservations[order.reservation_index].state !=
           ReservationState::released;
}

[[nodiscard]] bool effect_is_unsettled(const OutboxState state) noexcept {
    return state != OutboxState::acknowledged && state != OutboxState::rejected &&
           state != OutboxState::conflict && state != OutboxState::incomplete;
}

[[nodiscard]] std::uint64_t age_in_versions(const std::uint64_t now,
                                            const std::uint64_t then) noexcept {
    return now > then ? now - then : 0;
}

}

TradingMetrics collect_metrics(const TradingState& state) noexcept {
    TradingMetrics metrics;
    metrics.state_version = state.state_version;
    metrics.decision_count = static_cast<std::uint16_t>(state.decision_count);
    metrics.order_count = static_cast<std::uint16_t>(state.order_count);
    metrics.reservation_count = static_cast<std::uint16_t>(state.reservation_count);
    metrics.outbox_effect_count =
        static_cast<std::uint16_t>(state.outbox_effect_count);
    metrics.receipt_count = static_cast<std::uint16_t>(state.receipt_count);
    metrics.risk_account_count =
        static_cast<std::uint16_t>(state.risk_account_count);
    metrics.source_event_count =
        static_cast<std::uint16_t>(state.source_event_count);
    metrics.caller_cursor_count =
        static_cast<std::uint16_t>(state.caller_cursor_count);
    metrics.caller_cursor_headroom = static_cast<std::uint16_t>(
        kMaxCallerCursors - std::min(state.caller_cursor_count,
                                     kMaxCallerCursors));
    metrics.reclaimed_group_count = state.reclaimed_group_count;
    metrics.settled_incomplete_effect_count = state.settled_incomplete_effect_count;
    metrics.strategy_mode = state.strategy_mode;
    metrics.kill_epoch = state.kill_epoch;
    metrics.executor_generation = state.executor_generation;

    for (std::size_t index = 0; index < state.order_count; ++index) {
        const auto& order = state.orders[index];
        const auto slot = static_cast<std::size_t>(order.state);
        if (slot < metrics.orders_by_state.size()) ++metrics.orders_by_state[slot];

        const auto age =
            age_in_versions(state.state_version, order.authorization_state_version);
        if (order_holds_unresolved_risk(state, order)) {
            ++metrics.unresolved_orders;
            metrics.oldest_unresolved_order_age =
                std::max(metrics.oldest_unresolved_order_age, age);
        }
        if (order.state == ClusterOrderState::reconciling) {
            metrics.oldest_reconciling_order_age_since_authorization =
                std::max(
                    metrics.oldest_reconciling_order_age_since_authorization,
                    age);
        }
        if (order.state == ClusterOrderState::conflict) {
            metrics.oldest_conflicted_order_age =
                std::max(metrics.oldest_conflicted_order_age, age);
        }

        for (std::size_t slot_index = 0; slot_index < state.decision_count;
             ++slot_index) {
            if (state.decision_hot[slot_index].decision_id != order.decision_id) {
                continue;
            }
            metrics.max_decision_to_authorization_distance =
                std::max(metrics.max_decision_to_authorization_distance,
                         age_in_versions(order.authorization_state_version,
                                         state.decision_hot[slot_index].state_version));
        }
    }

    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        const auto& effect = state.outbox_effects[index];
        const auto slot = static_cast<std::size_t>(effect.state);
        if (slot < metrics.effects_by_state.size()) ++metrics.effects_by_state[slot];
        if (!effect_is_unsettled(effect.state)) continue;
        ++metrics.unsettled_effects;
        for (std::size_t receipt = 0; receipt < state.receipt_count; ++receipt) {
            const auto& result = state.receipts[receipt].result;
            if (result.code != ApplyCode::applied ||
                result.detail_kind != ApplyResultDetailKind::authorization ||
                result.effect_id() != effect.effect_id) {
                continue;
            }
            metrics.oldest_unsettled_effect_age =
                std::max(metrics.oldest_unsettled_effect_age,
                         age_in_versions(state.state_version,
                                         result.state_version));
            break;
        }
    }

    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        const auto& result = state.receipts[index].result;
        metrics.accepted_venue_event_count +=
            result.code == ApplyCode::applied &&
            (result.detail_kind == ApplyResultDetailKind::fill ||
             result.detail_kind == ApplyResultDetailKind::venue_event);
    }
    metrics.venue_event_headroom = static_cast<std::uint16_t>(
        kMaxVenueEventReceipts -
        std::min<std::size_t>(metrics.accepted_venue_event_count,
                              kMaxVenueEventReceipts));

    for (std::size_t index = 0; index < state.reservation_count; ++index) {
        const auto slot = static_cast<std::size_t>(state.reservations[index].state);
        if (slot < metrics.reservations_by_state.size()) {
            ++metrics.reservations_by_state[slot];
        }
    }

    for (std::size_t index = 0; index < state.risk_account_count; ++index) {
        const auto& risk = state.risk_accounts[index];
        auto& row = metrics.risk_accounts[metrics.risk_account_metric_count++];
        row.account_id = risk.account_id;
        row.instrument_id = risk.instrument_id;
        row.reserved_quantity = risk.reserved_quantity;
        row.position_quantity = risk.position_quantity;
        row.reserved_margin = risk.reserved_margin;
        row.reserved_fee = risk.reserved_fee;
        row.risk_version = risk.risk_version;
        row.risk_policy_version = risk.risk_policy_version;
        if (const auto exposure = risk.reserved_margin.checked_add(risk.reserved_fee)) {
            row.reserved_exposure = *exposure;
            if (const auto headroom =
                    risk.max_reserved_exposure.checked_sub(*exposure)) {
                row.exposure_headroom =
                    headroom->atoms() > 0 ? *headroom : MoneyMicros{};
            }
        }
        if (const auto headroom =
                risk.max_reserved_quantity.checked_sub(risk.reserved_quantity)) {
            row.quantity_headroom =
                headroom->atoms() > 0 ? *headroom : QuantityLots{};
        }
    }

    return metrics;
}

void observe_outcome(OutcomeCounters& counters, const ApplyResult& result) noexcept {
    switch (result.code) {
        case ApplyCode::applied:
            ++counters.applied;
            return;
        case ApplyCode::already_applied:
            ++counters.duplicates;
            return;
        case ApplyCode::duplicate_conflict:
        case ApplyCode::decision_id_conflict:
        case ApplyCode::fill_identity_conflict:
        case ApplyCode::venue_event_identity_conflict:
        case ApplyCode::immutable_policy_conflict:
        case ApplyCode::source_event_conflict:
        case ApplyCode::active_policy_scope_conflict:
        case ApplyCode::executable_policy_identity_conflict:
        case ApplyCode::derived_identity_collision:
        case ApplyCode::fill_quantity_conflict:
        case ApplyCode::fill_price_conflict:
        case ApplyCode::fill_fee_conflict:
        case ApplyCode::caller_role_conflict:
            ++counters.conflicts;
            return;
        case ApplyCode::decision_capacity_exceeded:
        case ApplyCode::policy_capacity_exceeded:
        case ApplyCode::receipt_capacity_exceeded:
        case ApplyCode::reservation_capacity_exceeded:
        case ApplyCode::order_capacity_exceeded:
        case ApplyCode::outbox_capacity_exceeded:
        case ApplyCode::fill_capacity_exceeded:
        case ApplyCode::venue_event_capacity_exceeded:
        case ApplyCode::source_capacity_exceeded:
        case ApplyCode::source_event_capacity_exceeded:
        case ApplyCode::risk_account_capacity_exceeded:
            ++counters.capacity_refusals;
            return;
        case ApplyCode::stale_risk_state:
        case ApplyCode::insufficient_risk_capacity:
        case ApplyCode::risk_arithmetic_overflow:
        case ApplyCode::risk_account_not_found:
        case ApplyCode::risk_reconfiguration_blocked:
        case ApplyCode::risk_version_exhausted:
        case ApplyCode::position_limit_exceeded:
        case ApplyCode::loss_limit_reached:
        case ApplyCode::order_not_reducing:
            ++counters.risk_refusals;
            return;
        case ApplyCode::stale_control_generation:
        case ApplyCode::non_monotonic_kill_epoch:
        case ApplyCode::non_monotonic_executor_generation:
            ++counters.control_refusals;
            return;
        default:
            ++counters.other_refusals;
            return;
    }
}

}
