#include "v2_swap_signer_test_support.hpp"
#include "v2_swap_transaction_verifier_test_support.hpp"
#include "velocita/v2/swap_settlement_fence.hpp"

namespace {
using namespace swap_signer_test;
struct VerifiedRig : Rig {
    velocita::v2::test_support::RouteFixture tx;
    SwapSignerVerificationContext reviewed;
    SwapSignerConfig cfg{config()};
    std::string payload;
    explicit VerifiedRig(std::uint64_t payment=2'039'280,std::uint64_t age=100,std::uint64_t cap=2'039'280,std::uint64_t minimum=6'819'647,unsigned pool=0,bool selling=false)
        :tx(pool){
        if(selling)tx.make_sell();
        tx.payment=payment;tx.policy.accepted_costs.max_output_deduction=swap::BaseAtoms{cap};if(!selling)tx.pending.intent.min_base_receive=swap::BaseAtoms{minimum};
        tx.policy.max_envelope_age_ms=age;tx.policy.binding=cfg.binding;tx.pending.intent.binding=cfg.binding;
        reviewed={*truemarkets::tm_text("verifier-fixture"),*truemarkets::tm_text(std::string(31,'1')+"3"),tx.policy};
        cfg.transaction_policy_digest=swap_signer_verification_digest(reviewed);
        require(!cfg.transaction_policy_digest.is_zero(),"reviewed_context_has_identity");
        payload=tx.envelope();auto& state=voter.state.spot_swap;
        state.pending.intent=tx.pending.intent;state.pending.payload=sha256_digest(bytes());
        state.opening.limits.max_buy_price_per_sol=swap::QuoteAtoms{146'635'156};
        state.opening.limits.max_purchase_total=swap::QuoteAtoms{1'000'000};
        state.opening.limits.max_buy_debit=swap::QuoteAtoms{1'000'000};
        state.opening.limits.max_fee=swap::QuoteAtoms{2'000};
        state.opening.limits.max_realized_loss=swap::QuoteAtoms{500'000};
        state.opening.limits.max_sell_input=swap::BaseAtoms{250'000'000};
        state.reserved_quote=swap::QuoteAtoms{1'000'000};state.event_time=1000;clock.event=1001;
        if(selling){
            state.opening.base=swap::BaseAtoms{120'000'000};state.opening.base_cost=swap::QuoteAtoms{12'000'000};
            state.base=state.opening.base;state.cost=state.opening.base_cost;state.opening.limits.max_fee=swap::QuoteAtoms{50'000};
            state.reserved_quote={};state.reserved_base=swap::BaseAtoms{120'000'000};
        }
        session.reset();store.reset();binding=swap_signer_journal_binding(cfg,key.public_key);
        store=std::make_unique<SwapSubmissionStore>(scratch.base/"verified-replay",binding,SwapSubmissionFaultHook{Fault::proceed,&fault});
        require(store->create({binding,1,digest(51),EffectId{0}})==SwapSubmissionCode::none,"reviewed_journal_created");
        reopen();
    }
    std::span<const std::byte> bytes()const{return std::as_bytes(std::span(payload.data(),payload.size()));}
    void reopen(){session=std::make_unique<SwapSignerSession>(link,*store,*key.protected_key,cfg,clock.interface(),reviewed);}
    bool begin(){return session->start(EffectId{1},1,handle,bytes(),10,1100,tx.prefix());}
    bool begin(const truemarkets::JupiterLookupPrefix& lookup){return session->start(EffectId{1},1,handle,bytes(),10,1100,lookup);}
    bool recover_stamp(){return session->retrieve(EffectId{1},1,handle,bytes(),10,1100);}
    bool refused_unsigned(){return session->status()==SwapSignerStatus::refused && key.protected_key->signature_attempts()==0 &&
        voter.authority_requests==0 && store->latest().phase==SwapSubmissionPhase::idle;}
};
void verified_signature_and_recovery(){
    VerifiedRig r;require(r.begin(),"verified_payload_starts");
    r.reviewed.policy.accepted_costs.max_output_deduction=swap::BaseAtoms{0};
    require(r.finish()==SwapSignerStatus::signed_once,"verified_payload_signs_after_fresh_authority");
    require(r.key.verifies(r.bytes(),r.session->signature_der()) && r.voter.authority_requests==1,
        "real_format_exact_bytes_signature_and_authority");
    const auto stamp=std::string(r.session->stamp().view());
    r.reviewed.policy= r.tx.policy;r.session.reset();r.store.reset();
    r.store=std::make_unique<SwapSubmissionStore>(r.scratch.base/"verified-replay",r.binding);
    require(r.store->recover()==SwapSubmissionCode::none,"verified_result_durable");r.reopen();
    require(r.recover_stamp() && r.session->stamp().view()==stamp && r.key.protected_key->signature_attempts()==1,
        "verified_restart_returns_same_stamp_without_resigning");
    r.payload.back()=' ';require(!r.recover_stamp() && r.session->stamp().empty(),"changed_payload_cannot_recover_stamp");
}
void six_settled_signatures(){
    VerifiedRig r;
    auto& state=r.voter.state.spot_swap;
    state.opening.quote=swap::QuoteAtoms{25'000'000};state.quote=state.opening.quote;
    state.opening.limits.max_purchase_total=swap::QuoteAtoms{3'000'000'000};
    for(unsigned n=1;n<=6;++n){
        require(r.session->start(EffectId{n},n,handle,r.bytes(),r.voter.index,state.pending.intent.sign_by,r.tx.prefix()),
            "next settled effect starts independently authorized read");
        require(r.finish()==SwapSignerStatus::signed_once,"six distinct effects sign in one protected session");
        require(r.key.protected_key->signature_attempts()==n,"exactly one signature per effect");
        const auto prior=state.pending;
        require(swap::apply(state,swap::Attempt{EffectId{n},prior.payload},swap::Role::gateway,r.clock.event)==swap::Code::applied,"attempt applied");
        swap::Settlement fill;fill.effect=EffectId{n};fill.custody=state.opening.binding.custody;fill.payload=prior.payload;
        fill.transaction=digest(100+n);fill.evidence=digest(120+n);fill.finalized=true;fill.outcome=swap::Outcome::succeeded;
        fill.quote_debit=swap::QuoteAtoms{1'000'000};fill.base_credit=swap::BaseAtoms{8'000'000};fill.fee=swap::QuoteAtoms{2'000};
        require(swap::apply(state,fill,swap::Role::gateway,r.clock.event)==swap::Code::applied,"settlement applied");
        if(n==6)break;
        auto next=prior.intent;next.effect=EffectId{n+1};next.source_time+=1000;next.sign_by+=1000;
        r.clock.event+=1000;r.tx.timestamp+=1000;r.payload=r.tx.envelope();r.voter.index+=10;
        require(swap::apply(state,next,swap::Role::strategy,r.clock.event)==swap::Code::applied,"next intent applied");
        require(swap::apply(state,swap::Prepare{next.effect,sha256_digest(r.bytes()),prior.handle,1},swap::Role::gateway,r.clock.event)==swap::Code::applied,"next prepare applied");
        if(n==2 || n==4){r.session.reset();r.store.reset();
            r.store=std::make_unique<SwapSubmissionStore>(r.scratch.base/"verified-replay",r.binding);
            require(r.store->recover()==SwapSubmissionCode::none,"restart retains previous signature fence");r.reopen();}
    }
    require(state.settlement_count==6 && state.quote.value()==19'000'000,"six verified signatures preserve settlement accounting");
}
void predecessor_refusals(){
    VerifiedRig r;require(r.begin() && r.finish()==SwapSignerStatus::signed_once,"predecessor signed");
    const auto fence=r.store->latest();auto& state=r.voter.state.spot_swap;
    state.last_effect=2;state.pending.intent.effect=EffectId{2};r.voter.index=20;
    require(r.session->start(EffectId{2},2,handle,r.bytes(),20,1100,r.tx.prefix()),"next effect may ask for proof");
    require(r.finish()==SwapSignerStatus::refused && r.key.protected_key->signature_attempts()==1 &&
        r.store->latest().checksum==fence.checksum && r.voter.authority_requests==1,"missing prior settlement cannot advance fence or sign");
    auto proven=state;proven.pending={};proven.settlement_count=1;
    auto& settled=proven.settlements[0];settled.effect=fence.identity.effect;settled.payload=fence.identity.payload;
    settled.custody=state.opening.binding.custody;settled.finalized=true;settled.transaction=digest(80);settled.evidence=digest(81);
    require(swap_settled_predecessor(proven,fence,state.opening.binding.custody),"exact terminal predecessor proof admitted");
    for(unsigned n=0;n<8;++n){auto bad=proven;
        if(n==0)bad.settlements[0].payload=digest(82);
        if(n==1)bad.settlements[0].custody=digest(82);
        if(n==2)bad.settlements[0].finalized=false;
        if(n==3)bad.settlements[0].transaction={};
        if(n==4)bad.settlements[0].evidence={};
        if(n==5){bad.settlement_count=2;bad.settlements[1]=bad.settlements[0];}
        if(n==6){bad.pending.phase=swap::Phase::unknown;bad.pending.intent.effect=fence.identity.effect;}
        if(n==7)bad.settlement_count=0;
        require(!swap_settled_predecessor(bad,fence,state.opening.binding.custody),"incomplete or conflicting predecessor refused");
    }
    VerifiedRig replay;require(replay.begin() && replay.finish()==SwapSignerStatus::signed_once,"replay fixture signed");
    require(!replay.begin() && replay.key.protected_key->signature_attempts()==1,"same effect cannot request a second signature");
    VerifiedRig stale;require(stale.begin() && stale.finish()==SwapSignerStatus::signed_once,"stale fixture signed");
    stale.voter.state.spot_swap.last_effect=2;stale.voter.state.spot_swap.pending.intent.effect=EffectId{2};
    require(stale.session->start(EffectId{2},2,handle,stale.bytes(),20,1100,stale.tx.prefix()),"stale proof requested");
    require(stale.finish()==SwapSignerStatus::refused && stale.key.protected_key->signature_attempts()==1,"stale committed read cannot advance signer");
}
void live_scope(){
    VerifiedRig budget;budget.voter.state.spot_swap.opening.limits.max_purchase_total=swap::QuoteAtoms{3'000'000'000};
    require(budget.begin() && budget.finish()==SwapSignerStatus::signed_once,"envelope_purchase_budget_signs");
    VerifiedRig wider;wider.voter.state.spot_swap.opening.limits.max_purchase_total=swap::QuoteAtoms{3'000'000'001};
    require(wider.begin(),"wider_budget_request_starts");(void)wider.finish();
    require(wider.refused_unsigned(),"purchase_budget_above_envelope_refused");
    VerifiedRig later;auto& next=later.voter.state.spot_swap;next.last_effect=240;next.pending.intent.effect=EffectId{240};
    require(later.session->start(EffectId{240},1,handle,later.bytes(),10,1100,later.tx.prefix()) && later.finish()==SwapSignerStatus::signed_once,
        "order_240_signs_without_released_predecessors");
    VerifiedRig beyond;auto& over=beyond.voter.state.spot_swap;over.last_effect=241;over.pending.intent.effect=EffectId{241};
    require(beyond.session->start(EffectId{241},1,handle,beyond.bytes(),10,1100,beyond.tx.prefix()),"order_241_starts_read");(void)beyond.finish();
    require(beyond.session->status()==SwapSignerStatus::refused && beyond.key.protected_key->signature_attempts()==0,"order_241_refused");
    VerifiedRig loss;loss.voter.state.spot_swap.opening.limits.max_realized_loss=swap::QuoteAtoms{10'000'001};
    require(loss.begin(),"larger_loss_request_starts");(void)loss.finish();
    require(loss.refused_unsigned(),"loss_allowance_above_ten_dollars_refused");
    VerifiedRig cost(2'039'280,100,2'039'281);
    require(cost.begin(),"broader_cost_request_starts");(void)cost.finish();
    require(cost.refused_unsigned(),"larger_repayment_allowance_refused");
}
void sell_signs_under_live_scope(){
    VerifiedRig r(2'039'280,100,2'039'280,6'819'647,0,true);
    require(r.begin() && r.finish()==SwapSignerStatus::signed_once,"verified_sell_signs_once");
    require(r.key.verifies(r.bytes(),r.session->signature_der()),"sell_signature_covers_exact_payload");
    VerifiedRig floor(2'039'280,100,2'039'280,6'819'647,0,true);floor.voter.state.spot_swap.pending.intent.min_quote_receive=swap::QuoteAtoms{12'437'501};
    require(floor.begin(),"higher_sell_floor_starts");(void)floor.finish();
    require(floor.refused_unsigned(),"sell_floor_above_route_refused");
}
void conservative_mvp(){
    VerifiedRig r;
    require(r.begin() && r.finish()==SwapSignerStatus::signed_once,"conservative_cap_signs_supported_buy");
    require(r.key.verifies(r.bytes(),r.session->signature_der()),"conservative_signature_covers_exact_payload");
    VerifiedRig floor(2'039'280,100,2'039'280,7'324'290);
    require(floor.begin(),"one_lamport_higher_floor_starts");(void)floor.finish();
    require(floor.refused_unsigned(),"rounded_down_floor_does_not_waive_net_minimum");
}
void second_pool_same_journal(){
    VerifiedRig first;VerifiedRig second(2'039'280,100,2'039'280,6'819'647,1);second.tx.variant=18;
    second.payload=second.tx.envelope();second.voter.state.spot_swap.pending.payload=sha256_digest(second.bytes());
    require(swap_signer_journal_binding(second.cfg,first.key.public_key)==first.binding &&
        first.cfg.transaction_policy_digest==second.cfg.transaction_policy_digest,
        "different_pool_same_protected_journal_identity");
    require(second.begin() && second.finish()==SwapSignerStatus::signed_once,"different_pool_and_table_sign_without_adoption");
    require(first.tx.prefix().table!=second.tx.prefix().table,"pools_use_different_lookup_tables");
}
void lookup_evidence(){
    VerifiedRig empty;require(empty.begin(truemarkets::JupiterLookupPrefix{}),"empty_prefix_request_starts");(void)empty.finish();
    require(empty.refused_unsigned(),"missing_lookup_prefix_refused_before_fence");
    VerifiedRig swapped;auto prefix=swapped.tx.prefix();prefix.addresses[7]=velocita::v2::test_support::dex_program(177);
    require(swapped.begin(prefix),"substituted_prefix_request_starts");(void)swapped.finish();
    require(swapped.refused_unsigned(),"substituted_lookup_program_refused_before_fence");
    VerifiedRig other;prefix=other.tx.prefix();prefix.table[0]^=1;
    require(other.begin(prefix),"other_table_request_starts");(void)other.finish();
    require(other.refused_unsigned(),"other_lookup_table_refused_before_fence");
}
void economic_and_context_refusal(){
    VerifiedRig r(2'039'281);require(r.begin(),"recorded_payment_starts");(void)r.finish();
    require(r.refused_unsigned(),"recorded_payment_exceeds_accepted_cap_before_fence");
    VerifiedRig revised;revised.reviewed.policy.platform_fee[0]^=1;revised.reopen();
    require(revised.session->status()==SwapSignerStatus::closed,"changed_reviewed_policy_cannot_reuse_journal");
    VerifiedRig missing;missing.session=std::make_unique<SwapSignerSession>(missing.link,*missing.store,*missing.key.protected_key,missing.cfg,missing.clock.interface());
    require(missing.session->status()==SwapSignerStatus::closed,"missing_reviewed_context_closes_policy_bound_session");
    VerifiedRig org;org.reviewed.organization=*truemarkets::tm_text("different-org");org.reopen();
    require(org.session->status()==SwapSignerStatus::closed,"changed_organization_cannot_reuse_journal");
}
void freshness_and_authority(){
    VerifiedRig r(2'039'280,20);require(r.begin(),"freshness_fixture_starts");
    while(r.session->status()==SwapSignerStatus::reading_state)(void)r.session->poll();
    require(r.session->status()==SwapSignerStatus::reading_authority,"semantic_verification_precedes_authority");
    r.clock.event=1020;(void)r.finish();
    require(r.session->status()==SwapSignerStatus::refused && r.key.protected_key->signature_attempts()==0,
        "expired_verified_proof_cannot_sign_after_authority");
    VerifiedRig a;a.voter.authority_bad_mac=true;require(a.begin(),"bad_authority_starts");(void)a.finish();a.clock.tick=1020;(void)a.session->poll();
    require(a.session->status()==SwapSignerStatus::refused && a.key.protected_key->signature_attempts()==0,"verified_payload_still_needs_authenticated_authority");
    VerifiedRig recovered(2'039'280,20);require(recovered.begin() && recovered.finish()==SwapSignerStatus::signed_once,"expiry_recovery_fixture_signs");
    recovered.clock.event=1020;require(!recovered.recover_stamp() && recovered.session->stamp().empty(),"expired_payload_cannot_recover_stamp");
}
}
int main(int argc,char**argv){
    const std::pair<const char*,void(*)()> cases[]{{"predecessor_refusals",predecessor_refusals},{"six_settled_signatures",six_settled_signatures},{"verified_signature_and_recovery",verified_signature_and_recovery},{"conservative_mvp",conservative_mvp},{"live_scope",live_scope},{"sell_signs_under_live_scope",sell_signs_under_live_scope},
        {"economic_and_context_refusal",economic_and_context_refusal},{"freshness_and_authority",freshness_and_authority},
        {"second_pool_same_journal",second_pool_same_journal},{"lookup_evidence",lookup_evidence}};
    unsigned ran{},failed{};for(const auto&[name,fn]:cases){if(argc==2 && std::string_view(argv[1])!=name)continue;++ran;
        try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return ran&&!failed?0:1;
}
