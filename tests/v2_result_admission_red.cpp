// Historical _red filename retained; now the permanent result-admission suite.
#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main

namespace {

Command ordinary_result_command(std::uint64_t sequence) {
    return command(sequence, ValidatePolicy{PolicyId{10}, PolicyVersion{1}});
}

#ifndef VELOCITA_RESULT_ADMISSION_FIXTURES_ONLY
void leader_result_capacity(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root, 1, {1}));
    node.tick(1000);
    for (std::uint64_t i = 1; i <= 960; ++i) {
        require(node.propose(encoded(ordinary_result_command(i))).status == ProposeStatus::admitted,
                "ordinary result capacity refused before 960");
        node.flush_durability();
        node.tick(0);
    }
    require(node.status().applied_index == 960, "ordinary fixture did not apply 960 entries");
    const auto before = directory_image(root);
    const auto status = node.status();
    require(node.propose(encoded(ordinary_result_command(961))).status == ProposeStatus::result_capacity_exhausted,
            "961st ordinary result obligation was admitted without consuming a result");
    require(node.status().current_index == status.current_index && directory_image(root) == before,
            "full result refusal changed durable history or log index");
    require(node.propose(encoded(command_from(100, 1, AdvanceKillEpoch{KillEpoch{1}}))).status ==
                ProposeStatus::result_capacity_exhausted,
            "non-operator command spent protected result capacity");
    for (std::uint64_t i = 961; i <= 1024; ++i) {
        require(node.propose(encoded(command(i, AdvanceKillEpoch{KillEpoch{i}})),
                             i == 961 ? ProposeLane::normal : ProposeLane::control).status == ProposeStatus::admitted,
                "ordinary traffic consumed the protected result pool");
        node.flush_durability();
        node.tick(0);
    }
    require(node.propose(encoded(command(1025, AdvanceKillEpoch{KillEpoch{1025}})),
                         ProposeLane::control).status == ProposeStatus::result_capacity_exhausted,
            "result pool exceeded its total1024 bound");
    require(node.result_capacity_refusals() == 3, "leader result refusal counter incorrect");
    auto first = node.take_committed_result();
    require(first && first->index == 1 && first->identity.request_sequence == RequestSequence{1},
            "result saturation lost the oldest result");
    require(node.propose(encoded(ordinary_result_command(1025))).status == ProposeStatus::admitted,
            "consuming an ordinary result did not reopen admission");
    node.flush_durability();
    node.tick(0);
    for (std::int64_t index = 2; index <= 1025; ++index) {
        auto result = node.take_committed_result();
        require(result && result->index == index &&
                result->identity.request_sequence == RequestSequence{static_cast<std::uint64_t>(index)},
                "saturation/drain/retry lost or reordered an accepted result");
    }
    require(!node.take_committed_result(), "extra result after complete drain");
}

#endif

[[maybe_unused]] void fill_follower(ReplicatedKernel& node, std::uint64_t count, bool commit) {
    for (std::uint64_t start = 1; start <= count; start += 16) {
        velocita::AppendEntries batch{1, static_cast<std::int64_t>(start - 1), start == 1 ? 0 : 1,
            commit ? static_cast<std::int64_t>(std::min(start + 15, count)) : 0, {}};
        for (auto i = start; i <= std::min(start + 15, count); ++i)
            batch.entries.push_back({1, static_cast<std::int32_t>(i), RAFT_LOGTYPE_NORMAL,
                                     encoded(ordinary_result_command(i))});
        identity_test::deliver_declared_peer(node, PeerMessage{1, 2, batch});
        node.flush_durability();
        node.tick(0);
        while (identity_test::take_outbound_message(node)) {}
    }
}

#ifndef VELOCITA_RESULT_ADMISSION_FIXTURES_ONLY
void follower_result_capacity(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root, 2, {1, 2, 3}));
    fill_follower(node, 960, true);
    require(node.status().applied_index == 960, "follower fixture did not apply960");
    velocita::AppendEntries batch{2, 960, 1, 960, {
        {2, 961, RAFT_LOGTYPE_NORMAL, encoded(ordinary_result_command(961))},
        {2, 962, RAFT_LOGTYPE_NORMAL, encoded(ordinary_result_command(962))}}};
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, batch});
    require(node.status().current_index == 960,
            "full follower admitted unreserved result obligations");
    require(node.status().term == 2 && node.status().applied_index == 960,
            "result pressure suppressed the new term or advanced apply");
    auto reply = identity_test::take_outbound_message(node);
    require(reply && std::holds_alternative<velocita::AppendEntriesResponse>(reply->payload) &&
            std::get<velocita::AppendEntriesResponse>(reply->payload).current_index <= 960,
            "full follower acknowledged omitted entries");
    const auto first = node.take_committed_result();
    require(first && first->index == 1, "follower lost first ready result");
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, batch});
    require(node.status().current_index == 960,
            "follower partially appended a batch with capacity for only one entry");
    require(node.result_capacity_refusals() == 2, "follower result refusal counter incorrect");
    const auto second = node.take_committed_result();
    require(second && second->index == 2, "follower lost second ready result");
    batch.leader_commit = 962;
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, batch});
    node.flush_durability();
    node.tick(0);
    require(node.status().applied_index == 962, "drain/retry did not admit the whole batch");
    for (std::int64_t index = 3; index <= 962; ++index) {
        const auto result = node.take_committed_result();
        require(result && result->index == index, "follower saturation lost a result");
    }
    require(!node.take_committed_result(), "follower duplicate result after retry");
}

void recovery_result_reservations(const std::filesystem::path& root) {
    {
        ReplicatedKernel node(identity_test::bootstrap(root, 2, {1, 2, 3}));
        fill_follower(node, 960, false);
        require(node.status().applied_index == 0 && node.status().current_index == 960,
                "recovery fixture must contain an entirely uncommitted suffix");
    }
    ReplicatedKernel node(identity_test::recover(root, 2, {1, 2, 3}));
    require(!node.take_committed_result(), "restored uncommitted suffix emitted results");
    const velocita::AppendEntries extra{2, 960, 1, 0, {
        {2, 961, RAFT_LOGTYPE_NORMAL, encoded(ordinary_result_command(961))}}};
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, extra});
    require(node.status().current_index == 960 && node.status().term == 2,
            "restored uncommitted suffix has no result reservations");
    const velocita::AppendEntries commit{2, 960, 1, 960, {}};
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, commit});
    require(node.status().applied_index == 960, "restored reservations did not allow live commit");
    for (std::int64_t index = 1; index <= 960; ++index) {
        const auto result = node.take_committed_result();
        require(result && result->index == index, "restored suffix lost its live result");
    }
    require(!node.take_committed_result(), "restored suffix emitted extra results");
}

void oversized_recovery_refuses(const std::filesystem::path& root) {
    { auto owner = identity_test::bootstrap(root, 2, {1, 2, 3}); }
    auto metadata = identity_test::metadata_store(root);
    metadata.save({1, -1, 0});
    // Valid prior unbounded history; do not depend on future bounded admission to create it.
    identity_test::RawWalFixture wal(root);
    for (std::uint64_t i = 1; i <= 961; ++i)
        wal.append({i, 1, static_cast<std::int32_t>(i), RAFT_LOGTYPE_NORMAL,
                    encoded(ordinary_result_command(i))});
    const auto before = directory_image(root);
    bool refused = false;
    try { ReplicatedKernel node(identity_test::recover(root, 2, {1, 2, 3})); }
    catch (const std::runtime_error& error) {
        require(std::string(error.what()).find("uncommitted result capacity") != std::string::npos,
                std::string("oversized recovery refused for the wrong reason: ") + error.what());
        refused = true;
    }
    require(refused, "oversized uncommitted history bypassed result capacity on recovery");
    require(directory_image(root) == before, "oversized recovery refusal mutated history");
}

void full_uncommitted_replacement(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root, 2, {1, 2, 3}));
    fill_follower(node, 960, false);
    velocita::AppendEntries replacement{2, 0, 0, 16, {}};
    for (int i = 1; i <= 16; ++i)
        replacement.entries.push_back({2, i, RAFT_LOGTYPE_NORMAL,
            encoded(ordinary_result_command(static_cast<std::uint64_t>(1000 + i)))});
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, replacement});
    node.flush_durability();
    node.tick(0);
    require(node.status().current_index == 16 && node.status().applied_index == 16,
            "full uncommitted suffix cannot transfer reservations to replacement");
    for (std::int64_t index = 1; index <= 16; ++index) {
        const auto result = node.take_committed_result();
        require(result && result->index == index &&
                result->identity.request_sequence == RequestSequence{static_cast<std::uint64_t>(1000 + index)},
                "replacement result retained the old command's identity");
    }
    require(!node.take_committed_result(), "truncated commands produced results");
}

void matching_retention_and_markers(const std::filesystem::path& root) {
    {
        ReplicatedKernel node(identity_test::bootstrap(root, 2, {1, 2, 3}));
        fill_follower(node, 959, true);
        velocita::AppendEntries batch{1, 944, 1, 960, {}};
        for (int i = 945; i <= 960; ++i) {
            // Same index/term matches the retained entry in the pinned Raft
            // library; a retransmission must not overwrite its original binding.
            const auto sequence = i < 960 ? 2000 + i : i;
            batch.entries.push_back({1, i, RAFT_LOGTYPE_NORMAL,
                encoded(ordinary_result_command(static_cast<std::uint64_t>(sequence)))});
        }
        identity_test::deliver_declared_peer(node, PeerMessage{1, 2, batch});
        node.flush_durability();
        node.tick(0);
        require(node.status().applied_index == 960 && node.result_capacity_refusals() == 0,
                "matching batch entries consumed duplicate result reservations");
        require(identity_test::complete_compaction(node) == 960, "result retention fixture did not compact");

        const velocita::AppendEntries marker{2, 960, 1, 961, {marker_entry(77, 2)}};
        identity_test::deliver_declared_peer(node, PeerMessage{3, 2, marker});
        node.flush_durability();
        node.tick(0);
        require(node.status().term == 2 && node.status().applied_index == 961,
                "full result pool blocked a result-free authority marker");
        const velocita::AppendEntries later_duplicate{2, 961, 2, 962, {
            {2, 962, RAFT_LOGTYPE_NORMAL, encoded(ordinary_result_command(1))}}};
        identity_test::deliver_declared_peer(node, PeerMessage{3, 2, later_duplicate});
        require(node.status().current_index == 961 && node.result_capacity_refusals() == 1,
                "compaction/term change discarded unread results or duplicate bypassed reservation");
        const auto first = node.take_committed_result();
        require(first && first->index == 1, "compaction lost oldest result");
        identity_test::deliver_declared_peer(node, PeerMessage{3, 2, later_duplicate});
        node.flush_durability();
        node.tick(0);
        for (std::int64_t i = 2; i <= 960; ++i) {
            const auto next = node.take_committed_result();
            require(next && next->index == i &&
                    next->identity.request_sequence == RequestSequence{static_cast<std::uint64_t>(i)},
                    "term/compaction/retransmission changed a retained result identity");
        }
        const auto duplicate = node.take_committed_result();
        require(duplicate && duplicate->index == 962 && duplicate->identity.request_sequence == RequestSequence{1},
                "later-index duplicate lost its separate notification");
        require(!node.take_committed_result(), "marker emitted a business result");
    }
    ReplicatedKernel recovered(identity_test::recover(root, 2, {1, 2, 3}));
    require(recovered.status().applied_index == 962 && !recovered.take_committed_result(),
            "recovery re-emitted previously committed notifications");
}

void partial_batch_refusal(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root, 2, {1, 2, 3}));
    fill_follower(node, 959, true);
    const velocita::AppendEntries batch{2, 959, 1, 961, {
        {2, 960, RAFT_LOGTYPE_NORMAL, encoded(ordinary_result_command(960))},
        {2, 961, RAFT_LOGTYPE_NORMAL, encoded(ordinary_result_command(961))}}};
    bool threw = false;
    try { identity_test::deliver_declared_peer(node, PeerMessage{3, 2, batch}); }
    catch (const std::runtime_error&) { threw = true; }
    require(node.status().current_index == 959,
            "batch capacity refusal appended a partial suffix");
    require(!threw && node.status().term == 2 && node.status().applied_index == 959 &&
                node.result_capacity_refusals() == 1,
            "batch capacity refusal did not process control-only header");
}
#endif
} // namespace

#ifndef VELOCITA_RESULT_ADMISSION_FIXTURES_ONLY
int main() {
    try {
        auto pattern = (std::filesystem::temp_directory_path() / "v2-result-admission.XXXXXX").string();
        const auto* made = ::mkdtemp(pattern.data());
        require(made != nullptr, "result admission mkdtemp failed");
        const std::filesystem::path root{made};
        const auto* selected = std::getenv("RESULT_ADMISSION_CASE");
        const std::string name = selected ? selected : "all";
        require(name == "all" || name == "leader" || name == "follower" || name == "recovery" ||
                name == "oversized-recovery" || name == "replacement" || name == "retention" ||
                name == "partial", "unknown result admission selector");
        if (name == "all" || name == "leader") leader_result_capacity(root / "leader");
        if (name == "all" || name == "follower") follower_result_capacity(root / "follower");
        if (name == "all" || name == "recovery") recovery_result_reservations(root / "recovery");
        if (name == "all" || name == "oversized-recovery") oversized_recovery_refuses(root / "oversized-recovery");
        if (name == "all" || name == "replacement") full_uncommitted_replacement(root / "replacement");
        if (name == "all" || name == "retention") matching_retention_and_markers(root / "retention");
        if (name == "all" || name == "partial") partial_batch_refusal(root / "partial");
        std::filesystem::remove_all(root);
        std::cout << "result admission capacity tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#endif
