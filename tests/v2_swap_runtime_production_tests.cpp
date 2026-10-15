#include <cstring>
#include "velocita/v2/swap_runtime_production.hpp"
#include "v2_swap_signer_test_support.hpp"
#include <sstream>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include "velocita/v2/swap_account_review.hpp"
namespace {
using namespace swap_signer_test;
std::string hex(std::span<const std::byte> bytes){constexpr char alphabet[]="0123456789abcdef";std::string s;for(auto v:bytes){const auto n=std::to_integer<unsigned>(v);s+=alphabet[n>>4];s+=alphabet[n&15];}return s;}
std::string profile(const std::array<std::byte,33>&key){
    const auto d=hex(digest(1).bytes);std::ostringstream s;
    s<<"schema=1\nincarnation="<<d<<"\ncatalog="<<d<<"\ncustody="<<d<<"\nauthority_domain="<<d
     <<"\naccount=2\ninstrument=2\npolicy=901\npolicy_version=1\nrisk_policy_version=1\naccount_assets="<<d
     <<"\nsession_evidence="<<d<<"\nowner_wallet="<<d<<"\nsigner_public_key="<<hex(key)
     <<"\naccount_review_digest="<<d<<"\nfailure_fee_payer="<<hex(digest(2).bytes)<<"\nsocket=/var/run/velocita/socket\ngateway_uid="<<::getuid()<<"\nsigner_uid="<<::getuid()+1
     <<"\ngateway_caller=22\nsigner_caller=23\nipc_timeout_ms=1000\nbearer_digest="<<d
     <<"\nexpires_at_ms=1000\nsigner_instance="<<d<<"\nkey_version=1\nstate_timeout_ms=1000\norganization=fixture-org\nsigner_id=fixture-signer\nuser_id=12345678-1234-1234-1234-123456789abc\n";
    return s.str();
}
void profile_tests(){Scratch scratch;TestKey key(scratch.base/"key");const auto source=profile(key.public_key);
    const auto p=decode_swap_runtime_profile(source);require(p.has_value(),"valid runtime profile decodes");
    const auto costs=decode_swap_runtime_profile(source+"max_buy_output_deduction_lamports=550840\n");
    require(costs && costs->gateway.binding.max_buy_output_deduction_lamports==550840 &&
        costs->signer.binding.max_buy_output_deduction_lamports==550840,"both_processes_receive_explicit_buy_cost_cap");
    require(swap_runtime_identity_digest(costs->gateway.binding)!=swap_runtime_identity_digest(p->gateway.binding),"cost_profile_requires_new_account_review_identity");
    require(!decode_swap_runtime_profile(source+"max_buy_output_deduction_lamports=18446744073709551615\n"),"oversized_cost_profile_refused");
    const auto transaction=decode_swap_runtime_profile(source+"transaction_review_digest="+hex(digest(71).bytes)+"\n");
    require(transaction && transaction->gateway.binding.transaction_review_digest==digest(71) &&
        transaction->signer.binding.transaction_review_digest==digest(71),"transaction_review_pin_reaches_both_processes");
    require(swap_runtime_identity_digest(transaction->gateway.binding)!=swap_runtime_identity_digest(p->gateway.binding),
        "transaction_review_changes_runtime_account_identity");
    require(!decode_swap_runtime_profile(source+"transaction_review_digest="+std::string(64,'0')+"\n"),"explicit_zero_transaction_review_refused");
    require(p->gateway.binding.allocation.account==AccountId{2} && p->gateway.binding.signer_public_key==key.public_key &&
        p->signer.binding.gateway_caller==CallerId{22} && p->organization.view()=="fixture-org","profile preserves exact typed pins");
    require(!decode_swap_runtime_profile(source+"schema=1\n"),"duplicate profile field refuses");
    require(!decode_swap_runtime_profile(source+"typo=1\n"),"unknown profile field refuses");
    for(auto field:{"organization", "custody", "owner_wallet", "bearer_digest", "signer_public_key", "session_evidence", "user_id", "failure_fee_payer", "account_review_digest"}){auto missing=source;const auto start=missing.find(std::string(field)+"=");missing.erase(start,missing.find('\n',start)-start+1);require(!decode_swap_runtime_profile(missing),"missing profile pin refuses");}
    for(auto [from,to]:{std::pair{"expires_at_ms=1000","expires_at_ms=0"},{"schema=1","schema=2"},{"ipc_timeout_ms=1000","ipc_timeout_ms=4294967297"},{"gateway_caller=22","gateway_caller=23"},{"socket=/var/run/velocita/socket","socket=/var/../socket"},{"account=2","account=+2"}}){auto changed=source;changed.replace(changed.find(from),std::strlen(from),to);require(!decode_swap_runtime_profile(changed),"invalid numeric identity or path refuses");}
    require(!decode_swap_runtime_profile(std::string(8193,'a')),"oversized profile refuses");
    auto same=source;auto at=same.find("failure_fee_payer=")+std::strlen("failure_fee_payer=");same.replace(at,64,hex(digest(1).bytes));
    require(!decode_swap_runtime_profile(same),"owner cannot be configured as external failure sponsor");
    same.replace(at,64,std::string(64,'0'));require(!decode_swap_runtime_profile(same),"missing sponsor identity refuses");
    require(p->failure_policy.expected_external_fee_payer[0]==std::to_integer<std::uint8_t>(digest(2).bytes[0]),"profile preserves independent failure sponsor pin");
    require(!swap_runtime_live_contract_admitted(*p,10),"legacy_profile_cannot_admit_live_runtime");
    auto mvp=*transaction;mvp.gateway.binding.max_buy_output_deduction_lamports=2'039'280;
    mvp.signer.binding=mvp.gateway.binding;
    require(swap_runtime_live_contract_admitted(mvp,10),"reviewed_bounded_mvp_profile_admitted");
    auto invalid=mvp;++invalid.gateway.binding.max_buy_output_deduction_lamports;invalid.signer.binding=invalid.gateway.binding;
    require(!swap_runtime_live_contract_admitted(invalid,10),"larger_repayment_profile_refused");
    invalid=mvp;invalid.signer.binding.transaction_review_digest=digest(72);
    require(!swap_runtime_live_contract_admitted(invalid,10),"different_process_policy_refused");
    require(!swap_runtime_live_contract_admitted(mvp,1000),"expired_bearer_profile_refused");
    require(!swap_runtime_live_contract_admitted(SwapRuntimeProfile{},10),"empty_profile_refused");
}
void account_review_tests(){
    Scratch scratch;TestKey key(scratch.base/"review-key");
    auto p=*decode_swap_runtime_profile(profile(key.public_key));
    auto opening=swap_test::opening();
    p.gateway.binding.allocation=opening.binding;
    swap::State state;
    require(swap::apply(state,opening,swap::Role::operator_role,100)==swap::Code::applied,"review fixture initialized");
    auto document=[&](std::string activity=""){
        return "schema=1\nruntime_identity="+hex(swap_runtime_identity_digest(p.gateway.binding).bytes)+
            "\nopening_digest="+hex(swap_opening_review_digest(opening).bytes)+"\nhistory_evidence="+hex(digest(8).bytes)+
            "\nuser_id="+std::string(p.user_id.view())+"\nreviewed_at_ms=1000\nexpires_at_ms=2000\noutside_base=30\noutside_quote=50\nactivity_count="+
            (activity.empty()?"0\n":"2\n")+activity;
    };
    const auto activity="activity_0_evidence="+hex(digest(9).bytes)+"\nactivity_0_base_delta=7\nactivity_0_quote_delta=-20\nactivity_1_evidence="+
        hex(digest(10).bytes)+"\nactivity_1_base_delta=-2\nactivity_1_quote_delta=3\n";
    const auto source=document(activity);
    auto decode=[](const std::string& s){return decode_swap_account_review(s,sha256_digest(std::as_bytes(std::span(s.data(),s.size()))));};
    auto r=decode(source);require(r.has_value(),"attributed review decodes");
    const auto outside=reviewed_outside_balance(*r);
    require(outside && outside->base_atoms==35 && outside->quote_atoms==33,"independent outside arithmetic oracle 30+7-2 and 50-20+3");
    require(swap_account_review_matches(*r,p.gateway.binding,p.user_id.view(),state),"review binds actual opening and account");
    require(!decode_swap_account_review(source,digest(99)),"unreviewed document digest refuses");
    require(!decode(source+"extra=1\n") && !decode(source+"schema=1\n"),"unknown and duplicate review fields refuse");
    for(auto [from,to]:{std::pair{"activity_count=2","activity_count=17"},{"expires_at_ms=2000","expires_at_ms=86401001"},
        {"outside_base=30","outside_base=-1"},{"activity_0_quote_delta=-20","activity_0_quote_delta=-51"},
        {"activity_0_base_delta=7","activity_0_base_delta=9223372036854775807"},
        {"activity_0_base_delta=7","activity_0_base_delta=-9223372036854775808"}}){
        auto bad=source;bad.replace(bad.find(from),std::strlen(from),to);require(!decode(bad),"invalid attributed arithmetic or bounds refuses");
    }
    auto day=source;day.replace(day.find("expires_at_ms=2000"),18,"expires_at_ms=86401000");require(decode(day).has_value(),"review of exactly one day decodes");
    auto duplicate=source;auto at=duplicate.find("activity_1_evidence=")+std::strlen("activity_1_evidence=");duplicate.replace(at,64,hex(digest(9).bytes));
    require(!decode(duplicate),"duplicate activity evidence refuses");
    require(swap_account_review_current(*r,1000) && swap_account_review_current(*r,1999) &&
        !swap_account_review_current(*r,999) && !swap_account_review_current(*r,2000),"review expiry exclusive oracle");
    auto changed=state;changed.opening.funding_evidence=digest(55);
    require(!swap_account_review_matches(*r,p.gateway.binding,p.user_id.view(),changed),"changed funding evidence refuses");
    require(!swap_account_review_matches(*r,p.gateway.binding,"ffffffff-1234-1234-1234-123456789abc",state),"wrong account user refuses");
    auto binding=p.gateway.binding;binding.owner_wallet[0]^=std::byte{2};
    require(!swap_account_review_matches(*r,binding,p.user_id.view(),state),"wrong owner refuses");
    auto row=[](const char* symbol,std::uint64_t value,unsigned decimals){auto n=std::to_string(value);if(n.size()<=decimals)n.insert(0,decimals+1-n.size(),'0');n.insert(n.size()-decimals,".");
        return "{\"symbol\":\""+std::string(symbol)+"\",\"chain\":\"solana\",\"venue\":\"defi\",\"total\":\""+n+"\",\"available\":\""+n+"\",\"held\":\"0\"}";};
    const auto wallet="{\"data\":["+row("SOL",state.base.value()+35,9)+","+row("USDC",state.quote.value()+33,6)+"]}";
    CommittedBalanceReadBarrier barrier{opening.binding.incarnation,r->document_digest,digest(11),7,1100,1101};
    require(swap_reviewed_balance_matches(*r,p.gateway.binding,state,barrier,wallet,1102,1103),"fresh wallet matches managed plus attributed outside");
    const auto before=state;
    const auto unexplained="{\"data\":["+row("SOL",state.base.value()+36,9)+","+row("USDC",state.quote.value()+33,6)+"]}";
    require(!swap_reviewed_balance_matches(*r,p.gateway.binding,state,barrier,unexplained,1102,1103) && state==before,"unexplained atom refuses without funding mutation");
    require(!swap_reviewed_balance_matches(*r,p.gateway.binding,state,barrier,wallet,1102,2000),"expired read refuses");
    barrier.applied_index=0;require(!swap_reviewed_balance_matches(*r,p.gateway.binding,state,barrier,wallet,1102,1103),"missing authenticated barrier refuses");
}

void account_history_tests(){
    Scratch scratch;TestKey key(scratch.base/"history-key");
    auto p=*decode_swap_runtime_profile(profile(key.public_key));auto opening=swap_test::opening();p.gateway.binding.allocation=opening.binding;
    swap::State state;require(swap::apply(state,opening,swap::Role::operator_role,100)==swap::Code::applied,"history initialized");
    SwapAccountReview review;review.document_digest=digest(77);review.runtime_identity=swap_runtime_identity_digest(p.gateway.binding);
    review.opening_digest=swap_opening_review_digest(opening);review.user_id=p.user_id;review.reviewed_at_ms=1100;review.expires_at_ms=1100+kSwapReviewLifetimeMs;
    review.outside_base=35;review.outside_quote=33;
    const auto source="{\"schema\":1,\"scope\":\"sol-usdc-current-accounts\",\"owner_sha256\":\""+hex(sha256_digest(p.gateway.binding.owner_wallet).bytes)+
        "\",\"user_id_sha256\":\""+hex(sha256_digest(std::as_bytes(std::span(p.user_id.view()))).bytes)+
        "\",\"setup_sha256\":\""+hex(digest(2).bytes)+"\",\"balances_sha256\":\""+hex(digest(3).bytes)+
        "\",\"evidence_manifest_sha256\":\""+hex(digest(4).bytes)+
        "\",\"capture_started_ms\":1000,\"capture_finished_ms\":1050,\"start_slot\":7,\"end_slot\":8,\"base_atoms\":"+
        std::to_string(state.base.value()+35)+",\"quote_atoms\":"+std::to_string(state.quote.value()+33)+
        ",\"stable_window\":true,\"pagination_exhausted\":true,\"blocking_orders\":0,\"unsupported_accounts\":0}";
    auto decode=[](const std::string& body){return decode_swap_account_history_cut(body,sha256_digest(std::as_bytes(std::span(body))));};
    auto cut=decode(source);require(cut.has_value(),"eligible observed history cut decodes");review.history_evidence=cut->document;
    require(swap_account_history_funding(*cut,review,p.gateway.binding,state,1200),"exact history cut funds managed and outside partition");
    require(!decode_swap_account_history_cut(source,digest(99)),"unreviewed history bytes refused");
    for(auto [from,to]:{std::pair{"\"stable_window\":true","\"stable_window\":false"},
        {"\"pagination_exhausted\":true","\"pagination_exhausted\":false"},
        {"\"blocking_orders\":0","\"blocking_orders\":1"},{"\"unsupported_accounts\":0","\"unsupported_accounts\":1"},
        {"\"capture_finished_ms\":1050","\"capture_finished_ms\":999"},{"\"end_slot\":8","\"end_slot\":6"},
        {"\"schema\":1","\"schema\":2"},{"\"base_atoms\":35","\"base_atoms\":9223372036854775808"}}){
        auto bad=source;auto at=bad.find(from);require(at!=std::string::npos,"history mutation target exists");bad.replace(at,std::strlen(from),to);
        require(!decode(bad),"unsafe or incomplete history cut refused");
    }
    auto altered=*cut;altered.owner=digest(55);require(!swap_account_history_identity(altered,review,p.gateway.binding),"history for another wallet refused");
    auto long_capture=source;long_capture.replace(long_capture.find("\"capture_finished_ms\":1050"),26,"\"capture_finished_ms\":301001");
    require(!decode(long_capture),"capture over five minutes refused");
    altered=*cut;altered.user=digest(55);require(!swap_account_history_identity(altered,review,p.gateway.binding),"history for another user refused");
    altered=*cut;altered.document=digest(55);require(!swap_account_history_identity(altered,review,p.gateway.binding),"different history evidence refused");
    altered=*cut;++altered.quote_atoms;require(!swap_account_history_funding(altered,review,p.gateway.binding,state,1200),"one unassigned history atom refused");
    require(swap_account_history_funding(*cut,review,p.gateway.binding,state,1050+kSwapReviewLifetimeMs),"history funds for a full day");
    auto later=review;later.reviewed_at_ms=2000;later.expires_at_ms=2000+kSwapReviewLifetimeMs;
    require(!swap_account_history_funding(*cut,later,p.gateway.binding,state,1051+kSwapReviewLifetimeMs),"new review cannot revive day-old history");
    require(swap_account_review_current(review,1099+kSwapReviewLifetimeMs) && !swap_account_review_current(review,1100+kSwapReviewLifetimeMs),
        "review lasts exactly one day");
    auto longer=review;longer.expires_at_ms=1101+kSwapReviewLifetimeMs;require(!swap_account_review_current(longer,1200),"review longer than a day refused");
    require(!swap_account_history_funding(*cut,review,p.gateway.binding,state,1049),"future history cut refused");
    auto edge=review;edge.reviewed_at_ms=1050+kSwapReviewLifetimeMs;edge.expires_at_ms=edge.reviewed_at_ms+1;
    require(swap_account_history_identity(*cut,edge,p.gateway.binding),"review exactly one day after the history cut");
    ++edge.reviewed_at_ms;require(!swap_account_history_identity(*cut,edge,p.gateway.binding),"review more than one day after the history cut refused");
    SwapAccountStartupAdmission admission{review,*cut,swap::State{},state,{},false};
    require(!swap_admission_funded(admission,p.gateway.binding,1200),"funding refuses an absent current authenticated state");
    admission.state=state;
    require(swap_admission_funded(admission,p.gateway.binding,1200),"matching original current and funding state admitted");
    admission.state=state;admission.funding_state=swap::State{};
    require(!swap_admission_funded(admission,p.gateway.binding,1200),"missing opening in funding snapshot cannot fall back");
    auto bought=state;
    require(swap::apply(bought,swap_test::buy(opening),swap::Role::strategy,1150)==swap::Code::applied,"restart buy reserves");
    require(swap::apply(bought,swap::Prepare{EffectId{1},digest(21),digest(22),1},swap::Role::gateway,1151)==swap::Code::applied,"restart buy prepares");
    require(swap::apply(bought,swap::Attempt{EffectId{1},digest(21)},swap::Role::gateway,1152)==swap::Code::applied,"restart buy attempts");
    const swap::Settlement fill{EffectId{1},opening.binding.custody,digest(21),digest(23),digest(24),swap::Outcome::succeeded,
        swap::BaseAtoms{0},swap::BaseAtoms{9'000'000},swap::QuoteAtoms{1'002'000},swap::QuoteAtoms{0},swap::QuoteAtoms{2'000},true};
    require(swap::apply(bought,fill,swap::Role::gateway,1153)==swap::Code::applied && swap::valid_state(bought),"restart buy finalized");
    require(bought.base.value()==9'000'000 && bought.quote.value()==3'998'000,"restart accounting oracle 5000000 minus 1002000");
    SwapAccountStartupAdmission restarted{review,*cut,bought,bought,
        {opening.binding.incarnation,review.document_digest,digest(31),20,1170,1171},false};
    require(!swap_account_history_funding(*cut,review,p.gateway.binding,bought,1200),"old funding cut is not the post-trade wallet");
    require(swap_admission_funded(restarted,p.gateway.binding,1200),"restart after settled buy retains original reviewed funding");
    const auto unchanged=bought;
    const std::string wallet=R"({"data":[{"symbol":"SOL","chain":"solana","venue":"defi","total":"0.009000035","available":"0.009000035","held":"0"},{"symbol":"USDC","chain":"solana","venue":"defi","total":"3.998033","available":"3.998033","held":"0"}]})";
    require(swap_reviewed_balance_matches(review,p.gateway.binding,bought,restarted.barrier,wallet,1172,1173),
        "restart wallet equals current managed balances plus unchanged outside35lamports33USDCatoms");
    auto unexplained=wallet;auto at=unexplained.find("0.009000035");unexplained.replace(at,11,"0.009000036");
    at=unexplained.find("0.009000035");unexplained.replace(at,11,"0.009000036");
    require(!swap_reviewed_balance_matches(review,p.gateway.binding,bought,restarted.barrier,unexplained,1172,1173),
        "restart funding fallback never absorbs an unexplained current wallet atom");
    require(!swap_reviewed_balance_matches(review,p.gateway.binding,state,restarted.barrier,wallet,1172,1173),
        "opening balances must not replace current balances for wallet comparison");
    require(bought==unchanged && restarted.state==bought && restarted.funding_state==bought,"funding fallback does not mutate current or funding snapshots");
    auto bad=restarted;bad.state.opening.funding_evidence=digest(56);bad.funding_state.opening=bad.state.opening;
    require(!swap_admission_funded(bad,p.gateway.binding,1200),"fallback cannot accept a different pinned opening");
    bad=restarted;++bad.history.quote_atoms;
    require(!swap_admission_funded(bad,p.gateway.binding,1200),"fallback cannot accept one unmatched historical quote atom");
    require(!swap_admission_funded(restarted,p.gateway.binding,review.expires_at_ms),"fallback cannot extend the review lifetime");
    auto renewed_source=source;
    auto replace=[&](std::string_view from,std::string_view to){const auto offset=renewed_source.find(from);
        require(offset!=std::string::npos,"renewed history fixture field exists");renewed_source.replace(offset,from.size(),to);};
    replace("\"capture_started_ms\":1000","\"capture_started_ms\":1160");
    replace("\"capture_finished_ms\":1050","\"capture_finished_ms\":1170");
    replace("\"base_atoms\":35","\"base_atoms\":9000035");
    replace("\"quote_atoms\":5000033","\"quote_atoms\":3998033");
    const auto renewed_cut=decode(renewed_source);require(renewed_cut.has_value(),"renewed current history decodes");
    auto renewed=restarted;renewed.history=*renewed_cut;renewed.review.history_evidence=renewed_cut->document;
    renewed.review.document_digest=digest(78);renewed.review.reviewed_at_ms=1180;renewed.review.expires_at_ms=1180+kSwapReviewLifetimeMs;
    require(swap_admission_funded(renewed,p.gateway.binding,1200),"new reviewed history matching current balances remains supported");
    renewed.funding_state=state;
    require(!swap_admission_funded(renewed,p.gateway.binding,1200),"new current cut cannot be paired with stale original funding balances");
}

}
void unsent_funding_tests(){
    auto opening=swap_test::opening();swap::State idle;require(swap::apply(idle,opening,swap::Role::operator_role,100)==swap::Code::applied,"unsent fixture initialized");
    idle.mode=swap::Mode::normal;
    require(swap_unsent_funding_state(idle)==idle,"state without pending effect is its own funding state");
    auto reserved=idle;require(swap::apply(reserved,swap_test::buy(opening),swap::Role::strategy,100)==swap::Code::applied,"unsent fixture reserved");
    const auto funded=swap_unsent_funding_state(reserved);
    require(funded && funded->pending==swap::Pending{} && !funded->reserved_quote.value() && !funded->reserved_base.value() &&
        funded->base==reserved.base && funded->quote==reserved.quote && funded->last_effect==1 && swap::valid_state(*funded),
        "reserved but unsent effect funds exactly the pre-intent balances");
    auto prepared=reserved;require(swap::apply(prepared,swap::Prepare{EffectId{1},digest(7),digest(8),1},swap::Role::gateway,101)==swap::Code::applied,"unsent fixture prepared");
    require(!swap_unsent_funding_state(prepared),"prepared effect may be on chain and has no unsent funding state");
    SwapAccountStartupAdmission startup;startup.state=idle;startup.barrier={digest(1),digest(2),digest(3),40,1000,1010};
    SwapPrepareRead read{{1,1,3,41,digest(4),digest(5)},reserved,2000,2010};
    startup.funding_state=idle;
    const auto fresh=swap_prepare_admission(startup,read);
    require(fresh && fresh->funding_state==idle && fresh->state==*funded && fresh->barrier.incarnation==digest(1) && fresh->barrier.clock_epoch==digest(2) &&
        fresh->barrier.read_evidence_digest==digest(5) && fresh->barrier.applied_index==41 && fresh->barrier.request_started_ms==2000 &&
        fresh->barrier.received_at_ms==2010,"create admission uses the fresh committed read as its balance barrier");
    for(unsigned mode=0;mode<5;++mode){auto bad=read;
        if(mode==0)bad.reading.applied_index=39;
        if(mode==1)bad.reading.state_digest={};
        if(mode==2)bad.received_ms=1999;
        if(mode==3)bad.state=prepared;
        if(mode==4)bad.requested_ms=0;
        require(!swap_prepare_admission(startup,bad),"older, unproven, inverted or non-unsent read refuses create admission");}
}
void create_trace_tests(){
    transport::SwapHttpsResult r;r.status=transport::SwapHttpsStatus::complete;r.exposure=transport::SwapHttpsExposure::response_received;r.http_status=409;
    const std::string error=R"({"error":"order already open"})";r.body_size=error.size();std::copy(error.begin(),error.end(),r.body.begin());
    auto t=swap_create_trace(r);
    require(t.stage==SwapCreateStage::exchanged && t.http_status==409 && t.body_size==error.size() &&
        t.body_digest==sha256_digest(std::as_bytes(std::span(error))) && t.error_view()==error,"refused create keeps status, digest and its error body");
    require(render_swap_create_trace(t)=="velocita_swap_create_stage 6\nvelocita_swap_create_transport 0\nvelocita_swap_create_exposure 2\n"
        "velocita_swap_create_http_status 409\nvelocita_swap_create_body_bytes "+std::to_string(error.size())+"\n","create trace renders as bounded metrics");
    r.http_status=201;t=swap_create_trace(r);require(t.error_view().empty() && t.body_size==error.size(),"successful create body is never retained");
    const std::string large(5000,'x');r.http_status=500;r.body_size=large.size();std::copy(large.begin(),large.end(),r.body.begin());
    t=swap_create_trace(r);require(t.error_view()==std::string(1024,'x') && t.body_size==5000,"error body is bounded to 1 KiB");
    require(render_swap_create_trace(SwapCreateTrace{}).starts_with("velocita_swap_create_stage 0\n"),"uncalled create renders stage zero");
}
void startup_recovery_tests(){
    auto opening=swap_test::opening();swap::State idle;require(swap::apply(idle,opening,swap::Role::operator_role,100)==swap::Code::applied,"startup fixture initialized");
    idle.mode=swap::Mode::normal;auto reserved=idle;require(swap::apply(reserved,swap_test::buy(opening),swap::Role::strategy,100)==swap::Code::applied,"startup fixture reserved");
    auto prepared=reserved;require(swap::apply(prepared,swap::Prepare{EffectId{1},digest(7),digest(8),1},swap::Role::gateway,101)==swap::Code::applied,"startup fixture prepared");
    using P=SwapPreparePhase;using S=SwapSubmissionPhase;
    auto corrupt=idle;corrupt.settlement_count=corrupt.settlements.size()+1;
    require(swap_startup_recovery_only(corrupt,P::prepared,EffectId{1},S::resolved),"out-of-bounds settlement count never traversed during startup");
    require(!swap_startup_recovery_only(idle,P::idle,EffectId{0},S::idle) && !swap_startup_recovery_only(idle,P::released,EffectId{1},S::idle),
        "idle or released journal with no pending effect runs normally");
    require(!swap_startup_recovery_only(reserved,P::idle,EffectId{0},S::idle) && !swap_startup_recovery_only(reserved,P::create_intent,EffectId{1},S::idle) &&
        !swap_startup_recovery_only(reserved,P::released,EffectId{1},S::idle),"reserved effect with no signature path runs normally");
    require(swap_startup_recovery_only(reserved,P::create_intent,EffectId{2},S::idle) && swap_startup_recovery_only(idle,P::create_intent,EffectId{1},S::idle) &&
        swap_startup_recovery_only(reserved,P::prepared,EffectId{1},S::idle) && swap_startup_recovery_only(prepared,P::prepared,EffectId{1},S::idle) &&
        swap_startup_recovery_only(reserved,P::idle,EffectId{0},S::execute_intent),"foreign create, prepared bytes or sent request recover only");
    auto settled=prepared;
    require(swap::apply(settled,swap::Attempt{EffectId{1},digest(7)},swap::Role::gateway,102)==swap::Code::applied,"startup previous effect attempts");
    const swap::Settlement fill{EffectId{1},opening.binding.custody,digest(7),digest(9),digest(10),swap::Outcome::succeeded,
        swap::BaseAtoms{0},swap::BaseAtoms{9'000'000},swap::QuoteAtoms{1'002'000},swap::QuoteAtoms{0},swap::QuoteAtoms{2'000},true};
    require(swap::apply(settled,fill,swap::Role::gateway,103)==swap::Code::applied,"startup previous effect finalized");
    require(!swap_startup_recovery_only(settled,P::prepared,EffectId{1},S::resolved),"settled old submission restarts normally while idle");
    require(!swap_startup_recovery_only(settled,P::prepared,EffectId{1},S::execute_intent),"settled old unresolved journal can finish durable retirement");
    auto next=settled;
    require(swap::apply(next,swap_test::buy(opening,2),swap::Role::strategy,104)==swap::Code::applied,"startup next effect reserves");
    require(!swap_startup_recovery_only(next,P::prepared,EffectId{1},S::resolved),"settled old preparation permits a new reserved effect");
    require(swap_startup_recovery_only(next,P::prepared,EffectId{2},S::resolved),"current prepared bytes without settlement stay recovery only");
    require(swap::apply(next,swap::Prepare{EffectId{2},digest(11),digest(12),1},swap::Role::gateway,105)==swap::Code::applied,"startup current effect prepares");
    require(swap_startup_recovery_only(next,P::prepared,EffectId{2},S::resolved),"current prepared Raft effect requires supervised recovery after restart");
    require(swap::apply(next,swap::Attempt{EffectId{2},digest(11)},swap::Role::gateway,106)==swap::Code::applied,"startup current effect attempts");
    require(swap_startup_recovery_only(next,P::prepared,EffectId{2},S::execute_intent),"current attempted effect cannot restart with new trade authority");
    auto unproven=settled;unproven.settlements[0].finalized=false;
    require(swap_startup_recovery_only(unproven,P::prepared,EffectId{1},S::resolved),"nonfinalized predecessor cannot admit normal startup");
    unproven=settled;unproven.settlements[0].evidence={};
    require(swap_startup_recovery_only(unproven,P::prepared,EffectId{1},S::resolved),"missing predecessor evidence cannot admit normal startup");
    unproven=settled;unproven.settlements[0].custody=digest(99);
    require(swap_startup_recovery_only(unproven,P::prepared,EffectId{1},S::resolved),"foreign predecessor custody cannot admit normal startup");
}
std::string b64url(std::span<const std::uint8_t> bytes){
    constexpr char a[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";std::string out;std::uint32_t acc{};int bits{};
    for(auto b:bytes){acc=(acc<<8)|b;bits+=8;while(bits>=6){bits-=6;out+=a[(acc>>bits)&63];}}
    if(bits)out+=a[(acc<<(6-bits))&63];return out;
}
struct TestApiKey {
    EVP_PKEY* key{EVP_PKEY_Q_keygen(nullptr,nullptr,"EC","P-256")};
    std::array<std::uint8_t,32> d{},x{},y{};
    TestApiKey(){
        require(key!=nullptr,"test api key generated");BIGNUM* priv{};
        require(EVP_PKEY_get_bn_param(key,OSSL_PKEY_PARAM_PRIV_KEY,&priv)==1 && BN_bn2binpad(priv,d.data(),32)==32,"test private scalar");BN_free(priv);
        std::array<std::uint8_t,65> pub{};std::size_t n{};
        require(EVP_PKEY_get_octet_string_param(key,OSSL_PKEY_PARAM_PUB_KEY,pub.data(),pub.size(),&n)==1 && n==65 && pub[0]==4,"test public point");
        std::copy_n(pub.begin()+1,32,x.begin());std::copy_n(pub.begin()+33,32,y.begin());
    }
    ~TestApiKey(){EVP_PKEY_free(key);}
    std::optional<std::array<std::uint8_t,64>> raw(std::string_view message)const{
        EVP_MD_CTX* ctx=EVP_MD_CTX_new();std::array<unsigned char,80> der{};std::size_t size=der.size();
        const bool ok=EVP_DigestSignInit(ctx,nullptr,EVP_sha256(),nullptr,key)==1 &&
            EVP_DigestSign(ctx,der.data(),&size,reinterpret_cast<const unsigned char*>(message.data()),message.size())==1;
        EVP_MD_CTX_free(ctx);if(!ok)return {};
        const unsigned char* cursor=der.data();ECDSA_SIG* sig=d2i_ECDSA_SIG(nullptr,&cursor,static_cast<long>(size));std::array<std::uint8_t,64> out{};
        BN_bn2binpad(ECDSA_SIG_get0_r(sig),out.data(),32);BN_bn2binpad(ECDSA_SIG_get0_s(sig),out.data()+32,32);ECDSA_SIG_free(sig);return out;
    }
    bool verifies(std::string_view message,std::string_view raw_signature)const{
        std::array<std::uint8_t,64> raw{};std::size_t at{};std::uint32_t acc{};int bits{};
        constexpr std::string_view a="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        for(char c:raw_signature){const auto v=a.find(c);if(v==std::string_view::npos || at>=64)return false;acc=(acc<<6)|static_cast<std::uint32_t>(v);bits+=6;
            if(bits>=8){bits-=8;raw[at++]=static_cast<std::uint8_t>(acc>>bits);}}
        if(at!=64)return false;
        ECDSA_SIG* sig=ECDSA_SIG_new();ECDSA_SIG_set0(sig,BN_bin2bn(raw.data(),32,nullptr),BN_bin2bn(raw.data()+32,32,nullptr));
        unsigned char* der{};const int len=i2d_ECDSA_SIG(sig,&der);ECDSA_SIG_free(sig);
        EVP_MD_CTX* ctx=EVP_MD_CTX_new();
        const bool ok=EVP_DigestVerifyInit(ctx,nullptr,EVP_sha256(),nullptr,key)==1 &&
            EVP_DigestVerify(ctx,der,static_cast<std::size_t>(len),reinterpret_cast<const unsigned char*>(message.data()),message.size())==1;
        EVP_MD_CTX_free(ctx);OPENSSL_free(der);return ok;
    }
};
struct FakeSigner final:SwapTokenSigner {
    const TestApiKey& key;std::vector<std::string> messages;bool refuse{};
    explicit FakeSigner(const TestApiKey& k):key(k){}
    std::optional<std::array<std::uint8_t,64>> sign(std::string_view message)override{messages.emplace_back(message);if(refuse)return {};return key.raw(message);}
};
struct FakeTokenPort final:SwapTokenPort {
    std::vector<std::string> bodies;unsigned status{200};std::string reply;transport::SwapHttpsStatus outcome{transport::SwapHttpsStatus::complete};
    transport::SwapHttpsResult post(std::string_view body)override{
        bodies.emplace_back(body);transport::SwapHttpsResult r;r.status=outcome;
        r.exposure=transport::SwapHttpsExposure::response_received;r.http_status=status;r.body_size=reply.size();
        std::copy(reply.begin(),reply.end(),r.body.begin());return r;
    }
};
std::string token_reply(std::string_view token,std::string_view expiry){
    return "{\"access_token\":\""+std::string(token)+"\",\"refresh_token\":\"r.r.r\",\"expires_in\":\""+std::string(expiry)+"\",\"token_type\":\"Bearer\"}";
}
void api_key_tests(){
    constexpr std::string_view id="0b9e3a52-6f2c-4d6e-9a1b-2c3d4e5f6a7b";TestApiKey key;FakeSigner signer(key);
    const auto body=swap_api_key_request(id,1791993300,signer);
    require(body.has_value() && signer.messages==std::vector<std::string>{"0b9e3a52-6f2c-4d6e-9a1b-2c3d4e5f6a7b.1791993300"},"signer asked for id.seconds only");
    using namespace truemarkets;
    const auto key_id=json_member_string(*body,"key_id");const auto stamp=json_member_raw(*body,"timestamp");const auto signature=json_member_string(*body,"signature");
    require(validate_json_object(*body)==JsonError::none && key_id && *key_id==id && stamp && *stamp=="1791993300" && signature &&
        key.verifies("0b9e3a52-6f2c-4d6e-9a1b-2c3d4e5f6a7b.1791993300",*signature),"request carries key id, seconds and an ES256 signature over id.seconds");
    require(!key.verifies("0b9e3a52-6f2c-4d6e-9a1b-2c3d4e5f6a7b.1791993301",*signature),"signature binds the timestamp");
    require(!swap_api_key_request("not a uuid",1791993300,signer) && !swap_api_key_request("0b9e3a52-6f2c-4d6e-9a1b-2c3d4e5f6a7",1791993300,signer) &&
        !swap_api_key_request("0b9e3a52x6f2c-4d6e-9a1b-2c3d4e5f6a7b",1791993300,signer) && !swap_api_key_request("0b9e3a52-6f2c-4d6e-9a1b-2c3d4e5f6a7g",1791993300,signer) &&
        signer.messages.size()==1,"malformed key id refused before signing");
    require(swap_api_key_id_valid("0B9E3A52-6F2C-4D6E-9A1B-2C3D4E5F6A7B") && !swap_api_key_id_valid("0b9e3a52-6f2c-4d6e-9a1b-2c3d4e5f6a7b0"),"key id shape");
    signer.refuse=true;require(!swap_api_key_request(id,1791993300,signer),"signer refusal builds no request");signer.refuse=false;
    const auto minted=parse_swap_minted_token(200,token_reply("aa.bb.cc","2026-10-14T15:55:24.456143667Z"));
    require(minted && minted->token=="aa.bb.cc" && minted->expires_ms==1791993324456,"token and fractional ISO expiry");
    require(parse_swap_minted_token(200,token_reply("aa.bb.cc","2026-10-14T15:55:24Z"))->expires_ms==1791993324000 &&
        parse_swap_minted_token(200,token_reply("aa.bb.cc","2000-02-29T00:00:00.5Z"))->expires_ms==951782400500,"whole-second and leap-day expiry");
    require(!parse_swap_minted_token(401,token_reply("aa.bb.cc","2026-10-14T15:55:24Z")) && !parse_swap_minted_token(200,token_reply("aa bb","2026-10-14T15:55:24Z")) &&
        !parse_swap_minted_token(200,token_reply("aa.bb.cc","2026-10-14 15:55:24Z")) && !parse_swap_minted_token(200,token_reply("aa.bb.cc","2026-02-30T00:00:00Z")),
        "refused status, unsafe token or malformed expiry");
    for(auto token:{"aa.bb","aa.bb.cc.dd","aa..cc",".bb.cc","aa.bb.","aa.b+b.cc"})require(!parse_swap_minted_token(200,token_reply(token,"2026-10-14T15:55:24Z")),"three nonempty base64url parts");
    for(auto expiry:{"2026-10-14T24:00:00Z","2026-10-14T15:60:00Z","2026-10-14T15:55:60Z","2026-13-14T15:55:24Z","2026-00-14T15:55:24Z","2026-10-00T15:55:24Z",
        "2026-10-32T15:55:24Z","2025-02-29T00:00:00Z","1969-12-31T23:59:59Z","2026-10-14T15:55:24.Z","2026-10-14T15:55:24.1234567891Z","2026-10-14T15:55:24",
        "2026-10-14T15:55:24.5","2026/10/14T15:55:24Z","2026-10-14X15:55:24Z","2100-02-29T00:00:00Z","2024-01-32T00:00:00Z","2026-10-14T15-55:24Z","2026-10-14T15:55-24Z","2026-1a-14T15:55:24Z","2026-10-14T15:55:24+00:00"})
        require(!parse_swap_minted_token(200,token_reply("aa.bb.cc",expiry)),"strict UTC ISO-8601 expiry");
    require(parse_swap_minted_token(200,token_reply("aa.bb.cc","2024-02-29T23:59:59.999999999Z"))->expires_ms==1709251199999 &&
        parse_swap_minted_token(200,token_reply("aa.bb.cc","1970-01-01T00:00:00.05Z"))->expires_ms==50,"leap year end of day and epoch fractions");
    const std::string longest="a."+std::string(4092,'b')+".c";
    require(parse_swap_minted_token(200,token_reply(longest,"2026-10-14T15:55:24Z"))->token==longest && !parse_swap_minted_token(200,token_reply(longest+"c","2026-10-14T15:55:24Z")),
        "token fits the 4096-byte cache exactly");
    auto basic=token_reply("aa.bb.cc","2026-10-14T15:55:24Z");basic.replace(basic.find("Bearer"),6,"Basic ");require(!parse_swap_minted_token(200,basic),"bearer tokens only");
    FakeTokenPort port;port.reply=token_reply("t1.t1.t1","2026-10-14T16:00:00Z");
    SwapBearerCache cache(id,signer,port,1791996000000);
    require(cache.bearer(1791993300000)=="t1.t1.t1" && port.bodies.size()==1 && json_member_raw(port.bodies[0],"timestamp")=="1791993300","first use mints once at the current second");
    require(cache.bearer(1791993479999)=="t1.t1.t1" && port.bodies.size()==1,"cached until two minutes before expiry");
    port.reply=token_reply("t2.t2.t2","2026-10-14T17:00:00Z");
    require(cache.bearer(1791993480000)=="t2.t2.t2" && port.bodies.size()==2,"re-minted inside the last two minutes");
    require(cache.bearer(1791996000000).empty() && port.bodies.size()==2,"never past the reviewed window");
    FakeTokenPort refused;refused.status=500;SwapBearerCache failing(id,signer,refused,1791996600000);
    require(failing.bearer(1791993300000).empty() && failing.bearer(1791993300001).empty() && refused.bodies.size()==2,"failed mint returns no bearer");
    FakeTokenPort unused;unused.reply=token_reply("u.u.u","2026-10-14T17:00:00Z");signer.refuse=true;SwapBearerCache unsigned_cache(id,signer,unused,1791996600000);
    require(unsigned_cache.bearer(1791993300000).empty() && unused.bodies.empty(),"signer refusal posts nothing");signer.refuse=false;
    FakeTokenPort clockless;clockless.reply=token_reply("z.z.z","2026-10-14T17:00:00Z");SwapBearerCache zero(id,signer,clockless,1791996600000);
    require(zero.bearer(0).empty() && clockless.bodies.empty(),"zero clock mints nothing");
    FakeTokenPort broken;broken.reply=token_reply("b.b.b","2026-10-14T17:00:00Z");broken.outcome=transport::SwapHttpsStatus::transport_failure;
    SwapBearerCache failed_transport(id,signer,broken,1791996600000);require(failed_transport.bearer(1791993300000).empty() && broken.bodies.size()==1,"incomplete exchange gives no bearer");
    FakeTokenPort edge;edge.reply=token_reply("e.e.e","2026-10-14T15:57:00Z");SwapBearerCache boundary(id,signer,edge,1791996600000);
    require(boundary.bearer(1791993300000).empty(),"token with exactly two minutes left is refused");
    edge.reply=token_reply("e.e.e","2026-10-14T15:57:00.001Z");require(boundary.bearer(1791993300000)=="e.e.e","token with just over two minutes is used");
    FakeTokenPort short_lived;short_lived.reply=token_reply("s.s.s","2026-10-14T15:56:00Z");SwapBearerCache brief(id,signer,short_lived,1791996600000);
    require(brief.bearer(1791993300000).empty(),"token expiring within two minutes is refused");
}
int main(){try{profile_tests();account_review_tests();account_history_tests();unsent_funding_tests();create_trace_tests();startup_recovery_tests();api_key_tests();std::cout<<"PASS production profile and admission guards\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
