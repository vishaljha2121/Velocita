#pragma once

#include "velocita/v2/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace velocita::v2::sim {

// A deterministic adversarial venue.
//
// TEST-ONLY BY CONSTRUCTION. This lives in its own CMake library that no
// production target links, so "test-only" is a build fact rather than a comment
// somebody has to honour. Architecture 9.1.6 records the decision: promoting a
// test double to a product later is easier than discovering a test double in
// production.
//
// Two properties make it useful rather than merely present:
//
//   VIRTUAL TIME. The simulator never reads a clock. Every deadline is a tick
//   count supplied by the caller, so a timeout test is exact instead of slow and
//   flaky, and a fault matrix replays identically on a loaded CI box and an idle
//   laptop. This is the same discipline `OwnerLoopScheduler` established in V1.1.
//
//   SEEDED FAULTS. Every adversarial choice comes from a splitmix64 stream keyed
//   by (seed, operation ordinal). Re-running a seed reproduces the exact fault
//   sequence, which is what makes a failure in the G-033 matrix reportable
//   instead of anecdotal. The seed is required evidence in the Phase 5 gate.

using Tick = std::uint64_t;

// The nine adversarial classes G-033 names, plus the clean case. One fault
// applies per operation, chosen deterministically.
enum class Fault : std::uint8_t {
    none = 0,
    delay,               // the response arrives, late
    loss,                // the response never arrives at all
    duplication,         // the response arrives twice
    reorder,             // responses arrive out of their natural order
    partial_fill,        // the order fills in pieces rather than at once
    fill_after_cancel,   // a fill lands after cancellation was confirmed
    disconnect,          // the session ends; a new one gets a new identity
    rate_limit,          // the venue refuses to accept right now
    inconsistent_query,  // a query disagrees with what was already reported
};

// Probabilities in permille so a policy is exactly reproducible and expressible
// without floating point, matching the fixed-point discipline used everywhere
// else in this codebase.
struct FaultPolicy {
    std::uint16_t delay_permille{};
    std::uint16_t loss_permille{};
    std::uint16_t duplication_permille{};
    std::uint16_t reorder_permille{};
    std::uint16_t partial_fill_permille{};
    std::uint16_t fill_after_cancel_permille{};
    std::uint16_t disconnect_permille{};
    std::uint16_t rate_limit_permille{};
    std::uint16_t inconsistent_query_permille{};

    // Latency band, in ticks, for a response that is not lost.
    Tick minimum_latency{1};
    Tick maximum_latency{3};
    // Extra ticks a delayed response waits beyond the normal band.
    Tick delay_penalty{25};

    [[nodiscard]] static constexpr FaultPolicy clean() noexcept { return {}; }
};

// What the transport reports at the moment of sending. These are the five
// distinct outcomes G-030 requires, and keeping them distinct is the whole
// point: collapsing `timeout` into `rejected` is how a system convinces itself
// an order does not exist when it does.
enum class SendOutcome : std::uint8_t {
    accepted = 1,        // the venue took it
    venue_duplicate,     // the venue already has this client identity
    business_rejected,   // the venue refused it on its own terms
    pre_send_failure,    // it never left us; nothing is at the venue
    ambiguous,           // it may or may not have arrived: UNKNOWN, not failed
    rate_limited,        // refused for pacing; nothing is at the venue
};

// What a reconciliation query can say. `unavailable` is deliberately distinct
// from "no such order": one means the venue would not answer, the other means it
// answered that nothing exists. Treating them alike is how reconciliation
// invents a fact.
enum class QueryOutcome : std::uint8_t {
    known = 1,
    not_found,
    unavailable,
};

struct QueryResult {
    QueryOutcome outcome{QueryOutcome::unavailable};
    VenueOrderId venue_order_id{};
    QuantityLots filled_quantity{};
    QuantityLots remaining_quantity{};
    bool cancelled{};
    // True when this answer was deliberately corrupted by the
    // `inconsistent_query` fault, so a test can assert the gateway treated it as
    // a conflict rather than as truth.
    bool inconsistent{};
};

// One scheduled observation, owned by the simulator until its tick arrives.
struct ScheduledObservation {
    Tick due{};
    // Preserves natural order within a tick so `reorder` is a deliberate act
    // rather than an artefact of container ordering.
    std::uint64_t ordinal{};
    VenueObservation observation{};
};

class SimulatedVenue final {
public:
    SimulatedVenue(std::uint64_t seed, FaultPolicy policy, VenueId venue_id) noexcept;

    // Offer an effect to the venue at `now`. Returns what the transport saw.
    // Any resulting observations are scheduled, not returned: a venue answers
    // later, and pretending otherwise would hide every timing bug this exists to
    // find.
    [[nodiscard]] SendOutcome send(const OrderEffect& effect, Tick now);

    // Observations whose due tick has arrived, in delivery order. Draining is
    // the caller's business; the simulator holds nothing back once due.
    [[nodiscard]] std::vector<VenueObservation> poll(Tick now);

    // Reconciliation query. This is the API that makes send-then-reconcile
    // possible, and therefore the reason the gateway need not record intent in
    // consensus before sending (architecture 9.1.6).
    [[nodiscard]] QueryResult query(ClusterOrderId order, Tick now);

    // The current session. A disconnect retires it and issues a new one, which
    // is what makes a venue-scoped deduplication key unsafe across reconnects.
    [[nodiscard]] VenueSessionId session() const noexcept { return session_; }

    [[nodiscard]] const std::vector<Fault>& fault_log() const noexcept {
        return fault_log_;
    }
    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

private:
    struct Resting {
        ClusterOrderId cluster_order_id{};
        EffectId effect_id{};
        VenueOrderId venue_order_id{};
        Side side{};
        PriceTicks price{};
        QuantityLots original{};
        QuantityLots filled{};
        bool cancelled{};
    };

    [[nodiscard]] std::uint64_t next_random() noexcept;
    [[nodiscard]] bool roll(std::uint16_t permille) noexcept;
    [[nodiscard]] Fault choose_fault(bool for_query) noexcept;
    [[nodiscard]] Tick latency(Fault fault) noexcept;
    void schedule(VenueObservation observation, Tick due, bool duplicate, bool reorder);
    [[nodiscard]] VenueObservation base_observation(
        VenueObservationKind kind, const Resting& order) noexcept;
    [[nodiscard]] Resting* find(ClusterOrderId order) noexcept;

    std::uint64_t seed_;
    std::uint64_t random_state_;
    FaultPolicy policy_;
    VenueId venue_id_;
    VenueSessionId session_{1};
    std::uint64_t venue_sequence_{0};
    std::uint64_t venue_order_counter_{0};
    std::uint64_t execution_counter_{0};
    std::uint64_t ordinal_{0};
    std::vector<Resting> resting_;
    std::vector<ScheduledObservation> scheduled_;
    std::vector<Fault> fault_log_;
};

}  // namespace velocita::v2::sim
