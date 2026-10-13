#pragma once
#include "velocita/v2/swap_gateway_runtime.hpp"
#include "velocita/v2/swap_runtime_https.hpp"
#include "velocita/v2/swap_order_status.hpp"
#include "velocita/v2/swap_account_setup.hpp"
#include <vector>
#include "velocita/v2/swap_account_review.hpp"
#include "velocita/v2/swap_account_history.hpp"

namespace velocita::v2 {
struct SwapRuntimeProfile {
    SwapGatewayStartupConfig gateway{};
    SwapSignerStartupConfig signer{};
    truemarkets::TmText organization{}, signer_id{}, user_id{};
    SwapFailureAccountingPolicy failure_policy{};
    Digest256 account_review_digest{};
};
// Public configuration only: exact bounded key=value schema, no secret material.
[[nodiscard]] std::optional<SwapRuntimeProfile> decode_swap_runtime_profile(std::string_view) noexcept;
// Implementation wiring is qualified separately from admission of real transactions.
[[nodiscard]] constexpr bool swap_runtime_live_contract_admitted() noexcept { return false; }
[[nodiscard]] SwapAccountSetupResult read_swap_account_setup(SwapGatewayCredentials&,
    SwapFinalizedReadClock&,std::string_view expected_user,std::uint64_t timeout_ms);
struct SwapAccountStartupAdmission {
    SwapAccountReview review{};
    SwapAccountHistoryCut history{};
    swap::State state{};
    CommittedBalanceReadBarrier barrier{};
    bool recovery_only{};
};
[[nodiscard]] bool read_swap_reviewed_balance(SwapGatewayCredentials&, SwapFinalizedReadClock&,
    const SwapAccountStartupAdmission&, std::uint64_t timeout_ms);
class SwapProductionPorts final:public SwapPreparePort,public SwapExecutionPort,public SwapGatewaySettlementPort {
public:
    SwapProductionPorts(SwapGatewayCredentials&,SwapPrepareStore&,SwapSubmissionStore&,SwapPrepareBinding,
        SwapFinalizedReadClock&,truemarkets::TmText organization,truemarkets::TmText signer,
        std::span<const truemarkets::SolanaLookupEvidence>,std::uint64_t timeout_ms,SwapAccountStartupAdmission,SwapFailureAccountingPolicy={});
    [[nodiscard]] transport::SwapHttpsResult create(const truemarkets::SwapRestRequest&)override;
    void execute(const SwapSubmissionIdentity&,std::span<const std::byte>)override;
    [[nodiscard]] std::optional<VerifiedSwapSettlement> reconcile(const swap::Pending&)override;
private:
    SwapGatewayCredentials& credentials_;SwapPrepareStore& preparation_;SwapSubmissionStore& submission_;
    SwapPrepareBinding binding_;SwapFinalizedReadClock& clock_;truemarkets::TmText organization_,signer_;
    std::vector<truemarkets::SolanaLookupEvidence> lookups_;std::uint64_t timeout_{};
    SwapFailureAccountingPolicy failure_policy_{};
    SwapAccountStartupAdmission admission_;
    bool created_{},executed_{},reconciled_{};
};
// Owns the concrete adapters; borrowed credentials/stores/link outlive this object.
class SwapProductionGateway final {
public:
    SwapProductionGateway(GatewayLink&,SwapReconcileStore&,SwapPrepareStore&,SwapSubmissionStore&,
        SwapSubmissionStore& signer_requests,SwapGatewayCredentials&,SwapFinalizedReadClock&,
        const SwapRuntimeProfile&,SwapGatewayRuntimeConfig,SwapAccountStartupAdmission,
        std::span<const truemarkets::SolanaLookupEvidence> lookups={});
    [[nodiscard]] SwapGatewayRuntime& runtime()noexcept{return runtime_;}
private:
    std::unique_ptr<swap_signer::Client> client_;
    SwapDurableSigningPort signing_;
    SwapProductionPorts ports_;
    SwapGatewayRuntime runtime_;
};
}
