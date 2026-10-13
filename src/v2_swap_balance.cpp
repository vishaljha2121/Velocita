#include "velocita/v2/swap_balance.hpp"

#include <limits>

namespace velocita::v2 {
SwapBalanceExpectation expected_swap_balance(const swap::State& state, const SwapBalanceBinding& binding) noexcept {
    if (!swap::valid_state(state)) return {SwapBalanceCode::invalid_state, {}};
    if (!state.initialized) return {SwapBalanceCode::uninitialized, {}};
    if (binding.allocation != state.opening.binding || binding.account_and_assets_digest.is_zero() ||
        binding.base_atoms_per_lot == 0) return {SwapBalanceCode::invalid_binding, {}};
    if (state.conflicted) return {SwapBalanceCode::conflicted, {}};
    if (state.pending.phase != swap::Phase::none) return {SwapBalanceCode::unresolved_activity, {}};
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (state.base.value() > maximum || state.quote.value() > maximum || binding.base_atoms_per_lot > maximum)
        return {SwapBalanceCode::out_of_range, {}};
    ExpectedSpotBalance expected;
    expected.account_and_assets_digest = binding.account_and_assets_digest;
    expected.base_lots = static_cast<std::int64_t>(state.base.value() / binding.base_atoms_per_lot);
    expected.base_atoms_per_lot = static_cast<std::int64_t>(binding.base_atoms_per_lot);
    expected.residual_base_atoms = static_cast<std::int64_t>(state.base.value() % binding.base_atoms_per_lot);
    expected.quote_atoms = static_cast<std::int64_t>(state.quote.value());
    return {SwapBalanceCode::ready_for_comparison, expected};
}
}
