// Phase 4 metrics.
//
// The properties that matter are not "the numbers are right" in isolation but
// that the projection is HONEST about the two things it cannot do: it has no
// clock, and it is not authoritative. So the tests below pin that ages are
// state-version distances rather than durations, that collection never mutates
// state or participates in any digest, and that the unresolved counts key on
// retained RISK rather than on lifecycle state - because a conflicted order is
// terminal and still the most important thing on the dashboard.

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

    // A live order holding risk, authorized long ago.
    state.reservation_count = 2;
    state.reservations[0] = {ClusterOrderId{31}, QuantityLots::from_atoms(2), {},
                             QuantityLots::from_atoms(2),
                             MoneyMicros::from_atoms(3'000),
                             MoneyMicros::from_atoms(3'000),
                             MoneyMicros::from_atoms(20), ReservationState::held};
    // A resolved order holding nothing, authorized recently.
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

    state.reclaimed_group_count = 7;
    state.settled_incomplete_effect_count = 3;
    return state;
}

void test_ages_are_state_version_distances_not_durations() {
    auto state = state_with_orders();
    const auto metrics = collect_metrics(state);

    // Order 0 was authorized at version 40 and the cluster is at 100, so it has
    // been unresolved across 60 applied commands. No clock was consulted.
    require(metrics.oldest_unresolved_order_age == 60,
            "unresolved age was not the state-version distance");
    require(metrics.oldest_reconciling_order_age == 60,
            "reconciling age was not reported for G-074a");
    require(metrics.oldest_unsettled_effect_age == 60,
            "unsettled effect age did not inherit its order's authorization");

    // Advancing the cluster advances every age by exactly the same amount, which
    // a wall-clock measure could not guarantee under a clock correction.
    state.state_version = 130;
    const auto later = collect_metrics(state);
    require(later.oldest_unresolved_order_age ==
                metrics.oldest_unresolved_order_age + 30,
            "age did not advance with the state version");
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
    // Conflict is TERMINAL but still holds exposure of unknown size. A metric
    // that counted "not terminal" would miss it, which is precisely the record an
    // operator most needs to see.
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

    // Purity is checked at the byte level rather than through a snapshot,
    // deliberately. A snapshot would only prove the CANONICAL bytes are
    // unchanged, and canonical encoding omits inactive slots and scratch. A
    // memcmp over the whole trivially-copyable object proves collect_metrics
    // touched nothing at all, including the fields a snapshot would not carry.
    static_assert(std::is_trivially_copyable_v<TradingState>);
    const auto first = collect_metrics(state);
    require(std::memcmp(&state, &original, sizeof(TradingState)) == 0,
            "collect_metrics mutated authoritative state");

    // Same state, same metrics: the projection is a function, not a sampler, so
    // it can be recomputed by any observer without coordinating with the reducer.
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
    observe(ApplyCode::order_capacity_exceeded);
    observe(ApplyCode::stale_risk_state);
    observe(ApplyCode::stale_control_generation);
    observe(ApplyCode::invalid_command);
    require(counters.applied == 1 && counters.duplicates == 1 &&
                counters.conflicts == 1 && counters.capacity_refusals == 1 &&
                counters.risk_refusals == 1 && counters.control_refusals == 1 &&
                counters.other_refusals == 1,
            "outcome classification did not separate the refusal families");
}

}  // namespace

int main() {
    try {
        test_ages_are_state_version_distances_not_durations();
        test_a_future_dated_record_reports_zero_rather_than_wrapping();
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
