// Three in-process tape replicas over an in-memory bus: commit, crash and restart, a lost voter, and log recovery.
#include "velocita/tape/replica.hpp"
#include "velocita/v2/digest.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace velocita;
using namespace velocita::tape;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

std::vector<std::byte> bytes_of(const Entry& e) {
    std::vector<std::byte> out(kMaxEntryBytes + 64);
    out.resize(encode_entry(e, out));
    return out;
}

std::vector<std::byte> session_start(std::uint8_t recorder, std::uint64_t session) {
    RecorderEvent e;
    e.header = {recorder, session, 1};
    e.kind = EventKind::session_start;
    return bytes_of(e);
}

std::vector<std::byte> gap_event(std::uint8_t recorder, std::uint64_t session, std::uint64_t request) {
    RecorderEvent e;
    e.header = {recorder, session, request};
    e.kind = EventKind::gap;
    e.expected = request;
    e.received = request + 1;
    return bytes_of(e);
}

struct Cluster {
    std::filesystem::path root;
    std::array<std::unique_ptr<TapeReplica>, 3> nodes;
    std::vector<bool> up;
    std::vector<std::vector<PeerMessage>> inbox;

    explicit Cluster(const std::string& name) : root(std::filesystem::temp_directory_path() / ("tape-replica-" + name)) {
        std::filesystem::remove_all(root);
        up.assign(3, true);
        inbox.resize(3);
        for (int i = 0; i < 3; ++i) start(i);
    }
    ~Cluster() { std::filesystem::remove_all(root); }

    void start(int i) {
        ReplicaConfig c;
        c.node_id = i + 1;
        c.members = {1, 2, 3};
        c.data_dir = root / ("node-" + std::to_string(i + 1));
        c.tape.head_every_ticks = 0;
        nodes[i] = std::make_unique<TapeReplica>(c, [this](const PeerMessage& m) {
            const int to = m.recipient_id - 1;
            if (to >= 0 && to < 3 && up[to]) inbox[to].push_back(m);
        });
        up[i] = true;
    }
    void stop(int i) {
        nodes[i].reset();
        up[i] = false;
        inbox[i].clear();
    }
    void run(int ms) {
        for (int t = 0; t < ms; t += 10) {
            for (int i = 0; i < 3; ++i)
                if (up[i]) nodes[i]->tick(10);
            for (int round = 0; round < 4; ++round)
                for (int i = 0; i < 3; ++i) {
                    if (!up[i]) continue;
                    auto batch = std::move(inbox[i]);
                    inbox[i].clear();
                    for (const auto& m : batch) nodes[i]->receive(m);
                }
        }
    }
    int leader() {
        for (int i = 0; i < 3; ++i)
            if (up[i] && nodes[i]->is_leader()) return i;
        return -1;
    }
    int await_leader() {
        for (int k = 0; k < 600 && leader() < 0; ++k) run(10);
        return leader();
    }
    bool propose(const std::vector<std::byte>& entry) {
        const int l = await_leader();
        return l >= 0 && nodes[l]->propose(entry) == ProposeStatus::proposed;
    }
};

void test_commit_and_identical_history() {
    Cluster c("commit");
    for (std::uint8_t r = 1; r <= 3; ++r) check(c.propose(session_start(r, 100 + r)), "session start proposed");
    for (std::uint64_t q = 2; q < 40; ++q) check(c.propose(gap_event(1, 101, q)), "event proposed");
    c.run(2000);
    const auto a = c.nodes[0]->status(), b = c.nodes[1]->status(), d = c.nodes[2]->status();
    check(a.applied_index == 41 && b.applied_index == 41 && d.applied_index == 41, "all applied 41 entries");
    check(c.nodes[0]->state().frontier.root() == c.nodes[1]->state().frontier.root() &&
              c.nodes[1]->state().frontier.root() == c.nodes[2]->state().frontier.root(),
          "identical history roots");
    check(c.nodes[0]->state().sessions[0].last_request == 39, "session advanced to 39");
}

void test_random_restarts_keep_history_identical() {
    Cluster c("restarts");
    std::mt19937 rng(7);
    for (std::uint8_t r = 1; r <= 3; ++r) check(c.propose(session_start(r, 200 + r)), "session start proposed");
    std::uint64_t next = 2;
    for (int round = 0; round < 30; ++round) {
        for (int k = 0; k < 5; ++k)
            if (c.propose(gap_event(2, 202, next))) ++next;
        c.run(300);
        if (rng() % 3 == 0) {
            const int victim = static_cast<int>(rng() % 3);
            c.stop(victim);
            c.run(200);
            c.start(victim);
        }
        c.run(200);
    }
    c.run(3000);
    std::uint64_t applied = c.nodes[0]->status().applied_index;
    for (int i = 1; i < 3; ++i) applied = std::min(applied, c.nodes[i]->status().applied_index);
    check(applied > 50, "applied a meaningful history");
    const auto commit = c.nodes[0]->status().commit_index;
    bool equal = true;
    for (int i = 0; i < 3; ++i) equal = equal && c.nodes[i]->status().applied_index == commit;
    check(equal, "every voter applied through the commit index");
    check(c.nodes[0]->state().frontier.root() == c.nodes[1]->state().frontier.root() &&
              c.nodes[1]->state().frontier.root() == c.nodes[2]->state().frontier.root(),
          "identical roots after random restarts");
    const auto last = c.nodes[0]->state().sessions[1].last_request;
    check(last == next - 1, "every proposed request committed exactly once, in order");
}

void test_one_voter_down_keeps_committing_and_rejoins() {
    Cluster c("lost-voter");
    for (std::uint8_t r = 1; r <= 3; ++r) check(c.propose(session_start(r, 300 + r)), "session start proposed");
    c.run(500);
    const int l = c.leader();
    const int victim = (l + 1) % 3;
    c.stop(victim);
    const auto before = c.nodes[l]->status().commit_index;
    for (std::uint64_t q = 2; q < 30; ++q) check(c.propose(gap_event(3, 303, q)), "event proposed with one voter down");
    c.run(1000);
    const int l2 = c.leader();
    check(l2 >= 0 && c.nodes[l2]->status().commit_index >= before + 28, "two of three keep committing");
    c.start(victim);
    c.run(3000);
    check(c.nodes[victim]->status().applied_index == c.nodes[l2]->status().applied_index, "rejoined voter caught up");
    check(c.nodes[victim]->state().frontier.root() == c.nodes[l2]->state().frontier.root(), "rejoined voter's root matches");
}

void test_no_majority_commits_nothing() {
    Cluster c("no-majority");
    check(c.propose(session_start(1, 401)), "session start proposed");
    c.run(500);
    const int l = c.leader();
    const auto before = c.nodes[l]->status().commit_index;
    c.stop((l + 1) % 3);
    c.stop((l + 2) % 3);
    (void)c.nodes[l]->propose(gap_event(1, 401, 2));
    c.run(1000);
    check(c.nodes[l]->status().commit_index == before, "a lone leader commits nothing");
}

void test_wal_recovery_trims_torn_tail_and_refuses_corruption() {
    const auto dir = std::filesystem::temp_directory_path() / "tape-wal-recovery";
    std::filesystem::remove_all(dir);
    {
        TapeWal wal(dir);
        (void)wal.recover();
        const std::vector<std::byte> payload(100, std::byte{7});
        for (std::uint64_t i = 1; i <= 5; ++i) wal.append(i, 1, 0, payload);
        wal.sync();
    }
    {
        std::ofstream torn(dir / "wal.log", std::ios::binary | std::ios::app);
        torn.write("TWAL\x01partial", 12);
    }
    {
        TapeWal wal(dir);
        const auto entries = wal.recover();
        check(entries.size() == 5 && entries.back().index == 5, "torn tail trimmed, five entries kept");
    }
    {
        std::fstream f(dir / "wal.log", std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(200);
        f.put('\xFF');
    }
    bool refused = false;
    try {
        TapeWal wal(dir);
        (void)wal.recover();
    } catch (const StorageError&) {
        refused = true;
    }
    check(refused, "mid-log corruption refuses recovery");
    std::filesystem::remove_all(dir);
}

}

int main() {
    std::srand(12345);
    test_commit_and_identical_history();
    test_random_restarts_keep_history_identical();
    test_one_voter_down_keeps_committing_and_rejoins();
    test_no_majority_commits_nothing();
    test_wal_recovery_trims_torn_tail_and_refuses_corruption();
    if (failures) {
        std::cerr << failures << " failures\n";
        return 1;
    }
    std::cout << "tape replica tests passed\n";
    return 0;
}
