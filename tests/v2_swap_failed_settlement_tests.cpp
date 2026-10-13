#include "velocita/v2/swap_settlement_admission.hpp"
#include "velocita/v2/truemarkets_wire.hpp"
#include "swap_settlement_test_fixture.hpp"
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
}
int main(int argc,char** argv){const std::pair<const char*,void(*)()> tests[]={{"fee_only_rollback",fee_only_rollback},{"sponsor_policy",sponsor_policy},{"native_rollback",native_rollback},{"token_rollback",token_rollback},{"failure_shape",failure_shape},{"owner_payer",owner_payer},{"account_key_codec",account_key_codec},{"retained_failed_admission",retained_failed_admission}};for(auto [name,test]:tests){if(argc==2 && std::string_view(argv[1])!=name)continue;try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';return 1;}}}
