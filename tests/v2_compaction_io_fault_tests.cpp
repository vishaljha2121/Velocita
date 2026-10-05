#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <pthread.h>

enum class FaultCall { none, write, fsync, close, rename, pwrite, fdatasync };
struct FaultPlan {
    std::atomic<FaultCall> call{FaultCall::none};
    std::atomic<unsigned> remaining{};
    std::atomic<bool> fired{};
    std::atomic<bool> armed{};
    std::atomic<unsigned> total_fired{};
    pthread_t thread{};
};
FaultPlan plan;

bool inject(const FaultCall call) noexcept {
    if (plan.call.load(std::memory_order_acquire) != call ||
        !pthread_equal(plan.thread, pthread_self())) return false;
    auto left = plan.remaining.load(std::memory_order_relaxed);
    while (left != 0 && !plan.remaining.compare_exchange_weak(left, left - 1)) {}
    if (left != 1) return false;
    plan.fired.store(true, std::memory_order_release);
    plan.total_fired.fetch_add(1, std::memory_order_relaxed);
    errno = EIO;
    return true;
}

extern "C" ssize_t fault_write(int fd, const void* data, size_t size) noexcept {
    if (inject(FaultCall::write)) return -1;
    return ::write(fd, data, size);
}
extern "C" int fault_fsync(int fd) noexcept {
    if (inject(FaultCall::fsync)) return -1;
    return ::fsync(fd);
}
extern "C" int fault_close(int fd) noexcept {
    const bool fail = inject(FaultCall::close);
    const int result = ::close(fd);
    if (fail) { errno = EIO; return -1; }
    return result;
}
extern "C" int fault_rename(const char* from, const char* to) noexcept {
    if (inject(FaultCall::rename)) return -1;
    return ::rename(from, to);
}
extern "C" ssize_t fault_pwrite(int fd, const void* data, size_t size, off_t at) noexcept {
    if (inject(FaultCall::pwrite)) return -1;
    return ::pwrite(fd, data, size, at);
}
extern "C" int fault_fdatasync(int fd) noexcept {
    if (inject(FaultCall::fdatasync)) return -1;
#if defined(__APPLE__)
    return ::fsync(fd);
#else
    return ::fdatasync(fd);
#endif
}

struct Arm {
    StorageCheckpoint phase{};
    FaultCall call{};
    unsigned occurrence{1};
};
void arm_fault(StorageCheckpoint point, void* raw) {
    const auto& selected = *static_cast<const Arm*>(raw);
    if (point != selected.phase || plan.call.load() != FaultCall::none) return;
    plan.thread = pthread_self();
    plan.armed.store(true, std::memory_order_release);
    plan.remaining.store(selected.occurrence, std::memory_order_relaxed);
    plan.call.store(selected.call, std::memory_order_release);
}

struct TwoVoterFixture {
    std::filesystem::path root;
    Arm arm;
    std::vector<std::unique_ptr<ReplicatedKernel>> nodes;

    TwoVoterFixture(std::filesystem::path path, Arm selected)
        : root(std::move(path)), arm(selected) {
        std::filesystem::create_directories(root);
        for (int id : {1, 2}) {
            auto identity = identity_test::identity(id, {1, 2});
            identity.deployment_mode = DeploymentMode::laboratory;
            nodes.push_back(std::make_unique<ReplicatedKernel>(
                VerifiedIdentityStartup::bootstrap(root / std::to_string(id), identity,
                                                   catalog_test::standard_catalog()),
                StorageCheckpoints{&arm_fault, &arm}));
        }
    }
    void route() {
        std::vector<PeerMessage> messages;
        for (auto& node : nodes)
            while (auto message = identity_test::take_outbound_message(*node))
                messages.push_back(std::move(*message));
        for (const auto& message : messages)
            identity_test::deliver_peer(*nodes[static_cast<std::size_t>(message.recipient_id - 1)],
                                        message,
                                        nodes[static_cast<std::size_t>(message.sender_id - 1)]->static_identity());
    }
    ReplicatedKernel& leader() {
        for (int round = 0; round < 2000; ++round) {
            for (auto& node : nodes) node->flush_durability();
            for (auto& node : nodes) node->tick(10);
            route();
            for (auto& node : nodes) if (node->is_leader()) return *node;
        }
        throw std::runtime_error("fault fixture did not elect");
    }
    void commit(std::uint64_t sequence) {
        auto& elected = leader();
        const auto proposed = elected.propose(encoded(command(
            sequence, AdvanceKillEpoch{KillEpoch{sequence}})));
        require(proposed.status == ProposeStatus::admitted, "fault fixture proposal refused");
        for (int round = 0; round < 400; ++round) {
            for (auto& node : nodes) node->flush_durability();
            for (auto& node : nodes) node->tick(10);
            route();
            if (elected.status().applied_index >= proposed.index) return;
        }
        throw std::runtime_error("fault fixture command did not commit");
    }
};

void run_fault(const std::filesystem::path& root, const Arm arm) {
    plan.call.store(FaultCall::none);
    plan.remaining.store(0);
    plan.fired.store(false);
    plan.armed.store(false);
    TwoVoterFixture fixture(root, arm);
    fixture.commit(1);
    fixture.commit(2);
    fixture.commit(3);
    auto& leader = fixture.leader();
    const auto expected = leader.status();

    const auto suffix = leader.propose(encoded(command(
        4, AdvanceKillEpoch{KillEpoch{4}})));
    require(suffix.status == ProposeStatus::admitted, "durable suffix was refused");
    leader.flush_durability();
    require(leader.status().commit_index == 3 && leader.durable_index() >= 4,
            "fault trace lacks a durable uncommitted WAL suffix");
    const auto before_wal = identity_test::read_wal(root / std::to_string(expected.node_id));
    require(before_wal.size() == 4 && before_wal.back().index == 4,
            "fault trace did not persist the expected four-record WAL");
    for (std::uint64_t sequence = 1; sequence <= 4; ++sequence)
        require(before_wal[sequence - 1].index == sequence &&
                    before_wal[sequence - 1].payload == encoded(command(
                        sequence, AdvanceKillEpoch{KillEpoch{sequence}})),
                "pre-fault WAL disagrees with independently encoded intended command");
    const auto expected_suffix = before_wal.back();

    bool latched{};
    std::string failure;
    (void)leader.compact();
    require(leader.compaction_pending(),
            "compact synchronously refused a committed prefix with durable suffix");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline && !latched) {
        try { (void)leader.compact(); }
        catch (const std::exception& error) { latched = true; failure = error.what(); }
        std::this_thread::yield();
    }
    require(plan.armed.load(), "selected compaction phase was not reached: retained=" +
        std::to_string(leader.retained_log_count()) + " applied=" +
        std::to_string(leader.status().applied_index) + " commit=" +
        std::to_string(leader.status().commit_index) + " durable=" +
        std::to_string(leader.durable_index()));
    require(plan.fired.load(), "selected production syscall was not reached");
    require(latched, "integrated compaction syscall failure did not latch service before deadline");
    require(!failure.empty(), "latched compaction failure lost its underlying exception");
    bool leadership_refused{};
    try { leadership_refused = !leader.is_leader(); }
    catch (const std::exception&) { leadership_refused = true; }
    require(leadership_refused, "storage-failed replica still reported active leadership");
    bool result_refused{};
    try { (void)leader.take_committed_result(); }
    catch (const std::exception&) { result_refused = true; }
    require(result_refused, "storage-failed replica still served committed results");
    const auto leader_id = expected.node_id;
    fixture.nodes.clear();

    const auto before = directory_image(root / std::to_string(leader_id));
    auto identity = identity_test::identity(leader_id, {1, 2});
    identity.deployment_mode = DeploymentMode::laboratory;
    auto recovered = std::make_unique<ReplicatedKernel>(VerifiedIdentityStartup::recover(
        root / std::to_string(leader_id), identity, catalog_test::standard_catalog()));
    require(recovered->status().state_digest == expected.state_digest &&
                recovered->status().applied_index == 3 &&
                recovered->state().kill_epoch == KillEpoch{3},
            "restart after compaction I/O fault changed committed state");
    const auto wal = identity_test::read_wal(root / std::to_string(leader_id));
    const auto found = std::ranges::find_if(wal, [](const velocita::WalRecord& record) {
        return record.index == 4;
    });
    require(found != wal.end() && found->term == expected_suffix.term &&
                found->entry_id == expected_suffix.entry_id &&
                found->entry_type == expected_suffix.entry_type &&
                found->payload == expected_suffix.payload,
            "WAL rewrite fault lost or changed the exact durable suffix tuple");

    const auto catalog_bytes = read_bytes(root / std::to_string(leader_id) /
                                           "v2-archive/v2-archive-catalog.bin");
    std::array<ArchiveCatalogRow, kMaxArchiveCatalogRows> rows{};
    ArchiveCatalogHeader header;
    std::size_t count{};
    require(decode_archive_catalog(catalog_bytes, recovered->static_identity(),
                                   header, rows, count) == ArchiveCatalogError::none &&
                count == 1 && rows[0].first_index == 1 && rows[0].last_index == 3,
            "fault recovery lost authoritative archive coverage through three");
    const auto segment_bytes = read_bytes(root / std::to_string(leader_id) / "v2-archive" /
        ("s-" + to_hex(rows[0].segment_digest) + ".bin"));
    struct ArchiveOracle { const std::vector<velocita::WalRecord>* expected; std::size_t seen{}; } oracle{&before_wal};
    ArchiveSegmentScratch scratch;
    ArchiveSegmentMetadata metadata;
    require(decode_archive_segment(segment_bytes, recovered->static_identity(),
        [](const ArchiveRecordView& record, void* raw) noexcept {
            auto& value = *static_cast<ArchiveOracle*>(raw);
            if (value.seen >= 3) return false;
            const auto& expected_record = (*value.expected)[value.seen++];
            return record.index == expected_record.index && record.term == expected_record.term &&
                record.entry_id == expected_record.entry_id &&
                record.entry_type == expected_record.entry_type &&
                std::ranges::equal(record.payload, expected_record.payload);
        }, &oracle, scratch, metadata).error == ArchiveSegmentError::none && oracle.seen == 3,
        "catalog-named archive did not preserve exact historical tuples through three");
    require(directory_image(root / std::to_string(leader_id)) == before,
            "recovery mutated the preserved fault image");
}

struct AutomaticRoot {
    std::filesystem::path path;
    AutomaticRoot() {
        std::array<char, 64> pattern{};
        const std::string value = "/tmp/v2-compaction-io-XXXXXX";
        std::copy(value.begin(), value.end(), pattern.begin());
        const auto* made = ::mkdtemp(pattern.data());
        if (!made) throw std::runtime_error("mkdtemp failed");
        path = made;
    }
    ~AutomaticRoot() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

int main() {
    try {
        AutomaticRoot root;
        const std::array cases{
            Arm{StorageCheckpoint::compaction_archive_verified, FaultCall::write, 1},
            Arm{StorageCheckpoint::compaction_archive_verified, FaultCall::fsync, 1},
            Arm{StorageCheckpoint::compaction_archive_verified, FaultCall::close, 1},
            Arm{StorageCheckpoint::compaction_archive_verified, FaultCall::rename, 1},
            Arm{StorageCheckpoint::compaction_archive_verified, FaultCall::fsync, 2},
            Arm{StorageCheckpoint::compaction_before_wal_rewrite, FaultCall::pwrite, 1},
#if defined(__APPLE__)
            Arm{StorageCheckpoint::compaction_before_wal_rewrite, FaultCall::fsync, 1},
#else
            Arm{StorageCheckpoint::compaction_before_wal_rewrite, FaultCall::fdatasync, 1},
#endif
            Arm{StorageCheckpoint::compaction_before_wal_rewrite, FaultCall::rename, 1},
#if defined(__APPLE__)
            Arm{StorageCheckpoint::compaction_before_wal_rewrite, FaultCall::fsync, 2},
#else
            Arm{StorageCheckpoint::compaction_before_wal_rewrite, FaultCall::fsync, 1},
#endif
        };
        for (std::size_t index = 0; index < cases.size(); ++index)
            run_fault(root.path / std::to_string(index), cases[index]);
        require(plan.total_fired.load() == cases.size(),
                "not every configured syscall fault fired exactly once");
        std::cout << "integrated compaction I/O fault tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
