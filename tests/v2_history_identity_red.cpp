// Standalone open-contract diagnostics; intentionally outside passing CTest.
#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main

namespace {
void foreign_artifact(const std::filesystem::path& root, const std::string& kind) {
    const auto source_dir = root / "foreign" / "1";
    const auto target_dir = root / "target" / "1";
    {
        Cluster source(root / "foreign", 1, 0x11);
        Cluster target(root / "target", 1, 0x22);
        for (std::uint64_t sequence = 1; sequence <= 4; ++sequence) {
            (void)source.commit(command(sequence, AdvanceKillEpoch{KillEpoch{9}}));
            (void)target.commit(command(sequence, AdvanceKillEpoch{KillEpoch{1}}));
        }
        require(source.node(1).static_identity().manifest_digest() !=
                    target.node(1).static_identity().manifest_digest(),
                "non-vacuity: fixture lineages must differ");
        require(source.node(1).state().kill_epoch == KillEpoch{9} &&
                    target.node(1).state().kill_epoch == KillEpoch{1},
                "non-vacuity: fixture controls were not committed");
        if (kind == "snapshot") require(identity_test::complete_compaction(source.node(1)) > 0, "snapshot fixture did not compact");
    }
    const auto file = kind == "snapshot" ? "v2-snapshot.bin" :
                      kind == "wal" ? "v2-wal-blocks.bin" : velocita::kV2MetadataFileName;
    std::filesystem::copy_file(identity_test::runtime_directory(source_dir) / file,
                              identity_test::runtime_directory(target_dir) / file,
                              std::filesystem::copy_options::overwrite_existing);
    const auto before = directory_image(target_dir);
    // Current contracts cannot emit a typed lineage refusal. A future fix must
    // narrow its oracle to that reason, not count arbitrary startup exceptions.
    ReplicatedKernel recovered(identity_test::recover(target_dir, 1, {1},
                                   catalog_test::standard_catalog(), 0x22));
    std::cout << "foreign_" << kind << " accepted=1 recovered_kill="
              << recovered.state().kill_epoch.value() << " unchanged_tree="
              << (directory_image(target_dir) == before) << '\n';
    throw std::runtime_error("RED F5: foreign " + kind + " accepted under matching local sidecar");
}

void foreign_vote(const std::filesystem::path& root) {
    ReplicatedKernel source(identity_test::bootstrap(root / "foreign", 1, {1, 2},
                                catalog_test::standard_catalog(), 0x11));
    ReplicatedKernel target(identity_test::bootstrap(root / "target", 2, {1, 2},
                                catalog_test::standard_catalog(), 0x22));
    require(source.static_identity().manifest_digest() != target.static_identity().manifest_digest(),
            "non-vacuity: fixture peer lineages must differ");
    const auto before = target.status();
    for (int attempt = 0; attempt != 20; ++attempt) {
        source.tick(2000);
        while (const auto message = identity_test::take_outbound_message(source)) {
            if (!std::holds_alternative<velocita::RequestVote>(message->payload)) continue;
            const auto frame = identity_test::frame_peer_body(*message, source.static_identity());
            const auto error = target.receive_peer(frame);
            const auto after = target.status();
            std::cout << "foreign_vote before_term=" << before.term << " after_term=" << after.term << '\n';
            require(error == BoundFrameError::foreign_manifest,
                    "foreign peer did not return exact manifest refusal");
            require(after.term == before.term && after.current_index == before.current_index &&
                        after.commit_index == before.commit_index &&
                        after.state_digest == before.state_digest,
                    "foreign peer refusal mutated target term, log, or state");
            return;
        }
    }
    throw std::runtime_error("fixture did not generate a vote request");
}

void missing_hard_state(const std::filesystem::path& root) {
    const auto dir = root / "voter";
    std::int64_t previous_term{};
    {
        ReplicatedKernel node(identity_test::bootstrap(dir, 1, {1, 2}));
        node.tick(2000);
        previous_term = node.status().term;
        require(previous_term > 0, "non-vacuity: voter never recorded a term");
        const auto saved = identity_test::metadata_store(dir).load();
        require(saved.term == previous_term && saved.voted_for == 1,
                "non-vacuity: voter did not persist its self-vote");
    }
    std::filesystem::rename(identity_test::runtime_directory(dir) /
                                velocita::kV2MetadataFileName,
                            root / "retained-hard-state.bin");
    ReplicatedKernel restarted(identity_test::recover(dir, 1, {1, 2}));
    std::cout << "missing_hard_state previous_term=" << previous_term
              << " recovered_term=" << restarted.status().term << '\n';
    require(restarted.status().term >= previous_term,
            "RED F5/F6: missing hard state reset a participating voter's term and vote");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "usage: history-identity-red <metadata|wal|snapshot|peer|missing-hard-state> <fresh-root>");
        const std::filesystem::path root = argv[2];
        require(!std::filesystem::exists(root), "diagnostic requires a fresh owned fixture root");
        std::filesystem::create_directories(root);
        const std::string mode = argv[1];
        if (mode == "peer") foreign_vote(root);
        else if (mode == "missing-hard-state") missing_hard_state(root);
        else if (mode == "metadata" || mode == "wal" || mode == "snapshot") foreign_artifact(root, mode);
        else throw std::runtime_error("unknown diagnostic mode");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
