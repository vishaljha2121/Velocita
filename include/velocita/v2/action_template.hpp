#pragma once

#include "velocita/v2/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace velocita::v2 {

inline constexpr std::uint16_t kActionTemplateSchemaVersion = 1;
inline constexpr std::uint16_t kEconomicActionProposalSchemaVersion = 1;
inline constexpr std::uint16_t kInstrumentExecutionProfileSchemaVersion = 1;
inline constexpr std::uint16_t kVenueCapabilitiesSchemaVersion = 1;

inline constexpr std::uint8_t kVenueDirectivePlaceLimit = 1U << 0U;
inline constexpr std::uint8_t kVenueDirectiveCancel = 1U << 1U;
inline constexpr std::uint8_t kVenueDirectiveReplace = 1U << 2U;
inline constexpr std::uint8_t kKnownVenueDirectiveMask =
    kVenueDirectivePlaceLimit | kVenueDirectiveCancel | kVenueDirectiveReplace;

inline constexpr std::uint8_t kVenueTifGtc = 1U << 0U;
inline constexpr std::uint8_t kVenueTifIoc = 1U << 1U;
inline constexpr std::uint8_t kVenueTifFok = 1U << 2U;
inline constexpr std::uint8_t kKnownVenueTifMask =
    kVenueTifGtc | kVenueTifIoc | kVenueTifFok;

enum class EconomicValueSourceKind : std::uint8_t {
    none = 0,
    fixed = 1,
    feature = 2,
};

enum class PostOnlyMode : std::uint8_t {
    prohibited = 1,
    required = 2,
};

// Monetary conversion and venue limits are authoritative instrument facts.
// Strategy policy binds their version but does not get to redefine them.
struct InstrumentExecutionProfile {
    std::uint16_t schema_version{};
    VenueId venue_id{};
    InstrumentId instrument_id{};
    InstrumentSpecVersion instrument_spec_version{};
    MoneyMicros quote_micros_per_tick_lot{};
    PriceTicks minimum_price{};
    PriceTicks maximum_price{};
    QuantityLots minimum_quantity{};
    QuantityLots maximum_quantity{};

    constexpr auto operator<=>(
        const InstrumentExecutionProfile&) const noexcept = default;
};

struct VenueCapabilities {
    std::uint16_t schema_version{};
    VenueId venue_id{};
    VenueCapabilitiesVersion venue_capabilities_version{};
    std::uint8_t directive_mask{};
    std::uint8_t time_in_force_mask{};
    std::uint8_t supports_post_only{};

    constexpr auto operator<=>(const VenueCapabilities&) const noexcept = default;
};

// A feature value is converted to ticks by dividing its FeatureMicros atoms by
// feature_atoms_per_tick using the explicit rounding mode, then applying the
// checked tick offset. Inactive fields must be zero so one economic meaning has
// one canonical representation.
struct PriceSource {
    EconomicValueSourceKind kind{EconomicValueSourceKind::none};
    std::uint16_t feature_index{};
    RoundingMode rounding{RoundingMode::toward_zero};
    PriceTicks fixed_value{};
    std::int64_t feature_atoms_per_tick{};
    PriceTicks tick_offset{};

    constexpr auto operator<=>(const PriceSource&) const noexcept = default;
};

// Quantity feature conversion is deliberately narrower than price conversion:
// no implicit lot offset is available in V2.
struct QuantitySource {
    EconomicValueSourceKind kind{EconomicValueSourceKind::none};
    std::uint16_t feature_index{};
    RoundingMode rounding{RoundingMode::toward_zero};
    QuantityLots fixed_value{};
    std::int64_t feature_atoms_per_lot{};

    constexpr auto operator<=>(const QuantitySource&) const noexcept = default;
};

struct ActionSafetyCaps {
    QuantityLots max_quantity{};
    MoneyMicros max_notional{};
    PriceTicks max_abs_price_deviation{};
    PriceTicks max_abs_tick_offset{};
    std::int64_t max_market_data_age_ns{};
    PriceSource reference_price{};

    constexpr auto operator<=>(const ActionSafetyCaps&) const noexcept = default;
};

struct ActionTemplate {
    std::uint16_t schema_version{};
    Directive directive{Directive::no_action};
    Side side{};
    TimeInForce time_in_force{};
    PostOnlyMode post_only{};
    LogicalOrderSlot order_slot{};
    InstrumentSpecVersion instrument_spec_version{};
    VenueCapabilitiesVersion venue_capabilities_version{};
    PriceSource price_source{};
    QuantitySource quantity_source{};
    ActionSafetyCaps safety_caps{};

    constexpr auto operator<=>(const ActionTemplate&) const noexcept = default;
};

// This is an exact requested action, not a risk-approved order. Phase 4 must
// verify current risk state and atomically reserve before creating an effect.
struct EconomicActionProposal {
    std::uint16_t schema_version{};
    DecisionId decision_id{};
    PolicyId policy_id{};
    PolicyVersion policy_version{};
    RiskPolicyVersion risk_policy_version{};
    InstrumentSpecVersion instrument_spec_version{};
    VenueCapabilitiesVersion venue_capabilities_version{};
    AccountId account_id{};
    InstrumentId instrument_id{};
    VenueId venue_id{};
    LogicalOrderSlot order_slot{};
    Directive directive{Directive::no_action};
    Side side{};
    TimeInForce time_in_force{};
    PostOnlyMode post_only{};
    PriceTicks price{};
    QuantityLots quantity{};
    MoneyMicros notional{};

    constexpr auto operator<=>(const EconomicActionProposal&) const noexcept = default;
};

namespace action_detail {

[[nodiscard]] constexpr bool known(const RoundingMode value) noexcept {
    return value == RoundingMode::toward_zero ||
           value == RoundingMode::away_from_zero ||
           value == RoundingMode::nearest_ties_away_from_zero;
}

[[nodiscard]] constexpr bool known(const PostOnlyMode value) noexcept {
    return value == PostOnlyMode::prohibited ||
           value == PostOnlyMode::required;
}

[[nodiscard]] constexpr bool known_action_time_in_force(
    const TimeInForce value) noexcept {
    return value == TimeInForce::good_til_cancel ||
           value == TimeInForce::immediate_or_cancel ||
           value == TimeInForce::fill_or_kill;
}

[[nodiscard]] constexpr bool empty(const PriceSource& source) noexcept {
    return source.kind == EconomicValueSourceKind::none &&
           source.feature_index == 0 &&
           source.rounding == RoundingMode::toward_zero &&
           source.fixed_value.atoms() == 0 &&
           source.feature_atoms_per_tick == 0 &&
           source.tick_offset.atoms() == 0;
}

[[nodiscard]] constexpr bool empty(const QuantitySource& source) noexcept {
    return source.kind == EconomicValueSourceKind::none &&
           source.feature_index == 0 &&
           source.rounding == RoundingMode::toward_zero &&
           source.fixed_value.atoms() == 0 &&
           source.feature_atoms_per_lot == 0;
}

[[nodiscard]] constexpr bool empty(const ActionSafetyCaps& caps) noexcept {
    return caps.max_quantity.atoms() == 0 && caps.max_notional.atoms() == 0 &&
           caps.max_abs_price_deviation.atoms() == 0 &&
           caps.max_abs_tick_offset.atoms() == 0 &&
           caps.max_market_data_age_ns == 0 && empty(caps.reference_price);
}

}  // namespace action_detail

[[nodiscard]] constexpr ContractError validate(const PriceSource& source) noexcept {
    switch (source.kind) {
        case EconomicValueSourceKind::none:
            return action_detail::empty(source) ? ContractError::none
                                                : ContractError::non_canonical_value;
        case EconomicValueSourceKind::fixed:
            if (source.fixed_value.atoms() <= 0) {
                return ContractError::non_positive_price;
            }
            if (source.feature_index != 0 ||
                source.rounding != RoundingMode::toward_zero ||
                source.feature_atoms_per_tick != 0 ||
                source.tick_offset.atoms() != 0) {
                return ContractError::non_canonical_value;
            }
            return ContractError::none;
        case EconomicValueSourceKind::feature:
            if (source.feature_index >= kMaxDecisionFeatures) {
                return ContractError::invalid_feature_index;
            }
            if (source.feature_atoms_per_tick <= 0 ||
                !action_detail::known(source.rounding)) {
                return ContractError::invalid_value_source;
            }
            if (source.fixed_value.atoms() != 0) {
                return ContractError::non_canonical_value;
            }
            return ContractError::none;
    }
    return ContractError::invalid_enum;
}

[[nodiscard]] constexpr ContractError
validate(const InstrumentExecutionProfile& profile) noexcept {
    if (profile.venue_id.value() == 0 || profile.instrument_id.value() == 0 ||
        profile.instrument_spec_version.value() == 0) {
        return ContractError::invalid_identity;
    }
    if (profile.schema_version != kInstrumentExecutionProfileSchemaVersion) {
        return ContractError::unsupported_schema;
    }
    if (profile.quote_micros_per_tick_lot.atoms() <= 0) {
        return ContractError::non_positive_notional;
    }
    if (profile.minimum_price.atoms() <= 0 ||
        profile.maximum_price.atoms() <= 0) {
        return ContractError::non_positive_price;
    }
    if (profile.minimum_quantity.atoms() <= 0 ||
        profile.maximum_quantity.atoms() <= 0) {
        return ContractError::non_positive_quantity;
    }
    if (profile.maximum_price < profile.minimum_price ||
        profile.maximum_quantity < profile.minimum_quantity) {
        return ContractError::invalid_safety_cap;
    }
    return ContractError::none;
}

[[nodiscard]] constexpr ContractError
validate(const VenueCapabilities& capabilities) noexcept {
    if (capabilities.venue_id.value() == 0 ||
        capabilities.venue_capabilities_version.value() == 0) {
        return ContractError::invalid_identity;
    }
    if (capabilities.schema_version != kVenueCapabilitiesSchemaVersion) {
        return ContractError::unsupported_schema;
    }
    if ((capabilities.directive_mask & ~kKnownVenueDirectiveMask) != 0 ||
        capabilities.directive_mask == 0 ||
        (capabilities.time_in_force_mask & ~kKnownVenueTifMask) != 0) {
        return ContractError::invalid_capability;
    }
    const auto price_action_mask = static_cast<std::uint8_t>(
        kVenueDirectivePlaceLimit | kVenueDirectiveReplace);
    if ((capabilities.directive_mask & price_action_mask) != 0 &&
        capabilities.time_in_force_mask == 0) {
        return ContractError::invalid_capability;
    }
    if (capabilities.supports_post_only > 1) {
        return ContractError::non_canonical_value;
    }
    if (capabilities.supports_post_only != 0 &&
        (capabilities.directive_mask & price_action_mask) == 0) {
        return ContractError::invalid_capability;
    }
    return ContractError::none;
}

[[nodiscard]] constexpr ContractError
validate(const QuantitySource& source) noexcept {
    switch (source.kind) {
        case EconomicValueSourceKind::none:
            return action_detail::empty(source) ? ContractError::none
                                                : ContractError::non_canonical_value;
        case EconomicValueSourceKind::fixed:
            if (source.fixed_value.atoms() <= 0) {
                return ContractError::non_positive_quantity;
            }
            if (source.feature_index != 0 ||
                source.rounding != RoundingMode::toward_zero ||
                source.feature_atoms_per_lot != 0) {
                return ContractError::non_canonical_value;
            }
            return ContractError::none;
        case EconomicValueSourceKind::feature:
            if (source.feature_index >= kMaxDecisionFeatures) {
                return ContractError::invalid_feature_index;
            }
            if (source.feature_atoms_per_lot <= 0 ||
                !action_detail::known(source.rounding)) {
                return ContractError::invalid_value_source;
            }
            if (source.fixed_value.atoms() != 0) {
                return ContractError::non_canonical_value;
            }
            return ContractError::none;
    }
    return ContractError::invalid_enum;
}

[[nodiscard]] constexpr ContractError
validate(const ActionSafetyCaps& caps) noexcept {
    if (caps.max_quantity.atoms() <= 0) {
        return ContractError::non_positive_quantity;
    }
    if (caps.max_notional.atoms() <= 0) {
        return ContractError::non_positive_notional;
    }
    if (caps.max_abs_price_deviation.atoms() < 0 ||
        caps.max_abs_tick_offset.atoms() < 0) {
        return ContractError::invalid_safety_cap;
    }
    if (caps.max_market_data_age_ns < 0) return ContractError::invalid_age;
    const auto reference_error = validate(caps.reference_price);
    if (reference_error != ContractError::none) return reference_error;
    if (caps.reference_price.kind != EconomicValueSourceKind::feature) {
        return ContractError::invalid_safety_cap;
    }
    if (caps.reference_price.tick_offset.atoms() != 0) {
        return ContractError::invalid_safety_cap;
    }
    return ContractError::none;
}

[[nodiscard]] constexpr ContractError validate(const ActionTemplate& action) noexcept {
    if (action.schema_version != kActionTemplateSchemaVersion) {
        return ContractError::unsupported_schema;
    }
    if (action.order_slot.value() == 0) {
        return ContractError::invalid_order_target;
    }
    if (action.instrument_spec_version.value() == 0 ||
        action.venue_capabilities_version.value() == 0) {
        return ContractError::invalid_identity;
    }
    if (!detail::known(action.side)) return ContractError::invalid_enum;

    if (action.directive == Directive::cancel) {
        if (action.time_in_force != TimeInForce{} ||
            action.post_only != PostOnlyMode::prohibited ||
            !action_detail::empty(action.price_source) ||
            !action_detail::empty(action.quantity_source) ||
            !action_detail::empty(action.safety_caps)) {
            return ContractError::non_canonical_value;
        }
        return ContractError::none;
    }
    if (action.directive != Directive::place_limit &&
        action.directive != Directive::replace) {
        return ContractError::invalid_action_template;
    }
    if (!action_detail::known_action_time_in_force(action.time_in_force) ||
        !action_detail::known(action.post_only)) {
        return ContractError::invalid_enum;
    }
    if (action.post_only == PostOnlyMode::required &&
        action.time_in_force != TimeInForce::good_til_cancel) {
        return ContractError::invalid_action_template;
    }
    const auto price_error = validate(action.price_source);
    if (price_error != ContractError::none) return price_error;
    if (action.price_source.kind == EconomicValueSourceKind::none) {
        return ContractError::invalid_value_source;
    }
    const auto quantity_error = validate(action.quantity_source);
    if (quantity_error != ContractError::none) return quantity_error;
    if (action.quantity_source.kind == EconomicValueSourceKind::none) {
        return ContractError::invalid_value_source;
    }
    return validate(action.safety_caps);
}

[[nodiscard]] constexpr ContractError
validate(const EconomicActionProposal& proposal) noexcept {
    if (proposal.decision_id.value() == 0 || proposal.policy_id.value() == 0 ||
        proposal.policy_version.value() == 0 ||
        proposal.risk_policy_version.value() == 0 ||
        proposal.instrument_spec_version.value() == 0 ||
        proposal.venue_capabilities_version.value() == 0 ||
        proposal.account_id.value() == 0 || proposal.instrument_id.value() == 0 ||
        proposal.venue_id.value() == 0) {
        return ContractError::invalid_identity;
    }
    if (proposal.schema_version != kEconomicActionProposalSchemaVersion) {
        return ContractError::unsupported_schema;
    }
    if (proposal.order_slot.value() == 0) {
        return ContractError::invalid_order_target;
    }
    if (!detail::known(proposal.side)) return ContractError::invalid_enum;
    if (proposal.directive == Directive::cancel) {
        if (proposal.time_in_force != TimeInForce{} ||
            proposal.post_only != PostOnlyMode::prohibited ||
            proposal.price.atoms() != 0 || proposal.quantity.atoms() != 0 ||
            proposal.notional.atoms() != 0) {
            return ContractError::non_canonical_value;
        }
        return ContractError::none;
    }
    if (proposal.directive != Directive::place_limit &&
        proposal.directive != Directive::replace) {
        return ContractError::invalid_action_template;
    }
    if (!action_detail::known_action_time_in_force(proposal.time_in_force) ||
        !action_detail::known(proposal.post_only)) {
        return ContractError::invalid_enum;
    }
    if (proposal.post_only == PostOnlyMode::required &&
        proposal.time_in_force != TimeInForce::good_til_cancel) {
        return ContractError::invalid_action_template;
    }
    if (proposal.price.atoms() <= 0) return ContractError::non_positive_price;
    if (proposal.quantity.atoms() <= 0) {
        return ContractError::non_positive_quantity;
    }
    if (proposal.notional.atoms() <= 0) {
        return ContractError::non_positive_notional;
    }
    return ContractError::none;
}

static_assert(std::is_trivially_copyable_v<PriceSource>);
static_assert(std::is_trivially_copyable_v<QuantitySource>);
static_assert(std::is_trivially_copyable_v<ActionSafetyCaps>);
static_assert(std::is_trivially_copyable_v<ActionTemplate>);
static_assert(std::is_trivially_copyable_v<EconomicActionProposal>);
static_assert(std::is_trivially_copyable_v<InstrumentExecutionProfile>);
static_assert(std::is_trivially_copyable_v<VenueCapabilities>);
static_assert(sizeof(PriceSource) <= kPriceSourceObjectBudgetBytes);
static_assert(sizeof(QuantitySource) <= kQuantitySourceObjectBudgetBytes);
static_assert(sizeof(ActionSafetyCaps) <= kActionSafetyCapsObjectBudgetBytes);
static_assert(sizeof(ActionTemplate) <= kActionTemplateObjectBudgetBytes);
static_assert(sizeof(EconomicActionProposal) <=
              kEconomicActionProposalObjectBudgetBytes);
static_assert(sizeof(InstrumentExecutionProfile) <=
              kInstrumentExecutionProfileObjectBudgetBytes);
static_assert(sizeof(VenueCapabilities) <= kVenueCapabilitiesObjectBudgetBytes);

}  // namespace velocita::v2
