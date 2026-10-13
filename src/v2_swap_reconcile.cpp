#include "velocita/v2/swap_reconcile.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/truemarkets_wire.hpp"

#include <limits>

namespace velocita::v2 {
namespace {
void hash_integer(Sha256& hash, std::uint64_t value) noexcept {
    std::array<std::byte, 8> bytes{};
    for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<std::byte>(value >> (8U * i));
    hash.update(bytes);
}
bool timely(std::uint64_t start, std::uint64_t received, const SwapReconcileCut& cut) noexcept {
    return start != 0 && start <= received && received <= cut.now_ms && cut.now_ms - start <= cut.max_age_ms;
}
SwapReconcileCode parse_wallet(std::string_view response, const BalanceReadContext& context,
                               SpotBalanceSnapshot& out) noexcept {
    using namespace truemarkets;
    if (validate_json_object(response) != JsonError::none) return SwapReconcileCode::malformed_response;
    const auto data = json_member_raw(response, "data");
    if (!data) return SwapReconcileCode::malformed_response;
    std::array<std::string_view, 2> rows{};
    const auto count = json_array_elements(*data, rows);
    if (!count || *count != 2) return SwapReconcileCode::row_identity;
    bool base_seen{}, quote_seen{};
    SpotBalanceSnapshot wallet;
    wallet.account_and_assets_digest = context.account_and_assets_digest;
    wallet.received_at_ms = context.received_at_ms;
    for (const auto row : rows) {
        if (validate_json_object(row) != JsonError::none) return SwapReconcileCode::malformed_response;
        const auto symbol = json_member_string(row, "symbol");
        if (!symbol || json_member_string(row, "chain") != "solana" || json_member_string(row, "venue") != "defi")
            return SwapReconcileCode::row_identity;
        bool* seen{};
        SpotAssetBalance* balance{};
        std::uint8_t decimals{};
        if (*symbol == "SOL") { seen = &base_seen; balance = &wallet.base; decimals = 9; }
        else if (*symbol == "USDC") { seen = &quote_seen; balance = &wallet.quote; decimals = 6; }
        else return SwapReconcileCode::row_identity;
        if (*seen) return SwapReconcileCode::row_identity;
        *seen = true;
        const auto total = json_member_string(row, "total"), available = json_member_string(row, "available"),
                   held = json_member_string(row, "held");
        if (!total || !available || !held) return SwapReconcileCode::bad_amount;
        const auto t = parse_decimal_atoms(*total, decimals), a = parse_decimal_atoms(*available, decimals),
                   h = parse_decimal_atoms(*held, decimals);
        if (!t || !a || !h || *a > *t || *h != *t - *a) return SwapReconcileCode::bad_amount;
        *balance = {*t, *a, *h};
    }
    if (!base_seen || !quote_seen) return SwapReconcileCode::row_identity;
    if (wallet.base.held_atoms != 0 || wallet.quote.held_atoms != 0) return SwapReconcileCode::held_funds;
    out = wallet;
    return SwapReconcileCode::compared_snapshot_only;
}
}

Digest256 outside_balance_digest(const DeclaredOutsideBalance& outside) noexcept {
    if (outside.account_and_assets_digest.is_zero() || outside.clock_epoch.is_zero() || outside.evidence_digest.is_zero() ||
        outside.declared_at_ms == 0 || outside.base_atoms < 0 || outside.quote_atoms < 0) return {};
    constexpr std::string_view domain = "V2-OUTSIDE-BALANCE-1";
    Sha256 hash;
    hash.update(std::as_bytes(std::span{domain.data(), domain.size()}));
    hash.update(outside.account_and_assets_digest.bytes);
    hash.update(outside.clock_epoch.bytes);
    hash.update(outside.evidence_digest.bytes);
    hash_integer(hash, outside.declared_at_ms);
    hash_integer(hash, static_cast<std::uint64_t>(outside.base_atoms));
    hash_integer(hash, static_cast<std::uint64_t>(outside.quote_atoms));
    return hash.finish();
}
SwapReconcileResult reconcile_swap_balance(
    const swap::State& state, const SwapBalanceBinding& binding, const DeclaredOutsideBalance& outside,
    const CommittedBalanceReadBarrier& barrier, const BalanceReadContext& read, const SwapReconcileCut& cut,
    std::string_view response) noexcept {
    if (cut.incarnation.is_zero() || cut.clock_epoch.is_zero() || cut.expected_request_id.is_zero() ||
        cut.required_result_index == 0 || cut.max_age_ms == 0 || cut.now_ms == 0 ||
        barrier.read_evidence_digest.is_zero() ||
        barrier.applied_index > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        cut.required_result_index > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return {};
    if (cut.incarnation != binding.allocation.incarnation || barrier.incarnation != cut.incarnation ||
        barrier.clock_epoch != cut.clock_epoch || read.clock_epoch != cut.clock_epoch ||
        outside.clock_epoch != cut.clock_epoch ||
        read.request_id != cut.expected_request_id || read.account_and_assets_digest != binding.account_and_assets_digest ||
        outside.account_and_assets_digest != binding.account_and_assets_digest)
        return {SwapReconcileCode::identity_mismatch};
    if (barrier.applied_index < cut.required_result_index) return {SwapReconcileCode::state_watermark};
    if (!timely(barrier.request_started_ms, barrier.received_at_ms, cut)) return {SwapReconcileCode::stale_state};
    if (!timely(read.request_started_ms, read.received_at_ms, cut) || read.request_started_ms < barrier.received_at_ms)
        return {SwapReconcileCode::stale_observation};
    const auto partition = outside_balance_digest(outside);
    if (partition.is_zero() || partition != cut.outside_partition_digest || partition != read.outside_partition_digest ||
        outside.declared_at_ms > barrier.request_started_ms) return {SwapReconcileCode::partition_mismatch};
    const auto projected = expected_swap_balance(state, binding);
    if (projected.code != SwapBalanceCode::ready_for_comparison)
        return {SwapReconcileCode::projection_refused, projected.code};
    SpotBalanceSnapshot wallet;
    const auto parse_code = parse_wallet(response, read, wallet);
    if (parse_code != SwapReconcileCode::compared_snapshot_only) return {parse_code};
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto managed_base = static_cast<std::int64_t>(state.base.value());
    if (outside.base_atoms > maximum - managed_base || outside.quote_atoms > maximum - projected.expected.quote_atoms)
        return {SwapReconcileCode::overflow};
    const auto wallet_base = managed_base + outside.base_atoms;
    auto expected = projected.expected;
    expected.base_lots = wallet_base / expected.base_atoms_per_lot;
    expected.residual_base_atoms = wallet_base % expected.base_atoms_per_lot;
    expected.quote_atoms += outside.quote_atoms;
    SwapReconcileResult result;
    result.code = SwapReconcileCode::compared_snapshot_only;
    result.projection_code = projected.code;
    result.comparison = compare_spot_balance(expected, wallet, cut.now_ms, cut.max_age_ms);
    result.wallet = wallet;
    result.response_digest = sha256_digest(std::as_bytes(std::span{response.data(), response.size()}));
    return result;
}
}
