#include "velocita/v2/swap_signing_stamp.hpp"

#include <algorithm>

namespace velocita::v2::swap_signing {
namespace {
constexpr std::array<std::uint8_t,32> order{
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51};
std::uint8_t value(std::byte b) noexcept { return std::to_integer<std::uint8_t>(b); }
bool scalar(std::span<const std::byte> bytes,std::size_t& offset) noexcept {
    if(offset+2>bytes.size() || value(bytes[offset++])!=2) return false;
    const auto length=value(bytes[offset++]);
    if(length==0 || length>33 || length>bytes.size()-offset) return false;
    auto digits=bytes.subspan(offset,length);offset+=length;
    if((value(digits.front())&0x80)!=0) return false;
    if(digits.size()>1 && value(digits.front())==0) {
        if((value(digits[1])&0x80)==0) return false;
        digits=digits.subspan(1);
    }
    if(digits.size()>32) return false;
    if(std::all_of(digits.begin(),digits.end(),[](auto b){return value(b)==0;})) return false;
    if(digits.size()==32 && !std::lexicographical_compare(digits.begin(),digits.end(),order.begin(),order.end(),
        [](auto a,auto b){return static_cast<unsigned>(a)<static_cast<unsigned>(b);})) return false;
    return true;
}
bool canonical_der(std::span<const std::byte> bytes) noexcept {
    if(bytes.size()<8 || bytes.size()>72 || value(bytes[0])!=0x30 || value(bytes[1])!=bytes.size()-2) return false;
    std::size_t offset=2;
    return scalar(bytes,offset) && scalar(bytes,offset) && offset==bytes.size();
}
}
StampCode encode_api_key_stamp(std::span<const std::byte> public_key,std::span<const std::byte> der,ApiKeyStamp& destination) noexcept {
    destination={};
    if(public_key.size()!=33 || (value(public_key[0])!=2 && value(public_key[0])!=3)) return StampCode::invalid_public_key_encoding;
    if(!canonical_der(der)) return StampCode::invalid_der_signature;
    std::array<char,281> json{};std::size_t length{};
    const auto append=[&](std::string_view text){for(auto c:text)json[length++]=c;};
    const auto hex=[&](std::span<const std::byte> bytes){
        constexpr std::string_view digits="0123456789abcdef";
        for(auto b:bytes){json[length++]=digits[value(b)>>4];json[length++]=digits[value(b)&15];}
    };
    append(R"({"publicKey":")");hex(public_key);
    append(R"(","scheme":"SIGNATURE_SCHEME_TK_API_P256","signature":")");hex(der);append(R"("})");
    constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::size_t written{};
    for(std::size_t i=0;i<length;i+=3) {
        const auto count=std::min<std::size_t>(3,length-i);
        std::uint32_t bits=static_cast<unsigned char>(json[i])<<16;
        if(count>1)bits|=static_cast<unsigned char>(json[i+1])<<8;
        if(count>2)bits|=static_cast<unsigned char>(json[i+2]);
        destination.text_[written++]=alphabet[(bits>>18)&63];
        destination.text_[written++]=alphabet[(bits>>12)&63];
        if(count>1)destination.text_[written++]=alphabet[(bits>>6)&63];
        if(count>2)destination.text_[written++]=alphabet[bits&63];
    }
    destination.length_=static_cast<std::uint16_t>(written);
    return StampCode::encoded;
}
}
