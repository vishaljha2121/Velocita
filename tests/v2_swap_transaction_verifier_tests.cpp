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
static_assert(!std::is_default_constructible_v<VerifiedSwapTransaction>);
static_assert(!std::is_aggregate_v<VerifiedSwapTransaction>);
}
int main(int argc,char** argv){
    unsigned count{},failed{};
    const std::pair<const char*,void(*)()> tests[]={{"oracle",oracle},{"economics",economic_refusals},
        {"topology",topology_refusals},{"identity",freshness_and_identity},{"digests",digests},{"byreal",byreal}};
    for(auto [name,fn]:tests){if(argc==2 && std::string_view(argv[1])!=name)continue;++count;
        try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return count==0?2:failed?1:0;
}
