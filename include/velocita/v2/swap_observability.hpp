#pragma once

#include "velocita/v2/swap_gateway_runtime.hpp"
#include "velocita/v2/digest.hpp"
#include <string>

namespace velocita::v2 {

// Outside Raft apply: bounded scalar JSON for the local read-only observer.
// No wallet, handle, signature, payload or credentials are exported.
[[nodiscard]] inline std::optional<std::string> render_swap_observation(
    const SwapGatewayObservation& value, std::span<const std::byte,16> incarnation) {
    const auto& s=value.state;
    if(!s.initialized || s.conflicted || !swap::valid_state(s) || !value.observed_at_ms ||
       value.observed_at_ms<s.event_time || value.reading.applied_index<=0 ||
       value.reading.state_digest.is_zero() || value.reading.authority_domain.is_zero() ||
       s.opening.binding.incarnation!=sha256_digest(incarnation))return {};
    constexpr char hex[]="0123456789abcdef";
    std::string run;run.reserve(32);
    for(auto byte:incarnation){const auto n=std::to_integer<unsigned>(byte);run+=hex[n>>4];run+=hex[n&15];}
    std::string out="{\"schema\":2,\"incarnation\":\""+run+"\",\"observed_at_ms\":"+
        std::to_string(value.observed_at_ms)+",\"applied_index\":"+std::to_string(value.reading.applied_index)+
        ",\"base_atoms\":"+std::to_string(s.base.value())+",\"quote_atoms\":"+std::to_string(s.quote.value())+
        ",\"fees_atoms\":"+std::to_string(s.fees.value())+",\"realized_pnl_atoms\":"+std::to_string(s.realized_pnl)+
        ",\"last_effect\":"+std::to_string(s.last_effect)+",\"pending_effect\":"+
        std::to_string(s.pending.phase==swap::Phase::none?0:s.pending.intent.effect.value())+",\"settlements\":[";
    bool first=true;
    for(std::size_t n=0;n<s.settlement_count;++n){
        const auto& fill=s.settlements[n];
        if(fill.outcome!=swap::Outcome::succeeded)continue;
        // valid_state checks finalized/effect/evidence and replays exact accounting.
        const bool buy=fill.base_credit.value()!=0;
        if(!first)out+=',';
        first=false;
        out+="{\"effect\":"+std::to_string(fill.effect.value())+",\"side\":\""+(buy?"buy":"sell")+
             "\",\"base_atoms\":"+std::to_string(buy?fill.base_credit.value():fill.base_debit.value())+
             ",\"quote_atoms\":"+std::to_string(buy?fill.quote_debit.value():fill.quote_credit.value())+
             ",\"fee_atoms\":"+std::to_string(fill.fee.value())+",\"outcome\":\"succeeded\"}";
    }
    return out+"]}\n";
}
}
