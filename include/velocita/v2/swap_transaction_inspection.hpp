#pragma once

#include "velocita/v2/jupiter_route_decode.hpp"
#include "velocita/v2/solana_resolved_accounts.hpp"
#include "velocita/v2/swap.hpp"

namespace velocita::v2::truemarkets {

struct CapturedBuyProfile {
    std::array<SolanaAccountKey,33> account_keys{};
    swap::Intent intent{};
    std::uint64_t now_ms{}, max_envelope_age_ms{}, max_compute_price{};
    std::uint32_t max_compute_units{};
    std::uint16_t max_slippage_bps{}, max_platform_fee_bps{};
};
enum class TransactionInspectionCode : std::uint8_t {
    invalid_profile, invalid_envelope, invalid_accounts, identity_mismatch,
    unsupported_topology, invalid_instruction, invalid_route, route_bound_mismatch,
    matched_capture_shape_only
};
enum TransactionBlocker : std::uint32_t {
    route_economics_unproven=1, account_lifecycle_unproven=2,
    cpi_semantics_unproven=4, committed_authority_unproven=8,
    auxiliary_sol_payment_conflict=16, nominal_slippage_shortfall=32,
    chain_validity_unproven=64, lookup_provenance_unproven=128
};
struct CapturedBuyInspection {
    TransactionInspectionCode code{TransactionInspectionCode::invalid_profile};
    std::uint32_t blockers{};
    Digest256 envelope_digest{};
    ResolvedSolanaAccounts accounts{};
    JupiterSharedRouteData route{};
    std::uint64_t explicit_owner_payment{}, nominal_output_after_slippage{};
    std::uint64_t compute_price{}, priority_fee_upper_lamports{};
    std::uint32_t compute_units{};
};
[[nodiscard]] CapturedBuyInspection inspect_captured_buy_transaction(
    std::string_view envelope, std::string_view organization, std::string_view signer,
    const CapturedBuyProfile&, std::span<const SolanaLookupEvidence>) noexcept;

}
