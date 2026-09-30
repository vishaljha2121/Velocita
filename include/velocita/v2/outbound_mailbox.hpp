#pragma once

#include "velocita/v2/bound_peer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace velocita::v2 {

inline constexpr std::size_t kOutboundRemotePeers = 2;
inline constexpr std::size_t kOutboundKinds = 4;
inline constexpr std::size_t kOutboundSmallFrameBytes = 160;

enum class OutboundEnqueueError : std::uint8_t {
    none, invalid_peer, stale_term, append_entries_requires_view, encode_failed,
};

struct OutboundEnqueueResult {
    OutboundEnqueueError error{OutboundEnqueueError::none};
    BoundFrameError frame_error{BoundFrameError::none};
    bool replaced{};
};

enum class OutboundTakeError : std::uint8_t { none, empty, output_too_small };

struct OutboundTakeResult {
    OutboundTakeError error{OutboundTakeError::empty};
    std::size_t bytes_written{};
    std::int32_t recipient_id{};
    PeerMessageType kind{};
};

struct OutboundMailboxCounters {
    std::uint64_t replacements{};
    std::uint64_t stale_term_refusals{};
};

// Borrows identity for its full lifetime. Identity must remain immutable and valid.
class OutboundMailboxes final {
public:
    explicit OutboundMailboxes(const VerifiedStaticIdentity& identity) noexcept;
    OutboundMailboxes(const OutboundMailboxes&) = delete;
    OutboundMailboxes& operator=(const OutboundMailboxes&) = delete;
    OutboundMailboxes(OutboundMailboxes&&) = delete;
    OutboundMailboxes& operator=(OutboundMailboxes&&) = delete;

    // Owning convenience input is allocation-free for the three small message kinds.
    [[nodiscard]] OutboundEnqueueResult enqueue(
        const PeerMessage& message, BoundFrameScratch& scratch) noexcept;
    [[nodiscard]] OutboundEnqueueResult enqueue(
        const PeerMessageView& message, BoundFrameScratch& scratch) noexcept;
    // Takes the next frame whose recipient is not in HELD. A held peer's frames
    // stay queued and keep coalescing, so one unreachable peer cannot stall the
    // others.
    [[nodiscard]] OutboundTakeResult take(
        std::span<std::byte> output, std::span<const std::int32_t> held = {}) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] const OutboundMailboxCounters& counters() const noexcept { return counters_; }

private:
    template<std::size_t N> struct Slot {
        std::array<std::byte, N> bytes{};
        std::int64_t latest_term{};
        std::uint32_t length{};
        bool has_term{};
        bool occupied{};
    };
    struct PeerSlots {
        std::int32_t peer_id{};
        Slot<kMaxBoundPeerBytes> append_entries;
        std::array<Slot<kOutboundSmallFrameBytes>, 3> small;
    };

    const VerifiedStaticIdentity* identity_{};
    std::array<PeerSlots, kOutboundRemotePeers> peers_{};
    OutboundMailboxCounters counters_{};
    std::size_t size_{};
    std::uint8_t cursor_{};
};

} // namespace velocita::v2
