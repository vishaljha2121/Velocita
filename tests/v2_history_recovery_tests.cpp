#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main

namespace {
using namespace velocita;
using namespace velocita::v2;
using namespace std::chrono_literals;

constexpr std::uint64_t kBridgeIndex = kRetainedLogEntries + 1;

struct Root {
    std::filesystem::path path;
    Root() {
        auto pattern = (std::filesystem::temp_directory_path() /
                        "v2-history-recovery-XXXXXX").string();
        std::vector<char> bytes(pattern.begin(), pattern.end());
        bytes.push_back('\0');
        const auto* made = ::mkdtemp(bytes.data());
        require(made != nullptr, "history recovery mkdtemp failed");
        path = std::filesystem::canonical(made);
    }
    ~Root() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

struct Bridge {
    TradingState state;
    std::vector<std::byte> payload;
    std::vector<std::byte> wal_bytes;
};

Bridge build_bridge(const std::filesystem::path& root,
                    std::vector<std::int32_t> members) {
    identity_test::provision(root, 1, std::move(members));
    const auto duplicate = command(1, AdvanceKillEpoch{KillEpoch{1}});
    const auto payload = encoded(duplicate);
    TradingState state = catalog_test::state();
    ApplyScratch scratch;
    for (std::uint64_t index = 1; index <= kBridgeIndex; ++index) {
        const auto outcome = apply(state, duplicate, scratch,
                                   &catalog_test::standard_catalog());
        require(outcome.code == ApplyCode::applied ||
                    outcome.code == ApplyCode::already_applied,
                "independent duplicate bridge reduction failed");
    }
    identity_test::snapshot_store(root).save(state, kBridgeIndex, 1);
    identity_test::metadata_store(root).save(
        {1, -1, static_cast<std::int64_t>(kBridgeIndex)});
    identity_test::RawWalFixture wal(root);
    for (std::uint64_t index = 1; index <= kBridgeIndex; ++index)
        wal.append({index, 1, 1, RAFT_LOGTYPE_NORMAL, payload});
    const auto wal_path = identity_test::runtime_directory(root) / "v2-wal-blocks.bin";
    return {state, payload, read_bytes(wal_path)};
}

void verify_archive(const std::filesystem::path& root,
                    const std::vector<std::byte>& payload,
                    const std::uint64_t through = kBridgeIndex) {
    const auto identity = identity_test::stored_identity(root);
    const auto catalog_bytes = read_bytes(root / "v2-archive" /
                                          "v2-archive-catalog.bin");
    std::array<ArchiveCatalogRow, kMaxArchiveCatalogRows> rows{};
    ArchiveCatalogHeader header;
    std::size_t count{};
    require(decode_archive_catalog(catalog_bytes, identity, header, rows, count) ==
                ArchiveCatalogError::none && count >= 1 &&
                header.observed_commit_index == through &&
                header.observed_durable_index >= through &&
                rows[0].first_index == 1 && rows[count - 1].last_index == through,
            "history recovery catalog does not cover the full bridge");
    std::uint64_t expected_index = 1;
    for (std::size_t row = 0; row < count; ++row) {
        const auto segment = read_bytes(root / "v2-archive" /
            ("s-" + to_hex(rows[row].segment_digest) + ".bin"));
        struct Oracle {
            std::uint64_t* expected_index;
            const std::vector<std::byte>* payload;
        } oracle{&expected_index, &payload};
        ArchiveSegmentScratch scratch;
        ArchiveSegmentMetadata metadata;
        require(decode_archive_segment(segment, identity,
            [](const ArchiveRecordView& record, void* raw) noexcept {
                auto& value = *static_cast<Oracle*>(raw);
                const auto index = (*value.expected_index)++;
                return record.index == index && record.term == 1 &&
                    record.entry_id == 1 && record.entry_type == RAFT_LOGTYPE_NORMAL &&
                    std::ranges::equal(record.payload, *value.payload);
            }, &oracle, scratch, metadata).error == ArchiveSegmentError::none,
            "history recovery segment changed an authored record");
    }
    require(expected_index == through + 1,
            "history recovery archive lost or invented bridge records");
}

void pump_recovery(ReplicatedKernel& node) {
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (node.history_recovery_pending() &&
           std::chrono::steady_clock::now() < deadline) {
        node.flush_durability();
        std::this_thread::yield();
    }
    require(!node.history_recovery_pending(),
            "history recovery worker did not publish before deadline");
}

void successful_recovery_then_progress_and_restart() {
    Root root;
    const auto bridge = build_bridge(root.path, {1});
    const auto wal_path = identity_test::runtime_directory(root.path) /
                          "v2-wal-blocks.bin";
    std::int64_t fresh_index{};
    {
    ReplicatedKernel node(identity_test::recover(root.path, 1, {1}));
    require(node.history_recovery_pending() && node.persistence_pending(),
            "oversized valid bridge did not enter history recovery gate");
    node.start_writer();
    pump_recovery(node);
    require(read_bytes(wal_path) == bridge.wal_bytes,
            "history recovery deleted or rewrote retained WAL evidence");
    verify_archive(root.path, bridge.payload);
    require(node.status().state_digest == state_digest_of(bridge.state) &&
                node.status().applied_index == static_cast<std::int64_t>(kBridgeIndex),
            "history recovery changed the installed snapshot state");

    node.tick(1);
    const auto election_deadline = std::chrono::steady_clock::now() + 5s;
    while (!node.is_leader() && std::chrono::steady_clock::now() < election_deadline) {
        node.flush_durability();
        node.tick(1);
    }
    require(node.is_leader(), "recovered single voter did not resume consensus");
    const auto fresh_command = command(2, AdvanceKillEpoch{KillEpoch{2}});
    const auto fresh = node.propose(encoded(fresh_command));
    require(fresh.status == ProposeStatus::admitted,
            "history-recovered node refused a fresh command");
    const auto commit_deadline = std::chrono::steady_clock::now() + 5s;
    while (node.status().applied_index < fresh.index &&
           std::chrono::steady_clock::now() < commit_deadline) {
        node.flush_durability();
        node.tick(0);
    }
    require(node.status().applied_index == fresh.index &&
                node.state().kill_epoch == KillEpoch{2},
            "fresh command did not commit after history recovery");
    const auto result = node.take_committed_result();
    require(result && result->index == fresh.index &&
                result->identity == fresh_command.identity &&
                result->result.code == ApplyCode::applied,
            "fresh post-recovery commit produced no exact actual result");
    fresh_index = fresh.index;
    node.stop_writer();
    }

    ReplicatedKernel restarted(identity_test::recover(root.path, 1, {1}));
    require(restarted.status().applied_index == fresh_index &&
                restarted.state().kill_epoch == KillEpoch{2},
            "restart lost fresh state after history recovery");
}

struct Gate {
    StorageCheckpoint point{StorageCheckpoint::history_recovery_before_publication};
    std::mutex mutex;
    std::condition_variable changed;
    bool parked{};
    bool released{};
    bool fail{};
    static void observe(StorageCheckpoint point, void* raw) {
        auto& gate = *static_cast<Gate*>(raw);
        if (point != gate.point) return;
        std::unique_lock lock(gate.mutex);
        if (gate.fail) throw std::runtime_error("injected history recovery worker failure");
        gate.parked = true;
        gate.changed.notify_all();
        gate.changed.wait(lock, [&] { return gate.released; });
    }
    void release() { std::lock_guard lock(mutex); released = true; changed.notify_all(); }
};
struct Release { Gate& gate; ~Release() { gate.release(); } };

void paused_recovery_gates_protocol() {
    Root root;
    (void)build_bridge(root.path, {1, 2});
    Gate gate;
    ReplicatedKernel node(identity_test::recover(root.path, 1, {1, 2}),
                          {&Gate::observe, &gate});
    Release release{gate};
    node.start_writer();
    node.flush_durability();
    {
        std::unique_lock lock(gate.mutex);
        require(gate.changed.wait_for(lock, 5s, [&] { return gate.parked; }),
                "history recovery worker did not reach publication pause");
    }
    require(node.history_recovery_pending() && !node.is_leader(),
            "paused history recovery exposed leadership");
    const auto before_tick = node.status();
    node.tick(1'000'000);
    const auto after_tick = node.status();
    require(after_tick.term == before_tick.term &&
                after_tick.role == before_tick.role &&
                after_tick.leader_id == before_tick.leader_id &&
                after_tick.current_index == before_tick.current_index &&
                after_tick.commit_index == before_tick.commit_index &&
                after_tick.applied_index == before_tick.applied_index &&
                after_tick.state_digest == before_tick.state_digest,
            "paused history recovery tick mutated fenced owner state");
    require(node.propose(encoded(command(2, AdvanceKillEpoch{KillEpoch{2}}))).status ==
                ProposeStatus::persistence_busy,
            "paused history recovery admitted a proposal");
    const PeerMessage vote{2, 1, RequestVote{99, 2, 0, 0}};
    const auto frame = identity_test::frame_peer_body(
        vote, identity_test::declared_sender_identity(vote, node.static_identity()));
    require(node.receive_peer(frame).status == PeerReceiveStatus::busy,
            "paused history recovery accepted peer mutation");
    require(!identity_test::take_outbound_message(node) &&
                !node.take_authority_proof() && node.ready_result_count() == 0,
            "paused history recovery exposed output, proof, or result");
    gate.release();
    pump_recovery(node);
}

void published_archive_still_waits_for_owner_completion() {
    Root root;
    const auto bridge = build_bridge(root.path, {1, 2});
    Gate gate;
    gate.point = StorageCheckpoint::history_recovery_published;
    ReplicatedKernel node(identity_test::recover(root.path, 1, {1, 2}),
                          {&Gate::observe, &gate});
    Release release{gate};
    node.start_writer();
    node.flush_durability();
    {
        std::unique_lock lock(gate.mutex);
        require(gate.changed.wait_for(lock, 10s, [&] { return gate.parked; }),
                "history worker did not pause after durable catalog publication");
    }
    // Disk publication is complete, but only the owner may consume the exact
    // completion and release protocol work.
    verify_archive(root.path, bridge.payload);
    require(node.history_recovery_pending() && node.persistence_pending() &&
                !node.is_leader(),
            "durable catalog bypassed owner completion fencing");
    node.tick(1'000'000);
    require(node.propose(encoded(command(2, AdvanceKillEpoch{KillEpoch{2}}))).status ==
                ProposeStatus::persistence_busy &&
                !identity_test::take_outbound_message(node) &&
                !node.take_authority_proof(),
            "published-but-uncollected recovery exposed protocol work");
    gate.release();
    pump_recovery(node);
}

void partial_archive_resumes_across_cold_restart() {
    Root root;
    auto bridge = build_bridge(root.path, {1});
    constexpr std::uint64_t first_cut = kRetainedLogEntries;
    identity_test::snapshot_store(root.path).save(bridge.state, first_cut, 1);
    identity_test::metadata_store(root.path).save(
        {1, -1, static_cast<std::int64_t>(first_cut)});
    const auto wal_path = identity_test::runtime_directory(root.path) /
                          "v2-wal-blocks.bin";
    {
        ReplicatedKernel first(identity_test::recover(root.path, 1, {1}));
        require(first.history_recovery_pending(),
                "8192 snapshot overlap did not request its first archive bridge");
        first.start_writer();
        pump_recovery(first);
        require(first.ready_result_count() == 0 && !first.take_committed_result(),
                "first bridge publication repeated a recovery result");
    }
    verify_archive(root.path, bridge.payload, first_cut);
    require(read_bytes(wal_path) == bridge.wal_bytes,
            "first bridge publication deleted overlapping WAL");

    // This is a cold restart-overlap witness, not a simulated physical crash:
    // advance the independently valid snapshot/metadata while leaving the full
    // retained WAL and already-published archive prefix in place.
    identity_test::snapshot_store(root.path).save(bridge.state, kBridgeIndex, 1);
    identity_test::metadata_store(root.path).save(
        {1, -1, static_cast<std::int64_t>(kBridgeIndex)});
    {
        ReplicatedKernel resumed(identity_test::recover(root.path, 1, {1}));
        require(resumed.history_recovery_pending(),
                "archive8192 plus snapshot8193 did not resume the missing record");
        resumed.start_writer();
        pump_recovery(resumed);
        require(resumed.status().applied_index == static_cast<std::int64_t>(kBridgeIndex) &&
                    resumed.status().state_digest == state_digest_of(bridge.state) &&
                    resumed.ready_result_count() == 0 &&
                    !resumed.take_committed_result(),
                "resumed history publication changed state or repeated results");
    }
    verify_archive(root.path, bridge.payload);
    require(read_bytes(wal_path) == bridge.wal_bytes,
            "resumed bridge publication deleted or rewrote overlapping WAL");
}

void worker_failure_preserves_bridge_for_retry() {
    Root root;
    const auto bridge = build_bridge(root.path, {1});
    const auto wal_path = identity_test::runtime_directory(root.path) /
                          "v2-wal-blocks.bin";
    Gate gate;
    gate.fail = true;
    {
        ReplicatedKernel node(identity_test::recover(root.path, 1, {1}),
                              {&Gate::observe, &gate});
        node.start_writer();
        bool failed{};
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!failed && std::chrono::steady_clock::now() < deadline) {
            try { node.flush_durability(); }
            catch (const std::runtime_error&) { failed = true; }
            std::this_thread::yield();
        }
        require(failed && read_bytes(wal_path) == bridge.wal_bytes,
                "history recovery failure did not fail closed with bridge preserved");
        bool output_refused{};
        try { (void)identity_test::take_outbound_message(node); }
        catch (const std::runtime_error&) { output_refused = true; }
        require(output_refused, "failed history recovery still served output");
    }
    ReplicatedKernel retry(identity_test::recover(root.path, 1, {1}));
    retry.start_writer();
    pump_recovery(retry);
    require(read_bytes(wal_path) == bridge.wal_bytes,
            "retry recovery changed preserved bridge WAL");
    verify_archive(root.path, bridge.payload);
}

void bounded_refusals_publish_nothing() {
    {
        Root root;
        const auto bridge = build_bridge(root.path, {1});
        const auto before = directory_image(root.path);
        bool refused{};
        try {
            ReplicatedKernel node(identity_test::recover(root.path, 1, {1}),
                                  {}, {}, {}, HistoryRecoveryConfig{4096});
        } catch (const std::runtime_error&) { refused = true; }
        require(refused, "small WAL recovery budget did not refuse");
        require(!std::filesystem::exists(root.path / "v2-archive") &&
                    directory_image(root.path) == before,
                "small WAL recovery budget published archive bytes");
        (void)bridge;
    }
    {
        Root root;
        const auto bridge = build_bridge(root.path, {1});
        const auto wal_path = identity_test::runtime_directory(root.path) /
                              "v2-wal-blocks.bin";
        bool refused{};
        try {
            ReplicatedKernel node(identity_test::recover(root.path, 1, {1}), {}, {},
                ArchiveStoreConfig{kDefaultArchiveLogicalByteLimit, 1});
            node.start_writer();
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            while (node.history_recovery_pending() &&
                   std::chrono::steady_clock::now() < deadline) {
                node.flush_durability();
                std::this_thread::yield();
            }
        } catch (const std::runtime_error&) { refused = true; }
        require(refused, "insufficient archive object quota did not refuse closed");
        require(read_bytes(wal_path) == bridge.wal_bytes,
                "archive quota refusal changed retained bridge WAL");
        require(!std::filesystem::exists(root.path / "v2-archive" /
                                         "v2-archive-catalog.bin"),
                "archive quota preflight published a partial catalog");
        for (const auto& entry : std::filesystem::directory_iterator(root.path / "v2-archive")) {
            require(!entry.path().filename().string().starts_with("s-"),
                    "archive quota preflight published a partial segment");
        }
    }
}
} // namespace

int main() {
    const char* scenario = "successful recovery";
    try {
        successful_recovery_then_progress_and_restart();
        scenario = "paused pre-publication recovery";
        paused_recovery_gates_protocol();
        scenario = "published completion fence";
        published_archive_still_waits_for_owner_completion();
        scenario = "partial archive resume";
        partial_archive_resumes_across_cold_restart();
        scenario = "worker failure retry";
        worker_failure_preserves_bridge_for_retry();
        scenario = "bounded refusals";
        bounded_refusals_publish_nothing();
        std::cout << "history recovery tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "history recovery tests (" << scenario << "): "
                  << error.what() << '\n';
        return 1;
    }
}
