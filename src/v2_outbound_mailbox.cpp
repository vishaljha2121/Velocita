#include "velocita/v2/outbound_mailbox.hpp"

#include <algorithm>
#include <type_traits>

namespace velocita::v2 {
namespace {
std::int64_t term(const BorrowedPeerPayload& payload) noexcept {
    return std::visit([](const auto& value) { return value.term; }, payload);
}

std::size_t kind_index(const BorrowedPeerPayload& payload) noexcept {
    return payload.index();
}
} // namespace

OutboundMailboxes::OutboundMailboxes(const VerifiedStaticIdentity& identity) noexcept
    : identity_(&identity) {
    if (!identity.valid()) return;
    std::size_t at{};
    for (std::size_t i = 0; i < identity.value().voter_count && at < peers_.size(); ++i) {
        const auto id = identity.value().voters[i];
        if (id != identity.value().local_node_id) peers_[at++].peer_id = id;
    }
}

OutboundEnqueueResult OutboundMailboxes::enqueue(
    const PeerMessage& message, BoundFrameScratch& scratch) noexcept {
    if (std::holds_alternative<AppendEntries>(message.payload))
        return {OutboundEnqueueError::append_entries_requires_view, BoundFrameError::none, false};
    PeerMessageView view{message.sender_id, message.recipient_id,
        std::visit([](const auto& value) -> BorrowedPeerPayload {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AppendEntries>) return BorrowedAppendEntries{};
            else return value;
        }, message.payload)};
    return enqueue(view, scratch);
}

OutboundEnqueueResult OutboundMailboxes::enqueue(
    const PeerMessageView& message, BoundFrameScratch& scratch) noexcept {
    auto peer = std::find_if(peers_.begin(), peers_.end(), [&](const auto& candidate) {
        return candidate.peer_id == message.recipient_id;
    });
    if (peer == peers_.end())
        return {OutboundEnqueueError::invalid_peer, BoundFrameError::invalid_route, false};
    const auto kind = kind_index(message.payload);
    const auto incoming_term = term(message.payload);

    auto store = [&](auto& slot) -> OutboundEnqueueResult {
        if (slot.has_term && incoming_term < slot.latest_term) {
            ++counters_.stale_term_refusals;
            return {OutboundEnqueueError::stale_term, BoundFrameError::none, false};
        }
        const auto encoded = encode_bound_peer(message, *identity_, slot.bytes, scratch);
        if (encoded.error != BoundFrameError::none)
            return {OutboundEnqueueError::encode_failed, encoded.error, false};
        const bool replaced = slot.occupied;
        slot.latest_term = incoming_term;
        slot.has_term = true;
        slot.length = static_cast<std::uint32_t>(encoded.bytes_written);
        slot.occupied = true;
        if (replaced) ++counters_.replacements;
        else ++size_;
        return {OutboundEnqueueError::none, BoundFrameError::none, replaced};
    };

    if (kind == 2) return store(peer->append_entries);
    return store(peer->small[kind < 2 ? kind : kind - 1]);
}

OutboundTakeResult OutboundMailboxes::take(
    std::span<std::byte> output, std::span<const std::int32_t> held) noexcept {
    if (size_ == 0) return {};
    constexpr std::size_t total = kOutboundRemotePeers * kOutboundKinds;
    for (std::size_t visited = 0; visited < total; ++visited) {
        const auto flat = (static_cast<std::size_t>(cursor_) + visited) % total;
        auto& peer = peers_[flat / kOutboundKinds];
        if (std::find(held.begin(), held.end(), peer.peer_id) != held.end()) continue;
        const auto kind = flat % kOutboundKinds;
        auto take_slot = [&](auto& slot) -> OutboundTakeResult {
            if (!slot.occupied) return {};
            if (output.size() < slot.length)
                return {OutboundTakeError::output_too_small, 0, peer.peer_id,
                    static_cast<PeerMessageType>(kind + 1)};
            std::copy_n(slot.bytes.begin(), slot.length, output.begin());
            const auto length = slot.length;
            slot.length = 0;
            slot.occupied = false;
            --size_;
            cursor_ = static_cast<std::uint8_t>((flat + 1) % total);
            return {OutboundTakeError::none, length, peer.peer_id,
                static_cast<PeerMessageType>(kind + 1)};
        };
        OutboundTakeResult result = kind == 2 ? take_slot(peer.append_entries) :
            take_slot(peer.small[kind < 2 ? kind : kind - 1]);
        if (result.error != OutboundTakeError::empty) return result;
    }
    return {};
}

} // namespace velocita::v2
