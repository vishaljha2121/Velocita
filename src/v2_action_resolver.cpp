#include "velocita/v2/action_resolver.hpp"

#include <cstdint>
#include <optional>

namespace velocita::v2 {
namespace {

struct ResolvedInteger {
    ActionResolutionCode code{ActionResolutionCode::arithmetic_overflow};
    std::int64_t value{};
};

[[nodiscard]] constexpr ActionResolution reject(
    const ActionResolutionCode code,
    const ContractError error = ContractError::none) noexcept {
    return {code, error, {}};
}

[[nodiscard]] constexpr std::optional<std::int64_t> rounded_divide(
    const std::int64_t numerator,
    const std::int64_t positive_denominator,
    const RoundingMode mode) noexcept {
    if (positive_denominator <= 0) return std::nullopt;

    auto quotient = numerator / positive_denominator;
    const auto remainder = numerator % positive_denominator;
    if (remainder == 0 || mode == RoundingMode::toward_zero) return quotient;

    bool increment = mode == RoundingMode::away_from_zero;
    if (mode == RoundingMode::nearest_ties_away_from_zero) {
        const auto magnitude = remainder < 0 ? -remainder : remainder;
        const auto threshold =
            positive_denominator / 2 + positive_denominator % 2;
        increment = magnitude >= threshold;
    }
    if (increment) quotient += numerator < 0 ? -1 : 1;
    return quotient;
}

[[nodiscard]] ResolvedInteger resolve_price_atoms(
    const PriceSource& source,
    const DecisionFrame& frame) noexcept {
    if (source.kind == EconomicValueSourceKind::fixed) {
        return {ActionResolutionCode::proposed, source.fixed_value.atoms()};
    }
    if (source.feature_index >= frame.feature_count) {
        return {ActionResolutionCode::feature_unavailable, 0};
    }
    const auto converted = rounded_divide(
        frame.features[source.feature_index].atoms(),
        source.feature_atoms_per_tick, source.rounding);
    if (!converted) return {};
    const auto with_offset = PriceTicks::from_atoms(*converted).checked_add(
        source.tick_offset);
    if (!with_offset) return {};
    return {ActionResolutionCode::proposed, with_offset->atoms()};
}

[[nodiscard]] ResolvedInteger resolve_quantity_atoms(
    const QuantitySource& source,
    const DecisionFrame& frame) noexcept {
    if (source.kind == EconomicValueSourceKind::fixed) {
        return {ActionResolutionCode::proposed, source.fixed_value.atoms()};
    }
    if (source.feature_index >= frame.feature_count) {
        return {ActionResolutionCode::feature_unavailable, 0};
    }
    const auto converted = rounded_divide(
        frame.features[source.feature_index].atoms(),
        source.feature_atoms_per_lot, source.rounding);
    if (!converted) return {};
    return {ActionResolutionCode::proposed, *converted};
}

[[nodiscard]] constexpr std::uint8_t directive_bit(
    const Directive directive) noexcept {
    switch (directive) {
        case Directive::place_limit:
            return kVenueDirectivePlaceLimit;
        case Directive::cancel:
            return kVenueDirectiveCancel;
        case Directive::replace:
            return kVenueDirectiveReplace;
        case Directive::no_action:
            return 0;
    }
    return 0;
}

[[nodiscard]] constexpr std::uint8_t time_in_force_bit(
    const TimeInForce time_in_force) noexcept {
    switch (time_in_force) {
        case TimeInForce::good_til_cancel:
            return kVenueTifGtc;
        case TimeInForce::immediate_or_cancel:
            return kVenueTifIoc;
        case TimeInForce::fill_or_kill:
            return kVenueTifFok;
        case TimeInForce::post_only:
            return 0;
    }
    return 0;
}

[[nodiscard]] constexpr bool exceeds_absolute_cap(
    const std::int64_t value,
    const std::int64_t non_negative_cap) noexcept {
    return value > non_negative_cap || value < -non_negative_cap;
}

[[nodiscard]] constexpr EconomicActionProposal proposal_base(
    const DecisionId decision_id,
    const PolicyArtifact& policy,
    const DecisionFrame& frame,
    const ActionTemplate& action) noexcept {
    EconomicActionProposal proposal;
    proposal.schema_version = kEconomicActionProposalSchemaVersion;
    proposal.decision_id = decision_id;
    proposal.policy_id = policy.policy_id;
    proposal.policy_version = policy.policy_version;
    proposal.risk_policy_version = policy.risk_policy_version;
    proposal.instrument_spec_version = action.instrument_spec_version;
    proposal.venue_capabilities_version = action.venue_capabilities_version;
    proposal.account_id = frame.account_id;
    proposal.instrument_id = frame.instrument_id;
    proposal.venue_id = frame.venue_id;
    proposal.order_slot = action.order_slot;
    proposal.directive = action.directive;
    proposal.side = action.side;
    proposal.time_in_force = action.time_in_force;
    proposal.post_only = action.post_only;
    return proposal;
}

}

ActionResolution resolve_action(
    const DecisionId decision_id,
    const PolicyArtifact& policy,
    const DecisionFrame& frame,
    const ActionTemplate& action,
    const InstrumentExecutionProfile& instrument,
    const VenueCapabilities& venue) noexcept {
    if (decision_id.value() == 0) {
        return reject(ActionResolutionCode::invalid_decision_id,
                      ContractError::invalid_identity);
    }

    if (const auto error = validate(policy); error != ContractError::none) {
        return reject(ActionResolutionCode::invalid_policy, error);
    }
    if (const auto error = validate(frame); error != ContractError::none) {
        return reject(ActionResolutionCode::invalid_frame, error);
    }
    if (const auto error = validate(action); error != ContractError::none) {
        return reject(ActionResolutionCode::invalid_template, error);
    }
    if (const auto error = validate(instrument); error != ContractError::none) {
        return reject(ActionResolutionCode::invalid_instrument_profile, error);
    }
    if (const auto error = validate(venue); error != ContractError::none) {
        return reject(ActionResolutionCode::invalid_venue_capabilities, error);
    }

    if (policy.policy_id != frame.policy_id ||
        policy.policy_version != frame.policy_version ||
        policy.feature_schema_id != frame.feature_schema_id ||
        policy.account_id != frame.account_id ||
        policy.instrument_id != frame.instrument_id) {
        return reject(ActionResolutionCode::policy_scope_mismatch);
    }
    if (instrument.instrument_id != frame.instrument_id) {
        return reject(ActionResolutionCode::instrument_scope_mismatch);
    }
    if (instrument.venue_id != frame.venue_id || venue.venue_id != frame.venue_id) {
        return reject(ActionResolutionCode::venue_scope_mismatch);
    }
    if (instrument.instrument_spec_version != action.instrument_spec_version) {
        return reject(ActionResolutionCode::instrument_version_mismatch);
    }
    if (venue.venue_capabilities_version !=
        action.venue_capabilities_version) {
        return reject(ActionResolutionCode::capability_version_mismatch);
    }
    if (frame.event_time_ns < policy.valid_from_event_time_ns) {
        return reject(ActionResolutionCode::policy_not_yet_valid);
    }
    if (frame.event_time_ns > policy.valid_until_event_time_ns) {
        return reject(ActionResolutionCode::policy_expired);
    }

    if ((venue.directive_mask & directive_bit(action.directive)) == 0) {
        return reject(ActionResolutionCode::unsupported_directive);
    }

    auto proposal = proposal_base(decision_id, policy, frame, action);
    if (action.directive == Directive::cancel) {
        return {ActionResolutionCode::proposed, ContractError::none, proposal};
    }

    if ((venue.time_in_force_mask & time_in_force_bit(action.time_in_force)) == 0) {
        return reject(ActionResolutionCode::unsupported_time_in_force);
    }
    if (action.post_only == PostOnlyMode::required &&
        venue.supports_post_only == 0) {
        return reject(ActionResolutionCode::unsupported_post_only);
    }
    if (frame.market_data_status != MarketDataStatus::ready) {
        return reject(ActionResolutionCode::market_data_not_ready);
    }
    if (frame.market_data_age_ns > action.safety_caps.max_market_data_age_ns) {
        return reject(ActionResolutionCode::market_data_stale);
    }
    if (exceeds_absolute_cap(action.price_source.tick_offset.atoms(),
                             action.safety_caps.max_abs_tick_offset.atoms())) {
        return reject(ActionResolutionCode::tick_offset_exceeded);
    }

    const auto price = resolve_price_atoms(action.price_source, frame);
    if (price.code != ActionResolutionCode::proposed) return reject(price.code);
    const auto quantity = resolve_quantity_atoms(action.quantity_source, frame);
    if (quantity.code != ActionResolutionCode::proposed) {
        return reject(quantity.code);
    }
    const auto reference =
        resolve_price_atoms(action.safety_caps.reference_price, frame);
    if (reference.code != ActionResolutionCode::proposed) {
        return reject(reference.code);
    }

    proposal.price = PriceTicks::from_atoms(price.value);
    proposal.quantity = QuantityLots::from_atoms(quantity.value);
    if (proposal.price.atoms() <= 0) {
        return reject(ActionResolutionCode::non_positive_price);
    }
    if (proposal.quantity.atoms() <= 0) {
        return reject(ActionResolutionCode::non_positive_quantity);
    }
    if (proposal.price < instrument.minimum_price ||
        proposal.price > instrument.maximum_price) {
        return reject(ActionResolutionCode::price_out_of_bounds);
    }
    if (proposal.quantity < instrument.minimum_quantity ||
        proposal.quantity > instrument.maximum_quantity) {
        return reject(ActionResolutionCode::quantity_out_of_bounds);
    }
    if (reference.value <= 0) {
        return reject(ActionResolutionCode::non_positive_price);
    }
    const auto larger = price.value > reference.value ? price.value : reference.value;
    const auto smaller = price.value > reference.value ? reference.value : price.value;
    if (larger - smaller > action.safety_caps.max_abs_price_deviation.atoms()) {
        return reject(ActionResolutionCode::price_deviation_exceeded);
    }
    if (proposal.quantity > action.safety_caps.max_quantity) {
        return reject(ActionResolutionCode::quantity_cap_exceeded);
    }

    const auto price_notional =
        instrument.quote_micros_per_tick_lot.checked_multiply(price.value);
    if (!price_notional) return reject(ActionResolutionCode::arithmetic_overflow);
    const auto notional = price_notional->checked_multiply(quantity.value);
    if (!notional) return reject(ActionResolutionCode::arithmetic_overflow);
    proposal.notional = *notional;
    if (proposal.notional > action.safety_caps.max_notional) {
        return reject(ActionResolutionCode::notional_cap_exceeded);
    }

    return {ActionResolutionCode::proposed, ContractError::none, proposal};
}

}
