#pragma once
#include "velocita/v2/swap_account_review.hpp"

namespace velocita::v2 {
struct SwapAccountHistoryCut {
    Digest256 document{}, owner{}, user{}, setup{}, balances{}, manifest{};
    std::uint64_t started_ms{}, finished_ms{}, start_slot{}, end_slot{};
    std::uint64_t base_atoms{}, quote_atoms{};
};
[[nodiscard]] std::optional<SwapAccountHistoryCut> decode_swap_account_history_cut(std::string_view,Digest256 expected) noexcept;
[[nodiscard]] bool swap_account_history_identity(const SwapAccountHistoryCut&,const SwapAccountReview&,const SwapRuntimeBinding&) noexcept;
[[nodiscard]] bool swap_account_history_funding(const SwapAccountHistoryCut&,const SwapAccountReview&,const SwapRuntimeBinding&,
    const swap::State&,std::uint64_t now_ms) noexcept;
}
