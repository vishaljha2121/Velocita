#include "velocita/v2/swap_settlement_admission.hpp"
#include "swap_settlement_test_fixture.hpp"
#include "velocita/v2/swap_codec.hpp"
#include "velocita/v2/swap_submission_store.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unistd.h>
#include <vector>
using namespace velocita::v2;
static_assert(!std::is_default_constructible_v<VerifiedSwapSettlement>);
static_assert(!std::is_constructible_v<VerifiedSwapSettlement,swap::Settlement>);
static_assert(!std::is_aggregate_v<VerifiedSwapSettlement>);
static_assert(!std::is_trivially_copyable_v<VerifiedSwapSettlement>);
namespace {
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
truemarkets::SolanaAccountKey key(unsigned n){truemarkets::SolanaAccountKey k{};k[31]=static_cast<std::uint8_t>(n);return k;}
std::string address(unsigned n){return std::string(31,'1')+"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"[n];}
Digest256 digest(unsigned n){Digest256 d;d.bytes[0]=static_cast<std::byte>(n);return d;}
std::string base64(const std::vector<std::uint8_t>& data){
    constexpr char digits[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string s;
    for(std::size_t i=0;i<data.size();i+=3){const unsigned n=(unsigned(data[i])<<16)|(i+1<data.size()?unsigned(data[i+1])<<8:0)|(i+2<data.size()?data[i+2]:0);
        s+=digits[n>>18];s+=digits[(n>>12)&63];s+=i+1<data.size()?digits[(n>>6)&63]:'=';s+=i+2<data.size()?digits[n&63]:'=';}
    return s;
}
std::string wrap(const std::vector<std::uint8_t>& b){std::string h;for(auto c:b){h+="0123456789abcdef"[c>>4];h+="0123456789abcdef"[c&15];}
    return "{\"organizationId\":\"org\",\"parameters\":{\"signWith\":\""+address(2)+"\",\"type\":\"TRANSACTION_TYPE_SOLANA\",\"unsignedTransaction\":\""+h+"\"},\"timestampMs\":\"1000\",\"type\":\"ACTIVITY_TYPE_SIGN_TRANSACTION_V2\"}";}
std::string token(unsigned index,unsigned owner,std::uint64_t amount){return "{\"accountIndex\":"+std::to_string(index)+",\"mint\":\""+std::string(truemarkets::kSwapUsdcMint)+"\",\"owner\":\""+address(owner)+"\",\"programId\":\"TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA\",\"uiTokenAmount\":{\"amount\":\""+std::to_string(amount)+"\",\"decimals\":6}}";}
void replace(std::string& s,std::string_view a,std::string_view b){auto p=s.find(a);require(p!=std::string::npos,"fixture replacement exists");s.replace(p,a.size(),b);}
struct Clock:SwapFinalizedReadClock{std::uint64_t now{10};std::uint64_t now_ms()noexcept override{return now;}};
struct Port:SwapFinalizedRpcPort{
#ifdef VELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY
    std::function<void()> during;
#endif
Digest256 identity{digest(1)};SettlementRpcResponse reply{};std::string requested;std::uint64_t requested_timeout{};unsigned calls{};Clock* clock{};bool change_identity{},late{},backward{},throws{};
    Digest256 provider_identity()const noexcept override{return identity;}
    SettlementRpcResponse request(std::string_view b,std::uint64_t timeout_ms)override{
#ifdef VELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY
        if(during)during();
#endif
        ++calls;requested=b;requested_timeout=timeout_ms;if(change_identity)identity=digest(2);if(late)clock->now=30;if(backward)clock->now=9;if(throws)throw std::runtime_error("lost");return reply;}
    void body(const std::string& s){reply={};reply.transport_complete=true;reply.http_status=200;reply.size=s.size();std::copy(s.begin(),s.end(),reply.body.begin());}
};
struct Fixture {
    Clock clock;Port port;std::vector<std::uint8_t> unsigned_tx,signed_tx;SwapSubmissionTransaction signature{};std::string original,json;
    Fixture(){port.clock=&clock;unsigned_tx={2};unsigned_tx.resize(129,0);unsigned_tx.insert(unsigned_tx.end(),{128,2,0,1,5});
        for(unsigned i=1;i<=5;++i){unsigned_tx.insert(unsigned_tx.end(),31,0);unsigned_tx.push_back(i);}
        unsigned_tx.insert(unsigned_tx.end(),32,3);unsigned_tx.insert(unsigned_tx.end(),{1,4,1,1,1,42,0});
        signed_tx=unsigned_tx;for(unsigned i=0;i<64;++i){signature[i]=static_cast<std::byte>(i+1);signed_tx[i+1]=i+1;signed_tx[65+i]=i+2;}original=wrap(unsigned_tx);rebuild();}
    void rebuild(){json="{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{\"slot\":100,\"version\":0,\"transaction\":[\""+base64(signed_tx)+"\",\"base64\"],\"meta\":{\"err\":null,\"fee\":500,\"preBalances\":[30000000,10000000,2039280,2039280,1],\"postBalances\":[20999500,19000000,2039280,2039280,1],\"loadedAddresses\":{\"writable\":[],\"readonly\":[]},\"preTokenBalances\":["+token(2,2,2000000)+","+token(3,9,1000000)+"],\"postTokenBalances\":["+token(2,2,998000)+","+token(3,9,2002000)+"]}}}";port.body(json);}
    FinalizedReadResult read(){SwapFinalizedReader reader(port,clock,digest(1),20);return reader.read(signature,7,100);}
    SettlementEvidence decode(){port.body(json);const auto r=read();require(r.evidence.has_value(),"fixture receives attributed response");return decode_settlement_evidence(*r.evidence,original,"org",address(2),{},key(2));}
};

Digest256 intent_hash(const swap::Intent& intent){
    std::array<std::byte,swap::max_record_bytes> bytes{};
    const auto encoded=swap::encode({swap::Role::strategy,intent.source_time,intent},bytes);
    require(encoded.error==swap::CodecError::none,"canonical intent fixture encoding");
    return sha256_digest(std::span(bytes).first(encoded.size));
}
void retained_admission(){
    for(unsigned mode=0;mode<9;++mode){
        Fixture f;
        auto root=std::filesystem::path(std::getenv("HOME"))/".cache/velocita-build/settlement-admission-20261012";
        std::filesystem::create_directories(root);auto directory=(root/"case-XXXXXX").string();require(::mkdtemp(directory.data()),"admission scratch");
        struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}}cleanup{directory};
        const SwapPrepareBinding binding{{digest(1),digest(2),digest(3),AccountId{2},InstrumentId{2},PolicyId{901},PolicyVersion{1},RiskPolicyVersion{1}},digest(4),digest(5)};
        swap::Intent intent;intent.binding=binding.allocation;intent.effect=EffectId{1};intent.side=Side::buy;
        intent.quote_input=swap::QuoteAtoms{998000};intent.fee_cap=swap::QuoteAtoms{2000};intent.max_quote_debit=swap::QuoteAtoms{1000000};
        intent.min_base_receive=swap::BaseAtoms{9000000};intent.source_time=1000;intent.sign_by=2000;intent.kill_epoch=1;intent.executor_generation=1;
        SwapPrepareStore preparation(std::filesystem::path(directory)/"prepare",binding);
        SwapSubmissionStore submission(std::filesystem::path(directory)/"submit",{binding.allocation.incarnation,binding.authority_domain});
        require(preparation.create({binding,1,digest(6),EffectId{0}})==SwapPrepareStoreCode::none,"preparation bootstrap");
        truemarkets::SwapRestRequest request;require(truemarkets::encode_swap_rest_create({Side::buy,1000000},request)==truemarkets::SwapRestCode::encoded,"create request");
        require(preparation.begin({intent.effect,intent_hash(intent),sha256_digest(std::as_bytes(std::span(request.body.data(),request.body_size))),{Side::buy,1000000}},2,digest(8))==SwapPrepareStoreCode::none,"prepare intent retained");
        truemarkets::SwapRestPrepared p;p.handle=*truemarkets::tm_text("order-1");p.payload_size=f.original.size();
        std::copy(f.original.begin(),f.original.end(),p.payload.begin());p.payload_digest=sha256_digest(std::as_bytes(std::span(f.original.data(),f.original.size())));
        p.quoted_input=1000000;p.quoted_output=9000000;p.quoted_fee=2000;
        require(preparation.complete(p,digest(9))==SwapPrepareStoreCode::none,"original retained");
        require(submission.create({{binding.allocation.incarnation,binding.authority_domain},1,digest(10),EffectId{0}})==SwapSubmissionCode::none,"submission bootstrap");
        SwapSubmissionIdentity identity;identity.effect=intent.effect;identity.payload=p.payload_digest;identity.execution_request=digest(11);
        identity.handle_size=p.handle.length;std::copy_n(reinterpret_cast<const std::byte*>(p.handle.bytes.data()),p.handle.length,identity.handle.begin());
        identity.handle_digest=sha256_digest(std::span(identity.handle).first(identity.handle_size));
        require(submission.begin(identity,3)==SwapSubmissionCode::none,"retained execute");
        if(mode!=3)require(submission.observe_transaction(identity,f.signature)==SwapSubmissionCode::none,"retained transaction");
        if(mode==1)intent.min_base_receive=swap::BaseAtoms{1};
        if(mode==2)intent.binding.custody=digest(80);
        if(mode==4){replace(f.json,"\"err\":null","\"err\":{\"InstructionError\":[0,1]}");f.port.body(f.json);}
        if(mode==5)f.port.body("{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":null}");
        if(mode==6)f.port.during=[&]{require(submission.resolve({identity,4,digest(88)})==SwapSubmissionCode::none,"advance during RPC");};
        if(mode==7){replace(f.json,"19000000","18999999");f.port.body(f.json);}
        if(mode==8)intent.effect=EffectId{2};
        const auto checksum=submission.latest().checksum;
        SwapFinalizedReader reader(f.port,f.clock,digest(1),20);
        const auto result=admit_retained_swap_settlement(preparation,submission,binding,intent,reader,7,100,"org",address(2),{},key(2));
        require(!result.verified,"no_observation_can_mint_settlement_proof");
        if(mode==0){require(result.code==SwapSettlementAdmissionCode::lifecycle_unproven,"balanced_receipt_still_needs_lifecycle");
            require(result.required_evidence==15,"all_missing_economic_facts_explicit");
            require(result.observation.evidence.signature==f.signature,"exact_receipt_identity_retained");}
        if(mode==1 || mode==2 || mode==8)require(result.code==SwapSettlementAdmissionCode::intent_refused && f.port.calls==0,"changed_intent_refused_before_RPC");
        if(mode==3 || mode==6)require(result.code==SwapSettlementAdmissionCode::retained_identity_refused,"retained_identity_or_race_refused");
        if(mode==4 || mode==7)require(result.code==SwapSettlementAdmissionCode::evidence_refused,"failed_or_unbalanced_not_zero_charge");
        if(mode==5)require(result.code==SwapSettlementAdmissionCode::rpc_unresolved,"missing_transaction_remains_unknown");
        if(mode!=6)require(submission.latest().checksum==checksum && submission.latest().phase==SwapSubmissionPhase::execute_intent,"admission_does_not_release_or_resolve");
    }
}

SwapSettlementAdmissionResult admit_buy_record(settlement_lifecycle_fixture::LifecycleFixture& f,unsigned mode,bool sell=false){
    auto root=std::filesystem::path(std::getenv("HOME"))/".cache/velocita-build/settlement-proof-20261012";std::filesystem::create_directories(root);
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
    auto result=admit_retained_swap_settlement(preparation,submission,binding,intent,reader,7,100,"org",address(2),{},key(2));
    require(submission.latest().checksum==checksum && submission.latest().phase==SwapSubmissionPhase::execute_intent,"proof_does_not_resolve_before_kernel_commit");
    if(result.verified){swap::Initialize opening;opening.binding=intent.binding;
        opening.limits={swap::QuoteAtoms{5000000},swap::QuoteAtoms{2000000},swap::QuoteAtoms{10000},swap::QuoteAtoms{500000},swap::BaseAtoms{1000000000},swap::BaseAtoms{3000000},swap::QuoteAtoms{125000000},swap::QuoteAtoms{50000000},10000,20000};
        opening.base=swap::BaseAtoms{sell?8400000U:0U};opening.base_cost=swap::QuoteAtoms{sell?1000000U:0U};opening.quote=swap::QuoteAtoms{25000000};opening.funding_evidence=digest(90);opening.kill_epoch=1;opening.executor_generation=1;
        swap::State state;
        require(swap::apply(state,opening,swap::Role::operator_role,1000)==swap::Code::applied && swap::apply(state,intent,swap::Role::strategy,1000)==swap::Code::applied,"admission_composition_reserves_exact_intent");
        require(swap::apply(state,swap::Prepare{intent.effect,p.payload_digest,identity.handle_digest,1},swap::Role::gateway,1001)==swap::Code::applied && swap::apply(state,swap::Attempt{intent.effect,p.payload_digest},swap::Role::gateway,1002)==swap::Code::applied,"admission_composition_prepares_and_attempts");
        require(swap::apply(state,result.verified->settlement(),swap::Role::gateway,1003)==swap::Code::applied,"admitted_receipt_is_kernel_compatible");
        require(state.base.value()==(sell?0U:9410957U) && state.quote.value()==(sell?26006989U:24000000U) && state.fees.value()==(sell?2018U:2000U) && state.cost.value()==(sell?0U:998000U) && state.realized_pnl==(sell?6989:-2000) && state.purchases.value()==(sell?0U:1000000U) && state.reserved_base.value()==0 && state.reserved_quote.value()==0 && state.pending.phase==swap::Phase::none,"admitted_atoms_apply_once_without_double_fee");
    }
    return result;
}
void bounded_buy_admission(){for(unsigned mode=0;mode<14;++mode){settlement_lifecycle_fixture::LifecycleFixture f;
    std::vector<std::uint8_t> route{193,32,155,51,65,214,156,129,6,1,0,0,0,141,0,100,0,1};
    settlement_lifecycle_fixture::little(route,mode==9?999999:1000000,8);settlement_lifecycle_fixture::little(route,9410957,8);settlement_lifecycle_fixture::little(route,500,2);route.push_back(20);
    f.top[1]={10,{8,13,1,2,3,6,5,11,12,4,10,14,10},route};
    if(mode!=1 && mode!=7){f.repayment_lamports=1488440;f.top[3].data.clear();settlement_lifecycle_fixture::little(f.top[3].data,2,4);settlement_lifecycle_fixture::little(f.top[3].data,f.repayment_lamports,8);}
    if(mode==2)f.top[1].accounts[9]=3;
    if(mode==6)f.route[0].accounts={3,4,13};
    if(mode==8)f.top[1].program=13;
    if(mode==10)f.top[1].accounts[9]=2;
    if(mode==11)f.top[1].accounts[0]=9;
    if(mode==12){std::vector<std::uint8_t> change{6,3,1};change.insert(change.end(),f.keys[0].begin(),f.keys[0].end());f.route.insert(f.route.begin(),{8,{2,1},change});}
    if(mode==13){std::vector<std::uint8_t> direct{229,23,203,151,122,227,173,42,1,0,0,0,26,100,0,1};settlement_lifecycle_fixture::little(direct,1000000,8);settlement_lifecycle_fixture::little(direct,9410957,8);settlement_lifecycle_fixture::little(direct,500,2);direct.push_back(20);f.top[1]={10,{8,1,2,5,10,12,4,14,10,3,6,13},direct};}
    f.rebuild();if(mode==6){replace(f.json,"\"amount\":\"6989\"","\"amount\":\"8989\"");replace(f.json,"\"amount\":\"1998000\"","\"amount\":\"1996000\"");}
    const auto result=admit_buy_record(f,mode);
    if(mode==0 || mode==13){require(result.verified.has_value(),"ABI_bound_finalized_buy_can_construct_private_proof");const auto& s=result.verified->settlement();
        require(s.finalized && s.outcome==swap::Outcome::succeeded && s.effect==EffectId{1} && s.custody==digest(3) && s.payload==sha256_digest(std::as_bytes(std::span(f.envelope.data(),f.envelope.size()))) && s.transaction==*solana_signature_transaction_digest(f.signature) && s.evidence==result.observation.evidence.response_digest,"admission_identity_matches_retained_resolution");
        require(s.base_debit.value()==0 && s.base_credit.value()==9410957 && s.quote_debit.value()==1000000 && s.quote_credit.value()==0 && s.fee.value()==2000,"proof_exact_net_atoms_and_fee");}
    else require(!result.verified,"ambiguous_fee_role_input_or_owner_cost_never_admitted");
}}
void bounded_sell_admission(){for(unsigned mode:{0U,4U}){using namespace settlement_lifecycle_fixture;LifecycleFixture f;
    std::vector<std::uint8_t> route{193,32,155,51,65,214,156,129,6,1,0,0,0,141,0,100,0,1};little(route,8400000,8);little(route,1009007,8);little(route,500,2);route.push_back(20);
    std::vector<std::uint8_t> funding;little(funding,2,4);little(funding,8400000,8);
    f.top={f.top[0],{7,{1,5},funding},{8,{5},{17}},{10,{8,13,1,5,6,3,2,12,11,4,10,14,10},route},{8,{5,0,1},{9}}};
    f.route_top_index=3;f.route={{8,{5,6,1},amount(3,8400000)},{8,{3,2,13},amount(3,1009007)},{8,{2,4,1},amount(3,2018)}};f.rebuild();
    ::replace(f.json,"30512840,18860117","29962000,1600000");
    ::replace(f.json,"0,10589043","0,28400000");
    ::replace(f.json,f.tb(3,9,1000000),f.tb(3,9,2000000));
    ::replace(f.json,f.tb(2,2,6989),f.tb(2,2,2013978));::replace(f.json,f.tb(3,9,1998000),f.tb(3,9,990993));::replace(f.json,f.tb(4,9,2000),f.tb(4,9,2018));::replace(f.json,f.tb(6,9,9100603,true),f.tb(6,9,26911560,true));
    const auto result=admit_buy_record(f,mode,true);
    if(mode==0){::require(result.verified.has_value(),"ABI_bound_finalized_sell_can_construct_private_proof");const auto& record=result.verified->settlement();::require(record.base_debit.value()==8400000 && record.base_credit.value()==0 && record.quote_credit.value()==1006989 && record.quote_debit.value()==0 && record.fee.value()==2018,"sell_exact_owner_debit_net_receipt_and_fee");}
    else ::require(!result.verified,"sell_fee_exceeding_intent_remains_unresolved");
}}
void fixture_ownership(){
#ifdef VELOCITA_SWAP_SETTLEMENT_SYNTHETIC_FIXTURE
    swap::Settlement s;s.effect=EffectId{2};s.base_credit=swap::BaseAtoms{9000000};
    const auto proof=settlement_test_fixture::make_verified(s);s.base_credit=swap::BaseAtoms{1};
    const auto copied=proof;require(copied.settlement().base_credit.value()==9000000 && copied.settlement().effect==EffectId{2},"synthetic_fixture_owns_exact_record");
#endif
}
}
int main(int argc,char** argv){const std::pair<const char*,void(*)()> tests[]={{"retained_admission",retained_admission},{"fixture_ownership",fixture_ownership},{"bounded_buy_admission",bounded_buy_admission},{"bounded_sell_admission",bounded_sell_admission}};
    for(auto [name,test]:tests){if(argc==2 && std::string_view(argv[1])!=name)continue;try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';return 1;}}
}
