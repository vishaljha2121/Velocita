// A tape voter: replica, authenticated network, in-order proposals, ticks, head signing and publishing.
#pragma once

#include "velocita/tape/auth.hpp"
#include "velocita/tape/replica.hpp"
#include "velocita/tape/signing.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace velocita::tape {

class Network {
public:
    virtual ~Network() = default;
    virtual bool send(const Party& to, std::span<const std::byte> frame) = 0;
    virtual std::size_t poll(std::deque<std::vector<std::byte>>& out) = 0;
};

struct VoterConfig {
    ReplicaConfig replica;
    std::uint64_t incarnation{};
    Key master{};
    std::uint64_t boot{};
    std::filesystem::path signing_key;
    std::array<std::filesystem::path, kVoters> public_keys;
    std::uint64_t tick_ms{1000};
    std::uint64_t ack_ms{200};
    std::uint64_t state_page_ms{1000};
    std::size_t pending_per_recorder{8192};
};

struct HeadRecord {
    std::uint64_t tick{};
    std::uint64_t tree_size{};
    Digest root;
    std::array<bool, kVoters> signed_by{};
};

class Voter {
public:
    Voter(VoterConfig config, Network& network);
    void step(std::uint64_t now_ms, std::int64_t utc_ns);
    [[nodiscard]] TapeReplica& replica() noexcept { return *replica_; }
    [[nodiscard]] const std::vector<HeadRecord>& published_heads() const noexcept { return published_; }
    [[nodiscard]] std::uint64_t refused_submissions() const noexcept { return refused_; }
    [[nodiscard]] std::uint64_t opened_frames() const noexcept { return opened_; }
    [[nodiscard]] std::uint64_t rejected_frames() const noexcept { return rejected_; }

private:
    void handle_submission(const Party& from, std::vector<std::byte> bytes);
    void drain_pending();
    void on_applied(std::uint64_t index, std::span<const std::byte> bytes, const ApplyResult& result);
    void after_apply();
    void send_acks();
    void write_state_page(std::int64_t utc_ns) const;
    void broadcast_submission(std::span<const std::byte> bytes);
    void publish_head(std::uint64_t tick);

    VoterConfig config_;
    Network& network_;
    Party self_;
    Sealer sealer_;
    std::unique_ptr<TapeReplica> replica_;
    std::optional<SigningKey> signing_;
    std::array<std::optional<VerifyKey>, kVoters> verify_;
    std::array<std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<std::byte>>, kRecorders> pending_;
    std::map<std::pair<std::uint64_t, std::uint8_t>, std::vector<std::byte>> pending_heads_;
    std::vector<std::uint64_t> heads_to_sign_;
    std::vector<std::uint64_t> heads_to_check_;
    std::set<std::uint64_t> published_ticks_;
    std::vector<HeadRecord> published_;
    std::uint64_t last_tick_ms_{};
    std::uint64_t last_ack_ms_{};
    std::uint64_t last_page_ms_{};
    std::uint64_t last_step_ms_{};
    bool started_{};
    std::int64_t utc_ns_{};
    std::uint64_t refused_{};
    std::uint64_t opened_{};
    std::uint64_t rejected_{};
};

[[nodiscard]] std::vector<std::byte> encode_ack(std::uint8_t recorder, std::uint64_t session, std::uint64_t applied);
[[nodiscard]] bool decode_ack(std::span<const std::byte> bytes, std::uint8_t& recorder, std::uint64_t& session,
                              std::uint64_t& applied);

}
