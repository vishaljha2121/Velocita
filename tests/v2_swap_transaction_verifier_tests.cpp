#include "v2_swap_transaction_verifier_test_support.hpp"
#include "velocita/v2/solana_instruction_decode.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace velocita::v2;
using namespace velocita::v2::truemarkets;
namespace {
using Code=SwapTransactionVerificationCode;
void require(bool ok,const char* name){if(!ok)throw std::runtime_error(name);}
using Fixture=velocita::v2::test_support::DirectBuyFixture;
using velocita::v2::test_support::key;
void refusal(Fixture f,Code expected,const char* name){const auto r=f.run();require(r.code==expected && !r.verified,name);}
void oracle(){
    Fixture f;const auto r=f.run();require(r.code==Code::verified_under_program_contract && r.verified.has_value(),"supported_buy_verified");
    require(r.bounds.total_usdc_debit==1'000'000 && r.bounds.explicit_usdc_fee==2'000,"inclusive_debit_oracle");
    require(r.bounds.gross_sol_floor==8'940'092 && r.bounds.maximum_sol_deduction==550'840 &&
        r.bounds.net_sol_floor==8'389'252,"net_floor_oracle");
    require(r.verified->payload_digest()==f.pending.payload && r.verified->intent_digest()==verified_swap_intent_digest(f.pending.intent) &&
        r.verified->policy_digest()==direct_buy_policy_digest(f.policy) && r.verified->expires_at_ms()==1100,"proof_exact_bindings");
    auto saved=*r.verified;f.payment=0;require(saved.bounds().maximum_sol_deduction==550'840,"proof_owns_data");
}
void economic_refusals(){
    Fixture f;f.payment=2'039'280;f.pending.intent.min_base_receive=swap::BaseAtoms{6'000'000};refusal(f,Code::deduction_exceeded,"idempotent_zero_funding_refused");
    f=Fixture{};f.payment=550'841;refusal(f,Code::deduction_exceeded,"one_lamport_cap_excess");
    f=Fixture{};f.pending.intent.min_base_receive=swap::BaseAtoms{8'389'253};refusal(f,Code::minimum_shortfall,"one_lamport_receipt_shortfall");
    f=Fixture{};f.policy.accepted_costs.max_output_deduction=swap::BaseAtoms{2'039'280};
    f.payment=2'039'280;f.pending.intent.min_base_receive=swap::BaseAtoms{6'900'812};
    const auto larger=f.run();require(larger.verified && larger.bounds.net_sol_floor==6'900'812,
        "separate_larger_policy_exact_bound");
    f.pending.intent.min_base_receive=swap::BaseAtoms{6'900'813};refusal(f,Code::minimum_shortfall,"larger_policy_still_requires_minimum");
    f=Fixture{};f.input=1'010'000;refusal(f,Code::debit_mismatch,"larger_debit_refused");
    f=Fixture{};f.fee=21;refusal(f,Code::debit_mismatch,"different_fee_refused");
    f=Fixture{};f.pending.intent.quote_input=swap::QuoteAtoms{997'999};refusal(f,Code::debit_mismatch,"principal_mismatch_refused");
    f=Fixture{};f.input=999'999;f.pending.intent.max_quote_debit=swap::QuoteAtoms{999'999};
    refusal(f,Code::debit_mismatch,"fractional_fee_below_divisibility_refused");
    f=Fixture{};f.input=1'000'001;f.pending.intent.max_quote_debit=swap::QuoteAtoms{1'000'001};f.pending.intent.quote_input=swap::QuoteAtoms{998'001};
    refusal(f,Code::debit_mismatch,"fractional_fee_above_divisibility_refused");
    f=Fixture{};f.quoted=1;refusal(f,Code::minimum_shortfall,"receipt_underflow_refused");
}
void topology_refusals(){
    Fixture f;f.extra=true;refusal(f,Code::unsupported_topology,"extra_instruction_refused");
    f=Fixture{};f.wrong_close=true;refusal(f,Code::unsupported_topology,"rent_recipient_refused");
    f=Fixture{};f.wrong_route_account=true;refusal(f,Code::unsupported_topology,"route_account_role_refused");
    f=Fixture{};f.route_kind=177;refusal(f,Code::unsupported_route,"other_dex_refused");
    f=Fixture{};f.route_direction=1;refusal(f,Code::unsupported_route,"reverse_direction_refused");
    f=Fixture{};f.route_percent=99;refusal(f,Code::unsupported_route,"partial_route_refused");
    f=Fixture{};f.policy.account_keys[17]=key(200);f.lookup[0].addresses[5]=key(200);
    f.policy.lookup_image_digest=direct_buy_lookup_digest(f.lookup);refusal(f,Code::invalid_policy,"arbitrary_dex_policy_refused");
}
void freshness_and_identity(){
    Fixture f;f.timestamp=900;refusal(f,Code::invalid_envelope,"stale_envelope_refused");
    f=Fixture{};f.timestamp=1002;refusal(f,Code::invalid_envelope,"future_envelope_refused");
    f=Fixture{};f.signed_payload=true;refusal(f,Code::invalid_envelope,"already_signed_refused");
    f=Fixture{};f.pending.intent.sign_by=1001;refusal(f,Code::invalid_intent,"expired_intent_refused");
    f=Fixture{};f.policy.expires_at_ms=1001;refusal(f,Code::invalid_policy,"expired_policy_refused");
    f=Fixture{};f.lookup[0].observed_slot++;refusal(f,Code::invalid_accounts,"lookup_provenance_changed");
    f=Fixture{};f.pending.intent.binding.policy=PolicyId{88};refusal(f,Code::invalid_intent,"intent_binding_changed");
    f=Fixture{};const auto e=f.envelope();f.pending.payload.bytes[0]=std::byte{1};
    require(verify_swap_transaction(e,"verifier-fixture",std::string(31,'1')+'3',f.pending,f.policy,f.lookup,1001).code==Code::payload_mismatch,
        "prepared_payload_mismatch");
}
void digests(){
    Fixture f;const auto p=direct_buy_policy_digest(f.policy),i=verified_swap_intent_digest(f.pending.intent);
    require(!p.is_zero() && !i.is_zero(),"nonzero_digests");
    f.policy.accepted_costs.max_output_deduction=swap::BaseAtoms{550'841};
    require(direct_buy_policy_digest(f.policy)!=p,"policy_cost_bound_digest");
    f=Fixture{};f.policy.account_keys[2]=key(230);require(direct_buy_policy_digest(f.policy)!=p,"policy_owner_account_digest");
    f=Fixture{};f.policy.lookup_image_digest.bytes[1]=std::byte{1};require(direct_buy_policy_digest(f.policy)!=p,"policy_lookup_digest");
    f=Fixture{};f.pending.intent.min_base_receive=swap::BaseAtoms{1};require(verified_swap_intent_digest(f.pending.intent)!=i,"intent_minimum_digest");
    f=Fixture{};f.pending.intent.kill_epoch++;require(verified_swap_intent_digest(f.pending.intent)!=i,"intent_epoch_digest");
}
void byreal(){
    using F=test_support::ByrealBuyFixture;
    F f;auto r=f.run();require(r.verified && r.code==Code::verified_under_program_contract,"byreal_exact_capture_verified");
    // 9,345,460*95/100 = 8,878,187; less full repayment = 6,838,907.
    require(r.bounds.total_usdc_debit==1'000'000 && r.bounds.explicit_usdc_fee==2'000 &&
        r.bounds.gross_sol_floor==8'878'187 && r.bounds.maximum_sol_deduction==2'039'280 && r.bounds.net_sol_floor==6'838'907,"byreal_economic_oracle");
    f.pending.intent.min_base_receive=swap::BaseAtoms{6'838'908};require(!f.run().verified,"byreal_one_lamport_shortfall");
    f=F{};f.payment++;require(!f.run().verified,"byreal_excess_repayment");
    f=F{};f.input+=10000;require(!f.run().verified,"byreal_excess_debit");
    f=F{};f.fee++;require(!f.run().verified,"byreal_wrong_fee");
    f=F{};f.extra=true;require(!f.run().verified,"byreal_extra_instruction");
    f=F{};f.wrong_close=true;require(!f.run().verified,"byreal_wrong_close_recipient");
    f=F{};f.wrong_route_account=true;require(!f.run().verified,"byreal_wrong_route_account");
    f=F{};f.readonly_unsigned=7;require(!f.run().verified,"byreal_permission_escalation");
    f=F{};f.route_kind=109;require(!f.run().verified,"byreal_other_swap_variant");
    f=F{};f.signed_payload=true;require(!f.run().verified,"byreal_already_signed");
    f=F{};f.policy.additional_account_keys[1]=key(231);f.lookup[0].addresses[5]=key(231);
    f.policy.lookup_image_digest=direct_buy_lookup_digest(f.lookup);require(!f.run().verified,"byreal_arbitrary_program");
    f=F{};auto e=f.envelope();f.pending.payload=sha256_digest(std::as_bytes(std::span(e)));auto legacy=Fixture{}.policy;
    require(!verify_swap_transaction(e,"verifier-fixture",std::string(31,'1')+'3',f.pending,legacy,f.lookup,1001).verified,"legacy_policy_cannot_admit_byreal");
    Fixture old;old.policy.additional_account_keys[0]=key(200);require(!old.run().verified,"legacy_unused_keys_refused");
}
void obsidian(){
    using F=test_support::ObsidianBuyFixture;
    for(bool omit:{false,true}) {
        F f;f.omit_price=omit;auto r=f.run();
        require(r.verified.has_value(),"obsidian_both_layouts_verified");
        // 9,298,883 * 95/100 = 8,833,938.85; ceil=8,833,939.
        // Deduct the entire 2,039,280 repayment; sponsor credit is zero.
        require(r.bounds.total_usdc_debit==1'000'000 && r.bounds.explicit_usdc_fee==2'000 &&
            r.bounds.gross_sol_floor==8'833'939 && r.bounds.net_sol_floor==6'794'659,
            "obsidian_independent_economic_oracle");
        f.pending.intent.min_base_receive=swap::BaseAtoms{6'794'660};require(!f.run().verified,"obsidian_one_lamport_shortfall");
    }
    for(bool omit:{false,true}) {
        F f;f.omit_price=omit;f.wrong_close=true;require(!f.run().verified,"obsidian_both_wrong_close");
        f=F{};f.omit_price=omit;f.wrong_route_account=true;require(!f.run().verified,"obsidian_both_wrong_route");
        f=F{};f.omit_price=omit;f.extra=true;require(!f.run().verified,"obsidian_both_extra_instruction");
        f=F{};f.omit_price=omit;f.payment++;require(!f.run().verified,"obsidian_both_excess_repayment");
    }
    F f;f.policy.account_keys[14]=key(230);f.lookup[0].addresses[3]=key(230);
    f.policy.lookup_image_digest=direct_buy_lookup_digest(f.lookup);require(!f.run().verified,"obsidian_arbitrary_sysvar");
    f=F{};f.payment++;require(!f.run().verified,"obsidian_excess_repayment");
    f=F{};f.extra=true;require(!f.run().verified,"obsidian_extra_instruction");
    f=F{};f.wrong_close=true;require(!f.run().verified,"obsidian_wrong_close_recipient");
    f=F{};f.wrong_route_account=true;require(!f.run().verified,"obsidian_wrong_route_account");
    f=F{};f.readonly_unsigned=5;require(!f.run().verified,"obsidian_permission_escalation");
    f=F{};f.route_kind=156;require(!f.run().verified,"obsidian_wrong_dex");
    f=F{};f.policy.account_keys[16]=key(231);f.lookup[0].addresses[5]=key(231);
    f.policy.lookup_image_digest=direct_buy_lookup_digest(f.lookup);require(!f.run().verified,"obsidian_arbitrary_program");
    f=F{};f.policy.account_keys[17]=key(231);require(!f.run().verified,"obsidian_unused_keys_refused");
    f=F{};f.policy.additional_account_keys[0]=key(231);require(!f.run().verified,"obsidian_additional_keys_refused");
    f=F{};f.fee++;require(!f.run().verified,"obsidian_wrong_fee");
    f=F{};f.input+=10000;require(!f.run().verified,"obsidian_excess_debit");
    f=F{};f.policy.max_compute_price=0;require(!f.run().verified,"obsidian_price_cap");
    f.omit_price=true;require(f.run().verified.has_value(),"obsidian_no_price_is_zero");
    f=F{};f.signed_payload=true;require(!f.run().verified,"obsidian_already_signed");
}
void bison(){
    using F=test_support::BisonBuyFixture;
    F f;auto r=f.run();require(r.verified.has_value(),"bison_exact_layout_verified");
    require(r.bounds.total_usdc_debit==1'000'000 && r.bounds.explicit_usdc_fee==2'000 &&
        r.bounds.gross_sol_floor==8'817'231 && r.bounds.maximum_sol_deduction==2'039'280 &&
        r.bounds.net_sol_floor==6'777'951,"bison_independent_economic_oracle");
    f.pending.intent.min_base_receive=swap::BaseAtoms{6'777'952};refusal(f,Code::minimum_shortfall,"bison_one_lamport_shortfall");
    f=F{};f.payment++;refusal(f,Code::deduction_exceeded,"bison_excess_repayment");
    f=F{};f.route_direction=1;refusal(f,Code::unsupported_route,"bison_reverse_direction");
    f=F{};f.route_percent=99;refusal(f,Code::unsupported_route,"bison_partial_route");
    f=F{};f.route_kind=181;refusal(f,Code::unsupported_route,"bison_wrong_variant");
    f=F{};f.wrong_close=true;refusal(f,Code::unsupported_topology,"bison_wrong_close");
    f=F{};f.wrong_route_account=true;refusal(f,Code::unsupported_topology,"bison_wrong_route_account");
    f=F{};f.bison_layout=false;refusal(f,Code::unsupported_topology,"bison_legacy_topology");
    f=F{};f.extra=true;refusal(f,Code::unsupported_topology,"bison_extra_instruction");
    f=F{};f.readonly_unsigned=6;refusal(f,Code::account_mismatch,"bison_permission_escalation");
    f=F{};f.signed_payload=true;refusal(f,Code::invalid_envelope,"bison_already_signed");
    f=F{};f.input+=10000;refusal(f,Code::debit_mismatch,"bison_excess_debit");
    f=F{};f.fee++;refusal(f,Code::debit_mismatch,"bison_wrong_fee");
    for(unsigned index:{9,16,17,19}) {
        f=F{};f.policy.account_keys[index]=key(230);
        if(index>=12)f.lookup[0].addresses[index-12]=key(230);
        f.policy.lookup_image_digest=direct_buy_lookup_digest(f.lookup);
        refusal(f,Code::invalid_policy,"bison_canonical_readonly_and_program");
    }
    f=F{};f.policy.additional_account_keys[0]=key(230);refusal(f,Code::invalid_policy,"bison_unused_keys");
    f=F{};f.policy.route=DirectBuyRoute::kipseli_v3;refusal(f,Code::invalid_policy,"bison_not_legacy_policy");
}

void invariant(){
    using F=test_support::InvariantBuyFixture;
    const auto rejected=[](F f,Code code,const char* name){const auto r=f.run();require(r.code==code && !r.verified,name);};
    F f;auto r=f.run();require(r.code==Code::verified_under_program_contract && r.verified.has_value(),
        "invariant_exact_five_instruction_layout_verified");
    // Hand arithmetic: 9,420,476 * 95 / 100 = 8,949,452.2; ceiling=8,949,453.
    // Deduct all 2,039,280 repayment lamports, without presumed sponsor credit.
    require(r.bounds.total_usdc_debit==1'000'000 && r.bounds.explicit_usdc_fee==2'000 &&
        r.bounds.gross_sol_floor==8'949'453 && r.bounds.maximum_sol_deduction==2'039'280 &&
        r.bounds.net_sol_floor==6'910'173,"invariant_hand_computed_inclusive_fee_and_net_floor");
    require(r.verified->policy_digest()==direct_buy_policy_digest(f.policy) &&
        r.verified->payload_digest()==f.pending.payload &&
        r.verified->intent_digest()==verified_swap_intent_digest(f.pending.intent),"invariant_proof_binds_exact_inputs");
    f.pending.intent.min_base_receive=swap::BaseAtoms{6'910'174};
    rejected(f,Code::minimum_shortfall,"invariant_floor_plus_one_refused");
    f=F{};f.payment++;rejected(f,Code::deduction_exceeded,"invariant_repayment_cap_plus_one");
    f=F{};f.quoted=1;rejected(f,Code::minimum_shortfall,"invariant_floor_cannot_underflow");
    f=F{};f.route_direction=0;rejected(f,Code::unsupported_route,"invariant_reverse_direction_refused");
    f=F{};f.route_direction=2;rejected(f,Code::unsupported_route,"invariant_invalid_direction_refused");
    f=F{};f.route_kind=181;rejected(f,Code::unsupported_route,"invariant_other_variant_refused");
    f=F{};f.route_percent=99;rejected(f,Code::unsupported_route,"invariant_partial_route_refused");
    f=F{};f.input+=10'000;rejected(f,Code::debit_mismatch,"invariant_excess_input_refused");
    f=F{};f.fee++;rejected(f,Code::debit_mismatch,"invariant_fee_changed_refused");
    f=F{};f.pending.intent.quote_input=swap::QuoteAtoms{997'999};
    rejected(f,Code::debit_mismatch,"invariant_principal_not_debit_minus_fee");
    f=F{};f.slippage=501;rejected(f,Code::unsupported_route,"invariant_slippage_cap_refused");
    f=F{};f.include_price=true;rejected(f,Code::unsupported_topology,"invariant_even_zero_compute_price_not_reviewed");
    f=F{};f.extra=true;rejected(f,Code::unsupported_topology,"invariant_extra_instruction_refused");
    f=F{};f.swap_last_instructions=true;rejected(f,Code::unsupported_topology,"invariant_order_changed_refused");
    for(std::size_t i=0;i<f.programs.size();++i){f=F{};f.programs[i]=10;
        rejected(f,Code::unsupported_topology,"invariant_every_instruction_program_pinned");}
    for(std::size_t i=0;i<f.route_accounts.size();++i){f=F{};f.route_accounts[i]=(f.route_accounts[i]+1)%19;
        rejected(f,Code::unsupported_topology,"invariant_every_route_role_pinned");}
    for(std::size_t i=0;i<f.ata_accounts.size();++i){f=F{};f.ata_accounts[i]=(f.ata_accounts[i]+1)%19;
        rejected(f,Code::unsupported_topology,"invariant_every_ata_role_pinned");}
    for(std::size_t i=0;i<f.close_accounts.size();++i){f=F{};f.close_accounts[i]=(f.close_accounts[i]+1)%19;
        rejected(f,Code::unsupported_topology,"invariant_every_close_role_pinned");}
    for(std::size_t i=0;i<f.pay_accounts.size();++i){f=F{};f.pay_accounts[i]=(f.pay_accounts[i]+1)%19;
        rejected(f,Code::unsupported_topology,"invariant_every_repayment_role_pinned");}
    f=F{};f.readonly_unsigned=5;rejected(f,Code::account_mismatch,"invariant_static_permission_escalation");
    f=F{};f.readonly_unsigned=7;rejected(f,Code::account_mismatch,"invariant_static_permission_removed");
    f=F{};f.readonly_signed=1;rejected(f,Code::account_mismatch,"invariant_owner_must_be_writable");
    f=F{};f.required_signatures=3;rejected(f,Code::account_mismatch,"invariant_extra_signer_refused");
    f=F{};f.writable_lookup.push_back(4);f.readonly_lookup.erase(f.readonly_lookup.begin());
    rejected(f,Code::account_mismatch,"invariant_lookup_permission_escalation");
    f=F{};f.readonly_lookup.insert(f.readonly_lookup.begin(),3);f.writable_lookup.pop_back();
    rejected(f,Code::account_mismatch,"invariant_lookup_permission_removed");
    f=F{};f.replace_wire_key=2;rejected(f,Code::account_mismatch,"invariant_source_key_changed_in_wire");
    f=F{};f.signed_payload=true;rejected(f,Code::invalid_envelope,"invariant_already_signed_refused");
    for(unsigned i:{5,6,7,8,9,10,15,17}){f=F{};f.policy.account_keys[i]=key(230);
        if(i>=11)f.lookup[0].addresses[i-11]=key(230);
        f.policy.lookup_image_digest=direct_buy_lookup_digest(f.lookup);
        rejected(f,Code::invalid_policy,"invariant_canonical_programs_and_mint_pinned");}
    f=F{};f.policy.account_keys[19]=key(230);rejected(f,Code::invalid_policy,"invariant_twentieth_key_must_be_zero");
    for(unsigned i=0;i<7;++i){f=F{};f.policy.additional_account_keys[i]=key(230);
        rejected(f,Code::invalid_policy,"invariant_all_additional_keys_must_be_zero");}
    f=F{};f.policy.account_keys[14]=f.policy.account_keys[11];
    rejected(f,Code::invalid_policy,"invariant_duplicate_account_refused");
    f=F{};const auto digest=direct_buy_policy_digest(f.policy);
    require(!digest.is_zero(),"invariant_policy_digest_nonzero");
    f.policy.route=DirectBuyRoute::bison_market_backed;
    require(direct_buy_policy_digest(f.policy)!=digest && !f.run().verified,"invariant_not_bison_policy");
}
static_assert(!std::is_default_constructible_v<VerifiedSwapTransaction>);
static_assert(!std::is_aggregate_v<VerifiedSwapTransaction>);
}
int main(int argc,char** argv){
    unsigned count{},failed{};
    const std::pair<const char*,void(*)()> tests[]={{"oracle",oracle},{"economics",economic_refusals},
        {"topology",topology_refusals},{"identity",freshness_and_identity},{"digests",digests},{"byreal",byreal},{"obsidian",obsidian},{"bison",bison},{"invariant",invariant}};
    for(auto [name,fn]:tests){if(argc==2 && std::string_view(argv[1])!=name)continue;++count;
        try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return count==0?2:failed?1:0;
}
