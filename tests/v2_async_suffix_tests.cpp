#include "v2_identity_test_support.hpp"

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <unistd.h>

namespace {
using namespace velocita;
using namespace velocita::v2;
using namespace std::chrono_literals;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Root {
    std::filesystem::path path;
    Root() {
        auto value = (std::filesystem::temp_directory_path() /
                      "v2-async-suffix-XXXXXX").string();
        std::vector<char> pattern(value.begin(), value.end());
        pattern.push_back('\0');
        const auto* created = ::mkdtemp(pattern.data());
        require(created != nullptr, "mkdtemp failed");
        path = std::filesystem::canonical(created);
    }
    ~Root() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
struct Fence {
    StorageCheckpoint pause_at{StorageCheckpoint::rewrite_block};
    const std::thread::id owner{std::this_thread::get_id()};
    std::mutex mutex;
    std::condition_variable changed;
    bool armed{}, parked{}, released{};
    bool fail{}, fired{};
    unsigned owner_rewrites{}, worker_rewrites{};
    static void observe(StorageCheckpoint checkpoint, void* raw) {
        auto& self = *static_cast<Fence*>(raw);
        std::unique_lock lock(self.mutex);
        if (!self.armed) return;
        if (std::this_thread::get_id() == self.owner) {
            if (checkpoint == StorageCheckpoint::rewrite_block) ++self.owner_rewrites;
            return;
        }
        if (checkpoint == StorageCheckpoint::rewrite_block) ++self.worker_rewrites;
        if (checkpoint != self.pause_at) return;
        if (self.fail) {
            self.fired = true;
            throw std::runtime_error("injected suffix rewrite worker failure");
        }
        self.parked = true;
        self.changed.notify_all();
        self.changed.wait(lock, [&] { return self.released; });
    }
    void release() { std::lock_guard lock(mutex); released = true; changed.notify_all(); }
};
struct Release { Fence& fence; ~Release() { fence.release(); } };
std::vector<std::byte> command(unsigned sequence) {
    velocita::v2::Command value;
    value.identity.caller_id = CallerId{1};
    value.identity.request_sequence = RequestSequence{sequence};
    value.identity.payload_digest.bytes[0] = std::byte(sequence);
    value.body = AdvanceKillEpoch{KillEpoch{sequence}};
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(value, bytes);
    require(encoded.error == KernelCodecError::none, "command fixture encode failed");
    return {bytes.begin(), bytes.begin() + encoded.bytes_written};
}
void run(bool previous_term_conflict, bool higher_term, bool staged_tail,
         StorageCheckpoint pause_at) {
    Root root;
    Fence fence;
    fence.pause_at = pause_at;
    ReplicatedKernel node(identity_test::bootstrap(root.path / "node", 1, {1, 2}),
                          {&Fence::observe, &fence});
    AppendEntries initial{2, 0, 0, 0, {}};
    for (int i = 1; i <= 3; ++i) initial.entries.push_back({1, i, 0, command(i)});
    identity_test::deliver_declared_peer(node, {2, 1, initial});
    node.flush_durability();
    require(node.durable_index() == 3 && node.reserved_result_count() == 3,
            "fixture needs three durable uncommitted result obligations");
    while (identity_test::take_outbound_message(node)) {}
    const auto original = identity_test::read_wal(root.path / "node");
    require(original.size() == 3 && original[0].payload == command(1) &&
            original[1].payload == command(2) && original[2].payload == command(3),
            "fixture WAL does not match independent commands");
    if (staged_tail) {
        identity_test::deliver_declared_peer(node,
            {2, 1, AppendEntries{2, 3, 1, 0, {{1, 5, 0, command(5)}}}});
        require(node.durable_index() == 3 && node.retained_log_count() == 4,
                "fixture tail was not staged independently of durability");
        while (identity_test::take_outbound_message(node)) {}
    }
    const auto old_count = staged_tail ? 4U : 3U;
    node.start_writer();
    Release release{fence};
    { std::lock_guard lock(fence.mutex); fence.armed = true; }
    AppendEntries conflict{higher_term ? 3 : 2, previous_term_conflict ? 2 : 1,
                           previous_term_conflict ? 2 : 1, 0, {}};
    if (!previous_term_conflict) conflict.entries.push_back({2, 4, 0, command(4)});
    const PeerMessage message{2, 1, conflict};
    auto bytes = identity_test::frame_peer_body(message,
        identity_test::declared_sender_identity(message, node.static_identity()));
    (void)node.receive_peer(bytes);
    {
        std::lock_guard lock(fence.mutex);
        require(fence.owner_rewrites == 0,
                "RED F6: conflicting suffix rewrite executed on protocol owner");
    }
    // Caller storage becomes garbage immediately; a deferred receive must own it.
    require(node.retained_log_count() == old_count && node.reserved_result_count() == old_count,
            "suffix cells or reservations released before worker preparation");
    std::fill(bytes.begin(), bytes.end(), std::byte{0xCC});
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    bool parked = false;
    while (std::chrono::steady_clock::now() < deadline) {
        node.flush_durability();
        { std::lock_guard lock(fence.mutex); parked = fence.parked; }
        if (parked) break;
        std::this_thread::yield();
    }
    require(parked, "suffix worker did not reach actual replacement-block write");
    require(node.retained_log_count() == old_count && node.reserved_result_count() == old_count &&
                node.durable_index() == old_count,
            "suffix ownership or durable index released before replacement sync");
    require(!identity_test::take_outbound_message(node), "append response escaped pending suffix");
    require(node.status().applied_index == 0, "uncommitted suffix applied");
    const auto during = identity_test::read_wal(root.path / "node");
    if (pause_at == StorageCheckpoint::rewrite_block) {
        require(during.size() == old_count && during[2].payload == command(3),
                "live WAL replaced before temporary file was durable");
        if (staged_tail) require(during[3].payload == command(5), "staged tail lost before deletion");
    } else {
        require(during.size() == 1 && during[0].payload == command(1),
                "renamed suffix image must preserve exactly the surviving prefix");
    }
    fence.release();
    const auto expected = previous_term_conflict ? 1U : 2U;
    bool completed = false;
    const auto finish = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < finish) {
        node.flush_durability();
        node.tick(0);
        if (!node.persistence_pending() && node.retained_log_count() == expected &&
            node.durable_index() == expected) { completed = true; break; }
        std::this_thread::yield();
    }
    require(completed, "suffix continuation did not finish exact replacement");
    require(node.reserved_result_count() == expected, "suffix result reservations not reconciled");
    const auto after = identity_test::read_wal(root.path / "node");
    require(after.size() == expected && after[0].index == 1 && after[0].payload == command(1),
            "suffix rewrite lost unchanged prefix");
    if (!previous_term_conflict)
        require(after[1].index == 2 && after[1].term == 2 && after[1].payload == command(4),
                "suffix retry lost exact new command bytes");
    const auto response = identity_test::take_outbound_message(node);
    require(response && std::holds_alternative<AppendEntriesResponse>(response->payload),
            "no append response after suffix durability");
    require(std::get<AppendEntriesResponse>(response->payload).success == !previous_term_conflict,
            "retry changed Raft conflict response semantics");
    { std::lock_guard lock(fence.mutex);
      require(fence.owner_rewrites == 0 && fence.worker_rewrites == 1,
              "whole-suffix deletion must use one worker rewrite, not per-entry rewrites"); }
}

void run_worker_failure(StorageCheckpoint fail_at) {
    Root root;
    Fence fence;
    fence.pause_at = fail_at;
    fence.fail = true;
    {
        ReplicatedKernel node(identity_test::bootstrap(root.path / "node", 1, {1, 2}),
                              {&Fence::observe, &fence});
        AppendEntries initial{2, 0, 0, 0, {}};
        for (int i = 1; i <= 3; ++i) initial.entries.push_back({1, i, 0, command(i)});
        identity_test::deliver_declared_peer(node, {2, 1, initial});
        node.flush_durability();
        require(node.durable_index() == 3 && node.retained_log_count() == 3 &&
                    node.reserved_result_count() == 3,
                "failure fixture needs three durable owned suffix cells");
        while (identity_test::take_outbound_message(node)) {}
        node.start_writer();
        { std::lock_guard lock(fence.mutex); fence.armed = true; }
        const AppendEntries replacement{2, 1, 1, 0,
            {{2, 4, 0, command(4)}}};
        identity_test::deliver_declared_peer(node, {2, 1, replacement});

        bool latched{};
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!latched && std::chrono::steady_clock::now() < deadline) {
            try { node.flush_durability(); }
            catch (const std::runtime_error&) { latched = true; }
            std::this_thread::yield();
        }
        { std::lock_guard lock(fence.mutex);
          require(fence.fired, "selected suffix rewrite failure cut did not fire"); }
        require(latched, "suffix rewrite worker exception did not latch fail closed");
        require(node.retained_log_count() == 3 && node.reserved_result_count() == 3 &&
                    node.durable_index() == 3,
                "failed suffix rewrite released owner cells, reservations, or durability");
        require(node.status().applied_index == 0 && node.state().kill_epoch == KillEpoch{0} &&
                    !node.is_leader(),
                "failed suffix rewrite retained authoritative or applied state");
        bool output_refused{};
        try { (void)identity_test::take_outbound_message(node); }
        catch (const std::runtime_error&) { output_refused = true; }
        require(output_refused, "failed suffix rewrite still served protocol output");
        bool proposal_refused{};
        try { (void)node.propose(command(9)); }
        catch (const std::runtime_error&) { proposal_refused = true; }
        require(proposal_refused, "failed suffix rewrite still accepted a proposal");
    }

    // Injected orderly worker exceptions are not a power-loss model.  They do
    // prove restart accepts only one whole publication: old [1..3], or the
    // renamed surviving prefix [1], with every retained command byte exact.
    ReplicatedKernel recovered(identity_test::recover(root.path / "node", 1, {1, 2}));
    const auto records = identity_test::read_wal(root.path / "node");
    require(records.size() == 1 || records.size() == 3,
            "restart adopted a partial suffix rewrite image");
    for (std::size_t i = 0; i < records.size(); ++i)
        require(records[i].index == i + 1 && records[i].payload == command(i + 1),
                "restart altered a command in the old/new valid suffix image");
    require(recovered.status().current_index == static_cast<std::int64_t>(records.size()) &&
                recovered.status().applied_index == 0,
            "restart state does not match the selected whole suffix image");
}
}
int main() {
    try {
        for (bool previous : {false, true})
            for (bool higher : {false, true})
                for (bool staged : {false, true})
                    for (auto cut : {StorageCheckpoint::rewrite_block, StorageCheckpoint::file_renamed})
                        run(previous, higher, staged, cut);
        for (auto cut : {StorageCheckpoint::rewrite_block,
                         StorageCheckpoint::file_renamed})
            run_worker_failure(cut);
        std::cout << "async suffix tests passed: 16 conflict cuts + 2 worker failures\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
