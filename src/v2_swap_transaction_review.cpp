#include "velocita/v2/swap_transaction_review.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <charconv>
#include <limits>

namespace velocita::v2 {
namespace {
constexpr std::array<std::string_view,12> names{
    "schema","account_keys","lookup_table","lookup_slot","lookup_evidence","lookup_address_count",
    "lookup_addresses","max_envelope_age_ms","max_compute_price","max_compute_units","max_slippage_bps","expires_at_ms"};
bool number(std::string_view s,std::uint64_t& out)noexcept{
    if(s.empty() || s.front()<'0' || s.front()>'9' || (s.size()>1 && s.front()=='0'))return false;
    const auto r=std::from_chars(s.data(),s.data()+s.size(),out);
    return r.ec==std::errc{} && r.ptr==s.data()+s.size();
}
bool hex(std::string_view s,std::span<std::byte> out)noexcept{
    if(s.size()!=out.size()*2)return false;
    const auto digit=[](char c)->int {if(c>='0' && c<='9')return c-'0';if(c>='a' && c<='f')return c-'a'+10;return -1;};
    for(std::size_t i=0;i<out.size();++i){const auto hi=digit(s[2*i]),lo=digit(s[2*i+1]);if(hi<0 || lo<0)return false;out[i]=static_cast<std::byte>(hi*16+lo);}
    return true;
}
}
bool swap_mvp_buy_scope(const swap::State& state,const SwapSignerVerificationContext& context)noexcept{
    const auto& limits=state.opening.limits;
    if(!state.initialized || state.conflicted || state.opening.binding!=context.policy.binding ||
        context.policy.accepted_costs.max_output_deduction.value()>2'039'280 ||
        limits.max_purchase_total.value()!=1'000'000 || limits.max_buy_debit.value()!=1'000'000 ||
        limits.max_fee.value()!=2'000 || !limits.max_realized_loss.value() || limits.max_realized_loss.value()>500'000 ||
        state.purchases.value()>1'000'000 || state.last_effect>1 || state.settlement_count>1)return false;
    if(state.pending.phase==swap::Phase::none)return true;
    const auto& intent=state.pending.intent;
    return intent.effect.value()==1 && intent.side==Side::buy &&
        intent.max_quote_debit.value()==1'000'000 && intent.quote_input.value()==998'000 && intent.fee_cap.value()==2'000;
}
std::optional<SwapSignerVerificationContext> decode_swap_transaction_review(std::string_view document,
    const Digest256& expected,const SwapRuntimeBinding& runtime,std::string_view organization,std::string_view signer)noexcept{
    if(document.empty() || document.size()>32*1024 || expected.is_zero() || sha256_digest(std::as_bytes(std::span(document)))!=expected)return {};
    std::array<std::string_view,names.size()> fields{};
    while(!document.empty()){
        const auto end=document.find('\n');const auto line=document.substr(0,end);
        document=end==std::string_view::npos?std::string_view{}:document.substr(end+1);
        const auto equal=line.find('=');
        if(equal==std::string_view::npos || !equal || equal+1==line.size())return {};
        const auto key=line.substr(0,equal);const auto it=std::find(names.begin(),names.end(),key);
        if(it==names.end())return {};const auto index=static_cast<std::size_t>(it-names.begin());
        if(!fields[index].empty())return {};fields[index]=line.substr(equal+1);
    }
    if(std::any_of(fields.begin(),fields.end(),[](auto s){return s.empty();}) || fields[0]!="1")return {};
    const auto org=truemarkets::tm_text(organization),who=truemarkets::tm_text(signer);
    if(!org || !who)return {};
    SwapSignerVerificationContext result;result.organization=*org;result.signer=*who;
    auto& p=result.policy;auto& lookup=result.lookup;
    p.binding=runtime.allocation;p.accepted_costs.max_output_deduction=swap::BaseAtoms{runtime.max_buy_output_deduction_lamports};
    if(!hex(fields[1],std::as_writable_bytes(std::span(p.account_keys))) ||
        !hex(fields[2],std::as_writable_bytes(std::span(lookup.table))) ||
        !hex(fields[4],lookup.evidence_digest.bytes))return {};
    if(!std::equal(p.account_keys[1].begin(),p.account_keys[1].end(),runtime.owner_wallet.begin(),
        [](auto a,auto b){return a==std::to_integer<std::uint8_t>(b);}))return {};
    std::uint64_t count{},units{},slippage{};
    if(!number(fields[3],lookup.observed_slot) || !lookup.observed_slot ||
        !number(fields[5],count) || !count || count>lookup.addresses.size() ||
        !number(fields[7],p.max_envelope_age_ms) || !p.max_envelope_age_ms || p.max_envelope_age_ms>30'000 ||
        !number(fields[8],p.max_compute_price) || !number(fields[9],units) || !units || units>1'400'000 ||
        !number(fields[10],slippage) || slippage>10'000 || !number(fields[11],p.expires_at_ms) || !p.expires_at_ms ||
        p.max_compute_price>std::numeric_limits<std::uint64_t>::max()/units)return {};
    lookup.address_count=static_cast<std::uint16_t>(count);
    p.max_compute_units=static_cast<std::uint32_t>(units);p.max_slippage_bps=static_cast<std::uint16_t>(slippage);
    if(!hex(fields[6],std::as_writable_bytes(std::span(lookup.addresses).first(lookup.address_count))))return {};
    p.lookup_image_digest=truemarkets::direct_buy_lookup_digest(std::span(&lookup,1));
    if(p.lookup_image_digest.is_zero() || truemarkets::direct_buy_policy_digest(p).is_zero())return {};
    return result;
}
}
