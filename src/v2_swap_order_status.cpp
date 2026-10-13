#include "velocita/v2/swap_order_status.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <limits>

#if !defined(VELOCITA_SWAP_ORDER_STATUS_TEST_ONLY) && !defined(VELOCITA_SWAP_RUNTIME_WIRING)
#error "Order status attribution requires isolated qualification before production use"
#endif

namespace velocita::v2 {
namespace {
bool identity_valid(SwapOrderSessionIdentity value)noexcept{
    return !value.account_and_assets.is_zero() && !value.authenticated_session.is_zero();
}
bool safe_handle(std::span<const std::byte> value)noexcept{
    return !value.empty() && value.size()<=truemarkets::kMaxTmTextBytes && std::all_of(value.begin(),value.end(),[](auto b){
        const auto c=static_cast<unsigned char>(b);
        return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-';});
}
}
std::optional<SwapSubmissionTransaction> decode_solana_signature(std::string_view text) noexcept {
    if(text.empty() || text.size()>88)return {};
    constexpr std::string_view alphabet="123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    SwapSubmissionTransaction out{};std::size_t leading{};
    while(leading<text.size() && text[leading]=='1')++leading;
    for(const auto c:text){
        const auto digit=alphabet.find(c);if(digit==alphabet.npos)return {};
        unsigned carry=static_cast<unsigned>(digit);
        for(std::size_t n=out.size();n>0;--n){carry+=std::to_integer<unsigned>(out[n-1])*58;out[n-1]=static_cast<std::byte>(carry&255);carry>>=8;}
        if(carry)return {};
    }
    const auto first=std::find_if(out.begin(),out.end(),[](auto b){return b!=std::byte{};});
    const auto significant=static_cast<std::size_t>(out.end()-first);
    if(!significant || leading+significant!=out.size())return {};
    return out;
}
SwapOrderStatusReader::SwapOrderStatusReader(SwapSubmissionStore& s,const SwapPrepareStore& p,SwapOrderGet& h,SwapOrderReadClock& c,SwapOrderStatusConfig cfg)
    :store_(s),prepare_(p),http_(h),clock_(c),config_(cfg){}
const SwapOrderReadResult& SwapOrderStatusReader::read(){
    using namespace truemarkets;
    if(result_.status!=SwapOrderReadStatus::not_read)return result_;
    result_.status=SwapOrderReadStatus::refused;
    const auto start=clock_.now_ms();
    if(!config_.timeout_ms || config_.timeout_ms>30'000 || !start || start>std::numeric_limits<std::uint64_t>::max()-config_.timeout_ms ||
        !identity_valid(config_.http_identity) || http_.identity()!=config_.http_identity || store_.status()!=SwapSubmissionCode::none ||
        store_.latest().binding!=SwapSubmissionBinding{config_.binding.allocation.incarnation,config_.binding.authority_domain} ||
        store_.latest().phase!=SwapSubmissionPhase::execute_intent || prepare_.status()!=SwapPrepareStoreCode::none ||
        prepare_.latest().phase!=SwapPreparePhase::prepared || prepare_.latest().binding!=config_.binding)return result_;
    const auto captured=store_.latest();const auto& i=captured.identity;
    if(i.handle_size>i.handle.size() || !safe_handle(std::span(i.handle).first(i.handle_size)))return result_;
    const auto prepared=prepare_.latest();
    if(prepared.operation.effect!=i.effect || prepared.prepared.payload_digest!=i.payload ||
        captured.committed_index<=prepared.committed_index ||
        prepared.prepared.handle.view()!=std::string_view(reinterpret_cast<const char*>(i.handle.data()),i.handle_size))return result_;
    const auto input=prepared.operation.input;
    if((input.side!=Side::buy && input.side!=Side::sell) || !input.quantity_atoms ||
        input.quantity_atoms>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))return result_;
    const std::string_view handle{reinterpret_cast<const char*>(i.handle.data()),i.handle_size};
    std::array<char,160> path{};constexpr std::string_view prefix="/v1/gateway/orders/";
    std::copy(prefix.begin(),prefix.end(),path.begin());std::copy(handle.begin(),handle.end(),path.begin()+prefix.size());
    transport::SwapHttpsResult response;
    try{response=http_.get({path.data(),prefix.size()+handle.size()});}
    catch(...){result_.status=SwapOrderReadStatus::http_refused;return result_;}
    const auto finish=clock_.now_ms();
    if(finish<start || finish>=start+config_.timeout_ms || http_.identity()!=config_.http_identity ||
        store_.status()!=SwapSubmissionCode::none || store_.latest().checksum!=captured.checksum ||
        store_.latest().ordinal!=captured.ordinal || prepare_.status()!=SwapPrepareStoreCode::none ||
        prepare_.latest().ordinal!=prepared.ordinal || prepare_.latest().checksum!=prepared.checksum)return result_;
    if(response.status!=transport::SwapHttpsStatus::complete || response.exposure!=transport::SwapHttpsExposure::response_received ||
        response.http_status!=200 || response.body_size>response.body.size() || response.body_size>kMaxTmResponseBytes){
        result_.status=SwapOrderReadStatus::http_refused;return result_;
    }
    const RestSpotAsset asset{*tm_text(kSwapSolAssetId),*tm_text("solana"),*tm_text("SOL"),*tm_text("USDC"),{9,6}};
    const RestOrderExpectation expected{*tm_text(handle),asset.asset_id,asset.base_asset,asset.quote_asset,RestOrderType::market,
        input.side,input.side==Side::buy?QtyUnit::quote:QtyUnit::base,static_cast<std::int64_t>(input.quantity_atoms)};
    const auto disposition=assess_rest_spot_order(200,response.body_view(),{9,6},expected,&asset);
    result_.status=SwapOrderReadStatus::identity_conflict;
    if(disposition!=RestReadDisposition::working && disposition!=RestReadDisposition::terminal_needs_reconciliation)return result_;
    SwapOrderReadResult candidate;
    if(decode_rest_spot_order(response.body_view(),{9,6},candidate.detail,&asset)!=RestDecodeError::none)return result_;
    candidate.status=disposition==RestReadDisposition::working?SwapOrderReadStatus::observed_working:SwapOrderReadStatus::terminal_needs_finality;
    if(!candidate.detail.tx_hash.empty()){
        const auto signature=decode_solana_signature(candidate.detail.tx_hash.view());if(!signature)return result_;
        if(captured.transaction!=SwapSubmissionTransaction{} && captured.transaction!=*signature)return result_;
        if(store_.observe_transaction(i,*signature)!=SwapSubmissionCode::none){result_.status=SwapOrderReadStatus::storage_failed;return result_;}
        candidate.transaction=*signature;
    }
    candidate.response_digest=sha256_digest(std::as_bytes(std::span(response.body.data(),response.body_size)));
    result_=candidate;return result_;
}
}
