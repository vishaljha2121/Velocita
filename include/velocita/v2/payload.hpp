#pragma once

#include "velocita/v2/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace velocita::v2 {

inline constexpr std::size_t kPolicyPayloadPrefixBytes = 132;
inline constexpr std::size_t kPolicyRuleEncodedBytes = 12;
inline constexpr std::size_t kMaxPolicyPayloadBytes =
    kPolicyPayloadPrefixBytes + kMaxPolicyRules * kPolicyRuleEncodedBytes;
inline constexpr std::size_t kDecisionFramePayloadFixedBytes = 125;
inline constexpr std::size_t kMaxDecisionFramePayloadBytes =
    kDecisionFramePayloadFixedBytes + kMaxDecisionFeatures * sizeof(std::int64_t);
inline constexpr std::size_t kOrderEffectPayloadBytes = 165;
inline constexpr std::size_t kVenueObservationPayloadBytes = 116;

enum class PayloadError : std::uint8_t {
    none = 0,
    truncated,
    length_mismatch,
    unsupported_schema,
    capacity_exceeded,
    invalid_contract,
};

struct EncodePayloadResult {
    PayloadError error{PayloadError::none};
    ContractError contract_error{ContractError::none};
    std::vector<std::byte> bytes;
};

template <typename Value>
struct DecodePayloadResult {
    PayloadError error{PayloadError::none};
    ContractError contract_error{ContractError::none};
    std::optional<Value> value;
};

[[nodiscard]] EncodePayloadResult encode_payload(const PolicyArtifact& value);
[[nodiscard]] EncodePayloadResult encode_payload(const DecisionFrame& value);
[[nodiscard]] EncodePayloadResult encode_payload(const OrderEffect& value);
[[nodiscard]] EncodePayloadResult encode_payload(const VenueObservation& value);

[[nodiscard]] DecodePayloadResult<PolicyArtifact>
decode_policy_artifact(std::span<const std::byte> bytes);
[[nodiscard]] DecodePayloadResult<DecisionFrame>
decode_decision_frame(std::span<const std::byte> bytes);
[[nodiscard]] DecodePayloadResult<OrderEffect>
decode_order_effect(std::span<const std::byte> bytes);
[[nodiscard]] DecodePayloadResult<VenueObservation>
decode_venue_observation(std::span<const std::byte> bytes);

}  // namespace velocita::v2
