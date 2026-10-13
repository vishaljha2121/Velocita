#include "velocita/v2/swap_runtime_https.hpp"
#include "velocita/v2/swap_order_status.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#if !defined(VELOCITA_SWAP_RUNTIME_HTTPS_TEST_ONLY) && !defined(VELOCITA_SWAP_RUNTIME_WIRING)
#error "Runtime HTTPS ports require isolated qualification before release integration"
#endif
namespace velocita::v2 {
namespace {
using namespace transport;
bool pinned(const SwapSubmissionIdentity&i,const truemarkets::SwapRestRequest&r){
    if(!i.effect.value() || i.payload.is_zero() || i.execution_request.is_zero() || i.handle_digest.is_zero() ||
        !i.handle_size || i.handle_size>truemarkets::kMaxTmTextBytes || i.handle_size>i.handle.size() ||
        !r.path_size || r.path_size>r.path.size() || !r.body_size || r.body_size>r.body.size())return false;
    const auto handle=std::span(i.handle).first(i.handle_size);
    if(!std::all_of(handle.begin(),handle.end(),[](std::byte b){const auto c=std::to_integer<unsigned>(b);return
        (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';}) || sha256_digest(handle)!=i.handle_digest)return false;
    const auto path="/v1/gateway/orders/"+std::string(reinterpret_cast<const char*>(handle.data()),handle.size())+"/execute";
    return r.path_view()==path && sha256_digest(std::as_bytes(std::span(r.body.data(),r.body_size)))==i.execution_request;
}
SwapHttpsConfig execute_config(const SwapSubmissionIdentity&i,const truemarkets::SwapRestRequest&r,long timeout){
    SwapHttpsConfig c;c.origin="https://api.truemarkets.co";c.timeout_ms=timeout;
    if(timeout<1 || timeout>60000 || !pinned(i,r))return c;
    c.routes[0]={SwapHttpsMethod::post,std::string(r.path_view())};c.route_count=1;return c;
}
SwapHttpsConfig rpc_config(){SwapHttpsConfig c;c.origin="https://api.mainnet-beta.solana.com";c.routes[0]={SwapHttpsMethod::post,"/"};c.route_count=1;c.max_response_bytes=kSettlementRpcBytes;return c;}
#ifdef VELOCITA_SWAP_RUNTIME_HTTPS_TEST_ONLY
bool local(std::string_view origin)noexcept{
    constexpr std::string_view a="https://localhost:",b="https://127.0.0.1:";
    if(origin.starts_with(a))origin.remove_prefix(a.size());else if(origin.starts_with(b))origin.remove_prefix(b.size());else return false;
    if(origin.empty() || origin.size()>5)return false;unsigned port{};const auto result=std::from_chars(origin.data(),origin.data()+origin.size(),port);
    return result.ec==std::errc{} && result.ptr==origin.data()+origin.size() && port>0 && port<=65535;
}
#endif
bool canonical_rpc(std::string_view body)noexcept{
    constexpr std::string_view prefix="{\"jsonrpc\":\"2.0\",\"id\":";
    constexpr std::string_view middle=",\"method\":\"getTransaction\",\"params\":[\"";
    constexpr std::string_view suffix="\",{\"encoding\":\"base64\",\"commitment\":\"finalized\",\"maxSupportedTransactionVersion\":0}]}";
    if(body.size()>512 || !body.starts_with(prefix))return false;body.remove_prefix(prefix.size());
    const auto comma=body.find(',');if(comma==body.npos || comma==0 || comma>19 || body[0]=='0')return false;
    std::uint64_t id{};const auto read=std::from_chars(body.data(),body.data()+comma,id);
    if(read.ec!=std::errc{} || read.ptr!=body.data()+comma || !id || id>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))return false;
    body.remove_prefix(comma);if(!body.starts_with(middle) || !body.ends_with(suffix))return false;
    body.remove_prefix(middle.size());if(body.size()<suffix.size())return false;body.remove_suffix(suffix.size());
    return decode_solana_signature(body).has_value();
}
}
SwapRuntimeExecutionHttps::SwapRuntimeExecutionHttps(SwapGatewayCredentials&c,SwapFinalizedReadClock&t,SwapSubmissionIdentity i,truemarkets::SwapRestRequest r,long timeout)
    :SwapRuntimeExecutionHttps(c,t,i,r,execute_config(i,r,timeout)){}
SwapRuntimeExecutionHttps::SwapRuntimeExecutionHttps(SwapGatewayCredentials&c,SwapFinalizedReadClock&t,SwapSubmissionIdentity i,truemarkets::SwapRestRequest r,SwapHttpsConfig cfg)
    :credentials_(c),clock_(t),expected_(i),request_(r),client_(cfg),valid_(cfg.route_count==1 && cfg.timeout_ms>0 && cfg.timeout_ms<=60000 && pinned(i,r)){
    result_.status=SwapHttpsStatus::local_refusal;
}
void SwapRuntimeExecutionHttps::execute(const SwapSubmissionIdentity&i,std::span<const std::byte>body){
    if(used_)return;used_=true;
    if(!valid_ || i!=expected_ || body.size()!=request_.body_size ||
        !std::equal(body.begin(),body.end(),reinterpret_cast<const std::byte*>(request_.body.data())))return;
    const auto token=credentials_.bearer(clock_.now_ms());if(token.empty())return;
    result_.status=SwapHttpsStatus::transport_failure;result_.exposure=SwapHttpsExposure::possibly_sent;
    try{result_=client_.exchange({SwapHttpsMethod::post,request_.path_view(),token,request_.body_view()});}catch(...){}
}
#ifdef VELOCITA_SWAP_RUNTIME_HTTPS_TEST_ONLY
std::unique_ptr<SwapRuntimeExecutionHttps> SwapRuntimeExecutionHttps::local_fixture(SwapGatewayCredentials&c,SwapFinalizedReadClock&t,SwapSubmissionIdentity i,
    truemarkets::SwapRestRequest r,std::string origin,std::string ca,long timeout){
    if(!local(origin))return {};auto cfg=execute_config(i,r,timeout);cfg.origin=std::move(origin);cfg.ca_bundle=std::move(ca);
    return std::unique_ptr<SwapRuntimeExecutionHttps>(new SwapRuntimeExecutionHttps(c,t,i,r,std::move(cfg)));
}
#endif
SwapRuntimeFinalizedHttps::SwapRuntimeFinalizedHttps(std::uint64_t n):SwapRuntimeFinalizedHttps(rpc_config(),n){}
SwapRuntimeFinalizedHttps::SwapRuntimeFinalizedHttps(SwapHttpsConfig c,std::uint64_t n):config_(std::move(c)),maximum_timeout_(n){
    if(!n || n>60000)return;
    const auto identity="velocita.finalized-rpc.v1\n"+config_.origin+"\n"+config_.ca_bundle;
    provider_=sha256_digest(std::as_bytes(std::span(identity.data(),identity.size())));
}
SettlementRpcResponse SwapRuntimeFinalizedHttps::request(std::string_view body,std::uint64_t timeout){
    if(provider_.is_zero() || !timeout || timeout>maximum_timeout_ || !canonical_rpc(body))return {};
    auto config=config_;config.timeout_ms=static_cast<long>(timeout);SwapHttpsClient client(std::move(config));
    const auto response=client.exchange({SwapHttpsMethod::post,"/",{},body});SettlementRpcResponse result;
    if(response.status!=SwapHttpsStatus::complete || response.exposure!=SwapHttpsExposure::response_received || response.body_size>result.body.size())return result;
    result.transport_complete=true;result.http_status=response.http_status;result.size=response.body_size;
    std::copy_n(response.body.begin(),response.body_size,result.body.begin());return result;
}
#ifdef VELOCITA_SWAP_RUNTIME_HTTPS_TEST_ONLY
std::unique_ptr<SwapRuntimeFinalizedHttps> SwapRuntimeFinalizedHttps::local_fixture(std::string origin,std::string ca,std::uint64_t n){
    if(!local(origin))return {};auto config=rpc_config();config.origin=std::move(origin);config.ca_bundle=std::move(ca);
    return std::unique_ptr<SwapRuntimeFinalizedHttps>(new SwapRuntimeFinalizedHttps(std::move(config),n));
}
#endif
}
