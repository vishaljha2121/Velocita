#include "v2_swap_signer_test_support.hpp"
#include "v2_swap_transaction_verifier_test_support.hpp"

namespace {
using namespace swap_signer_test;
struct VerifiedRig : Rig {
    velocita::v2::test_support::RouteFixture tx;
    SwapSignerVerificationContext reviewed;
    SwapSignerConfig cfg{config()};
    std::string payload;
    explicit VerifiedRig(std::uint64_t payment=2'039'280,std::uint64_t age=100,std::uint64_t cap=2'039'280,std::uint64_t minimum=6'819'647,unsigned pool=0)
        :tx(pool){
        tx.payment=payment;tx.policy.accepted_costs.max_output_deduction=swap::BaseAtoms{cap};tx.pending.intent.min_base_receive=swap::BaseAtoms{minimum};
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
        state.reserved_quote=swap::QuoteAtoms{1'000'000};state.event_time=1000;clock.event=1001;
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
void one_buy_scope(){
    VerifiedRig r;r.voter.state.spot_swap.opening.limits.max_purchase_total=swap::QuoteAtoms{2'000'000};
    require(r.begin(),"broader_budget_request_starts");(void)r.finish();
    require(r.refused_unsigned(),"mvp_refuses_broader_runtime_purchase_budget");
    VerifiedRig second;second.voter.state.spot_swap.last_effect=2;
    second.voter.state.spot_swap.pending.intent.effect=EffectId{2};
    require(second.session->start(EffectId{2},1,handle,second.bytes(),10,1100,second.tx.prefix()),"second_effect_starts_read");(void)second.finish();
    require(second.session->status()==SwapSignerStatus::refused && second.key.protected_key->signature_attempts()==0,
        "mvp_refuses_second_effect_even_with_unspent_budget");
    VerifiedRig released;auto& after=released.voter.state.spot_swap;after.last_effect=2;after.pending.intent.effect=EffectId{2};
    after.last_release=swap::ReleaseUnsent{EffectId{1},digest(60)};
    require(released.session->start(EffectId{2},1,handle,released.bytes(),10,1100,released.tx.prefix()) &&
        released.finish()==SwapSignerStatus::signed_once,"mvp_signs_next_effect_after_the_previous_was_released_unsent");
    VerifiedRig skipped;auto& gap=skipped.voter.state.spot_swap;gap.last_effect=3;gap.pending.intent.effect=EffectId{3};
    gap.last_release=swap::ReleaseUnsent{EffectId{1},digest(60)};
    require(skipped.session->start(EffectId{3},1,handle,skipped.bytes(),10,1100,skipped.tx.prefix()),"gap_effect_starts_read");(void)skipped.finish();
    require(skipped.session->status()==SwapSignerStatus::refused && skipped.key.protected_key->signature_attempts()==0,
        "mvp_refuses_effect_that_skips_an_unreleased_predecessor");
    VerifiedRig loss;loss.voter.state.spot_swap.opening.limits.max_realized_loss=swap::QuoteAtoms{500'001};
    require(loss.begin(),"broader_loss_request_starts");(void)loss.finish();
    require(loss.refused_unsigned(),"mvp_refuses_larger_loss_allowance");
    VerifiedRig cost(2'039'280,100,2'039'281);
    require(cost.begin(),"broader_cost_request_starts");(void)cost.finish();
    require(cost.refused_unsigned(),"mvp_refuses_larger_repayment_allowance");
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
    const std::pair<const char*,void(*)()> cases[]{{"verified_signature_and_recovery",verified_signature_and_recovery},{"conservative_mvp",conservative_mvp},{"one_buy_scope",one_buy_scope},
        {"economic_and_context_refusal",economic_and_context_refusal},{"freshness_and_authority",freshness_and_authority},
        {"second_pool_same_journal",second_pool_same_journal},{"lookup_evidence",lookup_evidence}};
    unsigned ran{},failed{};for(const auto&[name,fn]:cases){if(argc==2 && std::string_view(argv[1])!=name)continue;++ran;
        try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return ran&&!failed?0:1;
}
