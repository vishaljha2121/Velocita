// Canonical byte encoding of the whole trading state.
#pragma once

#include "velocita/v2/kernel.hpp"
#include "velocita/v2/payload.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace velocita::v2 {

inline constexpr std::uint16_t kStateSnapshotSchemaVersion = 12;
inline constexpr std::size_t kStateSnapshotMagicBytes = 4;
inline constexpr std::size_t kStateSnapshotLengthBytes = 4;
inline constexpr std::size_t kStateSnapshotSchemaBytes = 2;
inline constexpr std::size_t kStateSnapshotSchemaOffset =
    kStateSnapshotMagicBytes + kStateSnapshotLengthBytes;
inline constexpr std::size_t kStateSnapshotDigestInputOffset =
    kStateSnapshotSchemaOffset;
inline constexpr std::size_t kStateSnapshotPayloadOffset =
    kStateSnapshotSchemaOffset + kStateSnapshotSchemaBytes;

inline constexpr std::size_t kStateSnapshotEmptyPayloadBytes = 98;
inline constexpr std::size_t kStateSnapshotPolicyFixedBytes = 133;
inline constexpr std::size_t kStateSnapshotPolicyRuleBytes = 12;
inline constexpr std::size_t kStateSnapshotSourceBytes = 24;
inline constexpr std::size_t kStateSnapshotSourceEventBytes = 56;
inline constexpr std::size_t kStateSnapshotReceiptBytes = 133;
inline constexpr std::size_t kStateSnapshotEconomicProposalBytes = 104;
inline constexpr std::size_t kStateSnapshotDecisionFixedBytes = 241;
inline constexpr std::size_t kStateSnapshotDecisionProposalBytes = 54;
inline constexpr std::size_t kStateSnapshotDecisionFeatureBytes = 8;
inline constexpr std::size_t kStateSnapshotRiskAccountBytes = 169;
inline constexpr std::size_t kStateSnapshotRiskReservationBytes = 57;
inline constexpr std::size_t kStateSnapshotClusterOrderBytes = 43;
inline constexpr std::size_t kStateSnapshotCallerCursorBytes = 25;
inline constexpr std::size_t kStateSnapshotOutboxEffectBytes = 62;
inline constexpr std::size_t kStateSnapshotInstrumentBytes = 66;
inline constexpr std::size_t kStateSnapshotVenueBytes = 21;
inline constexpr std::size_t kStateSnapshotExecutableBundleMetadataBytes = 16;
inline constexpr std::size_t kMaxStateSnapshotExecutableBundleBytes =
    kMaxFeatureSchemaPayloadBytes + kMaxExecutablePolicyPayloadBytes +
    kStateSnapshotInstrumentBytes + kStateSnapshotVenueBytes +
    kStateSnapshotExecutableBundleMetadataBytes;
inline constexpr std::size_t kStateSnapshotFixedBytes =
    kStateSnapshotPayloadOffset + kStateSnapshotEmptyPayloadBytes + kDigestBytes;
inline constexpr std::size_t kMaxStateSnapshotLegacyPayloadBytes =
    kStateSnapshotEmptyPayloadBytes +
    kMaxPolicies *
        (kStateSnapshotPolicyFixedBytes +
         kMaxPolicyRules * kStateSnapshotPolicyRuleBytes) +
    kMaxExecutablePolicyArtifacts * kMaxStateSnapshotExecutableBundleBytes +
    kMaxSources * kStateSnapshotSourceBytes +
    kMaxSourceEventReceipts * kStateSnapshotSourceEventBytes +
    kMaxRequestReceipts * kStateSnapshotReceiptBytes +
    kMaxDecisionRecords *
        (kStateSnapshotDecisionFixedBytes +
         kStateSnapshotDecisionProposalBytes +
         kMaxDecisionFeatures * kStateSnapshotDecisionFeatureBytes) +
    kMaxRiskAccounts * kStateSnapshotRiskAccountBytes +
    kMaxRiskReservations * kStateSnapshotRiskReservationBytes +
    kMaxClusterOrders * kStateSnapshotClusterOrderBytes +
    kMaxOutboxEffects * kStateSnapshotOutboxEffectBytes +
    kMaxCallerCursors * kStateSnapshotCallerCursorBytes;

inline constexpr std::size_t kMaxStateSnapshotSwapPayloadBytes =
    kStateSnapshotEmptyPayloadBytes + swap::max_state_bytes - 1 +
    kMaxSources * kStateSnapshotSourceBytes +
    kMaxSourceEventReceipts * kStateSnapshotSourceEventBytes +
    kMaxRequestReceipts * kStateSnapshotReceiptBytes +
    kMaxCallerCursors * kStateSnapshotCallerCursorBytes;
inline constexpr std::size_t kMaxStateSnapshotPayloadBytes =
    kMaxStateSnapshotLegacyPayloadBytes > kMaxStateSnapshotSwapPayloadBytes
        ? kMaxStateSnapshotLegacyPayloadBytes : kMaxStateSnapshotSwapPayloadBytes;

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
static_assert(kMaxStateSnapshotBytes == 61586U);
static_assert(std::is_nothrow_copy_assignable_v<TradingState>);

}
