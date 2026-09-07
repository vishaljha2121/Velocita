#include "velocita/v2/action_template.hpp"

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

velocita::v2::PriceSource feature_price(const std::uint16_t index,
                                        const std::int64_t atoms_per_tick,
                                        const std::int64_t offset_ticks = 0) {
    using namespace velocita::v2;
    return {EconomicValueSourceKind::feature,
            index,
            RoundingMode::nearest_ties_away_from_zero,
            {},
            atoms_per_tick,
            PriceTicks::from_atoms(offset_ticks)};
}

velocita::v2::QuantitySource fixed_quantity(const std::int64_t lots) {
    using namespace velocita::v2;
    return {EconomicValueSourceKind::fixed,
            0,
            RoundingMode::toward_zero,
            QuantityLots::from_atoms(lots),
            0};
}

velocita::v2::ActionSafetyCaps valid_caps() {
    using namespace velocita::v2;
    return {QuantityLots::from_atoms(10),
            MoneyMicros::from_atoms(20'000'000),
            PriceTicks::from_atoms(5),
            PriceTicks::from_atoms(2),
            50'000'000,
            feature_price(1, 1'000'000)};
}

velocita::v2::ActionTemplate valid_place_template() {
    using namespace velocita::v2;
    return {kActionTemplateSchemaVersion,
            Directive::place_limit,
            Side::buy,
            TimeInForce::good_til_cancel,
            PostOnlyMode::required,
            LogicalOrderSlot{1},
            InstrumentSpecVersion{2},
            VenueCapabilitiesVersion{3},
            feature_price(0, 1'000'000, -1),
            fixed_quantity(2),
            valid_caps()};
}

velocita::v2::EconomicActionProposal valid_proposal() {
    using namespace velocita::v2;
    return {kEconomicActionProposalSchemaVersion,
            DecisionId{1},
            PolicyId{2},
            PolicyVersion{3},
            RiskPolicyVersion{4},
            InstrumentSpecVersion{5},
            VenueCapabilitiesVersion{6},
            AccountId{7},
            InstrumentId{8},
            VenueId{9},
            LogicalOrderSlot{10},
            Directive::place_limit,
            Side::buy,
            TimeInForce::good_til_cancel,
            PostOnlyMode::required,
            PriceTicks::from_atoms(100),
            QuantityLots::from_atoms(2),
            MoneyMicros::from_atoms(2'000'000)};
}

void test_typed_bounded_layout() {
    using namespace velocita::v2;
    static_assert(std::is_trivially_copyable_v<PriceSource>);
    static_assert(std::is_trivially_copyable_v<QuantitySource>);
    static_assert(std::is_trivially_copyable_v<ActionSafetyCaps>);
    static_assert(std::is_trivially_copyable_v<ActionTemplate>);
    static_assert(std::is_trivially_copyable_v<EconomicActionProposal>);
    static_assert(!std::is_constructible_v<LogicalOrderSlot,
                                           InstrumentSpecVersion>);
}

void test_value_source_canonical_forms() {
    using namespace velocita::v2;

    require(validate(feature_price(0, 1'000'000)) == ContractError::none,
            "valid feature-derived price was rejected");
    require(validate(fixed_quantity(2)) == ContractError::none,
            "valid fixed quantity was rejected");

    auto price = feature_price(kMaxDecisionFeatures, 1'000'000);
    require(validate(price) == ContractError::invalid_feature_index,
            "out-of-range price feature was accepted");

    price = feature_price(0, 0);
    require(validate(price) == ContractError::invalid_value_source,
            "zero feature conversion denominator was accepted");

    price = feature_price(0, 1'000'000);
    price.fixed_value = PriceTicks::from_atoms(100);
    require(validate(price) == ContractError::non_canonical_value,
            "feature source retained an inactive fixed value");

    auto quantity = fixed_quantity(0);
    require(validate(quantity) == ContractError::non_positive_quantity,
            "zero fixed quantity was accepted");

    quantity = fixed_quantity(2);
    quantity.feature_atoms_per_lot = 1'000'000;
    require(validate(quantity) == ContractError::non_canonical_value,
            "fixed quantity retained inactive feature conversion state");
}

void test_safety_caps_are_strict_and_bounded() {
    using namespace velocita::v2;
    auto caps = valid_caps();
    require(validate(caps) == ContractError::none,
            "valid strategy safety caps were rejected");

    caps.max_quantity = QuantityLots::from_atoms(0);
    require(validate(caps) == ContractError::non_positive_quantity,
            "zero maximum quantity was accepted");

    caps = valid_caps();
    caps.max_notional = MoneyMicros::from_atoms(0);
    require(validate(caps) == ContractError::non_positive_notional,
            "zero maximum notional was accepted");

    caps = valid_caps();
    caps.max_abs_price_deviation = PriceTicks::from_atoms(-1);
    require(validate(caps) == ContractError::invalid_safety_cap,
            "negative price-deviation cap was accepted");

    caps = valid_caps();
    caps.max_market_data_age_ns = -1;
    require(validate(caps) == ContractError::invalid_age,
            "negative action freshness cap was accepted");

    caps = valid_caps();
    caps.reference_price.kind = EconomicValueSourceKind::fixed;
    caps.reference_price.fixed_value = PriceTicks::from_atoms(100);
    caps.reference_price.feature_atoms_per_tick = 0;
    caps.reference_price.feature_index = 0;
    caps.reference_price.rounding = RoundingMode::toward_zero;
    require(validate(caps) == ContractError::invalid_safety_cap,
            "non-feature reference price was accepted");

    caps = valid_caps();
    caps.reference_price.tick_offset = PriceTicks::from_atoms(1);
    require(validate(caps) == ContractError::invalid_safety_cap,
            "deviation reference price accepted a manipulable tick offset");
}

void test_action_template_directive_specific_shapes() {
    using namespace velocita::v2;
    auto action = valid_place_template();
    require(validate(action) == ContractError::none,
            "valid place-limit template was rejected");

    action.schema_version += 1;
    require(validate(action) == ContractError::unsupported_schema,
            "unsupported action-template schema was accepted");

    action = valid_place_template();
    action.order_slot = {};
    require(validate(action) == ContractError::invalid_order_target,
            "zero logical order slot was accepted");

    action = valid_place_template();
    action.directive = Directive::no_action;
    require(validate(action) == ContractError::invalid_action_template,
            "no-action was accepted as an economic template");

    action = valid_place_template();
    action.time_in_force = TimeInForce::post_only;
    require(validate(action) == ContractError::invalid_enum,
            "legacy post-only TIF duplicated the Phase 3D post-only field");

    action = valid_place_template();
    action.time_in_force = TimeInForce::immediate_or_cancel;
    require(validate(action) == ContractError::invalid_action_template,
            "IOC plus required post-only was accepted");

    ActionTemplate cancel{};
    cancel.schema_version = kActionTemplateSchemaVersion;
    cancel.directive = Directive::cancel;
    cancel.side = Side::sell;
    cancel.post_only = PostOnlyMode::prohibited;
    cancel.order_slot = LogicalOrderSlot{4};
    cancel.instrument_spec_version = InstrumentSpecVersion{5};
    cancel.venue_capabilities_version = VenueCapabilitiesVersion{6};
    require(validate(cancel) == ContractError::none,
            "canonical cancel template was rejected");

    cancel.quantity_source = fixed_quantity(1);
    require(validate(cancel) == ContractError::non_canonical_value,
            "cancel template retained an inactive quantity source");
}

void test_proposal_is_exact_and_never_implicitly_clamped() {
    using namespace velocita::v2;
    auto proposal = valid_proposal();
    require(validate(proposal) == ContractError::none,
            "valid exact economic proposal was rejected");

    proposal.quantity = QuantityLots::from_atoms(0);
    require(validate(proposal) == ContractError::non_positive_quantity,
            "zero proposal quantity was accepted");

    proposal = valid_proposal();
    proposal.notional = MoneyMicros::from_atoms(0);
    require(validate(proposal) == ContractError::non_positive_notional,
            "zero proposal notional was accepted");

    proposal = valid_proposal();
    proposal.venue_capabilities_version = {};
    require(validate(proposal) == ContractError::invalid_identity,
            "proposal without capability provenance was accepted");
}

}  // namespace

int main() {
    try {
        test_typed_bounded_layout();
        test_value_source_canonical_forms();
        test_safety_caps_are_strict_and_bounded();
        test_action_template_directive_specific_shapes();
        test_proposal_is_exact_and_never_implicitly_clamped();
    } catch (const std::exception& error) {
        std::cerr << "V2 action-template contract test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
