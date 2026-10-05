#include "velocita/v2/artifact_digest.hpp"

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

velocita::v2::FeatureSchemaArtifact unsigned_schema() {
    using namespace velocita::v2;
    FeatureSchemaArtifact value;
    value.schema_version = kFeatureSchemaArtifactSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{3};
    value.feature_count = 3;
    value.features[0] = {FeatureEconomicKind::price, 1'000'000};
    value.features[1] = {FeatureEconomicKind::price, 1'000'000};
    value.features[2] = {FeatureEconomicKind::quantity, 1'000'000};
    return value;
}

velocita::v2::PriceSource feature_price(const std::uint16_t index) {
    using namespace velocita::v2;
    return {EconomicValueSourceKind::feature, index,
            RoundingMode::toward_zero, {}, 1'000'000, {}};
}

velocita::v2::ActionTemplate action() {
    using namespace velocita::v2;
    ActionTemplate value;
    value.schema_version = kActionTemplateSchemaVersion;
    value.directive = Directive::place_limit;
    value.side = Side::buy;
    value.time_in_force = TimeInForce::good_til_cancel;
    value.post_only = PostOnlyMode::required;
    value.order_slot = LogicalOrderSlot{1};
    value.instrument_spec_version = InstrumentSpecVersion{20};
    value.venue_capabilities_version = VenueCapabilitiesVersion{30};
    value.price_source = feature_price(0);
    value.quantity_source = {EconomicValueSourceKind::fixed, 0,
                             RoundingMode::toward_zero,
                             QuantityLots::from_atoms(2), 0};
    value.safety_caps.max_quantity = QuantityLots::from_atoms(5);
    value.safety_caps.max_notional = MoneyMicros::from_atoms(5'000'000);
    value.safety_caps.max_abs_price_deviation = PriceTicks::from_atoms(2);
    value.safety_caps.max_abs_tick_offset = PriceTicks::from_atoms(1);
    value.safety_caps.max_market_data_age_ns = 50'000;
    value.safety_caps.reference_price = feature_price(1);
    return value;
}

velocita::v2::ExecutablePolicyArtifact unsigned_executable(
    const velocita::v2::Digest256 schema_digest) {
    using namespace velocita::v2;
    ExecutablePolicyArtifact value;
    value.schema_version = kExecutablePolicyArtifactSchemaVersion;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{2};
    value.policy_artifact_digest = digest(std::byte{0x12});
    value.feature_schema_id = FeatureSchemaId{3};
    value.feature_schema_digest = schema_digest;
    value.template_count = 1;
    value.templates[0] = action();
    value.rule_binding_count = 2;
    value.rule_template_slots[0] = ActionTemplateSlot{1};
    value.rule_template_slots[1] = ActionTemplateSlot{0};
    return value;
}

void test_domains_and_golden_digests() {
    using namespace velocita::v2;
    static_assert(kFeatureSchemaDigestDomain != kExecutablePolicyDigestDomain);

    auto schema = unsigned_schema();
    const auto schema_result = compute_artifact_digest(schema);
    require(schema_result.error == ArtifactDigestError::none,
            "valid unsigned feature schema did not hash");
    const auto schema_hash = to_hex(schema_result.digest);
    require(schema_hash ==
                "95f7c0a99a1098eccff3652c9ce66404220fd5768f6b6febbc3bf4191a9bb690",
            "feature-schema artifact digest corpus changed: " + schema_hash);
    schema.artifact_digest = schema_result.digest;
    require(verify_artifact_digest(schema).error == ArtifactDigestError::none,
            "correct feature-schema digest did not verify");

    auto executable = unsigned_executable(schema.artifact_digest);
    const auto executable_result = compute_artifact_digest(executable);
    require(executable_result.error == ArtifactDigestError::none,
            "valid unsigned executable policy did not hash");
    const auto executable_hash = to_hex(executable_result.digest);
    require(executable_hash ==
                "497298b19ad71de9519835f249fe1e68e3c386f87c36c774998805b2e4885dd6",
            "executable-policy artifact digest corpus changed: " +
                executable_hash);
    executable.artifact_digest = executable_result.digest;
    require(verify_artifact_digest(executable).error == ArtifactDigestError::none,
            "correct executable-policy digest did not verify");
}

void test_self_digest_is_excluded_but_authority_is_covered() {
    using namespace velocita::v2;
    auto schema = unsigned_schema();
    const auto first = compute_artifact_digest(schema);
    schema.artifact_digest = digest(std::byte{0xee});
    const auto second = compute_artifact_digest(schema);
    require(first.error == ArtifactDigestError::none &&
                second.error == ArtifactDigestError::none &&
                first.digest == second.digest,
            "self digest leaked into its own preimage");

    schema.artifact_digest = first.digest;
    schema.features[0].atoms_per_output_unit = 2'000'000;
    require(verify_artifact_digest(schema).error ==
                ArtifactDigestError::digest_mismatch,
            "feature conversion tamper was not detected");

    schema = unsigned_schema();
    schema.artifact_digest = compute_artifact_digest(schema).digest;
    auto executable = unsigned_executable(schema.artifact_digest);
    executable.artifact_digest = compute_artifact_digest(executable).digest;

    auto tampered = executable;
    tampered.policy_artifact_digest = digest(std::byte{0x13});
    require(verify_artifact_digest(tampered).error ==
                ArtifactDigestError::digest_mismatch,
            "base-policy digest tamper was not detected");

    tampered = executable;
    tampered.templates[0].safety_caps.max_quantity =
        QuantityLots::from_atoms(6);
    require(verify_artifact_digest(tampered).error ==
                ArtifactDigestError::digest_mismatch,
            "safety-cap tamper was not detected");

    tampered = executable;
    tampered.rule_template_slots[1] = ActionTemplateSlot{1};
    require(verify_artifact_digest(tampered).error ==
                ArtifactDigestError::digest_mismatch,
            "valid rule-binding tamper was not detected");

    tampered = executable;
    tampered.rule_template_slots[0] = ActionTemplateSlot{0};
    require(verify_artifact_digest(tampered).error ==
                ArtifactDigestError::invalid_contract,
            "structurally invalid binding was misclassified as digest mismatch");
}

void test_invalid_unsigned_content_fails_before_hashing() {
    using namespace velocita::v2;
    auto schema = unsigned_schema();
    schema.features[0].kind = static_cast<FeatureEconomicKind>(0xff);
    const auto schema_result = compute_artifact_digest(schema);
    require(schema_result.error == ArtifactDigestError::invalid_contract &&
                schema_result.contract_error == ContractError::invalid_enum &&
                schema_result.digest.is_zero(),
            "invalid schema content produced an authoritative digest");

    auto executable = unsigned_executable(digest(std::byte{0x31}));
    executable.feature_schema_digest = {};
    const auto executable_result = compute_artifact_digest(executable);
    require(executable_result.error == ArtifactDigestError::invalid_contract &&
                executable_result.contract_error == ContractError::missing_digest,
            "missing bound feature digest was accepted while hashing");
}

}

int main() {
    try {
        test_domains_and_golden_digests();
        test_self_digest_is_excluded_but_authority_is_covered();
        test_invalid_unsigned_content_fails_before_hashing();
        std::cout << "all V2 artifact-digest tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 artifact-digest test failed: " << error.what() << '\n';
        return 1;
    }
}
