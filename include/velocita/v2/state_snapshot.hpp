#pragma once

#include "velocita/v2/kernel.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace velocita::v2 {

inline constexpr std::uint16_t kStateSnapshotSchemaVersion = 1;
inline constexpr std::size_t kStateSnapshotMagicBytes = 4;
inline constexpr std::size_t kStateSnapshotLengthBytes = 4;
inline constexpr std::size_t kStateSnapshotSchemaBytes = 2;
inline constexpr std::size_t kStateSnapshotSchemaOffset =
    kStateSnapshotMagicBytes + kStateSnapshotLengthBytes;
inline constexpr std::size_t kStateSnapshotDigestInputOffset =
    kStateSnapshotSchemaOffset;
inline constexpr std::size_t kStateSnapshotPayloadOffset =
    kStateSnapshotSchemaOffset + kStateSnapshotSchemaBytes;

inline constexpr std::size_t kStateSnapshotEmptyPayloadBytes = 33;
inline constexpr std::size_t kStateSnapshotPolicyFixedBytes = 133;
inline constexpr std::size_t kStateSnapshotPolicyRuleBytes = 12;
inline constexpr std::size_t kStateSnapshotSourceBytes = 24;
inline constexpr std::size_t kStateSnapshotSourceEventBytes = 56;
inline constexpr std::size_t kStateSnapshotReceiptBytes = 116;
inline constexpr std::size_t kStateSnapshotFixedBytes =
    kStateSnapshotPayloadOffset + kStateSnapshotEmptyPayloadBytes + kDigestBytes;
inline constexpr std::size_t kMaxStateSnapshotPayloadBytes =
    kStateSnapshotEmptyPayloadBytes +
    kMaxPolicies *
        (kStateSnapshotPolicyFixedBytes +
         kMaxPolicyRules * kStateSnapshotPolicyRuleBytes) +
    kMaxSources * kStateSnapshotSourceBytes +
    kMaxSourceEventReceipts * kStateSnapshotSourceEventBytes +
    kMaxRequestReceipts * kStateSnapshotReceiptBytes;
inline constexpr std::size_t kMaxStateSnapshotBytes =
    kStateSnapshotPayloadOffset + kMaxStateSnapshotPayloadBytes + kDigestBytes;

enum class StateSnapshotError : std::uint8_t {
    none = 0,
    output_too_small,
    truncated,
    invalid_magic,
    unsupported_schema,
    length_mismatch,
    digest_mismatch,
    invalid_state,
};

struct EncodeStateSnapshotResult {
    StateSnapshotError error{StateSnapshotError::none};
    std::size_t bytes_written{};
    Digest256 state_digest{};
};

struct DecodeStateSnapshotResult {
    StateSnapshotError error{StateSnapshotError::none};
    Digest256 state_digest{};
};

// Decode is transactional: candidate is caller-owned scratch and destination
// is assigned only after framing, digest, bounds, enums, and state invariants
// have all been validated.
struct StateSnapshotScratch final {
    TradingState candidate{};
};

[[nodiscard]] EncodeStateSnapshotResult
encode_state_snapshot(const TradingState& state,
                      std::span<std::byte> output) noexcept;

[[nodiscard]] DecodeStateSnapshotResult
decode_state_snapshot(std::span<const std::byte> snapshot,
                      TradingState& destination,
                      StateSnapshotScratch& scratch) noexcept;

static_assert(kMaxStateSnapshotBytes < 64U * 1024U);
static_assert(std::is_nothrow_copy_assignable_v<TradingState>);

}  // namespace velocita::v2
