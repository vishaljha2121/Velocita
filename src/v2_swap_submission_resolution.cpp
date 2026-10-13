#include "velocita/v2/swap_submission_resolution.hpp"
#include "velocita/v2/swap_kernel.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

#if !defined(VELOCITA_SWAP_SUBMISSION_RESOLUTION_TEST_ONLY) && !defined(VELOCITA_SWAP_RUNTIME_WIRING)
#error "Submission resolution requires isolated qualification before production gateway integration"
#endif

namespace velocita::v2 {
SwapSubmissionResolutionSession::SwapSubmissionResolutionSession(GatewayLink& link,SwapSubmissionStore& store,
    SwapSubmissionResolutionConfig config):link_(link),store_(store),config_(config) {
    if(store_.status()!=SwapSubmissionCode::none || !config_.state_timeout || config_.binding.incarnation.is_zero() ||
        config_.authority_domain.is_zero() ||
        store_.latest().binding!=SwapSubmissionBinding{config_.binding.incarnation,config_.authority_domain})return;
    if(store_.latest().phase==SwapSubmissionPhase::execute_intent)result_.status=SwapSubmissionResolutionStatus::idle;
    if(store_.latest().phase==SwapSubmissionPhase::resolved)result_.status=SwapSubmissionResolutionStatus::resolved;
}
SwapSubmissionResolutionSession::~SwapSubmissionResolutionSession(){
    if(result_.status==SwapSubmissionResolutionStatus::reading_state)link_.abandon();
}
SwapSubmissionResolutionStatus SwapSubmissionResolutionSession::finish(SwapSubmissionResolutionStatus status) noexcept {
    if(result_.status==SwapSubmissionResolutionStatus::reading_state)link_.abandon();
    result_.status=status;return status;
}
bool SwapSubmissionResolutionSession::journal_unchanged() const noexcept {
    const auto& now=store_.latest();
    return store_.status()==SwapSubmissionCode::none && now.ordinal==captured_.ordinal && now.checksum==captured_.checksum &&
        now.phase==SwapSubmissionPhase::execute_intent && now.binding==captured_.binding && now.identity==captured_.identity &&
        now.transaction==captured_.transaction && now.committed_index==captured_.committed_index && now.evidence==captured_.evidence;
}
bool SwapSubmissionResolutionSession::start(std::int64_t minimum_commit_index,Tick now){
    if(result_.status!=SwapSubmissionResolutionStatus::idle || link_.busy())return false;
    if(store_.status()!=SwapSubmissionCode::none || store_.latest().phase!=SwapSubmissionPhase::execute_intent ||
        store_.latest().binding!=SwapSubmissionBinding{config_.binding.incarnation,config_.authority_domain} ||
        minimum_commit_index<store_.latest().committed_index || now>std::numeric_limits<Tick>::max()-config_.state_timeout){
        (void)finish(SwapSubmissionResolutionStatus::refused);return false;
    }
    captured_=store_.latest();
    minimum_index_=minimum_commit_index;last_tick_=now;deadline_=now+config_.state_timeout;
    if(!link_.request_state(now,config_.state_timeout)){
        (void)finish(SwapSubmissionResolutionStatus::refused);return false;
    }
    result_.status=SwapSubmissionResolutionStatus::reading_state;return true;
}
SwapSubmissionResolutionStatus SwapSubmissionResolutionSession::poll(Tick now){
    if(result_.status!=SwapSubmissionResolutionStatus::reading_state)return result_.status;
    if(now<last_tick_ || now>=deadline_)return finish(SwapSubmissionResolutionStatus::refused);
    last_tick_=now;
    if(store_.status()!=SwapSubmissionCode::none)return finish(SwapSubmissionResolutionStatus::storage_failed);
    if(!journal_unchanged())return finish(SwapSubmissionResolutionStatus::refused);
    const auto outcome=link_.pump(now);
    if(outcome.status==LinkStatus::pending || outcome.status==LinkStatus::idle)return result_.status;
    if(outcome.status!=LinkStatus::done || outcome.request!=LinkRequest::state ||
        outcome.reading.authority_domain!=config_.authority_domain || outcome.reading.applied_index<minimum_index_ ||
        outcome.reading.applied_index<=captured_.committed_index || outcome.reading.state_digest.is_zero())
        return finish(SwapSubmissionResolutionStatus::refused);
    const auto& state=link_.state();const auto& swap=state.spot_swap;
    if(!swap_state_bindings_valid(state) || !swap.initialized || swap.conflicted || swap.opening.binding!=config_.binding)
        return finish(SwapSubmissionResolutionStatus::refused);
    if(swap.pending.phase!=swap::Phase::none && swap.pending.intent.effect==captured_.identity.effect)
        return finish(SwapSubmissionResolutionStatus::needs_reconciliation);
    const auto known_transaction=solana_signature_transaction_digest(captured_.transaction);
    const swap::Settlement* found{};
    for(std::size_t n=0;n<swap.settlement_count;++n){
        const auto& candidate=swap.settlements[n];
        if(candidate.effect!=captured_.identity.effect)continue;
        if(found || candidate.payload!=captured_.identity.payload || candidate.custody!=config_.binding.custody ||
            !candidate.finalized || candidate.transaction.is_zero() || candidate.evidence.is_zero() ||
            (known_transaction && candidate.transaction!=*known_transaction))
            return finish(SwapSubmissionResolutionStatus::refused);
        found=&candidate;
    }
    if(!found)return finish(SwapSubmissionResolutionStatus::needs_reconciliation);
    if(store_.resolve({captured_.identity,outcome.reading.applied_index,outcome.reading.state_digest})!=SwapSubmissionCode::none)
        return finish(SwapSubmissionResolutionStatus::storage_failed);
    result_.reading=outcome.reading;result_.settlement=*found;
    return finish(SwapSubmissionResolutionStatus::resolved);
}
}
