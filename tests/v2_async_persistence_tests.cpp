#include "velocita/v2/replication.hpp"
#include "v2_identity_test_support.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {
struct ActualSyncTrace {
    std::thread::id owner{std::this_thread::get_id()};
    std::atomic<std::size_t> calls{};
    std::atomic<std::size_t> owner_calls{};
    std::atomic<std::size_t> fail_on_call{};
    std::atomic<bool> failure_fired{};
};
std::atomic<ActualSyncTrace*> active_sync_trace{};
} // namespace

int observed_metadata_fsync(const int fd) {
    if (auto* trace = active_sync_trace.load(std::memory_order_acquire)) {
        const auto call = trace->calls.fetch_add(1, std::memory_order_relaxed) + 1;
        if (std::this_thread::get_id() == trace->owner)
            trace->owner_calls.fetch_add(1, std::memory_order_relaxed);
        if (trace->fail_on_call.load(std::memory_order_relaxed) == call) {
            trace->failure_fired.store(true, std::memory_order_release);
            errno = EIO;
            return -1;
        }
    }
    return ::fsync(fd);
}

// Compile the actual store behind a forwarding syscall shim.  Its definitions
// satisfy the static-library references, so the uninstrumented store object is
// not extracted by the linker (the same arrangement as the original RED).
#define fsync observed_metadata_fsync
#include "../src/v2_bound_metadata.cpp"
#undef fsync

namespace {
using namespace velocita;
using namespace velocita::v2;
using namespace std::chrono_literals;

void require(const bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

class AutomaticRoot final {
public:
    AutomaticRoot() {
        auto value = (std::filesystem::temp_directory_path() /
                      "v2-async-persistence-XXXXXX").string();
        std::vector<char> pattern(value.begin(), value.end());
        pattern.push_back('\0');
        const auto* made = ::mkdtemp(pattern.data());
        if (!made) throw std::runtime_error("mkdtemp failed");
        path = std::filesystem::canonical(made);
    }
    ~AutomaticRoot() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

enum class PausePoint { before_file_sync, before_directory_sync };

// MetadataCheckpoints bracket the actual BoundMetadataStore fsync calls.  A
// `file_synced` or `directory_synced` observation therefore means that the
// immediately preceding real syscall succeeded; it is not a mocked durability
// acknowledgement.  The callback parks only a non-owner thread, so the old
// synchronous implementation reaches the owner-sync assertion without hanging.
class MetadataFence final {
public:
    explicit MetadataFence(const PausePoint point)
        : owner_(std::this_thread::get_id()), point_(point) {
        require(active_sync_trace.exchange(&syscalls_) == nullptr,
                "another metadata fsync trace is already active");
    }
    ~MetadataFence() { active_sync_trace.store(nullptr, std::memory_order_release); }

    MetadataCheckpoints checkpoints() noexcept {
        return {&MetadataFence::observe, this};
    }

    void arm() {
        std::lock_guard lock(mutex_);
        armed_ = true;
        worker_parked_ = false;
        released_ = false;
        file_syncs_ = 0;
        directory_syncs_ = 0;
        owner_syncs_ = 0;
        syscalls_.calls.store(0, std::memory_order_relaxed);
        syscalls_.owner_calls.store(0, std::memory_order_relaxed);
        syscalls_.fail_on_call.store(0, std::memory_order_relaxed);
        syscalls_.failure_fired.store(false, std::memory_order_relaxed);
    }

    void inject_sync_failure(const std::size_t call) {
        arm();
        std::lock_guard lock(mutex_);
        // A failure test observes the named actual syscall and does not park it.
        armed_ = false;
        syscalls_.fail_on_call.store(call, std::memory_order_relaxed);
    }

    [[nodiscard]] bool failure_fired() const noexcept {
        return syscalls_.failure_fired.load(std::memory_order_acquire);
    }

    void await_worker_or_fail(const char* scenario) {
        std::unique_lock lock(mutex_);
        const bool reached = changed_.wait_for(lock, 3s, [&] {
            return worker_parked_ || syscalls_.owner_calls.load() != 0;
        });
        require(reached, std::string(scenario) +
            ": metadata persistence reached neither worker nor owner");
        require(syscalls_.owner_calls.load() == 0, std::string("RED F6: ") + scenario +
            " performed BoundMetadataStore fsync on the Raft owner");
        require(worker_parked_, std::string(scenario) +
            ": metadata worker did not stop at the selected durability cut");
    }

    void release() {
        std::lock_guard lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }

    void require_completed_off_owner(const char* scenario) {
        std::lock_guard lock(mutex_);
        require(syscalls_.owner_calls.load() == 0, std::string(scenario) +
            ": completion moved a metadata fsync back to the owner");
        require(syscalls_.calls.load() >= 2, std::string(scenario) +
            ": actual file and directory fsync syscalls were not both observed");
        require(file_syncs_ != 0, std::string(scenario) +
            ": no successful real metadata file fsync was observed");
        require(directory_syncs_ != 0, std::string(scenario) +
            ": no successful real metadata directory fsync was observed");
    }

private:
    static void observe(const MetadataCheckpoint checkpoint, void* raw) {
        static_cast<MetadataFence*>(raw)->at(checkpoint);
    }

    void at(const MetadataCheckpoint checkpoint) {
        std::unique_lock lock(mutex_);
        const bool owner = std::this_thread::get_id() == owner_;
        if (checkpoint == MetadataCheckpoint::file_synced) {
            ++file_syncs_;
            if (owner) ++owner_syncs_;
        } else if (checkpoint == MetadataCheckpoint::directory_synced) {
            ++directory_syncs_;
            if (owner) ++owner_syncs_;
        }
        const bool selected =
            (point_ == PausePoint::before_file_sync &&
             checkpoint == MetadataCheckpoint::file_written) ||
            (point_ == PausePoint::before_directory_sync &&
             checkpoint == MetadataCheckpoint::file_renamed);
        if (armed_ && selected && !owner) {
            worker_parked_ = true;
            changed_.notify_all();
            changed_.wait(lock, [&] { return released_; });
        }
        changed_.notify_all();
    }

    const std::thread::id owner_;
    const PausePoint point_;
    ActualSyncTrace syscalls_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool armed_{};
    bool worker_parked_{};
    bool released_{};
    std::size_t file_syncs_{};
    std::size_t directory_syncs_{};
    std::size_t owner_syncs_{};
};

struct ReleaseOnExit {
    MetadataFence* fence{};
    ~ReleaseOnExit() { if (fence) fence->release(); }
};

template <typename Done>
void pump(ReplicatedKernel& node, Done done, const char* failure) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        node.flush_durability();
        node.tick(0);
        if (done()) return;
        std::this_thread::yield();
    }
    throw std::runtime_error(failure);
}

std::vector<std::byte> kill_command() {
    velocita::v2::Command command;
    command.identity.caller_id = CallerId{1};
    command.identity.request_sequence = RequestSequence{1};
    command.identity.payload_digest.bytes[0] = std::byte{1};
    command.body = AdvanceKillEpoch{KillEpoch{1}};
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(command, bytes);
    require(encoded.error == KernelCodecError::none, "fixture command did not encode");
    return {bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written)};
}

void assert_quarantined(ReplicatedKernel& node, const ReplicaStatus& before,
                        const bool check_apply) {
    require(node.persistence_pending(), "owner did not expose the pending persistence fence");
    std::array<std::byte, kMaxBoundPeerBytes> frame{};
    require(node.take_outbound_frame(frame).error == OutboundTakeError::empty,
            "protocol output was readable before metadata directory durability");
    // This fixture has not prepared an authority barrier; the check is only a
    // boundary smoke test, not evidence that an already-prepared proof is held.
    // Substantive proof-guard coverage belongs in the authority composition
    // fixture, which can build the required committed effect/catalog state.
    require(!node.take_authority_proof(),
            "authority proof escaped before metadata directory durability");
    if (check_apply) {
        const auto during = node.status();
        require(during.applied_index == before.applied_index &&
                    during.state_version == before.state_version &&
                    node.state().kill_epoch == KillEpoch{0},
                "state applied before metadata directory durability");
        require(node.ready_result_count() == 0 && !node.take_committed_result(),
                "committed result escaped before metadata directory durability");
    }
}

void election_case(const PausePoint point, const std::filesystem::path& root) {
    MetadataFence fence(point);
    ReplicatedKernel node(identity_test::bootstrap(root, 1, {1, 2}), {}, fence.checkpoints());
    ReleaseOnExit release{&fence};
    node.start_writer();
    const auto old = identity_test::metadata_store(root).load();
    fence.arm();
    node.tick(10'000);
    node.flush_durability();
    fence.await_worker_or_fail("election");
    assert_quarantined(node, node.status(), false);
    const auto pending_status = node.status();
    node.tick(1'000'000);
    require(node.status().term == pending_status.term,
            "election timer advanced the term while persistence was pending");
    require(node.propose(kill_command()).status == ProposeStatus::persistence_busy,
            "proposal was not explicitly backpressured while persistence was pending");
    const PeerMessage newer{2, 1,
        RequestVoteResponse{pending_status.term + 10, false}};
    const auto newer_frame = identity_test::frame_peer_body(
        newer, identity_test::declared_sender_identity(newer, node.static_identity()));
    const auto busy = node.receive_peer(newer_frame);
    require(busy.status == PeerReceiveStatus::busy &&
                node.status().term == pending_status.term,
            "new higher-term traffic mutated Raft instead of returning busy");
    if (point == PausePoint::before_file_sync) {
        const auto observed = identity_test::metadata_store(root).load();
        require(observed.term == old.term && observed.voted_for == old.voted_for &&
                    observed.commit_index == old.commit_index,
                "election metadata changed before its file fsync");
    }
    fence.release();
    pump(node, [&] { return !node.persistence_pending(); },
         "election persistence did not complete");
    const auto stored = identity_test::metadata_store(root).load();
    require(stored.term > old.term && stored.voted_for == 1,
            "election completion did not publish the exact self-vote tuple");
    const auto message = identity_test::take_outbound_message(node);
    require(message && std::holds_alternative<RequestVote>(message->payload) &&
                std::get<RequestVote>(message->payload).term == stored.term,
            "election did not release its RequestVote after durability");
    fence.require_completed_off_owner("election");
}

void vote_case(const PausePoint point, const std::filesystem::path& root) {
    MetadataFence fence(point);
    ReplicatedKernel node(identity_test::bootstrap(root, 1, {1, 2}), {}, fence.checkpoints());
    ReleaseOnExit release{&fence};
    node.start_writer();
    const auto old = identity_test::metadata_store(root).load();
    fence.arm();
    identity_test::deliver_declared_peer(node, {2, 1, RequestVote{7, 2, 0, 0}});
    node.flush_durability();
    fence.await_worker_or_fail("higher-term vote");
    assert_quarantined(node, node.status(), false);
    if (point == PausePoint::before_file_sync) {
        const auto observed = identity_test::metadata_store(root).load();
        require(observed.term == old.term && observed.voted_for == old.voted_for &&
                    observed.commit_index == old.commit_index,
                "vote metadata changed before its file fsync");
    }
    fence.release();
    pump(node, [&] { return !node.persistence_pending(); },
         "vote persistence did not complete");
    const auto stored = identity_test::metadata_store(root).load();
    require(stored.term == 7 && stored.voted_for == 2,
            "vote completion did not publish the exact granted-vote tuple");
    const auto message = identity_test::take_outbound_message(node);
    require(message && std::holds_alternative<RequestVoteResponse>(message->payload) &&
                std::get<RequestVoteResponse>(message->payload).vote_granted,
            "granted response was not released after durable vote");
    fence.require_completed_off_owner("higher-term vote");
}

void response_case(const PeerPayload& payload, const char* label,
                   const std::filesystem::path& root) {
    MetadataFence fence(PausePoint::before_file_sync);
    ReplicatedKernel node(identity_test::bootstrap(root, 1, {1, 2}), {}, fence.checkpoints());
    ReleaseOnExit release{&fence};
    node.start_writer();
    node.tick(10'000);
    pump(node, [&] { return !node.persistence_pending(); },
         "response fixture did not finish its candidate transition");
    while (identity_test::take_outbound_message(node)) {}
    const auto old = identity_test::metadata_store(root).load();
    require(old.term > 0 && old.voted_for == 1,
            "response fixture did not durably establish a self vote");
    if (std::holds_alternative<AppendEntriesResponse>(payload)) {
        identity_test::deliver_declared_peer(
            node, {2, 1, RequestVoteResponse{old.term, true}});
        require(node.is_leader(), "append-response fixture did not become leader");
        while (identity_test::take_outbound_message(node)) {}
    }
    fence.arm();
    const PeerMessage message{2, 1, payload};
    const auto frame = identity_test::frame_peer_body(
        message, identity_test::declared_sender_identity(message, node.static_identity()));
    const auto received = node.receive_peer(frame);
    require(received.status == PeerReceiveStatus::accepted_deferred,
            std::string(label) + ": higher-term response was not retained behind persistence");
    node.flush_durability();
    fence.await_worker_or_fail(label);
    assert_quarantined(node, node.status(), false);
    require(identity_test::metadata_store(root).load().term == old.term,
            std::string(label) + ": higher term reached disk before file fsync");
    fence.release();
    pump(node, [&] { return !node.persistence_pending(); },
         "higher-term response persistence did not complete");
    const auto stored = identity_test::metadata_store(root).load();
    require(stored.term == 9 && stored.voted_for == -1,
            std::string(label) + ": response did not durably clear the old vote");
    fence.require_completed_off_owner(label);
}

void commit_case(const PausePoint point, const std::filesystem::path& root) {
    MetadataFence fence(point);
    ReplicatedKernel node(identity_test::bootstrap(root, 1, {1}), {}, fence.checkpoints());
    ReleaseOnExit release{&fence};
    node.start_writer();
    // Establish the single-voter leader and finish its term fence before arming
    // the checkpoint under test.
    node.tick(1);
    pump(node, [&] { return !node.persistence_pending() && node.is_leader(); },
         "single voter did not finish leader setup");

    require(node.propose(kill_command()).status == ProposeStatus::admitted,
            "fixture command was not admitted");
    const auto wal_deadline = std::chrono::steady_clock::now() + 5s;
    while (node.durable_index() != 1 && std::chrono::steady_clock::now() < wal_deadline) {
        node.flush_durability();
        std::this_thread::yield();
    }
    require(node.durable_index() == 1, "fixture WAL did not become durable");
    const auto before = node.status();
    require(before.applied_index == 0 && node.ready_result_count() == 0,
            "fixture command applied before the commit-checkpoint turn");

    fence.arm();
    node.tick(0);
    node.flush_durability();
    fence.await_worker_or_fail("commit checkpoint");
    assert_quarantined(node, before, true);
    fence.release();
    pump(node, [&] {
        return !node.persistence_pending() && node.status().applied_index == 1;
    }, "commit persistence did not resume apply");
    const auto stored = identity_test::metadata_store(root).load();
    require(stored.commit_index == 1 && node.state().kill_epoch == KillEpoch{1},
            "commit completion did not publish and apply the exact checkpoint");
    require(node.ready_result_count() == 1,
            "commit completion did not release exactly one result");
    fence.require_completed_off_owner("commit checkpoint");
}

void sync_failure_case(const std::size_t failed_call,
                       const std::filesystem::path& root) {
    {
        MetadataFence fence(PausePoint::before_file_sync);
        ReplicatedKernel node(identity_test::bootstrap(root, 1, {1, 2}), {}, fence.checkpoints());
        ReleaseOnExit release{&fence};
        node.start_writer();
        fence.inject_sync_failure(failed_call);
        const auto before = node.status();
        node.tick(10'000);
        bool failed{};
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!failed && std::chrono::steady_clock::now() < deadline) {
            try {
                node.flush_durability();
            } catch (const std::exception&) {
                failed = true;
            }
            std::this_thread::yield();
        }
        require(fence.failure_fired(), "configured metadata fsync EIO did not fire");
        require(failed, "metadata fsync EIO did not latch the replica closed");
        require(node.status().applied_index == before.applied_index &&
                    node.state().state_version == before.state_version,
                "metadata fsync failure applied state");
        bool output_refused{};
        try {
            std::array<std::byte, kMaxBoundPeerBytes> output{};
            (void)node.take_outbound_frame(output);
        } catch (const std::exception&) {
            output_refused = true;
        }
        require(output_refused, "metadata fsync failure still served protocol output");
        bool proposal_refused{};
        try {
            (void)node.propose(kill_command());
        } catch (const std::exception&) {
            proposal_refused = true;
        }
        require(proposal_refused, "metadata fsync failure still accepted new proposals");
    }

    // The failed publication may leave either the old durable tuple, the new
    // complete tuple after a directory-sync error, or an image recovery refuses.
    // It may never recover a malformed or unrelated tuple.
    try {
        ReplicatedKernel recovered(identity_test::recover(root, 1, {1, 2}));
        const auto stored = identity_test::metadata_store(root).load();
        require((stored.term == 0 && stored.voted_for == -1) ||
                    (stored.term == 1 && stored.voted_for == 1),
                "restart adopted an invalid tuple after metadata fsync EIO");
        require(recovered.status().applied_index == 0,
                "restart invented applied state after metadata fsync EIO");
    } catch (const BoundMetadataError&) {
        // Explicit refusal is an allowed crash image outcome.
    }
}

template <typename Run>
void at_both_cuts(Run run) {
    for (const auto point : {PausePoint::before_file_sync,
                             PausePoint::before_directory_sync}) {
        AutomaticRoot root;
        run(point, root.path);
    }
}
} // namespace

int main() {
    try {
        at_both_cuts(election_case);
        at_both_cuts(vote_case);
        at_both_cuts(commit_case);
        {
            AutomaticRoot root;
            response_case(RequestVoteResponse{9, false}, "higher-term vote response", root.path);
        }
        {
            AutomaticRoot root;
            response_case(AppendEntriesResponse{9, false, 0, 1},
                          "higher-term append response", root.path);
        }
        for (const std::size_t failed_call : {1U, 2U}) {
            AutomaticRoot root;
            sync_failure_case(failed_call, root.path);
        }
        std::cout << "async persistence fence tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "async persistence fence test: " << error.what() << '\n';
        return 1;
    }
}
