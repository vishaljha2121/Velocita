#include "velocita/v2/replication.hpp"
#include "v2_identity_test_support.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

// Compile the actual integration privately so this test can name the private
// typed completion through its single friend. The production API exposes no
// completion injection or corruption hook.
#include "../src/v2_replication.cpp"

namespace velocita::v2 {

struct PersistenceFenceTestAccess {
    enum class Fault {
        operation,
        generation,
        sequence,
        durable_index,
        staged_index,
        truncate_target,
        tuple,
        success,
    };
    enum class HistoryFault {
        generation,
        sequence,
        cut,
        covered_through,
        publication_error,
    };

    static bool acquire_worker_completion(ReplicatedKernel& node) {
        return node.persistence_ && node.writer_->take_maintenance_completion();
    }

    static void corrupt(ReplicatedKernel& node, const Fault fault) {
        if (!node.persistence_) throw std::runtime_error("missing persistence job");
        auto& completion = node.persistence_->completion;
        switch (fault) {
            case Fault::operation:
                completion.operation = ReplicatedKernel::PersistenceJob::Operation::truncate_suffix;
                break;
            case Fault::generation: ++completion.storage_generation; break;
            case Fault::sequence: ++completion.sequence; break;
            case Fault::durable_index: ++completion.captured_durable_index; break;
            case Fault::staged_index: ++completion.captured_staged_index; break;
            case Fault::truncate_target: ++completion.truncate_from; break;
            case Fault::tuple: ++completion.durable.term; break;
            case Fault::success: completion.succeeded = false; break;
        }
    }

    static bool accept(ReplicatedKernel& node) {
        return node.accept_persistence_completion();
    }

    static bool acquire_history_completion(ReplicatedKernel& node) {
        return node.history_recovery_ &&
            node.writer_->take_maintenance_completion();
    }

    static void corrupt_history(ReplicatedKernel& node, const HistoryFault fault) {
        if (!node.history_recovery_) throw std::runtime_error("missing history recovery job");
        auto& completion = node.history_recovery_->completion;
        switch (fault) {
            case HistoryFault::generation: ++completion.generation; break;
            case HistoryFault::sequence: ++completion.sequence; break;
            case HistoryFault::cut: ++completion.cut; break;
            case HistoryFault::covered_through:
                ++completion.publication.covered_through;
                break;
            case HistoryFault::publication_error:
                completion.publication.error = ArchiveStoreError::publication_failed;
                break;
        }
    }

    static bool accept_history(ReplicatedKernel& node) {
        return node.accept_history_recovery_completion();
    }

    static bool failed(const ReplicatedKernel& node) noexcept { return node.storage_failed_; }
    static PersistentMetadata durable(const ReplicatedKernel& node) { return node.metadata_; }
    static std::size_t job_bytes() noexcept { return sizeof(ReplicatedKernel::PersistenceJob); }
    static std::size_t history_job_bytes() noexcept {
        return sizeof(ReplicatedKernel::HistoryRecoveryJob);
    }
};

} // namespace velocita::v2

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
        auto pattern = (std::filesystem::temp_directory_path() /
                        "v2-persistence-completion-XXXXXX").string();
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

void await_completion(ReplicatedKernel& node) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (PersistenceFenceTestAccess::acquire_worker_completion(node)) return;
        std::this_thread::yield();
    }
    throw std::runtime_error("worker did not publish persistence completion");
}

std::vector<std::byte> history_command() {
    velocita::v2::Command value;
    value.identity.caller_id = CallerId{1};
    value.identity.request_sequence = RequestSequence{1};
    value.identity.payload_digest.bytes[0] = std::byte{0x41};
    value.body = AdvanceKillEpoch{KillEpoch{1}};
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(value, bytes);
    require(encoded.error == KernelCodecError::none,
            "history completion command did not encode");
    return {bytes.begin(), bytes.begin() +
        static_cast<std::ptrdiff_t>(encoded.bytes_written)};
}

Digest256 build_history_bridge(const std::filesystem::path& root) {
    identity_test::provision(root, 1, {1});
    TradingState state = catalog_test::state();
    ApplyScratch scratch;
    velocita::v2::Command value;
    value.identity.caller_id = CallerId{1};
    value.identity.request_sequence = RequestSequence{1};
    value.identity.payload_digest.bytes[0] = std::byte{0x41};
    value.body = AdvanceKillEpoch{KillEpoch{1}};
    require(apply(state, value, scratch, &catalog_test::standard_catalog()).code ==
                ApplyCode::applied,
            "history completion state did not reduce");
    identity_test::snapshot_store(root).save(state, 1, 1);
    identity_test::metadata_store(root).save({1, -1, 1});
    identity_test::RawWalFixture wal(root);
    wal.append({1, 1, 1, RAFT_LOGTYPE_NORMAL, history_command()});
    return state_digest_of(state);
}

void await_history_completion(ReplicatedKernel& node) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (PersistenceFenceTestAccess::acquire_history_completion(node)) return;
        std::this_thread::yield();
    }
    throw std::runtime_error("worker did not publish history recovery completion");
}

void corrupt_case(const PersistenceFenceTestAccess::Fault fault) {
    AutomaticRoot root;
    ReplicatedKernel node(identity_test::bootstrap(root.path, 1, {1, 2}));
    node.start_writer();
    node.tick(10'000);
    require(node.persistence_pending(), "election did not stage hard state");
    node.flush_durability();
    await_completion(node);
    PersistenceFenceTestAccess::corrupt(node, fault);
    bool refused = false;
    try {
        (void)PersistenceFenceTestAccess::accept(node);
    } catch (const std::runtime_error&) {
        refused = true;
    }
    require(refused, "corrupted completion was accepted");
    require(PersistenceFenceTestAccess::failed(node),
            "corrupted completion did not latch storage failure");
    std::array<std::byte, kMaxBoundPeerBytes> output{};
    bool output_refused = false;
    try {
        (void)node.take_outbound_frame(output);
    } catch (const std::runtime_error&) {
        output_refused = true;
    }
    require(output_refused, "corrupted completion released protocol output");
}

void duplicate_case() {
    AutomaticRoot root;
    ReplicatedKernel node(identity_test::bootstrap(root.path, 1, {1, 2}));
    node.start_writer();
    node.tick(10'000);
    node.flush_durability();
    await_completion(node);
    require(PersistenceFenceTestAccess::accept(node), "valid completion was refused");
    const auto before = PersistenceFenceTestAccess::durable(node);
    require(!PersistenceFenceTestAccess::accept(node), "duplicate completion was accepted");
    const auto after = PersistenceFenceTestAccess::durable(node);
    require(before.term == after.term && before.voted_for == after.voted_for &&
                before.commit_index == after.commit_index &&
                !PersistenceFenceTestAccess::failed(node),
            "duplicate completion mutated durable owner state");
}

void corrupt_history_case(const PersistenceFenceTestAccess::HistoryFault fault) {
    AutomaticRoot root;
    const auto expected_state = build_history_bridge(root.path);
    ReplicatedKernel node(identity_test::recover(root.path, 1, {1}));
    require(node.history_recovery_pending(),
            "cold bridge did not create a history recovery job");
    node.start_writer();
    node.flush_durability();
    await_history_completion(node);
    PersistenceFenceTestAccess::corrupt_history(node, fault);
    bool refused{};
    try { (void)PersistenceFenceTestAccess::accept_history(node); }
    catch (const std::runtime_error&) { refused = true; }
    require(refused && PersistenceFenceTestAccess::failed(node),
            "corrupted history completion did not latch storage failure");
    require(node.history_recovery_pending() &&
                node.status().state_digest == expected_state &&
                node.status().applied_index == 1 &&
                node.state().kill_epoch == KillEpoch{1},
            "corrupted history completion released fence or changed cold state");
    std::array<std::byte, kMaxBoundPeerBytes> output{};
    bool output_refused{};
    try { (void)node.take_outbound_frame(output); }
    catch (const std::runtime_error&) { output_refused = true; }
    require(output_refused,
            "corrupted history completion released protocol output");
}

void duplicate_history_case() {
    AutomaticRoot root;
    const auto expected_state = build_history_bridge(root.path);
    ReplicatedKernel node(identity_test::recover(root.path, 1, {1}));
    node.start_writer();
    node.flush_durability();
    await_history_completion(node);
    require(PersistenceFenceTestAccess::accept_history(node),
            "valid history completion was refused");
    require(!node.history_recovery_pending() &&
                node.status().state_digest == expected_state,
            "valid history completion did not release exact cold state");
    require(!PersistenceFenceTestAccess::accept_history(node),
            "duplicate history completion was accepted");
    require(!PersistenceFenceTestAccess::failed(node) &&
                node.status().state_digest == expected_state,
            "duplicate history completion mutated owner state");
}

} // namespace

int main() {
    try {
        corrupt_case(PersistenceFenceTestAccess::Fault::operation);
        corrupt_case(PersistenceFenceTestAccess::Fault::generation);
        corrupt_case(PersistenceFenceTestAccess::Fault::sequence);
        corrupt_case(PersistenceFenceTestAccess::Fault::durable_index);
        corrupt_case(PersistenceFenceTestAccess::Fault::staged_index);
        corrupt_case(PersistenceFenceTestAccess::Fault::truncate_target);
        corrupt_case(PersistenceFenceTestAccess::Fault::tuple);
        corrupt_case(PersistenceFenceTestAccess::Fault::success);
        duplicate_case();
        corrupt_history_case(PersistenceFenceTestAccess::HistoryFault::generation);
        corrupt_history_case(PersistenceFenceTestAccess::HistoryFault::sequence);
        corrupt_history_case(PersistenceFenceTestAccess::HistoryFault::cut);
        corrupt_history_case(PersistenceFenceTestAccess::HistoryFault::covered_through);
        corrupt_history_case(PersistenceFenceTestAccess::HistoryFault::publication_error);
        duplicate_history_case();
        std::cout << "persistence completion tests passed; job="
                  << PersistenceFenceTestAccess::job_bytes()
                  << " history_job=" << PersistenceFenceTestAccess::history_job_bytes()
                  << " replica=" << sizeof(ReplicatedKernel)
                  << " retry_buffer=" << kMaxBoundPeerBytes << " bytes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "persistence completion test failed: " << error.what() << '\n';
        return 1;
    }
}
