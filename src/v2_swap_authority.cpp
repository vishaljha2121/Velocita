#include "velocita/v2/swap_authority.hpp"
#include "velocita/v2/swap_kernel.hpp"
#include "velocita/v2/digest.hpp"

namespace velocita::v2 {
Digest256 swap_signing_authority_digest(const swap::Pending& p)noexcept{
    std::array<std::byte,swap::max_record_bytes> bytes{};
    const auto encoded=swap::encode({swap::Role::strategy,p.intent.source_time,p.intent},bytes);
    if(encoded.error!=swap::CodecError::none || p.payload.is_zero() || p.handle.is_zero())return {};
    constexpr std::string_view domain="velocita.swap.signing.authority.v1";
    Sha256 hash;hash.update(std::as_bytes(std::span(domain.data(),domain.size())));
    hash.update(std::span(bytes).first(encoded.size));hash.update(p.payload.bytes);hash.update(p.handle.bytes);return hash.finish();
}
bool swap_authority_matches(const TradingState& state,const AuthorityBarrier& r,const Digest256& domain)noexcept{
    const auto& s=state.spot_swap;const auto& p=s.pending;
    return !domain.is_zero() && r.authority_domain==domain && swap_state_bindings_valid(state) &&
        s.initialized && !s.conflicted && p.phase==swap::Phase::prepared &&
        s.mode!=swap::Mode::halted && (p.intent.side!=Side::buy || s.mode==swap::Mode::normal) &&
        r.effect_id==p.intent.effect && r.catalog_root==s.opening.binding.catalog &&
        r.expected_kill_epoch.value()==s.kill_epoch && r.expected_executor_generation.value()==s.executor_generation &&
        p.intent.kill_epoch==s.kill_epoch && p.intent.executor_generation==s.executor_generation &&
        s.event_time<p.intent.sign_by && !r.expected_effect_payload_digest.is_zero() &&
        r.expected_effect_payload_digest==swap_signing_authority_digest(p);
}
}
