#include "velocita/v2/swap_prepare_session.hpp"
#include "velocita/v2/swap_kernel.hpp"
#include "velocita/v2/digest.hpp"

#include <limits>

#if !defined(VELOCITA_SWAP_PREPARE_SESSION_TEST_ONLY) && !defined(VELOCITA_SWAP_RUNTIME_WIRING)
#error "Prepare session requires admitted create contract and input mapping before production use"
#endif

namespace velocita::v2 {
Digest256 fee_inclusive_prepare_mapping_identity() noexcept {
    constexpr std::string_view domain="velocita.tm.fee-inclusive-principal-plus-cap.v1";
    return sha256_digest(std::as_bytes(std::span(domain.data(),domain.size())));
}
SwapPrepareSession::SwapPrepareSession(GatewayLink& link,SwapPrepareStore& store,SwapPreparePort& port,SwapPrepareSessionConfig config)
    :link_(link),store_(store),port_(port),config_(config){
    if(store_.status()!=SwapPrepareStoreCode::none || !config_.state_timeout || store_.latest().binding!=config_.binding ||
        (config_.mapping!=SwapPrepareInputMapping::fixture_principal_and_base_input &&
         config_.mapping!=SwapPrepareInputMapping::fee_inclusive_quote_input))return;
    if(config_.mapping==SwapPrepareInputMapping::fee_inclusive_quote_input &&
        config_.binding.mapping_identity!=fee_inclusive_prepare_mapping_identity())return;
    if(config_.mapping==SwapPrepareInputMapping::fixture_principal_and_base_input &&
        config_.binding.mapping_identity==fee_inclusive_prepare_mapping_identity())return;
    effect_=store_.latest().operation.effect;
    if(store_.latest().phase==SwapPreparePhase::idle || store_.latest().phase==SwapPreparePhase::released)status_=SwapPrepareSessionStatus::idle;
    if(store_.latest().phase==SwapPreparePhase::create_intent)status_=SwapPrepareSessionStatus::needs_reconciliation;
    if(store_.latest().phase==SwapPreparePhase::prepared)status_=SwapPrepareSessionStatus::prepared;
}
SwapPrepareSession::~SwapPrepareSession(){if(status_==SwapPrepareSessionStatus::reading_state)link_.abandon();}
SwapPrepareSessionStatus SwapPrepareSession::finish(SwapPrepareSessionStatus status)noexcept{
    if(status_==SwapPrepareSessionStatus::reading_state)link_.abandon();status_=status;return status;
}
const truemarkets::SwapRestPrepared* SwapPrepareSession::prepared()const noexcept{
    if(status_!=SwapPrepareSessionStatus::prepared || store_.status()!=SwapPrepareStoreCode::none ||
        store_.latest().phase!=SwapPreparePhase::prepared || store_.latest().binding!=config_.binding ||
        store_.latest().operation.effect!=effect_)return nullptr;
    return &store_.latest().prepared;
}
bool SwapPrepareSession::start(EffectId effect,std::int64_t minimum_commit_index,Tick now,std::uint64_t event_time){
    if((status_!=SwapPrepareSessionStatus::idle && status_!=SwapPrepareSessionStatus::prepared) || link_.busy())return false;
    if(store_.status()!=SwapPrepareStoreCode::none || store_.latest().binding!=config_.binding ||
        minimum_commit_index<store_.latest().committed_index || now>std::numeric_limits<Tick>::max()-config_.state_timeout){
        (void)finish(SwapPrepareSessionStatus::refused);return false;
    }
    if(store_.latest().phase==SwapPreparePhase::prepared && effect==store_.latest().operation.effect){
        effect_=effect;status_=SwapPrepareSessionStatus::prepared;return true;
    }
    if(store_.latest().phase==SwapPreparePhase::create_intent || effect.value()<=store_.latest().operation.effect.value()){
        (void)finish(SwapPrepareSessionStatus::needs_reconciliation);return false;
    }
    effect_=effect;minimum_index_=minimum_commit_index;ordinal_=store_.latest().ordinal;checksum_=store_.latest().checksum;
    last_tick_=now;deadline_=now+config_.state_timeout;requested_ms_=event_time;
    if(!link_.request_state(now,config_.state_timeout)){(void)finish(SwapPrepareSessionStatus::refused);return false;}
    status_=SwapPrepareSessionStatus::reading_state;return true;
}
SwapPrepareSessionStatus SwapPrepareSession::poll(Tick now,std::uint64_t event_time){
    if(status_!=SwapPrepareSessionStatus::reading_state)return status_;
    if(now<last_tick_ || now>=deadline_)return finish(SwapPrepareSessionStatus::refused);
    last_tick_=now;
    if(store_.status()!=SwapPrepareStoreCode::none)return finish(SwapPrepareSessionStatus::storage_failed);
    if(store_.latest().ordinal!=ordinal_ || store_.latest().checksum!=checksum_)return finish(SwapPrepareSessionStatus::refused);
    const auto outcome=link_.pump(now);
    if(outcome.status==LinkStatus::pending || outcome.status==LinkStatus::idle)return status_;
    if(outcome.status!=LinkStatus::done || outcome.request!=LinkRequest::state || outcome.reading.authority_domain!=config_.binding.authority_domain ||
        outcome.reading.applied_index<minimum_index_ || outcome.reading.applied_index<=store_.latest().committed_index || outcome.reading.state_digest.is_zero())
        return finish(SwapPrepareSessionStatus::refused);
    const auto& state=link_.state();const auto& swap=state.spot_swap;const auto& pending=swap.pending;const auto& intent=pending.intent;
    if(!swap_state_bindings_valid(state) || !swap.initialized || swap.conflicted || swap.opening.binding!=config_.binding.allocation ||
        pending.phase!=swap::Phase::reserved || intent.effect!=effect_ || intent.kill_epoch!=swap.kill_epoch ||
        intent.executor_generation!=swap.executor_generation || swap.mode==swap::Mode::halted ||
        (intent.side==Side::buy && swap.mode!=swap::Mode::normal) || event_time<swap.event_time || event_time<intent.source_time || event_time>=intent.sign_by)
        return finish(SwapPrepareSessionStatus::refused);
    CallerVerifiedPreparePredecessor predecessor;const CallerVerifiedPreparePredecessor* prior{};
    if(store_.latest().phase==SwapPreparePhase::prepared){
        for(std::size_t n=0;n<swap.settlement_count;++n){const auto& settled=swap.settlements[n];
            if(settled.effect!=store_.latest().operation.effect)continue;
            if(prior || settled.payload!=store_.latest().prepared.payload_digest || settled.custody!=config_.binding.allocation.custody ||
                !settled.finalized || settled.evidence.is_zero() ||
                settled.transaction.is_zero()!=(settled.outcome==swap::Outcome::expired_unexecuted))return finish(SwapPrepareSessionStatus::needs_reconciliation);
            predecessor={settled,outcome.reading.applied_index,outcome.reading.state_digest};prior=&predecessor;
        }
        if(!prior)return finish(SwapPrepareSessionStatus::needs_reconciliation);
    }
    truemarkets::SwapRestInput input{intent.side,intent.side==Side::buy?intent.quote_input.value():intent.base_input.value()};
    if(config_.mapping==SwapPrepareInputMapping::fee_inclusive_quote_input && intent.side==Side::buy){
        if(intent.quote_input.value()>std::numeric_limits<std::uint64_t>::max()-intent.fee_cap.value())
            return finish(SwapPrepareSessionStatus::refused);
        input.quantity_atoms=intent.quote_input.value()+intent.fee_cap.value();
        if(input.quantity_atoms>intent.max_quote_debit.value())return finish(SwapPrepareSessionStatus::refused);
    }
    truemarkets::SwapRestRequest request;
    if(truemarkets::encode_swap_rest_create(input,request)!=truemarkets::SwapRestCode::encoded)return finish(SwapPrepareSessionStatus::refused);
    const auto request_digest=sha256_digest(std::as_bytes(std::span(request.body.data(),request.body_size)));
    const auto intent_digest=swap_record_digest({swap::Role::strategy,intent.source_time,intent});
    if(store_.begin({effect_,intent_digest,request_digest,input},outcome.reading.applied_index,outcome.reading.state_digest,prior)!=SwapPrepareStoreCode::none)
        return finish(SwapPrepareSessionStatus::storage_failed);
    const auto durable_ordinal=store_.latest().ordinal;const auto durable_checksum=store_.latest().checksum;
    status_=SwapPrepareSessionStatus::needs_reconciliation;
    transport::SwapHttpsResult response;
    try{response=port_.create(request,{outcome.reading,swap,requested_ms_,event_time});}catch(...){return status_;}
    if(store_.status()!=SwapPrepareStoreCode::none)return finish(SwapPrepareSessionStatus::storage_failed);
    if(store_.latest().ordinal!=durable_ordinal || store_.latest().checksum!=durable_checksum)return finish(SwapPrepareSessionStatus::refused);
    if(response.status!=transport::SwapHttpsStatus::complete || response.exposure!=transport::SwapHttpsExposure::response_received || response.body_size>response.body.size())return status_;
    truemarkets::SwapRestPrepared prepared;
    if(truemarkets::decode_swap_rest_prepare(response.http_status,response.body_view(),input,prepared)!=truemarkets::SwapRestCode::decoded_unsigned_only)return status_;
    if(config_.mapping==SwapPrepareInputMapping::fee_inclusive_quote_input &&
        (prepared.quoted_fee>intent.fee_cap.value() ||
         (intent.side==Side::buy && (prepared.quoted_fee>prepared.quoted_input ||
          prepared.quoted_input-prepared.quoted_fee!=intent.quote_input.value()))))return status_;
    const swap::Pending candidate{swap::Phase::prepared,intent,prepared.payload_digest,
        sha256_digest(std::as_bytes(std::span(prepared.handle.bytes.data(),prepared.handle.length)))};
    bool admitted=false;
    try{admitted=port_.admit(candidate,prepared.payload_view());}catch(...){admitted=false;}
    if(!admitted)return status_;
    const auto response_digest=sha256_digest(std::as_bytes(std::span(response.body.data(),response.body_size)));
    if(store_.complete(prepared,response_digest)!=SwapPrepareStoreCode::none)return finish(SwapPrepareSessionStatus::storage_failed);
    return finish(SwapPrepareSessionStatus::prepared);
}
}
