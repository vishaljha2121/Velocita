// velocita-tape-voter: one voter of the tape's Raft group.
#include "velocita/tape/config.hpp"
#include "velocita/tape/head_file.hpp"
#include "velocita/tape/link.hpp"
#include "velocita/tape/voter.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace velocita;
using namespace velocita::tape;

namespace {

std::atomic<bool> running{true};
void stop(int) { running = false; }

class LinkNetwork final : public Network {
public:
    explicit LinkNetwork(AeronLink& link) : link_(link) {}
    bool send(const Party& to, std::span<const std::byte> frame) override { return link_.send(to, frame); }
    std::size_t poll(std::deque<std::vector<std::byte>>& out) override { return link_.poll(out); }

private:
    AeronLink& link_;
};

}

int main(int argc, char** argv) {
    if (argc != 3 || std::string(argv[1]) != "--config") {
        std::cerr << "usage: velocita-tape-voter --config FILE\n";
        return 64;
    }
    try {
        const auto kv = KvConfig::load(argv[2]);
        VoterConfig c;
        c.replica.node_id = static_cast<std::int32_t>(kv.number("node_id"));
        if (c.replica.node_id < 1 || c.replica.node_id > 3) throw std::runtime_error("node_id must be 1, 2 or 3");
        c.replica.members = {1, 2, 3};
        c.replica.data_dir = kv.get("data_dir");
        c.replica.election_timeout_ms = static_cast<int>(kv.number("election_timeout_ms", 3000));
        c.replica.request_timeout_ms = static_cast<int>(kv.number("request_timeout_ms", 200));
        c.replica.tape.verdict_ticks = kv.number("verdict_ticks", 5);
        c.replica.tape.head_every_ticks = kv.number("head_every_ticks", 60);
        c.incarnation = kv.number("incarnation");
        c.master = load_master_key(kv.get("master_key_file"));
        c.boot = random_u64();
        c.signing_key = kv.get("signing_key_file");
        for (int v = 1; v <= 3; ++v) c.public_keys[v - 1] = kv.get("public_key." + std::to_string(v));
        if (const auto previous = kv.find("previous_head_file")) {
            const auto head = read_head_file(*previous);
            std::array<std::optional<VerifyKey>, 3> keys;
            for (int v = 0; v < 3; ++v) keys[v] = VerifyKey::load(c.public_keys[v]);
            if (head.incarnation == 0 || head.incarnation == c.incarnation || valid_head_signatures(head, keys) < 2)
                throw std::runtime_error("previous head " + *previous + " is not a signed head of an earlier segment");
            c.previous_segment = SegmentStart{head.incarnation, head.tick, head.tree_size, head.root};
        }
        c.tick_ms = kv.number("tick_ms", 1000);

        std::map<Party, Endpoint> destinations;
        for (std::uint8_t v = 1; v <= 3; ++v)
            if (v != c.replica.node_id)
                destinations[{Role::voter, v}] = Endpoint::parse(kv.get("peer." + std::to_string(v)));
        for (std::uint8_t r = 1; r <= 3; ++r)
            if (const auto e = kv.find("recorder." + std::to_string(r))) destinations[{Role::recorder, r}] = Endpoint::parse(*e);

        unsigned seed = static_cast<unsigned>(random_u64());
        std::srand(seed);
        std::signal(SIGTERM, stop);
        std::signal(SIGINT, stop);

        AeronRuntime runtime(kv.get("aeron_dir"));
        AeronLink link(runtime, Endpoint::parse(kv.get("listen")), destinations);
        LinkNetwork network(link);
        Voter voter(c, network);
        const auto labels = "node=\"" + std::to_string(c.replica.node_id) + "\",process=\"voter\"";
        voter.set_extra_metrics([&runtime, &link, labels] {
            return aeron_metrics(runtime, labels) + "velocita_tape_link_dropped_total{" + labels + "} " +
                   std::to_string(link.dropped()) + "\n";
        });
        std::cerr << "velocita-tape-voter " << c.replica.node_id << " incarnation " << c.incarnation << " started\n";
        const auto start = std::chrono::steady_clock::now();
        while (running) {
            const auto now = std::chrono::steady_clock::now();
            const auto ms = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
            const auto utc = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
            voter.step(ms, static_cast<std::int64_t>(utc));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::cerr << "velocita-tape-voter stopping\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "velocita-tape-voter: " << e.what() << "\n";
        return 70;
    }
}
