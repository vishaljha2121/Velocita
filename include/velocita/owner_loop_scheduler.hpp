#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace velocita {

struct OwnerLoopConfig {
    std::chrono::milliseconds tick_interval{1};
    std::chrono::nanoseconds client_safety_margin{std::chrono::microseconds{250}};
    int peer_fragment_limit{64};
    int client_fragment_limit{1};
    std::size_t raft_output_drain_limit{128};
    std::size_t completed_response_drain_limit{64};
    std::size_t peer_offer_limit{64};
    std::size_t response_offer_limit{16};
    std::size_t peer_queue_capacity{4096};
    std::size_t response_queue_capacity{4096};
};

enum class ClientPollBlockReason {
    none,
    tick_slack,
    response_backlog,
};

struct ClientPollDecision {
    int fragment_limit{};
    ClientPollBlockReason blocked_by{ClientPollBlockReason::none};
};

struct OwnerLoopWorkBudget {
    std::size_t raft_outputs_remaining{};
    std::size_t completed_responses_remaining{};
    std::size_t peer_offers_remaining{};
};

struct OwnerLoopIterationResult {
    bool ticked{};
    int peer_fragments{};
    int client_fragments{};
    std::size_t progressed{};
    ClientPollBlockReason client_blocked_by{ClientPollBlockReason::none};
    OwnerLoopWorkBudget remaining{};
};

class OwnerLoopScheduler {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit OwnerLoopScheduler(OwnerLoopConfig config, const TimePoint started)
        : config_(config), last_tick_(started) {
        if (config_.tick_interval <= std::chrono::milliseconds::zero()) {
            throw std::invalid_argument("owner-loop tick interval must be positive");
        }
        if (config_.client_safety_margin < std::chrono::nanoseconds::zero() ||
            config_.client_safety_margin >= config_.tick_interval) {
            throw std::invalid_argument(
                "owner-loop client safety margin must be non-negative and below the tick interval");
        }
        if (config_.peer_fragment_limit <= 0 || config_.client_fragment_limit <= 0 ||
            config_.raft_output_drain_limit == 0 ||
            config_.completed_response_drain_limit == 0 ||
            config_.peer_offer_limit == 0 || config_.response_offer_limit == 0) {
            throw std::invalid_argument("owner-loop work limits must be positive");
        }
        if (config_.peer_queue_capacity == 0 || config_.response_queue_capacity == 0) {
            throw std::invalid_argument("owner-loop queue capacities must be positive");
        }
    }

    [[nodiscard]] const OwnerLoopConfig& config() const noexcept { return config_; }

    [[nodiscard]] OwnerLoopWorkBudget new_iteration_budget() const noexcept {
        return {
            config_.raft_output_drain_limit,
            config_.completed_response_drain_limit,
            config_.peer_offer_limit,
        };
    }

    [[nodiscard]] TimePoint next_tick_deadline() const noexcept {
        return last_tick_ + config_.tick_interval;
    }

    [[nodiscard]] std::optional<int> consume_due_tick(const TimePoint now) noexcept {
        if (now < next_tick_deadline()) return std::nullopt;
        const auto gap = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - last_tick_).count();
        if (static_cast<std::uint64_t>(gap) > maximum_tick_gap_nanoseconds_) {
            maximum_tick_gap_nanoseconds_ = static_cast<std::uint64_t>(gap);
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick_);
        const auto maximum = std::chrono::milliseconds{std::numeric_limits<int>::max()};
        if (elapsed > maximum) elapsed = maximum;
        last_tick_ += elapsed;
        return static_cast<int>(elapsed.count());
    }

    [[nodiscard]] ClientPollDecision client_poll_decision(
        const TimePoint now, const bool response_capacity_available) const noexcept {
        if (!response_capacity_available) {
            return {0, ClientPollBlockReason::response_backlog};
        }
        const auto deadline = next_tick_deadline();
        if (now >= deadline || deadline - now <= config_.client_safety_margin) {
            return {0, ClientPollBlockReason::tick_slack};
        }
        return {config_.client_fragment_limit, ClientPollBlockReason::none};
    }

    [[nodiscard]] std::uint64_t maximum_tick_gap_nanoseconds() const noexcept {
        return maximum_tick_gap_nanoseconds_;
    }

private:
    OwnerLoopConfig config_;
    TimePoint last_tick_;
    std::uint64_t maximum_tick_gap_nanoseconds_{};
};

// The callable types remain template parameters so production uses static
// dispatch. Tests can inject a fake clock and fake pollers without adding
// std::function allocation or virtual dispatch to the owner loop.
template <typename ClockSource, typename Tick, typename PeerPoll,
          typename RaftOutputDrain, typename PeerOffer,
          typename ResponseCapacity, typename ClientPoll,
          typename ResponseOffer>
[[nodiscard]] OwnerLoopIterationResult run_owner_loop_iteration(
    OwnerLoopScheduler& scheduler,
    ClockSource&& clock,
    Tick&& tick,
    PeerPoll&& poll_peer,
    RaftOutputDrain&& drain_raft_outputs,
    PeerOffer&& offer_peer_messages,
    ResponseCapacity&& response_capacity_available,
    ClientPoll&& poll_client,
    ResponseOffer&& offer_responses) {
    OwnerLoopIterationResult result;
    result.remaining = scheduler.new_iteration_budget();

    if (const auto elapsed = scheduler.consume_due_tick(clock())) {
        tick(*elapsed);
        result.ticked = true;
    }

    result.peer_fragments = poll_peer(scheduler.config().peer_fragment_limit);
    result.progressed += drain_raft_outputs(
        result.remaining.raft_outputs_remaining,
        result.remaining.completed_responses_remaining);
    result.progressed += offer_peer_messages(result.remaining.peer_offers_remaining);

    const auto client_decision = scheduler.client_poll_decision(
        clock(), response_capacity_available());
    result.client_blocked_by = client_decision.blocked_by;
    if (client_decision.fragment_limit > 0) {
        result.client_fragments = poll_client(client_decision.fragment_limit);
    }

    result.progressed += drain_raft_outputs(
        result.remaining.raft_outputs_remaining,
        result.remaining.completed_responses_remaining);
    result.progressed += offer_peer_messages(result.remaining.peer_offers_remaining);
    result.progressed += offer_responses(scheduler.config().response_offer_limit);
    return result;
}

}  // namespace velocita
