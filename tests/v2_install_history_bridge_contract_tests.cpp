#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main

namespace {
using namespace velocita;
using namespace velocita::v2;

struct ContractRoot {
    std::filesystem::path path;
    ContractRoot() {
        auto pattern = (std::filesystem::temp_directory_path() /
                        "v2-install-history-XXXXXX").string();
        std::vector<char> bytes(pattern.begin(), pattern.end());
        bytes.push_back('\0');
        const auto* made = ::mkdtemp(bytes.data());
        require(made != nullptr, "history bridge mkdtemp failed");
        path = std::filesystem::canonical(made);
    }
    ~ContractRoot() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

bool same_history_record(const WalRecord& lhs, const WalRecord& rhs) {
    return lhs.index == rhs.index && lhs.term == rhs.term &&
        lhs.entry_id == rhs.entry_id && lhs.entry_type == rhs.entry_type &&
        lhs.payload == rhs.payload;
}

TradingState authored_state(const std::uint64_t through) {
    TradingState state = catalog_test::state();
    ApplyScratch scratch;
    for (std::uint64_t index = 1; index <= through; ++index) {
        const auto outcome = apply(state,
            command(index, AdvanceKillEpoch{KillEpoch{index}}), scratch,
            &catalog_test::standard_catalog());
        require(outcome.code == ApplyCode::applied,
                "independent bridge reducer rejected authored command");
    }
    return state;
}

void publish_snapshot_and_metadata(const std::filesystem::path& root,
                                   const TradingState& state,
                                   const std::uint64_t index,
                                   const std::int64_t term = 1) {
    identity_test::snapshot_store(root).save(state, index, term);
    identity_test::metadata_store(root).save(
        {term, -1, static_cast<std::int64_t>(index)});
}

void missing_history_refuses_specifically() {
    ContractRoot fixture;
    constexpr std::uint64_t snapshot_index = 3;
    identity_test::provision(fixture.path, 1, {1});
    publish_snapshot_and_metadata(
        fixture.path, authored_state(snapshot_index), snapshot_index);
    const auto before = directory_image(fixture.path);

    bool refused{};
    std::string reason;
    try {
        ReplicatedKernel recovered(identity_test::recover(fixture.path, 1, {1}));
    } catch (const std::runtime_error& error) {
        refused = true;
        reason = error.what();
    }
    require(refused && reason.find("reclaimed history has no verified archive prefix") !=
                           std::string::npos,
            "snapshot without archive or retained prefix did not refuse for missing history");
    require(directory_image(fixture.path) == before,
            "missing-history refusal changed cold storage evidence");
}

void retained_full_prefix_bridges_snapshot() {
    ContractRoot fixture;
    constexpr std::uint64_t snapshot_index = 3;
    constexpr std::int64_t term = 1;
    identity_test::provision(fixture.path, 1, {1});
    const auto expected_state = authored_state(snapshot_index);
    publish_snapshot_and_metadata(fixture.path, expected_state, snapshot_index, term);
    identity_test::RawWalFixture wal(fixture.path);
    std::vector<WalRecord> expected_wal;
    for (std::uint64_t index = 1; index <= snapshot_index; ++index) {
        expected_wal.push_back({index, term, static_cast<std::int32_t>(index),
                    RAFT_LOGTYPE_NORMAL,
                    encoded(command(index, AdvanceKillEpoch{KillEpoch{index}}))});
        wal.append(expected_wal.back());
    }
    const auto before = directory_image(fixture.path);

    ReplicatedKernel recovered(identity_test::recover(fixture.path, 1, {1}));
    const auto status = recovered.status();
    require(status.current_index == static_cast<std::int64_t>(snapshot_index) &&
                status.commit_index == static_cast<std::int64_t>(snapshot_index) &&
                status.applied_index == static_cast<std::int64_t>(snapshot_index),
            "retained full prefix did not bridge the installed snapshot index");
    require(status.state_digest == state_digest_of(expected_state) &&
                recovered.state().kill_epoch == KillEpoch{snapshot_index} &&
                recovered.state().state_version == expected_state.state_version,
            "snapshot bridge recovery disagreed with independent reducer state");
    require(recovered.retained_log_count() == 0 &&
                recovered.ready_result_count() == 0 &&
                recovered.reserved_result_count() == 0 &&
                !recovered.take_committed_result() &&
                !identity_test::take_outbound_message(recovered) &&
                recovered.history_recovery_pending() && recovered.persistence_pending(),
            "cold snapshot bridge manufactured a process-local response promise");
    const auto recovered_wal = identity_test::read_wal(fixture.path);
    require(std::ranges::equal(recovered_wal, expected_wal, same_history_record),
            "snapshot bridge changed an exact retained WAL tuple");
    require(directory_image(fixture.path) == before,
            "snapshot bridge recovery changed retained WAL or other cold bytes");
}

void snapshot_covered_history_enters_gated_recovery() {
    ContractRoot fixture;
    constexpr std::uint64_t snapshot_index = kRetainedLogEntries + 1;
    constexpr std::int64_t term = 1;
    identity_test::provision(fixture.path, 1, {1});

    const auto duplicate = command(1, AdvanceKillEpoch{KillEpoch{1}});
    const auto duplicate_bytes = encoded(duplicate);
    TradingState expected_state = catalog_test::state();
    ApplyScratch scratch;
    for (std::uint64_t index = 1; index <= snapshot_index; ++index) {
        const auto outcome = apply(expected_state, duplicate, scratch,
                                   &catalog_test::standard_catalog());
        require(outcome.code == ApplyCode::applied ||
                    outcome.code == ApplyCode::already_applied,
                "duplicate history reducer fixture was not legitimate");
    }
    publish_snapshot_and_metadata(fixture.path, expected_state, snapshot_index, term);
    identity_test::RawWalFixture wal(fixture.path);
    for (std::uint64_t index = 1; index <= snapshot_index; ++index) {
        wal.append({index, term, 1, RAFT_LOGTYPE_NORMAL, duplicate_bytes});
    }
    const auto before = directory_image(fixture.path);

    ReplicatedKernel recovered(identity_test::recover(fixture.path, 1, {1}));
    require(recovered.history_recovery_pending() && !recovered.is_leader() &&
                recovered.retained_log_count() == 0 && recovered.ready_result_count() == 0,
            "snapshot-covered streaming history bypassed gated recovery");
    require(directory_image(fixture.path) == before,
            "constructing gated recovery changed the 8193-entry cold history image");
}
}

int main() {
    try {
        missing_history_refuses_specifically();
        retained_full_prefix_bridges_snapshot();
        snapshot_covered_history_enters_gated_recovery();
        std::cout << "install history bridge contract witnesses passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "install history bridge contract: " << error.what() << '\n';
        return 1;
    }
}
