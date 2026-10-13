#pragma once
#include "velocita/v2/swap_settlement_evidence.hpp"

namespace velocita::v2 {
enum class SwapLifecycleCode : std::uint8_t {
    invalid_receipt, missing_trace, malformed_trace, capacity, unsupported_instruction,
    unproven_control, replay_mismatch, replayed_effects_only
};
enum class SwapFeeAttributionCode : std::uint8_t { unproven, observed_abi_bound_usdc_fee };
struct SwapLifecycleReplay {
    SwapLifecycleCode code{SwapLifecycleCode::invalid_receipt};
    Digest256 response_digest{}, payload_digest{};
    std::int64_t owner_wallet_lamports{}, owner_usdc_atoms{}, owner_new_refundable_deposits{};
    std::uint16_t instructions{}, created_accounts{}, closed_accounts{};
    bool opaque_containers{};
    SwapFeeAttributionCode fee_attribution{SwapFeeAttributionCode::unproven};
    Side route_side{};
    std::uint64_t route_input{}, route_source_debit{}, route_destination_credit{}, route_principal_debit{}, fee_atoms{};
};
[[nodiscard]] SwapLifecycleReplay replay_swap_settlement_lifecycle(
    const VerifiedFinalizedRead&, std::string_view original_envelope,
    std::string_view organization, std::string_view signer,
    std::span<const truemarkets::SolanaLookupEvidence>, const truemarkets::SolanaAccountKey& owner) noexcept;
struct SwapFailureAccountingPolicy {
    truemarkets::SolanaAccountKey expected_external_fee_payer{};
};
enum class FailedSwapAccountingCode : std::uint8_t {
    invalid_receipt, sponsor_unproven, unsupported_transaction, rollback_mismatch,
    observed_external_fee_only
};
struct FailedSwapAccounting {
    FailedSwapAccountingCode code{FailedSwapAccountingCode::invalid_receipt};
    std::uint64_t external_fee_lamports{};
};
[[nodiscard]] FailedSwapAccounting account_failed_swap(
    const VerifiedFinalizedRead&, std::string_view original_envelope,
    std::string_view organization, std::string_view signer,
    std::span<const truemarkets::SolanaLookupEvidence>, const truemarkets::SolanaAccountKey& owner,
    SwapFailureAccountingPolicy) noexcept;

}
