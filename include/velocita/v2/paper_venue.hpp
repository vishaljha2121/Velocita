#pragma once

#include "velocita/v2/action_template.hpp"
#include "velocita/v2/contracts.hpp"
#include "velocita/v2/venue_transport.hpp"
#include "velocita/v2/venue_wire.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace velocita::v2::paper {

// Paper TrueMarkets venue behind the VenueTransport seam. Owner decisions it
// encodes: fill-or-kill at the order's limit price, measured per-side cost in
// basis points, long-only, a finite quote budget. Bounded, allocation-free,
// deterministic for a given (config, faults, seed, input sequence).

using sim::QueryOutcome;
using sim::QueryResult;
using sim::SendOutcome;
using sim::Tick;

inline constexpr std::size_t kMaxPaperOrders = 256;
inline constexpr std::size_t kMaxPaperObservations = 128;
inline constexpr std::uint8_t kMaxPaperSplitPieces = 4;
inline constexpr std::uint32_t kBasisPointsPerUnit = 10'000;

struct PaperVenueConfig {
    // Must agree with `profile` under `spec_agrees_with_profile`; the fee is
    // charged on the same notional the kernel reserved against.
    VenueInstrumentSpec spec{};
    InstrumentExecutionProfile profile{};
    std::uint32_t buy_fee_bps{};
    std::uint32_t sell_fee_bps{};
    MoneyMicros quote_balance{};
    QuantityLots base_balance{};
    Tick latency{1};
};

[[nodiscard]] bool valid(const PaperVenueConfig& config) noexcept;

// Each class rolls independently on every send (query_unavailable on every
// query), always consuming a draw, so toggling one class leaves the others'
// draws unchanged.
struct PaperFaultPolicy {
    std::uint16_t delay_permille{};
    std::uint16_t duplicate_permille{};
    std::uint16_t reorder_permille{};
    // The request never reaches the venue; the sender cannot tell.
    std::uint16_t drop_request_permille{};
    // The venue executes, but the response and its reports are lost.
    std::uint16_t drop_response_permille{};
    std::uint16_t reject_permille{};
    std::uint16_t split_permille{};
    std::uint16_t query_unavailable_permille{};
    Tick delay_ticks{25};
    std::uint8_t split_pieces{2};

    [[nodiscard]] static constexpr PaperFaultPolicy clean() noexcept { return {}; }
};

struct PaperFaults {
    bool delay{};
    bool duplicate{};
    bool reorder{};
    bool drop_request{};
    bool drop_response{};
    bool reject{};
    bool split{};
    bool query_unavailable{};

    constexpr bool operator==(const PaperFaults&) const noexcept = default;
};

enum class PaperRejectReason : std::uint8_t {
    none = 0,
    fault,
    not_fill_or_kill,
    insufficient_quote,
    insufficient_base,
    // Fill-or-kill orders never rest, so there is never anything to cancel.
    nothing_resting,
    unsupported_operation,
};

struct PaperBalances {
    MoneyMicros quote{};
    QuantityLots base{};
    FeeMicros fees_paid{};

    constexpr bool operator==(const PaperBalances&) const noexcept = default;
};

struct PaperOrder {
    ClusterOrderId cluster_order_id{};
    EffectId effect_id{};
    VenueOrderId venue_order_id{};
    Side side{};
    PriceTicks price{};
    QuantityLots quantity{};
    MoneyMicros notional{};
    FeeMicros fee{};
};

// quote_micros_per_tick_lot * price * quantity, in the action resolver's
// multiplication order. No value on overflow or a non-positive input.
[[nodiscard]] std::optional<MoneyMicros> paper_notional(
    PriceTicks price, QuantityLots quantity,
    MoneyMicros quote_micros_per_tick_lot) noexcept;

// ceil(notional * bps / 10'000) in micros: a fractional micro is charged to
// us, never forgiven. No value on overflow, negative notional, or bps > 10'000.
[[nodiscard]] std::optional<FeeMicros> paper_fee(MoneyMicros notional,
                                                 std::uint32_t bps) noexcept;

class PaperVenue final {
public:
    PaperVenue(const PaperVenueConfig& config, const PaperFaultPolicy& faults,
               std::uint64_t seed) noexcept;

    [[nodiscard]] bool configured() const noexcept { return configured_; }

    // Outcomes: accepted (filled whole; reports follow through poll),
    // business_rejected (killed whole; an order_rejected report follows),
    // venue_duplicate, pre_send_failure (refused before anything existed at the
    // venue), ambiguous (a dropped request or a dropped response).
    [[nodiscard]] SendOutcome send(const OrderEffect& effect, Tick now) noexcept;

    // Writes at most out.size() due reports in (due, schedule) order; the rest
    // stay owned by the venue and are offered on a later poll.
    [[nodiscard]] std::size_t poll(std::span<VenueObservation> out, Tick now) noexcept;
    void acknowledge(const VenueObservation& observation) noexcept;
    void restart_delivery() noexcept;

    [[nodiscard]] QueryResult query(ClusterOrderId order, Tick now) noexcept;
    [[nodiscard]] VenueSessionId session() const noexcept { return VenueSessionId{1}; }

    [[nodiscard]] PaperBalances balances() const noexcept { return balances_; }
    [[nodiscard]] PaperFaults last_faults() const noexcept { return last_faults_; }
    [[nodiscard]] PaperRejectReason last_reject() const noexcept { return last_reject_; }
    [[nodiscard]] std::size_t order_count() const noexcept { return order_count_; }
    [[nodiscard]] std::size_t pending_observation_count() const noexcept {
        return scheduled_count_;
    }
    [[nodiscard]] const PaperOrder* find_order(ClusterOrderId order) const noexcept;

private:
    struct Scheduled {
        Tick due{};
        std::uint64_t ordinal{};
        VenueObservation observation{};
        bool delivered{};
        bool withheld{};
    };

    [[nodiscard]] std::uint64_t next_random() noexcept;
    [[nodiscard]] bool roll(std::uint16_t permille) noexcept;
    [[nodiscard]] VenueObservation report(VenueObservationKind kind,
                                          const OrderEffect& effect) noexcept;
    void schedule(const VenueObservation& observation, Tick due, bool duplicate,
                  bool withheld) noexcept;
    [[nodiscard]] SendOutcome refuse(const OrderEffect& effect, PaperRejectReason reason,
                                     Tick due, bool duplicate, bool drop_response) noexcept;

    PaperVenueConfig config_;
    PaperFaultPolicy policy_;
    bool configured_{};
    std::uint64_t random_state_;
    std::uint64_t venue_sequence_{};
    std::uint64_t venue_order_counter_{};
    std::uint64_t execution_counter_{};
    std::uint64_t ordinal_{};
    PaperBalances balances_{};
    PaperFaults last_faults_{};
    PaperRejectReason last_reject_{PaperRejectReason::none};
    std::size_t order_count_{};
    std::array<PaperOrder, kMaxPaperOrders> orders_{};
    std::size_t scheduled_count_{};
    std::array<Scheduled, kMaxPaperObservations> scheduled_{};
};

// Trivially copyable handle; the venue must outlive every copy.
[[nodiscard]] sim::VenueTransport paper_venue_transport(PaperVenue& venue) noexcept;

}  // namespace velocita::v2::paper
