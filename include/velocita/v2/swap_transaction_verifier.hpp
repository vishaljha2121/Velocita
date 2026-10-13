#pragma once

#include "velocita/v2/solana_resolved_accounts.hpp"
#include "velocita/v2/swap_owner_accounting.hpp"

#include <optional>

namespace velocita::v2::truemarkets {

struct DirectBuyVerificationPolicy {
    swap::Binding binding{};
    std::array<SolanaAccountKey,20> account_keys{};
    std::uint64_t max_envelope_age_ms{}, max_compute_price{};
    std::uint32_t max_compute_units{};
    std::uint16_t max_slippage_bps{};
    SwapAcceptedBuyCosts accepted_costs{};
    Digest256 lookup_image_digest{};
    std::uint64_t expires_at_ms{};
};

[[nodiscard]] Digest256 direct_buy_policy_digest(const DirectBuyVerificationPolicy&) noexcept;
[[nodiscard]] Digest256 direct_buy_lookup_digest(std::span<const SolanaLookupEvidence>) noexcept;
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
    [[nodiscard]] const SwapTransactionBounds& bounds() const noexcept { return bounds_; }
    [[nodiscard]] std::uint64_t expires_at_ms() const noexcept { return expires_; }
private:
    friend SwapTransactionVerification verify_swap_transaction(std::string_view,
        std::string_view, std::string_view, const swap::Pending&, const DirectBuyVerificationPolicy&,
        std::span<const SolanaLookupEvidence>, std::uint64_t) noexcept;
    VerifiedSwapTransaction(Digest256 payload,Digest256 intent,Digest256 policy,
        SwapTransactionBounds bounds,std::uint64_t expires) noexcept
        :payload_(payload),intent_(intent),policy_(policy),bounds_(bounds),expires_(expires){}
    Digest256 payload_{},intent_{},policy_{};
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
    const DirectBuyVerificationPolicy&,std::span<const SolanaLookupEvidence>,std::uint64_t now_ms) noexcept;

}
