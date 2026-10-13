#pragma once
#include "velocita/v2/swap_settlement_connection.hpp"
#include "velocita/v2/swap_settlement_lifecycle.hpp"

namespace velocita::v2 {
class VerifiedSwapSettlement;
struct SwapSettlementAdmissionResult;
#ifdef VELOCITA_SWAP_SETTLEMENT_SYNTHETIC_FIXTURE
namespace settlement_test_fixture {
[[nodiscard]] VerifiedSwapSettlement make_verified(const swap::Settlement&) noexcept;
}
#endif
class VerifiedSwapSettlement final {
public:
    VerifiedSwapSettlement(const VerifiedSwapSettlement& other) noexcept : settlement_(other.settlement_) {}
    VerifiedSwapSettlement& operator=(const VerifiedSwapSettlement&) noexcept = default;
    [[nodiscard]] const swap::Settlement& settlement() const noexcept { return settlement_; }
private:
    friend SwapSettlementAdmissionResult admit_retained_swap_settlement(
        const SwapPrepareStore&,const SwapSubmissionStore&,const SwapPrepareBinding&,const swap::Intent&,
        SwapFinalizedReader&,std::uint64_t,std::uint64_t,std::string_view,std::string_view,
        std::span<const truemarkets::SolanaLookupEvidence>,const truemarkets::SolanaAccountKey&,
        SwapFailureAccountingPolicy,SwapAcceptedBuyCosts);
    explicit VerifiedSwapSettlement(const swap::Settlement& value) noexcept : settlement_(value) {}
#ifdef VELOCITA_SWAP_SETTLEMENT_SYNTHETIC_FIXTURE
    friend VerifiedSwapSettlement settlement_test_fixture::make_verified(const swap::Settlement&) noexcept;
#endif
    swap::Settlement settlement_{};
};
#ifdef VELOCITA_SWAP_SETTLEMENT_SYNTHETIC_FIXTURE
namespace settlement_test_fixture {
inline VerifiedSwapSettlement make_verified(const swap::Settlement& value) noexcept { return VerifiedSwapSettlement(value); }
}
#endif

enum class SwapSettlementAdmissionCode : std::uint8_t {
    intent_refused, retained_identity_refused, rpc_unresolved, evidence_refused, lifecycle_unproven, economic_refused, admitted
};
enum SwapSettlementRequiredEvidence : std::uint32_t {
    complete_lifecycle_trace = 1, account_control_continuity = 2,
    fee_and_principal_attribution = 4, owner_net_receipt_and_costs = 8
};
struct SwapSettlementAdmissionResult {
    SwapSettlementAdmissionCode code{SwapSettlementAdmissionCode::intent_refused};
    std::uint32_t required_evidence{};
    std::optional<VerifiedSwapSettlement> verified{};
    SwapSettlementConnectionResult observation{};
    SwapLifecycleReplay lifecycle{};
    FailedSwapAccounting failure{};
    ObservedSwapBounds bounds{ObservedSwapBounds::unusable};
};
[[nodiscard]] SwapSettlementAdmissionResult admit_retained_swap_settlement(
    const SwapPrepareStore&, const SwapSubmissionStore&, const SwapPrepareBinding&, const swap::Intent&,
    SwapFinalizedReader&, std::uint64_t request_id, std::uint64_t minimum_slot,
    std::string_view organization, std::string_view signer,
    std::span<const truemarkets::SolanaLookupEvidence>, const truemarkets::SolanaAccountKey& owner,
    SwapFailureAccountingPolicy failure_policy = {}, SwapAcceptedBuyCosts accepted_buy_costs = {});
}
