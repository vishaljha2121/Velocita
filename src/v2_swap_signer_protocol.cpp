#include "velocita/v2/swap_signer_protocol.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <limits>
namespace velocita::v2::swap_signer {
namespace {
void put(std::span<std::byte> out,std::uint64_t v,unsigned count)noexcept{for(unsigned n=0;n<count;++n)out[n]=static_cast<std::byte>(v>>(8*n));}
std::uint64_t get(std::span<const std::byte> in,unsigned count)noexcept{std::uint64_t v{};for(unsigned n=0;n<count;++n)v|=std::uint64_t(std::to_integer<unsigned>(in[n]))<<(8*n);return v;}
bool zeros(std::span<const std::byte> b)noexcept{return std::all_of(b.begin(),b.end(),[](auto v){return v==std::byte{};});}
bool valid(const Request&r)noexcept{
    if(r.operation==Operation::authenticate)
        return !r.effect.value() && !r.sequence && !r.sign_by_ms && !r.minimum_index && !r.handle_size && r.payload_size && r.payload_size<=64 &&
            !r.lookup.count && r.lookup.table==truemarkets::SolanaAccountKey{};
    return (r.operation==Operation::sign || r.operation==Operation::retrieve) && r.effect.value() && r.sequence && r.sign_by_ms && r.minimum_index>0 &&
        r.handle_size && r.handle_size<=r.handle.size() && r.payload_size && r.payload_size<=r.payload.size() &&
        std::all_of(r.handle.begin(),r.handle.begin()+r.handle_size,[](unsigned char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';}) &&
        r.lookup.count<=truemarkets::kMaxLookupPrefix && (r.lookup.count==0)==(r.lookup.table==truemarkets::SolanaAccountKey{});
}
std::size_t lookup_bytes(const Request& r)noexcept{return r.lookup.count?32+32*std::size_t{r.lookup.count}:0;}
void seal(std::span<std::byte> b)noexcept{const auto d=sha256_digest(b.first(b.size()-32));std::copy(d.bytes.begin(),d.bytes.end(),b.end()-32);}
bool envelope(std::span<const std::byte>b,std::string_view magic)noexcept{
    if(b.size()<88 || frame_length(b)!=b.size() || !std::equal(magic.begin(),magic.end(),reinterpret_cast<const char*>(b.data())))return false;
    const auto digest=sha256_digest(b.first(b.size()-32));return std::equal(digest.bytes.begin(),digest.bytes.end(),b.end()-32);
}
}
std::size_t frame_length(std::span<const std::byte> b)noexcept{
    if(b.size()<8)return 0;
    const bool request=b[0]==std::byte{'V'}&&b[1]==std::byte{'S'}&&b[2]==std::byte{'I'}&&b[3]==std::byte{'2'};
    const bool reply=b[0]==std::byte{'V'}&&b[1]==std::byte{'S'}&&b[2]==std::byte{'R'}&&b[3]==std::byte{'2'};
    const auto n=get(b.subspan(4),4);return (request&&n>=88&&n<=kRequestBytes)||(reply&&n>=88&&n<=kReplyBytes)?static_cast<std::size_t>(n):0;
}
std::size_t encode_request(const Request&r,std::span<std::byte> out)noexcept{
    if(!valid(r))return 0;const std::size_t n=88+r.handle_size+r.payload_size+lookup_bytes(r);if(out.size()<n)return 0;out=out.first(n);std::fill(out.begin(),out.end(),std::byte{});
    constexpr std::string_view magic="VSI2";std::copy(magic.begin(),magic.end(),reinterpret_cast<char*>(out.data()));put(out.subspan(4),n,4);out[8]=static_cast<std::byte>(r.operation);
    put(out.subspan(16),r.effect.value(),8);put(out.subspan(24),r.sequence,8);put(out.subspan(32),static_cast<std::uint64_t>(r.minimum_index),8);put(out.subspan(40),r.sign_by_ms,8);
    put(out.subspan(48),r.handle_size,2);put(out.subspan(50),r.payload_size,2);std::copy_n(r.handle.begin(),r.handle_size,reinterpret_cast<char*>(out.data()+56));
    std::copy_n(r.payload.begin(),r.payload_size,out.begin()+56+r.handle_size);
    put(out.subspan(52),r.lookup.count,2);
    if(r.lookup.count){std::size_t at=56+r.handle_size+r.payload_size;
        for(auto k:r.lookup.table)out[at++]=static_cast<std::byte>(k);
        for(std::size_t i=0;i<r.lookup.count;++i)for(auto k:r.lookup.addresses[i])out[at++]=static_cast<std::byte>(k);}
    seal(out);return n;
}
bool decode_request(std::span<const std::byte> b,Request&out)noexcept{
    out={};if(!envelope(b,"VSI2") || !zeros(b.subspan(9,7)) || !zeros(b.subspan(54,2)))return false;
    Request r;r.operation=static_cast<Operation>(b[8]);r.effect=EffectId{get(b.subspan(16),8)};r.sequence=get(b.subspan(24),8);r.sign_by_ms=get(b.subspan(40),8);
    const auto index=get(b.subspan(32),8);if(index>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))return false;r.minimum_index=static_cast<std::int64_t>(index);
    r.handle_size=static_cast<std::uint16_t>(get(b.subspan(48),2));r.payload_size=static_cast<std::uint16_t>(get(b.subspan(50),2));
    r.lookup.count=static_cast<std::uint16_t>(get(b.subspan(52),2));
    if(r.handle_size>r.handle.size() || r.payload_size>r.payload.size() || r.lookup.count>truemarkets::kMaxLookupPrefix ||
        b.size()!=std::size_t{88}+r.handle_size+r.payload_size+lookup_bytes(r))return false;
    std::copy_n(reinterpret_cast<const char*>(b.data()+56),r.handle_size,r.handle.begin());std::copy_n(b.begin()+56+r.handle_size,r.payload_size,r.payload.begin());
    if(r.lookup.count){auto at=56+r.handle_size+r.payload_size;
        for(auto& k:r.lookup.table)k=std::to_integer<std::uint8_t>(b[at++]);
        for(std::size_t i=0;i<r.lookup.count;++i)for(auto& k:r.lookup.addresses[i])k=std::to_integer<std::uint8_t>(b[at++]);}
    if(!valid(r))return false;out=r;return true;
}
std::size_t encode_reply(const Reply&r,std::span<std::byte>out)noexcept{
    const bool signed_result=r.code==ReplyCode::signed_once || r.code==ReplyCode::retrieved || r.code==ReplyCode::authenticated;
    if(r.request_digest.is_zero() || static_cast<unsigned>(r.code)>4 || r.der_size>72)return 0;
    swap_signing::ApiKeyStamp stamp;
    if(signed_result){if(swap_signing::encode_api_key_stamp(r.public_key,std::span(r.der).first(r.der_size),stamp)!=swap_signing::StampCode::encoded)return 0;}
    else if(r.der_size || !zeros(r.public_key) || !zeros(r.der))return 0;
    const std::size_t n=88+(signed_result?33+r.der_size:0);if(out.size()<n)return 0;out=out.first(n);std::fill(out.begin(),out.end(),std::byte{});
    constexpr std::string_view magic="VSR2";std::copy(magic.begin(),magic.end(),reinterpret_cast<char*>(out.data()));put(out.subspan(4),n,4);out[8]=static_cast<std::byte>(r.code);
    std::copy(r.request_digest.bytes.begin(),r.request_digest.bytes.end(),out.begin()+16);put(out.subspan(48),r.der_size,2);
    if(signed_result){std::copy(r.public_key.begin(),r.public_key.end(),out.begin()+56);std::copy_n(r.der.begin(),r.der_size,out.begin()+89);}
    seal(out);return n;
}
bool decode_reply(std::span<const std::byte>b,Reply&out)noexcept{
    out={};if(!envelope(b,"VSR2") || !zeros(b.subspan(9,7)) || !zeros(b.subspan(50,6)))return false;
    Reply r;r.code=static_cast<ReplyCode>(b[8]);std::copy_n(b.begin()+16,32,r.request_digest.bytes.begin());r.der_size=static_cast<std::uint16_t>(get(b.subspan(48),2));
    if(r.request_digest.is_zero() || static_cast<unsigned>(r.code)>4 || r.der_size>72)return false;
    const bool signed_result=r.code==ReplyCode::signed_once || r.code==ReplyCode::retrieved || r.code==ReplyCode::authenticated;
    if(b.size()!=std::size_t{88}+(signed_result?33+r.der_size:0) || (!signed_result && r.der_size))return false;
    if(signed_result){std::copy_n(b.begin()+56,33,r.public_key.begin());std::copy_n(b.begin()+89,r.der_size,r.der.begin());
        if(swap_signing::encode_api_key_stamp(r.public_key,std::span(r.der).first(r.der_size),r.stamp)!=swap_signing::StampCode::encoded)return false;}
    out=r;return true;
}
}
