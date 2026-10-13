#include "velocita/v2/swap_signer_protocol.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
using namespace velocita::v2;using namespace velocita::v2::swap_signer;
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
    require(encode_request(full,bytes)==kRequestBytes,"maximum request capacity is accepted exactly");
    require(decode_request(bytes,decoded) && decoded.payload_size==8192 && decoded.handle_size==96,"maximum request decodes without truncation");
}
void malformed_request(){
    std::array<std::byte,kRequestBytes> bytes{};const auto n=encode_request(request(),bytes);Request out=request();
    for(unsigned mode=0;mode<5;++mode){auto copy=bytes;if(mode==0)copy[9]=std::byte{1};if(mode==1)copy[8]=std::byte{3};
        if(mode==2)copy[56]=std::byte{'/'};if(mode==3)copy[32]=std::byte{0};if(mode==4)copy[57]^=std::byte{1};
        if(mode!=4)reseal(std::span(copy).first(n));
        require(!decode_request(std::span(copy).first(n),out) && out.payload_size==0,"malformed request clears reusable output");}
    require(!decode_request(std::span(bytes).first(n-1),out),"truncated request is refused");
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
}
int main(int argc,char**argv){const std::pair<const char*,void(*)()> cases[]{{"request",request_codec},{"malformed",malformed_request},{"reply",reply_codec}};
    unsigned ran{},failed{};for(auto[name,fn]:cases){if(argc==2&&std::string_view(argv[1])!=name)continue;++ran;try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}return ran&&!failed?0:1;}
