#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main

#include <atomic>

namespace {
struct FenceGate {
    StorageCheckpoint point{StorageCheckpoint::compaction_snapshot_verified};
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::thread::id owner{std::this_thread::get_id()};
    bool fail{};
    static void observe(StorageCheckpoint point, void* context) {
        auto& gate = *static_cast<FenceGate*>(context);
        if (point != gate.point) return;
        require(std::this_thread::get_id() != gate.owner,
                "compaction storage operation ran on protocol owner");
        gate.entered.store(true, std::memory_order_release);
        if (gate.fail) throw std::runtime_error("injected compaction persistence failure");
        while (!gate.release.load(std::memory_order_acquire)) std::this_thread::yield();
    }
};

void commit_one(ReplicatedKernel& node) {
    node.tick(1000);
    require(node.propose(encoded(command(1, AdvanceKillEpoch{KillEpoch{1}}))).status == ProposeStatus::admitted,
            "fence fixture did not admit");
    node.flush_durability();
    node.tick(1);
    require(node.propose(encoded(command(2, AdvanceKillEpoch{KillEpoch{2}}))).status == ProposeStatus::admitted,
            "second fence fixture command refused");
    node.flush_durability();
    node.tick(1);
    require(node.status().applied_index == 2 && node.ready_result_count() == 2,
            "fence fixture must apply with unread result");
}

void test_parked_worker(const std::filesystem::path& root) {
    FenceGate gate;
    ReplicatedKernel node(identity_test::bootstrap(root, 1, {1}), {&FenceGate::observe, &gate});
    commit_one(node);
    require(node.compact() == 0 && node.compaction_pending(), "compaction did not enter async fence");
    struct Release { FenceGate& gate; ~Release() { gate.release.store(true, std::memory_order_release); } } release{gate};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!gate.entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    require(gate.entered.load(std::memory_order_acquire), "worker did not reach durable-snapshot fence");
    const auto before = node.status();
    for (int i = 0; i < 100; ++i) {
        require(node.compact() == 0, "owner released compaction before worker completion");
        node.tick(1000);
        node.flush_durability();
        require(!node.is_leader() && !node.take_committed_result(), "pending storage fence served authority or result");
        require(node.propose(encoded(command(2, AdvanceKillEpoch{KillEpoch{2}}))).status == ProposeStatus::persistence_busy,
                "pending fence admitted protocol work");
    }
    require(node.status().applied_index == before.applied_index && node.retained_log_count() == 2,
            "owner progressed or polled retained bytes before durable completion");
    require(identity_test::read_wal(root).size() == 2, "WAL removed before owner release");
    gate.release.store(true, std::memory_order_release);
    require(identity_test::complete_compaction(node) == 2, "released compaction did not finish");
    require(node.retained_log_count() == 0 && identity_test::read_wal(root).empty(),
            "completed transaction did not reclaim both real owners");
    require(node.take_committed_result().has_value(), "fence lost unread result");
}

void test_failure_cuts(const std::filesystem::path& root) {
    const std::array points{StorageCheckpoint::compaction_archive_verified,
        StorageCheckpoint::compaction_snapshot_verified, StorageCheckpoint::compaction_before_wal_rewrite,
        StorageCheckpoint::compaction_wal_rewritten};
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto directory = root / std::to_string(i);
        FenceGate gate;
        gate.point = points[i];
        gate.fail = true;
        {
            ReplicatedKernel node(identity_test::bootstrap(directory, 1, {1}), {&FenceGate::observe, &gate});
            commit_one(node);
            bool failed{};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!failed && std::chrono::steady_clock::now() < deadline) {
                try { (void)node.compact(); }
                catch (const std::runtime_error&) { failed = true; }
                std::this_thread::yield();
            }
            require(failed && gate.entered.load(std::memory_order_acquire), "intended persistence cut did not fire");
            require(!node.is_leader(), "failed compaction retained authority");
            bool refused{};
            try { (void)node.take_committed_result(); } catch (const std::runtime_error&) { refused = true; }
            require(refused, "failed persistence released success");
        }
        ReplicatedKernel recovered(identity_test::recover(directory, 1, {1}));
        require(recovered.status().applied_index == 2 && recovered.state().kill_epoch == KillEpoch{2},
                "recovery across archive/snapshot/rewrite failure lost committed state");
        if (!identity_test::read_wal(directory).empty() && recovered.retained_log_count())
            require(identity_test::complete_compaction(recovered) == 2, "recovered archive publication could not finish");
    }
}

void test_underfilled_cut_defers_without_killing_service(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root, 1, {1}), {}, {},
                          ArchiveStoreConfig{kDefaultArchiveLogicalByteLimit, 1});
    commit_one(node);
    require(node.propose(encoded(command(3, AdvanceKillEpoch{KillEpoch{3}}))).status == ProposeStatus::admitted,
            "protected third obligation did not fit one packed object");
    node.flush_durability();
    require(node.status().applied_index == 2 && node.durable_index() == 3,
            "early-cut fixture did not retain a durable unfinished obligation");
    (void)node.compact();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (node.compaction_pending() && std::chrono::steady_clock::now() < deadline) {
        (void)node.compact();
        std::this_thread::yield();
    }
    require(!node.compaction_pending() && node.is_leader() && node.retained_log_count() == 3 &&
                identity_test::read_wal(root).size() == 3,
            "capacity-declined cut killed service or released accepted history");
    require(!std::filesystem::exists(root / "v2-archive" / "v2-archive-catalog.bin"),
            "underfilled cut published before reaccounting remaining object liability");
    node.tick(1);
    const auto apply_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (node.status().applied_index < 3 &&
           std::chrono::steady_clock::now() < apply_deadline) {
        node.flush_durability();
        node.tick(0);
        std::this_thread::yield();
    }
    require(node.status().applied_index == 3,
            "larger-cut fixture did not resume after durable commit checkpoint");
    require(identity_test::complete_compaction(node) == 3,
            "larger packed cut did not complete after harmless deferral");
}

void test_cached_archive_is_not_deletion_authority(const std::filesystem::path& root) {
    FenceGate gate;
    gate.point = StorageCheckpoint::compaction_archive_verified;
    gate.fail = true;
    {
        ReplicatedKernel node(identity_test::bootstrap(root, 1, {1}), {&FenceGate::observe, &gate});
        commit_one(node);
        bool failed{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!failed && std::chrono::steady_clock::now() < deadline) {
            try { (void)node.compact(); } catch (const std::runtime_error&) { failed = true; }
            std::this_thread::yield();
        }
        require(failed && gate.entered.load(), "archive-ahead fixture did not stop at intended cut");
    }
    ReplicatedKernel recovered(identity_test::recover(root, 1, {1}));
    bool changed{};
    for (const auto& entry : std::filesystem::directory_iterator(root / "v2-archive")) {
        if (!entry.path().filename().string().starts_with("s-")) continue;
        auto bytes = read_bytes(entry.path());
        bytes[100] ^= std::byte{1};
        write_bytes(entry.path(), bytes);
        changed = true;
    }
    require(changed && recovered.retained_log_count() == 2, "cached archive fixture vacuous");
    bool refused{};
    try { (void)identity_test::complete_compaction(recovered); }
    catch (const std::runtime_error&) { refused = true; }
    require(refused && identity_test::read_wal(root).size() == 2,
            "cached archive coverage reclaimed WAL after its named segment was corrupted");
}
}

int main() {
    std::array<char, 64> pattern{};
    const std::string prefix = "/tmp/v2-compaction-fence-XXXXXX";
    std::copy(prefix.begin(), prefix.end(), pattern.begin());
    const char* made = ::mkdtemp(pattern.data());
    if (!made) return 2;
    const std::filesystem::path root{made};
    try {
        test_parked_worker(root / "parked");
        test_failure_cuts(root / "failures");
        test_underfilled_cut_defers_without_killing_service(root / "quota");
        test_cached_archive_is_not_deletion_authority(root / "cached");
        std::filesystem::remove_all(root);
        std::cout << "compaction fence tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " evidence=" << root << '\n';
        return 1;
    }
}
