#include "velocita/v2/swap_account_review.hpp"
#include "velocita/v2/swap_account_setup.hpp"
#include "velocita/v2/swap_codec.hpp"
#include "velocita/v2/digest.hpp"
#include <charconv>
#include <limits>
#include <map>
#include <string>

namespace velocita::v2 {
Digest256 swap_opening_review_digest(const swap::Initialize& opening) noexcept {
    std::array<std::byte,swap::max_record_bytes> bytes{};
    const auto encoded=swap::encode({swap::Role::operator_role,0,opening},bytes);
    return encoded.error==swap::CodecError::none?sha256_digest(std::span(bytes).first(encoded.size)):Digest256{};
}
std::optional<DeclaredOutsideBalance> reviewed_outside_balance(const SwapAccountReview& r) noexcept {
    if(r.outside_base<0 || r.outside_quote<0 || r.activity_count>r.activity.size() ||
       r.document_digest.is_zero() || r.history_evidence.is_zero())return {};
    auto base=r.outside_base,quote=r.outside_quote;
    auto add=[](std::int64_t& value,std::int64_t delta){
        if(delta==std::numeric_limits<std::int64_t>::min() ||
           (delta<0 && value< -delta) || (delta>0 && value>std::numeric_limits<std::int64_t>::max()-delta))return false;
        value+=delta;return true;
    };
    for(std::size_t i=0;i<r.activity_count;++i){
        const auto& event=r.activity[i];
        if(event.evidence.is_zero() || (event.base_delta==0 && event.quote_delta==0))return {};
        for(std::size_t j=0;j<i;++j)if(event.evidence==r.activity[j].evidence)return {};
        if(!add(base,event.base_delta) || !add(quote,event.quote_delta))return {};
    }
    return DeclaredOutsideBalance{{},r.document_digest,r.history_evidence,r.reviewed_at_ms,base,quote};
}
std::optional<SwapAccountReview> decode_swap_account_review(std::string_view document,Digest256 expected) noexcept {
    try{
        if(document.empty() || document.size()>8192 || expected.is_zero() ||
           sha256_digest(std::as_bytes(std::span(document.data(),document.size())))!=expected)return {};
        SwapAccountReview r;r.document_digest=expected;
        std::map<std::string_view,std::string_view> fields;
        while(!document.empty()){
            const auto end=document.find('\n'),equal=document.find('=');const auto line=document.substr(0,end);
            if(line.empty() || equal==std::string_view::npos || !equal || equal>=line.size()-1 ||
               !fields.emplace(line.substr(0,equal),line.substr(equal+1)).second)return {};
            document=end==std::string_view::npos?std::string_view{}:document.substr(end+1);
        }
        auto get=[&](std::string_view key){auto it=fields.find(key);if(it==fields.end())throw 0;auto value=it->second;fields.erase(it);return value;};
        auto number=[&](std::string_view key,auto& out){auto s=get(key);auto result=std::from_chars(s.data(),s.data()+s.size(),out);
            if(result.ec!=std::errc{} || result.ptr!=s.data()+s.size())throw 0;};
        auto digest=[&](std::string_view key,Digest256& out){auto s=get(key);if(s.size()!=64)throw 0;
            for(std::size_t i=0;i<32;++i){unsigned n{};auto result=std::from_chars(s.data()+2*i,s.data()+2*i+2,n,16);
                if(result.ec!=std::errc{} || result.ptr!=s.data()+2*i+2)throw 0;out.bytes[i]=static_cast<std::byte>(n);}
            if(out.is_zero())throw 0;};
        if(get("schema")!="1")return {};
        digest("runtime_identity",r.runtime_identity);digest("opening_digest",r.opening_digest);digest("history_evidence",r.history_evidence);
        auto user=truemarkets::tm_text(get("user_id"));if(!user || !swap_account_user_id_valid(user->view()))return {};r.user_id=*user;
        number("reviewed_at_ms",r.reviewed_at_ms);number("expires_at_ms",r.expires_at_ms);
        number("outside_base",r.outside_base);number("outside_quote",r.outside_quote);
        unsigned count{};number("activity_count",count);if(count>r.activity.size())return {};r.activity_count=count;
        for(unsigned i=0;i<count;++i){const auto prefix="activity_"+std::to_string(i)+"_";
            digest(prefix+"evidence",r.activity[i].evidence);number(prefix+"base_delta",r.activity[i].base_delta);number(prefix+"quote_delta",r.activity[i].quote_delta);}
        if(!fields.empty() || !r.reviewed_at_ms || r.expires_at_ms<=r.reviewed_at_ms ||
           r.expires_at_ms-r.reviewed_at_ms>900'000 || !reviewed_outside_balance(r))return {};
        return r;
    }catch(...){return {};}
}
bool swap_account_review_matches(const SwapAccountReview& r,const SwapRuntimeBinding& b,std::string_view user,const swap::State& state) noexcept {
    const auto identity=swap_runtime_identity_digest(b),opening=swap_opening_review_digest(state.opening);
    return !identity.is_zero() && !opening.is_zero() && swap::valid_state(state) && state.initialized && !state.conflicted &&
        state.opening.binding==b.allocation && r.runtime_identity==identity && r.opening_digest==opening &&
        !user.empty() && r.user_id.view()==user && reviewed_outside_balance(r).has_value();
}
bool swap_account_review_current(const SwapAccountReview& r,std::uint64_t now) noexcept {
    return r.reviewed_at_ms && r.reviewed_at_ms<=now && now<r.expires_at_ms &&
        r.expires_at_ms-r.reviewed_at_ms<=900'000;
}
bool swap_reviewed_balance_matches(const SwapAccountReview& r,const SwapRuntimeBinding& b,const swap::State& state,const CommittedBalanceReadBarrier& barrier,
    std::string_view response,std::uint64_t started,std::uint64_t received) noexcept {
    if(!swap_account_review_matches(r,b,r.user_id.view(),state) || !swap_account_review_current(r,started) ||
       !swap_account_review_current(r,received) || received<started || received-started>30'000)return false;
    auto outside=reviewed_outside_balance(r);if(!outside)return false;
    outside->account_and_assets_digest=b.account_session.account_and_assets_digest;
    const auto partition=outside_balance_digest(*outside);
    const SwapBalanceBinding binding{b.allocation,b.account_session.account_and_assets_digest,1};
    const auto epoch=r.document_digest;
    const auto result=reconcile_swap_balance(state,binding,*outside,
        barrier,
        {binding.account_and_assets_digest,epoch,epoch,partition,started,received},
        {b.allocation.incarnation,epoch,epoch,partition,barrier.applied_index,received,30'000},response);
    return result.code==SwapReconcileCode::compared_snapshot_only &&
        result.comparison.verdict==SpotBalanceVerdict::matching_snapshot_only;
}
}
