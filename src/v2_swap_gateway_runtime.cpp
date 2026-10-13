#include "velocita/v2/swap_gateway_runtime.hpp"
#include "velocita/v2/swap_kernel.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <limits>

namespace velocita::v2 {
SwapDurableSigningPort::SwapDurableSigningPort(const SwapPrepareStore& p,SwapSubmissionStore& r,swap_signer::Client& client,SwapSigningConnectionConfig c)
    :preparation_(p),requests_(r),client_(client),config_(c){}
std::optional<SwapSignedExecution> SwapDurableSigningPort::sign_or_retrieve(std::uint64_t seq,std::int64_t index,std::uint64_t expiry){
    SwapSigningConnection connection(preparation_,requests_,client_,config_);
    const bool ok=connection.status()==SwapSigningConnectionStatus::idle?connection.sign(seq,index,expiry):connection.retrieve();
    if(!ok || !connection.execution())return {};
    return *connection.execution();
}
SwapGatewayRuntime::SwapGatewayRuntime(GatewayLink& link,SwapReconcileStore& recovery,SwapPrepareStore& preparation,SwapSubmissionStore& submissions,
    SwapPreparePort& prepare,SwapGatewaySigningPort& signer,SwapExecutionPort& execute,SwapGatewaySettlementPort& settlement,SwapGatewayRuntimeConfig config)
    :link_(link),recovery_(recovery),preparation_(preparation),submissions_(submissions),prepare_port_(prepare),signer_(signer),execute_(execute),settlement_(settlement),config_(config),sequence_(config.first_sequence){
    if(!config.timeout || !config.state_interval || !sequence_ || sequence_>=config.sequence_limit || !config.caller.value() || is_operator_caller(config.caller) ||
        config.preparation.mapping_identity!=fee_inclusive_prepare_mapping_identity() ||
        preparation.status()!=SwapPrepareStoreCode::none || preparation.latest().binding!=config.preparation ||
        submissions.status()!=SwapSubmissionCode::none || submissions.latest().binding!=SwapSubmissionBinding{config.preparation.allocation.incarnation,config.preparation.authority_domain} ||
        recovery.status()!=SwapReconcileStoreCode::none || recovery.latest().result.index<=0 ||
        recovery.latest().result.binding!=SwapReconcileStoreBinding{config.preparation.allocation.incarnation,config.preparation.authority_domain}){
        stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::identity);return;}
    seen_index_=std::max({recovery.latest().result.index,preparation.latest().committed_index,submissions.latest().committed_index});
    if(preparation.latest().phase!=SwapPreparePhase::idle)effect_=preparation.latest().operation.effect;
}
SwapGatewayRuntime::~SwapGatewayRuntime(){if(owns_link_)link_.abandon();}
void SwapGatewayRuntime::stop(SwapGatewayRuntimeStatus s,SwapGatewayRuntimeCause cause)noexcept{
    preparing_.reset();submitting_.reset();resolving_.reset();signed_.reset();
    if(owns_link_)link_.abandon();owns_link_=false;status_=s;cause_=cause;
}
void SwapGatewayRuntime::command(swap::Command body,Tick now,std::uint64_t event){
    if(sequence_>=config_.sequence_limit){stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::sequence_limit);return;}
    swap::Record record{swap::Role::gateway,event,std::move(body)};command_digest_=swap_record_digest(record);
    Command c{{config_.caller,RequestSequence{sequence_++},command_digest_},ApplySpotSwap{record}};
    owns_link_=link_.submit(c,ProposeLane::normal,now,config_.timeout);
    if(!owns_link_)stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::state);
}
bool SwapGatewayRuntime::prepared_matches(const swap::Pending& p)const noexcept{
    const auto& saved=preparation_.latest();const auto& v=saved.prepared;
    return saved.phase==SwapPreparePhase::prepared && saved.operation.effect==p.intent.effect && saved.binding==config_.preparation &&
        v.payload_digest==p.payload && v.handle.length && v.handle.length<=v.handle.bytes.size() &&
        sha256_digest(std::as_bytes(std::span(v.handle.bytes.data(),v.handle.length)))==p.handle;
}
void SwapGatewayRuntime::step(Tick now,std::uint64_t event){
    if(stopped())return;
    if(now<last_tick_ || event<last_event_){stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::state);return;}last_tick_=now;last_event_=event;
    if(recovery_.status()!=SwapReconcileStoreCode::none || preparation_.status()!=SwapPrepareStoreCode::none || submissions_.status()!=SwapSubmissionCode::none){
        stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::storage);return;}
    if(preparing_){const auto s=preparing_->poll(now,event);if(s==SwapPrepareSessionStatus::reading_state)return;
        preparing_.reset();status_=SwapGatewayRuntimeStatus::running;next_read_=0;
        if(s!=SwapPrepareSessionStatus::prepared)stop(SwapGatewayRuntimeStatus::blocked,SwapGatewayRuntimeCause::prepare_unknown);return;}
    if(submitting_){const auto s=submitting_->poll(now,event);if(s==SwapSubmissionSessionStatus::reading_state)return;
        submitting_.reset();signed_.reset();status_=SwapGatewayRuntimeStatus::running;next_read_=0;
        if(s!=SwapSubmissionSessionStatus::needs_reconciliation)stop(SwapGatewayRuntimeStatus::blocked,SwapGatewayRuntimeCause::state);return;}
    if(resolving_){const auto s=resolving_->poll(now);if(s==SwapSubmissionResolutionStatus::reading_state)return;
        resolving_.reset();stop(s==SwapSubmissionResolutionStatus::resolved?SwapGatewayRuntimeStatus::complete:SwapGatewayRuntimeStatus::blocked,
            s==SwapSubmissionResolutionStatus::resolved?SwapGatewayRuntimeCause::none:SwapGatewayRuntimeCause::settlement_unproven);return;}
    if(!link_.busy()){
        if(now>=next_read_){owns_link_=link_.request_state(now,config_.timeout);
            if(!owns_link_)stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::state);}
        return;
    }
    if(!owns_link_){stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::state);return;}
    const auto outcome=link_.pump(now);if(outcome.status==LinkStatus::pending || outcome.status==LinkStatus::idle)return;
    if(outcome.status!=LinkStatus::done){stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::state);return;}
    owns_link_=false;
    if(outcome.request==LinkRequest::command){
        if((outcome.result.code!=ApplyCode::applied && outcome.result.code!=ApplyCode::already_applied) || command_digest_.is_zero() ||
            recovery_.persist({recovery_.latest().result.binding,outcome.index,command_digest_})!=SwapReconcileStoreCode::none){
            stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::storage);return;}
        seen_index_=std::max(seen_index_,outcome.index);command_digest_={};next_read_=0;return;
    }
    if(outcome.request!=LinkRequest::state || outcome.reading.authority_domain!=config_.preparation.authority_domain){stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::identity);return;}
    next_read_=now>std::numeric_limits<Tick>::max()-config_.state_interval?std::numeric_limits<Tick>::max():now+config_.state_interval;
    if(outcome.reading.applied_index<seen_index_)return;
    seen_index_=outcome.reading.applied_index;observed(outcome.reading,now,event);
}
void SwapGatewayRuntime::observed(const StateReading& reading,Tick now,std::uint64_t event){
    const auto& state=link_.state();const auto& s=state.spot_swap;const auto pending=s.pending;
    if(!swap_state_bindings_valid(state) || !s.initialized || s.conflicted || s.opening.binding!=config_.preparation.allocation || event<s.event_time){
        stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::identity);return;}
    if(config_.recovery_only && submissions_.latest().phase!=SwapSubmissionPhase::execute_intent &&
       submissions_.latest().phase!=SwapSubmissionPhase::resolved){
        stop(SwapGatewayRuntimeStatus::blocked,SwapGatewayRuntimeCause::state);return;
    }
    if(config_.recovery_only && pending.phase!=swap::Phase::none && pending.phase!=swap::Phase::attempted && pending.phase!=swap::Phase::unknown){
        stop(SwapGatewayRuntimeStatus::blocked,SwapGatewayRuntimeCause::state);return;
    }
    if(pending.phase==swap::Phase::none){
        if(submissions_.latest().phase==SwapSubmissionPhase::execute_intent){
            resolving_=std::make_unique<SwapSubmissionResolutionSession>(link_,submissions_,SwapSubmissionResolutionConfig{config_.preparation.allocation,config_.preparation.authority_domain,config_.timeout});
            if(!resolving_->start(reading.applied_index,now)){stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::state);return;}
            status_=SwapGatewayRuntimeStatus::resolving;
        }else if(submissions_.latest().phase==SwapSubmissionPhase::resolved)stop(SwapGatewayRuntimeStatus::complete,SwapGatewayRuntimeCause::none);
        return;
    }
    if(effect_.value() && pending.intent.effect!=effect_){stop(SwapGatewayRuntimeStatus::blocked,SwapGatewayRuntimeCause::identity);return;}
    effect_=pending.intent.effect;
    if(submissions_.latest().phase!=SwapSubmissionPhase::idle){
        const auto& j=submissions_.latest().identity;
        if(j.effect!=effect_ || j.payload!=pending.payload || j.handle_digest!=pending.handle){
            stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::identity);return;}
    }
    if(pending.phase==swap::Phase::reserved){
        if(preparation_.latest().phase==SwapPreparePhase::create_intent){stop(SwapGatewayRuntimeStatus::blocked,SwapGatewayRuntimeCause::prepare_unknown);return;}
        if(preparation_.latest().phase==SwapPreparePhase::prepared){
            const auto& saved=preparation_.latest();const auto& p=saved.prepared;
            if(saved.operation.effect!=effect_ || saved.operation.intent_digest!=swap_record_digest({swap::Role::strategy,pending.intent.source_time,pending.intent})){
                stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::identity);return;}
            command(swap::Prepare{effect_,p.payload_digest,sha256_digest(std::as_bytes(std::span(p.handle.bytes.data(),p.handle.length))),1},now,event);return;
        }
        preparing_=std::make_unique<SwapPrepareSession>(link_,preparation_,prepare_port_,SwapPrepareSessionConfig{config_.preparation,SwapPrepareInputMapping::fee_inclusive_quote_input,config_.timeout});
        if(!preparing_->start(effect_,reading.applied_index,now)){stop(SwapGatewayRuntimeStatus::blocked,SwapGatewayRuntimeCause::prepare_unknown);return;}
        status_=SwapGatewayRuntimeStatus::preparing;return;
    }
    if(!prepared_matches(pending)){stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::identity);return;}
    if(pending.phase==swap::Phase::prepared || (pending.phase==swap::Phase::attempted && submissions_.latest().phase!=SwapSubmissionPhase::execute_intent)){
        if(!signed_){try{signed_=signer_.sign_or_retrieve(sequence_,reading.applied_index,pending.intent.sign_by);}catch(...){signed_.reset();}}
        if(!signed_ || signed_->identity.effect!=effect_ || signed_->identity.payload!=pending.payload || signed_->identity.handle_digest!=pending.handle){
            stop(SwapGatewayRuntimeStatus::blocked,SwapGatewayRuntimeCause::signer_refused);return;}
        if(pending.phase==swap::Phase::prepared){command(swap::Attempt{effect_,pending.payload},now,event);return;}
        submitting_=std::make_unique<SwapSubmissionSession>(link_,submissions_,execute_,SwapSubmissionSessionConfig{config_.preparation.allocation,config_.preparation.authority_domain,config_.timeout});
        const auto& request=signed_->request;
        if(!request.body_size || request.body_size>request.body.size()){
            stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::identity);return;}
        if(!submitting_->start(signed_->identity,std::as_bytes(std::span(request.body.data(),request.body_size)),reading.applied_index,now)){
            stop(SwapGatewayRuntimeStatus::blocked,SwapGatewayRuntimeCause::state);return;}
        status_=SwapGatewayRuntimeStatus::submitting;return;
    }
    if(pending.phase==swap::Phase::attempted){command(swap::MarkUnknown{effect_},now,event);return;}
    if(pending.phase==swap::Phase::unknown){
        std::optional<VerifiedSwapSettlement> verified;try{verified=settlement_.reconcile(pending);}catch(...){verified.reset();}
        if(!verified){stop(SwapGatewayRuntimeStatus::blocked,SwapGatewayRuntimeCause::settlement_unproven);return;}
        const auto& settled=verified->settlement();
        if(settled.effect!=effect_ || settled.payload!=pending.payload || settled.custody!=config_.preparation.allocation.custody){
            stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::identity);return;}
        command(settled,now,event);return;
    }
    stop(SwapGatewayRuntimeStatus::blocked,SwapGatewayRuntimeCause::state);
}
}
