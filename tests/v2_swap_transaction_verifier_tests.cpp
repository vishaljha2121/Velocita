#include "v2_swap_transaction_verifier_test_support.hpp"
#include "velocita/v2/digest.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace velocita::v2;
using namespace velocita::v2::truemarkets;
namespace {
using Code=SwapTransactionVerificationCode;
using test_support::require;
using test_support::key;
using Fixture=test_support::RouteFixture;
void refusal(Fixture f,Code expected,const char* name){const auto r=f.run();require(r.code==expected && !r.verified,name);}
void oracle(){
    Fixture f;const auto r=f.run();require(r.code==Code::verified_under_program_contract && r.verified.has_value(),"supported_buy_verified");
    require(r.bounds.total_usdc_debit==1'000'000 && r.bounds.explicit_usdc_fee==2'000,"inclusive_debit_oracle");
    require(r.bounds.gross_sol_floor==9'363'569 && r.bounds.maximum_sol_deduction==2'039'280 &&
        r.bounds.net_sol_floor==7'324'289,"net_floor_rounds_down");
    require(r.verified->payload_digest()==f.pending.payload && r.verified->intent_digest()==verified_swap_intent_digest(f.pending.intent) &&
        r.verified->policy_digest()==swap_execution_policy_digest(f.policy) &&
        r.verified->lookup_digest()==jupiter_lookup_prefix_digest(f.prefix()) && r.verified->expires_at_ms()==1100,"proof_exact_bindings");
    f.pending.intent.min_base_receive=swap::BaseAtoms{7'324'289};require(f.run().verified.has_value(),"minimum_equal_to_floor_verified");
    Fixture shorter;shorter.policy.max_envelope_age_ms=80;const auto early=shorter.run();
    require(early.verified && early.verified->expires_at_ms()==1080,"expiry_is_earliest_of_intent_and_envelope_age");
    f.pending.intent.min_base_receive=swap::BaseAtoms{7'324'290};refusal(f,Code::minimum_shortfall,"rounded_up_floor_refused");
}
Fixture sell(){Fixture f;f.make_sell();return f;}
void sell_oracle(){
    auto f=sell();const auto r=f.run();require(r.code==Code::verified_under_program_contract && r.verified.has_value(),"supported_sell_verified");
    require(r.bounds.total_usdc_debit==0 && r.bounds.explicit_usdc_fee==25'051 && r.bounds.gross_sol_floor==0 &&
        r.bounds.maximum_sol_deduction==0 && r.bounds.net_sol_floor==0 && r.bounds.base_input==120'000'000 &&
        r.bounds.usdc_floor==12'437'500,"sell_bounds_oracle");
    require(r.verified->intent_digest()==verified_swap_intent_digest(f.pending.intent) && r.verified->expires_at_ms()==1100,"sell_proof_bindings");
    f.pending.intent.min_quote_receive=swap::QuoteAtoms{12'437'501};refusal(f,Code::minimum_shortfall,"sell_floor_shortfall_refused");
    f=sell();f.pending.intent.fee_cap=swap::QuoteAtoms{25'050};refusal(f,Code::debit_mismatch,"sell_fee_above_cap_refused");
    f=sell();f.with_price=false;require(f.run().verified.has_value(),"sell_without_price_verified");
}
void sell_refusals(){
    auto f=sell();f.wrap=119'999'999;refusal(f,Code::unsupported_topology,"wrap_differs_from_base_input_refused");
    f=sell();f.wrap_accounts={0,3};refusal(f,Code::unsupported_topology,"sponsor_funded_wrap_refused");
    f=sell();f.wrap_accounts={1,2};refusal(f,Code::unsupported_topology,"wrap_into_usdc_refused");
    f=sell();f.sync_accounts={2};refusal(f,Code::unsupported_topology,"sync_of_other_account_refused");
    f=sell();f.close_accounts={3,1,1};refusal(f,Code::unsupported_topology,"sell_rent_must_return_to_sponsor");
    f=sell();f.close_accounts={3,0,0};refusal(f,Code::unsupported_topology,"sell_close_authority_refused");
    f=sell();f.route_fixed={8,1,2,3,7,16,4,10,7};refusal(f,Code::unsupported_topology,"buy_oriented_route_refused");
    f=sell();f.route_fixed={8,1,3,2,7,14,4,10,7};refusal(f,Code::unsupported_topology,"sell_into_sol_mint_refused");
    f=sell();f.input=119'000'000;refusal(f,Code::debit_mismatch,"route_input_differs_from_base_input_refused");
    f=sell();f.fee=0;refusal(f,Code::debit_mismatch,"sell_platform_fee_refused");
    f=sell();f.slippage=501;refusal(f,Code::unsupported_route,"sell_slippage_above_policy_refused");
    f=sell();f.extra=1;refusal(f,Code::unsupported_topology,"sell_extra_instruction_refused");
    f=sell();f.program_order={6,6,9,8,5,7,8};refusal(f,Code::unsupported_topology,"sync_before_wrap_refused");
    f=sell();f.units=1'400'001;refusal(f,Code::invalid_instruction,"sell_compute_units_above_policy_refused");
    f=sell();f.price=71'429;refusal(f,Code::invalid_instruction,"sell_compute_price_above_policy_refused");
    f=sell();f.program_order={6,6,9,8,8,7,8};refusal(f,Code::unsupported_topology,"sell_wrap_slot_program_refused");
    f=sell();f.pending.intent.quote_input=swap::QuoteAtoms{1};refusal(f,Code::invalid_intent,"sell_with_quote_input_refused");
    f=sell();f.pending.intent.min_quote_receive={};refusal(f,Code::invalid_intent,"sell_without_floor_refused");
    f=sell();f.pending.intent.side=Side::buy;refusal(f,Code::invalid_intent,"sell_envelope_for_buy_intent_refused");
    Fixture buy;buy.pending.intent=sell().pending.intent;refusal(buy,Code::unsupported_topology,"buy_envelope_for_sell_intent_refused");
}
void variants(){
    for(std::uint8_t variant:{18,109,156,177,181}){Fixture f;f.variant=variant;f.direction=0;
        require(f.run().verified.has_value(),"reviewed_variant_verified");}
    Fixture f;f.variant=26;refusal(f,Code::unsupported_route,"decodable_unreviewed_variant_refused");
    f=Fixture{};f.variant=38;refusal(f,Code::unsupported_route,"undecodable_variant_refused");
    f=Fixture{};f.variant=177;f.route_tail[0]=11;refusal(f,Code::unsupported_route,"variant_program_absent_refused");
    f=Fixture{};f.percent=99;refusal(f,Code::unsupported_route,"partial_route_refused");
    f=Fixture{};f.input_index=1;refusal(f,Code::unsupported_route,"route_input_index_refused");
    f=Fixture{};f.output_index=2;refusal(f,Code::unsupported_route,"route_output_index_refused");
    f=Fixture{};f.slippage=501;refusal(f,Code::unsupported_route,"slippage_above_policy_refused");
}
void pools(){
    Fixture first(0),second(1);second.variant=18;
    const auto a=first.run(),b=second.run();
    require(a.verified && b.verified,"two_pools_verified");
    require(swap_execution_policy_digest(first.policy)==swap_execution_policy_digest(second.policy) &&
        a.verified->policy_digest()==b.verified->policy_digest(),"one_stable_policy_for_both_pools");
    require(a.verified->lookup_digest()!=b.verified->lookup_digest() && a.verified->payload_digest()!=b.verified->payload_digest(),
        "attempt_evidence_differs");
}
void roles(){
    Fixture f;f.policy.sponsor=key(90);refusal(f,Code::account_mismatch,"sponsor_changed_refused");
    f=Fixture{};f.policy.owner_usdc=key(90);refusal(f,Code::account_mismatch,"owner_usdc_absent_refused");
    f=Fixture{};f.policy.platform_fee=key(90);refusal(f,Code::account_mismatch,"fee_account_absent_refused");
    f=Fixture{};f.policy.owner_wsol=f.table_entries[4];refusal(f,Code::account_mismatch,"owner_account_from_lookup_refused");
    f=Fixture{};f.readonly_signed=1;refusal(f,Code::account_mismatch,"owner_must_be_writable");
    f=Fixture{};f.readonly_unsigned=7;refusal(f,Code::account_mismatch,"fee_account_must_be_writable");
    f=Fixture{};f.required_signatures=3;refusal(f,Code::account_mismatch,"extra_signer_refused");
    for(std::size_t i=0;i<f.route_fixed.size();++i){f=Fixture{};f.route_fixed[i]=f.route_fixed[i]==11?12:11;
        refusal(f,Code::unsupported_topology,"every_jupiter_role_pinned");}
}
void instructions(){
    Fixture f;f.with_price=false;require(f.run().verified.has_value(),"price_instruction_optional");
    f=Fixture{};f.extra=1;refusal(f,Code::unsupported_topology,"extra_instruction_refused");
    f=Fixture{};f.with_price=false;f.extra=2;refusal(f,Code::unsupported_topology,"trailing_instructions_without_price_refused");
    f=Fixture{};f.program_order[1]=5;refusal(f,Code::unsupported_topology,"non_compute_price_slot_refused");
    f=Fixture{};f.program_order[2]=8;refusal(f,Code::unsupported_topology,"ata_slot_program_refused");
    f=Fixture{};f.program_order[4]=9;refusal(f,Code::unsupported_topology,"close_slot_program_refused");
    f=Fixture{};f.ata_accounts[0]=1;refusal(f,Code::unsupported_topology,"owner_funded_ata_refused");
    f=Fixture{};f.close_accounts[2]=0;refusal(f,Code::unsupported_topology,"close_authority_refused");
    f=Fixture{};f.pay_accounts={0,0};refusal(f,Code::unsupported_topology,"repayment_source_refused");
    f=Fixture{};f.route_tail.clear();refusal(f,Code::unsupported_topology,"jupiter_without_route_accounts_refused");
    f=Fixture{};f.program_order={6,6,9,8,7,5};refusal(f,Code::unsupported_topology,"reordered_instructions_refused");
    f=Fixture{};f.ata_accounts[2]=0;refusal(f,Code::unsupported_topology,"ata_for_other_wallet_refused");
    f=Fixture{};f.ata_accounts[3]=11;refusal(f,Code::unsupported_topology,"ata_for_other_mint_refused");
    f=Fixture{};f.ata_accounts[1]=2;refusal(f,Code::unsupported_topology,"ata_for_source_refused");
    f=Fixture{};f.close_accounts[1]=0;refusal(f,Code::unsupported_topology,"rent_recipient_refused");
    f=Fixture{};f.close_accounts[0]=2;refusal(f,Code::unsupported_topology,"close_source_refused");
    f=Fixture{};f.pay_accounts={0,1};refusal(f,Code::unsupported_topology,"repayment_direction_refused");
    f=Fixture{};f.pay_accounts={1,4};refusal(f,Code::unsupported_topology,"repayment_recipient_refused");
    f=Fixture{};f.units=1'400'001;f.policy.max_compute_units=1'400'000;refusal(f,Code::invalid_instruction,"compute_units_capped");
    f=Fixture{};f.price=71'429;refusal(f,Code::invalid_instruction,"compute_price_capped");
}
void lookups(){
    Fixture f;f.table=key(250);auto e=f.envelope();f.pending.payload=sha256_digest(std::as_bytes(std::span(e)));
    auto prefix=f.prefix();prefix.table=key(251);
    require(verify_swap_transaction(e,"verifier-fixture",std::string(31,'1')+'3',f.pending,f.policy,prefix,1001).code==Code::invalid_accounts,
        "lookup_table_mismatch_refused");
    f=Fixture{};f.prefix_count=9;refusal(f,Code::invalid_accounts,"short_lookup_prefix_refused");
    f=Fixture{};f.table_entries[2]=f.policy.owner_usdc;refusal(f,Code::invalid_accounts,"lookup_alias_of_owner_account_refused");
    f=Fixture{};e=f.envelope();f.pending.payload=sha256_digest(std::as_bytes(std::span(e)));prefix=f.prefix();
    prefix.addresses[7]=test_support::dex_program(177);
    require(verify_swap_transaction(e,"verifier-fixture",std::string(31,'1')+'3',f.pending,f.policy,prefix,1001).code==Code::unsupported_route,
        "substituted_lookup_program_refused");
}
void economics(){
    Fixture f;f.payment=2'039'281;refusal(f,Code::deduction_exceeded,"one_lamport_deduction_excess");
    f=Fixture{};f.input=1'010'000;refusal(f,Code::debit_mismatch,"larger_debit_refused");
    f=Fixture{};f.fee=21;refusal(f,Code::debit_mismatch,"different_fee_refused");
    f=Fixture{};f.pending.intent.quote_input=swap::QuoteAtoms{997'999};refusal(f,Code::debit_mismatch,"principal_mismatch_refused");
    f=Fixture{};f.input=999'999;f.pending.intent.max_quote_debit=swap::QuoteAtoms{999'999};
    refusal(f,Code::debit_mismatch,"fractional_fee_refused");
    f=Fixture{};f.quoted=1;refusal(f,Code::minimum_shortfall,"receipt_underflow_refused");
}
void freshness(){
    Fixture f;f.timestamp=900;refusal(f,Code::invalid_envelope,"stale_envelope_refused");
    f=Fixture{};f.timestamp=1002;refusal(f,Code::invalid_envelope,"future_envelope_refused");
    f=Fixture{};f.signed_payload=true;refusal(f,Code::invalid_envelope,"already_signed_refused");
    f=Fixture{};f.pending.intent.sign_by=1001;refusal(f,Code::invalid_intent,"expired_intent_refused");
    f=Fixture{};f.pending.intent.binding.policy=PolicyId{88};refusal(f,Code::invalid_intent,"intent_binding_changed");
    f=Fixture{};const auto e=f.envelope();f.pending.payload.bytes[0]=std::byte{1};
    require(verify_swap_transaction(e,"verifier-fixture",std::string(31,'1')+'3',f.pending,f.policy,f.prefix(),1001).code==Code::payload_mismatch,
        "prepared_payload_mismatch");
}
void policy(){
    Fixture f;f.policy.owner_wsol=f.policy.owner_usdc;refusal(f,Code::invalid_policy,"duplicate_policy_key_refused");
    f=Fixture{};f.policy.platform_fee={};refusal(f,Code::invalid_policy,"zero_policy_key_refused");
    f=Fixture{};f.policy.max_envelope_age_ms=30'001;refusal(f,Code::invalid_policy,"envelope_age_cap");
    f=Fixture{};f.policy.binding.catalog={};refusal(f,Code::invalid_policy,"invalid_binding_refused");
    const auto base=swap_execution_policy_digest(Fixture{}.policy);require(!base.is_zero(),"policy_digest_nonzero");
    const std::vector<void(*)(SwapExecutionPolicy&)> changes{
        [](SwapExecutionPolicy& p){p.sponsor=key(91);},[](SwapExecutionPolicy& p){p.owner=key(91);},
        [](SwapExecutionPolicy& p){p.owner_usdc=key(91);},[](SwapExecutionPolicy& p){p.owner_wsol=key(91);},
        [](SwapExecutionPolicy& p){p.platform_fee=key(91);},[](SwapExecutionPolicy& p){p.max_envelope_age_ms=99;},
        [](SwapExecutionPolicy& p){p.max_compute_price=1;},[](SwapExecutionPolicy& p){p.max_compute_units=1;},
        [](SwapExecutionPolicy& p){p.max_slippage_bps=1;},[](SwapExecutionPolicy& p){p.accepted_costs.max_output_deduction=swap::BaseAtoms{1};},
        [](SwapExecutionPolicy& p){p.binding.policy_version=PolicyVersion{2};}};
    for(const auto change:changes){auto p=Fixture{}.policy;change(p);require(swap_execution_policy_digest(p)!=base,"every_policy_field_in_digest");}
    auto intent=Fixture{}.pending.intent;const auto i=verified_swap_intent_digest(intent);
    intent.min_base_receive=swap::BaseAtoms{1};require(verified_swap_intent_digest(intent)!=i,"intent_minimum_digest");
}
std::vector<std::uint8_t> table_account(const std::vector<SolanaAccountKey>& entries){
    std::vector<std::uint8_t> d{1,0,0,0};d.insert(d.end(),8,255);test_support::le(d,123,8);d.push_back(0);d.push_back(1);
    d.insert(d.end(),test_support::kLookupAuthority.begin(),test_support::kLookupAuthority.end());d.insert(d.end(),2,0);
    for(const auto& k:entries)d.insert(d.end(),k.begin(),k.end());
    return d;
}
void lookup_reading(){
    Fixture f;const auto e=f.envelope();
    const auto need=jupiter_lookup_requirement(e,"verifier-fixture",std::string(31,'1')+'3');
    require(need && need->table==f.table && need->count==10,"requirement_is_highest_index_plus_one");
    require(!jupiter_lookup_requirement(e,"other",std::string(31,'1')+'3'),"requirement_checks_envelope_identity");
    auto entries=f.table_entries;entries.push_back(key(999));
    auto data=table_account(entries);const auto prefix=jupiter_lookup_prefix_from_account(*need,data);
    require(prefix && prefix->table==f.table && prefix->count==10 &&
        std::equal(prefix->addresses.begin(),prefix->addresses.begin()+10,entries.begin()),"prefix_is_required_entries_only");
    const auto refused=[&](auto mutate,const char* name){auto d=table_account(entries);mutate(d);
        require(!jupiter_lookup_prefix_from_account(*need,d),name);};
    refused([](auto& d){d[0]=2;},"wrong_account_type_refused");
    refused([](auto& d){d[4]=0;},"deactivated_table_refused");
    refused([](auto& d){d[21]=0;},"table_without_authority_refused");
    refused([](auto& d){d[22]^=1;},"other_authority_refused");
    refused([](auto& d){d[54]=1;},"nonzero_padding_refused");
    refused([](auto& d){d.pop_back();},"partial_entry_refused");
    refused([](auto& d){d.resize(56+9*32);},"too_few_entries_refused");
    JupiterLookupRequirement none{f.table,0};require(!jupiter_lookup_prefix_from_account(none,data),"empty_requirement_refused");
    const auto a=jupiter_lookup_prefix_digest(*prefix);auto other=*prefix;other.addresses[3]=key(998);
    require(a!=jupiter_lookup_prefix_digest(other),"lookup_digest_covers_entries");
    other=*prefix;other.table=key(997);require(a!=jupiter_lookup_prefix_digest(other),"lookup_digest_covers_table");
    const auto evidence=jupiter_lookup_evidence(*prefix);
    require(evidence.table==prefix->table && evidence.address_count==10 && !evidence.evidence_digest.is_zero() && evidence.observed_slot,
        "evidence_for_resolver");
}
static_assert(!std::is_default_constructible_v<VerifiedSwapTransaction>);
static_assert(!std::is_aggregate_v<VerifiedSwapTransaction>);
}
int main(int argc,char** argv){
    unsigned count{},failed{};
    const std::pair<const char*,void(*)()> tests[]={{"oracle",oracle},{"sell_oracle",sell_oracle},{"sell_refusals",sell_refusals},{"variants",variants},{"pools",pools},{"roles",roles},
        {"instructions",instructions},{"lookups",lookups},{"economics",economics},{"freshness",freshness},{"policy",policy},
        {"lookup_reading",lookup_reading}};
    for(auto [name,fn]:tests){if(argc==2 && std::string_view(argv[1])!=name)continue;++count;
        try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return count==0?2:failed?1:0;
}
