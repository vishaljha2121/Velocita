#pragma once
#include "velocita/v2/swap_prepare_store.hpp"
#include "velocita/v2/swap_settlement_evidence.hpp"

namespace velocita::v2 {
enum class SwapSettlementConnectionCode:std::uint8_t {identity_refused,rpc_unresolved,evidence_refused,observed_needs_account_lifecycle,observed_failed_needs_accounting};
struct SwapSettlementConnectionResult {
    SwapSettlementConnectionCode code{SwapSettlementConnectionCode::identity_refused};
    FinalizedReadCode read_code{FinalizedReadCode::refused};
    SettlementEvidence evidence{};
    std::optional<VerifiedFinalizedRead> finalized_read{};
};
// Collects only the transaction already retained by authenticated order recovery.
// Does not manufacture a Settlement or release the submission journal.
[[nodiscard]] SwapSettlementConnectionResult collect_retained_swap_settlement(
    const SwapPrepareStore&,const SwapSubmissionStore&,const SwapPrepareBinding&,
    SwapFinalizedReader&,std::uint64_t request_id,std::uint64_t minimum_slot,
    std::string_view organization,std::string_view signer,
    std::span<const truemarkets::SolanaLookupEvidence>,const truemarkets::SolanaAccountKey& owner);
}
