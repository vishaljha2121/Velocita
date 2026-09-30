#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace velocita::v2 {

// Disposable, read-only diagnostic view of one node (M2 decision G). Never a
// source of authority: a reader seeing "leader" has no permission to send.
struct NodeHealth {
    std::int32_t node_id{};
    std::uint64_t pid{};
    std::uint64_t session_epoch{};
    std::uint8_t role{};         // 0 none, 1 follower, 2 candidate, 3 leader
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
    std::uint64_t retained_ordinary{};   // retained Raft log entries, ordinary pool
    std::uint64_t retained_protected{};  // retained Raft log entries, operator-control reserve
    std::int64_t compaction_floor{};     // highest index every voter is known to hold durably
    std::int64_t snapshot_index{};       // highest index compacted into a snapshot
    std::uint64_t stranded_voters{};     // leader: voters behind its snapshot (need repair)
    std::uint64_t compactions{};
    std::uint64_t last_compaction_ms{};  // Raft is paused while one runs
    constexpr auto operator<=>(const NodeHealth&) const noexcept = default;
};

[[nodiscard]] std::optional<NodeHealth> read_node_health(const std::filesystem::path& file);

// Writer side: owned by the node's owner thread. publish() performs only
// relaxed atomic stores between two sequence increments; no syscall.
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

// Reader side: a consistent copy, or nullopt for a missing, foreign or
// continuously changing page. Retries are bounded.
[[nodiscard]] std::optional<NodeHealth> read_node_health(const std::filesystem::path& file);

}  // namespace velocita::v2
