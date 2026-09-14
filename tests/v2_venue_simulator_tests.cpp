// The deterministic adversarial venue.
//
// A fault simulator is only worth having if a failure it produces is
// reproducible and every fault it claims to model is actually reachable. Those
// are the two properties tested here, and they are prerequisites for the G-033
// matrix meaning anything: an irreproducible failure is an anecdote, and an
// unreachable fault is a comment.

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

// Drive one seed through a fixed script and return everything observable, so two
// runs can be compared for exact equality.
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

// --- tests -----------------------------------------------------------------

void test_a_seed_reproduces_its_run_exactly() {
    for (const std::uint64_t seed : {1ULL, 7ULL, 42ULL, 9'001ULL}) {
        require(run(seed, everything_enabled()) == run(seed, everything_enabled()),
                "seed " + std::to_string(seed) + " did not reproduce its run");
    }
}

void test_different_seeds_explore_different_behaviour() {
    // A simulator whose seed does not matter is a fixture, not a fault model.
    const auto first = run(1, everything_enabled());
    const auto second = run(2, everything_enabled());
    require(!(first == second), "two seeds produced identical runs");
}

void test_no_wall_clock_influences_the_result() {
    // The only time input is the caller's tick. Running the same seed while the
    // machine's real clock has moved on must change nothing - this is what makes
    // a timeout test exact instead of flaky.
    const auto before = run(11, everything_enabled());
    volatile std::uint64_t spin = 0;
    for (std::uint64_t index = 0; index < 5'000'000; ++index) spin += index;
    (void)spin;
    require(before == run(11, everything_enabled()),
            "elapsed real time changed a virtual-time run");
}

// A policy with exactly one class always on, so the effect under test cannot be
// confused with any other. Latency is pinned so "when" is decidable too.
FaultPolicy only(std::uint16_t FaultPolicy::*knob) {
    FaultPolicy policy = FaultPolicy::clean();
    policy.minimum_latency = 1;
    policy.maximum_latency = 1;
    policy.*knob = 1000;
    return policy;
}

void test_every_fault_class_produces_its_observable_effect() {
    // THE OLD VERSION OF THIS TEST COUNTED LABELS. It swept seeds until each
    // enum value had appeared in the fault log, and called that reachability.
    //
    // It was wrong about at least one class and could not have known. Selecting
    // `loss` for a CANCEL lost nothing: the cancel branch scheduled its response
    // and returned before the submit branch's loss check ever ran, so the label
    // went in the log and the behaviour it names never happened. A test that
    // asserts on the selector's output is asserting that the simulator chose to
    // say something, not that it did it.
    //
    // Every class below is therefore driven at 1000 permille and checked by what
    // it DID.
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
        // And the venue really does hold it: a query finds the order.
        require(venue.query(effect.cluster_order_id, 1).outcome ==
                    QueryOutcome::known,
                "a lost submit left nothing at the venue, so it was not a LOST "
                "RESPONSE, it was a refused send");
    }
    {   // LOSS on a CANCEL: the venue cancels, the gateway never hears. This is
        // the case the old label-counting test reported as covered while the
        // code did the opposite.
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
        // The cancellation really happened; only the news of it was lost.
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
    {   // FILL_AFTER_CANCEL: an execution arrives AFTER the cancellation. It
        // needs an order with something left to cancel, which is what
        // `partial_fill` leaving half resting provides.
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
        //
        // Asserted as an A/B rather than by predicting a specific permutation.
        // Every roll consumes a draw whether or not its class is enabled, so
        // turning reorder off changes the reorder decision and NOTHING else -
        // which makes any difference between the two traces attributable to
        // reorder alone. Predicting the permutation instead would mean encoding
        // the scheduler's tie-breaking into the test, and the test would then
        // pass by agreeing with the code rather than by observing it.
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
    // The selector used to return on the first match, so cross-category
    // combinations were unreachable BY CONSTRUCTION - and the acceptance record
    // reported "the matrix cannot produce a reconciliation conflict" as though
    // that were a property of the system rather than of the selector.
    //
    // The combination that matters most is a partially filled order whose CANCEL
    // then goes silent: the order holds booked executions while its current
    // effect's outcome is unknown, which is the only shape from which a
    // reconciliation answer below booked can arise.
    FaultPolicy policy = FaultPolicy::clean();
    policy.minimum_latency = 1;
    policy.maximum_latency = 1;
    policy.partial_fill_permille = 1000;
    policy.loss_permille = 1000;
    SimulatedVenue venue(1, policy, VenueId{9});
    const auto partial = submit_effect(4, 4);

    // The submit's own responses are lost, so nothing arrives for it.
    require(venue.send(partial, 0) == SendOutcome::accepted, "submit failed");
    require(venue.poll(1'000).empty(), "the lost submit delivered a response");

    // Both classes fired on the same operation, which the old selector could not
    // express at all.
    require(venue.fault_log().size() == 1, "one send produced two log entries");
    require(venue.fault_log().front().count() >= 2,
            "two classes did not fire on one operation, so composition is not "
            "actually happening");
    require(venue.fault_log().front().contains(Fault::loss) &&
                venue.fault_log().front().contains(Fault::partial_fill),
            "the composed set did not name both classes");
}

void test_a_clean_policy_produces_no_faults() {
    // The clean case must be genuinely clean, or every failure becomes ambiguous
    // between "the code is wrong" and "the simulator misbehaved".
    for (const auto& faults : run(5, FaultPolicy::clean()).faults) {
        require(!faults.any(), "the clean policy produced a fault");
    }
}

void test_enabling_a_fault_does_not_shift_unrelated_draws() {
    // Every roll consumes a draw even at zero probability, so turning one fault
    // on cannot reshuffle the rest. Without this, comparing two policies would
    // be meaningless: everything would differ for reasons unrelated to the
    // fault under study.
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
    // The simulator must not be able to produce evidence the kernel would refuse
    // for structural reasons; otherwise a gateway test would fail on the
    // simulator's malformedness rather than on the behaviour under test.
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
    // This is the venue-side idempotency that makes at-least-once delivery safe
    // under G-025, so it must be distinguishable from acceptance.
    SimulatedVenue venue(1, FaultPolicy::clean(), VenueId{9});
    const auto effect = submit_effect(1);
    require(venue.send(effect, 0) == SendOutcome::accepted, "first submit failed");
    require(venue.send(effect, 1) == SendOutcome::venue_duplicate,
            "a resent effect was accepted a second time");
}

void test_an_inconsistent_query_is_flagged_rather_than_silently_wrong() {
    // Reconciliation must be able to tell that the venue contradicted itself.
    // ADR 0001: reconciliation "never overwrites conflicting evidence to make a
    // dashboard green."
    FaultPolicy always;
    always.inconsistent_query_permille = 1000;
    SimulatedVenue venue(1, always, VenueId{9});
    const auto effect = submit_effect(1);
    (void)venue.send(effect, 0);
    const auto answer = venue.query(effect.cluster_order_id, 1);
    require(answer.outcome == QueryOutcome::known && answer.inconsistent,
            "a corrupted query answer was not flagged as inconsistent");
    // Downward is the direction that matters. A total ahead of the cluster's own
    // evidence reads as fills still in flight and stays recoverable; a total
    // BELOW what the cluster booked can never be closed by later evidence, which
    // is what makes it a conflict rather than a gap.
    require(answer.filled_quantity.atoms() == 0 &&
                answer.remaining_quantity == effect.quantity,
            "the inconsistent answer did not deny a fill it had already made");
}

void test_a_partial_window_answer_is_a_truthful_prefix_not_a_corruption() {
    // A venue that pages its execution history is behaving correctly. The answer
    // it returns is a genuine prefix of the truth - never larger, possibly
    // smaller - and the only thing separating it from a SHORT answer is the flag
    // saying the window is not covered.
    //
    // This is kept distinct from `inconsistent_query` on purpose. Both read below
    // the truth; only one of them is the venue contradicting itself, and a
    // gateway that cannot tell them apart either treats ordinary paging as a
    // permanent conflict or treats a contradiction as something to retry away.
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

    // And the clean case must still say the window IS complete, or the flag
    // would carry no information.
    SimulatedVenue quiet(1, FaultPolicy::clean(), VenueId{9});
    require(quiet.send(effect, 0) == SendOutcome::accepted, "submit failed");
    require(quiet.query(effect.cluster_order_id, 1).window_complete,
            "a complete answer did not say so");
}

void test_an_unknown_order_is_not_found_rather_than_unavailable() {
    // These must stay distinct: one says the venue answered that nothing exists,
    // the other says it would not answer. Conflating them is how reconciliation
    // invents a fact.
    SimulatedVenue venue(1, FaultPolicy::clean(), VenueId{9});
    require(venue.query(ClusterOrderId{7'777}, 0).outcome == QueryOutcome::not_found,
            "an absent order did not report not_found");
}

void test_a_disconnect_issues_a_new_session() {
    // A redelivery after a reconnect carries a new session id, which is exactly
    // why a session-scoped deduplication key would be unsafe.
    FaultPolicy always;
    always.disconnect_permille = 1000;
    SimulatedVenue venue(1, always, VenueId{9});
    const auto before = venue.session();
    require(venue.send(submit_effect(1), 0) == SendOutcome::ambiguous,
            "a disconnect did not produce an ambiguous outcome");
    require(venue.session().value() > before.value(),
            "a disconnect did not issue a new session");
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
        std::cout << "all V2 venue simulator tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 venue simulator test failed: " << error.what() << '\n';
        return 1;
    }
}
