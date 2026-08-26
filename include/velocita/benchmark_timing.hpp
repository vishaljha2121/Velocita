#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace velocita {

inline constexpr std::uint64_t kBenchmarkTimingValid = 1;

struct BenchmarkTiming {
    std::uint64_t flags{};
    std::uint64_t ingress_to_admission_ns{};
    std::uint64_t local_wal_append_ns{};
    std::uint64_t quorum_append_ns{};
    std::uint64_t admission_to_commit_ns{};
    std::uint64_t commit_to_apply_ns{};
};

[[nodiscard]] std::uint64_t benchmark_monotonic_nanoseconds() noexcept;

class BenchmarkTraceTable {
public:
    explicit BenchmarkTraceTable(std::size_t capacity);

    [[nodiscard]] bool begin(std::uint64_t request_id, std::uint64_t ingress_ns,
                             std::uint64_t admission_start_ns) noexcept;
    void record_wal(std::uint64_t request_id, std::uint64_t duration_ns) noexcept;
    void record_admitted(std::uint64_t request_id, std::int64_t index,
                         std::uint64_t admitted_ns) noexcept;
    void record_committed(std::int64_t first_index, std::int64_t last_index,
                          std::uint64_t committed_ns, std::uint64_t quorum_append_ns) noexcept;
    [[nodiscard]] std::optional<BenchmarkTiming> complete(
        std::uint64_t request_id, std::uint64_t applied_ns) noexcept;
    [[nodiscard]] std::uint64_t collision_count() const noexcept { return collision_count_; }

private:
    struct Slot {
        std::uint64_t request_id{};
        std::int64_t index{};
        std::uint64_t ingress_ns{};
        std::uint64_t admission_start_ns{};
        std::uint64_t admitted_ns{};
        std::uint64_t wal_append_ns{};
        std::uint64_t committed_ns{};
        std::uint64_t quorum_append_ns{};
        bool active{};
    };

    struct IndexSlot {
        std::int64_t index{};
        std::uint64_t request_id{};
    };

    [[nodiscard]] Slot* find(std::uint64_t request_id) noexcept;

    std::vector<Slot> slots_;
    std::vector<IndexSlot> indexes_;
    std::size_t mask_{};
    std::uint64_t collision_count_{};
};

}  // namespace velocita
