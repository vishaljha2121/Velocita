#include "velocita/v2/replication.hpp"
#include "v2_catalog_test_support.hpp"
#include "v2_identity_test_support.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

using namespace velocita;
using namespace velocita::v2;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Digest256 digest(const std::uint64_t sequence) {
    Digest256 value{};
    value.bytes[0] = static_cast<std::byte>(sequence);
    value.bytes[1] = std::byte{0x5a};
    return value;
}

velocita::v2::Command command(const std::uint64_t sequence) {
    return {{CallerId{7}, RequestSequence{sequence}, digest(sequence)},
            AdvanceKillEpoch{KillEpoch{sequence}}};
}

std::vector<std::byte> encoded(const velocita::v2::Command& value) {
    std::array<std::byte, kMaxKernelCommandBytes> storage{};
    const auto result = encode_kernel_command(value, storage);
    require(result.error == KernelCodecError::none, "fixture command did not encode");
    return {storage.begin(), storage.begin() +
        static_cast<std::ptrdiff_t>(result.bytes_written)};
}

bool same_apply_result(const ApplyResult& a, const ApplyResult& b) {
    return a.code == b.code && a.contract_error == b.contract_error &&
        a.metadata_words == b.metadata_words &&
        a.policy_lifecycle == b.policy_lifecycle &&
        a.state_version == b.state_version && a.strategy_mode == b.strategy_mode &&
        a.kill_epoch == b.kill_epoch &&
        a.executor_generation == b.executor_generation && a.detail == b.detail &&
        a.decision_id == b.decision_id && a.decision_code == b.decision_code &&
        a.proposed_directive == b.proposed_directive &&
        a.effective_directive == b.effective_directive &&
        a.evaluation_mode == b.evaluation_mode &&
        a.matched_rule_index == b.matched_rule_index &&
        a.rules_visited == b.rules_visited &&
        a.action_resolution_code == b.action_resolution_code &&
        a.detail_kind == b.detail_kind;
}

void cold_recovery_applies_committed_suffix_without_notifications(
    const std::filesystem::path& root) {
    constexpr std::uint64_t snapshot_index = 2;
    constexpr std::uint64_t captured_commit = 4;
    constexpr std::uint64_t retained_uncommitted = 5;
    constexpr std::int64_t term = 1;

    identity_test::provision(root, 2, {1, 2, 3});

    TradingState snapshot_state{};
    TradingState committed_state{};
    TradingState final_state{};
    ApplyScratch scratch{};
    std::array<ApplyResult, retained_uncommitted> authored_outcomes{};
    for (std::uint64_t index = 1; index <= retained_uncommitted; ++index) {
        const auto authored = command(index);
        authored_outcomes[index - 1] =
            apply(final_state, authored, scratch, &catalog_test::standard_catalog());
        require(authored_outcomes[index - 1].code == ApplyCode::applied,
                "independent reducer rejected fixture command");
        if (index == snapshot_index) snapshot_state = final_state;
        if (index == captured_commit) committed_state = final_state;
    }

    identity_test::snapshot_store(root).save(snapshot_state, snapshot_index, term);
    identity_test::RawWalFixture wal(root);
    for (std::uint64_t index = 1; index <= retained_uncommitted; ++index) {
        wal.append({index, term, static_cast<std::int32_t>(index),
                    RAFT_LOGTYPE_NORMAL, encoded(command(index))});
    }
    identity_test::metadata_store(root).save({term, -1,
        static_cast<std::int64_t>(captured_commit)});

    ReplicatedKernel recovered(identity_test::recover(root, 2, {1, 2, 3}));
    const auto status = recovered.status();
    require(status.current_index == static_cast<std::int64_t>(retained_uncommitted) &&
                status.commit_index == static_cast<std::int64_t>(captured_commit) &&
                status.applied_index == static_cast<std::int64_t>(captured_commit),
            "cold recovery did not install S and apply exactly S<i<=C");
    require(recovered.state().kill_epoch == committed_state.kill_epoch &&
                recovered.state().state_version == committed_state.state_version &&
                recovered.state().kill_epoch == KillEpoch{4} &&
                recovered.state().state_version == 4 &&
                status.state_digest == state_digest_of(committed_state),
            "cold recovery state disagreed with independent suffix reduction");
    require(recovered.ready_result_count() == 0 && !recovered.take_committed_result(),
            "cold recovery unexpectedly promised process-local committed notifications");
    require(recovered.reserved_result_count() == 1,
            "retained i>C did not preserve its ordinary recovery reservation");

    require(recovered.history_recovery_pending(),
            "unarchived snapshot prefix did not quarantine recovery");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (recovered.persistence_pending() &&
           std::chrono::steady_clock::now() < deadline) {
        recovered.flush_durability();
        std::this_thread::yield();
    }
    require(!recovered.persistence_pending() &&
                recovered.reserved_result_count() == 1 &&
                recovered.ready_result_count() == 0 &&
                recovered.status().state_digest == state_digest_of(committed_state),
            "history recovery changed retained result ownership or reducer state");

    const AppendEntries commit_retained{term,
        static_cast<std::int64_t>(retained_uncommitted), term,
        static_cast<std::int64_t>(retained_uncommitted), {}};
    identity_test::deliver_declared_peer(
        recovered, PeerMessage{1, 2, commit_retained});
    const auto commit_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (recovered.ready_result_count() == 0 &&
           std::chrono::steady_clock::now() < commit_deadline) {
        recovered.flush_durability();
        recovered.tick(0);
        std::this_thread::yield();
    }
    const auto result = recovered.take_committed_result();
    require(result && result->index == static_cast<std::int64_t>(retained_uncommitted) &&
                result->term == term && result->identity == command(retained_uncommitted).identity &&
                same_apply_result(result->result, authored_outcomes.back()) &&
                result->result.state_version == 5 && result->result.kill_epoch == KillEpoch{5},
            "retained i>C did not publish its independently authored outcome normally");
    require(recovered.status().state_digest == state_digest_of(final_state) &&
                recovered.reserved_result_count() == 0 &&
                recovered.ready_result_count() == 0 &&
                !recovered.take_committed_result(),
            "retained suffix commit left duplicate or stranded result ownership");

    std::cout << "cold result contract: S=" << snapshot_index
              << " C=" << captured_commit
              << " retained=" << retained_uncommitted
              << " committed_suffix_notifications=0 retained_notifications=1\n";
}

}

int main(int argc, char** argv) {
    std::filesystem::path root;
    bool owns_root = false;
    try {
        require(argc == 1 || argc == 2,
                "usage: v2_snapshot_install_results_contract_tests [ROOT]");
        if (argc == 2) {
            root = argv[1];
        } else {
            std::array<char, 64> pattern{};
            const std::string prefix = "/tmp/v2-install-results.XXXXXX";
            std::copy(prefix.begin(), prefix.end(), pattern.begin());
            require(::mkdtemp(pattern.data()) != nullptr,
                    "could not create owned temporary root");
            root = pattern.data();
            owns_root = true;
            root = std::filesystem::canonical(root);
        }
        if (!owns_root) {
            require(!std::filesystem::exists(root),
                    "refusing to replace an existing fixture root");
            std::filesystem::create_directories(root);
        }
        cold_recovery_applies_committed_suffix_without_notifications(root);
        if (owns_root) {
            std::error_code error;
            std::filesystem::remove_all(root, error);
            require(!error, "could not remove owned temporary root");
        }
        std::cout << "snapshot install result contract prerequisite passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (owns_root && !root.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
        std::cerr << "snapshot install result contract prerequisite failed: "
                  << error.what() << '\n';
        return 1;
    }
}
