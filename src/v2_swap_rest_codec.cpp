#include "velocita/v2/swap_rest_codec.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <charconv>
#include <limits>

namespace velocita::v2::truemarkets {
namespace {
bool valid_input(SwapRestInput input) noexcept {
    return (input.side==Side::buy || input.side==Side::sell) && input.quantity_atoms>0 &&
        input.quantity_atoms<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}
bool safe_handle(std::string_view value) noexcept {
    return !value.empty() && value.size()<=kMaxTmTextBytes && std::all_of(value.begin(),value.end(),[](unsigned char c){
        return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_';});
}
struct Writer {
    std::span<char> target;std::size_t at{};bool valid{true};
    void append(std::string_view text) noexcept {
        if(!valid || text.size()>target.size()-at){valid=false;return;}
        std::copy(text.begin(),text.end(),target.begin()+static_cast<std::ptrdiff_t>(at));at+=text.size();
    }
    void quantity(std::uint64_t atoms,std::uint8_t decimals) noexcept {
        std::uint64_t scale=1;for(unsigned n=0;n<decimals;++n)scale*=10;
        std::array<char,32> digits{};auto [end,error]=std::to_chars(digits.data(),digits.data()+digits.size(),atoms/scale);
        if(error!=std::errc{}){valid=false;return;}
        append({digits.data(),static_cast<std::size_t>(end-digits.data())});append(".");
        auto fraction=atoms%scale;for(unsigned n=0;n<decimals;++n){scale/=10;const char c=static_cast<char>('0'+fraction/scale);append({&c,1});fraction%=scale;}
    }
};
bool no_issues(std::string_view object) noexcept {
    const auto value=json_member_raw(object,"issues");if(!value || *value=="null")return true;
    std::array<std::string_view,1> entries;const auto count=json_array_elements(*value,entries);return count && *count==0;
}
int hex(char c) noexcept {if(c>='0' && c<='9')return c-'0';if(c>='a' && c<='f')return c-'a'+10;if(c>='A' && c<='F')return c-'A'+10;return -1;}
bool payload_string(std::string_view raw,SwapRestPrepared& value) noexcept {
    if(raw.size()<2 || raw.front()!='"' || raw.back()!='"')return false;
    std::size_t count{};
    for(std::size_t n=1;n+1<raw.size();++n){
        unsigned char c=static_cast<unsigned char>(raw[n]);
        if(c=='\\'){
            if(++n+1>=raw.size())return false;
            switch(raw[n]){
                case '"':c='"';break;case '\\':c='\\';break;case '/':c='/';break;
                case 'n':c='\n';break;case 'r':c='\r';break;case 't':c='\t';break;case 'b':c='\b';break;case 'f':c='\f';break;
                case 'u':{
                    if(n+4>=raw.size()-1)return false;
                    unsigned code{};for(unsigned j=0;j<4;++j){const auto h=hex(raw[++n]);if(h<0)return false;code=(code<<4)|static_cast<unsigned>(h);}
                    if(code==0 || code>127)return false;c=static_cast<unsigned char>(code);break;
                }
                default:return false;
            }
        }else if(c<32 || c>=127 || c=='"')return false;
        if(c==0 || count==value.payload.size())return false;
        value.payload[count++]=static_cast<char>(c);
    }
    value.payload_size=static_cast<std::uint16_t>(count);return count>0;
}
std::optional<std::uint64_t> amount(std::string_view object,std::string_view key,std::uint8_t decimals) noexcept {
    const auto text=json_member_string(object,key);if(!text)return {};
    const auto parsed=parse_decimal_atoms(*text,decimals);if(!parsed || *parsed<0)return {};return static_cast<std::uint64_t>(*parsed);
}
}
SwapRestCode encode_swap_rest_create(SwapRestInput input,SwapRestRequest& out) noexcept {
    out={};if(!valid_input(input))return SwapRestCode::invalid_input;
    SwapRestRequest value;Writer path{value.path},body{value.body};path.append("/v1/gateway/orders");
    body.append("{\"base_asset\":\"");body.append(kSwapSolMint);body.append("\",\"chain\":\"solana\",\"side\":\"");
    body.append(input.side==Side::buy?"buy":"sell");body.append("\",\"type\":\"market\",\"qty\":\"");
    body.quantity(input.quantity_atoms,input.side==Side::buy?6:9);body.append("\",\"qty_unit\":\"");
    body.append(input.side==Side::buy?"quote":"base");body.append("\"}");
    if(!path.valid || !body.valid)return SwapRestCode::invalid_input;
    value.path_size=static_cast<std::uint16_t>(path.at);value.body_size=static_cast<std::uint16_t>(body.at);out=value;return SwapRestCode::encoded;
}
SwapRestCode encode_swap_rest_execute(const TmText& handle,const swap_signing::ApiKeyStamp& stamp,SwapRestRequest& out) noexcept {
    out={};if(handle.length>handle.bytes.size() || !safe_handle(handle.view()))return SwapRestCode::unsafe_handle;
    if(stamp.empty())return SwapRestCode::invalid_input;
    SwapRestRequest value;Writer path{value.path},body{value.body};
    path.append("/v1/gateway/orders/");path.append(handle.view());path.append("/execute");
    body.append("{\"auth_type\":\"api_key\",\"signatures\":[\"");body.append(stamp.view());body.append("\"]}");
    if(!path.valid || !body.valid)return SwapRestCode::invalid_input;
    value.path_size=static_cast<std::uint16_t>(path.at);value.body_size=static_cast<std::uint16_t>(body.at);out=value;return SwapRestCode::encoded;
}
SwapRestCode decode_swap_rest_prepare(unsigned http,std::string_view document,SwapRestInput input,SwapRestPrepared& out) noexcept {
    out={};if(!valid_input(input))return SwapRestCode::invalid_input;
    if(http!=201)return SwapRestCode::invalid_http;
    if(validate_json_object(document)!=JsonError::none)return SwapRestCode::malformed;
    if(json_member_string(document,"status")!="initialized")return SwapRestCode::unsupported;
    const auto handle=json_member_string(document,"order_id");if(!handle || !safe_handle(*handle))return SwapRestCode::unsafe_handle;
    const auto quote=json_member_raw(document,"quote");if(!quote || validate_json_object(*quote)!=JsonError::none)return SwapRestCode::malformed;
    if(!no_issues(document) || !no_issues(*quote))return SwapRestCode::issue;
    if(json_member_string(*quote,"base_asset")!=kSwapSolMint || json_member_string(*quote,"quote_asset")!=kSwapUsdcMint ||
        json_member_string(*quote,"fee_asset")!=kSwapUsdcMint)return SwapRestCode::quote_mismatch;
    const auto qty=amount(*quote,"qty",input.side==Side::buy?6:9);
    const auto receive=amount(*quote,"qty_out",input.side==Side::buy?9:6),fee=amount(*quote,"fee",6);
    if(!qty || *qty!=input.quantity_atoms || !receive || *receive==0 || !fee)return SwapRestCode::quote_mismatch;
    const auto payloads=json_member_raw(document,"payloads");std::array<std::string_view,2> entries{};
    const auto count=payloads?json_array_elements(*payloads,entries):std::nullopt;
    if(!count || *count!=1)return SwapRestCode::unsupported;
    if(validate_json_object(entries[0])!=JsonError::none)return SwapRestCode::malformed;
    const auto raw=json_member_raw(entries[0],"payload"),digest=json_member_string(entries[0],"digest");
    SwapRestPrepared value;
    if(!raw || !payload_string(*raw,value))return SwapRestCode::malformed;
    if(!digest || digest->size()!=64 || !std::all_of(digest->begin(),digest->end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}))
        return SwapRestCode::payload_digest_mismatch;
    const auto claimed=parse_digest256(*digest);
    value.payload_digest=sha256_digest(std::as_bytes(std::span(value.payload.data(),value.payload_size)));
    if(!claimed || value.payload_digest!=*claimed)return SwapRestCode::payload_digest_mismatch;
    value.handle=*tm_text(*handle);value.quoted_input=*qty;value.quoted_output=*receive;value.quoted_fee=*fee;
    out=value;return SwapRestCode::decoded_unsigned_only;
}
}
