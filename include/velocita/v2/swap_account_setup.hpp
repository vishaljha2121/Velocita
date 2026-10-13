#pragma once
#include "velocita/v2/solana_resolved_accounts.hpp"

namespace velocita::v2 {
enum class SwapAccountSetupCode : std::uint8_t {
    malformed, invalid_pin, wrong_user, defi_disabled, wallet_ambiguous,
    wrong_wallet, matching_setup_only
};
struct SwapAccountSetupResult {
    SwapAccountSetupCode code{SwapAccountSetupCode::malformed};
    Digest256 response_digest{};
};
[[nodiscard]] bool swap_account_user_id_valid(std::string_view) noexcept;
[[nodiscard]] SwapAccountSetupResult verify_swap_account_setup(
    std::string_view, std::string_view expected_user,
    const truemarkets::SolanaAccountKey& expected_owner) noexcept;
}
