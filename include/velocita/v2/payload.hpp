#pragma once

#include "velocita/v2/contracts.hpp"
#include "velocita/v2/executable_policy.hpp"

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
inline constexpr std::size_t kDecisionFramePayloadFixedBytes = 165;
inline constexpr std::size_t kMaxDecisionFramePayloadBytes =
    kDecisionFramePayloadFixedBytes + kMaxDecisionFeatures * sizeof(std::int64_t);
inline constexpr std::size_t kOrderEffectPayloadBytes = 166;
inline constexpr std::size_t kVenueObservationPayloadBytes = 116;
inline constexpr std::size_t kFeatureDescriptorEncodedBytes = 9;
inline constexpr std::size_t kFeatureSchemaPayloadPrefixBytes = 44;
inline constexpr std::size_t kMaxFeatureSchemaPayloadBytes =
    kFeatureSchemaPayloadPrefixBytes +
    kMaxDecisionFeatures * kFeatureDescriptorEncodedBytes;
inline constexpr std::size_t kPriceSourceEncodedBytes = 28;
inline constexpr std::size_t kQuantitySourceEncodedBytes = 20;
inline constexpr std::size_t kActionSafetyCapsEncodedBytes =
    5 * sizeof(std::int64_t) + kPriceSourceEncodedBytes;
inline constexpr std::size_t kActionTemplateEncodedBytes = 140;
inline constexpr std::size_t kExecutablePolicyPayloadPrefixBytes = 124;
inline constexpr std::size_t kMaxExecutablePolicyPayloadBytes =
    kExecutablePolicyPayloadPrefixBytes +
    kMaxExecutableActionTemplates * kActionTemplateEncodedBytes +
    sizeof(std::uint16_t) + kMaxPolicyRules * sizeof(std::uint8_t);

static_assert(kMaxDecisionFramePayloadBytes <= kDecisionFramePayloadBudgetBytes,
              "DecisionFrame exceeded its reviewed canonical payload budget");
static_assert(kMaxPolicyPayloadBytes <= kPolicyArtifactPayloadBudgetBytes,
              "PolicyArtifact exceeded its reviewed canonical payload budget");
static_assert(kActionSafetyCapsEncodedBytes == 68);
static_assert(kActionTemplateEncodedBytes ==
              2 + 4 + 2 + 8 + 8 + kPriceSourceEncodedBytes +
                  kQuantitySourceEncodedBytes + kActionSafetyCapsEncodedBytes);
static_assert(kMaxFeatureSchemaPayloadBytes <=
              kFeatureSchemaArtifactPayloadBudgetBytes,
              "FeatureSchemaArtifact exceeded its candidate payload budget");
static_assert(kMaxExecutablePolicyPayloadBytes <=
              kExecutablePolicyArtifactPayloadBudgetBytes,
              "ExecutablePolicyArtifact exceeded its candidate payload budget");

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
[[nodiscard]] EncodePayloadResult encode_payload(const FeatureSchemaArtifact& value);
[[nodiscard]] EncodePayloadResult encode_payload(const ExecutablePolicyArtifact& value);

[[nodiscard]] DecodePayloadResult<PolicyArtifact>
decode_policy_artifact(std::span<const std::byte> bytes);
[[nodiscard]] DecodePayloadResult<DecisionFrame>
decode_decision_frame(std::span<const std::byte> bytes);
[[nodiscard]] DecodePayloadResult<OrderEffect>
decode_order_effect(std::span<const std::byte> bytes);
[[nodiscard]] DecodePayloadResult<VenueObservation>
decode_venue_observation(std::span<const std::byte> bytes);
[[nodiscard]] DecodePayloadResult<FeatureSchemaArtifact>
decode_feature_schema_artifact(std::span<const std::byte> bytes);
[[nodiscard]] DecodePayloadResult<ExecutablePolicyArtifact>
decode_executable_policy_artifact(std::span<const std::byte> bytes);

}  // namespace velocita::v2
