// Raft peer messages bound to the static identity.
#pragma once

#include "velocita/v2/bound_frame.hpp"
#include "velocita/wire.hpp"

#include <variant>

namespace velocita::v2 {

struct BorrowedPeerLogEntry {
    std::int64_t term{};
    std::int32_t id{};
    std::int32_t type{};
    std::span<const std::byte> payload;
};

struct BorrowedAppendEntries {
    std::int64_t term{};
    std::int64_t previous_log_index{};
    std::int64_t previous_log_term{};
    std::int64_t leader_commit{};
    std::span<const BorrowedPeerLogEntry> entries;
    std::int64_t compaction_floor{};
};

using BorrowedPeerPayload = std::variant<RequestVote, RequestVoteResponse,
    BorrowedAppendEntries, AppendEntriesResponse>;

struct PeerMessageView {
    std::int32_t sender_id{};
    std::int32_t recipient_id{};
    BorrowedPeerPayload payload;
};

inline constexpr std::uint16_t kBoundPeerBodyVersion = 3;

[[nodiscard]] std::vector<std::byte> encode_bound_peer_body(const PeerMessage& message);

[[nodiscard]] BoundFrameError decode_bound_peer(
    std::span<const std::byte> input, const VerifiedStaticIdentity& receiver,
    PeerMessage& destination, BoundFrameScratch& scratch);

[[nodiscard]] BoundFrameEncodeResult encode_bound_peer(
    const PeerMessage& message, const VerifiedStaticIdentity& sender,
    std::span<std::byte> output, BoundFrameScratch& scratch);

[[nodiscard]] BoundFrameEncodeResult encode_bound_peer(
    const PeerMessageView& message, const VerifiedStaticIdentity& sender,
    std::span<std::byte> output, BoundFrameScratch& scratch) noexcept;

}
