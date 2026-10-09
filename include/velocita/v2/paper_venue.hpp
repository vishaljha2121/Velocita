// A simulated True Markets venue: fill-or-kill at the limit price with measured fees.
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

using sim::QueryOutcome;
using sim::QueryResult;
using sim::SendOutcome;
using sim::Tick;

inline constexpr std::size_t kMaxPaperOrders = 256;
inline constexpr std::size_t kMaxPaperObservations = 128;
inline constexpr std::uint8_t kMaxPaperSplitPieces = 4;
inline constexpr std::uint32_t kBasisPointsPerUnit = 10'000;

struct PaperVenueConfig {
    VenueInstrumentSpec spec{};
    InstrumentExecutionProfile profile{};
    std::uint32_t buy_fee_bps{};
    std::uint32_t sell_fee_bps{};
    MoneyMicros quote_balance{};
    QuantityLots base_balance{};
    Tick latency{1};
};

[[nodiscard]] bool valid(const PaperVenueConfig& config) noexcept;

struct PaperFaultPolicy {
    std::uint16_t delay_permille{};
    std::uint16_t duplicate_permille{};
    std::uint16_t reorder_permille{};
    std::uint16_t drop_request_permille{};
    std::uint16_t drop_response_permille{};
    std::uint16_t reject_permille{};
    std::uint16_t split_permille{};
    std::uint16_t query_unavailable_permille{};
    Tick delay_ticks{25};
    std::uint8_t split_pieces{2};

    [[nodiscard]] static constexpr PaperFaultPolicy clean() noexcept { return {}; }
};

struct PaperFaultRates {
    std::uint64_t reject_permille{};
    std::uint64_t split_permille{};
    std::uint64_t lost_request_permille{};
    std::uint64_t lost_response_permille{};
};

[[nodiscard]] std::optional<PaperFaultPolicy> paper_fault_policy(const PaperFaultRates& rates) noexcept;

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

struct PaperCommittedLedger {
    std::int64_t committed_through{};
    std::int64_t fee_micros_total{};
    std::int64_t volume_micros_buy{};
    std::int64_t volume_micros_sell{};
    double last_fill_wall_seconds{};

    constexpr bool operator==(const PaperCommittedLedger&) const noexcept = default;
};

struct PaperOwnedReport {
    std::uint64_t ordinal{};
    VenueObservation observation{};
    bool withheld{};
};

struct PaperVenueImage {
    VenueId venue_id{};
    InstrumentId instrument_id{};
    std::uint64_t random_state{};
    std::uint64_t venue_sequence{};
    std::uint64_t venue_order_counter{};
    std::uint64_t execution_counter{};
    std::uint64_t ordinal{};
    PaperBalances balances{};
    PaperCommittedLedger ledger{};
    std::size_t order_count{};
    std::array<PaperOrder, kMaxPaperOrders> orders{};
    std::size_t report_count{};
    std::array<PaperOwnedReport, kMaxPaperObservations> reports{};
};

[[nodiscard]] std::optional<MoneyMicros> paper_notional(
    PriceTicks price, QuantityLots quantity,
    MoneyMicros quote_micros_per_tick_lot) noexcept;

[[nodiscard]] std::optional<FeeMicros> paper_fee(MoneyMicros notional,
                                                 std::uint32_t bps) noexcept;

class PaperVenue final {
public:
    PaperVenue(const PaperVenueConfig& config, const PaperFaultPolicy& faults,
               std::uint64_t seed) noexcept;

    [[nodiscard]] bool configured() const noexcept { return configured_; }

    [[nodiscard]] SendOutcome send(const OrderEffect& effect, Tick now) noexcept;

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

    void record_committed(const sim::CommittedEvidence& evidence) noexcept;
    [[nodiscard]] PaperCommittedLedger ledger() const noexcept { return ledger_; }
    void read_book(sim::VenueBook& out) const noexcept;

    void capture(PaperVenueImage& out) const noexcept;
    [[nodiscard]] bool restore(const PaperVenueImage& image) noexcept;

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
    PaperCommittedLedger ledger_{};
    PaperFaults last_faults_{};
    PaperRejectReason last_reject_{PaperRejectReason::none};
    std::size_t order_count_{};
    std::array<PaperOrder, kMaxPaperOrders> orders_{};
    std::size_t scheduled_count_{};
    std::array<Scheduled, kMaxPaperObservations> scheduled_{};
};

[[nodiscard]] sim::VenueTransport paper_venue_transport(PaperVenue& venue) noexcept;
[[nodiscard]] sim::VenueBookSource paper_venue_book(PaperVenue& venue) noexcept;

}
