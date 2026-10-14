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
    test_support::DirectBuyFixture tx;
    SwapRuntimeBinding binding{};
    std::string document;
    Fixture(){
        binding.allocation=tx.policy.binding;binding.max_buy_output_deduction_lamports=550'840;
        std::transform(tx.policy.account_keys[1].begin(),tx.policy.account_keys[1].end(),binding.owner_wallet.begin(),[](auto b){return static_cast<std::byte>(b);});
        document="schema=1\naccount_keys=";
        for(const auto& k:tx.policy.account_keys)document+=hex(k);
        document+="\nlookup_table="+hex(tx.lookup[0].table)+"\nlookup_slot=777\nlookup_evidence="+to_hex(tx.lookup[0].evidence_digest)+
            "\nlookup_address_count=8\nlookup_addresses=";
        for(unsigned i=0;i<8;++i)document+=hex(tx.lookup[0].addresses[i]);
        document+="\nmax_envelope_age_ms=100\nmax_compute_price=71428\nmax_compute_units=1400000\nmax_slippage_bps=500\nexpires_at_ms=2000\n";
    }
    void set(std::string_view field,std::string_view value){auto pos=document.find(std::string(field)+'=');require(pos!=std::string::npos,"test_field_present");pos+=field.size()+1;const auto end=document.find('\n',pos);document.replace(pos,end-pos,value);}
    auto read()const{return decode_swap_transaction_review(document,hash(document),binding,"verifier-fixture",std::string(31,'1')+'3');}
};
void valid(){
    Fixture f;const auto r=f.read();require(r.has_value(),"valid_review_decodes");
    require(r->policy.binding==f.binding.allocation && r->policy.account_keys==f.tx.policy.account_keys &&
        r->policy.accepted_costs.max_output_deduction.value()==550'840,"review_uses_runtime_allocation_and_cost");
    require(r->lookup.table==f.tx.lookup[0].table && r->lookup.address_count==8 &&
        r->policy.lookup_image_digest==truemarkets::direct_buy_lookup_digest(std::span(&r->lookup,1)),"review_lookup_owned_and_bound");
    require(truemarkets::direct_buy_policy_digest(r->policy)==truemarkets::direct_buy_policy_digest(f.tx.policy),"review_canonical_policy_equal");
    require(r->organization.view()=="verifier-fixture" && r->signer.view()==std::string(31,'1')+'3',"review_runtime_tm_identity");
    Fixture maximum;maximum.set("lookup_address_count","256");std::string image;
    for(unsigned n=0;n<256;++n)image+=hex(maximum.tx.lookup[0].addresses[n]);
    maximum.set("lookup_addresses",image);require(maximum.read().has_value(),"maximum_lookup_image_fits");
    const auto saved=r->policy.account_keys;f.document.assign(100,'x');require(r->policy.account_keys==saved,"review_owns_all_fields");
}
void identities(){
    Fixture f;require(!decode_swap_transaction_review(f.document,Digest256{},f.binding,"verifier-fixture",std::string(31,'1')+'3'),"zero_expected_digest_refused");
    auto expected=hash(f.document);f.set("expires_at_ms","3000");require(!decode_swap_transaction_review(f.document,expected,f.binding,"verifier-fixture",std::string(31,'1')+'3'),"raw_bytes_digest_refused");
    f=Fixture{};f.binding.owner_wallet[0]^=std::byte{1};require(!f.read(),"owner_binding_mismatch_refused");
    f=Fixture{};f.binding.max_buy_output_deduction_lamports=std::numeric_limits<std::uint64_t>::max();require(!f.read(),"runtime_cost_overflow_refused");
    f=Fixture{};f.binding.allocation.catalog={};require(!f.read(),"invalid_runtime_allocation_refused");
    f=Fixture{};require(!decode_swap_transaction_review(f.document,hash(f.document),f.binding,"bad org",std::string(31,'1')+'3'),"unsafe_runtime_text_refused");
    f=Fixture{};f.document+="owner_wallet=00\n";require(!f.read(),"document_cannot_override_runtime_owner");
}
void schema(){
    for(const auto suffix:{"schema=1\n","unknown=1\n","\n","expires_at_ms=3\n"}){Fixture f;f.document+=suffix;require(!f.read(),"duplicate_unknown_or_blank_refused");}
    Fixture f;f.set("schema","2");require(!f.read(),"unsupported_schema_refused");
    f=Fixture{};f.document.erase(0,9);require(!f.read(),"missing_field_refused");
    f=Fixture{};f.document[0]='S';require(!f.read(),"case_changed_field_refused");
    f=Fixture{};f.set("lookup_table",std::string(64,'G'));require(!f.read(),"nonhex_key_refused");
    f=Fixture{};f.set("account_keys",std::string(20*64-1,'0'));require(!f.read(),"partial_account_key_refused");
    f=Fixture{};f.document.resize(32769,'x');require(!f.read(),"oversize_document_refused");
    f=Fixture{};f.set("expires_at_ms","2000\r");require(!f.read(),"carriage_return_refused");
    f=Fixture{};f.set("max_compute_price","");require(!f.read(),"empty_value_refused");
}
void byreal_review(){
    Fixture f;test_support::ByrealBuyFixture tx;f.binding.max_buy_output_deduction_lamports=2'039'280;
    std::string keys;for(std::size_t i=0;i<tx.policy.account_count();++i)keys+=hex(tx.policy.account_key(i));
    f.set("schema","2");f.set("account_keys",keys);f.set("lookup_address_count","11");
    std::string lookup;for(unsigned i=0;i<11;++i)lookup+=hex(tx.lookup[0].addresses[i]);f.set("lookup_addresses",lookup);
    auto r=f.read();require(r && r->policy.route==truemarkets::DirectBuyRoute::byreal_dynamic_v3 &&
        r->policy.account_count()==27,"byreal_schema2_decodes");
    require(truemarkets::direct_buy_policy_digest(r->policy)==truemarkets::direct_buy_policy_digest(tx.policy),"byreal_review_digest_matches");
    f.set("schema","1");require(!f.read(),"legacy_schema_rejects_27_accounts");
    f.set("schema","2");f.set("account_keys",keys.substr(0,20*64));require(!f.read(),"byreal_schema_rejects_20_accounts");
    f.set("account_keys",keys);f.set("schema","3");require(!f.read(),"unknown_review_schema_refused");
}

void bounds(){
    const std::pair<std::string_view,std::string_view> bad[]={{"lookup_slot","0"},{"lookup_slot","18446744073709551616"},
        {"lookup_address_count","0"},{"lookup_address_count","257"},{"lookup_address_count","9"},
        {"max_envelope_age_ms","0"},{"max_envelope_age_ms","30001"},{"max_compute_units","0"},
        {"max_compute_units","1400001"},{"max_compute_units","4294967297"},{"max_slippage_bps","10001"},
        {"max_slippage_bps","65536"},{"expires_at_ms","0"},{"max_compute_price","18446744073709551615"},
        {"max_compute_price","-1"},{"max_compute_price","+1"},{"max_compute_price","1junk"}};
    for(auto [field,value]:bad){Fixture f;f.set(field,value);require(!f.read(),"numeric_or_image_bound_refused");}
    Fixture f;f.set("lookup_evidence",std::string(64,'0'));require(!f.read(),"zero_lookup_evidence_refused");
    f=Fixture{};auto all=f.tx.policy.account_keys;all[17]=test_support::key(240);std::string keys;for(const auto& k:all)keys+=hex(k);
    f.set("account_keys",keys);require(!f.read(),"canonical_program_required");
    f=Fixture{};all=f.tx.policy.account_keys;all[2]=all[4];keys.clear();for(const auto& k:all)keys+=hex(k);
    f.set("account_keys",keys);require(!f.read(),"account_alias_refused");
}
}
int main(int argc,char** argv){const std::pair<const char*,void(*)()> tests[]={{"valid",valid},{"identities",identities},{"schema",schema},{"bounds",bounds},{"byreal",byreal_review}};
    unsigned ran{},failed{};for(auto [name,fn]:tests){if(argc==2 && std::string_view(argv[1])!=name)continue;++ran;
        try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return ran&&!failed?0:1;
}
