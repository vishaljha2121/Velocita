#pragma once

#include "velocita/v2/solana_instruction_decode.hpp"
#include "velocita/v2/swap.hpp"

namespace velocita::v2 {

inline constexpr std::size_t kOwnerPropertyAccounts = 32;
enum class OwnedAccountKind : std::uint8_t { native_wallet, wrapped_sol, usdc_token };
enum class DepositControl : std::uint8_t { unverified, owner, external };
struct DeclaredOwnedAccount {
    truemarkets::SolanaInstructionKey key{};
    OwnedAccountKind kind{};
};
struct AccountProperty {
    bool exists{};
    std::uint64_t lamports{}, token_atoms{}, native_reserve{};
    DepositControl deposit_control{};
};
struct OwnerAccountChange {
    truemarkets::SolanaInstructionKey key{};
    AccountProperty before{}, after{};
};
struct OwnerPropertyTotal {
    std::uint64_t wallet_lamports{}, wrapped_backing{}, refundable_deposits{}, usdc_atoms{}, total_lamports{}, external_deposits{};
    bool operator==(const OwnerPropertyTotal&) const = default;
};
enum class OwnerAccountingCode : std::uint8_t { invalid_scope, invalid_account, overflow, measured_declared_scope_only };
struct OwnerPropertyMeasurement {
    OwnerAccountingCode code{OwnerAccountingCode::invalid_scope};
    OwnerPropertyTotal before{}, after{};
    std::int64_t sol_change{}, usdc_change{}, deposit_change{}, base_change_excluding_deposits{};
};
enum class ObservedSwapBounds : std::uint8_t { unusable, debit_exceeded, receipt_shortfall, within_observed_net_bounds_only };

[[nodiscard]] OwnerPropertyMeasurement measure_owner_property(
    std::span<const DeclaredOwnedAccount>, std::span<const OwnerAccountChange>) noexcept;
[[nodiscard]] ObservedSwapBounds compare_observed_swap_bounds(
    const swap::Intent&, const OwnerPropertyMeasurement&) noexcept;

}
