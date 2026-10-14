#pragma once

#include "velocita/v2/solana_resolved_accounts.hpp"
#include "velocita/v2/swap_owner_accounting.hpp"

#include <optional>

namespace velocita::v2::truemarkets {

inline constexpr std::size_t kMaxLookupPrefix = 256;
inline constexpr std::size_t kLookupTableMetaBytes = 56;

struct SwapExecutionPolicy {
    swap::Binding binding{};
    SolanaAccountKey sponsor{}, owner{}, owner_usdc{}, owner_wsol{}, platform_fee{};
    std::uint64_t max_envelope_age_ms{}, max_compute_price{};
    std::uint32_t max_compute_units{};
    std::uint16_t max_slippage_bps{};
    SwapAcceptedBuyCosts accepted_costs{};
};

struct JupiterLookupPrefix {
    SolanaAccountKey table{};
    std::uint16_t count{};
    std::array<SolanaAccountKey, kMaxLookupPrefix> addresses{};
    bool operator==(const JupiterLookupPrefix&) const = default;
};

struct JupiterLookupRequirement {
    SolanaAccountKey table{};
    std::uint16_t count{};
};

[[nodiscard]] Digest256 swap_execution_policy_digest(const SwapExecutionPolicy&) noexcept;
[[nodiscard]] Digest256 jupiter_lookup_prefix_digest(const JupiterLookupPrefix&) noexcept;
[[nodiscard]] SolanaLookupEvidence jupiter_lookup_evidence(const JupiterLookupPrefix&) noexcept;
[[nodiscard]] std::optional<JupiterLookupRequirement> jupiter_lookup_requirement(std::string_view envelope,
    std::string_view organization, std::string_view signer) noexcept;
[[nodiscard]] std::optional<JupiterLookupPrefix> jupiter_lookup_prefix_from_account(
    const JupiterLookupRequirement&, std::span<const std::uint8_t> account_data) noexcept;
[[nodiscard]] Digest256 verified_swap_intent_digest(const swap::Intent&) noexcept;

enum class SwapTransactionVerificationCode : std::uint8_t {
    invalid_policy, invalid_intent, payload_mismatch, invalid_envelope,
    invalid_accounts, account_mismatch, unsupported_topology, invalid_instruction,
    unsupported_route, debit_mismatch, deduction_exceeded, minimum_shortfall,
    verified_under_program_contract
};
struct SwapTransactionBounds {
    std::uint64_t total_usdc_debit{}, explicit_usdc_fee{}, gross_sol_floor{},
        maximum_sol_deduction{}, net_sol_floor{};
};
struct SwapTransactionVerification;
class VerifiedSwapTransaction final {
public:
    [[nodiscard]] const Digest256& payload_digest() const noexcept { return payload_; }
    [[nodiscard]] const Digest256& intent_digest() const noexcept { return intent_; }
    [[nodiscard]] const Digest256& policy_digest() const noexcept { return policy_; }
    [[nodiscard]] const Digest256& lookup_digest() const noexcept { return lookup_; }
    [[nodiscard]] const SwapTransactionBounds& bounds() const noexcept { return bounds_; }
    [[nodiscard]] std::uint64_t expires_at_ms() const noexcept { return expires_; }
private:
    friend SwapTransactionVerification verify_swap_transaction(std::string_view,
        std::string_view, std::string_view, const swap::Pending&, const SwapExecutionPolicy&,
        const JupiterLookupPrefix&, std::uint64_t) noexcept;
    VerifiedSwapTransaction(Digest256 payload,Digest256 intent,Digest256 policy,Digest256 lookup,
        SwapTransactionBounds bounds,std::uint64_t expires) noexcept
        :payload_(payload),intent_(intent),policy_(policy),lookup_(lookup),bounds_(bounds),expires_(expires){}
    Digest256 payload_{},intent_{},policy_{},lookup_{};
    SwapTransactionBounds bounds_{};
    std::uint64_t expires_{};
};
struct SwapTransactionVerification {
    SwapTransactionVerificationCode code{SwapTransactionVerificationCode::invalid_policy};
    SwapTransactionBounds bounds{};
    std::optional<VerifiedSwapTransaction> verified{};
};
[[nodiscard]] SwapTransactionVerification verify_swap_transaction(std::string_view envelope,
    std::string_view organization,std::string_view signer,const swap::Pending&,
    const SwapExecutionPolicy&,const JupiterLookupPrefix&,std::uint64_t now_ms) noexcept;

}
