#include "v2_swap_signer_test_support.hpp"
#include "v2_swap_transaction_verifier_test_support.hpp"

namespace {
using namespace swap_signer_test;
struct VerifiedRig : Rig {
    velocita::v2::test_support::DirectBuyFixture tx;
    SwapSignerVerificationContext reviewed;
    SwapSignerConfig cfg{config()};
    std::string payload;
    explicit VerifiedRig(std::uint64_t payment=550'840,std::uint64_t age=100,std::uint64_t policy_expiry=2000,std::uint64_t cap=550'840,std::uint64_t minimum=8'389'252){
        tx.payment=payment;tx.policy.accepted_costs.max_output_deduction=swap::BaseAtoms{cap};tx.pending.intent.min_base_receive=swap::BaseAtoms{minimum};tx.policy.max_envelope_age_ms=age;tx.policy.expires_at_ms=policy_expiry;tx.policy.binding=cfg.binding;tx.pending.intent.binding=cfg.binding;
        reviewed={*truemarkets::tm_text("verifier-fixture"),*truemarkets::tm_text(std::string(31,'1')+"3"),tx.policy,tx.lookup[0]};
        cfg.transaction_policy_digest=swap_signer_verification_digest(reviewed);
        require(!cfg.transaction_policy_digest.is_zero(),"reviewed_context_has_identity");
        payload=tx.envelope();auto& state=voter.state.spot_swap;
        state.pending.intent=tx.pending.intent;state.pending.payload=sha256_digest(bytes());
        if(cap==2'039'280)state.opening.limits.max_buy_price_per_sol=swap::QuoteAtoms{145'000'000};
        state.opening.limits.max_purchase_total=swap::QuoteAtoms{1'000'000};
        state.opening.limits.max_buy_debit=swap::QuoteAtoms{1'000'000};
        state.opening.limits.max_fee=swap::QuoteAtoms{2'000};
        state.opening.limits.max_realized_loss=swap::QuoteAtoms{500'000};
        state.reserved_quote=swap::QuoteAtoms{1'000'000};state.event_time=1000;clock.event=1001;
        session.reset();store.reset();binding=swap_signer_journal_binding(cfg,key.public_key);
        store=std::make_unique<SwapSubmissionStore>(scratch.base/"verified-replay",binding,SwapSubmissionFaultHook{Fault::proceed,&fault});
        require(store->create({binding,1,digest(51),EffectId{0}})==SwapSubmissionCode::none,"reviewed_journal_created");
        reopen();
    }
    std::span<const std::byte> bytes()const{return std::as_bytes(std::span(payload.data(),payload.size()));}
    void reopen(){session=std::make_unique<SwapSignerSession>(link,*store,*key.protected_key,cfg,clock.interface(),reviewed);}
    bool begin(){return session->start(EffectId{1},1,handle,bytes(),10,1100);}
    bool recover_stamp(){return session->retrieve(EffectId{1},1,handle,bytes(),10,1100);}
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
void one_buy_scope(){
    VerifiedRig r;r.voter.state.spot_swap.opening.limits.max_purchase_total=swap::QuoteAtoms{2'000'000};
    require(r.begin(),"broader_budget_request_starts");(void)r.finish();
    require(r.session->status()==SwapSignerStatus::refused && r.key.protected_key->signature_attempts()==0 && r.voter.authority_requests==0,
        "mvp_refuses_broader_runtime_purchase_budget");
    VerifiedRig second;second.voter.state.spot_swap.last_effect=2;
    second.voter.state.spot_swap.pending.intent.effect=EffectId{2};
    require(second.session->start(EffectId{2},1,handle,second.bytes(),10,1100),"second_effect_starts_read");(void)second.finish();
    require(second.session->status()==SwapSignerStatus::refused && second.key.protected_key->signature_attempts()==0,
        "mvp_refuses_second_effect_even_with_unspent_budget");
    VerifiedRig loss;loss.voter.state.spot_swap.opening.limits.max_realized_loss=swap::QuoteAtoms{500'001};
    require(loss.begin(),"broader_loss_request_starts");(void)loss.finish();
    require(loss.session->status()==SwapSignerStatus::refused && loss.key.protected_key->signature_attempts()==0,
        "mvp_refuses_larger_loss_allowance");
    VerifiedRig cost(2'039'280,100,2000,2'039'281,6'900'812);
    cost.voter.state.spot_swap.opening.limits.max_buy_price_per_sol=swap::QuoteAtoms{145'000'000};
    require(cost.begin(),"broader_cost_request_starts");(void)cost.finish();
    require(cost.session->status()==SwapSignerStatus::refused && cost.key.protected_key->signature_attempts()==0,
        "mvp_refuses_larger_repayment_allowance");
}
void conservative_mvp(){
    VerifiedRig r(2'039'280,100,2000,2'039'280,6'900'812);
    require(r.begin() && r.finish()==SwapSignerStatus::signed_once,"conservative_cap_signs_constructed_supported_buy");
    require(r.key.verifies(r.bytes(),r.session->signature_der()),"conservative_signature_covers_exact_payload");
    VerifiedRig floor(2'039'280,100,2000,2'039'280,6'900'813);
    require(floor.begin(),"one_lamport_higher_floor_starts");(void)floor.finish();
    require(floor.session->status()==SwapSignerStatus::refused && floor.key.protected_key->signature_attempts()==0,
        "conservative_cap_does_not_waive_net_minimum");
}
void economic_and_context_refusal(){
    VerifiedRig r(2'039'280);require(r.begin(),"recorded_payment_starts");(void)r.finish();
    require(r.session->status()==SwapSignerStatus::refused && r.key.protected_key->signature_attempts()==0 && r.voter.authority_requests==0 &&
        r.store->latest().phase==SwapSubmissionPhase::idle,"recorded_payment_exceeds_accepted_cap_before_fence");
    VerifiedRig changed;changed.reviewed.lookup.addresses[0][0]^=1;changed.reopen();
    require(changed.session->status()==SwapSignerStatus::closed,"changed_lookup_context_cannot_reuse_journal");
    VerifiedRig revised;++revised.reviewed.policy.expires_at_ms;
    revised.cfg.transaction_policy_digest=swap_signer_verification_digest(revised.reviewed);revised.reopen();
    require(revised.session->status()==SwapSignerStatus::closed,"journal_binds_changed_reviewed_policy");
    VerifiedRig missing;missing.session=std::make_unique<SwapSignerSession>(missing.link,*missing.store,*missing.key.protected_key,missing.cfg,missing.clock.interface());
    require(missing.session->status()==SwapSignerStatus::closed,"missing_reviewed_context_closes_policy_bound_session");
    VerifiedRig org;org.reviewed.organization=*truemarkets::tm_text("different-org");org.reopen();
    require(org.session->status()==SwapSignerStatus::closed,"changed_organization_cannot_reuse_journal");
}
void freshness_and_authority(){
    VerifiedRig r(550'840,20);require(r.begin(),"freshness_fixture_starts");
    while(r.session->status()==SwapSignerStatus::reading_state)(void)r.session->poll();
    require(r.session->status()==SwapSignerStatus::reading_authority,"semantic_verification_precedes_authority");
    r.clock.event=1020;(void)r.finish();
    require(r.session->status()==SwapSignerStatus::refused && r.key.protected_key->signature_attempts()==0,
        "expired_verified_proof_cannot_sign_after_authority");
    VerifiedRig p(550'840,100,1020);require(p.begin(),"policy_expiry_fixture_starts");
    while(p.session->status()==SwapSignerStatus::reading_state)(void)p.session->poll();
    p.clock.event=1020;(void)p.finish();
    require(p.session->status()==SwapSignerStatus::refused && p.key.protected_key->signature_attempts()==0,"policy_expiry_blocks_signing_before_intent_expiry");
    VerifiedRig a;a.voter.authority_bad_mac=true;require(a.begin(),"bad_authority_starts");(void)a.finish();a.clock.tick=1020;(void)a.session->poll();
    require(a.session->status()==SwapSignerStatus::refused && a.key.protected_key->signature_attempts()==0,"verified_payload_still_needs_authenticated_authority");
    VerifiedRig recovered(550'840,20);require(recovered.begin() && recovered.finish()==SwapSignerStatus::signed_once,"expiry_recovery_fixture_signs");
    recovered.clock.event=1020;require(!recovered.recover_stamp() && recovered.session->stamp().empty(),"expired_payload_cannot_recover_stamp");
}
}
int main(int argc,char**argv){
    const std::pair<const char*,void(*)()> cases[]{{"verified_signature_and_recovery",verified_signature_and_recovery},{"conservative_mvp",conservative_mvp},{"one_buy_scope",one_buy_scope},
        {"economic_and_context_refusal",economic_and_context_refusal},{"freshness_and_authority",freshness_and_authority}};
    unsigned ran{},failed{};for(const auto&[name,fn]:cases){if(argc==2 && std::string_view(argv[1])!=name)continue;++ran;
        try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return ran&&!failed?0:1;
}
