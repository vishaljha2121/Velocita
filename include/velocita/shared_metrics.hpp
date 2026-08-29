#pragma once

#include "velocita/raft_node.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace velocita {

inline constexpr std::uint64_t kMetricsMagic = 0x56454C4D45545231ULL;
inline constexpr std::uint32_t kMetricsVersion = 2;

enum class MetricRole : std::uint64_t {
    none = 0,
    follower = 1,
    candidate = 2,
    leader = 3,
};

enum class OfferChannel {
    peer,
    response,
};

enum class OfferOutcome {
    success,
    not_connected,
    back_pressured,
    admin_action,
    failed,
};

struct alignas(64) MetricCell {
    std::atomic<std::uint64_t> value{};
    std::byte padding[64 - sizeof(std::atomic<std::uint64_t>)]{};
};

struct alignas(64) MetricsHeader {
    std::uint64_t magic{};
    std::uint32_t version{};
    std::uint32_t page_size{};
    std::int32_t node_id{};
    std::uint32_t reserved{};
    std::uint64_t process_start_time_seconds{};
    std::uint64_t writer_pid{};
    std::byte padding[24]{};
};

struct alignas(64) SharedMetricsPage {
    MetricsHeader header;
    MetricCell active;
    MetricCell last_tick_monotonic_nanoseconds;
    MetricCell tick_total;
    MetricCell owner_loop_iterations_total;
    MetricCell owner_loop_max_gap_nanoseconds;
    MetricCell client_budget_exhausted_total;
    MetricCell peer_budget_exhausted_total;
    MetricCell client_poll_skipped_tick_slack_total;
    MetricCell client_poll_skipped_response_backlog_total;
    MetricCell status_sequence;
    MetricCell role;
    MetricCell leader_id;
    MetricCell term;
    MetricCell current_index;
    MetricCell commit_index;
    MetricCell applied_index;
    MetricCell counter_value;
    MetricCell observed_role_changes_total;
    MetricCell observed_leader_changes_total;
    MetricCell observed_term_changes_total;
    MetricCell request_fragments_total;
    MetricCell request_bytes_total;
    MetricCell rejected_requests_total;
    MetricCell peer_fragments_total;
    MetricCell peer_bytes_total;
    MetricCell rejected_peer_messages_total;
    MetricCell outbound_peer_messages_total;
    MetricCell dropped_peer_messages_total;
    MetricCell completed_responses_total;
    MetricCell peer_queue_depth;
    MetricCell peer_queue_high_watermark;
    MetricCell response_queue_depth;
    MetricCell response_queue_high_watermark;
    MetricCell peer_offer_success_total;
    MetricCell peer_offer_not_connected_total;
    MetricCell peer_offer_back_pressured_total;
    MetricCell peer_offer_admin_action_total;
    MetricCell peer_offer_failed_total;
    MetricCell response_offer_success_total;
    MetricCell response_offer_not_connected_total;
    MetricCell response_offer_back_pressured_total;
    MetricCell response_offer_admin_action_total;
    MetricCell response_offer_failed_total;
};

static_assert(sizeof(MetricCell) == 64);
static_assert(sizeof(MetricsHeader) == 64);
static_assert(alignof(SharedMetricsPage) == 64);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "shared metrics require lock-free 64-bit atomics");

struct MetricsSnapshot {
    MetricsHeader header;
    bool active{};
    std::uint64_t last_tick_monotonic_nanoseconds{};
    std::uint64_t tick_total{};
    std::uint64_t owner_loop_iterations_total{};
    std::uint64_t owner_loop_max_gap_nanoseconds{};
    std::uint64_t client_budget_exhausted_total{};
    std::uint64_t peer_budget_exhausted_total{};
    std::uint64_t client_poll_skipped_tick_slack_total{};
    std::uint64_t client_poll_skipped_response_backlog_total{};
    MetricRole role{MetricRole::none};
    std::int64_t leader_id{-1};
    std::uint64_t term{};
    std::uint64_t current_index{};
    std::uint64_t commit_index{};
    std::uint64_t applied_index{};
    std::int64_t counter_value{};
    std::uint64_t observed_role_changes_total{};
    std::uint64_t observed_leader_changes_total{};
    std::uint64_t observed_term_changes_total{};
    std::uint64_t request_fragments_total{};
    std::uint64_t request_bytes_total{};
    std::uint64_t rejected_requests_total{};
    std::uint64_t peer_fragments_total{};
    std::uint64_t peer_bytes_total{};
    std::uint64_t rejected_peer_messages_total{};
    std::uint64_t outbound_peer_messages_total{};
    std::uint64_t dropped_peer_messages_total{};
    std::uint64_t completed_responses_total{};
    std::uint64_t peer_queue_depth{};
    std::uint64_t peer_queue_high_watermark{};
    std::uint64_t response_queue_depth{};
    std::uint64_t response_queue_high_watermark{};
    std::uint64_t peer_offer_success_total{};
    std::uint64_t peer_offer_not_connected_total{};
    std::uint64_t peer_offer_back_pressured_total{};
    std::uint64_t peer_offer_admin_action_total{};
    std::uint64_t peer_offer_failed_total{};
    std::uint64_t response_offer_success_total{};
    std::uint64_t response_offer_not_connected_total{};
    std::uint64_t response_offer_back_pressured_total{};
    std::uint64_t response_offer_admin_action_total{};
    std::uint64_t response_offer_failed_total{};
};

class SharedMetrics {
public:
    SharedMetrics(std::int32_t node_id, const std::filesystem::path& path);
    ~SharedMetrics();
    SharedMetrics(const SharedMetrics&) = delete;
    SharedMetrics& operator=(const SharedMetrics&) = delete;

    void record_tick(std::uint64_t monotonic_nanoseconds) noexcept;
    void record_owner_loop(std::uint64_t iterations,
                           std::uint64_t maximum_gap_nanoseconds,
                           std::uint64_t client_budget_exhausted,
                           std::uint64_t peer_budget_exhausted,
                           std::uint64_t client_skipped_tick_slack,
                           std::uint64_t client_skipped_response_backlog) noexcept;
    void record_status(const NodeStatus& status) noexcept;
    void record_request(std::size_t bytes) noexcept;
    void record_rejected_request() noexcept;
    void record_peer_fragment(std::size_t bytes) noexcept;
    void record_rejected_peer_message() noexcept;
    void record_outbound_peer_message() noexcept;
    void record_dropped_peer_message() noexcept;
    void record_completed_response() noexcept;
    void set_peer_queue_depth(std::uint64_t depth) noexcept;
    void set_response_queue_depth(std::uint64_t depth) noexcept;
    void record_offer(OfferChannel channel, OfferOutcome outcome) noexcept;

private:
    static void increment(MetricCell& cell) noexcept;
    static void add(MetricCell& cell, std::uint64_t value) noexcept;
    static void set_depth(MetricCell& depth, MetricCell& high_watermark,
                          std::uint64_t value) noexcept;

    int file_descriptor_{-1};
    SharedMetricsPage* page_{};
    std::size_t mapped_size_{};
    MetricRole last_role_{MetricRole::none};
    std::int64_t last_leader_id_{-1};
    std::uint64_t last_term_{};
    bool have_status_{};
};

[[nodiscard]] std::optional<MetricsSnapshot> read_shared_metrics(
    const std::filesystem::path& path);

}  // namespace velocita
