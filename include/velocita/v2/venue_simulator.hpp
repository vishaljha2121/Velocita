#pragma once

#include "velocita/v2/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace velocita::v2::sim {

// Test-only venue with caller-supplied ticks and deterministic seeded faults.

using Tick = std::uint64_t;

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
    partial_window,      // a query answers truthfully but not for the whole window
};

// Classes roll independently; rate limits and disconnects prevent book mutation.
struct FaultSet {
    bool delay{};
    bool loss{};
    bool duplication{};
    bool reorder{};
    bool partial_fill{};
    bool fill_after_cancel{};
    bool disconnect{};
    bool rate_limit{};
    bool inconsistent_query{};
    bool partial_window{};

    [[nodiscard]] constexpr bool any() const noexcept {
        return delay || loss || duplication || reorder || partial_fill ||
               fill_after_cancel || disconnect || rate_limit ||
               inconsistent_query || partial_window;
    }

    [[nodiscard]] constexpr bool contains(const Fault fault) const noexcept {
        switch (fault) {
            case Fault::none: return !any();
            case Fault::delay: return delay;
            case Fault::loss: return loss;
            case Fault::duplication: return duplication;
            case Fault::reorder: return reorder;
            case Fault::partial_fill: return partial_fill;
            case Fault::fill_after_cancel: return fill_after_cancel;
            case Fault::disconnect: return disconnect;
            case Fault::rate_limit: return rate_limit;
            case Fault::inconsistent_query: return inconsistent_query;
            case Fault::partial_window: return partial_window;
        }
        return false;
    }

    [[nodiscard]] constexpr std::size_t count() const noexcept {
        return static_cast<std::size_t>(delay) + loss + duplication + reorder +
               partial_fill + fill_after_cancel + disconnect + rate_limit +
               inconsistent_query + partial_window;
    }

    constexpr bool operator==(const FaultSet&) const noexcept = default;
};

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
    std::uint16_t partial_window_permille{};
    Tick minimum_latency{1};
    Tick maximum_latency{3};
    Tick delay_penalty{25};

    [[nodiscard]] static constexpr FaultPolicy clean() noexcept { return {}; }
};

enum class SendOutcome : std::uint8_t {
    accepted = 1,        // the venue took it
    venue_duplicate,     // the venue already has this client identity
    business_rejected,   // the venue refused it on its own terms
    pre_send_failure,    // it never left us; nothing is at the venue
    ambiguous,           // it may or may not have arrived: UNKNOWN, not failed
    rate_limited,        // refused for pacing; nothing is at the venue
};

// Unavailable is silence; not_found is an explicit absence answer.
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
    bool inconsistent{};
    // A partial window is a prefix, not proof of the complete execution total.
    bool window_complete{true};
    bool ordered_after_relevant_sends{true};
};

// Observations remain owned until acknowledged; restart can redeliver them.
struct ScheduledObservation {
    Tick due{};
    // Preserves natural within-tick order unless the reorder fault applies.
    std::uint64_t ordinal{};
    VenueObservation observation{};
    bool delivered{};
};

struct SimulatedExecution {
    VenueId venue_id{};
    ExecutionId execution_id{};
    ClusterOrderId cluster_order_id{};
    Side side{};
    QuantityLots quantity{};
};

class SimulatedVenue final {
public:
    SimulatedVenue(std::uint64_t seed, FaultPolicy policy, VenueId venue_id) noexcept;

    // Returns the transport outcome; observations arrive asynchronously through poll.
    [[nodiscard]] SendOutcome send(const OrderEffect& effect, Tick now);

    // Returns due observations once per delivery run; acknowledgement removes them.
    [[nodiscard]] std::vector<VenueObservation> poll(Tick now);
    void acknowledge_observation(const VenueObservation& observation) noexcept;
    void restart_delivery() noexcept;

    [[nodiscard]] QueryResult query(ClusterOrderId order, Tick now);

    // Reconnects change session identity; execution deduplication must survive them.
    [[nodiscard]] VenueSessionId session() const noexcept { return session_; }

    [[nodiscard]] const std::vector<FaultSet>& fault_log() const noexcept {
        return fault_log_;
    }
    [[nodiscard]] const std::vector<SimulatedExecution>& executions() const noexcept {
        return executions_;
    }
    // Every observation the venue still owns (not yet acknowledged).
    [[nodiscard]] const std::vector<ScheduledObservation>& owned_observations() const noexcept {
        return scheduled_;
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
    [[nodiscard]] FaultSet choose_faults(bool for_query) noexcept;
    [[nodiscard]] Tick latency(const FaultSet& faults) noexcept;
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
    std::vector<SimulatedExecution> executions_;
    std::vector<FaultSet> fault_log_;
};

}  // namespace velocita::v2::sim
