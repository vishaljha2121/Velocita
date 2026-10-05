#include "velocita/benchmark.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace velocita {
namespace {

std::size_t next_power_of_two(std::size_t value) {
    if (value == 0) throw std::invalid_argument("pending request capacity must be positive");
    std::size_t result = 1;
    while (result < value) {
        if (result > std::numeric_limits<std::size_t>::max() / 2) {
            throw std::overflow_error("pending request capacity is too large");
        }
        result *= 2;
    }
    return result;
}

std::uint64_t percentile(const std::vector<std::uint64_t>& sorted, const long double quantile) {
    if (sorted.empty()) return 0;
    const auto rank = static_cast<std::size_t>(
        std::ceil(quantile * static_cast<long double>(sorted.size())));
    return sorted[std::min(sorted.size() - 1, rank == 0 ? std::size_t{0} : rank - 1)];
}

}

PendingRequestTable::PendingRequestTable(const std::size_t capacity)
    : slots_(next_power_of_two(capacity)), occupied_(slots_.size()) {}

std::size_t PendingRequestTable::slot_for(const std::uint64_t request_id) const noexcept {
    return static_cast<std::size_t>(request_id) & (slots_.size() - 1);
}

bool PendingRequestTable::insert(const PendingRequest& request) noexcept {
    if (request.request_id == 0 || size_ == slots_.size()) return false;
    auto slot = slot_for(request.request_id);
    for (std::size_t probe = 0; probe < slots_.size(); ++probe) {
        if (occupied_[slot] == 0) {
            slots_[slot] = request;
            occupied_[slot] = 1;
            ++size_;
            return true;
        }
        if (slots_[slot].request_id == request.request_id) return false;
        slot = (slot + 1) & (slots_.size() - 1);
    }
    return false;
}

PendingRequest* PendingRequestTable::find(const std::uint64_t request_id) noexcept {
    if (request_id == 0) return nullptr;
    auto slot = slot_for(request_id);
    for (std::size_t probe = 0; probe < slots_.size(); ++probe) {
        if (occupied_[slot] != 0 && slots_[slot].request_id == request_id) return &slots_[slot];
        slot = (slot + 1) & (slots_.size() - 1);
    }
    return nullptr;
}

const PendingRequest* PendingRequestTable::find(const std::uint64_t request_id) const noexcept {
    return const_cast<PendingRequestTable*>(this)->find(request_id);
}

bool PendingRequestTable::erase(const std::uint64_t request_id) noexcept {
    if (request_id == 0) return false;
    auto slot = slot_for(request_id);
    for (std::size_t probe = 0; probe < slots_.size(); ++probe) {
        if (occupied_[slot] != 0 && slots_[slot].request_id == request_id) {
            occupied_[slot] = 0;
            slots_[slot] = {};
            --size_;
            return true;
        }
        slot = (slot + 1) & (slots_.size() - 1);
    }
    return false;
}

ExactLatencyRecorder::ExactLatencyRecorder(const std::size_t capacity) {
    samples_.reserve(capacity);
}

bool ExactLatencyRecorder::record(const LatencySample& sample) noexcept {
    if (samples_.size() == samples_.capacity()) {
        ++dropped_samples_;
        return false;
    }
    samples_.push_back(sample);
    return true;
}

bool ExactLatencyRecorder::merge(const ExactLatencyRecorder& other) noexcept {
    const auto available = samples_.capacity() - samples_.size();
    const auto accepted = std::min(available, other.samples_.size());
    samples_.insert(samples_.end(), other.samples_.begin(), other.samples_.begin() +
                    static_cast<std::ptrdiff_t>(accepted));
    dropped_samples_ += other.dropped_samples_ + (other.samples_.size() - accepted);
    return accepted == other.samples_.size();
}

DistributionSummary ExactLatencyRecorder::summarize_total_latency() const {
    std::vector<std::uint64_t> values;
    values.reserve(samples_.size());
    for (const auto& sample : samples_) values.push_back(sample.scheduled_to_response_ns);
    return summarize_values(values);
}

DistributionSummary summarize_values(const std::span<const std::uint64_t> values) {
    DistributionSummary result;
    result.count = values.size();
    if (values.empty()) return result;
    std::vector<std::uint64_t> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    result.minimum_ns = sorted.front();
    result.p50_ns = percentile(sorted, 0.50L);
    result.p90_ns = percentile(sorted, 0.90L);
    result.p99_ns = percentile(sorted, 0.99L);
    result.p999_ns = percentile(sorted, 0.999L);
    result.p9999_ns = percentile(sorted, 0.9999L);
    result.maximum_ns = sorted.back();
    long double sum = 0;
    for (const auto value : sorted) sum += static_cast<long double>(value);
    result.mean_ns = sum / static_cast<long double>(sorted.size());
    return result;
}

std::vector<std::uint64_t> default_latency_bucket_bounds_ns() {
    return {
        1'000, 2'000, 5'000, 10'000, 20'000, 50'000, 100'000, 200'000, 500'000,
        1'000'000, 2'000'000, 5'000'000, 10'000'000, 20'000'000, 50'000'000,
        100'000'000, 200'000'000, 500'000'000, 1'000'000'000, 2'000'000'000,
        5'000'000'000, 10'000'000'000,
    };
}

std::vector<std::uint64_t> cumulative_bucket_counts(
    const std::span<const std::uint64_t> values, const std::span<const std::uint64_t> bounds) {
    if (!std::is_sorted(bounds.begin(), bounds.end())) {
        throw std::invalid_argument("latency bucket bounds must be sorted");
    }
    std::vector<std::uint64_t> counts(bounds.size());
    for (const auto value : values) {
        const auto first = std::lower_bound(bounds.begin(), bounds.end(), value);
        for (auto bucket = first; bucket != bounds.end(); ++bucket) {
            ++counts[static_cast<std::size_t>(bucket - bounds.begin())];
        }
    }
    return counts;
}

}
