#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace velocita {

enum class BenchmarkPhase : std::uint8_t {
    warmup,
    steady,
};

struct PendingRequest {
    std::uint64_t request_id{};
    std::uint64_t intended_send_ns{};
    std::uint64_t actual_send_ns{};
    std::uint64_t deadline_ns{};
    std::size_t channel_index{};
    std::uint32_t transport_attempts{};
    std::uint32_t retry_count{};
    BenchmarkPhase phase{BenchmarkPhase::warmup};
    bool needs_offer_retry{};
};

class PendingRequestTable {
public:
    explicit PendingRequestTable(std::size_t capacity);

    [[nodiscard]] bool insert(const PendingRequest& request) noexcept;
    [[nodiscard]] PendingRequest* find(std::uint64_t request_id) noexcept;
    [[nodiscard]] const PendingRequest* find(std::uint64_t request_id) const noexcept;
    [[nodiscard]] bool erase(std::uint64_t request_id) noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::span<PendingRequest> slots() noexcept { return slots_; }

private:
    [[nodiscard]] std::size_t slot_for(std::uint64_t request_id) const noexcept;

    std::vector<PendingRequest> slots_;
    std::vector<std::uint8_t> occupied_;
    std::size_t size_{};
};

struct LatencySample {
    std::uint64_t request_id{};
    std::uint64_t scheduled_to_offer_ns{};
    std::uint64_t offer_to_response_ns{};
    std::uint64_t scheduled_to_response_ns{};
    std::uint64_t ingress_to_admission_ns{};
    std::uint64_t local_wal_append_ns{};
    std::uint64_t quorum_append_ns{};
    std::uint64_t admission_to_commit_ns{};
    std::uint64_t commit_to_apply_ns{};
    std::uint32_t retries{};
    bool internal_timing_valid{};
};

struct DistributionSummary {
    std::uint64_t count{};
    std::uint64_t minimum_ns{};
    std::uint64_t p50_ns{};
    std::uint64_t p90_ns{};
    std::uint64_t p99_ns{};
    std::uint64_t p999_ns{};
    std::uint64_t p9999_ns{};
    std::uint64_t maximum_ns{};
    long double mean_ns{};
};

class ExactLatencyRecorder {
public:
    explicit ExactLatencyRecorder(std::size_t capacity);

    [[nodiscard]] bool record(const LatencySample& sample) noexcept;
    [[nodiscard]] bool merge(const ExactLatencyRecorder& other) noexcept;
    [[nodiscard]] std::span<const LatencySample> samples() const noexcept { return samples_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return samples_.capacity(); }
    [[nodiscard]] std::uint64_t dropped_samples() const noexcept { return dropped_samples_; }
    [[nodiscard]] DistributionSummary summarize_total_latency() const;

private:
    std::vector<LatencySample> samples_;
    std::uint64_t dropped_samples_{};
};

[[nodiscard]] DistributionSummary summarize_values(std::span<const std::uint64_t> values);
[[nodiscard]] std::vector<std::uint64_t> default_latency_bucket_bounds_ns();
[[nodiscard]] std::vector<std::uint64_t> cumulative_bucket_counts(
    std::span<const std::uint64_t> values, std::span<const std::uint64_t> bounds);

}  // namespace velocita
