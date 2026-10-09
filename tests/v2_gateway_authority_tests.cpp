#include "velocita/v2/gateway_authority.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace velocita::v2;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Digest256 digest(std::byte value) {
    Digest256 result{};
    result.bytes.fill(value);
    return result;
}

BoundOrderEffect binding() {
    BoundOrderEffect result{};
    result.effect.effect_id = EffectId{7};
    result.effect.executor_generation = ExecutorGeneration{3};
    result.effect.kill_epoch = KillEpoch{2};
    result.effect.payload_digest = digest(std::byte{0x31});
    result.catalog_root = digest(std::byte{0x41});
    result.instrument_spec_version = InstrumentSpecVersion{5};
    result.venue_capabilities_version = VenueCapabilitiesVersion{6};
    return result;
}

GatewayControlView control() {
    return GatewayControlView{ExecutorGeneration{3}, KillEpoch{2},
                              StrategyMode::normal, 0, 0, 0};
}

GatewayAuthority authority(Tick bound = 50) {
    std::array<std::byte, 16> incarnation{};
    incarnation.fill(std::byte{0x21});
    return GatewayAuthority{incarnation, digest(std::byte{0x51}), bound};
}

void test_closed_default_and_activation() {
    auto owner = authority();
    require(owner.new_risk_stopped(), "constructor did not stop new risk");
    require(owner.all_transport_stopped(), "constructor did not stop transport");
    require(!owner.begin_request(binding(), control(), 10),
            "closed owner began request");
    owner.activate_after_reconciliation();
    require(owner.begin_request(binding(), control(), 10).has_value(),
            "activated owner refused valid request");
}

void test_a_request_begins_only_in_a_mode_that_allows_orders() {
    for (const auto& [mode, allowed] : {std::pair{StrategyMode::normal, true},
                                        std::pair{StrategyMode::reduce_only, true},
                                        std::pair{StrategyMode::flatten, true},
                                        std::pair{StrategyMode::no_new_risk, false},
                                        std::pair{StrategyMode::cancel_only, false},
                                        std::pair{StrategyMode::halted, false},
                                        std::pair{static_cast<StrategyMode>(0), false},
                                        std::pair{static_cast<StrategyMode>(7), false}}) {
        auto owner = authority();
        owner.activate_after_reconciliation();
        auto current = control();
        current.strategy_mode = mode;
        require(owner.begin_request(binding(), current, 10).has_value() == allowed,
                allowed ? "an authority request was refused in a mode that allows orders"
                        : "an authority request began in a mode that allows no orders");
    }
}

void test_one_slot_and_monotonic_abandon() {
    auto owner = authority();
    owner.activate_after_reconciliation();
    const auto first = owner.begin_request(binding(), control(), 10);
    require(first && first->request_sequence == 1, "wrong first sequence");
    require(!owner.begin_request(binding(), control(), 11),
            "second pending request admitted");
    owner.abandon_request();
    const auto second = owner.begin_request(binding(), control(), 12);
    require(second && second->request_sequence == 2,
            "abandon reused request sequence");
}

void test_zero_configuration_and_clock_regression_fail_closed() {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    static_assert(detail::next_authority_sequence(maximum - 1) == maximum);
    static_assert(!detail::next_authority_sequence(maximum));
    std::array<std::byte, 16> zero{};
    GatewayAuthority no_identity{zero, digest(std::byte{1}), 50};
    no_identity.activate_after_reconciliation();
    require(!no_identity.begin_request(binding(), control(), 1),
            "zero incarnation admitted");

    auto disabled = authority(0);
    disabled.activate_after_reconciliation();
    require(!disabled.begin_request(binding(), control(), 1),
            "zero freshness bound admitted");

    auto owner = authority();
    owner.activate_after_reconciliation();
    require(owner.begin_request(binding(), control(), 20).has_value(),
            "setup request failed");
    owner.abandon_request();
    require(!owner.begin_request(binding(), control(), 19),
            "regressed clock admitted request");
    require(owner.new_risk_stopped(), "clock regression did not latch stop");
}

void test_local_latches_drop_permission() {
    auto owner = authority();
    owner.activate_after_reconciliation();
    require(owner.begin_request(binding(), control(), 1).has_value(), "setup request failed");
    owner.stop_new_risk();
    require(owner.new_risk_stopped(), "new-risk latch not set");
    require(!owner.all_transport_stopped(), "new-risk latch stopped cancels");
    owner.activate_after_reconciliation();
    require(owner.begin_request(binding(), control(), 2).has_value(), "reactivation failed");
    owner.stop_all_transport();
    require(owner.new_risk_stopped() && owner.all_transport_stopped(),
            "all-transport latch incomplete");
}

void test_observed_fence_invalidates_pending() {
    auto owner = authority();
    owner.activate_after_reconciliation();
    require(owner.begin_request(binding(), control(), 1).has_value(), "setup request failed");
    auto killed = control();
    killed.kill_epoch = KillEpoch{3};
    killed.strategy_mode = StrategyMode::halted;
    owner.observe_fence(killed, 2);
    require(owner.new_risk_stopped(), "newer fence did not stop risk");
    owner.activate_after_reconciliation();
    require(!owner.begin_request(binding(), control(), 2),
            "older controls passed high-water mark");
}

void test_first_observed_term_during_pending_invalidates() {
    auto owner = authority();
    owner.activate_after_reconciliation();
    require(owner.begin_request(binding(), control(), 1).has_value(),
            "setup request failed");
    owner.observe_fence(control(), 2);
    require(owner.new_risk_stopped(),
            "first observed term silently adopted during pending request");
    require(!owner.begin_request(binding(), control(), 2),
            "new request admitted after pending term uncertainty");
}

void test_cancel_exception_respects_control_highwaters_and_stop_all() {
    auto owner = authority();
    owner.activate_after_reconciliation();
    owner.stop_new_risk();

    auto cancel = binding();
    cancel.effect.operation = EffectOperation::cancel;
    require(owner.permits_committed_cancel(cancel, control()),
            "new-risk stop incorrectly blocked committed cancel");

    auto newer = control();
    newer.kill_epoch = KillEpoch{3};
    owner.observe_fence(newer, 2);
    require(!owner.permits_committed_cancel(cancel, control()),
            "cancel below observed kill highwater was admitted");

    cancel.effect.kill_epoch = KillEpoch{3};
    require(owner.permits_committed_cancel(cancel, newer),
            "current committed cancel was not admitted while new risk stopped");

    owner.stop_all_transport();
    require(!owner.permits_committed_cancel(cancel, newer),
            "stop-all allowed committed cancel transport");
}

}

int main() {
    static_assert(!std::is_copy_constructible_v<GatewayAuthority>);
    static_assert(!std::is_move_constructible_v<GatewayAuthority>);
    try {
        test_closed_default_and_activation();
        test_a_request_begins_only_in_a_mode_that_allows_orders();
        test_one_slot_and_monotonic_abandon();
        test_zero_configuration_and_clock_regression_fail_closed();
        test_local_latches_drop_permission();
        test_observed_fence_invalidates_pending();
        test_first_observed_term_during_pending_invalidates();
        test_cancel_exception_respects_control_highwaters_and_stop_all();
    } catch (const std::exception& error) {
        std::cerr << "v2 gateway authority test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "v2 gateway authority tests passed\n";
    return EXIT_SUCCESS;
}
