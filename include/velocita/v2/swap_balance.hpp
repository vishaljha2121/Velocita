#pragma once

#include "velocita/v2/spot_balance.hpp"
#include "velocita/v2/swap.hpp"

namespace velocita::v2 {

struct SwapBalanceBinding {
    swap::Binding allocation{};
    Digest256 account_and_assets_digest{};
    std::uint64_t base_atoms_per_lot{};
};

enum class SwapBalanceCode : std::uint8_t {
    uninitialized, invalid_state, invalid_binding, conflicted,
    unresolved_activity, out_of_range, ready_for_comparison
};

struct SwapBalanceExpectation {
    SwapBalanceCode code{SwapBalanceCode::uninitialized};
    ExpectedSpotBalance expected{};
};

[[nodiscard]] SwapBalanceExpectation expected_swap_balance(
    const swap::State& state, const SwapBalanceBinding& binding) noexcept;

}
