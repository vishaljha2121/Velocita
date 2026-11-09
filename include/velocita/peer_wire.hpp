// Raft peer message encoding.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace velocita {

inline constexpr std::uint16_t kPeerWireVersion = 1;

enum class PeerMessageType : std::uint16_t {
    request_vote = 1,
    request_vote_response = 2,
    append_entries = 3,
    append_entries_response = 4,
};

struct PeerLogEntry {
    std::int64_t term{};
    std::int32_t id{};
    std::int32_t type{};
    std::vector<std::byte> payload;
};

struct RequestVote {
    std::int64_t term{};
    std::int32_t candidate_id{};
    std::int64_t last_log_index{};
    std::int64_t last_log_term{};
    bool prevote{};
};

struct RequestVoteResponse {
    std::int64_t term{};
    bool vote_granted{};
    bool prevote{};
};

struct AppendEntries {
    std::int64_t term{};
    std::int64_t previous_log_index{};
    std::int64_t previous_log_term{};
    std::int64_t leader_commit{};
    std::vector<PeerLogEntry> entries;
    std::int64_t compaction_floor{};
};

struct AppendEntriesResponse {
    std::int64_t term{};
    bool success{};
    std::int64_t current_index{};
    std::int64_t first_index{};
};

using PeerPayload = std::variant<RequestVote, RequestVoteResponse, AppendEntries, AppendEntriesResponse>;

struct PeerMessage {
    std::int32_t sender_id{};
    std::int32_t recipient_id{};
    PeerPayload payload;
};

std::vector<std::byte> encode_peer_message(const PeerMessage& message);
PeerMessage decode_peer_message(std::span<const std::byte> bytes);

}
