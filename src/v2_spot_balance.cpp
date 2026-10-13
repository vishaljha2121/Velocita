#include "velocita/v2/spot_balance.hpp"

#include <limits>

namespace velocita::v2 {
namespace {
bool valid(const SpotAssetBalance& value) noexcept {
    return value.total_atoms >= 0 && value.available_atoms >= 0 && value.held_atoms >= 0 &&
           value.available_atoms <= value.total_atoms &&
           value.held_atoms == value.total_atoms - value.available_atoms;
}
}

SpotBalanceCheck compare_spot_balance(const ExpectedSpotBalance& expected,
                                      const SpotBalanceSnapshot& observed,
                                      std::uint64_t now_ms, std::uint64_t max_age_ms) noexcept {
    if (expected.account_and_assets_digest.is_zero() || observed.account_and_assets_digest.is_zero() ||
        expected.base_lots < 0 || expected.base_atoms_per_lot <= 0 || expected.residual_base_atoms < 0 ||
        expected.residual_base_atoms >= expected.base_atoms_per_lot || expected.quote_atoms < 0 ||
        !valid(observed.base) || !valid(observed.quote) || max_age_ms == 0 || observed.received_at_ms == 0)
        return {};
    if (expected.base_lots > (std::numeric_limits<std::int64_t>::max() - expected.residual_base_atoms) /
                                expected.base_atoms_per_lot) return {};
    if (expected.account_and_assets_digest != observed.account_and_assets_digest)
        return {SpotBalanceVerdict::identity_mismatch};
    if (observed.received_at_ms > now_ms || now_ms - observed.received_at_ms > max_age_ms)
        return {SpotBalanceVerdict::stale};
    if (expected.unresolved_orders != 0) return {SpotBalanceVerdict::unresolved_activity};
    if (observed.base.held_atoms != 0 || observed.quote.held_atoms != 0) return {SpotBalanceVerdict::held_funds};
    const auto base = expected.base_lots * expected.base_atoms_per_lot + expected.residual_base_atoms;
    const auto base_delta = observed.base.total_atoms - base;
    const auto quote_delta = observed.quote.total_atoms - expected.quote_atoms;
    return {base_delta == 0 && quote_delta == 0 ? SpotBalanceVerdict::matching_snapshot_only
                                             : SpotBalanceVerdict::unexplained_delta,
            base_delta, quote_delta};
}
}
