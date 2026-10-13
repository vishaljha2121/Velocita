#pragma once

#include "velocita/v2/contracts.hpp"

namespace velocita::v2 {

struct SpotAssetBalance {
    std::int64_t total_atoms{}, available_atoms{}, held_atoms{};
};

struct SpotBalanceSnapshot {
    Digest256 account_and_assets_digest{};
    std::uint64_t received_at_ms{};
    SpotAssetBalance base{}, quote{};
};

struct ExpectedSpotBalance {
    Digest256 account_and_assets_digest{};
    std::int64_t base_lots{}, base_atoms_per_lot{}, residual_base_atoms{}, quote_atoms{};
    std::uint64_t unresolved_orders{};
};

enum class SpotBalanceVerdict : std::uint8_t {
    invalid, identity_mismatch, stale, unresolved_activity, held_funds,
    unexplained_delta, matching_snapshot_only
};

struct SpotBalanceCheck {
    SpotBalanceVerdict verdict{SpotBalanceVerdict::invalid};
    std::int64_t base_delta_atoms{}, quote_delta_atoms{};
};

[[nodiscard]] SpotBalanceCheck compare_spot_balance(
    const ExpectedSpotBalance& expected, const SpotBalanceSnapshot& observed,
    std::uint64_t now_ms, std::uint64_t max_age_ms) noexcept;

}
