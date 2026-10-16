#include "velocita/v2/swap_settlement_admission.hpp"
#include "velocita/v2/truemarkets_wire.hpp"
#include "swap_settlement_test_fixture.hpp"
#include "velocita/v2/swap_runtime_https.hpp"
#include "velocita/v2/swap_settlement_fence.hpp"
#include <map>
#include <deque>
using namespace settlement_lifecycle_fixture;
namespace {
Digest256 intent_hash(const swap::Intent& intent){std::array<std::byte,swap::max_record_bytes> bytes{};const auto encoded=swap::encode({swap::Role::strategy,intent.source_time,intent},bytes);require(encoded.error==swap::CodecError::none,"intent encoding");return sha256_digest(std::span(bytes).first(encoded.size));}
void failed(LifecycleFixture& f){f.rebuild();replace(f.json,"\"err\":null","\"err\":{\"InstructionError\":[1,{\"Custom\":6001}]}");
    const auto result=truemarkets::json_member_raw(f.json,"result");const auto meta=truemarkets::json_member_raw(*result,"meta");
    const auto post=std::string(*truemarkets::json_member_raw(*meta,"postBalances"));
    replace(f.json,post,"[29962000,10000000,1488440,1488440,1488440,0,20000000,1,1,1,1,1,1,1,1]");
    const auto r2=truemarkets::json_member_raw(f.json,"result");const auto m2=truemarkets::json_member_raw(*r2,"meta");
    const auto tokens=std::string(*truemarkets::json_member_raw(*m2,"postTokenBalances"));
    replace(f.json,tokens,"["+f.tb(2,2,1006989)+","+f.tb(3,9,1000000)+","+f.tb(4,9,0)+","+f.tb(6,9,18511560,true)+"]");
}
FailedSwapAccounting check(LifecycleFixture& f,SwapFailureAccountingPolicy policy){f.port.body(f.json);SwapFinalizedReader reader(f.port,f.clock,digest(1),20);const auto read=reader.read(f.signature,7,100);require(read.evidence.has_value(),"failed receipt attributed");return account_failed_swap(*read.evidence,f.envelope,"org",address(2),{},key(2),policy);}
void fee_only_rollback(){LifecycleFixture f;failed(f);const auto r=check(f,{key(1)});require(r.code==FailedSwapAccountingCode::observed_external_fee_only && r.external_fee_lamports==38000,"failed_sponsor_fee_exact_owner_rollback");}
void sponsor_policy(){LifecycleFixture f;failed(f);for(auto key_value:{truemarkets::SolanaAccountKey{},key(9),key(2)})require(check(f,{key_value}).code!=FailedSwapAccountingCode::observed_external_fee_only,"unconfigured_wrong_or_owner_sponsor_refused");}
void native_rollback(){for(unsigned mode=0;mode<4;++mode){LifecycleFixture f;failed(f);if(mode==0)replace(f.json,"29962000,10000000","29962001,9999999");if(mode==1)replace(f.json,"29962000,10000000,1488440","29962001,10000000,1488439");if(mode==2)replace(f.json,"\"fee\":38000","\"fee\":37999");if(mode==3)replace(f.json,"29962000,10000000,1488440,1488440","29962000,9999999,1488440,1488441");require(check(f,{key(1)}).code!=FailedSwapAccountingCode::observed_external_fee_only,"any_nonpayer_delta_or_wrong_fee_refused");}}
void token_rollback(){for(unsigned mode=0;mode<3;++mode){LifecycleFixture f;failed(f);auto at=f.json.find("\"postTokenBalances\"");std::string post=f.json.substr(at);f.json.resize(at);
    if(mode==0){replace(post,f.tb(2,2,1006989),f.tb(2,2,1006988));replace(post,f.tb(3,9,1000000),f.tb(3,9,1000001));}
    if(mode==1)replace(post,f.tb(2,2,1006989),f.tb(2,9,1006989));
    if(mode==2)replace(post,f.tb(4,9,0)+",","");f.json+=post;require(check(f,{key(1)}).code!=FailedSwapAccountingCode::observed_external_fee_only,"failed_token_amount_owner_or_presence_change_refused");}}
void owner_payer(){LifecycleFixture f;std::swap(f.keys[0],f.keys[1]);failed(f);require(check(f,{key(2)}).code!=FailedSwapAccountingCode::observed_external_fee_only,"owner_fee_payer_never_zero_charge");}
void account_key_codec(){const auto decoded=decode_solana_account_key(address(2));require(decoded && *decoded==key(2) && b58(*decoded)==address(2),"canonical_key_exact_32_bytes");for(const auto& invalid:{std::string{},std::string(32,'1'),std::string("1")+address(2),std::string("0")+address(2),address(2)+std::string(20,'z')})require(!decode_solana_account_key(invalid),"noncanonical_zero_or_overflow_key_refused");}
void failure_shape(){for(unsigned mode=0;mode<5;++mode){LifecycleFixture f;if(mode==0){std::vector<std::uint8_t> nonce;little(nonce,4,4);f.top[0]={7,{1},nonce};}failed(f);
    if(mode==1)replace(f.json,"{\"InstructionError\":[1,{\"Custom\":6001}]}","null");
    if(mode==2)replace(f.json,"\"InstructionError\":[1,","\"InstructionError\":[99,");
    if(mode==3)replace(f.json,"{\"Custom\":6001}","false");
    if(mode==4)replace(f.json,"\"preTokenBalances\"","\"missingTokenBalances\"");
    require(check(f,{key(1)}).code!=FailedSwapAccountingCode::observed_external_fee_only,"nonce_success_or_malformed_failure_refused");}}
SwapSettlementAdmissionResult admit_failed_record(LifecycleFixture& f,SwapFailureAccountingPolicy policy){
    constexpr unsigned mode=0;constexpr bool sell=false;
    auto root=std::filesystem::path(std::getenv("HOME"))/".cache/velocita-build/failed-settlement-20261013";std::filesystem::create_directories(root);
    auto directory=(root/"admission-XXXXXX").string();require(::mkdtemp(directory.data()),"proof fixture scratch");
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}}cleanup{directory};
    SwapPrepareBinding binding{{digest(1),digest(2),digest(3),AccountId{2},InstrumentId{2},PolicyId{901},PolicyVersion{1},RiskPolicyVersion{1}},digest(4),digest(5)};
    swap::Intent intent;intent.binding=binding.allocation;intent.effect=EffectId{1};intent.side=Side::buy;intent.quote_input=swap::QuoteAtoms{mode==3?999000U:998000U};intent.fee_cap=swap::QuoteAtoms{mode==4?1999U:2000U};intent.max_quote_debit=swap::QuoteAtoms{1000000};intent.min_base_receive=swap::BaseAtoms{mode==5?9410958U:mode==7?8000000U:9000000U};intent.source_time=1000;intent.sign_by=2000;intent.kill_epoch=1;intent.executor_generation=1;
    if(sell){intent.side=Side::sell;intent.quote_input=swap::QuoteAtoms{};intent.max_quote_debit=swap::QuoteAtoms{};intent.min_base_receive=swap::BaseAtoms{};intent.base_input=swap::BaseAtoms{8400000};intent.min_quote_receive=swap::QuoteAtoms{1000000};intent.fee_cap=swap::QuoteAtoms{mode==4?2017U:2018U};}
    const std::uint64_t input=sell?8400000:1000000;
    SwapPrepareStore preparation(std::filesystem::path(directory)/"prepare",binding);SwapSubmissionStore submission(std::filesystem::path(directory)/"submit",{binding.allocation.incarnation,binding.authority_domain});
    require(preparation.create({binding,1,digest(6),EffectId{0}})==SwapPrepareStoreCode::none,"proof preparation bootstrap");
    truemarkets::SwapRestRequest request;require(truemarkets::encode_swap_rest_create({intent.side,input},request)==truemarkets::SwapRestCode::encoded,"proof create request");
    require(preparation.begin({intent.effect,intent_hash(intent),sha256_digest(std::as_bytes(std::span(request.body.data(),request.body_size))),{intent.side,input}},2,digest(8))==SwapPrepareStoreCode::none,"proof canonical intent retained");
    truemarkets::SwapRestPrepared p;p.handle=*truemarkets::tm_text("proof-order");p.payload_size=f.envelope.size();std::copy(f.envelope.begin(),f.envelope.end(),p.payload.begin());p.payload_digest=sha256_digest(std::as_bytes(std::span(f.envelope.data(),f.envelope.size())));p.quoted_input=input;p.quoted_output=sell?1009007:9410957;p.quoted_fee=sell?2018:2000;
    require(preparation.complete(p,digest(9))==SwapPrepareStoreCode::none,"proof original retained");require(submission.create({{binding.allocation.incarnation,binding.authority_domain},1,digest(10),EffectId{0}})==SwapSubmissionCode::none,"proof submission bootstrap");
    SwapSubmissionIdentity identity;identity.effect=intent.effect;identity.payload=p.payload_digest;identity.execution_request=digest(11);identity.handle_size=p.handle.length;std::copy_n(reinterpret_cast<const std::byte*>(p.handle.bytes.data()),p.handle.length,identity.handle.begin());identity.handle_digest=sha256_digest(std::span(identity.handle).first(identity.handle_size));
    require(submission.begin(identity,3)==SwapSubmissionCode::none && submission.observe_transaction(identity,f.signature)==SwapSubmissionCode::none,"proof retained execute and transaction");
    const auto checksum=submission.latest().checksum;f.port.body(f.json);SwapFinalizedReader reader(f.port,f.clock,digest(1),20);
    auto result=admit_retained_swap_settlement(preparation,submission,binding,intent,reader,7,100,"org",address(2),{},key(2),policy);
    require(submission.latest().checksum==checksum && submission.latest().phase==SwapSubmissionPhase::execute_intent,"proof_does_not_resolve_before_kernel_commit");
    if(result.verified){swap::Initialize opening;opening.binding=intent.binding;
        opening.limits={swap::QuoteAtoms{5000000},swap::QuoteAtoms{2000000},swap::QuoteAtoms{10000},swap::QuoteAtoms{500000},swap::BaseAtoms{1000000000},swap::BaseAtoms{3000000},swap::QuoteAtoms{125000000},swap::QuoteAtoms{50000000},10000,20000};
        opening.base=swap::BaseAtoms{sell?8400000U:0U};opening.base_cost=swap::QuoteAtoms{sell?1000000U:0U};opening.quote=swap::QuoteAtoms{25000000};opening.funding_evidence=digest(90);opening.kill_epoch=1;opening.executor_generation=1;
        swap::State state;
        require(swap::apply(state,opening,swap::Role::operator_role,1000)==swap::Code::applied && swap::apply(state,intent,swap::Role::strategy,1000)==swap::Code::applied,"admission_composition_reserves_exact_intent");
        require(swap::apply(state,swap::Prepare{intent.effect,p.payload_digest,identity.handle_digest,1},swap::Role::gateway,1001)==swap::Code::applied && swap::apply(state,swap::Attempt{intent.effect,p.payload_digest},swap::Role::gateway,1002)==swap::Code::applied,"admission_composition_prepares_and_attempts");
        require(swap::apply(state,result.verified->settlement(),swap::Role::gateway,1003)==swap::Code::applied,"admitted_receipt_is_kernel_compatible");
        require(state.base.value()==0 && state.quote.value()==25000000 && state.fees.value()==0 && state.cost.value()==0 && state.realized_pnl==0 && state.purchases.value()==0 && state.reserved_base.value()==0 && state.reserved_quote.value()==0 && state.pending.phase==swap::Phase::none,"failed_proof_releases_only_reservation_without_charge");
    }
    return result;
}
void retained_failed_admission(){LifecycleFixture f;failed(f);auto result=admit_failed_record(f,{key(1)});require(result.verified.has_value(),"retained_failed_zero_owner_charge_admitted");const auto& s=result.verified->settlement();require(s.outcome==swap::Outcome::failed_without_charge && s.finalized && s.transaction==*solana_signature_transaction_digest(f.signature) && s.fee.value()==0 && s.base_debit.value()==0 && s.base_credit.value()==0 && s.quote_debit.value()==0 && s.quote_credit.value()==0,"failed_proof_exact_identity_and_zero_atoms");result=admit_failed_record(f,{});require(!result.verified,"disabled_failure_policy_keeps_exposure");replace(f.json,"29962000,10000000","29962001,9999999");result=admit_failed_record(f,{key(1)});require(!result.verified,"charged_failure_never_releases_exposure");}

struct ScriptPort final:SwapFinalizedRpcPort{
    Digest256 identity{digest(1)};std::map<std::string,std::deque<std::string>> replies;std::vector<std::string> requests;bool fail_transport{};std::size_t switch_after{};
    Digest256 provider_identity()const noexcept override{return identity;}
    SettlementRpcResponse request(std::string_view body,std::uint64_t)override{
        requests.emplace_back(body);require(swap_canonical_rpc_request(body),"every proof request is an allowlisted production template");
        const auto id_at=body.find("\"id\":")+5;const auto id=std::string(body.substr(id_at,body.find(',',id_at)-id_at));
        const auto m_at=body.find("\"method\":\"")+10;const auto method=std::string(body.substr(m_at,body.find('"',m_at)-m_at));
        SettlementRpcResponse r;if(fail_transport)return r;if(switch_after && requests.size()==switch_after)identity=digest(9);auto& q=replies[method];require(!q.empty(),"scripted reply exists");
        auto text=q.front();if(q.size()>1)q.pop_front();
        text="{\"jsonrpc\":\"2.0\",\"id\":"+id+","+text+"}";r.transport_complete=true;r.http_status=200;r.size=text.size();std::copy(text.begin(),text.end(),r.body.begin());return r;
    }
};
std::string sig_text(unsigned n){std::vector<std::uint8_t> b(64,0);b[0]=static_cast<std::uint8_t>(n);b[63]=1;return b58(b);}
std::string entry(unsigned n,std::uint64_t slot,std::uint64_t time){return "{\"signature\":\""+sig_text(n)+"\",\"slot\":"+std::to_string(slot)+",\"err\":null,\"memo\":null,\"blockTime\":"+std::to_string(time)+",\"confirmationStatus\":\"finalized\"}";}
std::string other_tx(const LifecycleFixture& f){std::string j=f.json;const auto r=truemarkets::json_member_raw(j,"result");return "\"result\":"+std::string(*r);}
constexpr std::uint64_t kStamp=1792118791000ULL;
void stamped(LifecycleFixture& f){replace(f.envelope,"\"timestampMs\":\"1000\"","\"timestampMs\":\""+std::to_string(kStamp)+"\"");}
void script(ScriptPort& p,const LifecycleFixture& f,std::string history="",std::string valid="false",std::uint64_t block_time=kStamp/1000+3600){
    p.replies["isBlockhashValid"]={"\"result\":{\"context\":{\"apiVersion\":\"4.3.0\",\"slot\":5000},\"value\":"+valid+"}"};
    p.replies["getBlockTime"]={"\"result\":"+std::to_string(block_time)};
    if(history.empty())history="["+entry(1,6000,kStamp/1000+7200)+","+entry(2,4000,kStamp/1000+3000)+","+entry(3,3000,kStamp/1000+10)+","+entry(4,100,kStamp/1000-600)+"]";
    p.replies["getSignaturesForAddress"]={"\"result\":"+history};
    std::string tx=other_tx(f);const auto at=tx.find("\"transaction\":[\"")+16;tx[at+250]=tx[at+250]=='A'?'B':'A';
    p.replies["getTransaction"]={tx};
}
UnexecutedChainProof prove(ScriptPort& p,const LifecycleFixture& f){return prove_unexecuted_on_chain(p,digest(1),20,7,f.envelope,"org",address(2),key(2));}
void chain_proven(){LifecycleFixture f;stamped(f);ScriptPort p;script(p,f);const auto r=prove(p,f);
    require(r.code==UnexecutedProofCode::proven && !r.evidence.is_zero() && r.slot==5000 && r.checked_transactions==2,"expired_blockhash_and_no_matching_history_proven");
    require(std::none_of(p.requests.begin(),p.requests.end(),[](const std::string& q){return q.find(sig_text(1))!=std::string::npos && q.find("getTransaction")!=std::string::npos;}),"history after the expiry slot is not fetched");
    require(p.requests[0].find("\"commitment\":\"finalized\"")!=std::string::npos && p.requests[2].find("\"minContextSlot\":5000")!=std::string::npos,"finalized evaluation at or after the expiry slot");
    ScriptPort again;script(again,f);require(prove(again,f).evidence==r.evidence,"evidence digest deterministic");}
void chain_refusals(){
    {LifecycleFixture f;stamped(f);ScriptPort p;script(p,f,"","true");require(prove(p,f).code==UnexecutedProofCode::blockhash_still_valid,"valid_blockhash_never_resolves");}
    {LifecycleFixture f;stamped(f);ScriptPort p;script(p,f,"","false",kStamp/1000+59);require(prove(p,f).code==UnexecutedProofCode::not_final_long_enough,"finalized_view_too_close_to_creation_refused");}
    {LifecycleFixture f;stamped(f);ScriptPort p;script(p,f);p.replies["getTransaction"]={other_tx(f)};
        require(prove(p,f).code==UnexecutedProofCode::landed,"matching_landed_message_refused");}
    {LifecycleFixture f;stamped(f);ScriptPort p;std::string page="[";for(unsigned n=0;n<64;++n){if(n)page+=',';page+=entry(10+n,9000,kStamp/1000+9000);}page+="]";
        script(p,f,page);require(prove(p,f).code==UnexecutedProofCode::history_incomplete,"eight_full_pages_without_reaching_creation_refused");}
    {LifecycleFixture f;stamped(f);ScriptPort p;std::string page="[";for(unsigned n=0;n<40;++n){if(n)page+=',';page+=entry(10+n,4000,kStamp/1000+100);}page+="]";
        script(p,f,page);require(prove(p,f).code==UnexecutedProofCode::history_too_large,"too_many_candidates_refused");}
    {LifecycleFixture f;stamped(f);ScriptPort p;script(p,f,"[{\"signature\":\""+sig_text(3)+"\",\"slot\":3000,\"err\":null,\"memo\":null,\"blockTime\":null}]");
        require(prove(p,f).code==UnexecutedProofCode::malformed,"missing_block_time_refused");}
    {LifecycleFixture f;stamped(f);ScriptPort p;script(p,f);p.fail_transport=true;require(prove(p,f).code==UnexecutedProofCode::transport_refused,"transport_failure_refused");}
    {LifecycleFixture f;stamped(f);ScriptPort p;script(p,f);p.identity=digest(9);require(prove(p,f).code==UnexecutedProofCode::refused,"wrong_provider_refused");}
    for(std::size_t at=1;at<=4;++at){LifecycleFixture f;stamped(f);ScriptPort p;script(p,f);p.switch_after=at;
        require(prove(p,f).code==UnexecutedProofCode::transport_refused,"provider_change_during_proof_refused");}
    {LifecycleFixture f;stamped(f);ScriptPort p;script(p,f);p.replies["isBlockhashValid"]={"\"error\":{\"code\":-32016,\"message\":\"Minimum context slot has not been reached\"},\"result\":null"};
        require(prove(p,f).code==UnexecutedProofCode::transport_refused,"rpc_error_refused");}
    {LifecycleFixture f;stamped(f);ScriptPort p;script(p,f);std::string page="[";for(unsigned n=0;n<64;++n){if(n)page+=',';page+=entry(10+n,9000,kStamp/1000+9000);}page+="]";
        p.replies["getSignaturesForAddress"]={"\"result\":"+page,"\"result\":["+entry(5,3000,kStamp/1000+10)+","+entry(6,90,kStamp/1000-3600)+"]"};
        const auto r=prove(p,f);require(r.code==UnexecutedProofCode::proven && r.checked_transactions==1,"second_page_reaches_creation_and_completes");
        require(std::count_if(p.requests.begin(),p.requests.end(),[](const std::string& q){return q.find("\"before\":\"")!=std::string::npos;})==1,"pagination uses the last signature");}
    {LifecycleFixture f;ScriptPort p;script(p,f);replace(f.envelope,"\"timestampMs\":\"1000\"","\"timestampMs\":\"0\"");require(prove(p,f).code==UnexecutedProofCode::envelope_refused,"untimed_envelope_refused");}
}
void rpc_templates(){
    const std::string head="{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"";
    require(swap_canonical_rpc_request(head+"isBlockhashValid\",\"params\":[\""+address(3)+"\",{\"commitment\":\"finalized\"}]}"),"blockhash template");
    require(swap_canonical_rpc_request(head+"getBlockTime\",\"params\":[5000]}"),"block time template");
    require(swap_canonical_rpc_request(head+"getSignaturesForAddress\",\"params\":[\""+address(3)+"\",{\"commitment\":\"finalized\",\"limit\":64,\"minContextSlot\":5000}]}"),"history template");
    require(swap_canonical_rpc_request(head+"getSignaturesForAddress\",\"params\":[\""+address(3)+"\",{\"commitment\":\"finalized\",\"limit\":64,\"minContextSlot\":5000,\"before\":\""+sig_text(4)+"\"}]}"),"history page template");
    for(const auto& bad:{head+"isBlockhashValid\",\"params\":[\""+address(3)+"\",{\"commitment\":\"processed\"}]}",head+"getBlockTime\",\"params\":[05000]}",
        head+"getSignaturesForAddress\",\"params\":[\""+address(3)+"\",{\"commitment\":\"finalized\",\"limit\":1000,\"minContextSlot\":5000}]}",
        head+"getSignaturesForAddress\",\"params\":[\""+address(3)+"\",{\"commitment\":\"finalized\",\"limit\":64}]}",
        head+"sendTransaction\",\"params\":[\"AAAA\"]}",head+"getSignaturesForAddress\",\"params\":[\""+address(3)+"\",{\"commitment\":\"finalized\",\"limit\":64,\"minContextSlot\":5000,\"before\":\"0\"}]}"})
        require(!swap_canonical_rpc_request(bad),"noncanonical, unfinalized, unbounded or sending request refused");
}
struct Retained{std::filesystem::path dir;SwapPrepareBinding binding;swap::Intent intent;std::unique_ptr<SwapPrepareStore> preparation;std::unique_ptr<SwapSubmissionStore> submission;truemarkets::SwapRestPrepared p;SwapSubmissionIdentity identity;
    ~Retained(){std::error_code e;std::filesystem::remove_all(dir,e);}};
std::unique_ptr<Retained> retain(const LifecycleFixture& f,bool with_transaction=false){
    auto r=std::make_unique<Retained>();auto root=std::filesystem::path(std::getenv("HOME"))/".cache/velocita-build/failed-settlement-20261013";std::filesystem::create_directories(root);
    auto directory=(root/"expired-XXXXXX").string();require(::mkdtemp(directory.data()),"expired scratch");r->dir=directory;
    r->binding={{digest(1),digest(2),digest(3),AccountId{2},InstrumentId{2},PolicyId{901},PolicyVersion{1},RiskPolicyVersion{1}},digest(4),digest(5)};
    auto& intent=r->intent;intent.binding=r->binding.allocation;intent.effect=EffectId{1};intent.side=Side::buy;intent.quote_input=swap::QuoteAtoms{998000};intent.fee_cap=swap::QuoteAtoms{2000};intent.max_quote_debit=swap::QuoteAtoms{1000000};intent.min_base_receive=swap::BaseAtoms{9000000};intent.source_time=1000;intent.sign_by=2000;intent.kill_epoch=1;intent.executor_generation=1;
    r->preparation=std::make_unique<SwapPrepareStore>(std::filesystem::path(directory)/"prepare",r->binding);r->submission=std::make_unique<SwapSubmissionStore>(std::filesystem::path(directory)/"submit",SwapSubmissionBinding{r->binding.allocation.incarnation,r->binding.authority_domain});
    require(r->preparation->create({r->binding,1,digest(6),EffectId{0}})==SwapPrepareStoreCode::none,"bootstrap");
    truemarkets::SwapRestRequest request;require(truemarkets::encode_swap_rest_create({Side::buy,1000000},request)==truemarkets::SwapRestCode::encoded,"create");
    require(r->preparation->begin({intent.effect,intent_hash(intent),sha256_digest(std::as_bytes(std::span(request.body.data(),request.body_size))),{Side::buy,1000000}},2,digest(8))==SwapPrepareStoreCode::none,"begin");
    auto& p=r->p;p.handle=*truemarkets::tm_text("expired-order");p.payload_size=f.envelope.size();std::copy(f.envelope.begin(),f.envelope.end(),p.payload.begin());p.payload_digest=sha256_digest(std::as_bytes(std::span(f.envelope.data(),f.envelope.size())));p.quoted_input=1000000;p.quoted_output=9410957;p.quoted_fee=2000;
    require(r->preparation->complete(p,digest(9))==SwapPrepareStoreCode::none,"prepared");require(r->submission->create({{r->binding.allocation.incarnation,r->binding.authority_domain},1,digest(10),EffectId{0}})==SwapSubmissionCode::none,"submission");
    auto& id=r->identity;id.effect=intent.effect;id.payload=p.payload_digest;id.execution_request=digest(11);id.handle_size=p.handle.length;std::copy_n(reinterpret_cast<const std::byte*>(p.handle.bytes.data()),p.handle.length,id.handle.begin());id.handle_digest=sha256_digest(std::span(id.handle).first(id.handle_size));
    require(r->submission->begin(id,3)==SwapSubmissionCode::none,"execute intent");if(with_transaction)require(r->submission->observe_transaction(id,f.signature)==SwapSubmissionCode::none,"transaction");
    return r;
}
SwapVenueFailedOrder venue_failed(){SwapVenueFailedOrder v;v.terminal=true;v.failed=true;v.executed_base_atoms=0;v.leaves_base_atoms=0;v.fee_quote_atoms=0;v.response_digest=digest(77);return v;}
SwapSettlementAdmissionResult admit_expired(Retained& r,const LifecycleFixture& f,ScriptPort& p,const SwapVenueFailedOrder& v){
    return admit_expired_unexecuted_settlement(*r.preparation,*r.submission,r.binding,r.intent,v,p,digest(1),20,7,"org",address(2),key(2));}
swap::State through_unknown(const Retained& r){swap::Initialize opening;opening.binding=r.intent.binding;
    opening.limits={swap::QuoteAtoms{20000000},swap::QuoteAtoms{2000000},swap::QuoteAtoms{10000},swap::QuoteAtoms{500000},swap::BaseAtoms{1000000000},swap::BaseAtoms{3000000},swap::QuoteAtoms{125000000},swap::QuoteAtoms{50000000},10000,20000};
    opening.quote=swap::QuoteAtoms{25000000};opening.funding_evidence=digest(90);opening.kill_epoch=1;opening.executor_generation=1;swap::State s;
    require(swap::apply(s,opening,swap::Role::operator_role,1000)==swap::Code::applied && swap::apply(s,r.intent,swap::Role::strategy,1000)==swap::Code::applied,"reserved");
    require(swap::apply(s,swap::Prepare{r.intent.effect,r.p.payload_digest,r.identity.handle_digest,1},swap::Role::gateway,1001)==swap::Code::applied &&
        swap::apply(s,swap::Attempt{r.intent.effect,r.p.payload_digest},swap::Role::gateway,1002)==swap::Code::applied &&
        swap::apply(s,swap::MarkUnknown{r.intent.effect},swap::Role::gateway,1003)==swap::Code::applied,"unknown");return s;}
void expired_admission(){
    LifecycleFixture f;stamped(f);auto r=retain(f);ScriptPort p;script(p,f);const auto checksum=r->submission->latest().checksum;
    auto result=admit_expired(*r,f,p,venue_failed());
    require(result.code==SwapSettlementAdmissionCode::admitted && result.verified,"venue_failed_unexecuted_order_admitted");
    const auto& x=result.verified->settlement();
    require(x.outcome==swap::Outcome::expired_unexecuted && x.transaction.is_zero() && x.finalized && !x.evidence.is_zero() && x.payload==r->p.payload_digest &&
        x.custody==r->intent.binding.custody && !x.base_debit.value() && !x.base_credit.value() && !x.quote_debit.value() && !x.quote_credit.value() && !x.fee.value(),"expired settlement carries no amounts and no fabricated transaction");
    require(r->submission->latest().checksum==checksum && r->submission->latest().phase==SwapSubmissionPhase::execute_intent,"admission never resolves journals before the kernel commits");
    auto s=through_unknown(*r);const auto before=s;
    require(swap::apply(s,x,swap::Role::gateway,1004)==swap::Code::applied,"kernel accepts expired settlement from unknown");
    require(s.quote==before.quote && s.base==before.base && s.cost==before.cost && s.fees==before.fees && s.purchases.value()==0 && s.realized_pnl==0 &&
        s.reserved_quote.value()==0 && s.pending.phase==swap::Phase::none && s.settled_total==1 && s.settlement_count==1 && swap::valid_state(s),"only the reservation is released and the snapshot revalidates");
    require(swap::apply(s,x,swap::Role::gateway,1005)==swap::Code::duplicate,"identical retry is idempotent");
    require(swap_settled_predecessor(s,r->submission->latest(),r->intent.binding.custody),"gateway and signer predecessor fence accepts the expired settlement");
    {auto forged=s;forged.settlements[0].outcome=swap::Outcome::succeeded;require(!swap_settled_predecessor(forged,r->submission->latest(),r->intent.binding.custody),"fence refuses a success without a transaction");
     forged=s;forged.settlements[0].transaction=digest(44);require(!swap_settled_predecessor(forged,r->submission->latest(),r->intent.binding.custody),"fence refuses an expired settlement with a transaction");}
    auto late=x;late.outcome=swap::Outcome::succeeded;late.transaction=digest(55);late.base_credit=swap::BaseAtoms{9500000};late.quote_debit=swap::QuoteAtoms{1000000};late.fee=swap::QuoteAtoms{2000};
    auto conflicted=s;require(swap::apply(conflicted,late,swap::Role::gateway,1006)==swap::Code::conflict && conflicted.conflicted,"a later conflicting success stops the account");
    for(unsigned mode=0;mode<7;++mode){LifecycleFixture g;stamped(g);auto q=retain(g);ScriptPort port;script(port,g);auto v=venue_failed();
        if(mode==0)v.failed=false;if(mode==1)v.executed_base_atoms=1;if(mode==2)v.fee_quote_atoms=1;if(mode==3)v.transaction_reported=true;if(mode==4)v.leaves_base_atoms.reset();if(mode==5)v.response_digest={};if(mode==6)v.terminal=false;
        require(!admit_expired(*q,g,port,v).verified,"working, charged, executed, hashed, incomplete or unattested venue answer keeps the reservation");}
    {LifecycleFixture g;stamped(g);auto q=retain(g,true);ScriptPort port;script(port,g);require(admit_expired(*q,g,port,venue_failed()).code==SwapSettlementAdmissionCode::retained_identity_refused,"a known venue transaction must use finalized settlement");}
    {LifecycleFixture g;stamped(g);auto q=retain(g);ScriptPort port;script(port,g,"","true");require(admit_expired(*q,g,port,venue_failed()).code==SwapSettlementAdmissionCode::chain_unproven,"venue failure alone never releases");}
    {LifecycleFixture g;stamped(g);auto q=retain(g);ScriptPort port;script(port,g);port.replies["getTransaction"]={other_tx(g)};require(!admit_expired(*q,g,port,venue_failed()).verified,"landed signed message never becomes expired");}
}
void expired_reducer(){
    LifecycleFixture f;stamped(f);auto r=retain(f);auto s=through_unknown(*r);
    swap::Settlement x;x.effect=r->intent.effect;x.custody=r->intent.binding.custody;x.payload=r->p.payload_digest;x.evidence=digest(66);x.finalized=true;x.outcome=swap::Outcome::expired_unexecuted;
    {auto t=s;auto y=x;y.transaction=digest(5);require(swap::apply(t,y,swap::Role::gateway,1004)==swap::Code::invalid,"expired with a transaction refused");}
    {auto t=s;auto y=x;y.outcome=swap::Outcome::failed_without_charge;require(swap::apply(t,y,swap::Role::gateway,1004)==swap::Code::invalid,"failed without a transaction refused");}
    {auto t=s;auto y=x;y.outcome=swap::Outcome::succeeded;y.base_credit=swap::BaseAtoms{9500000};y.quote_debit=swap::QuoteAtoms{1000000};y.fee=swap::QuoteAtoms{2000};require(swap::apply(t,y,swap::Role::gateway,1004)==swap::Code::invalid,"success without a transaction refused");}
    {auto t=s;auto y=x;y.quote_debit=swap::QuoteAtoms{1};require(swap::apply(t,y,swap::Role::gateway,1004)==swap::Code::conflict,"expired with any amount is a conflict");}
    {auto t=s;auto y=x;y.finalized=false;require(swap::apply(t,y,swap::Role::gateway,1004)==swap::Code::invalid,"unfinalized expired refused");}
    {auto t=s;auto y=x;y.evidence={};require(swap::apply(t,y,swap::Role::gateway,1004)==swap::Code::invalid,"expired without evidence refused");}
    {auto t=s;require(swap::apply(t,x,swap::Role::strategy,1004)==swap::Code::unauthorized,"strategy cannot author settlement");}
    {swap::State t;swap::Initialize o=s.opening;require(swap::apply(t,o,swap::Role::operator_role,1000)==swap::Code::applied && swap::apply(t,r->intent,swap::Role::strategy,1000)==swap::Code::applied,"reserved only");
        require(swap::apply(t,x,swap::Role::gateway,1004)==swap::Code::bad_transition,"reserved effect cannot be expired; it uses ReleaseUnsent");
        require(swap::apply(t,swap::Prepare{r->intent.effect,r->p.payload_digest,r->identity.handle_digest,1},swap::Role::gateway,1001)==swap::Code::applied &&
            swap::apply(t,x,swap::Role::gateway,1004)==swap::Code::bad_transition,"prepared but never attempted cannot be expired");}
    {auto t=s;require(swap::apply(t,x,swap::Role::gateway,1004)==swap::Code::applied,"expired applied");
        auto forged=t;forged.settlements[0].transaction=digest(3);require(!swap::valid_state(forged),"snapshot with expired and a transaction refused");
        forged=t;forged.settlements[0].quote_credit=swap::QuoteAtoms{1};require(!swap::valid_state(forged),"snapshot with expired amounts refused");}
    {auto t=s;require(swap::apply(t,x,swap::Role::gateway,1004)==swap::Code::applied,"first expired");
        for(std::uint64_t e=2;e<=18;++e){auto i=r->intent;i.effect=EffectId{e};i.source_time=1000+e*10;i.sign_by=i.source_time+1000;
            require(swap::apply(t,i,swap::Role::strategy,i.source_time)==swap::Code::applied,"next intent");
            const Digest256 pay=digest(100+e);require(swap::apply(t,swap::Prepare{i.effect,pay,digest(150+e),1},swap::Role::gateway,i.source_time+1)==swap::Code::applied &&
                swap::apply(t,swap::Attempt{i.effect,pay},swap::Role::gateway,i.source_time+2)==swap::Code::applied,"attempt");
            auto y=x;y.effect=i.effect;y.payload=pay;y.evidence=digest(200+e);
            if(e%3==0){y.outcome=swap::Outcome::succeeded;y.transaction=digest(e);y.base_credit=swap::BaseAtoms{9500000};y.quote_debit=swap::QuoteAtoms{1000000};y.fee=swap::QuoteAtoms{2000};}
            require(swap::apply(t,y,swap::Role::gateway,i.source_time+3)==swap::Code::applied,"window settlement");}
        require(t.settled_total==18 && t.settlement_count==16 && swap::valid_state(t),"evicted history with several expired entries revalidates");}
}
}
int main(int argc,char** argv){const std::pair<const char*,void(*)()> tests[]={{"fee_only_rollback",fee_only_rollback},{"sponsor_policy",sponsor_policy},{"native_rollback",native_rollback},{"token_rollback",token_rollback},{"failure_shape",failure_shape},{"owner_payer",owner_payer},{"account_key_codec",account_key_codec},{"retained_failed_admission",retained_failed_admission},{"chain_proven",chain_proven},{"chain_refusals",chain_refusals},{"rpc_templates",rpc_templates},{"expired_admission",expired_admission},{"expired_reducer",expired_reducer}};for(auto [name,test]:tests){if(argc==2 && std::string_view(argv[1])!=name)continue;try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';return 1;}}}
