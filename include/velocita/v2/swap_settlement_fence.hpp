#pragma once
#include "velocita/v2/swap_submission_store.hpp"
#include "velocita/v2/swap.hpp"

namespace velocita::v2 {
[[nodiscard]] inline bool swap_settled_predecessor(const swap::State& state,
    const SwapSubmissionRecord& record, const Digest256& custody) noexcept {
    if(!state.initialized || state.conflicted || state.settlement_count>state.settlements.size() || record.identity.effect.value()==0 ||
        (state.pending.phase!=swap::Phase::none && state.pending.intent.effect==record.identity.effect))return false;
    bool found=false;
    for(std::size_t n=0;n<state.settlement_count;++n){
        const auto& settlement=state.settlements[n];
        if(settlement.effect!=record.identity.effect)continue;
        if(found || settlement.payload!=record.identity.payload || settlement.custody!=custody ||
            !settlement.finalized || settlement.evidence.is_zero() ||
            settlement.transaction.is_zero()!=(settlement.outcome==swap::Outcome::expired_unexecuted))return false;
        found=true;
    }
    return found;
}
}
