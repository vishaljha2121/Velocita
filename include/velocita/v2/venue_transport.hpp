// The venue boundary: send, poll, acknowledge and query.
#pragma once

#include "velocita/v2/venue_simulator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace velocita::v2::sim {

inline constexpr std::size_t kMaxVenueObservationsPerPoll = 32;

struct VenueTransport {
    SendOutcome (*send_fn)(void* context, const OrderEffect& effect, Tick now){};

    std::size_t (*poll_fn)(void* context, std::span<VenueObservation> out, Tick now){};

    void (*acknowledge_fn)(void* context, const VenueObservation& observation){};

    QueryResult (*query_fn)(void* context, ClusterOrderId order, Tick now){};

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

enum class StepOutcome : std::uint8_t {
    succeeded = 1,
    refused,
    ambiguous,
    rate_limited,
};

struct TwoStepSend {
    StepOutcome (*prepare_fn)(void* context, const OrderEffect& effect){};
    StepOutcome (*sign_fn)(void* context, const OrderEffect& effect){};
    StepOutcome (*submit_fn)(void* context, const OrderEffect& effect){};
    void (*abandon_fn)(void* context, const OrderEffect& effect){};
    void* context{};
};

[[nodiscard]] SendOutcome resolve_two_step_send(const TwoStepSend& steps,
                                                const OrderEffect& effect) noexcept;

[[nodiscard]] VenueTransport simulated_venue_transport(SimulatedVenue& venue) noexcept;

inline constexpr std::size_t kMaxVenueBookPendingOrders = 128;

struct VenueBook {
    bool available{};
    QuantityLots base{};
    MoneyMicros quote{};
    bool quote_known{};
    std::size_t unacknowledged_reports{};
    std::size_t pending_order_count{};
    bool pending_orders_truncated{};
    std::array<ClusterOrderId, kMaxVenueBookPendingOrders> pending_orders{};
    std::int64_t committed_through{};
    std::int64_t fee_micros_total{};
    std::int64_t volume_micros_buy{};
    std::int64_t volume_micros_sell{};
    double last_fill_wall_seconds{};
};

struct CommittedEvidence {
    std::int64_t index{};
    VenueObservation observation{};
    std::int64_t notional_micros{};
    double wall_seconds{};
};

struct VenueBookSource {
    void (*read_fn)(void* context, VenueBook& out){};
    void (*committed_fn)(void* context, const CommittedEvidence& evidence){};
    void* context{};

    [[nodiscard]] bool present() const noexcept { return read_fn != nullptr; }
};

static_assert(std::is_trivially_copyable_v<VenueBookSource>);

[[nodiscard]] VenueBookSource simulated_venue_book(SimulatedVenue& venue) noexcept;

}
