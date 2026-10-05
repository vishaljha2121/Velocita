#include "velocita/v2/metrics.hpp"

#include <array>
#include <cstring>
#include <type_traits>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace velocita::v2;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

TradingState state_with_orders() {
    TradingState state;
    state.state_version = 100;
    state.strategy_mode = StrategyMode::normal;
    state.kill_epoch = KillEpoch{2};
    state.executor_generation = ExecutorGeneration{3};

    state.risk_account_count = 1;
    state.risk_accounts[0] = {
        AccountId{5}, InstrumentId{4}, RiskPolicyVersion{6}, RiskVersion{1},
        PositionVersion{1}, QuantityLots::from_atoms(4),
        QuantityLots::from_atoms(8), QuantityLots::from_atoms(2),
        MoneyMicros::from_atoms(4'000), MoneyMicros::from_atoms(8'000),
        MoneyMicros::from_atoms(3'000), MoneyMicros::from_atoms(20),
        1'000'000, 2'500};

    state.reservation_count = 2;
    state.reservations[0] = {ClusterOrderId{31}, QuantityLots::from_atoms(2), {},
                             QuantityLots::from_atoms(2),
                             MoneyMicros::from_atoms(3'000),
                             MoneyMicros::from_atoms(3'000),
                             MoneyMicros::from_atoms(20), ReservationState::held};
    state.reservations[1] = {ClusterOrderId{32}, QuantityLots::from_atoms(2),
                             QuantityLots::from_atoms(2), {}, {}, {}, {},
                             ReservationState::released};

    state.order_count = 2;
    state.orders[0] = {ClusterOrderId{31}, EffectId{41}, DecisionId{21}, 40, 0,
                       ClusterOrderState::reconciling};
    state.orders[1] = {ClusterOrderId{32}, EffectId{42}, DecisionId{22}, 90, 1,
                       ClusterOrderState::filled};

    state.outbox_effect_count = 2;
    state.outbox_effects[0] = {EffectId{41}, ClusterOrderId{31}, DecisionId{21},
                               Digest256{}, 1, OutboxState::unknown};
    state.outbox_effects[1] = {EffectId{42}, ClusterOrderId{32}, DecisionId{22},
                               Digest256{}, 1, OutboxState::acknowledged};

    ApplyResult cancel_authorization;
    cancel_authorization.code = ApplyCode::applied;
    cancel_authorization.state_version = 80;
    cancel_authorization.decision_id = DecisionId{21};
    cancel_authorization.detail = ApplyResultDetail::authorization(
        ClusterOrderId{31}, EffectId{41}, MoneyMicros::from_atoms(3'020));
    cancel_authorization.detail_kind = ApplyResultDetailKind::authorization;
    state.receipts[state.receipt_count++] = {
        {CallerId{1}, RequestSequence{1}, Digest256{}}, cancel_authorization};

    ApplyResult fill;
    fill.code = ApplyCode::applied;
    fill.decision_id = DecisionId{21};
    fill.detail = ApplyResultDetail::fill(
        VenueId{7}, ExecutionId{51}, QuantityLots::from_atoms(1));
    fill.detail_kind = ApplyResultDetailKind::fill;
    fill.metadata_words = {8, 9};
    state.receipts[state.receipt_count++] = {
        {CallerId{2}, RequestSequence{1}, Digest256{}}, fill};

    ApplyResult event;
    event.code = ApplyCode::applied;
    event.decision_id = DecisionId{22};
    event.detail = ApplyResultDetail::venue_event(
        VenueId{7}, VenueSessionId{8}, VenueSequence{10});
    event.detail_kind = ApplyResultDetailKind::venue_event;
    state.receipts[state.receipt_count++] = {
        {CallerId{3}, RequestSequence{1}, Digest256{}}, event};
    state.caller_cursor_count = 3;

    state.reclaimed_group_count = 7;
    state.settled_incomplete_effect_count = 3;
    return state;
}

void test_ages_are_state_version_distances_not_durations() {
    auto state = state_with_orders();
    const auto metrics = collect_metrics(state);

    require(metrics.oldest_unresolved_order_age == 60,
            "unresolved age was not the state-version distance");
    require(metrics.oldest_reconciling_order_age_since_authorization == 60,
            "reconciling upper-bound age was not reported for G-074a");
    require(metrics.oldest_unsettled_effect_age == 20,
            "unsettled effect age ignored the effect's own creation receipt");

    state.state_version = 130;
    const auto later = collect_metrics(state);
    require(later.oldest_unresolved_order_age ==
                metrics.oldest_unresolved_order_age + 30,
            "age did not advance with the state version");
}

void test_identity_arena_headroom_is_visible() {
    const auto metrics = collect_metrics(state_with_orders());
    require(metrics.accepted_venue_event_count == 2 &&
                metrics.venue_event_headroom == kMaxVenueEventReceipts - 2 &&
                metrics.caller_cursor_count == 3 &&
                metrics.caller_cursor_headroom == kMaxCallerCursors - 3,
            "identity-retention pressure was invisible to operators");
}

void test_a_future_dated_record_reports_zero_rather_than_wrapping() {
    auto state = state_with_orders();
    state.orders[0].authorization_state_version = state.state_version + 5;
    const auto metrics = collect_metrics(state);
    require(metrics.oldest_unresolved_order_age == 0,
            "an unsigned age wrapped instead of clamping to zero");
}

void test_unresolved_counts_key_on_risk_not_lifecycle_state() {
    auto state = state_with_orders();
    state.orders[0].state = ClusterOrderState::conflict;
    const auto metrics = collect_metrics(state);
    require(metrics.unresolved_orders == 1,
            "a conflicted order was not counted as unresolved");
    require(metrics.oldest_conflicted_order_age == 60,
            "conflicted age was not surfaced");
    require(metrics.orders_by_state[static_cast<std::size_t>(
                ClusterOrderState::conflict)] == 1,
            "orders_by_state did not record the conflict");
}

void test_collection_is_a_pure_projection() {
    const auto original = state_with_orders();
    auto state = original;

    static_assert(std::is_trivially_copyable_v<TradingState>);
    const auto first = collect_metrics(state);
    require(std::memcmp(&state, &original, sizeof(TradingState)) == 0,
            "collect_metrics mutated authoritative state");

    require(collect_metrics(state) == first, "collect_metrics was not deterministic");
}

void test_headroom_and_exposure_are_reported_per_account() {
    const auto state = state_with_orders();
    const auto metrics = collect_metrics(state);
    require(metrics.risk_account_metric_count == 1, "risk account was not reported");
    const auto& row = metrics.risk_accounts[0];
    require(row.reserved_exposure == MoneyMicros::from_atoms(3'020),
            "exposure was not margin plus fee");
    require(row.exposure_headroom == MoneyMicros::from_atoms(4'980),
            "exposure headroom was not the aggregate ceiling less the hold");
    require(row.quantity_headroom == QuantityLots::from_atoms(6),
            "quantity headroom was not the aggregate ceiling less the hold");
}

void test_outcome_counters_classify_refusals() {
    OutcomeCounters counters;
    const auto observe = [&](const ApplyCode code) {
        ApplyResult result;
        result.code = code;
        observe_outcome(counters, result);
    };
    observe(ApplyCode::applied);
    observe(ApplyCode::already_applied);
    observe(ApplyCode::decision_id_conflict);
    observe(ApplyCode::derived_identity_collision);
    observe(ApplyCode::fill_quantity_conflict);
    observe(ApplyCode::order_capacity_exceeded);
    observe(ApplyCode::stale_risk_state);
    observe(ApplyCode::position_limit_exceeded);
    observe(ApplyCode::stale_control_generation);
    observe(ApplyCode::invalid_command);
    require(counters.applied == 1 && counters.duplicates == 1 &&
                counters.conflicts == 3 && counters.capacity_refusals == 1 &&
                counters.risk_refusals == 2 && counters.control_refusals == 1 &&
                counters.other_refusals == 1,
            "outcome classification did not separate the refusal families");
}

}

int main() {
    try {
        test_ages_are_state_version_distances_not_durations();
        test_a_future_dated_record_reports_zero_rather_than_wrapping();
        test_identity_arena_headroom_is_visible();
        test_unresolved_counts_key_on_risk_not_lifecycle_state();
        test_collection_is_a_pure_projection();
        test_headroom_and_exposure_are_reported_per_account();
        test_outcome_counters_classify_refusals();
        std::cout << "all V2 metrics tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 metrics test failed: " << error.what() << '\n';
        return 1;
    }
}
