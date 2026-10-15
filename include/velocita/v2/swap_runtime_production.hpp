#pragma once
#include "velocita/v2/swap_gateway_runtime.hpp"
#include "velocita/v2/swap_runtime_https.hpp"
#include "velocita/v2/swap_transaction_verifier.hpp"
#include <optional>
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
[[nodiscard]] bool swap_runtime_live_contract_admitted(const SwapRuntimeProfile&,std::uint64_t now_ms) noexcept;
[[nodiscard]] std::unique_ptr<SwapTokenPort> make_swap_token_port(long timeout_ms);
[[nodiscard]] SwapAccountSetupResult read_swap_account_setup(SwapGatewayCredentials&,
    SwapFinalizedReadClock&,std::string_view expected_user,std::uint64_t timeout_ms);
struct SwapAccountStartupAdmission {
    SwapAccountReview review{};
    SwapAccountHistoryCut history{};
    swap::State state{},funding_state{};
    CommittedBalanceReadBarrier barrier{};
    bool recovery_only{};
};
[[nodiscard]] bool swap_admission_funded(const SwapAccountStartupAdmission&,const SwapRuntimeBinding&,std::uint64_t now_ms) noexcept;
[[nodiscard]] bool read_swap_reviewed_balance(SwapGatewayCredentials&, SwapFinalizedReadClock&,
    const SwapAccountStartupAdmission&, std::uint64_t timeout_ms);
[[nodiscard]] std::optional<swap::State> swap_unsent_funding_state(const swap::State&) noexcept;
[[nodiscard]] bool swap_startup_recovery_only(const swap::State&,SwapPreparePhase,EffectId prepared_effect,SwapSubmissionPhase) noexcept;
enum class SwapCreateStage:std::uint8_t {not_called,latched,journal,admission,balance,bearer,exchanged};
struct SwapCreateTrace {
    SwapCreateStage stage{};
    transport::SwapHttpsStatus transport{transport::SwapHttpsStatus::unavailable};
    transport::SwapHttpsExposure exposure{transport::SwapHttpsExposure::not_attempted};
    unsigned http_status{};
    std::size_t body_size{},error_size{};
    Digest256 body_digest{};
    std::array<char,1024> error{};
    [[nodiscard]] std::string_view error_view()const noexcept{return {error.data(),error_size};}
};
[[nodiscard]] SwapCreateTrace swap_create_trace(const transport::SwapHttpsResult&) noexcept;
[[nodiscard]] std::string render_swap_create_trace(const SwapCreateTrace&);
[[nodiscard]] std::optional<SwapAccountStartupAdmission> swap_prepare_admission(const SwapAccountStartupAdmission&,const SwapPrepareRead&) noexcept;
class SwapProductionPorts final:public SwapPreparePort,public SwapExecutionPort,public SwapGatewaySettlementPort,public SwapLookupSource {
public:
    SwapProductionPorts(SwapGatewayCredentials&,SwapPrepareStore&,SwapSubmissionStore&,SwapPrepareBinding,
        SwapFinalizedReadClock&,truemarkets::TmText organization,truemarkets::TmText signer,
        std::uint64_t timeout_ms,SwapAccountStartupAdmission,SwapFailureAccountingPolicy={},
        std::optional<truemarkets::SwapExecutionPolicy> route_policy={});
    [[nodiscard]] transport::SwapHttpsResult create(const truemarkets::SwapRestRequest&,const SwapPrepareRead&)override;
    [[nodiscard]] bool admit(const swap::Pending&,std::string_view payload)override;
    void execute(const SwapSubmissionIdentity&,std::span<const std::byte>)override;
    [[nodiscard]] std::optional<VerifiedSwapSettlement> reconcile(const swap::Pending&)override;
    [[nodiscard]] std::optional<truemarkets::JupiterLookupPrefix> prefix(std::span<const std::byte> payload)override;
    [[nodiscard]] const SwapCreateTrace& create_trace()const noexcept{return trace_;}
private:
    SwapGatewayCredentials& credentials_;SwapPrepareStore& preparation_;SwapSubmissionStore& submission_;
    SwapPrepareBinding binding_;SwapFinalizedReadClock& clock_;truemarkets::TmText organization_,signer_;
    std::uint64_t timeout_{};
    SwapFailureAccountingPolicy failure_policy_{};
    std::optional<truemarkets::SwapExecutionPolicy> route_policy_{};
    SwapAccountStartupAdmission admission_;
    EffectId created_{},executed_{},reconciling_{};
    unsigned reconcile_attempts_{};
    SwapCreateTrace trace_{};
};
// Owns the concrete adapters; borrowed credentials/stores/link outlive this object.
class SwapProductionGateway final {
public:
    SwapProductionGateway(GatewayLink&,SwapReconcileStore&,SwapPrepareStore&,SwapSubmissionStore&,
        SwapSubmissionStore& signer_requests,SwapGatewayCredentials&,SwapFinalizedReadClock&,
        const SwapRuntimeProfile&,SwapGatewayRuntimeConfig,SwapAccountStartupAdmission,
        std::optional<truemarkets::SwapExecutionPolicy> route_policy={});
    [[nodiscard]] SwapGatewayRuntime& runtime()noexcept{return runtime_;}
    [[nodiscard]] const SwapCreateTrace& create_trace()const noexcept{return ports_.create_trace();}
private:
    std::unique_ptr<swap_signer::Client> client_;
    SwapProductionPorts ports_;
    SwapDurableSigningPort signing_;
    SwapGatewayRuntime runtime_;
};
}
