#pragma once
#include "velocita/v2/swap_runtime_config.hpp"

namespace velocita::v2 {
[[nodiscard]] bool swap_mvp_buy_scope(const swap::State&,const SwapSignerVerificationContext&) noexcept;
[[nodiscard]] std::optional<SwapSignerVerificationContext> decode_swap_transaction_review(
    std::string_view document,const Digest256& expected_raw_sha256,const SwapRuntimeBinding&,
    std::string_view organization,std::string_view signer) noexcept;
}
