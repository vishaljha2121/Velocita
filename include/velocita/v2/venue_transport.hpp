#pragma once

#include "velocita/v2/venue_simulator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace velocita::v2::sim {

// Phase 9. THE VENUE BOUNDARY, made explicit.
//
// Until now there was none. `SimulatedVenue` is a concrete class used only by
// tests, and no production code talked to a venue at all - so "the gateway does
// not know which venue it is talking to" was a design intention with nothing
// enforcing it. This is the seam that makes it true, and the thing the True
// Markets adapter will sit behind.
//
// SHAPED AS FUNCTION POINTERS PLUS A CONTEXT, matching `StorageCheckpoints`
// rather than introducing virtual dispatch. It costs nothing at an I/O boundary
// either way; what matters is that the struct is trivially copyable, has no
// ownership, and can be handed to a pure planner without dragging a class
// hierarchy behind it.
//
// WHAT DELIBERATELY DOES NOT APPEAR HERE. Authentication, JWT refresh, rate-limit
// pacing, retry policy, and the venue's two-step DeFi signing flow - where
// `POST /orders` returns unsigned payloads and a second call submits the
// signatures. All of that is the adapter's business. A `send` is one call from
// this side however many round trips it costs on the other, because the gateway
// reasons about EFFECTS and not about HTTP. The moment any of it leaks through
// this struct, the simulator and the venue stop being substitutable and every
// test written against the simulator stops meaning anything about production.

// The largest number of observations one poll may hand back. A bound the CALLER
// owns, not the venue: a venue that floods us must be truncated rather than
// obeyed, and truncation must defer the remainder rather than drop it.
inline constexpr std::size_t kMaxVenueObservationsPerPoll = 32;

struct VenueTransport {
    // Deliver one committed effect. Returns the transport outcome only -
    // economic evidence arrives later and asynchronously through `poll`, which
    // is what makes an ambiguous send representable rather than a guess.
    SendOutcome (*send_fn)(void* context, const OrderEffect& effect, Tick now){};

    // Fill `out` with due observations and return how many were written. Never
    // writes more than `out.size()`; whatever did not fit stays owned by the
    // venue and is offered again.
    std::size_t (*poll_fn)(void* context, std::span<VenueObservation> out, Tick now){};

    // Release one observation. Until this is called the venue still owns it, so
    // a crash between poll and acknowledgement redelivers rather than loses.
    void (*acknowledge_fn)(void* context, const VenueObservation& observation){};

    // Ask the venue about one order. `QueryOutcome::unavailable` is silence and
    // `not_found` is an explicit absence answer; conflating them is how a
    // reconnect becomes an invented fact.
    QueryResult (*query_fn)(void* context, ClusterOrderId order, Tick now){};

    // Session identity. It CHANGES on reconnect, which is why deduplication may
    // never be scoped to it alone.
    VenueSessionId (*session_fn)(void* context){};

    void* context{};

    [[nodiscard]] SendOutcome send(const OrderEffect& effect, const Tick now) const {
        return send_fn(context, effect, now);
    }
    [[nodiscard]] std::size_t poll(const std::span<VenueObservation> out,
                                   const Tick now) const {
        return poll_fn(context, out, now);
    }
    void acknowledge(const VenueObservation& observation) const {
        acknowledge_fn(context, observation);
    }
    [[nodiscard]] QueryResult query(const ClusterOrderId order, const Tick now) const {
        return query_fn(context, order, now);
    }
    [[nodiscard]] VenueSessionId session() const { return session_fn(context); }
};

static_assert(std::is_trivially_copyable_v<VenueTransport>);

// ---------------------------------------------------------------------------
// ADR 0009. A TWO-STEP VENUE SEND IS ONE EFFECT.
//
// The DeFi order path is not one request: `POST /orders` returns unsigned
// payloads and leaves the order `initialized`, "awaiting client signature";
// signing happens locally; a second call submits the signatures and the order
// rests. The gateway never learns any of that, because nothing in the cluster's
// model could use it and everything in it would be complicated by it.
//
// This resolves the steps into ONE `SendOutcome`, and the resolution is the
// whole safety content of the adapter's send path.

enum class StepOutcome : std::uint8_t {
    // The step did what it said.
    succeeded = 1,
    // The step refused, and refused cleanly - we know it did not take effect.
    refused,
    // We do not know. A timeout, a dropped connection, a response we could not
    // read. NOT a failure, and treating it as one is how a live order becomes
    // invisible.
    ambiguous,
    // Refused for pacing. Distinct from `refused` because the caller should slow
    // down rather than conclude anything about the order.
    rate_limited,
};

// The three things the adapter can do, injected so the resolution can be tested
// without a network. `abandon` is best-effort cleanup of an order left
// `initialized`; its outcome is deliberately ignored - see below.
struct TwoStepSend {
    StepOutcome (*prepare_fn)(void* context, const OrderEffect& effect){};
    StepOutcome (*sign_fn)(void* context, const OrderEffect& effect){};
    StepOutcome (*submit_fn)(void* context, const OrderEffect& effect){};
    void (*abandon_fn)(void* context, const OrderEffect& effect){};
    void* context{};
};

// Resolve a two-step send into the single outcome the gateway understands.
//
// STEP ONE'S AMBIGUITY BECOMES `pre_send_failure`, and that is the one claim
// here that could be wrong. An `initialized` order cannot fill - it awaits a
// signature that was never sent - so it holds no exposure, and "nothing is at
// the venue" is true in the only sense the gateway cares about. It rests on the
// documented meaning of `initialized` and fails with it.
//
// STEP TWO'S AMBIGUITY BECOMES `ambiguous`, which is the dangerous one and the
// reason the distinction exists: the order may be resting, and only the
// gateway's reconciliation can find out.
[[nodiscard]] SendOutcome resolve_two_step_send(const TwoStepSend& steps,
                                                const OrderEffect& effect) noexcept;

// The simulator, presented through the seam. Every existing suite keeps talking
// to `SimulatedVenue` directly; this exists so the SAME venue can be driven both
// ways and the two compared, which is the only way the seam's transparency is a
// fact rather than a claim.
[[nodiscard]] VenueTransport simulated_venue_transport(SimulatedVenue& venue) noexcept;

// The venue's own account at one instant, for reconciliation against committed
// kernel state (ch50: compare at one cut). Only the venue answers this; the
// gateway never derives it from what it sent.
inline constexpr std::size_t kMaxVenueBookPendingOrders = 128;

struct VenueBook {
    bool available{};
    QuantityLots base{};
    MoneyMicros quote{};
    bool quote_known{};
    // Reports the venue still owns: the kernel may not have booked them yet.
    std::size_t unacknowledged_reports{};
    // Distinct orders those reports name. Truncated means "cannot compare".
    std::size_t pending_order_count{};
    bool pending_orders_truncated{};
    std::array<ClusterOrderId, kMaxVenueBookPendingOrders> pending_orders{};
    // Highest kernel log index at which evidence this venue released was
    // committed before being acknowledged. A kernel read applied below it is
    // older than the venue's own record and is not yet comparable.
    std::int64_t committed_through{};
    // Running totals of committed fills that outlive a gateway process
    // (a durable venue only; zero otherwise).
    std::int64_t fee_micros_total{};
    std::int64_t volume_micros_buy{};
    std::int64_t volume_micros_sell{};
    double last_fill_wall_seconds{};
};

// Evidence the kernel committed, handed to the venue's book just before the
// matching acknowledge so a durable venue records both in one write.
struct CommittedEvidence {
    std::int64_t index{};
    VenueObservation observation{};
    std::int64_t notional_micros{};  // fills only; 0 when unknown
    double wall_seconds{};
};

struct VenueBookSource {
    void (*read_fn)(void* context, VenueBook& out){};
    void (*committed_fn)(void* context, const CommittedEvidence& evidence){};  // optional
    void* context{};

    [[nodiscard]] bool present() const noexcept { return read_fn != nullptr; }
};

static_assert(std::is_trivially_copyable_v<VenueBookSource>);

// Test and lab venue: base is the signed sum of its executions, pending orders
// are those its undelivered or unacknowledged reports name.
[[nodiscard]] VenueBookSource simulated_venue_book(SimulatedVenue& venue) noexcept;

}  // namespace velocita::v2::sim
