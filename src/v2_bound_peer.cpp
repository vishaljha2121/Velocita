#include "velocita/v2/bound_peer.hpp"
#include "velocita/v2/authority_barrier.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace velocita::v2 {
namespace {
using Error = BoundFrameError;

std::uint32_t u32(std::span<const std::byte> bytes, std::size_t at) noexcept {
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i)
        value |= std::uint32_t(std::to_integer<unsigned>(bytes[at+i])) << (8*i);
    return value;
}

// Establish a complete bounded shape BEFORE the shared V1 decoder can reserve or copy.
Error preflight(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < 16 || u32(bytes, 0) != 0x31524550U ||
        bytes[4] != std::byte{kBoundPeerBodyVersion & 0xFFU} ||
        bytes[5] != std::byte{kBoundPeerBodyVersion >> 8U}) return Error::invalid_payload;
    const auto kind = std::to_integer<unsigned>(bytes[6]) |
        (std::to_integer<unsigned>(bytes[7]) << 8);
    // Vote bodies end in the pre-vote flag, canonical 0 or 1.
    if (kind == 1 || kind == 2)
        return bytes.size() == (kind == 1 ? 49U : 26U) &&
                std::to_integer<unsigned>(bytes.back()) <= 1 ? Error::none : Error::invalid_payload;
    if (kind == 4) return bytes.size() == 41 ? Error::none : Error::invalid_payload;
    if (kind != 3 || bytes.size() < 52) return Error::invalid_payload;
    const auto count = u32(bytes, 48);
    if (count > kMaxBoundPeerEntries) return Error::capacity_exceeded;
    std::size_t offset = 52;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (bytes.size() - offset < 20) return Error::invalid_payload;
        const auto length = u32(bytes, offset + 16);
        if (length > kMaxKernelCommandBytes) return Error::capacity_exceeded;
        offset += 20;
        if (length > bytes.size() - offset) return Error::invalid_payload;
        offset += length;
    }
    // The V2 trailer: the leader's compaction floor.
    return bytes.size() - offset == 8 ? Error::none : Error::invalid_payload;
}

bool member(const StaticIdentity& identity, std::int32_t id) noexcept {
    return std::find(identity.voters.begin(), identity.voters.begin() + identity.voter_count, id) !=
        identity.voters.begin() + identity.voter_count;
}

template<typename T>
void put(std::span<std::byte> out, std::size_t& offset, T value) noexcept {
    using U = std::make_unsigned_t<T>;
    const auto bits = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i)
        out[offset++] = static_cast<std::byte>(bits >> (8U * i));
}

Error validate(const PeerMessageView& message, const VerifiedStaticIdentity& identity) noexcept {
    if (!identity.valid()) return Error::invalid_identity;
    const auto& value = identity.value();
    if (message.sender_id == message.recipient_id || !member(value, message.sender_id) ||
        !member(value, message.recipient_id) || message.sender_id != value.local_node_id)
        return Error::invalid_route;
    if (const auto* vote = std::get_if<RequestVote>(&message.payload)) {
        if (vote->candidate_id != message.sender_id) return Error::invalid_route;
        if (vote->term < 0 || vote->last_log_index < 0 || vote->last_log_term < 0 ||
            vote->last_log_index == std::numeric_limits<std::int64_t>::max())
            return Error::invalid_payload;
    } else if (const auto* vote = std::get_if<RequestVoteResponse>(&message.payload)) {
        if (vote->term < 0) return Error::invalid_payload;
    } else if (const auto* append = std::get_if<BorrowedAppendEntries>(&message.payload)) {
        if (append->entries.size() > kMaxBoundPeerEntries) return Error::capacity_exceeded;
        const auto count = static_cast<std::int64_t>(append->entries.size());
        if (append->term < 0 || append->previous_log_index < 0 || append->previous_log_term < 0 ||
            append->leader_commit < 0 || append->compaction_floor < 0 || append->previous_log_index >
                std::numeric_limits<std::int64_t>::max() - count - 1)
            return Error::invalid_payload;
        KernelCommandCodecScratch codec_scratch;
        for (const auto& entry : append->entries) {
            if (entry.term <= 0) return Error::invalid_payload;
            if (entry.payload.size() > kMaxKernelCommandBytes) return Error::capacity_exceeded;
            if (entry.type == kAuthorityBarrierEntryType) {
                AuthorityBarrier marker;
                if (decode_authority_barrier(entry.type, entry.payload, marker).error !=
                        AuthorityBarrierCodecError::none ||
                    marker.authority_domain != identity.authority_domain())
                    return Error::invalid_payload;
            } else {
                Command command;
                if (entry.type != 0 || decode_kernel_command(entry.payload, command, codec_scratch).error !=
                        KernelCodecError::none)
                    return Error::invalid_payload;
            }
        }
    } else {
        const auto& response = std::get<AppendEntriesResponse>(message.payload);
        if (response.term < 0 || response.current_index < 0 ||
            response.current_index == std::numeric_limits<std::int64_t>::max() || response.first_index < 1)
            return Error::invalid_payload;
    }
    return Error::none;
}

Error validate(const PeerMessage& message, const VerifiedStaticIdentity& identity, bool incoming) {
    const auto& value = identity.value();
    if (message.sender_id == message.recipient_id || !member(value, message.sender_id) ||
        !member(value, message.recipient_id) ||
        (incoming ? message.recipient_id : message.sender_id) != value.local_node_id)
        return Error::invalid_route;
    if (const auto* vote = std::get_if<RequestVote>(&message.payload);
        vote) {
        if (vote->candidate_id != message.sender_id) return Error::invalid_route;
        if (vote->term < 0 || vote->last_log_index < 0 || vote->last_log_term < 0 ||
            vote->last_log_index == std::numeric_limits<std::int64_t>::max())
            return Error::invalid_payload;
    }
    if (const auto* vote = std::get_if<RequestVoteResponse>(&message.payload);
        vote && vote->term < 0) return Error::invalid_payload;
    if (const auto* append = std::get_if<AppendEntries>(&message.payload)) {
        if (append->entries.size() > kMaxBoundPeerEntries) return Error::capacity_exceeded;
        // Raft forms both the batch end and its following index using signed arithmetic.
        const auto count = static_cast<std::int64_t>(append->entries.size());
        if (append->term < 0 || append->previous_log_index < 0 ||
            append->previous_log_term < 0 || append->leader_commit < 0 || append->compaction_floor < 0 ||
            append->previous_log_index >
                std::numeric_limits<std::int64_t>::max() - count - 1)
            return Error::invalid_payload;
        KernelCommandCodecScratch codec_scratch;
        for (const auto& entry : append->entries) {
            if (entry.term <= 0) return Error::invalid_payload;
            if (entry.payload.size() > kMaxKernelCommandBytes) return Error::capacity_exceeded;
            if (entry.type == kAuthorityBarrierEntryType) {
                AuthorityBarrier marker;
                if (decode_authority_barrier(entry.type, entry.payload, marker).error !=
                        AuthorityBarrierCodecError::none || marker.authority_domain != identity.authority_domain())
                    return Error::invalid_payload;
            } else {
                Command command;
                if (entry.type != 0 || decode_kernel_command(entry.payload, command, codec_scratch).error !=
                        KernelCodecError::none) return Error::invalid_payload;
            }
        }
    }
    if (const auto* append = std::get_if<AppendEntriesResponse>(&message.payload);
        append && (append->term < 0 || append->current_index < 0 ||
            append->current_index == std::numeric_limits<std::int64_t>::max() ||
            append->first_index < 1))
        return Error::invalid_payload;
    return Error::none;
}
} // namespace

std::vector<std::byte> encode_bound_peer_body(const PeerMessage& message) {
    auto body = encode_peer_message(message);
    body[4] = std::byte{kBoundPeerBodyVersion & 0xFFU};
    body[5] = std::byte{kBoundPeerBodyVersion >> 8U};
    if (const auto* append = std::get_if<AppendEntries>(&message.payload)) {
        const auto bits = static_cast<std::uint64_t>(append->compaction_floor);
        for (unsigned i = 0; i < 8; ++i) body.push_back(static_cast<std::byte>(bits >> (8U * i)));
    } else if (const auto* vote = std::get_if<RequestVote>(&message.payload)) {
        body.push_back(std::byte{static_cast<unsigned char>(vote->prevote)});
    } else if (const auto* reply = std::get_if<RequestVoteResponse>(&message.payload)) {
        body.push_back(std::byte{static_cast<unsigned char>(reply->prevote)});
    }
    return body;
}

BoundFrameError decode_bound_peer(std::span<const std::byte> input,
    const VerifiedStaticIdentity& receiver, PeerMessage& destination, BoundFrameScratch& scratch) {
    BoundFrameView view;
    const auto framing = decode_bound_frame(input, BoundFrameKind::peer, receiver, view, scratch);
    if (framing != Error::none) return framing;
    const auto shape = preflight(view.payload);
    if (shape != Error::none) return shape;
    // Preflight fixed the shape: strip the V2 trailer and hand the V1 decoder
    // its own version, then restore the trailer's field.
    const auto kind = std::to_integer<unsigned>(view.payload[6]) |
                      (std::to_integer<unsigned>(view.payload[7]) << 8);
    const std::size_t trailer = kind == 3 ? 8 : (kind == 1 || kind == 2) ? 1 : 0;
    std::vector<std::byte> body(view.payload.begin(), view.payload.end() - static_cast<std::ptrdiff_t>(trailer));
    body[4] = std::byte{kWireVersion & 0xFFU};
    body[5] = std::byte{kWireVersion >> 8U};
    PeerMessage candidate;
    try {
        candidate = decode_peer_message(body);
    } catch (const std::runtime_error&) {
        return Error::invalid_payload;
    }
    if (auto* entries = std::get_if<AppendEntries>(&candidate.payload)) {
        std::uint64_t bits = 0;
        for (unsigned i = 0; i < 8; ++i)
            bits |= std::uint64_t{std::to_integer<unsigned>(view.payload[view.payload.size() - 8 + i])} << (8U * i);
        entries->compaction_floor = static_cast<std::int64_t>(bits);
    } else if (auto* vote = std::get_if<RequestVote>(&candidate.payload)) {
        vote->prevote = view.payload.back() == std::byte{1};
    } else if (auto* reply = std::get_if<RequestVoteResponse>(&candidate.payload)) {
        reply->prevote = view.payload.back() == std::byte{1};
    }
    const auto semantics = validate(candidate, receiver, true);
    if (semantics != Error::none) return semantics;
    destination = std::move(candidate);
    return Error::none;
}

BoundFrameEncodeResult encode_bound_peer(const PeerMessage& message,
    const VerifiedStaticIdentity& sender, std::span<std::byte> output, BoundFrameScratch& scratch) {
    if (!sender.valid()) return {Error::invalid_identity, 0};
    const auto semantics = validate(message, sender, false);
    if (semantics != Error::none) return {semantics, 0};
    const auto inner = encode_bound_peer_body(message);
    return encode_bound_frame(BoundFrameKind::peer, sender, inner, output, scratch);
}

BoundFrameEncodeResult encode_bound_peer(const PeerMessageView& message,
    const VerifiedStaticIdentity& sender, std::span<std::byte> output,
    BoundFrameScratch& scratch) noexcept {
    const auto semantics = validate(message, sender);
    if (semantics != Error::none) return {semantics, 0};
    std::size_t inner_size = 16;
    if (std::holds_alternative<RequestVote>(message.payload)) inner_size += 32 + 1;
    else if (std::holds_alternative<RequestVoteResponse>(message.payload)) inner_size += 9 + 1;
    else if (const auto* append = std::get_if<BorrowedAppendEntries>(&message.payload)) {
        inner_size += 36 + 8;  // + the compaction-floor trailer
        for (const auto& entry : append->entries) inner_size += 20 + entry.payload.size();
    } else inner_size += 25;
    if (inner_size + kBoundFrameOverheadBytes > kMaxBoundPeerBytes)
        return {Error::capacity_exceeded, 0};
    if (output.size() < inner_size + kBoundFrameOverheadBytes)
        return {Error::output_too_small, 0};

    // Stage the complete inner message before touching output. Entry payloads may
    // alias the caller's output buffer, just as encode_bound_frame permits.
    std::array<std::byte, kMaxBoundPeerBytes - kBoundFrameOverheadBytes> inner_storage{};
    auto inner = std::span<std::byte>{inner_storage}.first(inner_size);
    std::size_t at{};
    put(inner, at, std::uint32_t{0x31524550U});
    put(inner, at, kBoundPeerBodyVersion);
    const auto kind = static_cast<std::uint16_t>(message.payload.index() + 1);
    put(inner, at, kind);
    put(inner, at, message.sender_id);
    put(inner, at, message.recipient_id);
    if (const auto* vote = std::get_if<RequestVote>(&message.payload)) {
        put(inner, at, vote->term); put(inner, at, vote->candidate_id);
        put(inner, at, std::uint32_t{}); put(inner, at, vote->last_log_index);
        put(inner, at, vote->last_log_term); put(inner, at, std::uint8_t(vote->prevote));
    } else if (const auto* vote = std::get_if<RequestVoteResponse>(&message.payload)) {
        put(inner, at, vote->term); put(inner, at, std::uint8_t(vote->vote_granted));
        put(inner, at, std::uint8_t(vote->prevote));
    } else if (const auto* append = std::get_if<BorrowedAppendEntries>(&message.payload)) {
        put(inner, at, append->term); put(inner, at, append->previous_log_index);
        put(inner, at, append->previous_log_term); put(inner, at, append->leader_commit);
        put(inner, at, static_cast<std::uint32_t>(append->entries.size()));
        for (const auto& entry : append->entries) {
            put(inner, at, entry.term); put(inner, at, entry.id); put(inner, at, entry.type);
            put(inner, at, static_cast<std::uint32_t>(entry.payload.size()));
            std::copy(entry.payload.begin(), entry.payload.end(), inner.begin() + at);
            at += entry.payload.size();
        }
        put(inner, at, append->compaction_floor);
    } else {
        const auto& response = std::get<AppendEntriesResponse>(message.payload);
        put(inner, at, response.term); put(inner, at, std::uint8_t(response.success));
        put(inner, at, response.current_index); put(inner, at, response.first_index);
    }
    return encode_bound_frame(BoundFrameKind::peer, sender, inner, output, scratch);
}

static_assert(52 + kMaxBoundPeerEntries * (20 + kMaxKernelCommandBytes) + 8 +
    kBoundFrameOverheadBytes <= kMaxBoundPeerBytes);
}
