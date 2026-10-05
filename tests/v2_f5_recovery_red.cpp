#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main

namespace {

bool same_record(const velocita::WalRecord& record,
                 const std::span<const std::byte> command_bytes) {
    return record.entry_type == RAFT_LOGTYPE_NORMAL &&
           std::ranges::equal(record.payload, command_bytes);
}

void membership_drift_red(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    (void)cluster.commit(command(1, AdvanceKillEpoch{KillEpoch{1}}));
    (void)cluster.commit(command(2, SetExecutorGeneration{ExecutorGeneration{1}}));
    (void)cluster.commit(command(3, SetStrategyMode{
        StrategyMode::normal, KillEpoch{1}, ExecutorGeneration{1}}));
    (void)cluster.commit(command(4, SetStrategyMode{
        StrategyMode::halted, KillEpoch{1}, ExecutorGeneration{1}}));
    cluster.settle(100);

    auto& original = cluster.elect();
    const auto original_id = original.status().node_id;
    require(identity_test::complete_compaction(original) > 0, "fixture did not create a durable snapshot");

    cluster.partitioned.insert(original_id);
    for (auto& node : cluster.nodes) {
        if (node->status().node_id != original_id) continue;
        node.reset();
        const auto directory = root / std::to_string(original_id);
        const auto before = directory_image(directory);
        bool refused = false;
        try {
            ReplicatedKernel mismatched(identity_test::recover(
                directory, original_id, {original_id}));
        } catch (const IdentityStartupError& error) {
            refused = error.code() == IdentityStartupErrorCode::identity_mismatch;
        }
        require(refused, "F5/A04 membership drift lacked typed identity_mismatch refusal");
        require(directory_image(directory) == before,
                "F5/A04 membership drift refusal changed durable evidence");
        node = std::make_unique<ReplicatedKernel>(
            identity_test::recover(directory, original_id, cluster.members));
        node->tick(1000);
        std::cout << "membership_drift node=" << original_id
                  << " typed_refusal=1 unchanged_tree=1 healthy_restart_leader="
                  << node->is_leader() << '\n';
        require(!node->is_leader(),
                "healthy same-identity restart elected without its configured quorum");
        return;
    }
    throw std::runtime_error("fixture lost the voter selected for restart");
}

void replacement_catchup_red(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    (void)build_authorized_submit(cluster);
    cluster.settle(100);

    auto& leader = cluster.elect();
    const auto leader_id = leader.status().node_id;
    const auto included = identity_test::complete_compaction(leader);
    require(included > 0, "fixture did not compact a committed prefix");

    std::int32_t replacement_id{};
    for (auto& node : cluster.nodes) {
        if (node->status().node_id == leader_id) continue;
        replacement_id = node->status().node_id;
        node.reset();
        node = std::make_unique<ReplicatedKernel>(identity_test::bootstrap(
            root / "fresh-replacement", replacement_id, cluster.members));
        break;
    }
    require(replacement_id != 0, "fixture did not select a replacement voter");
    cluster.settle(5000);
    const auto applied = cluster.node(replacement_id).status().applied_index;
    std::cout << "replacement_catchup included=" << included
              << " replacement_applied=" << applied << '\n';
    require(applied >= static_cast<std::int64_t>(included),
            "RED F5/A08: a fresh configured voter cannot fetch the compacted prefix");
}

void audit_archive_red(const std::filesystem::path& root) {
    Cluster cluster(root, 1);
    (void)cluster.commit(command(0x4F, AdvanceKillEpoch{KillEpoch{6}}));
    (void)cluster.commit(command(0x50, SetExecutorGeneration{ExecutorGeneration{1}}));
    const auto audited = command(0x51, AdvanceKillEpoch{KillEpoch{7}});
    const auto bytes = encoded(audited);
    const auto audited_result = cluster.commit(audited);
    (void)cluster.commit(command(0x52, SetStrategyMode{
        StrategyMode::halted, KillEpoch{7}, ExecutorGeneration{1}}));
    cluster.settle(100);
    auto& node = cluster.elect();
    const auto node_root = root / std::to_string(node.status().node_id);

    velocita::WriteAheadLog legacy(identity_test::runtime_directory(node_root),
                                   velocita::kV2WalFileName);
    const auto before_segment = identity_test::read_wal(node_root);
    const auto before_legacy = legacy.read_all();
    require(std::ranges::any_of(before_segment, [&](const auto& record) {
                return same_record(record, bytes);
            }) || std::ranges::any_of(before_legacy, [&](const auto& record) {
                return same_record(record, bytes);
            }), "non-vacuity: exact audited command was not durable before compaction");
    const auto included = identity_test::complete_compaction(node);
    require(included >= static_cast<std::uint64_t>(audited_result.index),
            "fixture compaction did not cover the exact audited record index");

    const auto after_segment = identity_test::read_wal(node_root);
    const auto after_legacy = legacy.read_all();
    const auto retained_in_segment = std::ranges::any_of(after_segment, [&](const auto& record) {
        return same_record(record, bytes);
    });
    const auto retained_in_legacy = std::ranges::any_of(after_legacy, [&](const auto& record) {
        return same_record(record, bytes);
    });
    std::cout << "audit_archive included=" << included
              << " audited_index=" << audited_result.index
              << " retained_segment=" << retained_in_segment
              << " retained_legacy=" << retained_in_legacy
              << " archive_interface=absent" << '\n';
    require(retained_in_segment || retained_in_legacy,
            "RED F5/A14: compaction reclaimed the exact command without a verified archive handoff");
}

}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: v2_f5_recovery_red membership|replacement|archive ROOT\n";
        return 2;
    }
    const std::string mode = argv[1];
    const std::filesystem::path root = argv[2];
    if (std::filesystem::exists(root)) return 2;
    try {
        if (mode == "membership") membership_drift_red(root);
        else if (mode == "replacement") replacement_catchup_red(root);
        else if (mode == "archive") audit_archive_red(root);
        else return 2;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
