#define VELOCITA_MVP_BUILDER_TEST
#include "../tools/prepare-swap-mvp.cpp"
#include "v2_swap_transaction_verifier_test_support.hpp"
#include <functional>

namespace {
using namespace velocita::mvp_builder;
std::string hx(std::span<const std::uint8_t> value){constexpr char digits[]="0123456789abcdef";std::string s;for(auto n:value){s+=digits[n>>4];s+=digits[n&15];}return s;}
struct Fixture{
    velocita::v2::test_support::DirectBuyFixture tx;
    std::string profile,history,transaction,opening,intent;
    Fixture(){
        Fields t{{"schema","1"},{"account_keys",""},{"lookup_table",hx(tx.lookup[0].table)},{"lookup_slot","777"},
            {"lookup_evidence",to_hex(tx.lookup[0].evidence_digest)},{"lookup_address_count","8"},{"lookup_addresses",""},
            {"max_envelope_age_ms","100"},{"max_compute_price","71428"},{"max_compute_units","1400000"},{"max_slippage_bps","500"},{"expires_at_ms","2000"}};
        for(const auto& k:tx.policy.account_keys)t["account_keys"]+=hx(k);
        for(unsigned n=0;n<8;++n)t["lookup_addresses"]+=hx(tx.lookup[0].addresses[n]);transaction=document(t);
        const auto d=to_hex(hash("public-fixture"));const auto& b=tx.policy.binding;
        profile=document({{"schema","1"},{"incarnation",to_hex(b.incarnation)},{"catalog",to_hex(b.catalog)},{"custody",to_hex(b.custody)},
            {"authority_domain",d},{"account","2"},{"instrument","2"},{"policy","99"},{"policy_version","1"},{"risk_policy_version","1"},
            {"account_assets",d},{"session_evidence",d},{"owner_wallet",hx(tx.policy.account_keys[1])},
            {"signer_public_key","036b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"},
            {"max_buy_output_deduction_lamports","2039280"},{"transaction_review_digest",to_hex(hash(transaction))},{"account_review_digest",d},
            {"failure_fee_payer",hx(tx.policy.account_keys[0])},{"socket","/run/velocita/signer.sock"},{"gateway_uid","29001"},{"signer_uid","29002"},
            {"gateway_caller","22"},{"signer_caller","23"},{"ipc_timeout_ms","1000"},{"bearer_digest",d},{"expires_at_ms","2000"},
            {"signer_instance",d},{"key_version","1"},{"state_timeout_ms","1000"},{"organization","verifier-fixture"},
            {"signer_id",std::string(31,'1')+'3'},{"user_id","12345678-1234-1234-1234-123456789abc"}});
        const auto owner=sha256_digest(std::as_bytes(std::span(tx.policy.account_keys[1])));
        history="{\"schema\":1,\"scope\":\"sol-usdc-current-accounts\",\"owner_sha256\":\""+to_hex(owner)+"\",\"user_id_sha256\":\""+
            to_hex(hash("12345678-1234-1234-1234-123456789abc"))+"\",\"setup_sha256\":\""+d+"\",\"balances_sha256\":\""+d+
            "\",\"evidence_manifest_sha256\":\""+d+"\",\"capture_started_ms\":900,\"capture_finished_ms\":950,\"start_slot\":7,\"end_slot\":8,"+
            "\"base_atoms\":40000000,\"quote_atoms\":40000000,\"stable_window\":true,\"pagination_exhausted\":true,\"blocking_orders\":0,\"unsupported_accounts\":0}";
        opening=document({{"schema","1"},{"now_ms","1000"},{"review_expires_ms","1800"},{"base_atoms","40000000"},{"quote_atoms","40000000"},
            {"kill_epoch","1"},{"executor_generation","1"},{"max_realized_loss_atoms","500000"},{"max_sell_input_atoms","40000000"},
            {"max_buy_price_per_sol","145000000"},{"min_sell_price_per_sol","100000000"},{"max_source_age_ms","100"},{"max_sign_lifetime_ms","100"},
            {"valuation_net_quote_atoms","4000000"},{"valuation_observed_ms","990"},{"valuation_evidence_sha256",d}});
        intent=document({{"schema","1"},{"now_ms","1050"},{"source_time_ms","1040"},{"sign_by_ms","1100"},{"kill_epoch","1"},{"executor_generation","1"},
            {"minimum_base_atoms","6900812"},{"quote_gross_output_lamports","9410623"},{"quote_slippage_bps","500"},{"repayment_lamports","2039280"},
            {"quote_observed_ms","1030"},{"quote_evidence_sha256",d}});
    }
    Fields build()const{return build_opening(profile,history,transaction,opening);}
};
std::string change(std::string s,const std::string& key,const std::string& value){auto f=fields(s);f[key]=value;return document(f);}
void rejected(const std::function<void()>& operation,std::string_view reason,const char* assertion){
    try{operation();}catch(const std::runtime_error& e){require(std::string_view(e.what())==reason,assertion);return;}throw std::runtime_error(assertion);
}
void valid(){
    Fixture f;auto bundle=f.build();swap::Record r;
    require(swap::decode(std::as_bytes(std::span(bundle.at("opening.record"))),r)==swap::CodecError::none,"opening_decodes");
    const auto& opening=std::get<swap::Initialize>(r.command);
    require(r.role==swap::Role::operator_role && r.event_time==1000 && opening.base.value()==40'000'000 && opening.quote.value()==40'000'000 && opening.base_cost.value()==4'000'000,"opening_uses_explicit_observation_and_mark");
    require(opening.limits.max_purchase_total.value()==1'000'000 && opening.limits.max_buy_debit.value()==1'000'000 && opening.limits.max_fee.value()==2000 && opening.limits.max_transient_base.value()==0,"opening_exact_approved_limits");
    require(opening.funding_evidence==hash(bundle.at("funding.review")),"opening_pins_mark_provenance");
    const auto p=decode_swap_runtime_profile(bundle.at("runtime.profile"));require(p && p->account_review_digest==hash(bundle.at("account.review")) && p->account_review_digest!=decode_swap_runtime_profile(f.profile)->account_review_digest,"final_profile_uses_computed_review");
    const auto output=build_intent(bundle,f.intent);require(swap::decode(std::as_bytes(std::span(output.at("intent.record"))),r)==swap::CodecError::none,"intent_decodes");
    const auto& i=std::get<swap::Intent>(r.command);
    require(r.role==swap::Role::strategy && r.event_time==1050 && i.effect.value()==1 && i.side==Side::buy && i.quote_input.value()==998'000 &&
        i.max_quote_debit.value()==1'000'000 && i.fee_cap.value()==2'000 && i.min_base_receive.value()==6'900'812 && i.transient_base_hold.value()==0,"intent_exact_one_buy_oracle");
    require(fields(output.at("record-summary.txt")).at("conservative_quote_floor")=="6900812","floor_oracle_8940092_minus_2039280");
}
void parser(){
    rejected([]{(void)fields("schema=1\nschema=1\n");},"duplicate parameter","duplicate_parameter_refused");
    for(auto value:{"-1","+1","01","18446744073709551616","1junk"}){Fields f{{"n",value}};rejected([&]{(void)number(f,"n");},"invalid unsigned parameter","strict_integer_refused");}
    Fixture f;rejected([&]{(void)build_opening(f.profile,f.history,f.transaction,f.opening+"effect=2\n");},"unknown opening parameter","opening_unknown_refused");
    auto bundle=f.build();rejected([&]{(void)build_intent(bundle,f.intent+"effect=2\n");},"unknown intent parameter","second_effect_parameter_refused");
}
void scope(){
    Fixture f;rejected([&]{(void)build_opening(f.profile,f.history,f.transaction,change(f.opening,"max_realized_loss_atoms","500001"));},"opening reducer or one-buy scope refused","loss_cap_one_atom_refused");
    rejected([&]{(void)build_opening(change(f.profile,"max_buy_output_deduction_lamports","2039281"),f.history,f.transaction,f.opening);},"runtime profile not admitted","repayment_cap_one_atom_refused");
    rejected([&]{(void)build_opening(f.profile,f.history,f.transaction,change(f.opening,"base_atoms","40000001"));},"observed balances do not match all-inventory history","balance_discrepancy_refused");
    rejected([&]{(void)build_opening(f.profile,f.history,f.transaction,change(f.opening,"valuation_observed_ms","899"));},"mark or intent timing bound invalid","stale_mark_refused");
    rejected([&]{(void)build_opening(f.profile,f.history,f.transaction,change(f.opening,"review_expires_ms","2001"));},"review window exceeds current evidence","expiry_refused");
    auto bundle=f.build();rejected([&]{(void)build_intent(bundle,change(f.intent,"minimum_base_atoms","6900813"));},"conservative quote floor refused","one_lamport_receipt_shortfall_refused");
    rejected([&]{(void)build_intent(bundle,change(f.intent,"quote_observed_ms","950"));},"fresh quote or intent window refused","expired_quote_refused");
    rejected([&]{(void)build_intent(bundle,change(f.intent,"kill_epoch","2"));},"intent reducer or one-buy scope refused","wrong_epoch_refused");
    auto wrong=f.history;auto begin=wrong.find("\"owner_sha256\":\"")+16;wrong[begin]=wrong[begin]=='a'?'b':'a';
    rejected([&]{(void)build_opening(f.profile,wrong,f.transaction,f.opening);},"history identity or funding refused","wrong_history_owner_refused");
    rejected([&]{(void)build_opening(change(f.profile,"failure_fee_payer",std::string(63,'0')+'9'),f.history,f.transaction,f.opening);},"sponsor pin differs","wrong_sponsor_refused");
}
void files(const fs::path& root){
    Fixture f;const auto out=root/"bundle";emit(out,f.build());const auto old=read(out/"opening.record",512);
    rejected([&]{emit(out,{{"opening.record","changed"}});},"output already exists","exclusive_output_refused");
    require(read(out/"opening.record",512)==old,"existing_record_preserved");
    fs::create_symlink(out/"opening.record",root/"link");rejected([&]{(void)read(root/"link",512);},"input unavailable or symlink","symlink_input_refused");
    require(::mkfifo((root/"fifo").c_str(),0600)==0,"fifo_fixture_created");
    rejected([&]{(void)read(root/"fifo",512);},"input is not a bounded regular file","fifo_input_refused_without_blocking");
    struct stat st{};require(::stat((out/"opening.record").c_str(),&st)==0 && (st.st_mode&0777)==0600,"private_record_permissions");
}
}
int main(int argc,char** argv){
    try{require(argc==2,"fresh scratch directory argument required");const fs::path root=argv[1];require(fs::create_directory(root),"scratch already exists");
        valid();std::cout<<"PASS valid\n";parser();std::cout<<"PASS parser\n";scope();std::cout<<"PASS scope\n";files(root);std::cout<<"PASS files\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
