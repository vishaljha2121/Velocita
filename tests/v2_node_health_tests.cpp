#include "velocita/v2/node_health.hpp"
#include "velocita/v2/node.hpp"

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
    h.archive_bytes = h.archive_byte_limit = h.archive_objects = h.archive_object_limit = h.policies = k;
    h.ledger_count = k;
    h.quorum_step_downs = k;
    h.unauthorized_commands = k;
    for (auto& ledger : h.ledgers) {
        const auto signed_k = static_cast<std::int64_t>(k);
        ledger = {k, k, signed_k, signed_k, signed_k, signed_k, signed_k, signed_k, signed_k};
    }
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
    distinct.archive_bytes = 700'000'000;
    distinct.archive_byte_limit = 1'073'741'824;
    distinct.archive_objects = 1'209;
    distinct.archive_object_limit = 4'096;
    page->publish(distinct);
    const auto archive = read_node_health(file);
    require(archive && archive->archive_bytes == 700'000'000 && archive->archive_byte_limit == 1'073'741'824 &&
                archive->archive_objects == 1'209 && archive->archive_object_limit == 4'096,
            "archive fields were swapped or lost on the page");
    distinct.policies = 5;
    page->publish(distinct);
    const auto policies = read_node_health(file);
    require(policies && policies->policies == 5 && policies->archive_object_limit == 4'096,
            "policy count was lost on the page");
    distinct.ledger_count = 2;
    distinct.quorum_step_downs = 77;
    distinct.unauthorized_commands = 78;
    distinct.ledgers[0] = {2, 3, -4, -5, 6, -7, 8, -9, 10};
    distinct.ledgers[3] = {11, 12, 13, 14, 15, 16, 17, 18, 19};
    page->publish(distinct);
    const auto ledgers = read_node_health(file);
    require(ledgers && ledgers->ledger_count == 2 && ledgers->quorum_step_downs == 77 && ledgers->unauthorized_commands == 78 &&
                ledgers->ledgers[0] == NodeHealthLedger{2, 3, -4, -5, 6, -7, 8, -9, 10} &&
                ledgers->ledgers[1] == distinct.ledgers[1] && ledgers->ledgers[2] == distinct.ledgers[2] &&
                ledgers->ledgers[3] == NodeHealthLedger{11, 12, 13, 14, 15, 16, 17, 18, 19},
            "account ledgers were swapped, sign-lost or dropped on the page");
    { std::ofstream(dir.path / "foreign.page", std::ios::binary) << std::string(4096, 'x'); }
    require(!read_node_health(dir.path / "foreign.page").has_value(), "foreign page accepted");
    { std::ofstream(dir.path / "short.page", std::ios::binary) << "VLH"; }
    require(!read_node_health(dir.path / "short.page").has_value(), "short page accepted");
}

void status_maps_to_the_page_field_by_field() {
    NodeStatus s;
    s.replica.node_id = 2; s.replica.role = "leader"; s.replica.term = 11; s.replica.leader_id = 3;
    s.replica.current_index = 104; s.replica.commit_index = 103; s.replica.applied_index = 102;
    s.replica.retained_ordinary = 21; s.replica.retained_protected = 22;
    s.replica.compaction_floor = 23; s.replica.snapshot_index = 24; s.replica.stranded_voters = 25;
    s.replica.receipts = 26; s.replica.pinned_receipts = 27; s.replica.pinned_receipts_peak = 28;
    s.replica.archive_bytes = 29; s.replica.archive_byte_limit = 30;
    s.replica.archive_objects = 31; s.replica.archive_object_limit = 32; s.replica.policies = 33;
    s.replica.quorum_step_downs = 34;
    s.replica.unauthorized_commands = 35;
    s.replica.ledger_count = 2;
    s.replica.ledgers[0] = {61, 62, -63, -64, 65, -66, 67, -68, 69};
    s.replica.ledgers[1] = {71, 72, 73, 74, 75, 76, 77, 78, 79};
    s.replica.ledgers[2] = {81, 82, 83, 84, 85, 86, 87, 88, 89};
    s.ready = true; s.durable_index = 101; s.session_epoch = 12;
    s.counters.auth_refusals = 41; s.counters.fence_refusals = 42; s.counters.peer_busy_holds = 43;
    s.counters.peer_backpressure = 44; s.counters.ingress_refusals = 45; s.counters.results_sent = 46;
    s.counters.results_dropped = 47; s.counters.compactions = 48; s.counters.last_compaction_ms = 49;
    NodeHealth want;
    want.node_id = 2; want.pid = 51; want.session_epoch = 12; want.role = 3; want.ready = true; want.failed = true;
    want.term = 11; want.leader_id = 3; want.current_index = 104; want.commit_index = 103; want.applied_index = 102;
    want.durable_index = 101; want.turns = 52; want.last_turn_monotonic_ns = 53;
    want.auth_refusals = 41; want.fence_refusals = 42; want.peer_busy_holds = 43; want.peer_backpressure = 44;
    want.ingress_refusals = 45; want.results_sent = 46; want.results_dropped = 47;
    want.secret_reloads = 54; want.secret_reload_failures = 55;
    want.retained_ordinary = 21; want.retained_protected = 22; want.compaction_floor = 23; want.snapshot_index = 24;
    want.stranded_voters = 25; want.compactions = 48; want.last_compaction_ms = 49;
    want.receipts = 26; want.pinned_receipts = 27; want.pinned_receipts_peak = 28;
    want.archive_bytes = 29; want.archive_byte_limit = 30; want.archive_objects = 31; want.archive_object_limit = 32;
    want.policies = 33;
    want.quorum_step_downs = 34;
    want.unauthorized_commands = 35;
    want.ledger_count = 2;
    want.ledgers[0] = {61, 62, -63, -64, 65, -66, 67, -68, 69};
    want.ledgers[1] = {71, 72, 73, 74, 75, 76, 77, 78, 79};
    require(node_health_of(s, NodeHealthContext{51, 52, 53, true, 54, 55}) == want,
            "a status field reached the wrong health field, or none");
    for (const auto& [name, code] : {std::pair<const char*, int>{"follower", 1}, {"candidate", 2}, {"none", 0}}) {
        s.replica.role = name;
        require(node_health_of(s, NodeHealthContext{}).role == code, std::string("role code for ") + name);
    }
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
        status_maps_to_the_page_field_by_field();
        concurrent_reads_are_never_torn();
        std::cout << "node health tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "node health tests failed: " << error.what() << '\n';
        return 1;
    }
}
