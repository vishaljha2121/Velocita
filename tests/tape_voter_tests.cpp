// Three tape voters and simulated recorders on an in-memory network: ordering, acknowledgements, ticks and heads.
#include "velocita/tape/voter.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>

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

struct Bus;

struct Endpoint final : Network {
    Bus* bus;
    Party self;
    std::deque<std::vector<std::byte>> inbox;
    Endpoint(Bus* b, Party p) : bus(b), self(p) {}
    bool send(const Party& to, std::span<const std::byte> frame) override;
    std::size_t poll(std::deque<std::vector<std::byte>>& out) override {
        const auto n = inbox.size();
        while (!inbox.empty()) {
            out.push_back(std::move(inbox.front()));
            inbox.pop_front();
        }
        return n;
    }
};

struct Bus {
    std::map<Party, Endpoint*> endpoints;
    std::set<Party> down;
};

bool Endpoint::send(const Party& to, std::span<const std::byte> frame) {
    if (bus->down.contains(self) || bus->down.contains(to)) return false;
    const auto it = bus->endpoints.find(to);
    if (it == bus->endpoints.end()) return false;
    it->second->inbox.emplace_back(frame.begin(), frame.end());
    return true;
}

Key master_key() {
    Key k{};
    for (std::size_t i = 0; i < k.size(); ++i) k[i] = std::byte(i * 7 + 3);
    return k;
}

constexpr std::uint64_t kIncarnation = 4242;

std::vector<std::byte> entry_bytes(const Entry& e) {
    std::vector<std::byte> out(kMaxEntryBytes + 64);
    out.resize(encode_entry(e, out));
    return out;
}

std::vector<std::byte> depth_update(std::uint8_t r, std::uint64_t session, std::uint64_t request, std::uint64_t seq) {
    const std::string raw = R"({"seqnum":")" + std::to_string(seq) +
                            R"(","channel":"DEPTH","update":"UPDATE","data":{"symbol":"NVDAC-USDC","bids":[{"price":"1.5","qty":"1","order_count":"1"}],"asks":[]}})";
    Observation o;
    o.header = {r, session, request};
    o.channel = Channel::depth;
    o.kind = UpdateKind::update;
    o.symbol = *Symbol::from("NVDAC-USDC");
    o.has_seqnum = true;
    o.seqnum = seq;
    o.payload = std::as_bytes(std::span(raw.data(), raw.size()));
    o.payload_hash = v2::sha256_digest(o.payload);
    o.utc_recv_ns = 1;
    return entry_bytes(o);
}

std::vector<std::byte> session_start(std::uint8_t r, std::uint64_t session) {
    RecorderEvent e;
    e.header = {r, session, 1};
    e.kind = EventKind::session_start;
    return entry_bytes(e);
}

struct Harness {
    std::filesystem::path root = std::filesystem::temp_directory_path() / "tape-voter-tests";
    Bus bus;
    std::vector<std::unique_ptr<Endpoint>> endpoints;
    std::vector<std::unique_ptr<Voter>> voters;
    std::vector<std::unique_ptr<Sealer>> recorders;
    std::uint64_t now = 0;

    Harness() {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "keys");
        for (int v = 1; v <= 3; ++v) {
            auto key = SigningKey::generate();
            key.save_private(root / "keys" / ("head-" + std::to_string(v) + ".pem"));
            key.save_public(root / "keys" / ("head-" + std::to_string(v) + ".pub.pem"));
        }
        for (std::uint8_t v = 1; v <= 3; ++v) {
            endpoints.push_back(std::make_unique<Endpoint>(&bus, Party{Role::voter, v}));
            bus.endpoints[Party{Role::voter, v}] = endpoints.back().get();
        }
        for (std::uint8_t r = 1; r <= 3; ++r) {
            endpoints.push_back(std::make_unique<Endpoint>(&bus, Party{Role::recorder, r}));
            bus.endpoints[Party{Role::recorder, r}] = endpoints.back().get();
            recorders.push_back(std::make_unique<Sealer>(master_key(), kIncarnation, Party{Role::recorder, r}, 900 + r));
        }
        for (std::uint8_t v = 1; v <= 3; ++v) {
            VoterConfig c;
            c.replica.node_id = v;
            c.replica.members = {1, 2, 3};
            c.replica.data_dir = root / ("voter-" + std::to_string(v));
            c.replica.tape.head_every_ticks = 3;
            c.incarnation = kIncarnation;
            c.master = master_key();
            c.boot = 100 + v;
            c.signing_key = root / "keys" / ("head-" + std::to_string(v) + ".pem");
            for (int k = 0; k < 3; ++k) c.public_keys[k] = root / "keys" / ("head-" + std::to_string(k + 1) + ".pub.pem");
            c.tick_ms = 100;
            voters.push_back(std::make_unique<Voter>(c, *endpoints[v - 1]));
        }
    }
    ~Harness() { std::filesystem::remove_all(root); }

    void run(int ms) {
        for (int t = 0; t < ms; t += 5) {
            now += 5;
            for (auto& v : voters) v->step(now, static_cast<std::int64_t>(now) * 1'000'000);
        }
    }
    void submit_all(std::uint8_t r, const std::vector<std::byte>& entry) {
        auto& sealer = *recorders[r - 1];
        for (std::uint8_t v = 1; v <= 3; ++v) {
            const Party to{Role::voter, v};
            bus.endpoints[to]->inbox.push_back(sealer.seal_to(to, FrameKind::submit, entry));
        }
    }
    int leader() {
        for (std::size_t i = 0; i < voters.size(); ++i)
            if (voters[i]->replica().is_leader()) return static_cast<int>(i);
        return -1;
    }
};

void test_ordering_acks_ticks_and_heads() {
    Harness h;
    h.run(3000);
    check(h.leader() >= 0, "a leader is elected");
    for (std::uint8_t r = 1; r <= 3; ++r) h.submit_all(r, session_start(r, 7000 + r));
    std::vector<std::uint64_t> order;
    for (std::uint64_t q = 2; q <= 30; ++q) order.push_back(q);
    std::mt19937 rng(3);
    std::shuffle(order.begin(), order.end(), rng);
    for (const auto q : order) {
        h.submit_all(1, depth_update(1, 7001, q, 100 + q));
        if (q % 4 == 0) h.submit_all(1, depth_update(1, 7001, q, 100 + q));
        h.run(20);
    }
    h.run(3000);
    const auto& s = h.voters[0]->replica().state();
    check(s.sessions[0].last_request == 30, "all 29 requests committed in order, once");
    check(s.counters.apply_codes[static_cast<std::size_t>(ApplyCode::duplicate_request)] == 0 &&
              s.counters.apply_codes[static_cast<std::size_t>(ApplyCode::out_of_order_request)] == 0,
          "the leader never proposed a duplicate or out-of-order request");
    check(s.last_tick >= 20, "ticks advance");
    bool acked = false;
    auto& rec1 = *h.endpoints[3];
    std::deque<std::vector<std::byte>> frames;
    rec1.poll(frames);
    for (auto& f : frames) {
        const auto o = h.recorders[0]->open(f);
        std::uint8_t r{};
        std::uint64_t session{}, applied{};
        if (o && o->kind == FrameKind::ack && decode_ack(o->payload, r, session, applied))
            acked = acked || (r == 1 && session == 7001 && applied == 30);
    }
    check(acked, "recorder 1 is acknowledged through request 30");
    std::size_t with_heads = 0;
    for (auto& v : h.voters) with_heads += v->published_heads().empty() ? 0 : 1;
    check(with_heads == 3, "every voter published signed tree heads");
    for (auto& v : h.voters) {
        if (v->published_heads().empty()) continue;
        const auto& head = v->published_heads().front();
        std::size_t sigs = 0;
        for (const auto b : head.signed_by) sigs += b ? 1 : 0;
        check(sigs >= 2, "a published head carries at least two valid signatures");
    }
    h.run(1000);
    for (std::size_t i = 0; i < h.voters.size(); ++i) {
        const auto& heads = h.voters[i]->published_heads();
        if (heads.empty()) continue;
        const auto& head = heads.front();
        std::size_t sigs = 0;
        for (const auto b : head.signed_by) sigs += b ? 1 : 0;
        std::ifstream in(h.root / ("voter-" + std::to_string(i + 1)) / "heads" / ("head-" + std::to_string(head.tick) + ".json"));
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        const bool file_has_all = text.find("\"1\":\"") != std::string::npos && text.find("\"2\":\"") != std::string::npos &&
                                  text.find("\"3\":\"") != std::string::npos;
        check(sigs == 3 && file_has_all, "a published head gains the third signature once it lands");
    }
    const auto st = h.voters[0]->replica().status();
    bool same = true;
    for (auto& v : h.voters) {
        const auto& s2 = v->replica().state();
        same = same && s2.applied_index >= st.commit_index - 2;
    }
    check(same, "every voter applied the committed history");
}

void test_refusals() {
    Harness h;
    h.run(3000);
    h.submit_all(1, session_start(1, 8001));
    h.run(500);
    Tick t;
    t.number = h.voters[0]->replica().state().last_tick + 1;
    t.proposer = 1;
    h.submit_all(1, entry_bytes(t));
    h.submit_all(2, depth_update(1, 8001, 2, 5));
    std::uint64_t refused_before = 0;
    for (auto& v : h.voters) refused_before += v->refused_submissions();
    check(refused_before == 0, "nothing refused before the bad submissions are processed");
    h.run(200);
    std::uint64_t refused = 0;
    for (auto& v : h.voters) refused += v->refused_submissions();
    check(refused == 6, "a recorder's tick and a recorder impersonating another are refused by every voter");
    Sealer wrong(Key{}, kIncarnation, Party{Role::recorder, 1}, 1);
    const Party to{Role::voter, 1};
    h.bus.endpoints[to]->inbox.push_back(wrong.seal_to(to, FrameKind::submit, session_start(1, 1)));
    const auto rejected_before = h.voters[0]->rejected_frames();
    h.run(20);
    check(h.voters[0]->rejected_frames() == rejected_before + 1, "a frame sealed with the wrong key is rejected");
    check(h.voters[0]->replica().state().sessions[0].session == 8001, "the forged session never reached the log");
}

}

int main() {
    std::srand(99);
    test_ordering_acks_ticks_and_heads();
    test_refusals();
    if (failures) {
        std::cerr << failures << " failures\n";
        return 1;
    }
    std::cout << "tape voter tests passed\n";
    return 0;
}
