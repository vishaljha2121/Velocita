#pragma once
#include "velocita/v2/swap_runtime_config.hpp"
#include "velocita/v2/swap_rest_codec.hpp"
#include "velocita/v2/swap_submission_session.hpp"
#include "velocita/v2/swap_settlement_evidence.hpp"
namespace velocita::v2 {
class SwapRuntimeExecutionHttps final:public SwapExecutionPort {
public:
    SwapRuntimeExecutionHttps(SwapGatewayCredentials&,SwapFinalizedReadClock&,
        SwapSubmissionIdentity,truemarkets::SwapRestRequest,long timeout_ms=2000);
    void execute(const SwapSubmissionIdentity&,std::span<const std::byte>)override;
    [[nodiscard]] const transport::SwapHttpsResult& last_result()const noexcept{return result_;}
    [[nodiscard]] bool used()const noexcept{return used_;}
#ifdef VELOCITA_SWAP_RUNTIME_HTTPS_TEST_ONLY
    [[nodiscard]] static std::unique_ptr<SwapRuntimeExecutionHttps> local_fixture(SwapGatewayCredentials&,SwapFinalizedReadClock&,
        SwapSubmissionIdentity,truemarkets::SwapRestRequest,std::string origin,std::string ca_bundle,long timeout_ms=2000);
#endif
private:
    SwapRuntimeExecutionHttps(SwapGatewayCredentials&,SwapFinalizedReadClock&,SwapSubmissionIdentity,
        truemarkets::SwapRestRequest,transport::SwapHttpsConfig);
    SwapGatewayCredentials& credentials_;SwapFinalizedReadClock& clock_;
    SwapSubmissionIdentity expected_{};truemarkets::SwapRestRequest request_{};
    transport::SwapHttpsClient client_;transport::SwapHttpsResult result_{};
    bool valid_{},used_{};
};
[[nodiscard]] bool swap_canonical_rpc_request(std::string_view body)noexcept;
class SwapRuntimeFinalizedHttps final:public SwapFinalizedRpcPort {
public:
    explicit SwapRuntimeFinalizedHttps(std::uint64_t maximum_timeout_ms=2000);
    [[nodiscard]] Digest256 provider_identity()const noexcept override{return provider_;}
    [[nodiscard]] SettlementRpcResponse request(std::string_view,std::uint64_t timeout_ms)override;
#ifdef VELOCITA_SWAP_RUNTIME_HTTPS_TEST_ONLY
    [[nodiscard]] static std::unique_ptr<SwapRuntimeFinalizedHttps> local_fixture(std::string origin,
        std::string ca_bundle,std::uint64_t maximum_timeout_ms=2000);
#endif
private:
    SwapRuntimeFinalizedHttps(transport::SwapHttpsConfig,std::uint64_t);
    transport::SwapHttpsConfig config_;Digest256 provider_{};std::uint64_t maximum_timeout_{};
};
}
