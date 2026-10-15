#include "velocita/v2/swap_transaction_review.hpp"
#include "v2_swap_transaction_verifier_test_support.hpp"
#include <iostream>
#include <limits>

using namespace velocita::v2;
namespace {
using test_support::require;
Digest256 hash(std::string_view s){return sha256_digest(std::as_bytes(std::span(s)));}
std::string hex(std::span<const std::uint8_t> bytes){constexpr char chars[]="0123456789abcdef";std::string s;for(auto b:bytes){s+=chars[b>>4];s+=chars[b&15];}return s;}
struct Fixture {
    test_support::RouteFixture tx;
    SwapRuntimeBinding binding{};
    std::string document;
    Fixture(){
        binding.allocation=tx.policy.binding;binding.max_buy_output_deduction_lamports=2'039'280;
        std::transform(tx.policy.owner.begin(),tx.policy.owner.end(),binding.owner_wallet.begin(),[](auto b){return static_cast<std::byte>(b);});
        document="schema=6\nsponsor="+hex(tx.policy.sponsor)+"\nowner_usdc="+hex(tx.policy.owner_usdc)+"\nowner_wsol="+hex(tx.policy.owner_wsol)+
            "\nplatform_fee="+hex(tx.policy.platform_fee)+"\nmax_envelope_age_ms=100\nmax_compute_price=71428\nmax_compute_units=1400000\nmax_slippage_bps=500\n";
    }
    void set(std::string_view field,std::string_view value){auto pos=document.find(std::string(field)+'=');require(pos!=std::string::npos,"test_field_present");pos+=field.size()+1;const auto end=document.find('\n',pos);document.replace(pos,end-pos,value);}
    auto read()const{return decode_swap_transaction_review(document,hash(document),binding,"verifier-fixture",std::string(31,'1')+'3');}
};
void valid(){
    Fixture f;const auto r=f.read();require(r.has_value(),"valid_review_decodes");
    require(truemarkets::swap_execution_policy_digest(r->policy)==truemarkets::swap_execution_policy_digest(f.tx.policy),
        "review_equals_stable_policy");
    require(r->policy.owner==f.tx.policy.owner && r->policy.binding==f.binding.allocation &&
        r->policy.accepted_costs.max_output_deduction.value()==2'039'280,"review_uses_runtime_owner_allocation_and_cost");
    require(r->organization.view()=="verifier-fixture" && r->signer.view()==std::string(31,'1')+'3',"review_runtime_tm_identity");
    require(to_hex(swap_signer_verification_digest(*r))=="2c1f489a77fe5f13e9b5a5a28d455f28ea96c4d17111e35559e174950462c070",
        "verification_digest_python_parity_golden");
}
void identities(){
    Fixture f;require(!decode_swap_transaction_review(f.document,Digest256{},f.binding,"verifier-fixture",std::string(31,'1')+'3'),"zero_expected_digest_refused");
    auto expected=hash(f.document);f.set("max_slippage_bps","400");require(!decode_swap_transaction_review(f.document,expected,f.binding,"verifier-fixture",std::string(31,'1')+'3'),"raw_bytes_digest_refused");
    f=Fixture{};f.binding.max_buy_output_deduction_lamports=std::numeric_limits<std::uint64_t>::max();require(!f.read(),"runtime_cost_overflow_refused");
    f=Fixture{};f.binding.allocation.catalog={};require(!f.read(),"invalid_runtime_allocation_refused");
    f=Fixture{};require(!decode_swap_transaction_review(f.document,hash(f.document),f.binding,"bad org",std::string(31,'1')+'3'),"unsafe_runtime_text_refused");
    f=Fixture{};f.document+="owner=00\n";require(!f.read(),"document_cannot_override_runtime_owner");
    f=Fixture{};f.set("owner_usdc",hex(f.tx.policy.owner));require(!f.read(),"owner_alias_refused");
    Fixture a,b;b.binding.owner_wallet[0]^=std::byte{1};
    require(a.read() && b.read() && swap_signer_verification_digest(*a.read())!=swap_signer_verification_digest(*b.read()),
        "verification_digest_covers_owner");
    const auto one=swap_signer_verification_digest(*a.read());auto other=*a.read();other.organization=*truemarkets::tm_text("other-org");
    require(swap_signer_verification_digest(other)!=one,"verification_digest_covers_organization");
}
void schema(){
    for(const auto suffix:{"schema=6\n","unknown=1\n","\n","lookup_table=00\n","account_keys=00\n","expires_at_ms=3\n"}){
        Fixture f;f.document+=suffix;require(!f.read(),"duplicate_unknown_blank_or_removed_field_refused");}
    for(const auto old:{"1","2","3","4","5","7","06"}){Fixture f;f.set("schema",old);require(!f.read(),"only_schema6_accepted");}
    Fixture f;f.document.erase(0,9);require(!f.read(),"missing_field_refused");
    f=Fixture{};f.document[0]='S';require(!f.read(),"case_changed_field_refused");
    f=Fixture{};f.set("sponsor",std::string(64,'G'));require(!f.read(),"nonhex_key_refused");
    f=Fixture{};f.set("platform_fee",std::string(62,'0'));require(!f.read(),"short_key_refused");
    f=Fixture{};f.set("owner_wsol",std::string(64,'0'));require(!f.read(),"zero_key_refused");
    f=Fixture{};f.document.resize(8193,'x');require(!f.read(),"oversize_document_refused");
    f=Fixture{};f.set("max_slippage_bps","500\r");require(!f.read(),"carriage_return_refused");
    f=Fixture{};f.set("max_compute_price","");require(!f.read(),"empty_value_refused");
}
void bounds(){
    const std::pair<std::string_view,std::string_view> bad[]={
        {"max_envelope_age_ms","0"},{"max_envelope_age_ms","30001"},{"max_compute_units","0"},
        {"max_compute_units","1400001"},{"max_compute_units","4294967297"},{"max_slippage_bps","10001"},
        {"max_slippage_bps","65536"},{"max_compute_price","18446744073709551615"},
        {"max_compute_price","-1"},{"max_compute_price","+1"},{"max_compute_price","1junk"}};
    for(auto [field,value]:bad){Fixture f;f.set(field,value);require(!f.read(),"numeric_bound_refused");}
}
}
void live_scope(){
    Fixture f;const auto r=f.read();require(r.has_value(),"scope_review_decodes");
    swap::State s;s.initialized=true;s.opening.binding=f.binding.allocation;auto& l=s.opening.limits;
    l.max_purchase_total=swap::QuoteAtoms{3'000'000'000};l.max_buy_debit=swap::QuoteAtoms{25'000'000};l.max_fee=swap::QuoteAtoms{50'000};
    l.max_realized_loss=swap::QuoteAtoms{10'000'000};l.max_sell_input=swap::BaseAtoms{250'000'000};
    auto& i=s.pending.intent;s.pending.phase=swap::Phase::reserved;i.effect=EffectId{1};i.side=Side::buy;s.last_effect=1;
    require(swap_live_scope(s,*r),"buy_at_every_ceiling_in_scope");
    auto sell=s;sell.pending.intent.side=Side::sell;sell.pending.intent.effect=EffectId{240};sell.last_effect=240;sell.settlement_count=16;sell.settled_total=239;
    require(swap_live_scope(sell,*r),"sell_at_lifetime_order_limit_in_scope");
    auto beyond=sell;beyond.pending.intent.effect=EffectId{241};beyond.last_effect=241;require(!swap_live_scope(beyond,*r),"order_241_out_of_scope");
    auto idle=s;idle.pending={};idle.last_effect=240;require(swap_live_scope(idle,*r),"idle_at_order_limit_in_scope");
    idle.last_effect=241;require(!swap_live_scope(idle,*r),"idle_past_order_limit_out_of_scope");
    for(unsigned n=0;n<5;++n){auto wide=s;auto& w=wide.opening.limits;
        if(n==0)w.max_realized_loss=swap::QuoteAtoms{10'000'001};
        if(n==1)w.max_buy_debit=swap::QuoteAtoms{25'000'001};
        if(n==2)w.max_fee=swap::QuoteAtoms{50'001};
        if(n==3)w.max_sell_input=swap::BaseAtoms{250'000'001};
        if(n==4)w.max_purchase_total=swap::QuoteAtoms{3'000'000'001};
        require(!swap_live_scope(wide,*r),"opening_wider_than_owner_envelope_out_of_scope");}
    auto zero=s;zero.opening.limits.max_realized_loss={};require(!swap_live_scope(zero,*r),"opening_without_loss_limit_out_of_scope");
    auto conflicted=s;conflicted.conflicted=true;require(!swap_live_scope(conflicted,*r),"conflicted_state_out_of_scope");
    auto foreign=s;foreign.opening.binding.account=AccountId{99};require(!swap_live_scope(foreign,*r),"foreign_binding_out_of_scope");
    auto empty=s;empty.initialized=false;require(!swap_live_scope(empty,*r),"uninitialized_out_of_scope");
    auto transient=s;transient.pending.intent.transient_base_hold=swap::BaseAtoms{1};require(!swap_live_scope(transient,*r),"transient_hold_out_of_scope");
    auto costly=*r;costly.policy.accepted_costs.max_output_deduction=swap::BaseAtoms{2'039'281};require(!swap_live_scope(s,costly),"repayment_allowance_above_cap_out_of_scope");
}
int main(int argc,char** argv){const std::pair<const char*,void(*)()> tests[]={{"valid",valid},{"live_scope",live_scope},{"identities",identities},{"schema",schema},{"bounds",bounds}};
    unsigned ran{},failed{};for(auto [name,fn]:tests){if(argc==2 && std::string_view(argv[1])!=name)continue;++ran;
        try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return ran&&!failed?0:1;
}
