#include "velocita/v2/digest.hpp"
#include "velocita/v2/payload.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

velocita::v2::Digest256 digest(const std::byte marker) {
    velocita::v2::Digest256 value;
    value.bytes[0] = marker;
    return value;
}

velocita::v2::FeatureSchemaArtifact schema_fixture() {
    using namespace velocita::v2;
    FeatureSchemaArtifact value;
    value.schema_version = kFeatureSchemaArtifactSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{3};
    value.artifact_digest = digest(std::byte{0x31});
    value.feature_count = 3;
    value.features[0] = {FeatureEconomicKind::price, 1'000'000};
    value.features[1] = {FeatureEconomicKind::price, 1'000'000};
    value.features[2] = {FeatureEconomicKind::quantity, 1'000'000};
    return value;
}

velocita::v2::PriceSource feature_price(const std::uint16_t index) {
    using namespace velocita::v2;
    return {EconomicValueSourceKind::feature, index,
            RoundingMode::nearest_ties_away_from_zero, {}, 1'000'000,
            PriceTicks::from_atoms(1)};
}

velocita::v2::QuantitySource fixed_quantity() {
    using namespace velocita::v2;
    return {EconomicValueSourceKind::fixed, 0, RoundingMode::toward_zero,
            QuantityLots::from_atoms(2), 0};
}

velocita::v2::ActionTemplate action_fixture() {
    using namespace velocita::v2;
    ActionTemplate value;
    value.schema_version = kActionTemplateSchemaVersion;
    value.directive = Directive::place_limit;
    value.side = Side::buy;
    value.time_in_force = TimeInForce::good_til_cancel;
    value.post_only = PostOnlyMode::required;
    value.order_slot = LogicalOrderSlot{7};
    value.instrument_spec_version = InstrumentSpecVersion{20};
    value.venue_capabilities_version = VenueCapabilitiesVersion{30};
    value.price_source = feature_price(0);
    value.quantity_source = fixed_quantity();
    value.safety_caps.max_quantity = QuantityLots::from_atoms(5);
    value.safety_caps.max_notional = MoneyMicros::from_atoms(5'000'000);
    value.safety_caps.max_abs_price_deviation = PriceTicks::from_atoms(2);
    value.safety_caps.max_abs_tick_offset = PriceTicks::from_atoms(1);
    value.safety_caps.max_market_data_age_ns = 50'000;
    value.safety_caps.reference_price = feature_price(1);
    value.safety_caps.reference_price.tick_offset = {};
    return value;
}

velocita::v2::ExecutablePolicyArtifact executable_fixture() {
    using namespace velocita::v2;
    ExecutablePolicyArtifact value;
    value.schema_version = kExecutablePolicyArtifactSchemaVersion;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{2};
    value.policy_artifact_digest = digest(std::byte{0x12});
    value.feature_schema_id = FeatureSchemaId{3};
    value.feature_schema_digest = digest(std::byte{0x31});
    value.artifact_digest = digest(std::byte{0x41});
    value.template_count = 1;
    value.templates[0] = action_fixture();
    value.rule_binding_count = 2;
    value.rule_template_slots[0] = ActionTemplateSlot{1};
    value.rule_template_slots[1] = ActionTemplateSlot{0};
    return value;
}

void test_exact_sizes_and_round_trips() {
    using namespace velocita::v2;
    static_assert(kFeatureDescriptorEncodedBytes == 9);
    static_assert(kFeatureSchemaPayloadPrefixBytes == 44);
    static_assert(kMaxFeatureSchemaPayloadBytes == 332);
    static_assert(kActionTemplateEncodedBytes == 140);
    static_assert(kExecutablePolicyPayloadPrefixBytes == 124);
    static_assert(kMaxExecutablePolicyPayloadBytes == 1278);

    const auto schema = encode_payload(schema_fixture());
    require(schema.error == PayloadError::none && schema.bytes.size() == 71,
            "feature schema canonical size changed");
    const auto decoded_schema = decode_feature_schema_artifact(schema.bytes);
    require(decoded_schema.error == PayloadError::none &&
                decoded_schema.value == schema_fixture() &&
                encode_payload(*decoded_schema.value).bytes == schema.bytes,
            "feature schema did not round trip canonically");
    const auto schema_hash = to_hex(sha256_digest(schema.bytes));
    require(schema_hash ==
                "fdffdaf4820545ad8ab58b7e07e42bf83441e573f2cc5b6466b09d1a49575d27",
            "feature schema golden corpus changed: " + schema_hash);

    const auto executable = encode_payload(executable_fixture());
    require(executable.error == PayloadError::none && executable.bytes.size() == 268,
            "executable policy canonical size changed");
    const auto decoded_executable =
        decode_executable_policy_artifact(executable.bytes);
    require(decoded_executable.error == PayloadError::none &&
                decoded_executable.value == executable_fixture() &&
                encode_payload(*decoded_executable.value).bytes == executable.bytes,
            "executable policy did not round trip canonically");
    const auto executable_hash = to_hex(sha256_digest(executable.bytes));
    require(executable_hash ==
                "4930933b06c80157af8eafa00c4734bc4cd1df506a25edcbde63be4e6bcf09e4",
            "executable policy golden corpus changed: " + executable_hash);
}

void test_candidate_maxima_are_real_encodable_contracts() {
    using namespace velocita::v2;

    auto schema = schema_fixture();
    schema.feature_count = static_cast<std::uint16_t>(kMaxDecisionFeatures);
    for (std::size_t index = 0; index < schema.feature_count; ++index) {
        schema.features[index] = {FeatureEconomicKind::price, 1'000'000};
    }
    const auto schema_bytes = encode_payload(schema);
    require(schema_bytes.error == PayloadError::none &&
                schema_bytes.bytes.size() == kMaxFeatureSchemaPayloadBytes,
            "maximum feature schema did not reach its reviewed byte formula");
    require(decode_feature_schema_artifact(schema_bytes.bytes).error ==
                PayloadError::none,
            "maximum feature schema did not decode");

    auto executable = executable_fixture();
    executable.template_count =
        static_cast<std::uint16_t>(kMaxExecutableActionTemplates);
    for (std::size_t index = 0; index < executable.template_count; ++index) {
        executable.templates[index] = action_fixture();
        executable.templates[index].order_slot =
            LogicalOrderSlot{static_cast<std::uint16_t>(index + 1)};
    }
    executable.rule_binding_count = static_cast<std::uint16_t>(kMaxPolicyRules);
    for (std::size_t index = 0; index < executable.rule_binding_count; ++index) {
        executable.rule_template_slots[index] = ActionTemplateSlot{
            static_cast<std::uint8_t>(index % executable.template_count + 1)};
    }
    const auto executable_bytes = encode_payload(executable);
    require(executable_bytes.error == PayloadError::none &&
                executable_bytes.bytes.size() ==
                    kMaxExecutablePolicyPayloadBytes,
            "maximum executable policy did not reach its reviewed byte formula");
    require(decode_executable_policy_artifact(executable_bytes.bytes).error ==
                PayloadError::none,
            "maximum executable policy did not decode");
}

void test_feature_schema_fails_closed() {
    using namespace velocita::v2;
    auto bytes = encode_payload(schema_fixture()).bytes;
    bytes.pop_back();
    require(decode_feature_schema_artifact(bytes).error == PayloadError::truncated,
            "truncated feature schema was accepted");

    bytes = encode_payload(schema_fixture()).bytes;
    bytes.push_back(std::byte{0});
    require(decode_feature_schema_artifact(bytes).error ==
                PayloadError::length_mismatch,
            "feature schema trailing bytes were accepted");

    bytes = encode_payload(schema_fixture()).bytes;
    bytes[0] = std::byte{0xff};
    require(decode_feature_schema_artifact(bytes).error ==
                PayloadError::unsupported_schema,
            "unknown feature schema version was accepted");

    bytes = encode_payload(schema_fixture()).bytes;
    bytes[42] = std::byte{33};
    require(decode_feature_schema_artifact(bytes).error ==
                PayloadError::capacity_exceeded,
            "oversized feature count was accepted");
}

void test_executable_policy_fails_closed() {
    using namespace velocita::v2;
    auto bytes = encode_payload(executable_fixture()).bytes;
    bytes.pop_back();
    require(decode_executable_policy_artifact(bytes).error ==
                PayloadError::truncated,
            "truncated executable policy was accepted");

    bytes = encode_payload(executable_fixture()).bytes;
    bytes.push_back(std::byte{0});
    require(decode_executable_policy_artifact(bytes).error ==
                PayloadError::length_mismatch,
            "executable policy trailing bytes were accepted");

    bytes = encode_payload(executable_fixture()).bytes;
    bytes[0] = std::byte{0xff};
    require(decode_executable_policy_artifact(bytes).error ==
                PayloadError::unsupported_schema,
            "unknown executable policy version was accepted");

    bytes = encode_payload(executable_fixture()).bytes;
    bytes[122] = std::byte{9};
    require(decode_executable_policy_artifact(bytes).error ==
                PayloadError::capacity_exceeded,
            "oversized template count was accepted");

    bytes = encode_payload(executable_fixture()).bytes;
    bytes[124 + kActionTemplateEncodedBytes] = std::byte{33};
    require(decode_executable_policy_artifact(bytes).error ==
                PayloadError::capacity_exceeded,
            "oversized rule binding count was accepted");

    bytes = encode_payload(executable_fixture()).bytes;
    bytes[126] = std::byte{0xff};
    const auto invalid_directive = decode_executable_policy_artifact(bytes);
    require(invalid_directive.error == PayloadError::invalid_contract &&
                invalid_directive.contract_error == ContractError::invalid_action_template,
            "invalid embedded action directive was accepted or misclassified");
}

}  // namespace

int main() {
    try {
        test_exact_sizes_and_round_trips();
        test_candidate_maxima_are_real_encodable_contracts();
        test_feature_schema_fails_closed();
        test_executable_policy_fails_closed();
        std::cout << "all V2 executable-policy codec tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 executable-policy codec test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
