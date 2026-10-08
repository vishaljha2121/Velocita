// A voter's shared-memory health page for the metrics exporter.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace velocita::v2 {

struct NodeStatus;

inline constexpr std::size_t kNodeHealthLedgers = 4;

struct NodeHealthLedger {
    std::uint64_t account_id{};
    std::uint64_t instrument_id{};
    std::int64_t position_lots{};
    std::int64_t cash_micros{};
    std::int64_t cost_basis_micros{};
    std::int64_t realized_pnl_micros{};
    std::int64_t fees_paid_micros{};
    std::int64_t window_realized_pnl_micros{};
    std::int64_t max_window_loss_micros{};
    constexpr auto operator<=>(const NodeHealthLedger&) const noexcept = default;
};

struct NodeHealth {
    std::int32_t node_id{};
    std::uint64_t pid{};
    std::uint64_t session_epoch{};
    std::uint8_t role{};
    bool ready{};
    bool failed{};
    std::int64_t term{};
    std::int32_t leader_id{};
    std::int64_t current_index{};
    std::int64_t commit_index{};
    std::int64_t applied_index{};
    std::uint64_t durable_index{};
    std::uint64_t turns{};
    std::uint64_t last_turn_monotonic_ns{};
    std::uint64_t auth_refusals{};
    std::uint64_t fence_refusals{};
    std::uint64_t peer_busy_holds{};
    std::uint64_t peer_backpressure{};
    std::uint64_t ingress_refusals{};
    std::uint64_t results_sent{};
    std::uint64_t results_dropped{};
    std::uint64_t secret_reloads{};
    std::uint64_t secret_reload_failures{};
    std::uint64_t retained_ordinary{};
    std::uint64_t retained_protected{};
    std::int64_t compaction_floor{};
    std::int64_t snapshot_index{};
    std::uint64_t stranded_voters{};
    std::uint64_t compactions{};
    std::uint64_t last_compaction_ms{};
    std::uint64_t receipts{};
    std::uint64_t pinned_receipts{};
    std::uint64_t pinned_receipts_peak{};
    std::uint64_t archive_bytes{};
    std::uint64_t archive_byte_limit{};
    std::uint64_t archive_objects{};
    std::uint64_t archive_object_limit{};
    std::uint64_t policies{};
    std::uint64_t ledger_count{};
    std::array<NodeHealthLedger, kNodeHealthLedgers> ledgers{};
    constexpr auto operator<=>(const NodeHealth&) const noexcept = default;
};

[[nodiscard]] std::optional<NodeHealth> read_node_health(const std::filesystem::path& file);

struct NodeHealthContext {
    std::uint64_t pid{};
    std::uint64_t turns{};
    std::uint64_t monotonic_ns{};
    bool failed{};
    std::uint64_t secret_reloads{};
    std::uint64_t secret_reload_failures{};
};

[[nodiscard]] NodeHealth node_health_of(const NodeStatus& status, const NodeHealthContext& context) noexcept;

class NodeHealthPage final {
public:
    [[nodiscard]] static std::unique_ptr<NodeHealthPage> create(const std::filesystem::path& file);
    ~NodeHealthPage();
    NodeHealthPage(const NodeHealthPage&) = delete;
    NodeHealthPage& operator=(const NodeHealthPage&) = delete;
    void publish(const NodeHealth& health) noexcept;

private:
    struct Page;
    friend std::optional<NodeHealth> read_node_health(const std::filesystem::path& file);
    explicit NodeHealthPage(Page* page) noexcept : page_(page) {}
    Page* page_;
};

[[nodiscard]] std::optional<NodeHealth> read_node_health(const std::filesystem::path& file);

}
