#include "v2_swap_signer_test_support.hpp"
namespace {
using namespace swap_signer_test;
void lost_response_retrieval(){
    Rig r;require(r.start(),"lost response fixture starts");require(r.finish()==SwapSignerStatus::signed_once,"lost response fixture signs");
    const auto expected=std::string(r.session->stamp().view());const auto attempts=r.key.protected_key->signature_attempts();
    r.session.reset();r.store.reset();
    r.store=std::make_unique<SwapSubmissionStore>(r.scratch.base/"signer-replay",r.binding);
    require(r.store->recover()==SwapSubmissionCode::none,"signed result recovers durably");
    r.session=std::make_unique<SwapSignerSession>(r.link,*r.store,*r.key.protected_key,config(),r.clock.interface());
    require(r.session->retrieve(EffectId{1},1,handle,body_bytes(),10,20100),"lost stamp is recovered without another signature");
    require(r.session->stamp().view()==expected && r.key.protected_key->signature_attempts()==attempts,"recovered stamp bytes exactly equal original response");
    for(unsigned n=0;n<6;++n){
        r.session=std::make_unique<SwapSignerSession>(r.link,*r.store,*r.key.protected_key,config(),r.clock.interface());
        auto payload=body;if(n==0)payload[0]='X';
        if(n==4)r.clock.event=20100;
        if(n==5)r.clock.event=100;
        require(!r.session->retrieve(EffectId{1},n==1?2:1,handle,std::as_bytes(std::span(payload.data(),payload.size())),n==2?11:10,n==3?20101:20100),
            "changed payload sequence index expiry or expired result cannot be retrieved");
        require(r.session->stamp().empty() && r.key.protected_key->signature_attempts()==attempts,"retrieval refusal has no stamp or crypto side effect");
    }
}
void authority_fences(){
    for(unsigned n=0;n<3;++n){Rig r;
        if(n==0)r.voter.authority_silent=true;if(n==1)r.voter.authority_bad_mac=true;if(n==2)r.voter.authority_delta=0;
        require(r.start(),"authority refusal starts");(void)r.finish();r.clock.tick=1020;(void)r.session->poll();
        require(r.session->status()==SwapSignerStatus::refused && r.key.protected_key->signature_attempts()==0 && r.store->latest().phase==SwapSubmissionPhase::idle,
            "missing unauthenticated or nonfresh authority cannot sign");
    }
}
void durable_result_and_verification(){
    Rig r;r.fault.writes=0;r.fault.fail_result=true;require(r.start(),"result persistence failure starts");
    require(r.finish()==SwapSignerStatus::storage_failed && r.session->stamp().empty() && r.key.protected_key->signature_attempts()==1,
        "stamp is never published before signature result is durable");
    Rig s;s.fault.advance=true;require(s.start(),"unverified durable result fixture starts");require(s.finish()==SwapSignerStatus::fenced,"durable fence before synthetic signature injection");
    s.fault.advance=false;SwapSubmissionTransaction wrong{};wrong[31]=std::byte{1};wrong[63]=std::byte{2};
    require(s.store->observe_transaction(s.store->latest().identity,wrong)==SwapSubmissionCode::none,"synthetic invalid signature is persisted for verification test");
    s.session=std::make_unique<SwapSignerSession>(s.link,*s.store,*s.key.protected_key,config(),s.clock.interface());
    require(!s.session->retrieve(EffectId{1},1,handle,body_bytes(),10,20100) && s.session->stamp().empty(),"retrieved raw signature must independently verify original payload");
}
void exact_signature(){
    Rig r;require(r.start(),"eligible synthetic request starts");
    require(r.finish()==SwapSignerStatus::signed_once,"authenticated synthetic prepared request signs once");
    require(!r.session->stamp().empty() && r.key.verifies(body_bytes(),r.session->signature_der()),"independent OpenSSL verifies exact original bytes");
    auto changed=body;changed.back()=' ';
    require(!r.key.verifies(std::as_bytes(std::span(changed.data(),changed.size())),r.session->signature_der()),"altered newline invalidates signature");
    require(!r.start(),"same session cannot sign twice");
    require(r.store->latest().phase==SwapSubmissionPhase::execute_intent,"signing fence durable before result");
    require(r.voter.authority_requests==1 && r.store->latest().committed_index==11,"fresh committed authority precedes durable signing fence");
}
void restart_replay(){
    Rig r;require(r.start(),"restart fixture starts");require(r.finish()==SwapSignerStatus::signed_once,"restart fixture signs");
    r.session.reset();r.store.reset();
    r.store=std::make_unique<SwapSubmissionStore>(r.scratch.base/"signer-replay",r.binding);
    require(r.store->recover()==SwapSubmissionCode::none,"signing fence recovers");
    r.session=std::make_unique<SwapSignerSession>(r.link,*r.store,*r.key.protected_key,config(),r.clock.interface());
    require(r.session->status()==SwapSignerStatus::fenced && !r.start() && r.session->stamp().empty(),"restart never repeats possibly issued signature");
}
void state_fences(){
    for(unsigned n=0;n<6;++n){Rig r;
        if(n==0)r.voter.domain=digest(98);
        if(n==1)r.voter.index=9;
        if(n==2)r.voter.bad_mac=true;
        if(n==3)r.voter.bad_snapshot=true;
        if(n==4)r.voter.state.spot_swap.pending.payload=digest(99);
        if(n==5)r.voter.state.spot_swap.pending.handle=digest(99);
        require(r.start(),"state fence starts");(void)r.finish();r.clock.tick=1020;(void)r.session->poll();
        require(r.session->status()==SwapSignerStatus::refused && r.session->stamp().empty() && r.store->latest().phase==SwapSubmissionPhase::idle,
            "bad domain index authentication or pending digest refuses without crypto fence");}
}
void clock_and_storage(){
    {
        Rig r;auto c=config();c.account_review_expires_at_ms=r.clock.event+1;
        r.session=std::make_unique<SwapSignerSession>(r.link,*r.store,*r.key.protected_key,c,r.clock.interface());
        require(r.start(),"review expiry fixture starts");++r.clock.event;(void)r.finish();
        require(r.session->status()==SwapSignerStatus::refused && r.key.protected_key->signature_attempts()==0,
            "review expires during authority read before any signature");
    }
    {
        Rig r;auto c=config();c.account_review_expires_at_ms=r.clock.event;
        r.session=std::make_unique<SwapSignerSession>(r.link,*r.store,*r.key.protected_key,c,r.clock.interface());
        require(!r.start() && r.key.protected_key->signature_attempts()==0,"expired review rejects signing start");
    }
    Rig r;r.fault.armed=true;require(r.start(),"disk fixture starts");
    require(r.finish()==SwapSignerStatus::storage_failed && r.session->signature_der().empty() && r.key.protected_key->signature_attempts()==0,"fsync failure never returns signature");
    Rig s;s.fault.advance=true;require(s.start(),"slow fsync fixture starts");
    require(s.finish()==SwapSignerStatus::fenced && s.session->signature_der().empty() && s.key.protected_key->signature_attempts()==0 &&
        s.store->latest().phase==SwapSubmissionPhase::execute_intent,"deadline rechecked after durable fence before signing");
    Rig t;require(t.start(),"expiry fixture starts");t.clock.event=20100;(void)t.finish();
    require(t.session->status()==SwapSignerStatus::refused && t.store->latest().phase==SwapSubmissionPhase::idle,"expired intent cannot sign");
    Rig u;require(u.start(),"rollback fixture starts");u.clock.tick=999;(void)u.session->poll();
    require(u.session->status()==SwapSignerStatus::refused,"backward monotonic time refuses");
}
void crash_recovery(){
    for(auto stage:{SwapSubmissionStage::temporary_sync,SwapSubmissionStage::before_publish}){
        Rig r;r.session.reset();r.store.reset();const auto child=::fork();require(child>=0,"fork fixture");
        if(child==0){
            r.fault.crash=true;r.fault.crash_stage=stage;
            r.store=std::make_unique<SwapSubmissionStore>(r.scratch.base/"signer-replay",r.binding,SwapSubmissionFaultHook{Fault::proceed,&r.fault});
            if(r.store->recover()!=SwapSubmissionCode::none)::_exit(74);
            r.session=std::make_unique<SwapSignerSession>(r.link,*r.store,*r.key.protected_key,config(),r.clock.interface());
            if(!r.start())::_exit(75);(void)r.finish();::_exit(76);
        }
        int status{};require(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==73,"process stops inside durable signing fence");
        r.store=std::make_unique<SwapSubmissionStore>(r.scratch.base/"signer-replay",r.binding);
        const auto result=r.store->recover();
        if(stage==SwapSubmissionStage::temporary_sync)require(result==SwapSubmissionCode::temporary_present,"partial signer write quarantines restart");
        else {
            require(result==SwapSubmissionCode::none,"renamed signer fence recovers");
            r.session=std::make_unique<SwapSignerSession>(r.link,*r.store,*r.key.protected_key,config(),r.clock.interface());
            require(r.session->status()==SwapSignerStatus::fenced && !r.start(),"crash after durable fence never signs on restart");
        }
    }
}
void remaining_admission_fences(){
    for(unsigned n=0;n<5;++n){Rig r;
        if(n==0)r.voter.state.spot_swap.pending.phase=swap::Phase::attempted;
        if(n==1)r.clock.event=99;
        if(n==2){ApplyScratch work;require(apply(r.voter.state,{{CallerId{9},RequestSequence{90},digest(77)},
            SetStrategyMode{StrategyMode::halted,KillEpoch{1},ExecutorGeneration{1}}},work,
            &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"halt fixture");}
        if(n==3){auto c=config();c.binding.custody=digest(88);r.session=std::make_unique<SwapSignerSession>(r.link,*r.store,*r.key.protected_key,c,r.clock.interface());require(r.session->status()==SwapSignerStatus::closed,"changed allocation closes signer before state read");continue;}
        const auto effect=n==4?EffectId{2}:EffectId{1};
        require(r.session->start(effect,1,handle,body_bytes(),10,20100),"remaining fence starts");(void)r.finish();
        require(r.session->status()==SwapSignerStatus::refused && r.key.protected_key->signature_attempts()==0,
            "wrong phase time halt allocation or effect refuses signing");
    }
    Rig r;require(r.start(),"resolved fixture starts");require(r.finish()==SwapSignerStatus::signed_once,"resolved fixture signs");
    r.session.reset();const auto record=r.store->latest();
    require(r.store->resolve({record.identity,12,digest(99)})==SwapSubmissionCode::none,"synthetic external journal resolution");
    r.session=std::make_unique<SwapSignerSession>(r.link,*r.store,*r.key.protected_key,config(),r.clock.interface());
    require(r.session->status()==SwapSignerStatus::fenced,"one-use signer cannot be rearmed through generic resolved state");
}
void production_gate(){
    Rig r;require(r.start(),"production gate starts authenticated read");(void)r.finish();
    require(r.session->status()==SwapSignerStatus::refused && r.key.protected_key->signature_attempts()==0 &&
        r.store->latest().phase==SwapSubmissionPhase::idle,"nonfixture build cannot sign even authenticated synthetic transaction");
    Rig inclusive;constexpr std::string_view text="VELOCITA SYNTHETIC FEE INCLUSIVE SWAP\n";
    const auto bytes=std::as_bytes(std::span(text.data(),text.size()));
    auto& state=inclusive.voter.state.spot_swap;state.pending.payload=sha256_digest(bytes);
    state.pending.intent.quote_input=swap::QuoteAtoms{998'000};state.pending.intent.max_quote_debit=swap::QuoteAtoms{1'000'000};
    state.reserved_quote=swap::QuoteAtoms{1'000'000};
    require(inclusive.session->start(EffectId{1},1,handle,bytes,10,20100),"inclusive production gate starts");
    (void)inclusive.finish();
    require(inclusive.session->status()==SwapSignerStatus::refused && inclusive.key.protected_key->signature_attempts()==0 &&
        inclusive.store->latest().phase==SwapSubmissionPhase::idle,"fee_inclusive_mapping_does_not_enable_production_signing");
}
void key_and_domain(){
    Rig r;auto changed=r.key.public_key;changed.back()^=std::byte{1};
    auto reviewed=config();reviewed.transaction_policy_digest=digest(71);
    require(swap_signer_journal_binding(reviewed,r.key.public_key)!=r.binding,
        "verification_policy_changes_journal_domain");
    const auto first_policy=swap_signer_journal_binding(reviewed,r.key.public_key);
    reviewed.transaction_policy_digest=digest(72);
    require(swap_signer_journal_binding(reviewed,r.key.public_key)!=first_policy,
        "changed_verification_policy_cannot_retrieve_prior_signature");
    require(!SwapSignerKey::load_inherited_fd(r.key.fd,changed),"wrong pinned public key refuses");
    require(::fchmod(r.key.fd,0644)==0,"insecure fixture mode");
    require(!SwapSignerKey::load_inherited_fd(r.key.fd,r.key.public_key),"publicly readable private key refuses");
    auto c=config();c.key_version=2;SwapSignerSession other(r.link,*r.store,*r.key.protected_key,c,r.clock.interface());
    require(other.status()==SwapSignerStatus::closed,"key version cannot reuse old signing journal");
    c=config();c.caller=CallerId{9};SwapSignerSession caller(r.link,*r.store,*r.key.protected_key,c,r.clock.interface());
    require(caller.status()==SwapSignerStatus::closed,"principal cannot reuse old signing journal");
    c=config();c.signer_instance=digest(55);SwapSignerSession instance(r.link,*r.store,*r.key.protected_key,c,r.clock.interface());
    require(instance.status()==SwapSignerStatus::closed,"signer instance cannot reuse old signing journal");
    c=config();c.binding.custody=digest(66);SwapSignerSession allocation(r.link,*r.store,*r.key.protected_key,c,r.clock.interface());
    require(allocation.status()==SwapSignerStatus::closed,"changed allocation cannot retrieve old signer result");
}
void payload_and_economics(){
    Rig r;auto mutable_body=body;
    require(r.session->start(EffectId{1},1,handle,std::as_bytes(std::span(mutable_body.data(),mutable_body.size())),10,20100),"owned payload starts");
    mutable_body[0]='X';require(r.finish()==SwapSignerStatus::signed_once && r.key.verifies(body_bytes(),r.session->signature_der()),"asynchronous signing owns exact payload bytes");
    Rig s;s.voter.state.spot_swap.pending.intent.min_base_receive=swap::BaseAtoms{9'000'001};
    require(s.start(),"economic mismatch fixture starts");(void)s.finish();
    require(s.session->status()==SwapSignerStatus::refused,"synthetic admission pins exact economics not caller boolean");
    Rig t;auto different=body;different[0]='X';auto bytes=std::as_bytes(std::span(different.data(),different.size()));
    t.voter.state.spot_swap.pending.payload=sha256_digest(bytes);
    require(t.session->start(EffectId{1},1,handle,bytes,10,20100),"nonfixture authenticated payload starts");(void)t.finish();
    require(t.session->status()==SwapSignerStatus::refused,"test build cannot admit arbitrary authenticated payload");
    for(bool mismatched:{false,true}){
        Rig inclusive;constexpr std::string_view text="VELOCITA SYNTHETIC FEE INCLUSIVE SWAP\n";
        const auto bytes=std::as_bytes(std::span(text.data(),text.size()));
        auto& pending=inclusive.voter.state.spot_swap.pending;pending.payload=sha256_digest(bytes);
        if(!mismatched){pending.intent.quote_input=swap::QuoteAtoms{998'000};pending.intent.max_quote_debit=swap::QuoteAtoms{1'000'000};
            inclusive.voter.state.spot_swap.reserved_quote=swap::QuoteAtoms{1'000'000};}
        require(inclusive.session->start(EffectId{1},1,handle,bytes,10,20100),"inclusive signer fixture starts");
        (void)inclusive.finish();
        if(mismatched){require(inclusive.session->status()==SwapSignerStatus::refused && inclusive.key.protected_key->signature_attempts()==0,
            "inclusive_fixture_cannot_sign_legacy_principal");continue;}
        require(inclusive.session->status()==SwapSignerStatus::signed_once && inclusive.key.verifies(bytes,inclusive.session->signature_der()),
            "inclusive_fixture_signature_covers_exact_bytes");
        require(inclusive.session->retrieve(EffectId{1},1,handle,bytes,10,20100) && inclusive.key.protected_key->signature_attempts()==1,
            "inclusive_fixture_retrieval_never_signs_again");
    }
}
}
int main(int argc,char**argv){
    const std::pair<const char*,void(*)()> cases[]{{"lost_response_retrieval",lost_response_retrieval},{"authority_fences",authority_fences},{"durable_result",durable_result_and_verification},{"exact_signature",exact_signature},{"restart_replay",restart_replay},
        {"state_fences",state_fences},{"clock_and_storage",clock_and_storage},{"remaining_admission_fences",remaining_admission_fences},{"production_gate",production_gate},{"crash_recovery",crash_recovery},{"key_and_domain",key_and_domain},{"payload_and_economics",payload_and_economics}};
    unsigned ran{},failed{};for(const auto&[name,fn]:cases){if(argc==1 && std::string_view(name)=="production_gate")continue;if(argc==2 && std::string_view(argv[1])!=name)continue;++ran;
        try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return ran&&!failed?0:1;
}
