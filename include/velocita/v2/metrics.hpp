#pragma once

#include "velocita/v2/kernel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace velocita::v2 {

// Phase 4 operational metrics.
//
// Two constraints from ADR 0001 shape everything here.
//
// FIRST: metrics are disposable projections and never trading authority. So
// `collect_metrics` is a PURE FUNCTION OF STATE. It adds no field to
// TradingState, participates in no digest, and cannot be read back by the
// reducer. Anything a metric would need that state does not already carry is a
// signal to question the metric, not to grow authoritative state.
//
// SECOND: apply reads no clock. Section 15 asks for "oldest pending age" and
// "decision-to-effect latency", which are durations - and a duration needs a
// clock the reducer is forbidden to have. Every age here is therefore measured
// in STATE VERSIONS, not nanoseconds. A state version is a monotonic count of
// applied commands, so the quantity is exact, deterministic, and identical on
// every replica; converting it to wall-clock time is the exporter's job, on the
// exporter's clock, outside the authority boundary.
//
// That trade is worth understanding rather than working around. State-version
// age answers "how many decisions has the cluster made while this sat unresolved"
// which is arguably the better operational question: it is load-relative, so a
// stuck slot looks equally stuck whether the market is fast or quiet, and it
// cannot drift or jump when a host's clock is corrected.

// Arrays are indexed by the raw enum value, so slot zero is unused and the size
// is max-value plus one. Indexing by value rather than by a dense mapping keeps
// the exporter trivial and survives new enum members without a lookup table.
inline constexpr std::size_t kClusterOrderStateSlots = 13;
inline constexpr std::size_t kOutboxStateSlots = 10;
inline constexpr std::size_t kReservationStateSlots = 4;

static_assert(static_cast<std::size_t>(ClusterOrderState::conflict) <
              kClusterOrderStateSlots);
static_assert(static_cast<std::size_t>(OutboxState::incomplete) < kOutboxStateSlots);
static_assert(static_cast<std::size_t>(ReservationState::released) <
              kReservationStateSlots);

//: Per risk account, keyed positionally to the account arena.
struct RiskAccountMetrics {
    AccountId account_id{};
    InstrumentId instrument_id{};
    QuantityLots reserved_quantity{};
    QuantityLots position_quantity{};
    MoneyMicros reserved_margin{};
    MoneyMicros reserved_fee{};
    MoneyMicros reserved_exposure{};
    // Headroom against the configured aggregate ceilings, so an exporter can
    // alert on approaching exhaustion without republishing the limits.
    QuantityLots quantity_headroom{};
    MoneyMicros exposure_headroom{};
    RiskVersion risk_version{};
    RiskPolicyVersion risk_policy_version{};

    constexpr auto operator<=>(const RiskAccountMetrics&) const noexcept = default;
};

struct TradingMetrics {
    std::uint64_t state_version{};

    // Arena occupancy. Capacity is a compile-time constant, so an exporter can
    // publish a utilisation ratio without the kernel computing one.
    std::uint16_t decision_count{};
    std::uint16_t order_count{};
    std::uint16_t reservation_count{};
    std::uint16_t outbox_effect_count{};
    std::uint16_t receipt_count{};
    std::uint16_t risk_account_count{};
    std::uint16_t source_event_count{};

    std::array<std::uint16_t, kClusterOrderStateSlots> orders_by_state{};
    std::array<std::uint16_t, kOutboxStateSlots> effects_by_state{};
    std::array<std::uint16_t, kReservationStateSlots> reservations_by_state{};

    // Counts of records that hold exposure the cluster cannot yet resolve. These
    // are the numbers an operator watches: a nonzero, non-decreasing unresolved
    // count with a rising age is the signature of a stuck slot.
    std::uint16_t unresolved_orders{};
    std::uint16_t unsettled_effects{};

    // Ages in STATE VERSIONS, measured from the authorizing command. Zero when
    // the corresponding population is empty.
    std::uint64_t oldest_unresolved_order_age{};
    std::uint64_t oldest_unsettled_effect_age{};
    // G-074a: a slot held by an order stuck in reconciling has no timeout and no
    // automatic escalation, so this is the only way it becomes visible before an
    // operator notices the book has gone quiet.
    std::uint64_t oldest_reconciling_order_age{};
    std::uint64_t oldest_conflicted_order_age{};

    // Decision-to-authorization distance in state versions, the closest exact
    // analogue of the plan's decision-to-effect latency that a clockless reducer
    // can supply.
    std::uint64_t max_decision_to_authorization_distance{};

    // Retained monotonic counters, surfaced rather than recomputed.
    std::uint64_t reclaimed_group_count{};
    std::uint64_t settled_incomplete_effect_count{};

    StrategyMode strategy_mode{StrategyMode::halted};
    KillEpoch kill_epoch{};
    ExecutorGeneration executor_generation{};

    std::uint16_t risk_account_metric_count{};
    std::array<RiskAccountMetrics, kMaxRiskAccounts> risk_accounts{};

    constexpr auto operator<=>(const TradingMetrics&) const noexcept = default;
};

static_assert(std::is_trivially_copyable_v<TradingMetrics>);

// Pure, bounded, allocation-free. Reads no clock and mutates nothing.
[[nodiscard]] TradingMetrics collect_metrics(const TradingState& state) noexcept;

// Counters that cannot be derived from a state snapshot because they describe
// COMMAND OUTCOMES rather than retained records: a duplicate that returned its
// retained result leaves no trace in state, which is exactly what makes it a
// duplicate. They are accumulated by an observer as results flow past, which is
// where a projection belongs; putting them in TradingState would make an
// observability counter part of the state digest and therefore of consensus.
struct OutcomeCounters {
    std::uint64_t applied{};
    std::uint64_t duplicates{};
    std::uint64_t conflicts{};
    std::uint64_t capacity_refusals{};
    std::uint64_t risk_refusals{};
    std::uint64_t control_refusals{};
    std::uint64_t other_refusals{};

    constexpr auto operator<=>(const OutcomeCounters&) const noexcept = default;
};

void observe_outcome(OutcomeCounters& counters, const ApplyResult& result) noexcept;

}  // namespace velocita::v2
