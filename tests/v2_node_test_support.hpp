#pragma once
#include "velocita/v2/node.hpp"
#include "v2_catalog_test_support.hpp"
#include "v2_identity_test_support.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <thread>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using namespace velocita;
using namespace velocita::v2;
using Bytes = std::vector<std::byte>;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::int32_t kCallerKey = 9;
constexpr std::uint64_t kCallerId = 7;
const std::array<std::byte, 16> kIncarnation{std::byte{0xAB}, std::byte{0x5A}};

Bytes secret_bytes(std::uint8_t seed) {
    Bytes out(kAuthSecretBytes);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<std::byte>(seed + i);
    return out;
}
AuthSecret secret(std::uint8_t seed) { return std::move(*AuthSecret::from_bytes(secret_bytes(seed))); }
AuthKey key(std::uint8_t seed, AuthRole role, std::int32_t sender, std::int32_t recipient) {
    return std::move(*AuthKey::derive(secret(seed), {role, sender, recipient}, kIncarnation));
}
constexpr std::uint8_t kLinkSeed = 0x10, kCallerSeed = 0x60, kWrongSeed = 0x90;
constexpr std::uint8_t kNewLinkSeed = 0x30, kNewCallerSeed = 0x70;

Digest256 digest(std::uint64_t marker) {
    Digest256 value{};
    value.bytes[0] = static_cast<std::byte>(marker);
    value.bytes[1] = std::byte{0x77};
    return value;
}
velocita::v2::Command authored(std::uint64_t sequence, std::uint64_t caller = kCallerId) {
    return {{CallerId{caller}, RequestSequence{sequence}, digest(sequence)}, AdvanceKillEpoch{KillEpoch{sequence}}};
}
Bytes encoded(const velocita::v2::Command& command) {
    std::array<std::byte, kMaxKernelCommandBytes> storage{};
    const auto result = encode_kernel_command(command, storage);
    require(result.error == KernelCodecError::none, "fixture command did not encode");
    return {storage.begin(), storage.begin() + static_cast<std::ptrdiff_t>(result.bytes_written)};
}

struct Network;

struct FakeTransport final : NodeTransport {
    Network* network{};
    std::int32_t self{};
    std::deque<Bytes> peer_inbox, ingress_inbox;
    bool block_offers{};
    std::vector<Bytes> refused;

    std::optional<std::size_t> receive(Channel channel, std::span<std::byte> buffer) override {
        auto& inbox = channel == Channel::peer ? peer_inbox : ingress_inbox;
        if (inbox.empty()) return std::nullopt;
        auto frame = std::move(inbox.front());
        inbox.pop_front();
        require(frame.size() <= buffer.size(), "fixture frame exceeds node buffer");
        std::copy(frame.begin(), frame.end(), buffer.begin());
        return frame.size();
    }
    bool offer_peer(std::int32_t voter, std::span<const std::byte> envelope) override;
    bool offer_response(std::int32_t caller, std::span<const std::byte> envelope) override;
};

struct Network {
    std::map<std::int32_t, FakeTransport> transports;
    std::deque<Bytes> responses;
    std::vector<std::pair<std::int32_t, Bytes>> captured;
    bool partitioned{};
    std::set<std::int32_t> down;

    FakeTransport& at(std::int32_t id) {
        auto& transport = transports[id];
        transport.network = this;
        transport.self = id;
        return transport;
    }
};

bool FakeTransport::offer_peer(std::int32_t voter, std::span<const std::byte> envelope) {
    if (block_offers) {
        refused.emplace_back(envelope.begin(), envelope.end());
        return false;
    }
    if (network->down.contains(voter)) {
        refused.emplace_back(envelope.begin(), envelope.end());
        return false;
    }
    network->captured.emplace_back(voter, Bytes(envelope.begin(), envelope.end()));
    if (!network->partitioned) network->at(voter).peer_inbox.emplace_back(envelope.begin(), envelope.end());
    return true;
}
bool FakeTransport::offer_response(std::int32_t caller, std::span<const std::byte> envelope) {
    require(caller == kCallerKey, "response addressed to an unconfigured caller");
    network->responses.emplace_back(envelope.begin(), envelope.end());
    return true;
}

const std::vector<std::int32_t> kTwo{1, 2}, kThree{1, 2, 3};

NodeConfig config_for(const std::filesystem::path& base, std::int32_t id, NodeStartup startup,
                      const std::vector<std::int32_t>& voters, std::uint32_t compact_at_entries = 2048) {
    NodeConfig config;
    config.node_id = id;
    config.data_root = base / std::to_string(id);
    config.startup = startup;
    config.incarnation = kIncarnation;
    config.catalog_root = catalog_test::standard_catalog().root();
    config.catalog_file = "/unused-by-node";
    config.deployment_mode = DeploymentMode::laboratory;
    for (const std::int32_t voter : voters)
        config.voters.push_back({voter, {"127.0.0.1", static_cast<std::uint16_t>(40200 + voter)},
                                 voter == id ? std::nullopt : std::optional<std::filesystem::path>("/k")});
    config.ingress_endpoint = {"127.0.0.1", static_cast<std::uint16_t>(40300 + id)};
    config.callers.push_back({kCallerKey, kCallerId, "/c", {"127.0.0.1", 40400}});
    config.max_history_wal_bytes = 1ULL << 30;
    config.compact_at_entries = compact_at_entries;
    config.digest = digest(id);
    return config;
}

std::uint8_t link_seed(std::int32_t a, std::int32_t b) {
    return static_cast<std::uint8_t>(kLinkSeed + 4 * (a + b - 3));
}
NodeSecrets secrets_for(std::int32_t id, const std::vector<std::int32_t>& voters) {
    NodeSecrets secrets;
    for (const auto peer : voters)
        if (peer != id) secrets.voters.emplace(peer, SecretSet{secret(link_seed(id, peer))});
    secrets.callers.emplace(kCallerKey, SecretSet{secret(kCallerSeed)});
    return secrets;
}

struct Caller {
    SessionSequencer sequencer{1};
    std::map<std::int32_t, SessionFence> fences;
    std::uint8_t send_seed = kCallerSeed;
    std::vector<std::uint8_t> accept_seeds{kCallerSeed};

    Bytes ingress(std::int32_t node, const velocita::v2::Command& command, ProposeLane lane = ProposeLane::normal) {
        const auto bytes = encoded(command);
        std::array<std::byte, kMaxIngressBodyBytes> body{};
        const auto framed = encode_ingress(lane, bytes, body);
        require(framed.error == NodeProtocolError::none, "fixture ingress did not encode");
        Bytes out(kMaxAuthEnvelopeBytes);
        const auto env = encode_auth_envelope(key(send_seed, AuthRole::ingress, kCallerKey, node), 1,
                                              *sequencer.next(), std::span(body).first(framed.bytes_written), out);
        require(env.error == AuthEnvelopeError::none, "fixture ingress envelope did not encode");
        out.resize(env.bytes_written);
        return out;
    }
    std::optional<std::pair<std::int32_t, NodeResponse>> open(const Bytes& frame) {
        AuthRoute route;
        if (peek_auth_route(frame, route) != AuthEnvelopeError::none || route.role != AuthRole::response)
            return std::nullopt;
        VerifiedAuthEnvelope verified;
        bool authentic = false;
        for (const auto seed : accept_seeds)
            authentic = authentic || verify_auth_envelope(key(seed, AuthRole::response, route.sender, kCallerKey),
                                                          frame, verified) == AuthEnvelopeError::none;
        if (!authentic ||
            fences[route.sender].admit(verified.header.session_epoch, verified.header.sequence) !=
                SessionFence::Decision::accept)
            return std::nullopt;
        NodeResponse response;
        require(decode_response(verified.body, response) == NodeProtocolError::none, "node sent a malformed response");
        return std::pair{route.sender, response};
    }
};

struct Gate {
    std::atomic<bool> armed{false}, parked{false}, released{false};
    static void observe(MetadataCheckpoint checkpoint, void* context) {
        auto& gate = *static_cast<Gate*>(context);
        if (checkpoint != MetadataCheckpoint::before_write || !gate.armed.exchange(false)) return;
        gate.parked = true;
        while (!gate.released) std::this_thread::yield();
    }
};

struct Cluster {
    std::filesystem::path base;
    Network network;
    std::vector<std::int32_t> voters;
    std::map<std::int32_t, std::unique_ptr<V2Node>> nodes;
    std::map<std::int32_t, std::uint64_t> epochs;
    Caller caller;
    std::vector<std::pair<std::int32_t, NodeResponse>> received;
    std::map<std::int32_t, std::unique_ptr<Gate>> gates;
    bool deliver_responses_to_caller = true;
    std::uint32_t compact_at_entries = 2048;

    explicit Cluster(std::filesystem::path root, std::vector<std::int32_t> ids = kTwo)
        : base(std::move(root)), voters(std::move(ids)) {
        for (const auto id : voters) {
            epochs[id] = 1;
            start(id, NodeStartup::bootstrap);
        }
    }
    ~Cluster() {
        for (auto& [id, gate] : gates) gate->released = true;
        nodes.clear();
    }
    void start(std::int32_t id, NodeStartup startup) {
        if (const auto old = gates.find(id); old != gates.end()) old->second->released = true;
        nodes.erase(id);
        gates[id] = std::make_unique<Gate>();
        nodes[id] = std::make_unique<V2Node>(config_for(base, id, startup, voters, compact_at_entries),
                                             catalog_test::standard_catalog(), secrets_for(id, voters),
                                             epochs[id], network.at(id),
                                             MetadataCheckpoints{&Gate::observe, gates[id].get()});
    }
    V2Node& node(std::int32_t id) { return *nodes.at(id); }
    void stop(std::int32_t id) {
        if (const auto gate = gates.find(id); gate != gates.end()) gate->second->released = true;
        nodes.erase(id);
        network.down.insert(id);
        network.at(id).peer_inbox.clear();
        network.at(id).ingress_inbox.clear();
    }
    void restart(std::int32_t id) {
        network.down.erase(id);
        ++epochs[id];
        start(id, NodeStartup::recover);
    }

    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    void run(int rounds) {
        for (int round = 0; round < rounds; ++round) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count());
            if (elapsed > 0) last = now;
            for (auto& [id, node] : nodes) node->turn(elapsed);
            while (deliver_responses_to_caller && !network.responses.empty()) {
                auto frame = std::move(network.responses.front());
                network.responses.pop_front();
                if (auto opened = caller.open(frame)) received.push_back(*opened);
            }
        }
    }
    template <typename Predicate>
    void run_until(Predicate predicate, const std::string& what, int limit = 30000) {
        for (int round = 0; round < limit; ++round) {
            if (predicate()) return;
            run(1);
        }
        throw std::runtime_error("timed out waiting for " + what);
    }
    std::int32_t leader() {
        for (auto& [id, node] : nodes)
            if (node->status().replica.role == "leader" && node->status().ready) return id;
        return 0;
    }
    std::int32_t elect() {
        run_until([&] { return leader() != 0; }, "a ready leader");
        return leader();
    }
    std::uint64_t busy_holds() {
        std::uint64_t total = 0;
        for (auto& [id, node] : nodes) {
            const auto status = node->status();
            const auto& c = status.counters;
            require(c.peer_frames_fenced == c.peer_frames_accepted + c.peer_frame_errors + status.held_peer_frames,
                    "node " + std::to_string(id) + " lost an authenticated peer frame");
            total += c.peer_busy_holds;
        }
        return total;
    }
    std::optional<NodeResponse> response_for(std::uint64_t sequence, ResponseKind kind) const {
        for (const auto& [from, response] : received)
            if (response.kind == kind && response.correlation.request_sequence == sequence) return response;
        return std::nullopt;
    }
};

std::filesystem::path fresh_base(const char* leaf) {
    auto pattern = (std::filesystem::temp_directory_path() / (std::string(leaf) + "-XXXXXX")).string();
    std::vector<char> bytes(pattern.begin(), pattern.end());
    bytes.push_back('\0');
    require(::mkdtemp(bytes.data()) != nullptr, "mkdtemp failed");
    return std::filesystem::canonical(bytes.data());
}

struct Base {
    std::filesystem::path path;
    explicit Base(const char* leaf) : path(fresh_base(leaf)) {}
    ~Base() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

std::array<std::byte, kKernelResultBytes> expected_result(std::uint64_t through_sequence) {
    TradingState state{};
    ApplyScratch scratch{};
    ApplyResult last{};
    for (std::uint64_t s = 1; s <= through_sequence; ++s)
        last = apply(state, authored(s), scratch, &catalog_test::standard_catalog());
    std::array<std::byte, kKernelResultBytes> bytes{};
    require(encode_kernel_result(last, bytes).error == KernelCodecError::none, "expected result did not encode");
    return bytes;
}

}
