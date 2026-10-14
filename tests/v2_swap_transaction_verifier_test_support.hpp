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
inline SolanaAccountKey key(unsigned n){SolanaAccountKey k{};k[30]=static_cast<std::uint8_t>(n>>8);k.back()=static_cast<std::uint8_t>(n);return k;}
inline SolanaAccountKey from58(std::string_view text){
    constexpr std::string_view alphabet="123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    SolanaAccountKey result{};
    for(char c:text){unsigned carry=static_cast<unsigned>(alphabet.find(c));require(carry<58,"fixture_base58");
        for(auto i=result.rbegin();i!=result.rend();++i){carry+=58U**i;*i=static_cast<std::uint8_t>(carry);carry>>=8;}
        require(carry==0,"fixture_key_size");}
    return result;
}
inline void le(std::vector<std::uint8_t>& out,std::uint64_t n,unsigned width){for(unsigned i=0;i<width;++i)out.push_back(static_cast<std::uint8_t>(n>>(8*i)));}
inline const SolanaAccountKey kJupiter=from58("JUP6LkbZbjS1jKKwapdHNy74zcZ3tLUZoi5QNyVTaV4");
inline const SolanaAccountKey kEventAuthority=from58("D8cy77BBepLMngZx6ZukaTff5hCt1HrWyKk3Hnd9oitf");
inline const SolanaAccountKey kWrappedSol=from58("So11111111111111111111111111111111111111112");
inline const SolanaAccountKey kLookupAuthority=from58("9RAufBfjGQjDfrwxeyKmZWPADHSb8HcoqCdrmpqvCr1g");
inline SolanaAccountKey dex_program(std::uint8_t variant){
    switch(variant){
    case 18:return from58("HyaB3W9q6XdA5xwpU4XnSZV94htfmbmqJXZcEbRaJutt");
    case 109:return from58("HBVw6bZtcCaezhcBrmfyXBSBRWCdv72271xQ4GPvms2z");
    case 156:return from58("REALQqNEomY6cQGZJUGwywTBD2UmDT32rZcNnfxQ5N2");
    case 177:return from58("BiSoNHVpsVZW2F7rx2eQ59yQwKxzU5NvBcmKshCSUypi");
    case 181:return from58("3TK9D8aoBFYjYZtKCjciPrVrRStsnvo7KmpcJqDavpaU");
    default:return key(900+variant);
    }
}
inline bool variant_has_direction(std::uint8_t variant){return variant==18 || variant==177 || variant==181;}

struct RouteFixture {
    SwapExecutionPolicy policy{};
    swap::Pending pending{};
    std::array<SolanaAccountKey,11> statics{};
    std::vector<SolanaAccountKey> table_entries{};
    SolanaAccountKey table{key(240)};
    std::vector<std::uint8_t> writable_lookup{4,9,2},readonly_lookup{0,7,5};
    std::uint16_t prefix_count{0};
    std::uint64_t payment{2'039'280},quoted{9'410'623},input{1'000'000},timestamp{1000},units{1'400'000},price{71'428};
    std::uint8_t variant{181},direction{1},percent{100},input_index{0},output_index{1},fee{20},required_signatures{2},readonly_signed{0},readonly_unsigned{6};
    std::uint16_t slippage{50};
    bool with_price{true},signed_payload{};
    unsigned extra{};
    std::vector<std::uint8_t> ata_accounts{0,3,1,14,5,8},close_accounts{3,1,1},pay_accounts{1,0};
    std::vector<std::uint8_t> route_fixed{8,1,2,3,7,14,4,10,7};
    std::vector<std::uint8_t> route_tail{15,1,11,12,13,2,3,8,16};
    std::vector<std::uint8_t> program_order{6,6,9,7,8,5};
    RouteFixture(unsigned pool=0){
        auto& p=policy;
        p.sponsor=key(1);p.owner=key(2);p.owner_usdc=key(3);p.owner_wsol=key(4);p.platform_fee=key(5);
        statics={p.sponsor,p.owner,p.owner_usdc,p.owner_wsol,p.platform_fee,kSolanaSystemProgram,kSolanaComputeBudgetProgram,
            kJupiter,kSolanaClassicTokenProgram,kSolanaAssociatedTokenProgram,kEventAuthority};
        table_entries.resize(12);
        for(unsigned i=0;i<table_entries.size();++i)table_entries[i]=key(300+pool*20+i);
        table_entries[0]=kWrappedSol;
        if(pool)table=key(240+pool);
        p.binding.incarnation.bytes[0]=std::byte{1};p.binding.catalog.bytes[0]=std::byte{2};
        p.binding.custody.bytes[0]=std::byte{3};p.binding.account=AccountId{2};p.binding.instrument=InstrumentId{2};
        p.binding.policy=PolicyId{99};p.binding.policy_version=PolicyVersion{1};p.binding.risk_policy_version=RiskPolicyVersion{1};
        p.max_envelope_age_ms=100;p.max_compute_price=71'428;p.max_compute_units=1'400'000;
        p.max_slippage_bps=500;p.accepted_costs.max_output_deduction=swap::BaseAtoms{2'039'280};
        pending.phase=swap::Phase::prepared;pending.handle.bytes[0]=std::byte{5};
        auto& i=pending.intent;i.binding=p.binding;i.effect=EffectId{1};i.side=Side::buy;
        i.quote_input=swap::QuoteAtoms{998'000};i.max_quote_debit=swap::QuoteAtoms{1'000'000};i.fee_cap=swap::QuoteAtoms{2'000};
        i.min_base_receive=swap::BaseAtoms{6'819'647};i.source_time=990;i.sign_by=1100;i.kill_epoch=1;i.executor_generation=1;
    }
    void place_dex(){table_entries[readonly_lookup[1]]=dex_program(variant);}
    JupiterLookupPrefix prefix()const{
        JupiterLookupPrefix result;result.table=table;
        result.count=prefix_count?prefix_count:static_cast<std::uint16_t>(table_entries.size());
        for(std::size_t i=0;i<result.count && i<table_entries.size();++i)result.addresses[i]=table_entries[i];
        return result;
    }
    std::string envelope(){
        place_dex();
        std::vector<std::uint8_t> b{required_signatures};b.resize(1+64U*required_signatures);if(signed_payload)b[1]=1;
        b.insert(b.end(),{128,required_signatures,readonly_signed,readonly_unsigned,static_cast<std::uint8_t>(statics.size())});
        for(const auto& k:statics)b.insert(b.end(),k.begin(),k.end());
        b.insert(b.end(),32,42);
        std::vector<std::vector<std::uint8_t>> ixs;
        const auto ix=[&](unsigned program,std::vector<std::uint8_t> a,std::vector<std::uint8_t> d){
            std::vector<std::uint8_t> x{static_cast<std::uint8_t>(program),static_cast<std::uint8_t>(a.size())};
            x.insert(x.end(),a.begin(),a.end());x.push_back(static_cast<std::uint8_t>(d.size()));x.insert(x.end(),d.begin(),d.end());ixs.push_back(x);};
        std::size_t order{};
        std::vector<std::uint8_t> limit{2};le(limit,units,4);ix(program_order[order++],{},limit);
        if(with_price){std::vector<std::uint8_t> cu{3};le(cu,price,8);ix(program_order[order++],{},cu);}else ++order;
        ix(program_order[order++],ata_accounts,{1});
        std::vector<std::uint8_t> route{0xe5,0x17,0xcb,0x97,0x7a,0xe3,0xad,0x2a,1,0,0,0,variant};
        if(variant_has_direction(variant))route.push_back(direction);
        route.insert(route.end(),{percent,input_index,output_index});
        le(route,input,8);le(route,quoted,8);le(route,slippage,2);route.push_back(fee);
        std::vector<std::uint8_t> accounts=route_fixed;accounts.insert(accounts.end(),route_tail.begin(),route_tail.end());
        ix(program_order[order++],accounts,route);
        ix(program_order[order++],close_accounts,{9});
        std::vector<std::uint8_t> transfer{2,0,0,0};le(transfer,payment,8);ix(program_order[order++],pay_accounts,transfer);
        for(unsigned n=0;n<extra;++n)ix(5,pay_accounts,transfer);
        b.push_back(static_cast<std::uint8_t>(ixs.size()));
        for(const auto& x:ixs)b.insert(b.end(),x.begin(),x.end());
        b.push_back(1);b.insert(b.end(),table.begin(),table.end());
        b.push_back(static_cast<std::uint8_t>(writable_lookup.size()));b.insert(b.end(),writable_lookup.begin(),writable_lookup.end());
        b.push_back(static_cast<std::uint8_t>(readonly_lookup.size()));b.insert(b.end(),readonly_lookup.begin(),readonly_lookup.end());
        constexpr std::string_view digits="0123456789abcdef";std::string hex;
        for(auto c:b){hex+=digits[c>>4];hex+=digits[c&15];}
        return "{\"organizationId\":\"verifier-fixture\",\"parameters\":{\"signWith\":\""+std::string(31,'1')+
            "3\",\"type\":\"TRANSACTION_TYPE_SOLANA\",\"unsignedTransaction\":\""+hex+
            "\"},\"timestampMs\":\""+std::to_string(timestamp)+"\",\"type\":\"ACTIVITY_TYPE_SIGN_TRANSACTION_V2\"}";
    }
    SwapTransactionVerification run(std::uint64_t now=1001){
        auto e=envelope();pending.payload=sha256_digest(std::as_bytes(std::span(e)));
        return verify_swap_transaction(e,"verifier-fixture",std::string(31,'1')+'3',pending,policy,prefix(),now);
    }
};
}
