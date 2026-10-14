#pragma once
#include "velocita/v2/swap_transaction_verifier.hpp"
#include "velocita/v2/solana_instruction_decode.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
namespace velocita::v2::test_support {
using namespace truemarkets;
inline void require(bool ok,const char* name){if(!ok)throw std::runtime_error(name);}
inline SolanaAccountKey key(unsigned n){SolanaAccountKey k{};k.back()=static_cast<std::uint8_t>(n);return k;}
inline SolanaAccountKey from58(std::string_view text){
    constexpr std::string_view alphabet="123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    SolanaAccountKey result{};
    for(char c:text){unsigned carry=static_cast<unsigned>(alphabet.find(c));require(carry<58,"fixture_base58");
        for(auto i=result.rbegin();i!=result.rend();++i){carry+=58U**i;*i=static_cast<std::uint8_t>(carry);carry>>=8;}
        require(carry==0,"fixture_key_size");}
    return result;
}
inline void le(std::vector<std::uint8_t>& out,std::uint64_t n,unsigned width){for(unsigned i=0;i<width;++i)out.push_back(n>>(8*i));}
struct DirectBuyFixture {
    DirectBuyVerificationPolicy policy{};
    std::array<SolanaLookupEvidence,1> lookup{};
    swap::Pending pending{};
    std::uint64_t payment{550'840},quoted{9'410'623},input{1'000'000},timestamp{1000};
    std::uint8_t route_kind{181},route_direction{},route_percent{100},fee{20};
    std::uint16_t slippage{500};
    bool extra{},wrong_close{},wrong_route_account{},signed_payload{};
    DirectBuyFixture(){
        auto& p=policy;for(unsigned i=0;i<20;++i)p.account_keys[i]=key(i+1);
        p.account_keys[5]=kSolanaSystemProgram;p.account_keys[6]=kSolanaComputeBudgetProgram;
        p.account_keys[7]=from58("JUP6LkbZbjS1jKKwapdHNy74zcZ3tLUZoi5QNyVTaV4");
        p.account_keys[8]=kSolanaClassicTokenProgram;p.account_keys[10]=kSolanaAssociatedTokenProgram;
        p.account_keys[9]=from58("7EvGXYXffTFAMvGoE8XLQsGJ2qtjxTmLgpcnXb3Bmtuc");
        p.account_keys[11]=from58("D8cy77BBepLMngZx6ZukaTff5hCt1HrWyKk3Hnd9oitf");
        p.account_keys[15]=from58("So11111111111111111111111111111111111111112");
        p.account_keys[16]=from58("Sysvar1nstructions1111111111111111111111111");
        p.account_keys[17]=from58("3TK9D8aoBFYjYZtKCjciPrVrRStsnvo7KmpcJqDavpaU");
        p.account_keys[18]=from58("EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v");
        p.account_keys[19]=from58("J1to1yufRnoWn81KYg1XkTWzmKjnYSnmE2VY8DGUJ9Qv");
        p.binding.incarnation.bytes[0]=std::byte{1};p.binding.catalog.bytes[0]=std::byte{2};
        p.binding.custody.bytes[0]=std::byte{3};p.binding.account=AccountId{2};p.binding.instrument=InstrumentId{2};
        p.binding.policy=PolicyId{99};p.binding.policy_version=PolicyVersion{1};p.binding.risk_policy_version=RiskPolicyVersion{1};
        p.max_envelope_age_ms=100;p.max_compute_price=71'428;p.max_compute_units=1'400'000;
        p.max_slippage_bps=500;p.expires_at_ms=2000;p.accepted_costs.max_output_deduction=swap::BaseAtoms{550'840};
        lookup[0].table=key(240);lookup[0].observed_slot=777;lookup[0].evidence_digest.bytes[0]=std::byte{4};
        lookup[0].address_count=8;std::copy(p.account_keys.begin()+12,p.account_keys.end(),lookup[0].addresses.begin());
        p.lookup_image_digest=direct_buy_lookup_digest(lookup);
        pending.phase=swap::Phase::prepared;pending.handle.bytes[0]=std::byte{5};
        auto& i=pending.intent;i.binding=p.binding;i.effect=EffectId{1};i.side=Side::buy;
        i.quote_input=swap::QuoteAtoms{998'000};i.max_quote_debit=swap::QuoteAtoms{1'000'000};i.fee_cap=swap::QuoteAtoms{2'000};
        i.min_base_receive=swap::BaseAtoms{8'389'252};i.source_time=990;i.sign_by=1100;i.kill_epoch=1;i.executor_generation=1;
    }
    std::string envelope()const {
        std::vector<std::uint8_t> b{2};b.resize(129);if(signed_payload)b[1]=1;
        b.insert(b.end(),{128,2,0,7,12});
        for(unsigned i=0;i<12;++i)b.insert(b.end(),policy.account_keys[i].begin(),policy.account_keys[i].end());
        b.insert(b.end(),32,42);b.push_back(extra?7:6);
        const auto ix=[&](unsigned program,std::vector<std::uint8_t> a,std::vector<std::uint8_t> d){
            b.push_back(program);b.push_back(a.size());b.insert(b.end(),a.begin(),a.end());
            b.push_back(d.size());b.insert(b.end(),d.begin(),d.end());};
        ix(6,{}, {2,0xc0,0x5c,0x15,0});ix(6,{}, {3,4,0x17,1,0,0,0,0,0});
        ix(10,{0,3,1,15,5,8},{1});
        std::vector<std::uint8_t> route{0xe5,0x17,0xcb,0x97,0x7a,0xe3,0xad,0x2a,1,0,0,0,
            route_kind,route_direction,route_percent,0,1};
        le(route,input,8);le(route,quoted,8);le(route,slippage,2);route.push_back(fee);
        std::vector<std::uint8_t> accounts{8,1,2,3,7,15,4,11,7,17,1,12,14,13,15,18,3,2,8,5,1,9,9,19,16};
        if(wrong_route_account)accounts[2]=4;
        ix(7,accounts,route);ix(8,{3,static_cast<std::uint8_t>(wrong_close?0:1),1},{9});
        std::vector<std::uint8_t> transfer{2,0,0,0};le(transfer,payment,8);ix(5,{1,0},transfer);
        if(extra)ix(5,{1,0},transfer);
        b.push_back(1);b.insert(b.end(),lookup[0].table.begin(),lookup[0].table.end());
        b.insert(b.end(),{3,0,1,2,5,3,4,5,6,7});
        constexpr std::string_view digits="0123456789abcdef";std::string hex;
        for(auto c:b){hex+=digits[c>>4];hex+=digits[c&15];}
        return "{\"organizationId\":\"verifier-fixture\",\"parameters\":{\"signWith\":\""+std::string(31,'1')+
            "3\",\"type\":\"TRANSACTION_TYPE_SOLANA\",\"unsignedTransaction\":\""+hex+
            "\"},\"timestampMs\":\""+std::to_string(timestamp)+"\",\"type\":\"ACTIVITY_TYPE_SIGN_TRANSACTION_V2\"}";
    }
    SwapTransactionVerification run(std::uint64_t now=1001){auto e=envelope();pending.payload=sha256_digest(std::as_bytes(std::span(e)));
        return verify_swap_transaction(e,"verifier-fixture",std::string(31,'1')+'3',pending,policy,lookup,now);}
};
struct ByrealBuyFixture : DirectBuyFixture {
    std::uint8_t readonly_unsigned{8};
    ByrealBuyFixture() {
        policy.route=DirectBuyRoute::byreal_dynamic_v3;
        for(unsigned i=0;i<20;++i)policy.account_keys[i]=key(i+1);
        for(unsigned i=0;i<7;++i)policy.additional_account_keys[i]=key(i+21);
        policy.account_keys[8]=kSolanaSystemProgram;policy.account_keys[9]=kSolanaComputeBudgetProgram;
        policy.account_keys[10]=from58("JUP6LkbZbjS1jKKwapdHNy74zcZ3tLUZoi5QNyVTaV4");
        policy.account_keys[11]=kSolanaClassicTokenProgram;policy.account_keys[13]=kSolanaAssociatedTokenProgram;
        policy.account_keys[14]=from58("D8cy77BBepLMngZx6ZukaTff5hCt1HrWyKk3Hnd9oitf");
        policy.additional_account_keys[0]=from58("MemoSq4gqABAXKb96qnH8TysNcWxMyWCqXgDLGmfcHr");
        policy.additional_account_keys[1]=from58("REALQqNEomY6cQGZJUGwywTBD2UmDT32rZcNnfxQ5N2");
        policy.additional_account_keys[2]=from58("So11111111111111111111111111111111111111112");
        policy.additional_account_keys[3]=from58("TokenzQdBNbLqP5VEhdkAS6EPFLC1PHnBqCXEpPxuEb");
        policy.additional_account_keys[5]=from58("EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v");
        lookup[0].address_count=11;
        for(unsigned i=0;i<11;++i)lookup[0].addresses[i]=policy.account_key(16+i);
        policy.lookup_image_digest=direct_buy_lookup_digest(lookup);
        policy.accepted_costs.max_output_deduction=swap::BaseAtoms{2'039'280};
        payment=2'039'280;quoted=9'345'460;route_kind=156;
        pending.intent.min_base_receive=swap::BaseAtoms{6'838'907};
    }
    std::string envelope() const {
        std::vector<std::uint8_t> b{2};b.resize(129);if(signed_payload)b[1]=1;
        b.insert(b.end(),{128,2,0,readonly_unsigned,16});
        for(unsigned i=0;i<16;++i)b.insert(b.end(),policy.account_keys[i].begin(),policy.account_keys[i].end());
        b.insert(b.end(),32,42);b.push_back(extra?7:6);
        const auto ix=[&](unsigned program,std::vector<std::uint8_t> a,std::vector<std::uint8_t> d){
            b.push_back(program);b.push_back(a.size());b.insert(b.end(),a.begin(),a.end());b.push_back(d.size());b.insert(b.end(),d.begin(),d.end());};
        ix(9,{}, {2,0xc0,0x5c,0x15,0});ix(9,{}, {3,4,0x17,1,0,0,0,0,0});ix(13,{0,4,1,22,8,11},{1});
        std::vector<std::uint8_t> route{0xe5,0x17,0xcb,0x97,0x7a,0xe3,0xad,0x2a,1,0,0,0,route_kind,route_percent,0,1};
        le(route,input,8);le(route,quoted,8);le(route,slippage,2);route.push_back(fee);
        std::vector<std::uint8_t> accounts{11,1,3,4,10,22,5,14,10,21,1,24,18,3,4,19,17,16,11,23,20,25,22,26,7,2,6,12,15,10};
        if(wrong_route_account)accounts[2]=5;
        ix(10,accounts,route);ix(11,{4,static_cast<std::uint8_t>(wrong_close?0:1),1},{9});
        std::vector<std::uint8_t> transfer{2,0,0,0};le(transfer,payment,8);ix(8,{1,0},transfer);if(extra)ix(8,{1,0},transfer);
        b.push_back(1);b.insert(b.end(),lookup[0].table.begin(),lookup[0].table.end());
        b.insert(b.end(),{4,0,1,2,3,7,4,5,6,7,8,9,10});
        constexpr std::string_view digits="0123456789abcdef";std::string hex;
        for(auto c:b){hex+=digits[c>>4];hex+=digits[c&15];}
        return "{\"organizationId\":\"verifier-fixture\",\"parameters\":{\"signWith\":\""+std::string(31,'1')+
            "3\",\"type\":\"TRANSACTION_TYPE_SOLANA\",\"unsignedTransaction\":\""+hex+
            "\"},\"timestampMs\":\""+std::to_string(timestamp)+"\",\"type\":\"ACTIVITY_TYPE_SIGN_TRANSACTION_V2\"}";
    }
    SwapTransactionVerification run(std::uint64_t now=1001){auto e=envelope();pending.payload=sha256_digest(std::as_bytes(std::span(e)));
        return verify_swap_transaction(e,"verifier-fixture",std::string(31,'1')+'3',pending,policy,lookup,now);}
};

}
