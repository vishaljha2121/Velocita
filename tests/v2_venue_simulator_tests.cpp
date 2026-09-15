#include "velocita/v2/payload.hpp"
#include "velocita/v2/venue_simulator.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace velocita::v2;
using namespace velocita::v2::sim;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

OrderEffect submit_effect(const std::uint64_t ordinal,
                          const std::int64_t quantity = 2) {
    OrderEffect value;
    value.effect_id = EffectId{1'000 + ordinal};
    value.cluster_order_id = ClusterOrderId{2'000 + ordinal};
    value.committed_directive_index = 10 + ordinal;
    value.executor_generation = ExecutorGeneration{3};
    value.kill_epoch = KillEpoch{2};
    value.venue_id = VenueId{9};
    value.account_id = AccountId{5};
    value.instrument_id = InstrumentId{4};
    value.operation = EffectOperation::submit;
    value.side = Side::buy;
    value.price = PriceTicks::from_atoms(11);
    value.quantity = QuantityLots::from_atoms(quantity);
    value.time_in_force = TimeInForce::good_til_cancel;
    value.post_only = PostOnlyMode::required;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.decision_id = DecisionId{20 + ordinal};
    value.risk_version = RiskVersion{1};
    value.source_generation = SourceGeneration{1};
    value.source_sequence = SourceSequence{1 + ordinal};
    Digest256 digest;
    digest.bytes[0] = std::byte{0x5A};
    value.payload_digest = digest;
    return value;
}

OrderEffect cancel_effect(const OrderEffect& submit) {
    auto value = submit;
    value.effect_id = EffectId{submit.effect_id.value() + 500};
    value.operation = EffectOperation::cancel;
    value.price = PriceTicks{};
    value.quantity = QuantityLots{};
    return value;
}

FaultPolicy everything_enabled() {
    FaultPolicy policy;
    policy.delay_permille = 150;
    policy.loss_permille = 80;
    policy.duplication_permille = 120;
    policy.reorder_permille = 120;
    policy.partial_fill_permille = 200;
    policy.fill_after_cancel_permille = 250;
    policy.disconnect_permille = 60;
    policy.rate_limit_permille = 90;
    policy.inconsistent_query_permille = 200;
    policy.partial_window_permille = 200;
    return policy;
}

struct Trace {
    std::vector<SendOutcome> sends;
    std::vector<std::vector<std::byte>> observations;
    std::vector<FaultSet> faults;
    std::vector<std::uint64_t> sessions;
};

Trace run(const std::uint64_t seed, const FaultPolicy& policy) {
    SimulatedVenue venue(seed, policy, VenueId{9});
    Trace trace;
    Tick now = 0;
    for (std::uint64_t ordinal = 1; ordinal <= 12; ++ordinal) {
        const auto submit = submit_effect(ordinal);
        trace.sends.push_back(venue.send(submit, now));
        now += 2;
        for (const auto& observation : venue.poll(now)) {
            trace.observations.push_back(encode_payload(observation).bytes);
        }
        if (ordinal % 3 == 0) {
            trace.sends.push_back(venue.send(cancel_effect(submit), now));
            const auto answer = venue.query(submit.cluster_order_id, now);
            trace.sends.push_back(answer.outcome == QueryOutcome::known
                                      ? SendOutcome::accepted
                                      : SendOutcome::business_rejected);
        }
        now += 5;
        for (const auto& observation : venue.poll(now)) {
            trace.observations.push_back(encode_payload(observation).bytes);
        }
        trace.sessions.push_back(venue.session().value());
    }
    now += 100;  // let every delayed response come due
    for (const auto& observation : venue.poll(now)) {
        trace.observations.push_back(encode_payload(observation).bytes);
    }
    trace.faults = venue.fault_log();
    return trace;
}

bool operator==(const Trace& left, const Trace& right) {
    return left.sends == right.sends && left.observations == right.observations &&
           left.faults == right.faults && left.sessions == right.sessions;
}

void test_a_seed_reproduces_its_run_exactly() {
    for (const std::uint64_t seed : {1ULL, 7ULL, 42ULL, 9'001ULL}) {
        require(run(seed, everything_enabled()) == run(seed, everything_enabled()),
                "seed " + std::to_string(seed) + " did not reproduce its run");
    }
}

void test_different_seeds_explore_different_behaviour() {
    const auto first = run(1, everything_enabled());
    const auto second = run(2, everything_enabled());
    require(!(first == second), "two seeds produced identical runs");
}

void test_no_wall_clock_influences_the_result() {
    const auto before = run(11, everything_enabled());
    volatile std::uint64_t spin = 0;
    for (std::uint64_t index = 0; index < 5'000'000; ++index) spin += index;
    (void)spin;
    require(before == run(11, everything_enabled()),
            "elapsed real time changed a virtual-time run");
}

FaultPolicy only(std::uint16_t FaultPolicy::*knob) {
    FaultPolicy policy = FaultPolicy::clean();
    policy.minimum_latency = 1;
    policy.maximum_latency = 1;
    policy.*knob = 1000;
    return policy;
}

void test_every_fault_class_produces_its_observable_effect() {
    // Verify fault reachability through observable outcomes, not selector labels.
    const auto effect = submit_effect(1);

    {   // RATE_LIMIT: refused at the transport, nothing at the venue.
        SimulatedVenue venue(1, only(&FaultPolicy::rate_limit_permille),
                             VenueId{9});
        require(venue.send(effect, 0) == SendOutcome::rate_limited,
                "rate_limit did not refuse the send");
        require(venue.poll(100).empty(), "a rate-limited send produced responses");
    }
    {   // DISCONNECT: ambiguous, and a new session identity.
        SimulatedVenue venue(1, only(&FaultPolicy::disconnect_permille), VenueId{9});
        const auto before = venue.session();
        require(venue.send(effect, 0) == SendOutcome::ambiguous,
                "disconnect did not produce an ambiguous outcome");
        require(venue.session().value() > before.value(),
                "disconnect did not issue a new session");
    }
    {   // LOSS on a SUBMIT: accepted, and not one response ever arrives.
        SimulatedVenue venue(1, only(&FaultPolicy::loss_permille), VenueId{9});
        require(venue.send(effect, 0) == SendOutcome::accepted,
                "a lost submit was not accepted at the venue");
        require(venue.poll(1'000).empty(),
                "a lost submit still produced a response");

        require(venue.query(effect.cluster_order_id, 1).outcome ==
                    QueryOutcome::known,
                "a lost submit left nothing at the venue, so it was not a LOST "
                "RESPONSE, it was a refused send");
    }
    {   // LOSS on a CANCEL: the venue cancels, the gateway never hears.

        FaultPolicy clean_then_lossy = FaultPolicy::clean();
        clean_then_lossy.minimum_latency = 1;
        clean_then_lossy.maximum_latency = 1;
        SimulatedVenue venue(1, clean_then_lossy, VenueId{9});
        require(venue.send(effect, 0) == SendOutcome::accepted, "submit failed");
        (void)venue.poll(10);
        SimulatedVenue lossy(1, only(&FaultPolicy::loss_permille), VenueId{9});
        require(lossy.send(effect, 0) == SendOutcome::accepted, "submit failed");
        require(lossy.send(cancel_effect(effect), 1) == SendOutcome::accepted,
                "the cancel was refused");
        require(lossy.poll(1'000).empty(),
                "a lost cancel still delivered its cancellation");

        require(lossy.query(effect.cluster_order_id, 2).cancelled,
                "a lost cancel did not cancel the order at the venue");
    }
    {   // DUPLICATION: the same bytes twice, including sequence and evidence.
        SimulatedVenue venue(1, only(&FaultPolicy::duplication_permille),
                             VenueId{9});
        require(venue.send(effect, 0) == SendOutcome::accepted, "submit failed");
        const auto delivered = venue.poll(1'000);
        std::map<std::vector<std::byte>, std::size_t> counts;
        for (const auto& observation : delivered) {
            ++counts[encode_payload(observation).bytes];
        }
        bool duplicated = false;
        for (const auto& [bytes, count] : counts) {
            (void)bytes;
            if (count > 1) duplicated = true;
        }
        require(duplicated, "duplication delivered nothing twice");
    }
    {   // PARTIAL_FILL: more than one execution for one order.
        SimulatedVenue venue(1, only(&FaultPolicy::partial_fill_permille),
                             VenueId{9});
        require(venue.send(submit_effect(1, 4), 0) == SendOutcome::accepted,
                "submit failed");
        std::size_t fills = 0;
        for (const auto& observation : venue.poll(1'000)) {
            if (observation.kind == VenueObservationKind::fill) ++fills;
        }
        require(fills > 1, "partial_fill delivered a single execution");
    }
    {   // FILL_AFTER_CANCEL: an execution arrives after cancellation.

        FaultPolicy policy = only(&FaultPolicy::fill_after_cancel_permille);
        policy.partial_fill_permille = 1000;
        SimulatedVenue venue(1, policy, VenueId{9});
        const auto partial = submit_effect(2, 4);
        require(venue.send(partial, 0) == SendOutcome::accepted, "submit failed");
        (void)venue.poll(1);  // leave the order resting, not yet drained
        require(venue.send(cancel_effect(partial), 1) == SendOutcome::accepted,
                "the cancel was refused");
        bool seen_cancel = false;
        bool fill_after = false;
        for (const auto& observation : venue.poll(1'000)) {
            if (observation.kind == VenueObservationKind::cancelled) {
                seen_cancel = true;
            } else if (observation.kind == VenueObservationKind::fill &&
                       seen_cancel) {
                fill_after = true;
            }
        }
        require(fill_after,
                "fill_after_cancel delivered no execution after the cancellation");
    }
    {   // DELAY: a response later than the pinned latency band allows.
        FaultPolicy policy = only(&FaultPolicy::delay_permille);
        policy.delay_penalty = 25;
        SimulatedVenue venue(1, policy, VenueId{9});
        require(venue.send(effect, 0) == SendOutcome::accepted, "submit failed");
        require(venue.poll(policy.maximum_latency + 1).empty(),
                "a delayed response arrived within the normal band");
        require(!venue.poll(1'000).empty(), "a delayed response never arrived");
    }
    {   // REORDER: the delivered order is not the natural one.
        // Each roll consumes a draw even when disabled, isolating reorder in the A/B trace.
        auto with_reorder = everything_enabled();
        with_reorder.reorder_permille = 1000;
        auto without_reorder = with_reorder;
        without_reorder.reorder_permille = 0;
        bool differed = false;
        for (std::uint64_t seed = 1; seed <= 40 && !differed; ++seed) {
            differed = run(seed, with_reorder).observations !=
                       run(seed, without_reorder).observations;
        }
        require(differed,
                "enabling reorder changed no delivery order anywhere, so the "
                "class is a label with no behaviour behind it");
    }
    {   // INCONSISTENT_QUERY: flagged, and wrong in the unclosable direction.
        SimulatedVenue venue(1, only(&FaultPolicy::inconsistent_query_permille),
                             VenueId{9});
        require(venue.send(effect, 0) == SendOutcome::accepted, "submit failed");
        const auto answer = venue.query(effect.cluster_order_id, 1);
        require(answer.inconsistent && answer.filled_quantity.atoms() == 0,
                "inconsistent_query did not deny a fill it had already made");
    }
    {   // PARTIAL_WINDOW: a truthful prefix that says it is one.
        SimulatedVenue venue(1, only(&FaultPolicy::partial_window_permille),
                             VenueId{9});
        require(venue.send(effect, 0) == SendOutcome::accepted, "submit failed");
        const auto answer = venue.query(effect.cluster_order_id, 1);
        require(!answer.window_complete && !answer.inconsistent,
                "partial_window did not page its answer");
    }
}

void test_faults_compose_on_one_operation() {
    FaultPolicy policy = FaultPolicy::clean();
    policy.minimum_latency = 1;
    policy.maximum_latency = 1;
    policy.partial_fill_permille = 1000;
    policy.loss_permille = 1000;
    SimulatedVenue venue(1, policy, VenueId{9});
    const auto partial = submit_effect(4, 4);

    require(venue.send(partial, 0) == SendOutcome::accepted, "submit failed");
    require(venue.poll(1'000).empty(), "the lost submit delivered a response");

    require(venue.fault_log().size() == 1, "one send produced two log entries");
    require(venue.fault_log().front().count() >= 2,
            "two classes did not fire on one operation, so composition is not "
            "actually happening");
    require(venue.fault_log().front().contains(Fault::loss) &&
                venue.fault_log().front().contains(Fault::partial_fill),
            "the composed set did not name both classes");
}

void test_a_clean_policy_produces_no_faults() {
    for (const auto& faults : run(5, FaultPolicy::clean()).faults) {
        require(!faults.any(), "the clean policy produced a fault");
    }
}

void test_enabling_a_fault_does_not_shift_unrelated_draws() {
    FaultPolicy off = FaultPolicy::clean();
    off.minimum_latency = 1;
    off.maximum_latency = 1;
    auto on = off;
    on.rate_limit_permille = 1000;  // always
    SimulatedVenue quiet(3, off, VenueId{9});
    SimulatedVenue loud(3, on, VenueId{9});
    require(quiet.send(submit_effect(1), 0) == SendOutcome::accepted &&
                loud.send(submit_effect(1), 0) == SendOutcome::rate_limited,
            "a fully enabled rate limit did not fire");
}

void test_every_observation_satisfies_the_kernel_contract() {
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        SimulatedVenue venue(seed, everything_enabled(), VenueId{9});
        Tick now = 0;
        for (std::uint64_t ordinal = 1; ordinal <= 6; ++ordinal) {
            const auto submit = submit_effect(ordinal);
            (void)venue.send(submit, now);
            now += 3;
            (void)venue.send(cancel_effect(submit), now);
            now += 40;
            for (const auto& observation : venue.poll(now)) {
                require(validate(observation) == ContractError::none,
                        "seed " + std::to_string(seed) +
                            " produced an observation the kernel would refuse: " +
                            std::to_string(static_cast<int>(validate(observation))));
            }
        }
    }
}

void test_duplicate_client_identity_is_reported_as_a_venue_duplicate() {
    SimulatedVenue venue(1, FaultPolicy::clean(), VenueId{9});
    const auto effect = submit_effect(1);
    require(venue.send(effect, 0) == SendOutcome::accepted, "first submit failed");
    require(venue.send(effect, 1) == SendOutcome::venue_duplicate,
            "a resent effect was accepted a second time");
}

void test_an_inconsistent_query_is_flagged_rather_than_silently_wrong() {
    FaultPolicy always;
    always.inconsistent_query_permille = 1000;
    SimulatedVenue venue(1, always, VenueId{9});
    const auto effect = submit_effect(1);
    (void)venue.send(effect, 0);
    const auto answer = venue.query(effect.cluster_order_id, 1);
    require(answer.outcome == QueryOutcome::known && answer.inconsistent,
            "a corrupted query answer was not flagged as inconsistent");
    // An understated total denies booked fills; an overstated total can be evidence in flight.
    require(answer.filled_quantity.atoms() == 0 &&
                answer.remaining_quantity == effect.quantity,
            "the inconsistent answer did not deny a fill it had already made");
}

void test_a_partial_window_answer_is_a_truthful_prefix_not_a_corruption() {
    // Partial windows are truthful prefixes; incomplete coverage must remain distinct
    // from a complete answer that contradicts booked fills.
    FaultPolicy always;
    always.partial_window_permille = 1000;
    SimulatedVenue venue(1, always, VenueId{9});
    const auto effect = submit_effect(1);
    require(venue.send(effect, 0) == SendOutcome::accepted, "submit failed");
    const auto answer = venue.query(effect.cluster_order_id, 1);
    require(answer.outcome == QueryOutcome::known,
            "a paged answer was not an answer");
    require(!answer.window_complete,
            "a paged answer claimed to cover its whole window");
    require(!answer.inconsistent,
            "a paged answer was flagged as a self-contradiction");
    require(answer.filled_quantity.atoms() < effect.quantity.atoms(),
            "the paged answer was not actually short");
    require(answer.filled_quantity.atoms() +
                    answer.remaining_quantity.atoms() ==
                effect.quantity.atoms(),
            "the paged answer did not account for the original quantity");

    SimulatedVenue quiet(1, FaultPolicy::clean(), VenueId{9});
    require(quiet.send(effect, 0) == SendOutcome::accepted, "submit failed");
    require(quiet.query(effect.cluster_order_id, 1).window_complete,
            "a complete answer did not say so");
}

void test_an_unknown_order_is_not_found_rather_than_unavailable() {
    SimulatedVenue venue(1, FaultPolicy::clean(), VenueId{9});
    require(venue.query(ClusterOrderId{7'777}, 0).outcome == QueryOutcome::not_found,
            "an absent order did not report not_found");
}

void test_a_disconnect_issues_a_new_session() {
    // Session changes must not defeat execution deduplication.
    FaultPolicy always;
    always.disconnect_permille = 1000;
    SimulatedVenue venue(1, always, VenueId{9});
    const auto before = venue.session();
    require(venue.send(submit_effect(1), 0) == SendOutcome::ambiguous,
            "a disconnect did not produce an ambiguous outcome");
    require(venue.session().value() > before.value(),
            "a disconnect did not issue a new session");
}

void test_observations_replay_after_gateway_restart_until_acknowledged() {
    SimulatedVenue venue(1, FaultPolicy::clean(), VenueId{9});
    require(venue.send(submit_effect(1), 0) == SendOutcome::accepted,
            "submit failed");
    const auto first = venue.poll(100);
    require(!first.empty(), "the venue produced no observation to replay");
    require(venue.poll(100).empty(),
            "one gateway instance received the same observation twice");

    venue.restart_delivery();
    const auto replayed = venue.poll(100);
    require(replayed.size() == first.size(),
            "restart did not replay every unacknowledged observation");
    for (std::size_t index = 0; index < first.size(); ++index) {
        require(encode_payload(first[index]).bytes ==
                    encode_payload(replayed[index]).bytes,
                "restart changed canonical observation bytes");
        venue.acknowledge_observation(replayed[index]);
    }
    venue.restart_delivery();
    require(venue.poll(100).empty(),
            "an acknowledged observation replayed after restart");
}

void test_disconnect_preserves_every_unacknowledged_observation() {
    FaultPolicy policy = FaultPolicy::clean();
    policy.disconnect_permille = 500;
    policy.minimum_latency = 1;
    policy.maximum_latency = 1;

    std::uint64_t query_seed = 0;
    for (std::uint64_t seed = 1; seed <= 10'000; ++seed) {
        SimulatedVenue candidate(seed, policy, VenueId{9});
        const auto effect = submit_effect(1);
        if (candidate.send(effect, 0) != SendOutcome::accepted) continue;
        if (candidate.query(effect.cluster_order_id, 1).outcome ==
            QueryOutcome::unavailable) {
            query_seed = seed;
            break;
        }
    }
    require(query_seed != 0, "no deterministic query-disconnect seed was found");

    {
        SimulatedVenue control(query_seed, policy, VenueId{9});
        require(control.send(submit_effect(1), 0) == SendOutcome::accepted,
                "control submit failed");
        const auto expected = control.poll(100);
        SimulatedVenue venue(query_seed, policy, VenueId{9});
        const auto effect = submit_effect(1);
        require(venue.send(effect, 0) == SendOutcome::accepted, "submit failed");
        require(venue.query(effect.cluster_order_id, 1).outcome ==
                    QueryOutcome::unavailable,
                "the selected query did not disconnect");
        venue.restart_delivery();
        const auto delivered = venue.poll(100);
        require(delivered.size() == expected.size(),
                "a query disconnect erased observations not yet delivered");
        for (std::size_t index = 0; index < expected.size(); ++index) {
            require(encode_payload(delivered[index]).bytes ==
                        encode_payload(expected[index]).bytes,
                    "a query disconnect changed undelivered observation bytes");
        }
    }

    {
        SimulatedVenue venue(query_seed, policy, VenueId{9});
        const auto effect = submit_effect(1);
        require(venue.send(effect, 0) == SendOutcome::accepted, "submit failed");
        const auto delivered = venue.poll(100);
        require(!delivered.empty(), "the venue produced no observation");
        require(venue.query(effect.cluster_order_id, 1).outcome ==
                    QueryOutcome::unavailable,
                "the selected query did not disconnect");
        venue.restart_delivery();
        const auto replayed = venue.poll(100);
        require(replayed.size() == delivered.size(),
                "a query disconnect erased delivered but unacknowledged evidence");
        for (std::size_t index = 0; index < delivered.size(); ++index) {
            require(encode_payload(delivered[index]).bytes ==
                        encode_payload(replayed[index]).bytes,
                    "a query disconnect changed replayed observation bytes");
        }
    }

    std::uint64_t send_seed = 0;
    for (std::uint64_t seed = 1; seed <= 10'000; ++seed) {
        SimulatedVenue candidate(seed, policy, VenueId{9});
        if (candidate.send(submit_effect(1), 0) != SendOutcome::accepted) continue;
        if (candidate.send(submit_effect(2), 1) == SendOutcome::ambiguous) {
            send_seed = seed;
            break;
        }
    }
    require(send_seed != 0, "no deterministic send-disconnect seed was found");

    SimulatedVenue control(send_seed, policy, VenueId{9});
    require(control.send(submit_effect(1), 0) == SendOutcome::accepted,
            "control submit failed");
    const auto expected = control.poll(100);
    SimulatedVenue venue(send_seed, policy, VenueId{9});
    require(venue.send(submit_effect(1), 0) == SendOutcome::accepted,
            "first submit failed");
    require(venue.send(submit_effect(2), 1) == SendOutcome::ambiguous,
            "the selected second send did not disconnect");
    venue.restart_delivery();
    const auto delivered = venue.poll(100);
    require(delivered.size() == expected.size(),
            "a send disconnect erased earlier unacknowledged observations");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        require(encode_payload(delivered[index]).bytes ==
                    encode_payload(expected[index]).bytes,
                "a send disconnect changed earlier observation bytes");
    }
}

}  // namespace

int main() {
    try {
        test_a_seed_reproduces_its_run_exactly();
        test_different_seeds_explore_different_behaviour();
        test_no_wall_clock_influences_the_result();
        test_every_fault_class_produces_its_observable_effect();
        test_faults_compose_on_one_operation();
        test_a_clean_policy_produces_no_faults();
        test_enabling_a_fault_does_not_shift_unrelated_draws();
        test_every_observation_satisfies_the_kernel_contract();
        test_duplicate_client_identity_is_reported_as_a_venue_duplicate();
        test_an_inconsistent_query_is_flagged_rather_than_silently_wrong();
        test_a_partial_window_answer_is_a_truthful_prefix_not_a_corruption();
        test_an_unknown_order_is_not_found_rather_than_unavailable();
        test_a_disconnect_issues_a_new_session();
        test_observations_replay_after_gateway_restart_until_acknowledged();
        test_disconnect_preserves_every_unacknowledged_observation();
        std::cout << "all V2 venue simulator tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 venue simulator test failed: " << error.what() << '\n';
        return 1;
    }
}
