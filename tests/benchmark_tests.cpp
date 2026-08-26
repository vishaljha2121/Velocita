#include "velocita/benchmark.hpp"
#include "velocita/benchmark_timing.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_pending_table() {
    velocita::PendingRequestTable table(3);
    require(table.capacity() == 4, "pending capacity should round to a power of two");
    require(table.insert({1, 10, 20, 30, 0, 0, velocita::BenchmarkPhase::steady}),
            "first pending insert failed");
    require(table.insert({5, 11, 21, 31, 1, 0, velocita::BenchmarkPhase::steady}),
            "colliding pending insert failed");
    require(table.find(1) != nullptr && table.find(5) != nullptr,
            "pending lookup failed");
    require(!table.insert({1}), "duplicate pending request was accepted");
    require(table.erase(1) && table.find(1) == nullptr && table.find(5) != nullptr,
            "pending erase broke a colliding lookup");
}

void test_exact_percentiles_and_merge() {
    velocita::ExactLatencyRecorder first(5);
    for (std::uint64_t value : {10, 20, 30}) {
        require(first.record({value, 0, 0, value}), "sample record failed");
    }
    velocita::ExactLatencyRecorder second(2);
    require(second.record({4, 0, 0, 40}), "second sample record failed");
    require(second.record({5, 0, 0, 50}), "second sample record failed");
    require(first.merge(second), "exact recorder merge failed");
    const auto summary = first.summarize_total_latency();
    require(summary.count == 5 && summary.minimum_ns == 10 && summary.p50_ns == 30 &&
            summary.p90_ns == 50 && summary.maximum_ns == 50,
            "exact percentile calculation is wrong");
}

void test_buckets() {
    const std::vector<std::uint64_t> values{1, 5, 10, 11};
    const std::vector<std::uint64_t> bounds{1, 10};
    const auto counts = velocita::cumulative_bucket_counts(values, bounds);
    require(counts.size() == 2 && counts[0] == 1 && counts[1] == 3,
            "cumulative buckets are wrong");
}

void test_internal_trace_table() {
    velocita::BenchmarkTraceTable traces(8);
    require(traces.begin(42, 100, 110), "trace begin failed");
    traces.record_wal(42, 7);
    traces.record_admitted(42, 3, 120);
    traces.record_committed(3, 3, 170, 35);
    const auto result = traces.complete(42, 180);
    require(result.has_value(), "trace did not complete");
    require(result->flags == velocita::kBenchmarkTimingValid &&
            result->ingress_to_admission_ns == 20 && result->local_wal_append_ns == 7 &&
            result->quorum_append_ns == 35 && result->admission_to_commit_ns == 50 &&
            result->commit_to_apply_ns == 10,
            "internal trace durations are wrong");
}

}  // namespace

int main() {
    try {
        test_pending_table();
        test_exact_percentiles_and_merge();
        test_buckets();
        test_internal_trace_table();
        std::cout << "benchmark tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "benchmark tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
