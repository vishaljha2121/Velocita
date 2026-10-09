// Resolves a matched rule's action template and a decision frame into an exact economic proposal.
#pragma once

#include "velocita/v2/action_template.hpp"

#include <cstdint>
#include <type_traits>

namespace velocita::v2 {

enum class ActionResolutionCode : std::uint8_t {
    none = 0,
    proposed = 1,
    invalid_decision_id,
    invalid_policy,
    invalid_frame,
    invalid_template,
    invalid_instrument_profile,
    invalid_venue_capabilities,
    policy_scope_mismatch,
    instrument_scope_mismatch,
    venue_scope_mismatch,
    instrument_version_mismatch,
    capability_version_mismatch,
    policy_not_yet_valid,
    policy_expired,
    unsupported_directive,
    unsupported_time_in_force,
    unsupported_post_only,
    market_data_not_ready,
    market_data_stale,
    feature_unavailable,
    arithmetic_overflow,
    non_positive_price,
    non_positive_quantity,
    price_out_of_bounds,
    quantity_out_of_bounds,
    tick_offset_exceeded,
    price_deviation_exceeded,
    quantity_cap_exceeded,
    notional_cap_exceeded,
};

struct ActionResolution {
    ActionResolutionCode code{ActionResolutionCode::invalid_decision_id};
    ContractError contract_error{ContractError::none};
    EconomicActionProposal proposal{};

    constexpr auto operator<=>(const ActionResolution&) const noexcept = default;
};

[[nodiscard]] ActionResolution resolve_action(
    DecisionId decision_id,
    const PolicyArtifact& policy,
    const DecisionFrame& frame,
    const ActionTemplate& action,
    const InstrumentExecutionProfile& instrument,
    const VenueCapabilities& venue,
    QuantityLots quantity_override = {}) noexcept;

static_assert(std::is_trivially_copyable_v<ActionResolution>);
static_assert(sizeof(ActionResolution) <= kActionResolutionObjectBudgetBytes,
              "ActionResolution exceeded its reviewed result budget");

}
