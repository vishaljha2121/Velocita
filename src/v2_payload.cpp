#include "velocita/v2/payload.hpp"

#include <bit>
#include <type_traits>
#include <utility>

namespace velocita::v2 {
namespace {

template <typename T>
void append_le(std::vector<std::byte>& out, const T value) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    const U bits = static_cast<U>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        out.push_back(static_cast<std::byte>((bits >> (index * 8U)) & 0xFFU));
    }
}

void append_digest(std::vector<std::byte>& out, const Digest256& digest) {
    out.insert(out.end(), digest.bytes.begin(), digest.bytes.end());
}

class Reader {
public:
    explicit Reader(const std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

    template <typename T>
    [[nodiscard]] T read() noexcept {
        static_assert(std::is_integral_v<T>);
        using U = std::make_unsigned_t<T>;
        U bits{};
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            bits |= static_cast<U>(std::to_integer<unsigned char>(bytes_[offset_ + index]))
                    << (index * 8U);
        }
        offset_ += sizeof(T);
        if constexpr (std::is_signed_v<T>) {
            return std::bit_cast<T>(bits);
        } else {
            return bits;
        }
    }

    [[nodiscard]] Digest256 read_digest() noexcept {
        Digest256 digest;
        for (auto& byte : digest.bytes) byte = bytes_[offset_++];
        return digest;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

template <typename Value>
DecodePayloadResult<Value> size_error(const std::size_t actual,
                                      const std::size_t expected) {
    return {actual < expected ? PayloadError::truncated : PayloadError::length_mismatch,
            ContractError::none,
            std::nullopt};
}

template <typename Value>
DecodePayloadResult<Value> finish_decode(Value value) {
    const auto contract_error = validate(value);
    if (contract_error != ContractError::none) {
        return {PayloadError::invalid_contract, contract_error, std::nullopt};
    }
    return {PayloadError::none, ContractError::none, std::move(value)};
}

EncodePayloadResult invalid_encode(const ContractError error) {
    return {PayloadError::invalid_contract, error, {}};
}

void append_policy_identity(std::vector<std::byte>& bytes, const PolicyArtifact& value) {
    append_le(bytes, value.schema_version);
    append_le(bytes, value.policy_id.value());
    append_le(bytes, value.policy_version.value());
    append_le(bytes, value.feature_schema_id.value());
    append_le(bytes, value.instrument_id.value());
    append_le(bytes, value.account_id.value());
}

}  // namespace

EncodePayloadResult encode_payload(const PolicyArtifact& value) {
    const auto contract_error = validate(value);
    if (contract_error != ContractError::none) return invalid_encode(contract_error);

    EncodePayloadResult result;
    result.bytes.reserve(kPolicyPayloadPrefixBytes +
                         value.rule_count * kPolicyRuleEncodedBytes);
    append_policy_identity(result.bytes, value);
    append_digest(result.bytes, value.model_digest);
    append_digest(result.bytes, value.artifact_digest);
    append_le(result.bytes, value.valid_from_event_time_ns);
    append_le(result.bytes, value.valid_until_event_time_ns);
    append_le(result.bytes, value.risk_policy_version.value());
    append_le(result.bytes, value.rule_count);
    for (std::size_t index = 0; index < value.rule_count; ++index) {
        const auto& rule = value.rules[index];
        append_le(result.bytes, rule.feature_index);
        append_le(result.bytes, static_cast<std::uint8_t>(rule.predicate));
        append_le(result.bytes, static_cast<std::uint8_t>(rule.directive));
        append_le(result.bytes, rule.threshold.atoms());
    }
    return result;
}

EncodePayloadResult encode_payload(const DecisionFrame& value) {
    const auto contract_error = validate(value);
    if (contract_error != ContractError::none) return invalid_encode(contract_error);

    EncodePayloadResult result;
    result.bytes.reserve(kDecisionFramePayloadFixedBytes +
                         value.feature_count * sizeof(std::int64_t));
    append_le(result.bytes, kDecisionFrameSchemaVersion);
    append_le(result.bytes, value.policy_id.value());
    append_le(result.bytes, value.policy_version.value());
    append_le(result.bytes, value.feature_schema_id.value());
    append_le(result.bytes, value.venue_id.value());
    append_le(result.bytes, value.venue_session_id.value());
    append_le(result.bytes, value.venue_sequence.value());
    append_le(result.bytes, value.source_id.value());
    append_le(result.bytes, value.source_generation.value());
    append_le(result.bytes, value.source_sequence.value());
    append_le(result.bytes, value.event_time_ns);
    append_le(result.bytes, value.receive_time_ns);
    append_le(result.bytes, value.instrument_id.value());
    append_le(result.bytes, value.account_id.value());
    append_le(result.bytes, value.feature_count);
    for (std::size_t index = 0; index < value.feature_count; ++index) {
        append_le(result.bytes, value.features[index].atoms());
    }
    append_le(result.bytes, static_cast<std::uint8_t>(value.market_data_status));
    append_le(result.bytes, value.market_data_age_ns);
    append_le(result.bytes, value.position_version.value());
    append_le(result.bytes, value.risk_version.value());
    append_digest(result.bytes, value.raw_evidence_digest);
    return result;
}

EncodePayloadResult encode_payload(const OrderEffect& value) {
    const auto contract_error = validate(value);
    if (contract_error != ContractError::none) return invalid_encode(contract_error);

    EncodePayloadResult result;
    result.bytes.reserve(kOrderEffectPayloadBytes);
    append_le(result.bytes, kOrderEffectSchemaVersion);
    append_le(result.bytes, value.effect_id.value());
    append_le(result.bytes, value.cluster_order_id.value());
    append_le(result.bytes, value.committed_directive_index);
    append_le(result.bytes, value.executor_generation.value());
    append_le(result.bytes, value.kill_epoch.value());
    append_le(result.bytes, value.venue_id.value());
    append_le(result.bytes, value.account_id.value());
    append_le(result.bytes, value.instrument_id.value());
    append_le(result.bytes, static_cast<std::uint8_t>(value.operation));
    append_le(result.bytes, static_cast<std::uint8_t>(value.side));
    append_le(result.bytes, value.price.atoms());
    append_le(result.bytes, value.quantity.atoms());
    append_le(result.bytes, static_cast<std::uint8_t>(value.time_in_force));
    append_le(result.bytes, value.policy_id.value());
    append_le(result.bytes, value.policy_version.value());
    append_le(result.bytes, value.decision_id.value());
    append_le(result.bytes, value.risk_version.value());
    append_le(result.bytes, value.source_generation.value());
    append_le(result.bytes, value.source_sequence.value());
    append_digest(result.bytes, value.payload_digest);
    return result;
}

EncodePayloadResult encode_payload(const VenueObservation& value) {
    const auto contract_error = validate(value);
    if (contract_error != ContractError::none) return invalid_encode(contract_error);

    EncodePayloadResult result;
    result.bytes.reserve(kVenueObservationPayloadBytes);
    append_le(result.bytes, kVenueObservationSchemaVersion);
    append_le(result.bytes, static_cast<std::uint8_t>(value.kind));
    append_le(result.bytes, value.venue_id.value());
    append_le(result.bytes, value.venue_session_id.value());
    append_le(result.bytes, value.venue_sequence.value());
    append_le(result.bytes, value.cluster_order_id.value());
    append_le(result.bytes, value.effect_id.value());
    append_le(result.bytes, value.venue_order_id.value());
    append_le(result.bytes, value.execution_id.value());
    append_le(result.bytes, static_cast<std::uint8_t>(value.side));
    append_le(result.bytes, value.price.atoms());
    append_le(result.bytes, value.quantity.atoms());
    append_le(result.bytes, value.fee.atoms());
    append_digest(result.bytes, value.evidence_digest);
    return result;
}

DecodePayloadResult<PolicyArtifact>
decode_policy_artifact(const std::span<const std::byte> bytes) {
    if (bytes.size() < kPolicyPayloadPrefixBytes) {
        return size_error<PolicyArtifact>(bytes.size(), kPolicyPayloadPrefixBytes);
    }
    Reader reader(bytes);
    PolicyArtifact value;
    value.schema_version = reader.read<std::uint16_t>();
    if (value.schema_version != kPolicyArtifactSchemaVersion) {
        return {PayloadError::unsupported_schema, ContractError::unsupported_schema,
                std::nullopt};
    }
    value.policy_id = PolicyId{reader.read<std::uint64_t>()};
    value.policy_version = PolicyVersion{reader.read<std::uint64_t>()};
    value.feature_schema_id = FeatureSchemaId{reader.read<std::uint64_t>()};
    value.instrument_id = InstrumentId{reader.read<std::uint64_t>()};
    value.account_id = AccountId{reader.read<std::uint64_t>()};
    value.model_digest = reader.read_digest();
    value.artifact_digest = reader.read_digest();
    value.valid_from_event_time_ns = reader.read<std::int64_t>();
    value.valid_until_event_time_ns = reader.read<std::int64_t>();
    value.risk_policy_version = RiskPolicyVersion{reader.read<std::uint64_t>()};
    value.rule_count = reader.read<std::uint16_t>();
    if (value.rule_count > kMaxPolicyRules) {
        return {PayloadError::capacity_exceeded, ContractError::capacity_exceeded,
                std::nullopt};
    }
    const auto expected = kPolicyPayloadPrefixBytes +
                          value.rule_count * kPolicyRuleEncodedBytes;
    if (bytes.size() != expected) return size_error<PolicyArtifact>(bytes.size(), expected);
    for (std::size_t index = 0; index < value.rule_count; ++index) {
        auto& rule = value.rules[index];
        rule.feature_index = reader.read<std::uint16_t>();
        rule.predicate = static_cast<PredicateOperator>(reader.read<std::uint8_t>());
        rule.directive = static_cast<Directive>(reader.read<std::uint8_t>());
        rule.threshold = FeatureMicros::from_atoms(reader.read<std::int64_t>());
    }
    return finish_decode(std::move(value));
}

DecodePayloadResult<DecisionFrame>
decode_decision_frame(const std::span<const std::byte> bytes) {
    constexpr std::size_t schema_bytes = sizeof(std::uint16_t);
    if (bytes.size() < schema_bytes) {
        return size_error<DecisionFrame>(bytes.size(), schema_bytes);
    }
    Reader reader(bytes);
    if (reader.read<std::uint16_t>() != kDecisionFrameSchemaVersion) {
        return {PayloadError::unsupported_schema, ContractError::unsupported_schema,
                std::nullopt};
    }
    if (bytes.size() < kDecisionFramePayloadFixedBytes) {
        return size_error<DecisionFrame>(bytes.size(), kDecisionFramePayloadFixedBytes);
    }
    DecisionFrame value;
    value.policy_id = PolicyId{reader.read<std::uint64_t>()};
    value.policy_version = PolicyVersion{reader.read<std::uint64_t>()};
    value.feature_schema_id = FeatureSchemaId{reader.read<std::uint64_t>()};
    value.venue_id = VenueId{reader.read<std::uint64_t>()};
    value.venue_session_id = VenueSessionId{reader.read<std::uint64_t>()};
    value.venue_sequence = VenueSequence{reader.read<std::uint64_t>()};
    value.source_id = SourceId{reader.read<std::uint64_t>()};
    value.source_generation = SourceGeneration{reader.read<std::uint64_t>()};
    value.source_sequence = SourceSequence{reader.read<std::uint64_t>()};
    value.event_time_ns = reader.read<std::int64_t>();
    value.receive_time_ns = reader.read<std::int64_t>();
    value.instrument_id = InstrumentId{reader.read<std::uint64_t>()};
    value.account_id = AccountId{reader.read<std::uint64_t>()};
    value.feature_count = reader.read<std::uint16_t>();
    if (value.feature_count > kMaxDecisionFeatures) {
        return {PayloadError::capacity_exceeded, ContractError::capacity_exceeded,
                std::nullopt};
    }
    const auto expected = kDecisionFramePayloadFixedBytes +
                          value.feature_count * sizeof(std::int64_t);
    if (bytes.size() != expected) return size_error<DecisionFrame>(bytes.size(), expected);
    for (std::size_t index = 0; index < value.feature_count; ++index) {
        value.features[index] = FeatureMicros::from_atoms(reader.read<std::int64_t>());
    }
    value.market_data_status = static_cast<MarketDataStatus>(reader.read<std::uint8_t>());
    value.market_data_age_ns = reader.read<std::int64_t>();
    value.position_version = PositionVersion{reader.read<std::uint64_t>()};
    value.risk_version = RiskVersion{reader.read<std::uint64_t>()};
    value.raw_evidence_digest = reader.read_digest();
    return finish_decode(std::move(value));
}

DecodePayloadResult<OrderEffect>
decode_order_effect(const std::span<const std::byte> bytes) {
    if (bytes.size() != kOrderEffectPayloadBytes) {
        return size_error<OrderEffect>(bytes.size(), kOrderEffectPayloadBytes);
    }
    Reader reader(bytes);
    if (reader.read<std::uint16_t>() != kOrderEffectSchemaVersion) {
        return {PayloadError::unsupported_schema, ContractError::unsupported_schema,
                std::nullopt};
    }
    OrderEffect value;
    value.effect_id = EffectId{reader.read<std::uint64_t>()};
    value.cluster_order_id = ClusterOrderId{reader.read<std::uint64_t>()};
    value.committed_directive_index = reader.read<std::uint64_t>();
    value.executor_generation = ExecutorGeneration{reader.read<std::uint64_t>()};
    value.kill_epoch = KillEpoch{reader.read<std::uint64_t>()};
    value.venue_id = VenueId{reader.read<std::uint64_t>()};
    value.account_id = AccountId{reader.read<std::uint64_t>()};
    value.instrument_id = InstrumentId{reader.read<std::uint64_t>()};
    value.operation = static_cast<EffectOperation>(reader.read<std::uint8_t>());
    value.side = static_cast<Side>(reader.read<std::uint8_t>());
    value.price = PriceTicks::from_atoms(reader.read<std::int64_t>());
    value.quantity = QuantityLots::from_atoms(reader.read<std::int64_t>());
    value.time_in_force = static_cast<TimeInForce>(reader.read<std::uint8_t>());
    value.policy_id = PolicyId{reader.read<std::uint64_t>()};
    value.policy_version = PolicyVersion{reader.read<std::uint64_t>()};
    value.decision_id = DecisionId{reader.read<std::uint64_t>()};
    value.risk_version = RiskVersion{reader.read<std::uint64_t>()};
    value.source_generation = SourceGeneration{reader.read<std::uint64_t>()};
    value.source_sequence = SourceSequence{reader.read<std::uint64_t>()};
    value.payload_digest = reader.read_digest();
    return finish_decode(std::move(value));
}

DecodePayloadResult<VenueObservation>
decode_venue_observation(const std::span<const std::byte> bytes) {
    if (bytes.size() != kVenueObservationPayloadBytes) {
        return size_error<VenueObservation>(bytes.size(), kVenueObservationPayloadBytes);
    }
    Reader reader(bytes);
    if (reader.read<std::uint16_t>() != kVenueObservationSchemaVersion) {
        return {PayloadError::unsupported_schema, ContractError::unsupported_schema,
                std::nullopt};
    }
    VenueObservation value;
    value.kind = static_cast<VenueObservationKind>(reader.read<std::uint8_t>());
    value.venue_id = VenueId{reader.read<std::uint64_t>()};
    value.venue_session_id = VenueSessionId{reader.read<std::uint64_t>()};
    value.venue_sequence = VenueSequence{reader.read<std::uint64_t>()};
    value.cluster_order_id = ClusterOrderId{reader.read<std::uint64_t>()};
    value.effect_id = EffectId{reader.read<std::uint64_t>()};
    value.venue_order_id = VenueOrderId{reader.read<std::uint64_t>()};
    value.execution_id = ExecutionId{reader.read<std::uint64_t>()};
    value.side = static_cast<Side>(reader.read<std::uint8_t>());
    value.price = PriceTicks::from_atoms(reader.read<std::int64_t>());
    value.quantity = QuantityLots::from_atoms(reader.read<std::int64_t>());
    value.fee = FeeMicros::from_atoms(reader.read<std::int64_t>());
    value.evidence_digest = reader.read_digest();
    return finish_decode(std::move(value));
}

}  // namespace velocita::v2
