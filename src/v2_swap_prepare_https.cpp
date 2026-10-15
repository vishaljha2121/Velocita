#include "velocita/v2/swap_prepare_https.hpp"
#include <utility>

#if !defined(VELOCITA_SWAP_PREPARE_HTTPS_TEST_ONLY) && !defined(VELOCITA_SWAP_RUNTIME_WIRING)
#error "Unsigned prepare HTTPS port requires isolated qualification before production use"
#endif

namespace velocita::v2 {
namespace {
transport::SwapHttpsConfig config(long timeout) {
    transport::SwapHttpsConfig c;
    c.origin="https://api.truemarkets.co"; c.timeout_ms=timeout;
    c.max_response_bytes=truemarkets::kMaxTmResponseBytes;
    c.routes[0]={transport::SwapHttpsMethod::post,"/v1/gateway/orders"}; c.route_count=1;
    return c;
}
}
SwapHttpsPreparePort::SwapHttpsPreparePort(truemarkets::SwapRestInput input,std::string bearer,long timeout)
    :SwapHttpsPreparePort(input,std::move(bearer),config(timeout)){}
SwapHttpsPreparePort::SwapHttpsPreparePort(truemarkets::SwapRestInput input,std::string bearer,transport::SwapHttpsConfig c)
    :client_(std::move(c)),bearer_(std::move(bearer)) {
    valid_=truemarkets::encode_swap_rest_create(input,expected_)==truemarkets::SwapRestCode::encoded;
}
SwapHttpsPreparePort::~SwapHttpsPreparePort() {
    volatile char* p=bearer_.data(); for(std::size_t n=0;n<bearer_.size();++n)p[n]=0;
}
transport::SwapHttpsResult SwapHttpsPreparePort::create(const truemarkets::SwapRestRequest& request,const SwapPrepareRead&) {
    transport::SwapHttpsResult refused; refused.status=transport::SwapHttpsStatus::local_refusal;
    if(!valid_ || attempted_ || bearer_.empty() || request.path_size>request.path.size() ||
        request.body_size>request.body.size() || request.path_view()!=expected_.path_view() ||
        request.body_view()!=expected_.body_view())return refused;
    attempted_=true; // A timeout or lost response never permits another POST.
    return client_.exchange({transport::SwapHttpsMethod::post,expected_.path_view(),bearer_,expected_.body_view()});
}
#ifdef VELOCITA_SWAP_PREPARE_HTTPS_TEST_ONLY
std::unique_ptr<SwapHttpsPreparePort> SwapHttpsPreparePort::local_fixture(
    truemarkets::SwapRestInput input,std::string bearer,std::string origin,std::string ca) {
    if(!origin.starts_with("https://localhost:") && !origin.starts_with("https://127.0.0.1:"))return {};
    auto c=config(2000); c.origin=std::move(origin); c.ca_bundle=std::move(ca);
    return std::unique_ptr<SwapHttpsPreparePort>(new SwapHttpsPreparePort(input,std::move(bearer),std::move(c)));
}
#endif
}
