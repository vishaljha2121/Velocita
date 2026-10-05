// Read-only metrics computed from the trading state.
#pragma once

#include "velocita/v2/kernel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace velocita::v2 {

inline constexpr std::size_t kClusterOrderStateSlots = 13;
inline constexpr std::size_t kOutboxStateSlots = 10;
inline constexpr std::size_t kReservationStateSlots = 4;

static_assert(static_cast<std::size_t>(ClusterOrderState::conflict) <
              kClusterOrderStateSlots);
static_assert(static_cast<std::size_t>(OutboxState::incomplete) < kOutboxStateSlots);
static_assert(static_cast<std::size_t>(ReservationState::released) <
              kReservationStateSlots);

struct RiskAccountMetrics {
    AccountId account_id{};
    InstrumentId instrument_id{};
    QuantityLots reserved_quantity{};
    QuantityLots position_quantity{};
    MoneyMicros reserved_margin{};
    MoneyMicros reserved_fee{};
    MoneyMicros reserved_exposure{};
    QuantityLots quantity_headroom{};
    MoneyMicros exposure_headroom{};
    RiskVersion risk_version{};
    RiskPolicyVersion risk_policy_version{};

    constexpr auto operator<=>(const RiskAccountMetrics&) const noexcept = default;
};

struct TradingMetrics {
    std::uint64_t state_version{};

    std::uint16_t decision_count{};
    std::uint16_t order_count{};
    std::uint16_t reservation_count{};
    std::uint16_t outbox_effect_count{};
    std::uint16_t receipt_count{};
    std::uint16_t risk_account_count{};
    std::uint16_t source_event_count{};
    std::uint16_t accepted_venue_event_count{};
    std::uint16_t venue_event_headroom{};
    std::uint16_t caller_cursor_count{};
    std::uint16_t caller_cursor_headroom{};

    std::array<std::uint16_t, kClusterOrderStateSlots> orders_by_state{};
    std::array<std::uint16_t, kOutboxStateSlots> effects_by_state{};
    std::array<std::uint16_t, kReservationStateSlots> reservations_by_state{};

    std::uint16_t unresolved_orders{};
    std::uint16_t unsettled_effects{};

    std::uint64_t oldest_unresolved_order_age{};
    std::uint64_t oldest_unsettled_effect_age{};
    std::uint64_t oldest_reconciling_order_age_since_authorization{};
    std::uint64_t oldest_conflicted_order_age{};

    std::uint64_t max_decision_to_authorization_distance{};

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

[[nodiscard]] TradingMetrics collect_metrics(const TradingState& state) noexcept;

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

}
