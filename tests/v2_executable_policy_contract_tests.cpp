#include "velocita/v2/executable_policy.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

velocita::v2::Digest256 digest(const std::byte marker) {
    velocita::v2::Digest256 value;
    value.bytes[0] = marker;
    return value;
}

velocita::v2::PriceSource feature_price(const std::uint16_t index) {
    using namespace velocita::v2;
    return {EconomicValueSourceKind::feature,
            index,
            RoundingMode::toward_zero,
            {},
            1'000'000,
            {}};
}

velocita::v2::QuantitySource feature_quantity(const std::uint16_t index) {
    using namespace velocita::v2;
    return {EconomicValueSourceKind::feature,
            index,
            RoundingMode::toward_zero,
            {},
            1'000'000};
}

velocita::v2::ActionTemplate action(const velocita::v2::Directive directive,
                                    const std::uint16_t slot) {
    using namespace velocita::v2;
    ActionTemplate value;
    value.schema_version = kActionTemplateSchemaVersion;
    value.directive = directive;
    value.side = Side::buy;
    value.time_in_force = TimeInForce::good_til_cancel;
    value.post_only = PostOnlyMode::required;
    value.order_slot = LogicalOrderSlot{slot};
    value.instrument_spec_version = InstrumentSpecVersion{20};
    value.venue_capabilities_version = VenueCapabilitiesVersion{30};
    value.price_source = feature_price(0);
    value.quantity_source = feature_quantity(2);
    value.safety_caps.max_quantity = QuantityLots::from_atoms(5);
    value.safety_caps.max_notional = MoneyMicros::from_atoms(5'000'000);
    value.safety_caps.max_abs_price_deviation = PriceTicks::from_atoms(2);
    value.safety_caps.max_abs_tick_offset = PriceTicks::from_atoms(1);
    value.safety_caps.max_market_data_age_ns = 50'000;
    value.safety_caps.reference_price = feature_price(1);
    return value;
}

velocita::v2::PolicyArtifact policy() {
    using namespace velocita::v2;
    PolicyArtifact value;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{2};
    value.schema_version = kPolicyArtifactSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{3};
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.model_digest = digest(std::byte{0x11});
    value.artifact_digest = digest(std::byte{0x12});
    value.valid_from_event_time_ns = 100;
    value.valid_until_event_time_ns = 10'000;
    value.risk_policy_version = RiskPolicyVersion{6};
    value.rule_count = 3;
    value.rules[0] = PolicyRule{0, PredicateOperator::greater_than,
                                FeatureMicros::from_atoms(1),
                                Directive::place_limit};
    value.rules[1] = PolicyRule{0, PredicateOperator::less_than,
                                FeatureMicros::from_atoms(-1),
                                Directive::replace};
    value.rules[2] = PolicyRule{1, PredicateOperator::equal,
                                FeatureMicros::from_atoms(0),
                                Directive::no_action};
    return value;
}

velocita::v2::FeatureSchemaArtifact feature_schema() {
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

velocita::v2::ExecutablePolicyArtifact executable_policy() {
    using namespace velocita::v2;
    ExecutablePolicyArtifact value;
    value.schema_version = kExecutablePolicyArtifactSchemaVersion;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{2};
    value.policy_artifact_digest = digest(std::byte{0x12});
    value.feature_schema_id = FeatureSchemaId{3};
    value.feature_schema_digest = digest(std::byte{0x31});
    value.artifact_digest = digest(std::byte{0x41});
    value.template_count = 2;
    value.templates[0] = action(Directive::place_limit, 1);
    value.templates[1] = action(Directive::replace, 2);
    value.rule_binding_count = 3;
    value.rule_template_slots[0] = ActionTemplateSlot{1};
    value.rule_template_slots[1] = ActionTemplateSlot{2};
    value.rule_template_slots[2] = ActionTemplateSlot{0};
    return value;
}

void test_candidate_bounds_and_valid_contracts() {
    using namespace velocita::v2;
    static_assert(kMaxExecutablePolicyArtifacts == 2);
    static_assert(kMaxExecutableActionTemplates == 8);
    static_assert(kMaxPolicyRules == 32);
    static_assert(kMaxDecisionFeatures == 32);
    static_assert(std::is_trivially_copyable_v<FeatureDescriptor>);
    static_assert(std::is_trivially_copyable_v<FeatureSchemaArtifact>);
    static_assert(std::is_trivially_copyable_v<ExecutablePolicyArtifact>);
    static_assert(!std::is_constructible_v<ActionTemplateSlot,
                                           LogicalOrderSlot>);

    require(validate(feature_schema()) == ContractError::none,
            "valid feature schema was rejected");
    require(validate(executable_policy()) == ContractError::none,
            "valid executable policy was rejected");
    require(bind_executable_policy(policy(), feature_schema(),
                                   executable_policy()) ==
                ExecutablePolicyBindingCode::bound,
            "compatible executable policy did not bind");
}

void test_feature_schema_is_closed_and_canonical() {
    using namespace velocita::v2;
    auto schema = feature_schema();
    schema.feature_count = kMaxDecisionFeatures + 1;
    require(validate(schema) == ContractError::capacity_exceeded,
            "oversized feature schema was accepted");

    schema = feature_schema();
    schema.features[0].kind = static_cast<FeatureEconomicKind>(0xff);
    require(validate(schema) == ContractError::invalid_enum,
            "unknown feature economic kind was accepted");

    schema = feature_schema();
    schema.features[0].atoms_per_output_unit = 0;
    require(validate(schema) == ContractError::invalid_value_source,
            "economic feature without a conversion was accepted");

    schema = feature_schema();
    schema.features[0] = {FeatureEconomicKind::generic, 1};
    require(validate(schema) == ContractError::non_canonical_value,
            "generic feature retained an inactive conversion");

    schema = feature_schema();
    schema.features[3] = {FeatureEconomicKind::price, 1'000'000};
    require(validate(schema) == ContractError::non_canonical_value,
            "unused feature descriptor was not zero");
}

void test_template_slots_are_bounded_deduplicated_and_canonical() {
    using namespace velocita::v2;
    auto executable = executable_policy();
    executable.rule_template_slots[0] = ActionTemplateSlot{3};
    require(validate(executable) == ContractError::invalid_action_template,
            "out-of-range action-template slot was accepted");

    executable = executable_policy();
    executable.rule_template_slots[1] = ActionTemplateSlot{1};
    require(validate(executable) == ContractError::invalid_action_template,
            "unreferenced template was accepted");

    executable = executable_policy();
    executable.templates[1] = executable.templates[0];
    require(validate(executable) == ContractError::non_canonical_value,
            "duplicate template was accepted");

    executable = executable_policy();
    executable.templates[2] = action(Directive::replace, 3);
    require(validate(executable) == ContractError::non_canonical_value,
            "unused template storage was not canonical zero");

    executable = executable_policy();
    executable.rule_template_slots[3] = ActionTemplateSlot{1};
    require(validate(executable) == ContractError::non_canonical_value,
            "unused rule binding storage was not canonical zero");
}

void test_base_policy_binding_and_directive_consistency() {
    using namespace velocita::v2;
    auto executable = executable_policy();
    executable.policy_artifact_digest = digest(std::byte{0x7f});
    require(bind_executable_policy(policy(), feature_schema(), executable) ==
                ExecutablePolicyBindingCode::policy_digest_mismatch,
            "wrong base-policy digest was accepted");

    executable = executable_policy();
    executable.rule_binding_count = 2;
    executable.rule_template_slots[2] = {};
    require(validate(executable) == ContractError::none,
            "structurally valid short binding table was rejected early");
    require(bind_executable_policy(policy(), feature_schema(), executable) ==
                ExecutablePolicyBindingCode::rule_count_mismatch,
            "rule count mismatch was accepted");

    executable = executable_policy();
    executable.rule_template_slots[0] = {};
    require(validate(executable) == ContractError::invalid_action_template,
            "structural unreferenced-template invariant was bypassed");
    executable.template_count = 1;
    executable.templates[1] = {};
    executable.rule_template_slots[1] = ActionTemplateSlot{1};
    require(validate(executable) == ContractError::none,
            "zero slot could not represent no-action structurally");
    require(bind_executable_policy(policy(), feature_schema(), executable) ==
                ExecutablePolicyBindingCode::action_binding_missing,
            "economic rule was allowed to map to slot zero");

    executable = executable_policy();
    executable.rule_template_slots[2] = ActionTemplateSlot{1};
    require(bind_executable_policy(policy(), feature_schema(), executable) ==
                ExecutablePolicyBindingCode::no_action_binding_mismatch,
            "no-action rule acquired an economic template");

    executable = executable_policy();
    executable.templates[0].directive = Directive::replace;
    require(bind_executable_policy(policy(), feature_schema(), executable) ==
                ExecutablePolicyBindingCode::directive_mismatch,
            "rule/template directive mismatch was accepted");
}

void test_feature_units_and_conversions_are_authoritative() {
    using namespace velocita::v2;
    auto schema = feature_schema();
    schema.features[0].kind = FeatureEconomicKind::quantity;
    require(bind_executable_policy(policy(), schema, executable_policy()) ==
                ExecutablePolicyBindingCode::feature_kind_mismatch,
            "quantity feature was accepted as a price");

    schema = feature_schema();
    schema.features[2].atoms_per_output_unit = 2'000'000;
    require(bind_executable_policy(policy(), schema, executable_policy()) ==
                ExecutablePolicyBindingCode::feature_conversion_mismatch,
            "template conversion disagreed with authoritative feature schema");

    schema = feature_schema();
    schema.artifact_digest = digest(std::byte{0x32});
    require(bind_executable_policy(policy(), schema, executable_policy()) ==
                ExecutablePolicyBindingCode::feature_schema_digest_mismatch,
            "wrong feature-schema digest was accepted");
}

}  // namespace

int main() {
    try {
        test_candidate_bounds_and_valid_contracts();
        test_feature_schema_is_closed_and_canonical();
        test_template_slots_are_bounded_deduplicated_and_canonical();
        test_base_policy_binding_and_directive_consistency();
        test_feature_units_and_conversions_are_authoritative();
    } catch (const std::exception& error) {
        std::cerr << "V2 executable-policy contract test failure: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
