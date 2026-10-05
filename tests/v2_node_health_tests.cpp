#include "velocita/v2/node_health.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace velocita::v2;
namespace fs = std::filesystem;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct TempDir {
    fs::path path;
    TempDir() {
        auto pattern = (fs::temp_directory_path() / "v2-node-health-XXXXXX").string();
        std::vector<char> bytes(pattern.begin(), pattern.end());
        bytes.push_back('\0');
        require(::mkdtemp(bytes.data()) != nullptr, "mkdtemp failed");
        path = fs::canonical(bytes.data());
    }
    ~TempDir() { std::error_code ignored; fs::remove_all(path, ignored); }
};

NodeHealth uniform(std::uint64_t k) {
    NodeHealth h;
    h.node_id = static_cast<std::int32_t>(k % 1000 + 1);
    h.pid = k; h.session_epoch = k; h.role = static_cast<std::uint8_t>(k % 4);
    h.ready = k % 2 == 0; h.failed = k % 3 == 0;
    h.term = static_cast<std::int64_t>(k); h.leader_id = static_cast<std::int32_t>(k % 1000);
    h.current_index = h.commit_index = h.applied_index = static_cast<std::int64_t>(k);
    h.durable_index = h.turns = h.last_turn_monotonic_ns = k;
    h.auth_refusals = h.fence_refusals = h.peer_busy_holds = h.peer_backpressure = k;
    h.ingress_refusals = h.results_sent = h.results_dropped = k;
    h.secret_reloads = h.secret_reload_failures = k;
    h.retained_ordinary = h.retained_protected = k;
    h.compaction_floor = h.snapshot_index = static_cast<std::int64_t>(k);
    h.stranded_voters = h.compactions = h.last_compaction_ms = k;
    h.receipts = h.pinned_receipts = h.pinned_receipts_peak = k;
    return h;
}

void round_trip_and_refusals() {
    TempDir dir;
    const auto file = dir.path / "health.page";
    require(!read_node_health(file).has_value(), "missing page read as healthy");
    auto page = NodeHealthPage::create(file);
    require(!read_node_health(file).has_value(), "never-published page read as an all-zero status");
    page->publish(uniform(42));
    const auto read = read_node_health(file);
    require(read && *read == uniform(42), "published health did not read back exactly");
    auto distinct = uniform(42);
    distinct.retained_ordinary = 7001;
    distinct.retained_protected = 13;
    page->publish(distinct);
    const auto pools = read_node_health(file);
    require(pools && pools->retained_ordinary == 7001 && pools->retained_protected == 13,
            "retained-log pools were swapped or lost on the page");
    distinct.compaction_floor = 900;
    distinct.snapshot_index = 850;
    distinct.stranded_voters = 1;
    distinct.compactions = 7;
    distinct.last_compaction_ms = 42;
    page->publish(distinct);
    const auto compaction = read_node_health(file);
    require(compaction && compaction->compaction_floor == 900 && compaction->snapshot_index == 850 &&
                compaction->stranded_voters == 1 && compaction->compactions == 7 &&
                compaction->last_compaction_ms == 42,
            "compaction fields were swapped or lost on the page");
    distinct.receipts = 201;
    distinct.pinned_receipts = 18;
    distinct.pinned_receipts_peak = 33;
    page->publish(distinct);
    const auto receipts = read_node_health(file);
    require(receipts && receipts->receipts == 201 && receipts->pinned_receipts == 18 &&
                receipts->pinned_receipts_peak == 33,
            "receipt fields were swapped or lost on the page");
    { std::ofstream(dir.path / "foreign.page", std::ios::binary) << std::string(4096, 'x'); }
    require(!read_node_health(dir.path / "foreign.page").has_value(), "foreign page accepted");
    { std::ofstream(dir.path / "short.page", std::ios::binary) << "VLH"; }
    require(!read_node_health(dir.path / "short.page").has_value(), "short page accepted");
}

void concurrent_reads_are_never_torn() {
    TempDir dir;
    const auto file = dir.path / "health.page";
    auto page = NodeHealthPage::create(file);
    page->publish(uniform(1));
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (std::uint64_t k = 2; !stop.load(std::memory_order_relaxed); ++k) {
            page->publish(uniform(k));
            volatile int sink = 0;
            for (int spin = 0; spin < 256; ++spin) sink = spin;
            (void)sink;
        }
    });
    std::uint64_t consistent = 0;
    try {
        for (int i = 0; i < 200000; ++i) {
            if (const auto read = read_node_health(file)) {
                require(*read == uniform(read->pid), "torn health snapshot observed");
                ++consistent;
            }
        }
    } catch (...) {
        stop = true;
        writer.join();
        throw;
    }
    stop = true;
    writer.join();
    require(consistent > 1000, "reader almost never obtained a consistent snapshot");
}

}

int main() {
    try {
        round_trip_and_refusals();
        concurrent_reads_are_never_torn();
        std::cout << "node health tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "node health tests failed: " << error.what() << '\n';
        return 1;
    }
}
