#include "velocita/v2/venue_transport.hpp"

#include "velocita/v2/venue_simulator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace velocita::v2;
using namespace velocita::v2::sim;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

OrderEffect submit_effect(const std::uint64_t id) {
    OrderEffect effect;
    effect.effect_id = EffectId{id};
    effect.cluster_order_id = ClusterOrderId{100 + id};
    effect.committed_directive_index = id;
    effect.executor_generation = ExecutorGeneration{1};
    effect.kill_epoch = KillEpoch{1};
    effect.venue_id = VenueId{9};
    effect.account_id = AccountId{5};
    effect.instrument_id = InstrumentId{4};
    effect.operation = EffectOperation::submit;
    effect.side = Side::buy;
    effect.price = PriceTicks::from_atoms(11);
    effect.quantity = QuantityLots::from_atoms(1);
    effect.time_in_force = TimeInForce::good_til_cancel;
    effect.post_only = PostOnlyMode::prohibited;
    return effect;
}

// ADR 0009. Every path through the two-step send, with the venue's answers
// injected so nothing here touches a network.
struct StepScript {
    StepOutcome prepare{StepOutcome::succeeded};
    StepOutcome sign{StepOutcome::succeeded};
    StepOutcome submit{StepOutcome::succeeded};
    int abandons{};
    int prepares{};
    int signs{};
    int submits{};
};

TwoStepSend scripted(StepScript& script) {
    TwoStepSend steps;
    steps.context = &script;
    steps.prepare_fn = [](void* c, const OrderEffect&) {
        auto& s = *static_cast<StepScript*>(c);
        ++s.prepares;
        return s.prepare;
    };
    steps.sign_fn = [](void* c, const OrderEffect&) {
        auto& s = *static_cast<StepScript*>(c);
        ++s.signs;
        return s.sign;
    };
    steps.submit_fn = [](void* c, const OrderEffect&) {
        auto& s = *static_cast<StepScript*>(c);
        ++s.submits;
        return s.submit;
    };
    steps.abandon_fn = [](void* c, const OrderEffect&) {
        ++static_cast<StepScript*>(c)->abandons;
    };
    return steps;
}

void test_a_clean_two_step_send_is_one_accepted() {
    StepScript script;
    require(resolve_two_step_send(scripted(script), submit_effect(1)) ==
                SendOutcome::accepted,
            "a send where both steps succeeded was not accepted");
    require(script.prepares == 1 && script.signs == 1 && script.submits == 1,
            "the happy path did not run each step exactly once");
    require(script.abandons == 0, "a successful send abandoned its own order");
}

void test_nothing_happens_at_the_venue_before_step_one_succeeds() {
    for (const auto outcome : {StepOutcome::refused, StepOutcome::ambiguous}) {
        StepScript script;
        script.prepare = outcome;
        require(resolve_two_step_send(scripted(script), submit_effect(1)) ==
                    SendOutcome::pre_send_failure,
                "a failed first step did not report that nothing was sent");
        require(script.signs == 0 && script.submits == 0,
                "the adapter signed or submitted after the first step failed");
        require(script.abandons == 0,
                "the adapter tried to abandon an order the venue never created");
    }

    StepScript paced;
    paced.prepare = StepOutcome::rate_limited;
    require(resolve_two_step_send(scripted(paced), submit_effect(1)) ==
                SendOutcome::rate_limited,
            "pacing at the first step was reported as a failure, which tells the "
            "caller to reconsider the order rather than to slow down");
}

void test_step_one_ambiguity_is_not_step_two_ambiguity() {
    // THE DISTINCTION THIS WHOLE FUNCTION EXISTS FOR.
    //
    // Both are "we do not know what the venue did". They are reported
    // differently because an order left `initialized` CANNOT FILL - it awaits a
    // signature that was never sent - while an ambiguous submission may be
    // resting right now.
    //
    // Collapsing them either way is expensive. Reporting step one as `ambiguous`
    // sends the gateway reconciling for an order that cannot exist. Reporting
    // step two as `pre_send_failure` tells the gateway nothing is at the venue
    // when something may be, and the effect would be retried into a second live
    // order.
    StepScript first;
    first.prepare = StepOutcome::ambiguous;
    const auto first_step = resolve_two_step_send(scripted(first), submit_effect(1));

    StepScript second;
    second.submit = StepOutcome::ambiguous;
    const auto second_step = resolve_two_step_send(scripted(second), submit_effect(1));

    require(first_step == SendOutcome::pre_send_failure,
            "an ambiguous first step claimed something might be resting");
    require(second_step == SendOutcome::ambiguous,
            "an ambiguous submission claimed nothing was sent, which would let "
            "the effect be retried into a second live order");
    require(first_step != second_step,
            "the two ambiguity points were collapsed into one answer");
}

void test_an_ambiguous_submission_is_never_abandoned() {
    // Abandoning what may be a live order is worse than leaving it. Only the
    // gateway's reconciliation can establish which it is, and it asks with the
    // cluster's full picture rather than the adapter's partial one.
    StepScript script;
    script.submit = StepOutcome::ambiguous;
    require(resolve_two_step_send(scripted(script), submit_effect(1)) ==
                SendOutcome::ambiguous,
            "fixture drifted");
    require(script.abandons == 0,
            "the adapter cancelled an order that may have been resting");
}

void test_a_failure_after_step_one_abandons_the_unsigned_order() {
    // A signing failure or a clean refusal leaves an `initialized` order behind.
    // It cannot fill, so the cleanup is untidiness rather than risk - but
    // leaving it forever would litter the venue with unsigned orders.
    StepScript signing;
    signing.sign = StepOutcome::refused;
    require(resolve_two_step_send(scripted(signing), submit_effect(1)) ==
                SendOutcome::pre_send_failure,
            "a signing failure was not reported as nothing-sent");
    require(signing.abandons == 1 && signing.submits == 0,
            "a signing failure did not abandon the unsigned order");

    StepScript refused;
    refused.submit = StepOutcome::refused;
    require(resolve_two_step_send(scripted(refused), submit_effect(1)) ==
                SendOutcome::business_rejected,
            "a venue declining at submission was reported as a transport "
            "failure rather than as the economic answer it is");
    require(refused.abandons == 1, "a refused submission left its order behind");
}

// THE POINT OF THE WHOLE FILE. A scenario is run twice - once against the
// simulator directly, once through the transport vtable that wraps it - and the
// two must agree at every step.
//
// Without this the boundary is an assertion in a header. With it, the claim
// "the gateway cannot tell which venue it is talking to" is checkable, and it is
// the claim the real adapter will inherit: whatever the True Markets adapter
// does about JWTs, rate limits and two-step DeFi signing happens BEHIND this
// seam, and the gateway's behaviour must not change when it appears.
struct Trace {
    std::vector<int> send_outcomes;
    std::vector<std::size_t> poll_counts;
    std::vector<int> query_outcomes;
    std::vector<std::uint64_t> sessions;

    [[nodiscard]] bool operator==(const Trace&) const = default;
};

template <typename Send, typename Poll, typename Query, typename Session>
Trace run_scenario(Send send, Poll poll, Query query, Session session) {
    Trace trace;
    for (std::uint64_t id = 1; id <= 3; ++id) {
        trace.send_outcomes.push_back(
            static_cast<int>(send(submit_effect(id), Tick{id})));
    }
    for (Tick now = 1; now <= 12; ++now) {
        trace.poll_counts.push_back(poll(now));
    }
    for (std::uint64_t id = 1; id <= 3; ++id) {
        trace.query_outcomes.push_back(
            static_cast<int>(query(ClusterOrderId{100 + id}, Tick{20})));
    }
    trace.sessions.push_back(session());
    return trace;
}

void test_the_simulator_behaves_identically_through_the_seam() {
    // Identical seeds and policies, so any divergence is the seam and not the
    // venue's own randomness.
    constexpr std::uint64_t kSeed = 0xC0FFEE;
    const FaultPolicy policy{};

    SimulatedVenue direct(kSeed, policy, VenueId{9});
    const auto direct_trace = run_scenario(
        [&](const OrderEffect& effect, const Tick now) { return direct.send(effect, now); },
        [&](const Tick now) {
            auto observations = direct.poll(now);
            for (const auto& observation : observations) {
                direct.acknowledge_observation(observation);
            }
            return observations.size();
        },
        [&](const ClusterOrderId order, const Tick now) {
            return direct.query(order, now).outcome;
        },
        [&] { return direct.session().value(); });

    SimulatedVenue wrapped(kSeed, policy, VenueId{9});
    const auto transport = simulated_venue_transport(wrapped);
    std::array<VenueObservation, kMaxVenueObservationsPerPoll> buffer{};
    const auto seam_trace = run_scenario(
        [&](const OrderEffect& effect, const Tick now) {
            return transport.send(effect, now);
        },
        [&](const Tick now) {
            const auto count = transport.poll(buffer, now);
            for (std::size_t i = 0; i < count; ++i) {
                transport.acknowledge(buffer[i]);
            }
            return count;
        },
        [&](const ClusterOrderId order, const Tick now) {
            return transport.query(order, now).outcome;
        },
        [&] { return transport.session().value(); });

    require(direct_trace == seam_trace,
            "the same venue behaved differently through the transport seam, so "
            "the seam is not transparent and no adapter behind it can be trusted "
            "to behave like the simulator the suite was written against");

    // Non-vacuity: a scenario that sends nothing and sees nothing would compare
    // equal trivially.
    require(!direct_trace.send_outcomes.empty() &&
                direct_trace.sessions.front() != 0,
            "the scenario exercised nothing, so the comparison proves nothing");
}

void test_a_poll_never_overruns_the_caller_s_buffer() {
    // The simulator returns a `std::vector`; the seam writes into a span the
    // CALLER owns. That is the whole reason the signature differs rather than
    // forwarding the vector: an adapter must not be able to decide how much
    // memory the gateway spends, and a venue that floods us must be truncated
    // rather than obeyed.
    SimulatedVenue venue(0xBEEF, FaultPolicy{}, VenueId{9});
    const auto transport = simulated_venue_transport(venue);
    for (std::uint64_t id = 1; id <= 8; ++id) {
        (void)transport.send(submit_effect(id), Tick{id});
    }

    std::array<VenueObservation, 2> tiny{};
    std::size_t delivered = 0;
    for (Tick now = 1; now <= 40; ++now) {
        const auto count = transport.poll(tiny, now);
        require(count <= tiny.size(),
                "poll wrote " + std::to_string(count) + " observations into a " +
                    std::to_string(tiny.size()) + "-slot buffer");
        for (std::size_t i = 0; i < count; ++i) transport.acknowledge(tiny[i]);
        delivered += count;
    }
    require(delivered > tiny.size(),
            "the venue never produced more observations than the buffer holds, "
            "so truncation was never exercised");
}

void test_an_undelivered_observation_survives_truncation() {
    // Truncating must DEFER, never DROP. An observation the buffer had no room
    // for is economic evidence, and a seam that silently discarded it would lose
    // fills - the one thing the whole evidence path exists to prevent.
    SimulatedVenue venue(0xD00D, FaultPolicy{}, VenueId{9});
    const auto transport = simulated_venue_transport(venue);
    for (std::uint64_t id = 1; id <= 6; ++id) {
        (void)transport.send(submit_effect(id), Tick{id});
    }

    std::array<VenueObservation, 1> one_at_a_time{};
    std::vector<EffectId> seen_slowly;
    for (Tick now = 1; now <= 60; ++now) {
        const auto count = transport.poll(one_at_a_time, now);
        for (std::size_t i = 0; i < count; ++i) {
            seen_slowly.push_back(one_at_a_time[i].effect_id);
            transport.acknowledge(one_at_a_time[i]);
        }
    }

    SimulatedVenue reference(0xD00D, FaultPolicy{}, VenueId{9});
    std::vector<EffectId> seen_at_once;
    for (std::uint64_t id = 1; id <= 6; ++id) {
        (void)reference.send(submit_effect(id), Tick{id});
    }
    for (Tick now = 1; now <= 60; ++now) {
        for (const auto& observation : reference.poll(now)) {
            seen_at_once.push_back(observation.effect_id);
            reference.acknowledge_observation(observation);
        }
    }

    require(!seen_slowly.empty(), "the slow drain saw nothing at all");
    require(seen_slowly == seen_at_once,
            "draining one observation at a time lost or reordered evidence that "
            "a single wide poll delivered");
}

}  // namespace

int main() {
    try {
        test_a_clean_two_step_send_is_one_accepted();
        test_nothing_happens_at_the_venue_before_step_one_succeeds();
        test_step_one_ambiguity_is_not_step_two_ambiguity();
        test_an_ambiguous_submission_is_never_abandoned();
        test_a_failure_after_step_one_abandons_the_unsigned_order();
        test_the_simulator_behaves_identically_through_the_seam();
        test_a_poll_never_overruns_the_caller_s_buffer();
        test_an_undelivered_observation_survives_truncation();
        std::cout << "all V2 venue transport tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 venue transport test failed: " << error.what() << "\n";
        return 1;
    }
}
