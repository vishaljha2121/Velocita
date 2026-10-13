#include "velocita/v2/swap_account_history.hpp"
#include "velocita/v2/digest.hpp"
#include <charconv>
#include <limits>

namespace velocita::v2 {
std::optional<SwapAccountHistoryCut> decode_swap_account_history_cut(std::string_view body,Digest256 expected) noexcept {
    using namespace truemarkets;
    if(body.empty() || body.size()>4096 || expected.is_zero() || sha256_digest(std::as_bytes(std::span(body)))!=expected ||
       validate_json_object(body)!=JsonError::none)return {};
    if(json_member_raw(body,"schema")!="1" || json_member_string(body,"scope")!="sol-usdc-current-accounts" ||
       json_member_raw(body,"stable_window")!="true" || json_member_raw(body,"pagination_exhausted")!="true" ||
       json_member_raw(body,"blocking_orders")!="0" || json_member_raw(body,"unsupported_accounts")!="0")return {};
    SwapAccountHistoryCut cut;cut.document=expected;
    auto digest=[&](std::string_view key,Digest256& out){auto s=json_member_string(body,key);if(!s || s->size()!=64)return false;
        for(std::size_t i=0;i<32;++i){unsigned n{};auto parsed=std::from_chars(s->data()+2*i,s->data()+2*i+2,n,16);
            if(parsed.ec!=std::errc{} || parsed.ptr!=s->data()+2*i+2)return false;out.bytes[i]=static_cast<std::byte>(n);}
        return !out.is_zero();};
    auto integer=[&](std::string_view key,std::uint64_t& out){auto s=json_member_raw(body,key);if(!s)return false;
        auto parsed=std::from_chars(s->data(),s->data()+s->size(),out);return parsed.ec==std::errc{} && parsed.ptr==s->data()+s->size();};
    if(!digest("owner_sha256",cut.owner) || !digest("user_id_sha256",cut.user) || !digest("setup_sha256",cut.setup) ||
       !digest("balances_sha256",cut.balances) || !digest("evidence_manifest_sha256",cut.manifest) ||
       !integer("capture_started_ms",cut.started_ms) || !integer("capture_finished_ms",cut.finished_ms) ||
       !integer("start_slot",cut.start_slot) || !integer("end_slot",cut.end_slot) ||
       !integer("base_atoms",cut.base_atoms) || !integer("quote_atoms",cut.quote_atoms) ||
       !cut.started_ms || cut.finished_ms<cut.started_ms || cut.finished_ms-cut.started_ms>300'000 ||
       !cut.start_slot || cut.end_slot<cut.start_slot ||
       cut.base_atoms>static_cast<std::uint64_t>(INT64_MAX) || cut.quote_atoms>static_cast<std::uint64_t>(INT64_MAX))return {};
    return cut;
}
bool swap_account_history_identity(const SwapAccountHistoryCut& cut,const SwapAccountReview& review,const SwapRuntimeBinding& binding) noexcept {
    return !cut.document.is_zero() && cut.document==review.history_evidence &&
        cut.owner==sha256_digest(binding.owner_wallet) && cut.user==sha256_digest(std::as_bytes(std::span(review.user_id.view()))) &&
        cut.finished_ms<=review.reviewed_at_ms && review.reviewed_at_ms-cut.finished_ms<=900'000;
}
bool swap_account_history_funding(const SwapAccountHistoryCut& cut,const SwapAccountReview& review,const SwapRuntimeBinding& binding,
    const swap::State& state,std::uint64_t now) noexcept {
    if(!swap_account_history_identity(cut,review,binding) || !swap_account_review_matches(review,binding,review.user_id.view(),state) ||
       !swap_account_review_current(review,now) || now<cut.finished_ms || now-cut.finished_ms>900'000 ||
       state.pending.phase!=swap::Phase::none)return false;
    const auto outside=reviewed_outside_balance(review);if(!outside)return false;
    if(state.base.value()>cut.base_atoms || state.quote.value()>cut.quote_atoms)return false;
    return static_cast<std::uint64_t>(outside->base_atoms)==cut.base_atoms-state.base.value() &&
        static_cast<std::uint64_t>(outside->quote_atoms)==cut.quote_atoms-state.quote.value();
}
}
