#include "velocita/v2/bound_frame.hpp"

#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>

namespace velocita::v2 {
namespace {
constexpr std::array<std::byte, 26> kDomain{
    std::byte{'v'}, std::byte{'e'}, std::byte{'l'}, std::byte{'o'}, std::byte{'c'},
    std::byte{'i'}, std::byte{'t'}, std::byte{'a'}, std::byte{'.'}, std::byte{'v'},
    std::byte{'2'}, std::byte{'.'}, std::byte{'b'}, std::byte{'o'}, std::byte{'u'},
    std::byte{'n'}, std::byte{'d'}, std::byte{'-'}, std::byte{'f'}, std::byte{'r'},
    std::byte{'a'}, std::byte{'m'}, std::byte{'e'}, std::byte{'.'}, std::byte{'v'},
    std::byte{'1'}};
static_assert(kDomain.size() == 26);

constexpr bool known(const BoundFrameKind kind) noexcept {
    return kind >= BoundFrameKind::metadata && kind <= BoundFrameKind::peer;
}
constexpr bool local(const BoundFrameKind kind) noexcept { return kind != BoundFrameKind::peer; }
constexpr std::size_t minimum(const BoundFrameKind kind) noexcept {
    switch (kind) {
        case BoundFrameKind::metadata: return 24;
        case BoundFrameKind::wal_record: return 24;
        case BoundFrameKind::snapshot: return 16;
        case BoundFrameKind::peer: return 16;
    }
    return 0;
}
constexpr std::size_t capacity(const BoundFrameKind kind) noexcept {
    switch (kind) {
        case BoundFrameKind::metadata: return kBoundMetadataBytes - kBoundFrameOverheadBytes;
        case BoundFrameKind::wal_record: return kMaxBoundWalRecordBytes - kBoundFrameOverheadBytes;
        case BoundFrameKind::snapshot: return kMaxBoundSnapshotBytes - kBoundFrameOverheadBytes;
        case BoundFrameKind::peer: return kMaxBoundPeerBytes - kBoundFrameOverheadBytes;
    }
    return 0;
}
void put16(std::span<std::byte> b, std::size_t p, std::uint16_t v) noexcept {
    b[p] = static_cast<std::byte>(v); b[p + 1] = static_cast<std::byte>(v >> 8U);
}
void put32(std::span<std::byte> b, std::size_t p, std::uint32_t v) noexcept {
    for (unsigned i = 0; i < 4; ++i) b[p + i] = static_cast<std::byte>(v >> (8U * i));
}
std::uint16_t get16(std::span<const std::byte> b, std::size_t p) noexcept {
    return std::to_integer<std::uint8_t>(b[p]) |
           static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(b[p + 1])) << 8U;
}
std::uint32_t get32(std::span<const std::byte> b, std::size_t p) noexcept {
    std::uint32_t v{};
    for (unsigned i = 0; i < 4; ++i)
        v |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(b[p + i])) << (8U * i);
    return v;
}
Digest256 calculate(std::span<const std::byte> frame, BoundFrameScratch& scratch) noexcept {
    std::copy(kDomain.begin(), kDomain.end(), scratch.bytes.begin());
    std::copy(frame.begin(), frame.end(), scratch.bytes.begin() + kDomain.size());
    return sha256_digest(std::span<const std::byte>{scratch.bytes}.first(kDomain.size() + frame.size()));
}
bool digest_equal(std::span<const std::byte> encoded, const Digest256& expected) noexcept {
    std::uint8_t difference{};
    for (std::size_t i = 0; i < expected.bytes.size(); ++i)
        difference |= std::to_integer<std::uint8_t>(encoded[i] ^ expected.bytes[i]);
    return difference == 0;
}
} // namespace

BoundFrameEncodeResult encode_bound_frame(
    BoundFrameKind kind, const VerifiedStaticIdentity& identity,
    std::span<const std::byte> payload, std::span<std::byte> output,
    BoundFrameScratch& scratch) noexcept {
    if (!identity.valid()) return {BoundFrameError::invalid_identity, 0};
    if (!known(kind)) return {BoundFrameError::unknown_kind, 0};
    if (payload.size() > capacity(kind)) return {BoundFrameError::capacity_exceeded, 0};
    if (payload.size() < minimum(kind)) return {BoundFrameError::invalid_payload, 0};
    const auto total = kBoundFrameOverheadBytes + payload.size();
    if (output.size() < total) return {BoundFrameError::output_too_small, 0};

    std::array<std::byte, kBoundFrameHeaderBytes> header{};
    header[0] = std::byte{'V'}; header[1] = std::byte{'2'};
    header[2] = std::byte{'B'}; header[3] = std::byte{'F'};
    put16(header, 4, 1); put16(header, 6, static_cast<std::uint16_t>(kind));
    put32(header, 8, static_cast<std::uint32_t>(total));
    put32(header, 12, static_cast<std::uint32_t>(payload.size()));
    std::copy(identity.manifest_digest().bytes.begin(), identity.manifest_digest().bytes.end(),
              header.begin() + 16);
    put32(header, 48, local(kind) ? static_cast<std::uint32_t>(identity.value().local_node_id) : 0);

    // Stage the complete frame before touching output: payload is allowed to overlap output.
    std::copy(kDomain.begin(), kDomain.end(), scratch.bytes.begin());
    auto staged = std::span<std::byte>{scratch.bytes}.subspan(kDomain.size(), total);
    std::copy(header.begin(), header.end(), staged.begin());
    std::copy(payload.begin(), payload.end(), staged.begin() + kBoundFrameHeaderBytes);
    const auto digest = sha256_digest(std::span<const std::byte>{scratch.bytes}.first(
        kDomain.size() + kBoundFrameHeaderBytes + payload.size()));
    std::copy(digest.bytes.begin(), digest.bytes.end(), staged.begin() + kBoundFrameHeaderBytes + payload.size());
    std::copy(staged.begin(), staged.end(), output.begin());
    return {BoundFrameError::none, total};
}

BoundFrameError decode_bound_frame(
    std::span<const std::byte> input, BoundFrameKind expected_kind,
    const VerifiedStaticIdentity& identity, BoundFrameView& destination,
    BoundFrameScratch& scratch) noexcept {
    if (!identity.valid()) return BoundFrameError::invalid_identity;
    if (!known(expected_kind)) return BoundFrameError::unknown_kind;
    if (input.size() < kBoundFrameOverheadBytes) return BoundFrameError::truncated;
    if (input[0] != std::byte{'V'} || input[1] != std::byte{'2'} ||
        input[2] != std::byte{'B'} || input[3] != std::byte{'F'})
        return BoundFrameError::invalid_magic;
    if (get16(input, 4) != 1) return BoundFrameError::unsupported_schema;
    const auto raw_kind = get16(input, 6);
    if (raw_kind < 1 || raw_kind > 4) return BoundFrameError::unknown_kind;
    const auto actual_kind = static_cast<BoundFrameKind>(raw_kind);
    if (actual_kind != expected_kind) return BoundFrameError::wrong_kind;
    const std::size_t payload_size = get32(input, 12);
    if (payload_size > capacity(actual_kind)) return BoundFrameError::capacity_exceeded;
    if (payload_size < minimum(actual_kind)) return BoundFrameError::invalid_payload;
    const auto total = kBoundFrameOverheadBytes + payload_size;
    if (get32(input, 8) != total || input.size() != total) return BoundFrameError::length_mismatch;
    if (get32(input, 52) != 0) return BoundFrameError::nonzero_reserved;
    const auto digest_offset = kBoundFrameHeaderBytes + payload_size;
    const auto digest = calculate(input.first(digest_offset), scratch);
    if (!digest_equal(input.subspan(digest_offset, digest.bytes.size()), digest))
        return BoundFrameError::digest_mismatch;
    if (!std::equal(identity.manifest_digest().bytes.begin(), identity.manifest_digest().bytes.end(), input.begin() + 16))
        return BoundFrameError::foreign_manifest;
    const auto owner = static_cast<std::int32_t>(get32(input, 48));
    if (owner != (local(actual_kind) ? identity.value().local_node_id : 0))
        return BoundFrameError::wrong_owner;
    destination.payload = input.subspan(kBoundFrameHeaderBytes, payload_size);
    return BoundFrameError::none;
}

} // namespace velocita::v2
