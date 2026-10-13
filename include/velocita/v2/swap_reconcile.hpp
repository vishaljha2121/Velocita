#pragma once

#include "velocita/v2/swap_balance.hpp"

#include <string_view>

namespace velocita::v2 {

struct DeclaredOutsideBalance {
    Digest256 account_and_assets_digest{}, clock_epoch{}, evidence_digest{};
    std::uint64_t declared_at_ms{};
    std::int64_t base_atoms{}, quote_atoms{};
};

struct CommittedBalanceReadBarrier {
    Digest256 incarnation{}, clock_epoch{}, read_evidence_digest{};
    std::uint64_t applied_index{}, request_started_ms{}, received_at_ms{};
};

struct BalanceReadContext {
    Digest256 account_and_assets_digest{}, clock_epoch{}, request_id{}, outside_partition_digest{};
    std::uint64_t request_started_ms{}, received_at_ms{};
};

struct SwapReconcileCut {
    Digest256 incarnation{}, clock_epoch{}, expected_request_id{}, outside_partition_digest{};
    std::uint64_t required_result_index{}, now_ms{}, max_age_ms{};
};

enum class SwapReconcileCode : std::uint8_t {
    invalid_context, identity_mismatch, state_watermark, stale_state, stale_observation,
    partition_mismatch, projection_refused, malformed_response, row_identity,
    bad_amount, held_funds, overflow, compared_snapshot_only
};

struct SwapReconcileResult {
    SwapReconcileCode code{SwapReconcileCode::invalid_context};
    SwapBalanceCode projection_code{SwapBalanceCode::uninitialized};
    SpotBalanceCheck comparison{};
    SpotBalanceSnapshot wallet{};
    Digest256 response_digest{};
};

[[nodiscard]] Digest256 outside_balance_digest(const DeclaredOutsideBalance&) noexcept;
[[nodiscard]] SwapReconcileResult reconcile_swap_balance(
    const swap::State&, const SwapBalanceBinding&, const DeclaredOutsideBalance&,
    const CommittedBalanceReadBarrier&, const BalanceReadContext&, const SwapReconcileCut&,
    std::string_view response) noexcept;

}
