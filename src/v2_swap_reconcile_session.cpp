#include "velocita/v2/swap_reconcile_session.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/truemarkets_wire.hpp"

#include <algorithm>
#include <limits>

#ifndef VELOCITA_SWAP_RECONCILE_SESSION_TEST_ONLY
#error "Swap reconciliation session requires explicit test-only opt-in"
#endif

namespace velocita::v2 {
namespace {
bool identity_valid(const SwapBalanceSessionIdentity& identity) noexcept {
    return !identity.account_and_assets_digest.is_zero() && !identity.session_evidence_digest.is_zero();
}
Digest256 request_digest(Digest256 nonce, std::uint64_t sequence) noexcept {
    Sha256 hash; hash.update(nonce.bytes);
    std::array<std::byte,8> bytes{};
    for(unsigned i=0;i<8;++i) bytes[i]=static_cast<std::byte>(sequence>>(8U*i));
    hash.update(bytes);return hash.finish();
}
transport::SwapHttpsConfig balance_config(long timeout_ms) {
    transport::SwapHttpsConfig config;
    config.origin="https://api.truemarkets.co";
    config.timeout_ms=timeout_ms;
    config.max_response_bytes=truemarkets::kMaxTmResponseBytes;
    config.routes[0]={transport::SwapHttpsMethod::get,"/v1/gateway/balances"};
    config.route_count=1;
    return config;
}
}
SwapHttpsBalanceGet::SwapHttpsBalanceGet(SwapBalanceSessionIdentity identity, std::string bearer, long timeout_ms)
    : client_(balance_config(timeout_ms)), identity_(identity), bearer_(std::move(bearer)) {}
SwapHttpsBalanceGet::~SwapHttpsBalanceGet() {
    volatile char* cursor=bearer_.data();
    for(std::size_t i=0;i<bearer_.size();++i)cursor[i]='\0';
}
transport::SwapHttpsResult SwapHttpsBalanceGet::get() {
    if(!identity_valid(identity_) || bearer_.empty()) {
        transport::SwapHttpsResult refused;refused.status=transport::SwapHttpsStatus::local_refusal;return refused;
    }
    return client_.exchange({transport::SwapHttpsMethod::get,"/v1/gateway/balances",bearer_,{}});
}
SwapReconcileSession::SwapReconcileSession(GatewayLink& link, SwapBalanceGet& http, SwapReconcileClock& clock,
                                          SwapResultWatermark& watermark, SwapReconcileSessionConfig config)
    : link_(link), http_(http), clock_(clock), watermark_(watermark), config_(std::move(config)) {
    partition_=outside_balance_digest(config_.outside);
    valid_=identity_valid(config_.http_identity) &&
        config_.http_identity.account_and_assets_digest==config_.binding.account_and_assets_digest &&
        config_.outside.account_and_assets_digest==config_.binding.account_and_assets_digest &&
        config_.outside.clock_epoch==config_.clock_epoch && !partition_.is_zero() &&
        !config_.authority_domain.is_zero() && !config_.clock_epoch.is_zero() && !config_.session_nonce.is_zero() &&
        config_.max_age_ms>0 && config_.max_age_ms<=30'000 && config_.state_timeout_ms>0 &&
        config_.state_timeout_ms<=30'000 && watermark_.incarnation()==config_.binding.allocation.incarnation;
}
bool SwapReconcileSession::start() {
    if(result_.status==SwapReconcileSessionStatus::reading_state) return false;
    result_={};
    if(!valid_ || watermark_.index()<=0 || link_.busy() || sequence_==std::numeric_limits<std::uint64_t>::max()) return false;
    if(http_.identity()!=config_.http_identity) { result_.status=SwapReconcileSessionStatus::identity_refused;return false; }
    started_ms_=clock_.now_ms();
    if(started_ms_==0 || started_ms_>std::numeric_limits<std::uint64_t>::max()-config_.state_timeout_ms ||
       config_.outside.declared_at_ms>started_ms_) return false;
    request_id_=request_digest(config_.session_nonce,++sequence_);
    if(!link_.request_state(started_ms_,config_.state_timeout_ms)) return false;
    result_.status=SwapReconcileSessionStatus::reading_state;return true;
}
SwapReconcileSessionStatus SwapReconcileSession::poll() {
    if(result_.status!=SwapReconcileSessionStatus::reading_state) return result_.status;
    const auto now=clock_.now_ms();
    if(now<started_ms_ || watermark_.index()<=0) {
        link_.abandon();result_.status=SwapReconcileSessionStatus::closed;return result_.status;
    }
    const auto outcome=link_.pump(now);
    if(outcome.status==LinkStatus::pending || outcome.status==LinkStatus::idle) return result_.status;
    result_.status=SwapReconcileSessionStatus::state_refused;
    if(outcome.request!=LinkRequest::state || outcome.status!=LinkStatus::done || outcome.reading.applied_index<=0 ||
       outcome.reading.state_digest.is_zero() || now-started_ms_>config_.max_age_ms) return result_.status;
    if(outcome.reading.authority_domain!=config_.authority_domain || http_.identity()!=config_.http_identity) {
        result_.status=SwapReconcileSessionStatus::identity_refused;return result_.status;
    }
    result_.reading=outcome.reading;
    result_.captured=link_.state().spot_swap;
    const auto projected=expected_swap_balance(result_.captured,config_.binding);
    const auto before_http=watermark_.index();
    if(before_http<=0) { result_.status=SwapReconcileSessionStatus::closed;return result_.status; }
    if(outcome.reading.applied_index<before_http) {
        result_.comparison.code=SwapReconcileCode::state_watermark;
        result_.status=SwapReconcileSessionStatus::comparison_refused;return result_.status;
    }
    if(projected.code!=SwapBalanceCode::ready_for_comparison) {
        result_.comparison.code=SwapReconcileCode::projection_refused;result_.comparison.projection_code=projected.code;
        result_.status=SwapReconcileSessionStatus::comparison_refused;return result_.status;
    }
    const CommittedBalanceReadBarrier barrier{config_.binding.allocation.incarnation,config_.clock_epoch,
        result_.reading.state_digest,static_cast<std::uint64_t>(result_.reading.applied_index),started_ms_,now};
    const auto http_start=clock_.now_ms();
    if(http_start<now || http_start-started_ms_>config_.max_age_ms) {
        result_.status=SwapReconcileSessionStatus::comparison_refused;return result_.status;
    }
    transport::SwapHttpsResult response;
    try { response=http_.get(); }
    catch(...) { result_.status=SwapReconcileSessionStatus::http_refused;return result_.status; }
    const auto received=clock_.now_ms();
    result_.end_result_watermark=watermark_.index();
    if(result_.end_result_watermark<=0) { result_.status=SwapReconcileSessionStatus::closed;return result_.status; }
    if(http_.identity()!=config_.http_identity) { result_.status=SwapReconcileSessionStatus::identity_refused;return result_.status; }
    if(response.status!=transport::SwapHttpsStatus::complete || response.exposure!=transport::SwapHttpsExposure::response_received ||
       response.http_status!=200 || response.body_size>response.body.size() || response.body_size>truemarkets::kMaxTmResponseBytes) {
        result_.status=SwapReconcileSessionStatus::http_refused;return result_.status;
    }
    const BalanceReadContext context{config_.binding.account_and_assets_digest,config_.clock_epoch,request_id_,partition_,http_start,received};
    const SwapReconcileCut cut{config_.binding.allocation.incarnation,config_.clock_epoch,request_id_,partition_,
        static_cast<std::uint64_t>(result_.end_result_watermark),received,config_.max_age_ms};
    result_.comparison=reconcile_swap_balance(result_.captured,config_.binding,config_.outside,barrier,context,cut,
                                              {response.body.data(),response.body_size});
    result_.status=result_.comparison.code==SwapReconcileCode::compared_snapshot_only &&
        result_.comparison.comparison.verdict==SpotBalanceVerdict::matching_snapshot_only?
        SwapReconcileSessionStatus::complete:SwapReconcileSessionStatus::comparison_refused;
    return result_.status;
}
}
