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

// All spans are borrowed only for the duration of encode_bound_peer.
struct PeerMessageView {
    std::int32_t sender_id{};
    std::int32_t recipient_id{};
    BorrowedPeerPayload payload;
};

// V2 peer bodies are the V1 layout with version 3 and a trailer: for
// AppendEntries 8 bytes carrying the leader's compaction floor, for RequestVote
// and its reply 1 byte carrying the pre-vote flag. The V1 codec itself is
// unchanged. Any other version, V1 or the version 2 that predates pre-vote, is
// refused: a vote without the flag would be read as a real one.
inline constexpr std::uint16_t kBoundPeerBodyVersion = 3;

// Canonical V2 body without semantic validation: fixtures use it to reach the
// decoder with malformed content.
[[nodiscard]] std::vector<std::byte> encode_bound_peer_body(const PeerMessage& message);

// Checks incoming receiver identity/route; output changes only on success.
// Bounded allocations follow length/count preflight. This is not authentication.
[[nodiscard]] BoundFrameError decode_bound_peer(
    std::span<const std::byte> input, const VerifiedStaticIdentity& receiver,
    PeerMessage& destination, BoundFrameScratch& scratch);

// Outgoing route must originate at the supplied verified local identity.
[[nodiscard]] BoundFrameEncodeResult encode_bound_peer(
    const PeerMessage& message, const VerifiedStaticIdentity& sender,
    std::span<std::byte> output, BoundFrameScratch& scratch);

// Allocation-free canonical encoder for bounded borrowed AppendEntries payloads.
[[nodiscard]] BoundFrameEncodeResult encode_bound_peer(
    const PeerMessageView& message, const VerifiedStaticIdentity& sender,
    std::span<std::byte> output, BoundFrameScratch& scratch) noexcept;

} // namespace velocita::v2
