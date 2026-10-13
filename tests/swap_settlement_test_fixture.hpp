#pragma once
#include "velocita/v2/swap_settlement_lifecycle.hpp"
#include "velocita/v2/swap_rest_codec.hpp"
#include "velocita/v2/swap_codec.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unistd.h>
#include <vector>
using namespace velocita::v2;




namespace settlement_lifecycle_fixture {
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
truemarkets::SolanaAccountKey key(unsigned n){truemarkets::SolanaAccountKey k{};k[31]=static_cast<std::uint8_t>(n);return k;}
std::string address(unsigned n){return std::string(31,'1')+"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"[n];}
Digest256 digest(unsigned n){Digest256 d;d.bytes[0]=static_cast<std::byte>(n);return d;}
std::string base64(const std::vector<std::uint8_t>& data){
    constexpr char digits[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string s;
    for(std::size_t i=0;i<data.size();i+=3){const unsigned n=(unsigned(data[i])<<16)|(i+1<data.size()?unsigned(data[i+1])<<8:0)|(i+2<data.size()?data[i+2]:0);
        s+=digits[n>>18];s+=digits[(n>>12)&63];s+=i+1<data.size()?digits[(n>>6)&63]:'=';s+=i+2<data.size()?digits[n&63]:'=';}
    return s;
}
std::string wrap(const std::vector<std::uint8_t>& b){std::string h;for(auto c:b){h+="0123456789abcdef"[c>>4];h+="0123456789abcdef"[c&15];}
    return "{\"organizationId\":\"org\",\"parameters\":{\"signWith\":\""+address(2)+"\",\"type\":\"TRANSACTION_TYPE_SOLANA\",\"unsignedTransaction\":\""+h+"\"},\"timestampMs\":\"1000\",\"type\":\"ACTIVITY_TYPE_SIGN_TRANSACTION_V2\"}";}
void replace(std::string& s,std::string_view a,std::string_view b){auto p=s.find(a);require(p!=std::string::npos,"fixture replacement exists");s.replace(p,a.size(),b);}
struct Clock:SwapFinalizedReadClock{std::uint64_t now{10};std::uint64_t now_ms()noexcept override{return now;}};
struct Port:SwapFinalizedRpcPort{
#ifdef VELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY
    std::function<void()> during;
#endif
Digest256 identity{digest(1)};SettlementRpcResponse reply{};std::string requested;std::uint64_t requested_timeout{};unsigned calls{};Clock* clock{};bool change_identity{},late{},backward{},throws{};
    Digest256 provider_identity()const noexcept override{return identity;}
    SettlementRpcResponse request(std::string_view b,std::uint64_t timeout_ms)override{
#ifdef VELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY
        if(during)during();
#endif
        ++calls;requested=b;requested_timeout=timeout_ms;if(change_identity)identity=digest(2);if(late)clock->now=30;if(backward)clock->now=9;if(throws)throw std::runtime_error("lost");return reply;}
    void body(const std::string& s){reply={};reply.transport_complete=true;reply.http_status=200;reply.size=s.size();std::copy(s.begin(),s.end(),reply.body.begin());}
};

std::string b58(std::span<const std::uint8_t> bytes){
    constexpr std::string_view digits="123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    std::vector<unsigned> out(1);std::size_t zeroes{};while(zeroes<bytes.size() && !bytes[zeroes])++zeroes;
    for(auto b:bytes){unsigned carry=b;for(auto& n:out){carry+=n*256;n=carry%58;carry/=58;}while(carry){out.push_back(carry%58);carry/=58;}}
    std::string s(zeroes,'1');if(zeroes!=bytes.size())for(auto i=out.rbegin();i!=out.rend();++i)s+=digits[*i];return s;
}
truemarkets::SolanaAccountKey unbase58(std::string_view text){
    constexpr std::string_view digits="123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";truemarkets::SolanaAccountKey k{};
    for(char c:text){unsigned carry=digits.find(c);for(std::size_t i=32;i-->0;){carry+=k[i]*58;k[i]=carry&255;carry>>=8;}require(!carry,"fixture key fits");}return k;
}
void little(std::vector<std::uint8_t>& b,std::uint64_t n,unsigned count){for(unsigned i=0;i<count;++i)b.push_back(n>>(i*8));}
std::vector<std::uint8_t> amount(unsigned op,std::uint64_t n){std::vector<std::uint8_t> b{static_cast<std::uint8_t>(op)};little(b,n,8);return b;}
struct Ix{unsigned program;std::vector<std::uint8_t> accounts,data;unsigned height{2};};
std::string inner(const Ix& ix,unsigned height=2){std::string a;for(auto i:ix.accounts){if(!a.empty())a+=',';a+=std::to_string(i);}return "{\"programIdIndex\":"+std::to_string(ix.program)+",\"accounts\":["+a+"],\"data\":\""+b58(ix.data)+"\",\"stackHeight\":"+std::to_string(height)+"}";}
struct LifecycleFixture {
    Clock clock;Port port;SwapSubmissionTransaction signature{};
    std::array<truemarkets::SolanaAccountKey,15> keys{};
    std::uint64_t repayment_lamports{2039280};
    unsigned route_top_index{1};
    std::vector<Ix> top,create,route;std::string envelope,json;
    LifecycleFixture(){port.clock=&clock;for(unsigned i=0;i<7;++i)keys[i]=key(i+1);
        keys[7]=truemarkets::kSolanaSystemProgram;keys[8]=truemarkets::kSolanaClassicTokenProgram;keys[9]=truemarkets::kSolanaAssociatedTokenProgram;
        keys[10]=unbase58("JUP6LkbZbjS1jKKwapdHNy74zcZ3tLUZoi5QNyVTaV4");keys[11]=unbase58(truemarkets::kSwapUsdcMint);keys[12]=unbase58(truemarkets::kSwapSolMint);keys[13]=key(9);keys[14]=unbase58("D8cy77BBepLMngZx6ZukaTff5hCt1HrWyKk3Hnd9oitf");
        std::vector<std::uint8_t> funding;little(funding,0,4);little(funding,1488440,8);little(funding,165,8);funding.insert(funding.end(),keys[8].begin(),keys[8].end());
        std::vector<std::uint8_t> init{18};init.insert(init.end(),keys[1].begin(),keys[1].end());
        create={{7,{0,5},funding},{8,{5},{22}},{8,{5,12},init}};
        route={{8,{2,4,1},amount(3,2000)},{8,{2,3,1},amount(3,998000)},{8,{6,5,13},amount(3,9410957)}};
        std::vector<std::uint8_t> repayment;little(repayment,2,4);little(repayment,2039280,8);
        top={{9,{0,5,1,12,7,8},{1}},{10,{0,1,2,3,4,5,6,7,8,10,11,13},{42}},{8,{5,1,1},{9}},{7,{1,0},repayment}};rebuild();
    }
    std::string tb(unsigned index,unsigned owner,std::uint64_t atoms,bool native=false){return "{\"accountIndex\":"+std::to_string(index)+",\"mint\":\""+std::string(native?truemarkets::kSwapSolMint:truemarkets::kSwapUsdcMint)+"\",\"owner\":\""+address(owner)+"\",\"programId\":\"TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA\",\"uiTokenAmount\":{\"amount\":\""+std::to_string(atoms)+"\",\"decimals\":"+(native?"9":"6")+"}}";}
    void rebuild(){std::vector<std::uint8_t> tx{2};tx.resize(129,0);tx.insert(tx.end(),{128,2,0,8,15});for(auto k:keys)tx.insert(tx.end(),k.begin(),k.end());tx.insert(tx.end(),32,3);tx.push_back(top.size());
        for(const auto& i:top){tx.push_back(i.program);tx.push_back(i.accounts.size());tx.insert(tx.end(),i.accounts.begin(),i.accounts.end());tx.push_back(i.data.size());tx.insert(tx.end(),i.data.begin(),i.data.end());}tx.push_back(0);envelope=wrap(tx);
        for(unsigned i=0;i<64;++i){signature[i]=static_cast<std::byte>(i+1);tx[i+1]=i+1;tx[65+i]=i+2;}
        std::string groups;for(unsigned n=0;n<2;++n){if(n)groups+=',';groups+="{\"index\":"+std::to_string(n?route_top_index:0)+",\"instructions\":[";bool first=true;for(const auto& ix:n?route:create){if(!first)groups+=',';first=false;groups+=inner(ix,ix.height);}groups+="]}";}
        json="{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{\"slot\":100,\"version\":0,\"transaction\":[\""+base64(tx)+"\",\"base64\"],\"meta\":{\"err\":null,\"fee\":38000,\"preBalances\":[30000000,10000000,1488440,1488440,1488440,0,20000000,1,1,1,1,1,1,1,1],\"postBalances\":["+std::to_string(28473560+repayment_lamports)+","+std::to_string(20899397-repayment_lamports)+",1488440,1488440,1488440,0,10589043,1,1,1,1,1,1,1,1],\"loadedAddresses\":{\"writable\":[],\"readonly\":[]},\"preTokenBalances\":["+tb(2,2,1006989)+","+tb(3,9,1000000)+","+tb(4,9,0)+","+tb(6,9,18511560,true)+"],\"postTokenBalances\":["+tb(2,2,6989)+","+tb(3,9,1998000)+","+tb(4,9,2000)+","+tb(6,9,9100603,true)+"],\"innerInstructions\":["+groups+"]}}}";
        port.body(json);
    }
    SwapLifecycleReplay replay(){port.body(json);SwapFinalizedReader reader(port,clock,digest(1),20);const auto read=reader.read(signature,7,100);require(read.evidence.has_value(),"fixture finalized response");return replay_swap_settlement_lifecycle(*read.evidence,envelope,"org",address(2),{},key(2));}
};
}
