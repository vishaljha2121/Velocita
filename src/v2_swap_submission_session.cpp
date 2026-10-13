#include "velocita/v2/swap_submission_session.hpp"
#include "velocita/v2/swap_kernel.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <limits>
#if !defined(VELOCITA_SWAP_SUBMISSION_SESSION_TEST_ONLY) && !defined(VELOCITA_SWAP_RUNTIME_WIRING)
#error "Submission session requires isolated fixtures until signer and venue contracts are qualified"
#endif

namespace velocita::v2 {
SwapSubmissionSession::SwapSubmissionSession(GatewayLink& l, SwapSubmissionStore& s,
    SwapExecutionPort& p, SwapSubmissionSessionConfig c) : link_(l), store_(s), port_(p), config_(c) {
    if(store_.status()!=SwapSubmissionCode::none || !config_.state_timeout ||
        config_.binding.incarnation.is_zero() || config_.authority_domain.is_zero() ||
        store_.latest().binding!=SwapSubmissionBinding{config_.binding.incarnation,config_.authority_domain}) return;
    status_=store_.latest().phase==SwapSubmissionPhase::execute_intent ?
        SwapSubmissionSessionStatus::needs_reconciliation : SwapSubmissionSessionStatus::idle;
}
SwapSubmissionSession::~SwapSubmissionSession() {
    if(status_==SwapSubmissionSessionStatus::reading_state)link_.abandon();
    erase_request();
}
bool SwapSubmissionSession::start(const SwapSubmissionIdentity& identity,std::span<const std::byte> request,
    std::int64_t minimum_commit_index,Tick now) {
    if(status_!=SwapSubmissionSessionStatus::idle || link_.busy())return false;
    if(now>std::numeric_limits<Tick>::max()-config_.state_timeout ||
        store_.status()!=SwapSubmissionCode::none || store_.latest().phase==SwapSubmissionPhase::execute_intent ||
        identity.effect.value()<=store_.latest().identity.effect.value() || minimum_commit_index<=store_.latest().committed_index ||
        identity.payload.is_zero() || identity.handle_size==0 || identity.handle_size>identity.handle.size() ||
        request.empty() || request.size()>request_.size() || sha256_digest(request)!=identity.execution_request ||
        sha256_digest(std::span(identity.handle).first(identity.handle_size))!=identity.handle_digest ||
        !std::all_of(identity.handle.begin()+static_cast<std::ptrdiff_t>(identity.handle_size),identity.handle.end(),
            [](auto b){return b==std::byte{};})) {
        (void)refuse(SwapSubmissionSessionStatus::refused);return false;
    }
    identity_=identity;minimum_index_=minimum_commit_index;last_tick_=now;deadline_=now+config_.state_timeout;
    std::copy(request.begin(),request.end(),request_.begin());request_size_=request.size();
    if(!link_.request_state(now,config_.state_timeout)) {
        (void)refuse(SwapSubmissionSessionStatus::refused);return false;
    }
    status_=SwapSubmissionSessionStatus::reading_state;return true;
}
SwapSubmissionSessionStatus SwapSubmissionSession::poll(Tick now,std::uint64_t event_time) {
    if(status_!=SwapSubmissionSessionStatus::reading_state)return status_;
    if(now<last_tick_)return refuse(SwapSubmissionSessionStatus::refused);
    if(now>=deadline_)return refuse(SwapSubmissionSessionStatus::refused);
    last_tick_=now;
    if(store_.status()!=SwapSubmissionCode::none)return refuse(SwapSubmissionSessionStatus::storage_failed);
    const auto outcome=link_.pump(now);
    if(outcome.status==LinkStatus::pending || outcome.status==LinkStatus::idle)return status_;
    if(outcome.status!=LinkStatus::done || outcome.request!=LinkRequest::state ||
        outcome.reading.authority_domain!=config_.authority_domain || outcome.reading.applied_index<minimum_index_ ||
        outcome.reading.state_digest.is_zero())return refuse(SwapSubmissionSessionStatus::refused);
    const auto& state=link_.state();const auto& swap=state.spot_swap;const auto& pending=swap.pending;
    if(!swap_state_bindings_valid(state) || !swap.initialized || swap.conflicted ||
        swap.opening.binding!=config_.binding || pending.phase!=swap::Phase::attempted ||
        pending.intent.effect!=identity_.effect || pending.payload!=identity_.payload || pending.handle!=identity_.handle_digest ||
        swap.mode==swap::Mode::halted || (pending.intent.side==Side::buy && swap.mode!=swap::Mode::normal) ||
        pending.intent.kill_epoch!=swap.kill_epoch || pending.intent.executor_generation!=swap.executor_generation ||
        event_time<swap.event_time || event_time<pending.intent.source_time || event_time>=pending.intent.sign_by)
        return refuse(SwapSubmissionSessionStatus::refused);
    if(store_.begin(identity_,outcome.reading.applied_index)!=SwapSubmissionCode::none)
        return refuse(SwapSubmissionSessionStatus::storage_failed);
    status_=SwapSubmissionSessionStatus::needs_reconciliation;
    try { port_.execute(identity_,std::span(request_).first(request_size_)); } catch(...) {}
    erase_request();return status_;
}
void SwapSubmissionSession::erase_request() noexcept {
    volatile std::byte* bytes=request_.data();for(std::size_t n=0;n<request_.size();++n)bytes[n]=std::byte{};
    request_size_=0;
}
SwapSubmissionSessionStatus SwapSubmissionSession::refuse(SwapSubmissionSessionStatus s) noexcept {
    if(status_==SwapSubmissionSessionStatus::reading_state)link_.abandon();
    status_=s;erase_request();return s;
}
}
