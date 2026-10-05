// Frames that bind a payload to the cluster's static identity.
#pragma once

#include "velocita/v2/static_identity.hpp"
#include "velocita/v2/kernel_codec.hpp"
#include "velocita/v2/state_snapshot.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace velocita::v2 {

inline constexpr std::size_t kBoundFrameHeaderBytes = 56;
inline constexpr std::size_t kBoundFrameOverheadBytes = 88;
inline constexpr std::size_t kBoundMetadataBytes = 112;
inline constexpr std::size_t kMaxBoundWalRecordBytes = 112 + kMaxKernelCommandBytes;
inline constexpr std::size_t kMaxBoundSnapshotBytes = 104 + kMaxStateSnapshotBytes;
inline constexpr std::size_t kMaxBoundPeerBytes = 32768;
inline constexpr std::size_t kMaxBoundPeerEntries = 16;

static_assert(kMaxKernelCommandBytes == 1789 && kMaxStateSnapshotBytes == 65519);
static_assert(kMaxBoundWalRecordBytes == 1901 && kMaxBoundSnapshotBytes == 65623);

enum class BoundFrameKind : std::uint16_t {
    metadata = 1, wal_record = 2, snapshot = 3, peer = 4,
};

enum class BoundFrameError : std::uint8_t {
    none, output_too_small, truncated, invalid_magic, unsupported_schema,
    unknown_kind, wrong_kind, length_mismatch, capacity_exceeded, nonzero_reserved,
    digest_mismatch, invalid_identity, foreign_manifest, wrong_owner, invalid_payload,
    invalid_route,
};

struct BoundFrameEncodeResult {
    BoundFrameError error{BoundFrameError::none};
    std::size_t bytes_written{};
};

struct BoundFrameScratch {
    std::array<std::byte, kMaxBoundSnapshotBytes + 26> bytes{};
};
struct BoundFrameView {
    std::span<const std::byte> payload;
};

[[nodiscard]] BoundFrameEncodeResult encode_bound_frame(
    BoundFrameKind kind, const VerifiedStaticIdentity& identity,
    std::span<const std::byte> payload, std::span<std::byte> output,
    BoundFrameScratch& scratch) noexcept;

[[nodiscard]] BoundFrameError decode_bound_frame(
    std::span<const std::byte> input, BoundFrameKind expected_kind,
    const VerifiedStaticIdentity& identity, BoundFrameView& destination,
    BoundFrameScratch& scratch) noexcept;

}
