#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main

namespace {

void copy_replace(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
}

template <class Error, class Code>
void expect_typed_recovery_refusal(const std::filesystem::path& directory,
                                   std::int32_t node, std::vector<std::int32_t> voters,
                                   std::uint8_t lineage, Code expected,
                                   const char* context) {
    const auto before = directory_image(directory);
    try {
        ReplicatedKernel recovered(identity_test::recover(
            directory, node, std::move(voters), catalog_test::standard_catalog(), lineage));
    } catch (const Error& error) {
        if constexpr (std::is_same_v<Error, BoundMetadataError>)
            require(error.kind() == BoundMetadataErrorKind::frame &&
                        error.frame_error() == expected, context);
        else
            require(error.code() == expected, context);
        require(directory_image(directory) == before,
                std::string(context) + " changed durable directory bytes");
        return;
    }
    throw std::runtime_error(std::string(context) + " was accepted");
}

void foreign_swaps(const std::filesystem::path& root) {
    const auto donor_dir = root / "donor" / "1";
    const auto target_dir = root / "target" / "1";
    {
        Cluster donor(root / "donor", 1, 0x31);
        Cluster target(root / "target", 1, 0x42);
        (void)donor.commit(command(1, AdvanceKillEpoch{KillEpoch{9}}));
        (void)target.commit(command(1, AdvanceKillEpoch{KillEpoch{1}}));
        (void)donor.commit(command(2, SetExecutorGeneration{ExecutorGeneration{1}}));
        (void)target.commit(command(2, SetExecutorGeneration{ExecutorGeneration{1}}));
        (void)donor.commit(command(3, SetStrategyMode{
            StrategyMode::normal, KillEpoch{9}, ExecutorGeneration{1}}));
        (void)target.commit(command(3, SetStrategyMode{
            StrategyMode::normal, KillEpoch{1}, ExecutorGeneration{1}}));
        (void)donor.commit(command(4, SetStrategyMode{
            StrategyMode::halted, KillEpoch{9}, ExecutorGeneration{1}}));
        (void)target.commit(command(4, SetStrategyMode{
            StrategyMode::halted, KillEpoch{1}, ExecutorGeneration{1}}));
        require(identity_test::complete_compaction(donor.node(1)) > 0 && identity_test::complete_compaction(target.node(1)) > 0,
                "foreign fixture did not publish snapshots");
        (void)donor.commit(command(5, SetExecutorGeneration{ExecutorGeneration{2}}));
        (void)target.commit(command(5, SetExecutorGeneration{ExecutorGeneration{2}}));
    }
    require(identity_test::stored_identity(donor_dir).manifest_digest() !=
                identity_test::stored_identity(target_dir).manifest_digest(),
            "foreign fixture lineages unexpectedly match");

    const std::array cases{
        std::pair{std::string{velocita::kV2MetadataFileName}, 0},
        std::pair{std::string{"v2-wal-blocks.bin"}, 1},
        std::pair{std::string{"v2-snapshot.bin"}, 2},
    };
    for (const auto& [name, kind] : cases) {
        const auto saved = root / ("saved-" + name);
        const auto target_file = identity_test::runtime_directory(target_dir) / name;
        const auto donor_file = identity_test::runtime_directory(donor_dir) / name;
        copy_replace(target_file, saved);
        copy_replace(donor_file, target_file);
        if (kind == 0)
            expect_typed_recovery_refusal<BoundMetadataError>(
                target_dir, 1, {1}, 0x42, BoundFrameError::foreign_manifest,
                "foreign metadata");
        else if (kind == 1)
            expect_typed_recovery_refusal<BoundWalError>(
                target_dir, 1, {1}, 0x42, BoundFrameError::foreign_manifest,
                "foreign WAL");
        else
            expect_typed_recovery_refusal<SnapshotStoreError>(
                target_dir, 1, {1}, 0x42, BoundFrameError::foreign_manifest,
                "foreign snapshot");
        copy_replace(saved, target_file);
        ReplicatedKernel healthy(identity_test::recover(
            target_dir, 1, {1}, catalog_test::standard_catalog(), 0x42));
        require(healthy.state().kill_epoch == KillEpoch{1},
                "restored healthy lineage did not recover kill epoch one");
    }
}

void wrong_local_owner(const std::filesystem::path& root) {
    const auto one = root / "one";
    const auto two = root / "two";
    {
        ReplicatedKernel first(identity_test::bootstrap(one, 1, {1, 2},
                                  catalog_test::standard_catalog(), 0x55));
        ReplicatedKernel second(identity_test::bootstrap(two, 2, {1, 2},
                                   catalog_test::standard_catalog(), 0x55));
        require(first.static_identity().manifest_digest() == second.static_identity().manifest_digest(),
                "same-manifest fixture does not share a manifest");
    }
    const auto saved_metadata = root / "owner-metadata.saved";
    const auto one_metadata = identity_test::runtime_directory(one) /
        velocita::kV2MetadataFileName;
    const auto two_metadata = identity_test::runtime_directory(two) /
        velocita::kV2MetadataFileName;
    copy_replace(one_metadata, saved_metadata);
    copy_replace(two_metadata, one_metadata);
    expect_typed_recovery_refusal<BoundMetadataError>(
        one, 1, {1, 2}, 0x55, BoundFrameError::wrong_owner, "wrong-owner metadata");
    copy_replace(saved_metadata, one_metadata);

    WalBlockArena arena;
    velocita::WalRecord record{1, 1, 1, 0,
        encoded(command(1, AdvanceKillEpoch{KillEpoch{1}}))};
    require(arena.stage(encode_bound_wal_record(record, identity_test::stored_identity(two)), 1) ==
                StageOutcome::staged, "wrong-owner WAL fixture did not stage");
    arena.seal_open_block();
    { auto segment = identity_test::wal_segment(two); require(segment.flush(arena) == 1,
          "wrong-owner WAL fixture did not flush"); }
    copy_replace(identity_test::runtime_directory(two) / "v2-wal-blocks.bin",
                 identity_test::runtime_directory(one) / "v2-wal-blocks.bin");
    expect_typed_recovery_refusal<BoundWalError>(
        one, 1, {1, 2}, 0x55, BoundFrameError::wrong_owner, "wrong-owner WAL");
}

void missing_hard_state_and_legacy_flat(const std::filesystem::path& root) {
    const auto directory = root / "voter";
    std::int64_t term{};
    {
        ReplicatedKernel node(identity_test::bootstrap(directory, 1, {1, 2}));
        identity_test::campaign(node, 2);
        term = identity_test::metadata_store(directory).load().term;
        require(term > 0, "fixture voter did not persist a term");
    }
    const auto saved = root / "hard-state.saved";
    const auto metadata_path = identity_test::runtime_directory(directory) /
        velocita::kV2MetadataFileName;
    std::filesystem::rename(metadata_path, saved);
    const auto before = directory_image(directory);
    try {
        ReplicatedKernel missing(identity_test::recover(directory, 1, {1, 2}));
        throw std::runtime_error("missing hard state was accepted");
    } catch (const BoundMetadataError& error) {
        require(error.kind() == BoundMetadataErrorKind::absent,
                "missing hard state did not report typed absence");
    }
    require(directory_image(directory) == before, "missing-hard-state refusal changed files");
    std::filesystem::rename(saved, metadata_path);
    { ReplicatedKernel healthy(identity_test::recover(directory, 1, {1, 2}));
      require(healthy.status().term == term, "restored hard state lost its term"); }

    const auto legacy_path = identity_test::runtime_directory(directory) /
        velocita::kV2WalFileName;
    std::ofstream legacy(legacy_path, std::ios::binary);
    legacy.close();
    expect_typed_recovery_refusal<BoundWalError>(
        directory, 1, {1, 2}, 0xAB, BoundFrameError::unsupported_schema,
        "empty legacy flat WAL");
    std::filesystem::remove(legacy_path);
    ReplicatedKernel healthy(identity_test::recover(directory, 1, {1, 2}));
    require(healthy.status().term == term, "healthy recovery after legacy removal lost term");
}

void metadata_term_must_cover_retained_history(const std::filesystem::path& root,
                                                const std::string& selected = {}) {
    if (selected.empty() || selected == "wal-term") {
    const auto wal_dir = root / "wal";
    {
        ReplicatedKernel node(identity_test::bootstrap(wal_dir, 1, {1}));
    }
    WalBlockArena arena;
    velocita::WalRecord later_term{1, 9, 1, 0,
        encoded(command(1, AdvanceKillEpoch{KillEpoch{1}}))};
    require(arena.stage(encode_bound_wal_record(
                later_term, identity_test::stored_identity(wal_dir)), 1) == StageOutcome::staged,
            "higher-term WAL fixture did not stage");
    arena.seal_open_block();
    { auto segment = identity_test::wal_segment(wal_dir);
      require(segment.flush(arena) == 1, "higher-term WAL fixture did not flush"); }
    const auto wal_before = directory_image(wal_dir);
    try {
        ReplicatedKernel recovered(identity_test::recover(wal_dir, 1, {1}));
        throw std::runtime_error("metadata term behind retained WAL term was accepted");
    } catch (const BoundWalError& error) {
        require(error.code() == BoundFrameError::invalid_payload &&
                    std::string(error.what()) == "metadata term is behind retained WAL term",
                "WAL term regression lacked exact typed refusal");
    }
    require(directory_image(wal_dir) == wal_before,
            "WAL term-regression refusal changed durable bytes");
    }

    if (selected.empty() || selected == "snapshot-term") {
    const auto snapshot_dir = root / "snapshot";
    {
        ReplicatedKernel node(identity_test::bootstrap(snapshot_dir, 1, {1}));
    }
    identity_test::metadata_store(snapshot_dir).save({1, -1, 1});
    TradingState state;
    identity_test::snapshot_store(snapshot_dir).save(state, 1, 9);
    const auto snapshot_before = directory_image(snapshot_dir);
    try {
        ReplicatedKernel recovered(identity_test::recover(snapshot_dir, 1, {1}));
        throw std::runtime_error("metadata term behind snapshot term was accepted");
    } catch (const SnapshotStoreError& error) {
        require(error.code() == BoundFrameError::invalid_payload &&
                    std::string(error.what()) == "metadata term is behind snapshot term",
                "snapshot term regression lacked exact typed refusal");
    }
    require(directory_image(snapshot_dir) == snapshot_before,
            "snapshot term-regression refusal changed durable bytes");
    }
}

void snapshot_covered_history_must_still_be_consistent(const std::filesystem::path& root) {
    const auto directory = root / "1";
    std::vector<velocita::WalRecord> originals;
    {
        Cluster cluster(root, 1);
        for (std::uint64_t sequence = 1; sequence <= 3; ++sequence)
            (void)cluster.commit(command(sequence, AdvanceKillEpoch{KillEpoch{sequence}}));
        originals = identity_test::read_wal(directory);
        require(originals.size() == 3, "covered-history fixture missing its three entries");
        require(identity_test::complete_compaction(cluster.node(1)) == 3, "covered-history fixture did not compact");
    }
    const auto identity = identity_test::stored_identity(directory);
    const auto plant = [&](const std::vector<velocita::WalRecord>& records) {
        std::vector<std::byte> payload;
        for (const auto& record : records) {
            const auto bytes = identity_test::raw_record(record, identity);
            payload.insert(payload.end(), bytes.begin(), bytes.end());
        }
        write_bytes(identity_test::runtime_directory(directory) / "v2-wal-blocks.bin",
                    frame_block(1, 1, payload));
    };
    // A crash after snapshot publication but before reclamation legitimately
    // leaves this exact overlap; it must not be rejected unconditionally.
    plant(originals);
    { ReplicatedKernel healthy(identity_test::recover(directory, 1, {1}));
      require(healthy.state().kill_epoch == KillEpoch{3}, "valid covered overlap changed state"); }
    for (const auto mode : {"duplicate", "gap", "boundary-term"}) {
        auto altered = originals;
        if (std::string_view(mode) == "duplicate") altered[1].index = altered[0].index;
        else if (std::string_view(mode) == "gap") altered.erase(altered.begin() + 1);
        else {
            // Keep all record terms <= hard state: only the overlap disagrees.
            identity_test::metadata_store(directory).save({2, -1, 3});
            altered.back().term = 2;
        }
        plant(altered);
        expect_typed_recovery_refusal<BoundWalError>(directory, 1, {1}, 0xAB,
            BoundFrameError::invalid_payload, mode);
    }
    plant(originals);
    ReplicatedKernel healthy(identity_test::recover(directory, 1, {1}));
    require(healthy.state().kill_epoch == KillEpoch{3}, "restored covered history failed");
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("velocita-history-binding-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    try {
        if (const char* selected = std::getenv("HISTORY_BINDING_CASE")) {
            const std::string value{selected};
            require(value == "wal-term" || value == "snapshot-term" || value == "covered-history",
                    "unknown HISTORY_BINDING_CASE");
            if (value == "covered-history") snapshot_covered_history_must_still_be_consistent(root / "covered");
            else metadata_term_must_cover_retained_history(root / "term-regression", value);
            std::filesystem::remove_all(root);
            std::cout << "V2 history binding " << value << " test passed\n";
            return 0;
        }
        foreign_swaps(root / "foreign");
        wrong_local_owner(root / "owner");
        missing_hard_state_and_legacy_flat(root / "missing");
        metadata_term_must_cover_retained_history(root / "term-regression");
        snapshot_covered_history_must_still_be_consistent(root / "covered");
        std::filesystem::remove_all(root);
        std::cout << "V2 history binding tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << "V2 history binding test failed: " << error.what() << '\n';
        return 1;
    }
}
