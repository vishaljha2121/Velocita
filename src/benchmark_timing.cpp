#include "velocita/benchmark_timing.hpp"

#include <chrono>
#include <limits>
#include <stdexcept>

namespace velocita {
namespace {

std::size_t power_of_two(std::size_t value) {
    if (value == 0) throw std::invalid_argument("benchmark trace capacity must be positive");
    std::size_t result = 1;
    while (result < value) {
        if (result > std::numeric_limits<std::size_t>::max() / 2) {
            throw std::overflow_error("benchmark trace capacity is too large");
        }
        result *= 2;
    }
    return result;
}

std::uint64_t duration(const std::uint64_t end, const std::uint64_t begin) noexcept {
    return end >= begin ? end - begin : 0;
}

}

std::uint64_t benchmark_monotonic_nanoseconds() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

BenchmarkTraceTable::BenchmarkTraceTable(const std::size_t capacity)
    : slots_(power_of_two(capacity)), indexes_(slots_.size()), mask_(slots_.size() - 1) {}

BenchmarkTraceTable::Slot* BenchmarkTraceTable::find(const std::uint64_t request_id) noexcept {
    auto slot = static_cast<std::size_t>(request_id) & mask_;
    for (std::size_t probe = 0; probe < slots_.size(); ++probe) {
        if (slots_[slot].active && slots_[slot].request_id == request_id) return &slots_[slot];
        slot = (slot + 1) & mask_;
    }
    return nullptr;
}

bool BenchmarkTraceTable::begin(const std::uint64_t request_id, const std::uint64_t ingress_ns,
                                const std::uint64_t admission_start_ns) noexcept {
    auto slot = static_cast<std::size_t>(request_id) & mask_;
    for (std::size_t probe = 0; probe < slots_.size(); ++probe) {
        if (!slots_[slot].active) {
            slots_[slot] = {request_id, 0, ingress_ns, admission_start_ns, 0, 0, 0, 0, true};
            return true;
        }
        if (slots_[slot].request_id == request_id) return true;
        slot = (slot + 1) & mask_;
    }
    ++collision_count_;
    return false;
}

void BenchmarkTraceTable::record_wal(const std::uint64_t request_id,
                                     const std::uint64_t duration_ns) noexcept {
    if (auto* slot = find(request_id)) slot->wal_append_ns = duration_ns;
}

void BenchmarkTraceTable::record_admitted(const std::uint64_t request_id, const std::int64_t index,
                                          const std::uint64_t admitted_ns) noexcept {
    auto* slot = find(request_id);
    if (slot == nullptr || index <= 0) return;
    slot->index = index;
    slot->admitted_ns = admitted_ns;
    indexes_[static_cast<std::size_t>(index) & mask_] = {index, request_id};
}

void BenchmarkTraceTable::record_committed(const std::int64_t first_index,
                                           const std::int64_t last_index,
                                           const std::uint64_t committed_ns,
                                           const std::uint64_t quorum_append_ns) noexcept {
    if (first_index <= 0 || last_index < first_index) return;
    for (auto index = first_index; index <= last_index; ++index) {
        const auto& mapped = indexes_[static_cast<std::size_t>(index) & mask_];
        if (mapped.index != index) continue;
        if (auto* slot = find(mapped.request_id)) {
            slot->committed_ns = committed_ns;
            slot->quorum_append_ns = quorum_append_ns;
        }
    }
}

std::optional<BenchmarkTiming> BenchmarkTraceTable::complete(
    const std::uint64_t request_id, const std::uint64_t applied_ns) noexcept {
    auto* slot = find(request_id);
    if (slot == nullptr) return std::nullopt;
    if (slot->admitted_ns == 0 || slot->committed_ns == 0) {
        if (slot->index > 0) indexes_[static_cast<std::size_t>(slot->index) & mask_] = {};
        *slot = {};
        return std::nullopt;
    }
    BenchmarkTiming result;
    result.flags = kBenchmarkTimingValid;
    result.ingress_to_admission_ns = duration(slot->admitted_ns, slot->ingress_ns);
    result.local_wal_append_ns = slot->wal_append_ns;
    result.quorum_append_ns = slot->quorum_append_ns;
    result.admission_to_commit_ns = duration(slot->committed_ns, slot->admitted_ns);
    result.commit_to_apply_ns = duration(applied_ns, slot->committed_ns);
    if (slot->index > 0) indexes_[static_cast<std::size_t>(slot->index) & mask_] = {};
    *slot = {};
    return result;
}

}
