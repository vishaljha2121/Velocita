#pragma once
#include "velocita/v2/truemarkets_wire.hpp"
#include "velocita/v2/swap_runtime_config.hpp"
#include "velocita/v2/swap_reconcile.hpp"

namespace velocita::v2 {
struct SwapOutsideActivity {
    Digest256 evidence{};
    std::int64_t base_delta{}, quote_delta{};
};
struct SwapAccountReview {
    Digest256 document_digest{}, runtime_identity{}, opening_digest{}, history_evidence{};
    truemarkets::TmText user_id{};
    std::uint64_t reviewed_at_ms{}, expires_at_ms{};
    std::int64_t outside_base{}, outside_quote{};
    std::array<SwapOutsideActivity,16> activity{};
    std::uint8_t activity_count{};
};
[[nodiscard]] Digest256 swap_opening_review_digest(const swap::Initialize&) noexcept;
[[nodiscard]] std::optional<SwapAccountReview> decode_swap_account_review(std::string_view, Digest256 expected) noexcept;
[[nodiscard]] bool swap_account_review_matches(const SwapAccountReview&, const SwapRuntimeBinding&,
    std::string_view user, const swap::State&) noexcept;
[[nodiscard]] bool swap_account_review_current(const SwapAccountReview&, std::uint64_t now) noexcept;
[[nodiscard]] std::optional<DeclaredOutsideBalance> reviewed_outside_balance(const SwapAccountReview&) noexcept;
[[nodiscard]] bool swap_reviewed_balance_matches(const SwapAccountReview&,const SwapRuntimeBinding&,
    const swap::State&,const CommittedBalanceReadBarrier&,std::string_view response,std::uint64_t started,std::uint64_t received) noexcept;
}
