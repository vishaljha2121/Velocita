#include <cstring>
#include "velocita/v2/swap_runtime_production.hpp"
#include "v2_swap_signer_test_support.hpp"
#include <sstream>
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
    require(!swap_runtime_live_contract_admitted(),"production wiring never implies transaction admission");
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
    for(auto [from,to]:{std::pair{"activity_count=2","activity_count=17"},{"expires_at_ms=2000","expires_at_ms=901001"},
        {"outside_base=30","outside_base=-1"},{"activity_0_quote_delta=-20","activity_0_quote_delta=-51"},
        {"activity_0_base_delta=7","activity_0_base_delta=9223372036854775807"},
        {"activity_0_base_delta=7","activity_0_base_delta=-9223372036854775808"}}){
        auto bad=source;bad.replace(bad.find(from),std::strlen(from),to);require(!decode(bad),"invalid attributed arithmetic or bounds refuses");
    }
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
    review.opening_digest=swap_opening_review_digest(opening);review.user_id=p.user_id;review.reviewed_at_ms=1100;review.expires_at_ms=901000;
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
    auto later=review;later.reviewed_at_ms=2000;later.expires_at_ms=902000;
    require(!swap_account_history_funding(*cut,later,p.gateway.binding,state,901051),"new review cannot revive stale history");
    require(!swap_account_history_funding(*cut,review,p.gateway.binding,state,1049),"future history cut refused");
}

}
int main(){try{profile_tests();account_review_tests();account_history_tests();std::cout<<"PASS production profile and admission guards\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
