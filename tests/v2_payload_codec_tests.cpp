#include "velocita/v2/payload.hpp"
#include "velocita/v2/digest.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::byte> bytes_from_hex(const std::string& hex) {
    if ((hex.size() % 2U) != 0U) throw std::runtime_error("odd test hex length");
    std::vector<std::byte> bytes;
    bytes.reserve(hex.size() / 2U);
    for (std::size_t offset = 0; offset < hex.size(); offset += 2U) {
        const auto value = static_cast<unsigned long>(
            std::stoul(hex.substr(offset, 2), nullptr, 16));
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

velocita::v2::Digest256 digest_with_first_byte(const std::byte value) {
    velocita::v2::Digest256 digest{};
    digest.bytes[0] = value;
    return digest;
}

velocita::v2::PolicyArtifact policy_fixture() {
    using namespace velocita::v2;
    PolicyArtifact policy;
    policy.policy_id = PolicyId{1};
    policy.policy_version = PolicyVersion{2};
    policy.schema_version = kTradingContractSchemaVersion;
    policy.feature_schema_id = FeatureSchemaId{3};
    policy.instrument_id = InstrumentId{4};
    policy.account_id = AccountId{5};
    policy.model_digest = digest_with_first_byte(std::byte{0x11});
    policy.artifact_digest = digest_with_first_byte(std::byte{0x22});
    policy.valid_from_event_time_ns = 100;
    policy.valid_until_event_time_ns = 200;
    policy.risk_policy_version = RiskPolicyVersion{6};
    policy.rule_count = 1;
    policy.rules[0] = PolicyRule{0, PredicateOperator::greater_than_or_equal,
                                 FeatureMicros::from_atoms(1'250'000),
                                 Directive::place_limit};
    return policy;
}

velocita::v2::DecisionFrame frame_fixture() {
    using namespace velocita::v2;
    DecisionFrame frame;
    frame.policy_id = PolicyId{1};
    frame.policy_version = PolicyVersion{2};
    frame.feature_schema_id = FeatureSchemaId{3};
    frame.venue_id = VenueId{4};
    frame.venue_session_id = VenueSessionId{5};
    frame.venue_sequence = VenueSequence{6};
    frame.instrument_id = InstrumentId{7};
    frame.account_id = AccountId{8};
    frame.feature_count = 2;
    frame.features[0] = FeatureMicros::from_atoms(-500'000);
    frame.features[1] = FeatureMicros::from_atoms(2'000'000);
    frame.market_data_status = MarketDataStatus::ready;
    frame.market_data_age_ns = 1'000;
    frame.position_version = PositionVersion{9};
    frame.risk_version = RiskVersion{10};
    frame.raw_evidence_digest = digest_with_first_byte(std::byte{0x33});
    return frame;
}

velocita::v2::OrderEffect effect_fixture() {
    using namespace velocita::v2;
    OrderEffect effect;
    effect.effect_id = EffectId{1};
    effect.cluster_order_id = ClusterOrderId{2};
    effect.committed_directive_index = 3;
    effect.executor_generation = ExecutorGeneration{4};
    effect.kill_epoch = KillEpoch{5};
    effect.venue_id = VenueId{6};
    effect.account_id = AccountId{7};
    effect.instrument_id = InstrumentId{8};
    effect.operation = EffectOperation::submit;
    effect.side = Side::buy;
    effect.price = PriceTicks::from_atoms(10'000);
    effect.quantity = QuantityLots::from_atoms(9);
    effect.time_in_force = TimeInForce::good_til_cancel;
    effect.policy_id = PolicyId{10};
    effect.policy_version = PolicyVersion{11};
    effect.decision_id = DecisionId{12};
    effect.risk_version = RiskVersion{13};
    effect.source_generation = SourceGeneration{14};
    effect.source_sequence = SourceSequence{15};
    effect.payload_digest = digest_with_first_byte(std::byte{0x44});
    return effect;
}

velocita::v2::VenueObservation fill_fixture() {
    using namespace velocita::v2;
    VenueObservation fill;
    fill.kind = VenueObservationKind::fill;
    fill.venue_id = VenueId{1};
    fill.venue_session_id = VenueSessionId{2};
    fill.venue_sequence = VenueSequence{3};
    fill.cluster_order_id = ClusterOrderId{4};
    fill.effect_id = EffectId{5};
    fill.venue_order_id = VenueOrderId{6};
    fill.execution_id = ExecutionId{7};
    fill.side = Side::sell;
    fill.price = PriceTicks::from_atoms(9'999);
    fill.quantity = QuantityLots::from_atoms(2);
    fill.fee = FeeMicros::from_atoms(25'000);
    fill.evidence_digest = digest_with_first_byte(std::byte{0x55});
    return fill;
}

void test_policy_golden_and_round_trip() {
    using namespace velocita::v2;
    const auto policy = policy_fixture();
    const auto encoded = encode_payload(policy);
    require(encoded.error == PayloadError::none, "valid policy payload did not encode");

    const auto golden = bytes_from_hex(
        "010001000000000000000200000000000000030000000000000004000000000000000500"
        "000000000000110000000000000000000000000000000000000000000000000000000000"
        "000022000000000000000000000000000000000000000000000000000000000000006400"
        "000000000000c8000000000000000600000000000000010000000402d012130000000000");
    require(encoded.bytes == golden, "canonical policy payload bytes changed");
    require(to_hex(sha256_digest(golden)) ==
                "7cd115af41936eaaabbf53660d86e54d27c288c09f76f98efc1c2c75dd48ef30",
            "canonical policy payload corpus hash changed");

    const auto decoded = decode_policy_artifact(encoded.bytes);
    require(decoded.error == PayloadError::none && decoded.value.has_value(),
            "valid policy payload did not decode");
    require(encode_payload(*decoded.value).bytes == golden,
            "policy payload round trip changed canonical bytes");
}

void test_other_value_round_trips() {
    using namespace velocita::v2;

    const auto frame_bytes = encode_payload(frame_fixture());
    require(frame_bytes.error == PayloadError::none, "valid frame payload did not encode");
    const auto frame = decode_decision_frame(frame_bytes.bytes);
    require(frame.error == PayloadError::none && frame.value.has_value() &&
                encode_payload(*frame.value).bytes == frame_bytes.bytes,
            "decision frame payload did not round trip canonically");

    const auto effect_bytes = encode_payload(effect_fixture());
    require(effect_bytes.error == PayloadError::none, "valid effect payload did not encode");
    const auto effect = decode_order_effect(effect_bytes.bytes);
    require(effect.error == PayloadError::none && effect.value.has_value() &&
                encode_payload(*effect.value).bytes == effect_bytes.bytes,
            "order effect payload did not round trip canonically");

    const auto fill_bytes = encode_payload(fill_fixture());
    require(fill_bytes.error == PayloadError::none,
            "valid observation payload did not encode");
    const auto fill = decode_venue_observation(fill_bytes.bytes);
    require(fill.error == PayloadError::none && fill.value.has_value() &&
                encode_payload(*fill.value).bytes == fill_bytes.bytes,
            "venue observation payload did not round trip canonically");
}

void test_payloads_fail_closed() {
    using namespace velocita::v2;
    const auto canonical = encode_payload(policy_fixture()).bytes;

    auto malformed = canonical;
    malformed.pop_back();
    require(decode_policy_artifact(malformed).error == PayloadError::truncated,
            "truncated policy payload was accepted or misclassified");

    malformed = canonical;
    malformed.push_back(std::byte{0});
    require(decode_policy_artifact(malformed).error == PayloadError::length_mismatch,
            "policy payload trailing byte was accepted");

    malformed = canonical;
    malformed[0] = std::byte{2};
    require(decode_policy_artifact(malformed).error == PayloadError::unsupported_schema,
            "unsupported payload schema was accepted");

    malformed = canonical;
    malformed[130] = std::byte{0x21};
    malformed[131] = std::byte{0};
    require(decode_policy_artifact(malformed).error == PayloadError::capacity_exceeded,
            "oversized declared policy rule count was accepted");

    malformed = canonical;
    malformed[134] = std::byte{0xFF};
    const auto invalid_enum = decode_policy_artifact(malformed);
    require(invalid_enum.error == PayloadError::invalid_contract &&
                invalid_enum.contract_error == ContractError::invalid_enum,
            "invalid policy enum was accepted or lost its typed reason");
}

}  // namespace

int main() {
    try {
        test_policy_golden_and_round_trip();
        test_other_value_round_trips();
        test_payloads_fail_closed();
        std::cout << "all V2 payload-codec tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 payload-codec test failed: " << error.what() << '\n';
        return 1;
    }
}
