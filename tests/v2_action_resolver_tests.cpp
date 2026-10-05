#include "velocita/v2/action_resolver.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
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

velocita::v2::PriceSource feature_price(const std::uint16_t index,
                                        const std::int64_t atoms_per_tick,
                                        const std::int64_t offset = 0,
                                        const velocita::v2::RoundingMode rounding =
                                            velocita::v2::RoundingMode::toward_zero) {
    using namespace velocita::v2;
    return {EconomicValueSourceKind::feature,
            index,
            rounding,
            {},
            atoms_per_tick,
            PriceTicks::from_atoms(offset)};
}

velocita::v2::QuantitySource feature_quantity(
    const std::uint16_t index,
    const std::int64_t atoms_per_lot,
    const velocita::v2::RoundingMode rounding =
        velocita::v2::RoundingMode::toward_zero) {
    using namespace velocita::v2;
    return {EconomicValueSourceKind::feature,
            index,
            rounding,
            {},
            atoms_per_lot};
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
    value.rule_count = 1;
    value.rules[0] = PolicyRule{0, PredicateOperator::greater_than,
                                FeatureMicros::from_atoms(1),
                                Directive::place_limit};
    return value;
}

velocita::v2::DecisionFrame frame() {
    using namespace velocita::v2;
    DecisionFrame value;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{2};
    value.feature_schema_id = FeatureSchemaId{3};
    value.venue_id = VenueId{7};
    value.venue_session_id = VenueSessionId{8};
    value.venue_sequence = VenueSequence{9};
    value.source_id = SourceId{11};
    value.source_generation = SourceGeneration{1};
    value.source_sequence = SourceSequence{1};
    value.event_time_ns = 1'000;
    value.receive_time_ns = 1'001;
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.feature_count = 3;
    value.features[0] = FeatureMicros::from_atoms(100'500'000);
    value.features[1] = FeatureMicros::from_atoms(100'000'000);
    value.features[2] = FeatureMicros::from_atoms(2'000'000);
    value.market_data_status = MarketDataStatus::ready;
    value.market_data_age_ns = 10'000;
    value.position_version = PositionVersion{12};
    value.risk_version = RiskVersion{13};
    value.raw_evidence_digest = digest(std::byte{0x21});
    return value;
}

velocita::v2::ActionTemplate place_template() {
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
    value.price_source = feature_price(
        0, 1'000'000, -1, RoundingMode::nearest_ties_away_from_zero);
    value.quantity_source = feature_quantity(2, 1'000'000);
    value.safety_caps.max_quantity = QuantityLots::from_atoms(5);
    value.safety_caps.max_notional = MoneyMicros::from_atoms(5'000'000);
    value.safety_caps.max_abs_price_deviation = PriceTicks::from_atoms(2);
    value.safety_caps.max_abs_tick_offset = PriceTicks::from_atoms(1);
    value.safety_caps.max_market_data_age_ns = 50'000;
    value.safety_caps.reference_price = feature_price(1, 1'000'000);
    return value;
}

velocita::v2::InstrumentExecutionProfile profile() {
    using namespace velocita::v2;
    return {kInstrumentExecutionProfileSchemaVersion,
            VenueId{7},
            InstrumentId{4},
            InstrumentSpecVersion{20},
            MoneyMicros::from_atoms(10'000),
            PriceTicks::from_atoms(1),
            PriceTicks::from_atoms(1'000),
            QuantityLots::from_atoms(1),
            QuantityLots::from_atoms(10)};
}

velocita::v2::VenueCapabilities capabilities() {
    using namespace velocita::v2;
    return {kVenueCapabilitiesSchemaVersion,
            VenueId{7},
            VenueCapabilitiesVersion{30},
            static_cast<std::uint8_t>(kVenueDirectivePlaceLimit |
                                      kVenueDirectiveCancel |
                                      kVenueDirectiveReplace),
            static_cast<std::uint8_t>(kVenueTifGtc | kVenueTifIoc |
                                      kVenueTifFok),
            1};
}

velocita::v2::ActionResolution resolve(
    const velocita::v2::DecisionId decision_id,
    const velocita::v2::PolicyArtifact& policy_value,
    const velocita::v2::DecisionFrame& frame_value,
    const velocita::v2::ActionTemplate& action,
    const velocita::v2::InstrumentExecutionProfile& instrument,
    const velocita::v2::VenueCapabilities& venue) {
    return velocita::v2::resolve_action(decision_id, policy_value, frame_value,
                                        action, instrument, venue);
}

void test_exact_feature_resolution_and_rounding() {
    using namespace velocita::v2;
    const auto result = resolve(DecisionId{40}, policy(), frame(),
                                place_template(), profile(), capabilities());
    require(result.code == ActionResolutionCode::proposed,
            "valid economic action did not resolve");
    require(result.proposal.price == PriceTicks::from_atoms(100) &&
                result.proposal.quantity == QuantityLots::from_atoms(2) &&
                result.proposal.notional == MoneyMicros::from_atoms(2'000'000),
            "resolved proposal did not preserve exact requested economics");
    require(result.proposal.decision_id == DecisionId{40} &&
                result.proposal.policy_id == PolicyId{10} &&
                result.proposal.risk_policy_version == RiskPolicyVersion{6},
            "proposal lost authoritative provenance");

    auto negative_tie = frame();
    negative_tie.features[0] = FeatureMicros::from_atoms(-100'500'000);
    auto action = place_template();
    action.price_source.tick_offset = PriceTicks::from_atoms(102);
    action.safety_caps.max_abs_tick_offset = PriceTicks::from_atoms(102);
    action.safety_caps.max_abs_price_deviation = PriceTicks::from_atoms(1);
    negative_tie.features[1] = FeatureMicros::from_atoms(1'000'000);
    const auto rounded = resolve(DecisionId{41}, policy(), negative_tie, action,
                                 profile(), capabilities());
    require(rounded.code == ActionResolutionCode::proposed &&
                rounded.proposal.price == PriceTicks::from_atoms(1),
            "nearest ties away from zero was not deterministic for negatives");
}

void test_fixed_sources_and_inclusive_cap_boundaries() {
    using namespace velocita::v2;
    auto action = place_template();
    action.price_source = {EconomicValueSourceKind::fixed,
                           0,
                           RoundingMode::toward_zero,
                           PriceTicks::from_atoms(100),
                           0,
                           {}};
    action.quantity_source = {EconomicValueSourceKind::fixed,
                              0,
                              RoundingMode::toward_zero,
                              QuantityLots::from_atoms(2),
                              0};
    action.safety_caps.max_quantity = QuantityLots::from_atoms(2);
    action.safety_caps.max_notional = MoneyMicros::from_atoms(2'000'000);
    action.safety_caps.max_abs_price_deviation = PriceTicks::from_atoms(0);
    action.safety_caps.max_abs_tick_offset = PriceTicks::from_atoms(0);

    const auto result = resolve(DecisionId{42}, policy(), frame(), action,
                                profile(), capabilities());
    require(result.code == ActionResolutionCode::proposed &&
                result.proposal.price == PriceTicks::from_atoms(100) &&
                result.proposal.quantity == QuantityLots::from_atoms(2) &&
                result.proposal.notional == MoneyMicros::from_atoms(2'000'000),
            "inclusive policy caps rejected an exact-boundary fixed action");
}

void test_structural_scope_and_version_precedence() {
    using namespace velocita::v2;
    auto invalid_policy = policy();
    invalid_policy.policy_id = {};
    auto mismatched_frame = frame();
    mismatched_frame.account_id = AccountId{99};
    auto result = resolve(DecisionId{40}, invalid_policy, mismatched_frame,
                          place_template(), profile(), capabilities());
    require(result.code == ActionResolutionCode::invalid_policy &&
                result.contract_error == ContractError::invalid_identity,
            "input-contract rejection precedence changed");

    result = resolve(DecisionId{}, policy(), frame(), place_template(), profile(),
                     capabilities());
    require(result.code == ActionResolutionCode::invalid_decision_id,
            "zero decision identity was accepted");

    auto wrong_scope = frame();
    wrong_scope.account_id = AccountId{99};
    result = resolve(DecisionId{40}, policy(), wrong_scope, place_template(),
                     profile(), capabilities());
    require(result.code == ActionResolutionCode::policy_scope_mismatch,
            "policy/frame scope mismatch was not typed");

    auto wrong_profile = profile();
    wrong_profile.instrument_spec_version = InstrumentSpecVersion{21};
    result = resolve(DecisionId{40}, policy(), frame(), place_template(),
                     wrong_profile, capabilities());
    require(result.code == ActionResolutionCode::instrument_version_mismatch,
            "instrument version mismatch was not rejected");

    auto wrong_capabilities = capabilities();
    wrong_capabilities.venue_capabilities_version =
        VenueCapabilitiesVersion{31};
    result = resolve(DecisionId{40}, policy(), frame(), place_template(),
                     profile(), wrong_capabilities);
    require(result.code == ActionResolutionCode::capability_version_mismatch,
            "capability version mismatch was not rejected");
}

void test_capability_and_freshness_rejections() {
    using namespace velocita::v2;
    auto venue = capabilities();
    venue.directive_mask = kVenueDirectiveCancel;
    venue.supports_post_only = 0;
    auto result = resolve(DecisionId{40}, policy(), frame(), place_template(),
                          profile(), venue);
    require(result.code == ActionResolutionCode::unsupported_directive,
            "unsupported directive was accepted");

    venue = capabilities();
    venue.time_in_force_mask = kVenueTifIoc;
    result = resolve(DecisionId{40}, policy(), frame(), place_template(),
                     profile(), venue);
    require(result.code == ActionResolutionCode::unsupported_time_in_force,
            "unsupported TIF was accepted");

    venue = capabilities();
    venue.supports_post_only = 0;
    result = resolve(DecisionId{40}, policy(), frame(), place_template(),
                     profile(), venue);
    require(result.code == ActionResolutionCode::unsupported_post_only,
            "unsupported post-only intent was silently downgraded");

    auto stale = frame();
    stale.market_data_age_ns = 50'001;
    result = resolve(DecisionId{40}, policy(), stale, place_template(), profile(),
                     capabilities());
    require(result.code == ActionResolutionCode::market_data_stale,
            "action freshness cap was not enforced");

    stale = frame();
    stale.market_data_status = MarketDataStatus::stale;
    result = resolve(DecisionId{40}, policy(), stale, place_template(), profile(),
                     capabilities());
    require(result.code == ActionResolutionCode::market_data_not_ready,
            "non-ready market data was accepted");
}

void test_arithmetic_bounds_and_policy_caps_reject_without_clamping() {
    using namespace velocita::v2;
    auto action = place_template();
    action.price_source.feature_index = 3;
    auto result = resolve(DecisionId{40}, policy(), frame(), action, profile(),
                          capabilities());
    require(result.code == ActionResolutionCode::feature_unavailable,
            "missing action feature was not rejected");

    action = place_template();
    action.price_source.tick_offset =
        PriceTicks::from_atoms(std::numeric_limits<std::int64_t>::max());
    action.safety_caps.max_abs_tick_offset =
        PriceTicks::from_atoms(std::numeric_limits<std::int64_t>::max());
    result = resolve(DecisionId{40}, policy(), frame(), action, profile(),
                     capabilities());
    require(result.code == ActionResolutionCode::arithmetic_overflow,
            "price offset overflow was not rejected");

    action = place_template();
    action.price_source.tick_offset = PriceTicks::from_atoms(2);
    result = resolve(DecisionId{40}, policy(), frame(), action, profile(),
                     capabilities());
    require(result.code == ActionResolutionCode::tick_offset_exceeded,
            "excess offset was silently accepted or clamped");

    action = place_template();
    action.safety_caps.max_abs_price_deviation = PriceTicks::from_atoms(0);
    auto deviating_frame = frame();
    deviating_frame.features[0] = FeatureMicros::from_atoms(101'500'000);
    result = resolve(DecisionId{40}, policy(), deviating_frame, action, profile(),
                     capabilities());
    require(result.code == ActionResolutionCode::price_deviation_exceeded,
            "price deviation was silently accepted or clamped");

    action = place_template();
    action.safety_caps.max_quantity = QuantityLots::from_atoms(1);
    result = resolve(DecisionId{40}, policy(), frame(), action, profile(),
                     capabilities());
    require(result.code == ActionResolutionCode::quantity_cap_exceeded &&
                result.proposal == EconomicActionProposal{},
            "quantity violation did not return a canonical empty proposal");

    action = place_template();
    action.safety_caps.max_notional = MoneyMicros::from_atoms(1'999'999);
    result = resolve(DecisionId{40}, policy(), frame(), action, profile(),
                     capabilities());
    require(result.code == ActionResolutionCode::notional_cap_exceeded &&
                result.proposal == EconomicActionProposal{},
            "notional violation was silently clamped");

    auto huge = profile();
    huge.quote_micros_per_tick_lot =
        MoneyMicros::from_atoms(std::numeric_limits<std::int64_t>::max());
    result = resolve(DecisionId{40}, policy(), frame(), place_template(), huge,
                     capabilities());
    require(result.code == ActionResolutionCode::arithmetic_overflow,
            "notional multiplication overflow was not rejected");
}

void test_instrument_bounds_and_cancel_safety_path() {
    using namespace velocita::v2;
    auto instrument = profile();
    instrument.maximum_price = PriceTicks::from_atoms(99);
    auto result = resolve(DecisionId{40}, policy(), frame(), place_template(),
                          instrument, capabilities());
    require(result.code == ActionResolutionCode::price_out_of_bounds,
            "instrument price bound was not enforced");

    instrument = profile();
    instrument.maximum_quantity = QuantityLots::from_atoms(1);
    result = resolve(DecisionId{40}, policy(), frame(), place_template(),
                     instrument, capabilities());
    require(result.code == ActionResolutionCode::quantity_out_of_bounds,
            "instrument quantity bound was not enforced");

    ActionTemplate cancel{};
    cancel.schema_version = kActionTemplateSchemaVersion;
    cancel.directive = Directive::cancel;
    cancel.side = Side::sell;
    cancel.post_only = PostOnlyMode::prohibited;
    cancel.order_slot = LogicalOrderSlot{2};
    cancel.instrument_spec_version = InstrumentSpecVersion{20};
    cancel.venue_capabilities_version = VenueCapabilitiesVersion{30};
    auto degraded = frame();
    degraded.market_data_status = MarketDataStatus::stale;
    degraded.market_data_age_ns = std::numeric_limits<std::int64_t>::max();
    result = resolve(DecisionId{41}, policy(), degraded, cancel, profile(),
                     capabilities());
    require(result.code == ActionResolutionCode::proposed &&
                result.proposal.directive == Directive::cancel &&
                result.proposal.price == PriceTicks{} &&
                result.proposal.quantity == QuantityLots{} &&
                result.proposal.notional == MoneyMicros{},
            "stale market data blocked the bounded cancel safety path");
}

}

int main() {
    try {
        test_exact_feature_resolution_and_rounding();
        test_fixed_sources_and_inclusive_cap_boundaries();
        test_structural_scope_and_version_precedence();
        test_capability_and_freshness_rejections();
        test_arithmetic_bounds_and_policy_caps_reject_without_clamping();
        test_instrument_bounds_and_cancel_safety_path();
    } catch (const std::exception& error) {
        std::cerr << "V2 action resolver test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
