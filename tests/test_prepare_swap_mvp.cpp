#define VELOCITA_MVP_BUILDER_TEST
#include "../tools/prepare-swap-mvp.cpp"
#include "v2_swap_transaction_verifier_test_support.hpp"
#include <functional>
#include <vector>

namespace {
using namespace velocita::mvp_builder;
std::string hx(std::span<const std::uint8_t> value){constexpr char digits[]="0123456789abcdef";std::string s;for(auto n:value){s+=digits[n>>4];s+=digits[n&15];}return s;}
struct Fixture{
    velocita::v2::test_support::RouteFixture tx;
    std::string profile,history,transaction,opening,intent;
    Fixture(){
        transaction=document({{"schema","6"},{"sponsor",hx(tx.policy.sponsor)},{"owner_usdc",hx(tx.policy.owner_usdc)},
            {"owner_wsol",hx(tx.policy.owner_wsol)},{"platform_fee",hx(tx.policy.platform_fee)},
            {"max_envelope_age_ms","100"},{"max_compute_price","71428"},{"max_compute_units","1400000"},{"max_slippage_bps","500"}});
        const auto d=to_hex(hash("public-fixture"));const auto& b=tx.policy.binding;
        profile=document({{"schema","1"},{"incarnation",to_hex(b.incarnation)},{"catalog",to_hex(b.catalog)},{"custody",to_hex(b.custody)},
            {"authority_domain",d},{"account","2"},{"instrument","2"},{"policy","99"},{"policy_version","1"},{"risk_policy_version","1"},
            {"account_assets",d},{"session_evidence",d},{"owner_wallet",hx(tx.policy.owner)},
            {"signer_public_key","036b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"},
            {"max_buy_output_deduction_lamports","2039280"},{"transaction_review_digest",to_hex(hash(transaction))},{"account_review_digest",d},
            {"failure_fee_payer",hx(tx.policy.sponsor)},{"socket","/run/velocita/signer.sock"},{"gateway_uid","29001"},{"signer_uid","29002"},
            {"gateway_caller","22"},{"signer_caller","23"},{"ipc_timeout_ms","1000"},{"bearer_digest",d},{"expires_at_ms","2000"},
            {"signer_instance",d},{"key_version","1"},{"state_timeout_ms","1000"},{"organization","verifier-fixture"},
            {"signer_id",std::string(31,'1')+'3'},{"user_id","12345678-1234-1234-1234-123456789abc"}});
        const auto owner=sha256_digest(std::as_bytes(std::span(tx.policy.owner)));
        history="{\"schema\":1,\"scope\":\"sol-usdc-current-accounts\",\"owner_sha256\":\""+to_hex(owner)+"\",\"user_id_sha256\":\""+
            to_hex(hash("12345678-1234-1234-1234-123456789abc"))+"\",\"setup_sha256\":\""+d+"\",\"balances_sha256\":\""+d+
            "\",\"evidence_manifest_sha256\":\""+d+"\",\"capture_started_ms\":900,\"capture_finished_ms\":950,\"start_slot\":7,\"end_slot\":8,"+
            "\"base_atoms\":40000000,\"quote_atoms\":40000000,\"stable_window\":true,\"pagination_exhausted\":true,\"blocking_orders\":0,\"unsupported_accounts\":0}";
        opening=document({{"schema","1"},{"now_ms","1000"},{"review_expires_ms","1800"},{"base_atoms","40000000"},{"quote_atoms","40000000"},
            {"kill_epoch","1"},{"executor_generation","1"},{"max_realized_loss_atoms","500000"},{"max_sell_input_atoms","40000000"},
            {"max_buy_price_per_sol","145000000"},{"min_sell_price_per_sol","100000000"},{"max_source_age_ms","100"},{"max_sign_lifetime_ms","100"},
            {"valuation_net_quote_atoms","4000000"},{"valuation_observed_ms","990"},{"valuation_evidence_sha256",d}});
        intent=document({{"schema","1"},{"now_ms","1050"},{"source_time_ms","1040"},{"sign_by_ms","1100"},{"kill_epoch","1"},{"executor_generation","1"},
            {"minimum_base_atoms","6900811"},{"quote_gross_output_lamports","9410623"},{"quote_slippage_bps","500"},{"repayment_lamports","2039280"},
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
        i.max_quote_debit.value()==1'000'000 && i.fee_cap.value()==2'000 && i.min_base_receive.value()==6'900'811 && i.transient_base_hold.value()==0,"intent_exact_one_buy_oracle");
    require(fields(output.at("record-summary.txt")).at("conservative_quote_floor")=="6900811","floor_oracle_8940091_rounded_down_minus_2039280");
}
void parser(){
    rejected([]{(void)fields("schema=1\nschema=1\n");},"duplicate parameter","duplicate_parameter_refused");
    for(auto value:{"-1","+1","01","18446744073709551616","1junk"}){Fields f{{"n",value}};rejected([&]{(void)number(f,"n");},"invalid unsigned parameter","strict_integer_refused");}
    Fixture f;rejected([&]{(void)build_opening(f.profile,f.history,f.transaction,f.opening+"effect=2\n");},"unknown opening parameter","opening_unknown_refused");
    auto bundle=f.build();rejected([&]{(void)build_intent(bundle,f.intent+"effect=0\n");},"intent effect must follow released predecessors","zero_effect_refused");
    const auto next=build_intent(bundle,f.intent+"effect=2\n");swap::Record r;
    require(swap::decode(std::as_bytes(std::span(next.at("intent.record"))),r)==swap::CodecError::none && std::get<swap::Intent>(r.command).effect.value()==2 &&
        fields(next.at("record-summary.txt")).at("assumed_released_effect")=="1" && fields(next.at("record-summary.txt")).at("effect")=="2",
        "next_effect_builds_assuming_its_predecessor_was_released");
    require(!fields(build_intent(bundle,f.intent).at("record-summary.txt")).contains("assumed_released_effect"),"first_effect_assumes_nothing");
}
void scope(){
    Fixture f;rejected([&]{(void)build_opening(f.profile,f.history,f.transaction,change(f.opening,"max_realized_loss_atoms","10000001"));},"opening reducer or live scope refused","loss_cap_above_ten_dollars_refused");
    rejected([&]{(void)build_opening(change(f.profile,"max_buy_output_deduction_lamports","2039281"),f.history,f.transaction,f.opening);},"runtime profile not admitted","repayment_cap_one_atom_refused");
    rejected([&]{(void)build_opening(f.profile,f.history,f.transaction,change(f.opening,"base_atoms","40000001"));},"observed balances do not match all-inventory history","balance_discrepancy_refused");
    rejected([&]{(void)build_opening(f.profile,f.history,f.transaction,change(f.opening,"valuation_observed_ms","899"));},"mark or intent timing bound invalid","stale_mark_refused");
    rejected([&]{(void)build_opening(f.profile,f.history,f.transaction,change(f.opening,"review_expires_ms","2001"));},"review window exceeds current evidence","expiry_refused");
    auto bundle=f.build();rejected([&]{(void)build_intent(bundle,change(f.intent,"minimum_base_atoms","6900812"));},"conservative quote floor refused","one_lamport_receipt_shortfall_refused");
    rejected([&]{(void)build_intent(bundle,change(f.intent,"quote_observed_ms","950"));},"fresh quote or intent window refused","expired_quote_refused");
    rejected([&]{(void)build_intent(bundle,change(f.intent,"kill_epoch","2"));},"intent reducer or live scope refused","wrong_epoch_refused");
    auto wrong=f.history;auto begin=wrong.find("\"owner_sha256\":\"")+16;wrong[begin]=wrong[begin]=='a'?'b':'a';
    rejected([&]{(void)build_opening(f.profile,wrong,f.transaction,f.opening);},"history identity or funding refused","wrong_history_owner_refused");
    rejected([&]{(void)build_opening(change(f.profile,"failure_fee_payer",std::string(63,'0')+'9'),f.history,f.transaction,f.opening);},"sponsor pin differs","wrong_sponsor_refused");
}
std::string amendment_parameters(const Fields& bundle){
    swap::Record record;const auto& original=bundle.at("opening.record");
    require(swap::decode(std::as_bytes(std::span(original)),record)==swap::CodecError::none,"amendment_fixture_opening_decodes");
    return document({{"schema","1"},{"now_ms","1020"},
        {"expected_opening_sha256",to_hex(swap_opening_review_digest(std::get<swap::Initialize>(record.command)))},
        {"review_evidence_sha256",to_hex(hash("explicit-owner-price-review"))},
        {"expected_price","146635156"},{"replacement_price","147537213"},
        {"expected_revision","0"},{"kill_epoch","1"},{"executor_generation","1"}});
}
int command(std::vector<std::string> args){
    std::vector<char*> argv;for(auto& arg:args)argv.push_back(arg.data());return run(static_cast<int>(argv.size()),argv.data());
}
void amendment(const fs::path& root){
    Fixture f;f.opening=change(f.opening,"max_buy_price_per_sol","146635156");auto bundle=f.build();
    const auto parameters=amendment_parameters(bundle);const auto p=root/"amendment.parameters",o=root/"original.record",out=root/"amendment-proposal";
    write_new(p,parameters);write_new(o,bundle.at("opening.record"));
    try{require(command({"prepare-swap-mvp","price-amendment","--opening-record",o.string(),"--parameters",p.string(),"--output",out.string()})==0,"amendment_cli_success");}
    catch(const std::exception&){throw std::runtime_error("price_amendment_cli_must_prepare_offline_proposal");}
    const auto bytes=read(out/"amendment.record",512);swap::Record decoded;
    require(swap::decode(std::as_bytes(std::span(bytes)),decoded)==swap::CodecError::none && canonical(decoded)==bytes &&
        decoded.role==swap::Role::operator_role && decoded.event_time==1020,"amendment_operator_canonical_record");
    const auto& proposed=std::get<swap::AmendBuyPrice>(decoded.command);
    auto parameter_fields=fields(parameters);
    require(proposed.opening_digest==digest(parameter_fields,"expected_opening_sha256") &&
        proposed.review_evidence==hash("explicit-owner-price-review") && proposed.expected_price.value()==146635156 &&
        proposed.replacement_price.value()==147537213 && proposed.expected_revision==0 && proposed.kill_epoch==1 && proposed.executor_generation==1,
        "amendment_exact_reviewed_values");
    const auto summary=fields(read(out/"record-summary.txt",8192));
    require(summary.at("kind")=="AmendBuyPrice" && summary.at("proposal_only")=="1" && summary.at("committed")=="0" &&
        summary.at("original_opening_record_sha256")==to_hex(hash(bundle.at("opening.record"))) &&
        summary.at("replacement_price")=="147537213","amendment_summary_not_commit_evidence");
    require(read(o,512)==bundle.at("opening.record") && !fs::exists(out/"opening.record"),"amendment_preserves_original_opening");
    auto lower=change(change(f.intent,"minimum_base_atoms","6777951"),"quote_gross_output_lamports","9281296");
    rejected([&]{(void)build_intent(bundle,lower);},"intent reducer or live scope refused","unamended_price_floor_still_refuses");
    bundle["amendment.record"]=bytes;
    const auto intent=build_intent(bundle,lower);const auto description=fields(intent.at("record-summary.txt"));
    require(description.at("amendment_record_sha256")==to_hex(hash(bytes)) && description.at("amendment_committed")=="0" &&
        description.at("conservative_quote_floor")=="6777951","intent_pins_uncommitted_amendment_and_exact_floor");
    require(bundle.at("opening.record")==read(o,512),"intent_does_not_rewrite_opening");
    require(swap::decode(std::as_bytes(std::span(intent.at("intent.record"))),decoded)==swap::CodecError::none,"amended_intent_decodes");
    const auto& i=std::get<swap::Intent>(decoded.command);
    require(i.effect.value()==1 && i.max_quote_debit.value()==1000000 && i.quote_input.value()==998000 && i.fee_cap.value()==2000 &&
        i.min_base_receive.value()==6777951,"amended_intent_preserves_single_buy_economics");
    auto original=bundle;original["amendment.record"]=original.at("opening.record");
    rejected([&]{(void)build_intent(original,lower);},"amendment record refused","wrong_record_kind_refused");
    original=bundle;original["amendment.record"][0]^=1;
    rejected([&]{(void)build_intent(original,lower);},"amendment record refused","corrupt_amendment_refused");
    original=bundle;original["amendment.record"]+='x';
    rejected([&]{(void)build_intent(original,lower);},"amendment record refused","noncanonical_amendment_refused");
    rejected([&]{(void)build_intent(bundle,change(lower,"now_ms","1019"));},"amendment record refused","future_amendment_refused");
    const auto tighter=build_price_amendment(bundle.at("opening.record"),change(parameters,"replacement_price","147537212"));
    original=bundle;original["amendment.record"]=tighter.at("amendment.record");
    rejected([&]{(void)build_intent(original,lower);},"intent reducer or live scope refused","amended_price_one_micro_tighter_refuses");
    swap::Record bad;require(swap::decode(std::as_bytes(std::span(bytes)),bad)==swap::CodecError::none,"bad_amendment_fixture_decodes");
    std::get<swap::AmendBuyPrice>(bad.command).expected_revision=1;original=bundle;original["amendment.record"]=canonical(bad);
    rejected([&]{(void)build_intent(original,lower);},"price amendment reducer refused","intent_rechecks_amendment_revision");
    for(const auto& [key,value]:std::vector<std::pair<std::string,std::string>>{{"expected_revision","1"},{"expected_price","146635155"},{"kill_epoch","2"},{"executor_generation","2"},
        {"replacement_price","0"},{"expected_opening_sha256",to_hex(hash("different-opening"))},{"replacement_price","146635156"}}){
        rejected([&]{(void)build_price_amendment(bundle.at("opening.record"),change(parameters,key,value));},"price amendment reducer refused","amendment_invalid_cas_or_bound_refused");
    }
    rejected([&]{(void)build_price_amendment(bundle.at("opening.record"),change(parameters,"review_evidence_sha256",std::string(64,'0')));},"zero evidence digest","amendment_requires_review_evidence");
    rejected([&]{(void)build_price_amendment(bundle.at("opening.record"),parameters+"max_purchase_atoms=2000000\n");},"unknown amendment parameter","amendment_cannot_change_purchase_scope");
    swap::Record wide;const auto& opening_bytes=bundle.at("opening.record");
    require(swap::decode(std::as_bytes(std::span(opening_bytes)),wide)==swap::CodecError::none,"wide_opening_fixture_decodes");
    std::get<swap::Initialize>(wide.command).limits.max_purchase_total=swap::QuoteAtoms{2000000};
    rejected([&]{(void)build_price_amendment(canonical(wide),parameters);},"amendment requires existing single-buy scope","amendment_builder_preserves_existing_single_buy_scope");
    const auto input=root/"amended-bundle";emit(input,bundle);const auto intent_parameters=root/"intent.parameters";write_new(intent_parameters,lower);
    const auto intent_output=root/"amended-intent";
    try{require(command({"prepare-swap-mvp","intent","--bundle",input.string(),"--parameters",intent_parameters.string(),"--output",intent_output.string()})==0 &&
        read(intent_output/"intent.record",512)==intent.at("intent.record"),"intent_cli_loads_optional_amendment");}
    catch(const std::exception&){throw std::runtime_error("intent_cli_loads_optional_amendment");}
    fs::remove(input/"amendment.record");fs::create_symlink(root/"missing-amendment",input/"amendment.record");
    rejected([&]{(void)command({"prepare-swap-mvp","intent","--bundle",input.string(),"--parameters",intent_parameters.string(),"--output",(root/"must-not-exist").string()});},
        "input unavailable or symlink","dangling_optional_amendment_must_not_be_ignored");
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
        valid();std::cout<<"PASS valid\n";parser();std::cout<<"PASS parser\n";scope();std::cout<<"PASS scope\n";amendment(root);std::cout<<"PASS amendment\n";files(root);std::cout<<"PASS files\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
