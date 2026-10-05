#define VELOCITA_RESULT_ADMISSION_FIXTURES_ONLY
#include "v2_result_admission_red.cpp"

namespace {

velocita::AppendEntriesResponse take_append_reply(ReplicatedKernel& node) {
    const auto reply = identity_test::take_outbound_message(node);
    require(reply && std::holds_alternative<velocita::AppendEntriesResponse>(reply->payload),
            "append response missing");
    return std::get<velocita::AppendEntriesResponse>(reply->payload);
}

void saturated_prev_mismatch_releases_exact_tail(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root, 2, {1, 2, 3}));
    fill_follower(node, 960, false);
    require(node.status().current_index == 960 && node.status().commit_index == 0,
            "prev mismatch fixture is not a saturated uncommitted suffix");

    const velocita::AppendEntries mismatch{2, 960, 2, 961, {
        {2, 961, RAFT_LOGTYPE_NORMAL, encoded(ordinary_result_command(9001))}}};
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, mismatch});
    const auto refused = take_append_reply(node);
    require(!refused.success && refused.current_index <= 959,
            "prev mismatch acknowledged an omitted entry");
    require(node.status().term == 2 && node.status().current_index == 959 &&
                node.status().commit_index == 0 && node.result_capacity_refusals() == 0,
            "prev mismatch did not truncate exactly one uncommitted tail entry");
    node.flush_durability();
    require(node.durable_index() == 959, "prev mismatch truncation was not durable");

    const velocita::AppendEntries retry{2, 959, 1, 960, {
        {2, 960, RAFT_LOGTYPE_NORMAL, encoded(ordinary_result_command(9002))}}};
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, retry});
    node.flush_durability();
    node.tick(0);
    const auto accepted = take_append_reply(node);
    require(accepted.success && accepted.current_index <= 960 &&
                node.status().current_index == 960 && node.status().applied_index == 960,
            "retransmit did not consume the exactly freed capacity");
    for (std::int64_t index = 1; index < 960; ++index) {
        const auto result = node.take_committed_result();
        require(result && result->index == index &&
                    result->identity.request_sequence ==
                        RequestSequence{static_cast<std::uint64_t>(index)},
                "surviving saturated prefix result changed identity");
    }
    const auto replacement = node.take_committed_result();
    require(replacement && replacement->index == 960 &&
                replacement->identity.request_sequence == RequestSequence{9002} &&
                !node.take_committed_result(),
            "omitted entry appeared or replacement result was lost");
}

struct RewriteFailure {
    bool armed{};
    std::size_t completed_rewrites{};
};

void fail_after_whole_tail_rewrite(const StorageCheckpoint checkpoint, void* raw) {
    auto& state = *static_cast<RewriteFailure*>(raw);
    if (!state.armed) return;
    if (checkpoint == StorageCheckpoint::file_renamed) {
        ++state.completed_rewrites;
    } else if (checkpoint == StorageCheckpoint::directory_synced &&
               state.completed_rewrites == 1) {
        throw std::runtime_error("injected whole suffix completion failure");
    }
}

void failed_mid_suffix_rewrite_recovers_survivors(const std::filesystem::path& root) {
    RewriteFailure failure;
    {
        const StorageCheckpoints checkpoints{&fail_after_whole_tail_rewrite, &failure};
        ReplicatedKernel node(identity_test::bootstrap(root, 2, {1, 2, 3}), checkpoints);
        fill_follower(node, 960, false);
        failure.armed = true;
        velocita::AppendEntries replacement{2, 957, 1, 0, {}};
        for (std::int64_t index = 958; index <= 960; ++index)
            replacement.entries.push_back({2, static_cast<std::int32_t>(index),
                RAFT_LOGTYPE_NORMAL,
                encoded(ordinary_result_command(static_cast<std::uint64_t>(8000 + index)))});
        bool failed = false;
        try {
            identity_test::deliver_declared_peer(node, PeerMessage{3, 2, replacement});
        } catch (const std::runtime_error& error) {
            require(std::string(error.what()).find("injected whole suffix completion failure") !=
                        std::string::npos,
                    "suffix rewrite failed for an unexpected reason");
            failed = true;
        }
        require(failed && failure.completed_rewrites == 1,
                "fixture did not fail after exactly one durable whole-tail rewrite");
        require(node.retained_log_count() == 960 && node.reserved_result_count() == 960 && node.ready_result_count() == 0,
                "failed durable truncation released a surviving result reservation");
        bool latched = false;
        try { node.tick(0); }
        catch (const std::runtime_error& error) {
            latched = std::string(error.what()).find("storage failure") != std::string::npos;
        }
        require(latched, "storage rewrite failure did not close subsequent service");
    }

    ReplicatedKernel recovered(identity_test::recover(root, 2, {1, 2, 3}));
    require(recovered.status().current_index == 957 && recovered.status().commit_index == 0 &&
                recovered.status().applied_index == 0,
            "restart did not recover the precise surviving suffix");
    identity_test::deliver_declared_peer(recovered, PeerMessage{3, 2,
        velocita::AppendEntries{3, 957, 1, 957, {}}});
    for (std::int64_t index = 1; index <= 957; ++index) {
        const auto result = recovered.take_committed_result();
        require(result && result->index == index &&
                    result->identity.request_sequence ==
                        RequestSequence{static_cast<std::uint64_t>(index)},
                "restart changed a surviving suffix identity");
    }
    require(!recovered.take_committed_result(),
            "durably removed tail or failed replacement produced a result");
}

void append_uncommitted_pool_fixture(ReplicatedKernel& node) {
    for (std::uint64_t start = 1; start <= 1024; start += 16) {
        velocita::AppendEntries batch{1, static_cast<std::int64_t>(start - 1),
            start == 1 ? 0 : 1, 0, {}};
        for (auto index = start; index <= std::min(start + 15, std::uint64_t{1024}); ++index) {
            const auto payload = index <= 64
                ? encoded(command(index, AdvanceKillEpoch{KillEpoch{index}}))
                : encoded(ordinary_result_command(index));
            batch.entries.push_back({1, static_cast<std::int32_t>(index),
                                     RAFT_LOGTYPE_NORMAL, payload});
        }
        identity_test::deliver_declared_peer(node, PeerMessage{1, 2, batch});
        node.flush_durability();
        while (identity_test::take_outbound_message(node)) {}
    }
}

void protected_full_control_reuses_released_ordinary(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root, 2, {1, 2, 3}));
    append_uncommitted_pool_fixture(node);
    require(node.status().current_index == 1024 && node.status().commit_index == 0,
            "mixed-pool fixture did not saturate the ledger");

    velocita::AppendEntries replacement{2, 1022, 1, 0, {}};
    replacement.entries.push_back({2, 1023, RAFT_LOGTYPE_NORMAL,
        encoded(command(7001, AdvanceKillEpoch{KillEpoch{7001}}))});
    replacement.entries.push_back({2, 1024, RAFT_LOGTYPE_NORMAL,
        encoded(command(7002, AdvanceKillEpoch{KillEpoch{7002}}))});
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, replacement});
    node.flush_durability();
    const auto reply = take_append_reply(node);
    require(reply.success && node.status().current_index == 1024 &&
                node.status().applied_index == 0,
            "controls could not reuse replacement's released ordinary ownership");
    const velocita::AppendEntries ordinary{3, 1022, 1, 1024, {
        {3, 1023, RAFT_LOGTYPE_NORMAL, encoded(ordinary_result_command(8001))},
        {3, 1024, RAFT_LOGTYPE_NORMAL, encoded(ordinary_result_command(8002))}}};
    identity_test::deliver_declared_peer(node, PeerMessage{1, 2, ordinary});
    node.flush_durability();
    node.tick(0);
    require(node.status().applied_index == 1024 && node.result_capacity_refusals() == 0,
            "replacement inferred released pool from control kind instead of actual ownership");
    for (std::int64_t index = 1; index <= 1022; ++index) {
        const auto result = node.take_committed_result();
        require(result && result->index == index &&
                    result->identity.request_sequence ==
                        RequestSequence{static_cast<std::uint64_t>(index)},
                "mixed-pool surviving result identity changed");
    }
    for (std::int64_t index = 1023; index <= 1024; ++index) {
        const auto result = node.take_committed_result();
        require(result && result->index == index &&
                    result->identity.request_sequence ==
                        RequestSequence{static_cast<std::uint64_t>(6978 + index)},
                "ordinary replacement lost the slot borrowed by a previous control");
    }
    require(!node.take_committed_result(), "mixed-pool replacement emitted extra results");
}

}

int main() {
    try {
        auto pattern =
            (std::filesystem::temp_directory_path() / "v2-result-admission-edges.XXXXXX").string();
        const auto* made = ::mkdtemp(pattern.data());
        require(made != nullptr, "result admission edges mkdtemp failed");
        const std::filesystem::path root{made};
        saturated_prev_mismatch_releases_exact_tail(root / "prev-mismatch");
        failed_mid_suffix_rewrite_recovers_survivors(root / "rewrite-failure");
        protected_full_control_reuses_released_ordinary(root / "mixed-pools");
        std::filesystem::remove_all(root);
        std::cout << "result admission edge tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
