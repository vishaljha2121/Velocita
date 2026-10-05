#include "velocita/v2/replication.hpp"
#include "v2_identity_test_support.hpp"

#include <array>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {
using namespace velocita;
using namespace velocita::v2;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct SyncTrace {
    const std::thread::id owner{std::this_thread::get_id()};
    std::atomic<std::size_t> file_syncs{};
    std::atomic<std::size_t> owner_file_syncs{};
    void reset() noexcept {
        file_syncs.store(0);
        owner_file_syncs.store(0);
    }
};
std::atomic<SyncTrace*> active_trace{};
struct TraceScope {
    explicit TraceScope(SyncTrace& trace) { active_trace.store(&trace); }
    ~TraceScope() { active_trace.store(nullptr); }
};
}

int observed_metadata_fsync(int fd) {
    if (auto* trace = active_trace.load()) {
        ++trace->file_syncs;
        if (std::this_thread::get_id() == trace->owner) ++trace->owner_file_syncs;
    }
    return ::fsync(fd);
}

#define fsync observed_metadata_fsync
#include "../src/v2_bound_metadata.cpp"
#undef fsync

namespace {
void verify_owner_free(const char* scenario, const SyncTrace& trace,
                       const PersistentMetadata& persisted) {
    require(trace.file_syncs.load() != 0, "non-vacuity: no real metadata fsync observed");
    std::cout << "async-hard-state scenario=" << scenario
              << " metadata_sync_calls=" << trace.file_syncs.load()
              << " owner_metadata_sync_calls=" << trace.owner_file_syncs.load()
              << " persisted_term=" << persisted.term
              << " persisted_vote=" << persisted.voted_for
              << " persisted_commit=" << persisted.commit_index << '\n';
    require(trace.owner_file_syncs.load() == 0,
            "RED F6/A09: running WAL writer did not remove hard-state fsync from the Raft owner");
}
void election(const std::filesystem::path& root) {
    SyncTrace trace;
    ReplicatedKernel node(identity_test::bootstrap(root, 1, {1, 2}));
    node.start_writer();
    TraceScope observing(trace);
    trace.reset();
    node.tick(10'000);
    const auto metadata = identity_test::metadata_store(root).load();
    require(metadata.term > 0 && metadata.voted_for == 1,
            "non-vacuity: campaign did not persist a self vote");
    const auto message = identity_test::take_outbound_message(node);
    require(message && std::holds_alternative<RequestVote>(message->payload),
            "non-vacuity: candidate emitted no RequestVote");
    require(std::get<RequestVote>(message->payload).term == metadata.term,
            "campaign message is not from the persisted term");
    verify_owner_free("election", trace, metadata);
}
void vote(const std::filesystem::path& root) {
    SyncTrace trace;
    ReplicatedKernel node(identity_test::bootstrap(root, 1, {1, 2}));
    node.start_writer();
    TraceScope observing(trace);
    trace.reset();
    identity_test::deliver_declared_peer(node, {2, 1, RequestVote{7, 2, 0, 0}});
    const auto metadata = identity_test::metadata_store(root).load();
    require(metadata.term == 7 && metadata.voted_for == 2,
            "non-vacuity: higher-term vote did not reach disk");
    const auto message = identity_test::take_outbound_message(node);
    require(message && std::holds_alternative<RequestVoteResponse>(message->payload) &&
                std::get<RequestVoteResponse>(message->payload).vote_granted,
            "non-vacuity: valid candidate was not granted a vote");
    verify_owner_free("vote", trace, metadata);
}
void commit(const std::filesystem::path& root) {
    SyncTrace trace;
    ReplicatedKernel node(identity_test::bootstrap(root, 1, {1}));
    node.start_writer();
    node.tick(1);
    velocita::v2::Command command;
    command.identity.caller_id = CallerId{1};
    command.identity.request_sequence = RequestSequence{1};
    command.identity.payload_digest.bytes[0] = std::byte{1};
    command.body = AdvanceKillEpoch{KillEpoch{1}};
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(command, bytes);
    require(encoded.error == KernelCodecError::none, "fixture command did not encode");
    require(node.propose(std::span{bytes}.first(encoded.bytes_written)).status ==
                ProposeStatus::admitted, "fixture command was not admitted");
    node.flush_durability();
    node.stop_writer();
    node.start_writer();
    require(node.durable_index() == 1, "non-vacuity: WAL did not become durable");
    TraceScope observing(trace);
    trace.reset();
    node.tick(0);
    const auto metadata = identity_test::metadata_store(root).load();
    require(metadata.commit_index == 1 && node.state().kill_epoch == KillEpoch{1},
            "non-vacuity: durable committed command was not checkpointed/applied");
    verify_owner_free("commit", trace, metadata);
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "usage: async-hard-state-red election|vote|commit NEW_ROOT");
        const std::filesystem::path root(argv[2]);
        require(!std::filesystem::exists(root), "refusing to overwrite an existing fixture root");
        const std::string scenario(argv[1]);
        if (scenario == "election") election(root);
        else if (scenario == "vote") vote(root);
        else if (scenario == "commit") commit(root);
        else throw std::runtime_error("unknown scenario");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "async hard-state diagnostic: " << error.what() << '\n';
        return 1;
    }
}
