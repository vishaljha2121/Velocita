#include "velocita/v2/swap_signer_protocol.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
using namespace velocita::v2;using namespace velocita::v2::swap_signer;using velocita::v2::truemarkets::JupiterLookupPrefix;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
Request request(){Request r;r.operation=Operation::sign;r.effect=EffectId{7};r.sequence=9;r.minimum_index=11;r.sign_by_ms=20100;
    r.handle[0]='a';r.handle_size=1;constexpr std::string_view payload="original\n";r.payload_size=payload.size();
    std::copy(payload.begin(),payload.end(),reinterpret_cast<char*>(r.payload.data()));return r;}
void reseal(std::span<std::byte>b){const auto digest=sha256_digest(b.first(b.size()-32));std::copy(digest.bytes.begin(),digest.bytes.end(),b.end()-32);}
void request_codec(){
    std::array<std::byte,kRequestBytes> bytes{};const auto r=request();const auto n=encode_request(r,bytes);
    require(n==98 && bytes[0]==std::byte{'V'} && bytes[3]==std::byte{'2'} && bytes[8]==std::byte{1} && bytes[16]==std::byte{7} &&
        bytes[24]==std::byte{9} && bytes[32]==std::byte{11} && bytes[40]==std::byte{0x84} && bytes[41]==std::byte{0x4e},"request little endian framing matches worked oracle");
    Request decoded;require(decode_request(std::span(bytes).first(n),decoded) && decoded.payload_view().size()==9 &&
        std::equal(decoded.payload_view().begin(),decoded.payload_view().end(),r.payload_view().begin()),"request preserves exact original newline bytes");
    auto full=r;full.handle.fill('a');full.handle_size=96;full.payload.fill(std::byte{'x'});full.payload_size=8192;
    require(encode_request(full,bytes)==88+96+8192,"maximum request without prefix is accepted exactly");
    require(decode_request(std::span(bytes).first(88+96+8192),decoded) && decoded.payload_size==8192 && decoded.handle_size==96,"maximum request decodes without truncation");
}
void malformed_request(){
    std::array<std::byte,kRequestBytes> bytes{};const auto n=encode_request(request(),bytes);Request out=request();
    for(unsigned mode=0;mode<5;++mode){auto copy=bytes;if(mode==0)copy[9]=std::byte{1};if(mode==1)copy[8]=std::byte{3};
        if(mode==2)copy[56]=std::byte{'/'};if(mode==3)copy[32]=std::byte{0};if(mode==4)copy[57]^=std::byte{1};
        if(mode!=4)reseal(std::span(copy).first(n));
        require(!decode_request(std::span(copy).first(n),out) && out.payload_size==0,"malformed request clears reusable output");}
    require(!decode_request(std::span(bytes).first(n-1),out),"truncated request is refused");
}
Request with_lookup(std::uint16_t count){auto r=request();r.lookup.table[31]=9;r.lookup.count=count;
    for(std::size_t i=0;i<count;++i){r.lookup.addresses[i][0]=static_cast<std::uint8_t>(i);r.lookup.addresses[i][31]=1;}return r;}
void lookup_codec(){
    std::array<std::byte,kRequestBytes> bytes{};const auto r=with_lookup(10);const auto n=encode_request(r,bytes);
    require(n==98+32+320 && bytes[52]==std::byte{10} && bytes[53]==std::byte{0},"lookup count framed little endian");
    Request decoded;require(decode_request(std::span(bytes).first(n),decoded) && decoded.lookup==r.lookup,"lookup prefix round trips exactly");
    auto full=with_lookup(256);full.handle.fill('a');full.handle_size=96;full.payload.fill(std::byte{'x'});full.payload_size=8192;
    require(encode_request(full,bytes)==kRequestBytes && decode_request(bytes,decoded) && decoded.lookup.count==256,"maximum request with full prefix fits exactly");
    Request plain;require(decode_request(std::span(bytes).first(encode_request(request(),bytes)),plain) && plain.lookup==JupiterLookupPrefix{},
        "request without prefix decodes empty");
}
void malformed_lookup(){
    std::array<std::byte,kRequestBytes> bytes{};auto r=with_lookup(10);r.lookup.table={};
    require(encode_request(r,bytes)==0,"prefix without table is not encoded");
    r=with_lookup(10);r.lookup.count=257;require(encode_request(r,bytes)==0,"oversized prefix is not encoded");
    r=with_lookup(10);const auto n=encode_request(r,bytes);Request out;
    for(unsigned mode=0;mode<3;++mode){auto copy=bytes;std::size_t size=n;
        if(mode==0){copy[52]=std::byte{11};}
        if(mode==1){std::fill_n(copy.begin()+56+1+9,32,std::byte{});}
        if(mode==2){copy[52]=std::byte{0};}
        reseal(std::span(copy).first(size));
        require(!decode_request(std::span(copy).first(size),out) && out.lookup==JupiterLookupPrefix{},"inconsistent prefix framing refused");}
    auto reserved=bytes;reserved[54]=std::byte{1};reseal(std::span(reserved).first(n));
    require(!decode_request(std::span(reserved).first(n),out),"nonzero reserved bytes refused");
    std::array<std::byte,kRequestBytes> large{};const auto m=encode_request(with_lookup(256),large);
    std::fill_n(large.begin()+static_cast<std::ptrdiff_t>(m)-32,32,std::byte{});
    const auto bigger=m+32;large[4]=static_cast<std::byte>(bigger&255);large[5]=static_cast<std::byte>(bigger>>8);
    large[52]=std::byte{1};large[53]=std::byte{1};reseal(std::span(large).first(bigger));
    require(!decode_request(std::span(large).first(bigger),out) && out.lookup==JupiterLookupPrefix{},"prefix longer than the table capacity refused");
}
Reply signature_reply(){Reply r;r.code=ReplyCode::retrieved;r.request_digest.bytes[0]=std::byte{1};r.public_key[0]=std::byte{2};
    r.der_size=72;r.der[0]=std::byte{0x30};r.der[1]=std::byte{70};r.der[2]=std::byte{2};r.der[3]=std::byte{33};
    r.der[4]=std::byte{0};r.der[5]=std::byte{0x80};r.der[37]=std::byte{2};r.der[38]=std::byte{33};r.der[39]=std::byte{0};r.der[40]=std::byte{0x80};return r;}
void reply_codec(){
    const auto r=signature_reply();std::array<std::byte,kReplyBytes> bytes{};const auto n=encode_reply(r,bytes);
    require(n==193,"maximum DER signature fits bounded reply");
    Reply out;require(decode_reply(std::span(bytes).first(n),out) && out.stamp.view().size()==375 && out.code==ReplyCode::retrieved &&
        out.request_digest==r.request_digest,"maximum reply recreates canonical full stamp");
    bytes[8]=std::byte{0};reseal(std::span(bytes).first(n));require(!decode_reply(std::span(bytes).first(n),out) && out.stamp.empty(),"refused reply cannot carry signature material");
    Reply refused;refused.request_digest=r.request_digest;const auto m=encode_reply(refused,bytes);
    require(m==88 && decode_reply(std::span(bytes).first(m),out) && out.stamp.empty(),"refusal is explicit and contains no stamp");
    bytes[16]=std::byte{};reseal(std::span(bytes).first(m));require(!decode_reply(std::span(bytes).first(m),out),"reply requires request correlation");
}
void authenticate_codec(){
    constexpr std::string_view message="0b9e3a52-6f2c-4d6e-9a1b-2c3d4e5f6a7b.1791993300";
    Request r;r.operation=Operation::authenticate;r.payload_size=message.size();std::copy(message.begin(),message.end(),reinterpret_cast<char*>(r.payload.data()));
    std::array<std::byte,kRequestBytes> bytes{};const auto n=encode_request(r,bytes);Request out;
    require(n==88+message.size() && bytes[8]==std::byte{3} && decode_request(std::span(bytes).first(n),out) && out.operation==Operation::authenticate &&
        std::equal(out.payload_view().begin(),out.payload_view().end(),r.payload_view().begin()),"authenticate request carries only the message");
    for(unsigned mode=0;mode<7;++mode){auto x=r;if(mode==0)x.effect=EffectId{1};if(mode==1)x.sequence=1;if(mode==2)x.sign_by_ms=1;if(mode==3)x.minimum_index=1;
        if(mode==4){x.handle[0]='a';x.handle_size=1;}if(mode==5)x.payload_size=0;if(mode==6)x.payload_size=65;
        require(!encode_request(x,bytes),"authenticate request refuses signing fields and unbounded messages");}
    auto lookup=r;lookup.lookup.count=1;lookup.lookup.table[0]=1;require(!encode_request(lookup,bytes),"authenticate request refuses lookup tables");
    lookup.lookup.table[0]=0;require(!encode_request(lookup,bytes),"authenticate request refuses a lookup count");
    auto reply=signature_reply();reply.code=ReplyCode::authenticated;std::array<std::byte,kReplyBytes> frame{};const auto m=encode_reply(reply,frame);Reply decoded;
    require(m==193 && decode_reply(std::span(frame).first(m),decoded) && decoded.code==ReplyCode::authenticated && !decoded.stamp.empty(),"authenticated reply carries the signature");
    Reply unknown;unknown.request_digest=reply.request_digest;unknown.code=static_cast<ReplyCode>(5);require(!encode_reply(unknown,frame),"unknown reply code refused");
    unknown.code=ReplyCode::refused;const auto k=encode_reply(unknown,frame);frame[8]=std::byte{5};reseal(std::span(frame).first(k));
    require(k==88 && !decode_reply(std::span(frame).first(k),decoded),"unknown reply code not decoded");
}
}
int main(int argc,char**argv){const std::pair<const char*,void(*)()> cases[]{{"request",request_codec},{"malformed",malformed_request},{"reply",reply_codec},{"lookup",lookup_codec},{"malformed_lookup",malformed_lookup},{"authenticate",authenticate_codec}};
    unsigned ran{},failed{};for(auto[name,fn]:cases){if(argc==2&&std::string_view(argv[1])!=name)continue;++ran;try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}return ran&&!failed?0:1;}
