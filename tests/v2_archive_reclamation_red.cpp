// Historical F5 archive-loss reproducer. The stronger permanent CTest coverage
// is v2-archive-compaction-tests (named-catalog retrieval and restart, not a scan).
#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main

#include <algorithm>
#include <string_view>

namespace {

std::vector<std::byte> hex(const std::string_view text) {
    require(text.size() % 2 == 0, "independent golden has odd hex length");
    std::vector<std::byte> bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t at = 0; at < text.size(); at += 2) {
        bytes.push_back(std::byte(std::stoul(
            std::string(text.substr(at, 2)), nullptr, 16)));
    }
    return bytes;
}

// Extracted from the repository's independently Python-calculated block-v2
// golden (v2_wal_arena_tests), excluding its WAL metadata and outer frame digest:
// this 100-byte literal was not produced by encode_kernel_command at runtime.
// Canonical schema-8 AdvanceKillEpoch command:
// caller=1, request=1, payload identity digest 01:00..., kill epoch=1. This is
// literal evidence, not bytes obtained from the encoder under test.
const std::vector<std::byte>& audited_command_bytes() {
    static const auto bytes = hex(
        "56324b433800000009000600010000000000000001000000000000000100000000000000000000000000000000000000000000000000000000000000"
        "01000000000000007ebfc094cb08c72268b6e92acc835d5b5bbee17c517e564750290c1a437a118b");
    return bytes;
}

Command audited_command() {
    Command value;
    value.identity = {CallerId{1}, RequestSequence{1}, {}};
    value.identity.payload_digest.bytes[0] = std::byte{1};
    value.body = AdvanceKillEpoch{KillEpoch{1}};
    return value;
}

bool contains(const std::span<const std::byte> haystack,
              const std::span<const std::byte> needle) {
    return !needle.empty() && std::search(haystack.begin(), haystack.end(),
                                          needle.begin(), needle.end()) != haystack.end();
}

bool directory_contains(const std::filesystem::path& root,
                        const std::span<const std::byte> needle) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const auto bytes = read_bytes(entry.path());
        if (contains(bytes, needle)) return true;
    }
    return false;
}

void archive_before_last_copy_red(const std::filesystem::path& root) {
    Cluster cluster(root, 1);
    const auto command = audited_command();
    const auto produced = encoded(command);
    const auto& independent = audited_command_bytes();
    require(produced == independent,
            "fixture encoder disagrees with independently authored command/identity bytes");

    const auto committed = cluster.commit(command);
    (void)cluster.commit(::command(2, SetExecutorGeneration{ExecutorGeneration{1}}));
    (void)cluster.commit(::command(3, AdvanceKillEpoch{KillEpoch{2}}));
    cluster.settle(100);
    auto& node = cluster.elect();
    const auto node_root = root / std::to_string(node.status().node_id);

    const auto before = identity_test::read_wal(node_root);
    require(std::ranges::any_of(before, [&](const velocita::WalRecord& record) {
                return record.index == static_cast<std::uint64_t>(committed.index) &&
                       record.payload == independent;
            }),
            "non-vacuity: exact committed canonical command was absent before compaction");

    const auto included = identity_test::complete_compaction(node);
    require(included >= static_cast<std::uint64_t>(committed.index),
            "fixture snapshot did not cover the audited command");
    const auto snapshot = identity_test::snapshot_store(node_root).load();
    if (!snapshot || snapshot->last_included_index < static_cast<std::uint64_t>(committed.index) ||
        snapshot->state.kill_epoch != KillEpoch{2}) {
        std::cerr << "snapshot_present=" << snapshot.has_value()
                  << " included=" << included
                  << " committed_index=" << committed.index
                  << " snapshot_index=" << (snapshot ? snapshot->last_included_index : 0)
                  << " snapshot_kill=" << (snapshot ? snapshot->state.kill_epoch.value() : 0)
                  << '\n';
    }
    require(snapshot && snapshot->last_included_index >=
                            static_cast<std::uint64_t>(committed.index) &&
                snapshot->state.kill_epoch == KillEpoch{2},
            "compaction did not leave a verified recovery snapshot");

    const auto after = identity_test::read_wal(node_root);
    const bool retained_record = std::ranges::any_of(
        after, [&](const velocita::WalRecord& record) {
            return record.index == static_cast<std::uint64_t>(committed.index) &&
                   record.payload == independent;
        });
    const bool retained_anywhere = directory_contains(node_root, independent);
    std::cout << "archive_reclamation included=" << included
              << " audited_index=" << committed.index
              << " snapshot_state_kill=" << snapshot->state.kill_epoch.value()
              << " retained_wal_record=" << retained_record
              << " retained_raw_bytes_anywhere=" << retained_anywhere
              << " stronger_catalog_restart_proof=v2-archive-compaction-tests\n";

    // A recovery snapshot proves current state, not the sequence/caller bytes
    // which ADR 0007 makes the audit record. Until a separately verified archive
    // covers this index, compact must not remove the last raw canonical copy.
    require(retained_record || retained_anywhere,
            "RED F5/A14: compact removed the last raw committed command/identity bytes without verified archive coverage");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::filesystem::exists(argv[1])) {
        std::cerr << "usage: v2_archive_reclamation_red NEW_ROOT\n";
        return 2;
    }
    try {
        archive_before_last_copy_red(argv[1]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
