#include "velocita/v2/swap_order_status.hpp"
#include <algorithm>

#if !defined(VELOCITA_SWAP_ORDER_STATUS_TEST_ONLY) && !defined(VELOCITA_SWAP_RUNTIME_WIRING)
#error "Order HTTPS adapter requires isolated qualification before production use"
#endif

namespace velocita::v2 {
namespace {
transport::SwapHttpsConfig order_config(const truemarkets::TmText& handle,long timeout){
    transport::SwapHttpsConfig c;c.origin="https://api.truemarkets.co";c.timeout_ms=timeout;
    c.max_response_bytes=truemarkets::kMaxTmResponseBytes;
    if(!handle.length || handle.length>handle.bytes.size() || !std::all_of(handle.view().begin(),handle.view().end(),[](unsigned char v){
        return (v>='a' && v<='z') || (v>='A' && v<='Z') || (v>='0' && v<='9') || v=='-' || v=='_';}))return c;
    c.routes[0]={transport::SwapHttpsMethod::get,"/v1/gateway/orders/"+std::string(handle.view())};c.route_count=1;return c;
}
}
SwapHttpsOrderGet::SwapHttpsOrderGet(SwapOrderSessionIdentity identity,std::string bearer,const truemarkets::TmText& handle,long timeout)
    :SwapHttpsOrderGet(identity,std::move(bearer),order_config(handle,timeout)){}
SwapHttpsOrderGet::SwapHttpsOrderGet(SwapOrderSessionIdentity identity,std::string bearer,transport::SwapHttpsConfig config)
    :client_(std::move(config)),identity_(identity),bearer_(std::move(bearer)){}
SwapHttpsOrderGet::~SwapHttpsOrderGet(){volatile char* p=bearer_.data();for(std::size_t n=0;n<bearer_.size();++n)p[n]=0;}
transport::SwapHttpsResult SwapHttpsOrderGet::get(std::string_view path){
    if(identity_.account_and_assets.is_zero() || identity_.authenticated_session.is_zero() || bearer_.empty()){
        transport::SwapHttpsResult refused;refused.status=transport::SwapHttpsStatus::local_refusal;return refused;
    }
    return client_.exchange({transport::SwapHttpsMethod::get,path,bearer_,{}});
}
#ifdef VELOCITA_SWAP_ORDER_STATUS_TEST_ONLY
std::unique_ptr<SwapHttpsOrderGet> SwapHttpsOrderGet::local_fixture(SwapOrderSessionIdentity identity,std::string bearer,
    const truemarkets::TmText& handle,std::string origin,std::string ca){
    if(!origin.starts_with("https://localhost:") && !origin.starts_with("https://127.0.0.1:"))return {};
    auto config=order_config(handle,2000);config.origin=std::move(origin);config.ca_bundle=std::move(ca);
    return std::unique_ptr<SwapHttpsOrderGet>(new SwapHttpsOrderGet(identity,std::move(bearer),std::move(config)));
}
#endif
}
