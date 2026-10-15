#pragma once
#include "velocita/v2/swap_prepare_session.hpp"
#include "velocita/v2/swap_signing_connection.hpp"
#include "velocita/v2/swap_submission_session.hpp"
#include "velocita/v2/swap_submission_resolution.hpp"
#include "velocita/v2/swap_reconcile_store.hpp"
#include "velocita/v2/swap_settlement_admission.hpp"
#include <memory>

namespace velocita::v2 {
class SwapGatewaySigningPort {
public:
    virtual ~SwapGatewaySigningPort()=default;
    [[nodiscard]] virtual bool settled(const swap::State&,const StateReading&){return false;}
    [[nodiscard]] virtual std::optional<SwapSignedExecution> sign_or_retrieve(
        std::uint64_t sequence,std::int64_t prepared_index,std::uint64_t sign_by)=0;
};
class SwapDurableSigningPort final:public SwapGatewaySigningPort {
public:
    SwapDurableSigningPort(const SwapPrepareStore&,SwapSubmissionStore&,swap_signer::Client&,SwapSigningConnectionConfig,SwapLookupSource&);
    [[nodiscard]] std::optional<SwapSignedExecution> sign_or_retrieve(std::uint64_t,std::int64_t,std::uint64_t)override;
    [[nodiscard]] bool settled(const swap::State&,const StateReading&)override;
private:
    const SwapPrepareStore& preparation_;SwapSubmissionStore& requests_;swap_signer::Client& client_;SwapSigningConnectionConfig config_;
    SwapLookupSource& lookups_;
};
class SwapGatewaySettlementPort {
public:
    virtual ~SwapGatewaySettlementPort()=default;
    [[nodiscard]] virtual std::optional<VerifiedSwapSettlement> reconcile(const swap::Pending&)=0;
};
struct SwapGatewayRuntimeConfig {
    SwapPrepareBinding preparation{};
    CallerId caller{};
    std::uint64_t first_sequence{},sequence_limit{};
    Tick timeout{},state_interval{};
    bool recovery_only{};
    Tick settlement_retry{},settlement_window{};
    bool continuous{};
    Tick read_outage{};
};
enum class SwapGatewayRuntimeStatus:std::uint8_t {running,preparing,submitting,resolving,blocked,failed,complete};
enum class SwapGatewayRuntimeCause:std::uint8_t {none,state,storage,identity,prepare_unknown,signer_refused,settlement_unproven,sequence_limit};
// A copy of an authenticated committed read, never a quote or a command response.
// The timestamp is observation time, not the chain execution timestamp.
struct SwapGatewayObservation {
    swap::State state{};
    StateReading reading{};
    std::uint64_t observed_at_ms{};
};
class SwapGatewayRuntime final {
public:
    SwapGatewayRuntime(GatewayLink&,SwapReconcileStore&,SwapPrepareStore&,SwapSubmissionStore&,
        SwapPreparePort&,SwapGatewaySigningPort&,SwapExecutionPort&,SwapGatewaySettlementPort&,SwapGatewayRuntimeConfig);
    ~SwapGatewayRuntime();
    SwapGatewayRuntime(const SwapGatewayRuntime&)=delete;
    SwapGatewayRuntime& operator=(const SwapGatewayRuntime&)=delete;
    void step(Tick now,std::uint64_t event_time);
    [[nodiscard]] SwapGatewayRuntimeStatus status()const noexcept{return status_;}
    [[nodiscard]] SwapGatewayRuntimeCause cause()const noexcept{return cause_;}
    [[nodiscard]] std::uint64_t next_sequence()const noexcept{return sequence_;}
    [[nodiscard]] const std::optional<SwapGatewayObservation>& observation()const noexcept{return observation_;}
    [[nodiscard]] bool stopped()const noexcept{return status_==SwapGatewayRuntimeStatus::blocked || status_==SwapGatewayRuntimeStatus::failed || status_==SwapGatewayRuntimeStatus::complete;}
    void extend_sequence_limit(std::uint64_t limit)noexcept{if(limit>config_.sequence_limit)config_.sequence_limit=limit;}
private:
    void stop(SwapGatewayRuntimeStatus,SwapGatewayRuntimeCause)noexcept;
    void command(swap::Command,Tick,std::uint64_t);
    void observed(const StateReading&,Tick,std::uint64_t);
    [[nodiscard]] bool prepared_matches(const swap::Pending&)const noexcept;
    GatewayLink& link_;SwapReconcileStore& recovery_;SwapPrepareStore& preparation_;SwapSubmissionStore& submissions_;
    SwapPreparePort& prepare_port_;SwapGatewaySigningPort& signer_;SwapExecutionPort& execute_;SwapGatewaySettlementPort& settlement_;
    SwapGatewayRuntimeConfig config_;
    std::unique_ptr<SwapPrepareSession> preparing_;
    std::unique_ptr<SwapSubmissionSession> submitting_;
    std::unique_ptr<SwapSubmissionResolutionSession> resolving_;
    std::optional<SwapSignedExecution> signed_;
    // Port calls can block. Start the next Raft deadline on a fresh loop tick.
    std::optional<swap::Command> queued_command_;
    bool owns_link_{};
    EffectId effect_{};
    Digest256 command_digest_{};
    std::uint64_t sequence_{};std::int64_t seen_index_{};Tick next_read_{},last_tick_{},unproven_since_{},read_outage_since_{};std::uint64_t last_event_{};
    SwapGatewayRuntimeStatus status_{SwapGatewayRuntimeStatus::running};
    SwapGatewayRuntimeCause cause_{SwapGatewayRuntimeCause::none};
    std::optional<SwapGatewayObservation> observation_;
};
}
