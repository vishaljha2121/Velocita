#include "velocita/v2/swap_transaction_verifier.hpp"
#include "velocita/v2/jupiter_route_decode.hpp"
#include "velocita/v2/solana_instruction_decode.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <limits>

namespace velocita::v2::truemarkets {
namespace {
using Code=SwapTransactionVerificationCode;
constexpr auto max_signed=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
constexpr SolanaAccountKey jupiter{4,121,213,91,242,49,192,110,238,116,197,110,206,104,21,7,253,177,178,222,163,244,142,81,2,177,205,162,86,188,19,143};
constexpr SolanaAccountKey event_authority{180,63,250,39,245,215,246,74,116,192,155,31,41,88,121,222,75,9,171,54,223,201,221,81,75,50,26,167,179,140,229,232};
constexpr SolanaAccountKey wrapped_sol{6,155,136,87,254,171,129,132,251,104,127,99,70,24,192,53,218,196,57,220,26,235,59,85,152,160,240,0,0,0,0,1};
constexpr SolanaAccountKey usdc{198,250,122,243,190,219,173,58,61,101,243,106,171,201,116,49,177,187,228,194,210,246,224,228,124,166,2,3,69,47,93,97};
constexpr SolanaAccountKey lookup_authority{125,14,163,66,64,83,5,59,187,216,96,247,76,50,240,75,195,220,135,42,223,154,79,78,209,112,194,78,224,244,62,211};
constexpr SolanaAccountKey kipseli{36,116,252,210,179,53,119,156,91,10,143,10,199,77,189,139,146,186,52,103,172,51,237,99,161,80,185,53,130,139,213,137};
constexpr SolanaAccountKey byreal{6,52,210,15,27,245,109,209,100,227,193,60,120,54,164,34,158,197,171,33,255,84,82,131,144,165,23,137,243,205,187,219};
constexpr SolanaAccountKey obsidian{240,107,131,98,69,250,70,54,116,160,2,96,141,142,220,212,232,74,255,176,164,188,146,113,210,228,153,27,192,16,138,171};
constexpr SolanaAccountKey bison{159,50,218,156,31,108,18,80,192,155,115,57,35,236,195,103,13,40,99,231,212,106,156,28,244,125,253,187,208,213,254,87};
constexpr SolanaAccountKey invariant{252,57,6,111,128,78,200,77,70,90,204,12,175,206,177,222,189,251,158,71,81,227,201,158,203,84,99,172,99,205,137,201};

struct RouteContract { JupiterSwapKind kind; SolanaAccountKey program; };
constexpr std::array<RouteContract,5> contracts{{
    {JupiterSwapKind::invariant,invariant},{JupiterSwapKind::obsidian,obsidian},{JupiterSwapKind::byreal_dynamic_v3,byreal},
    {JupiterSwapKind::bison_fi_market_backed,bison},{JupiterSwapKind::kipseli_v3,kipseli}}};

void number(Sha256& hash,std::uint64_t value) noexcept {
    std::array<std::byte,8> bytes{};
    for(unsigned i=0;i<8;++i)bytes[i]=static_cast<std::byte>(value>>(8*i));
    hash.update(bytes);
}
void label(Sha256& hash,std::string_view text) noexcept {hash.update(std::as_bytes(std::span(text)));}
void account(Sha256& hash,const SolanaAccountKey& key) noexcept {hash.update(std::as_bytes(std::span(key)));}
void binding(Sha256& h,const swap::Binding& b) noexcept {
    h.update(b.incarnation.bytes);h.update(b.catalog.bytes);h.update(b.custody.bytes);
    number(h,b.account.value());number(h,b.instrument.value());number(h,b.policy.value());
    number(h,b.policy_version.value());number(h,b.risk_policy_version.value());
}
bool valid_binding(const swap::Binding& b) noexcept {
    return !b.incarnation.is_zero() && !b.catalog.is_zero() && !b.custody.is_zero() &&
        b.account.value() && b.instrument.value() && b.policy.value() && b.policy_version.value() && b.risk_policy_version.value();
}
std::array<SolanaAccountKey,5> owner_side(const SwapExecutionPolicy& p) noexcept {
    return {p.sponsor,p.owner,p.owner_usdc,p.owner_wsol,p.platform_fee};
}
bool valid_policy(const SwapExecutionPolicy& p) noexcept {
    if(!valid_binding(p.binding) || !p.max_envelope_age_ms || p.max_envelope_age_ms>30'000 ||
        !p.max_compute_units || p.max_compute_units>1'400'000 || p.max_slippage_bps>10'000 ||
        p.accepted_costs.max_output_deduction.value()>max_signed)return false;
    const auto keys=owner_side(p);
    for(std::size_t i=0;i<keys.size();++i){
        if(keys[i]==SolanaAccountKey{})return false;
        for(std::size_t j=0;j<i;++j)if(keys[i]==keys[j])return false;
    }
    return true;
}
SolanaAccountKey key_at(const DecodedSolanaPayload& d,std::size_t offset) noexcept {
    SolanaAccountKey key{};std::copy_n(d.transaction.begin()+static_cast<std::ptrdiff_t>(offset),key.size(),key.begin());return key;
}
const SolanaAccountKey& program_of(const DecodedSolanaPayload& d,const ResolvedSolanaAccounts& r,std::size_t n) noexcept {
    return r.accounts[d.instructions[n].program_index].key;
}
const SolanaAccountKey& account_of(const DecodedSolanaPayload& d,const ResolvedSolanaAccounts& r,std::size_t n,std::size_t i) noexcept {
    return r.accounts[d.transaction[d.instructions[n].accounts_offset+i]].key;
}
SolanaInstructionDecodeResult small(const DecodedSolanaPayload& d,const ResolvedSolanaAccounts& r,std::size_t n) noexcept {
    const auto& ix=d.instructions[n];std::array<SolanaInstructionAccount,6> a{};
    if(ix.accounts_count>a.size())return {};
    for(std::size_t i=0;i<ix.accounts_count;++i){const auto& x=r.accounts[d.transaction[ix.accounts_offset+i]];a[i]={x.key,x.signer,x.writable};}
    return decode_solana_instruction(r.accounts[ix.program_index].key,
        std::span(d.transaction).subspan(ix.data_offset,ix.data_size),std::span(a).first(ix.accounts_count));
}
bool static_writable(const DecodedSolanaPayload& d,const ResolvedSolanaAccounts& r,const SolanaAccountKey& key) noexcept {
    for(std::size_t i=0;i<d.static_key_count;++i)
        if(r.accounts[i].key==key)return r.accounts[i].writable && !r.accounts[i].signer;
    return false;
}
const RouteContract* contract_for(JupiterSwapKind kind) noexcept {
    for(const auto& c:contracts)if(c.kind==kind)return &c;
    return nullptr;
}
}
Digest256 swap_execution_policy_digest(const SwapExecutionPolicy& p) noexcept;
namespace {
bool compute_within_policy(const DecodedSolanaPayload& d,const ResolvedSolanaAccounts& r,bool priced,const SwapExecutionPolicy& p) noexcept {
    const auto compute=small(d,r,0);
    if(!std::holds_alternative<ComputeUnitLimit>(compute.body))return false;
    std::uint64_t micros{};
    if(priced){
        const auto price=small(d,r,1);
        if(!std::holds_alternative<ComputeUnitPrice>(price.body))return false;
        micros=std::get<ComputeUnitPrice>(price.body).micro_lamports;
    }
    const auto units=std::get<ComputeUnitLimit>(compute.body).units;
    return units && units<=p.max_compute_units && micros<=p.max_compute_price && micros<=std::numeric_limits<std::uint64_t>::max()/units;
}
struct SellCheck { Code code{}; SwapTransactionBounds bounds{}; std::uint64_t expires{}; };
SellCheck verify_sell(const DecodedSolanaPayload& d,const ResolvedSolanaAccounts& r,const swap::Intent& intent,const SwapExecutionPolicy& p) noexcept {
    const bool priced=d.instruction_count==7;
    if(d.instruction_count!=6 && !priced)return {Code::unsupported_topology};
    const std::size_t ata=priced?2:1,wrap=ata+1,sync=ata+2,route_index=ata+3,close=ata+4;
    if(program_of(d,r,0)!=kSolanaComputeBudgetProgram || (priced && program_of(d,r,1)!=kSolanaComputeBudgetProgram) ||
        program_of(d,r,ata)!=kSolanaAssociatedTokenProgram || program_of(d,r,wrap)!=kSolanaSystemProgram ||
        program_of(d,r,sync)!=kSolanaClassicTokenProgram || program_of(d,r,route_index)!=jupiter ||
        program_of(d,r,close)!=kSolanaClassicTokenProgram)
        return {Code::unsupported_topology};
    const auto create=small(d,r,ata),wrapping=small(d,r,wrap),syncing=small(d,r,sync),closing=small(d,r,close);
    if(!std::holds_alternative<CreateAssociatedTokenIdempotent>(create.body) || !std::holds_alternative<TransferLamports>(wrapping.body) ||
        !std::holds_alternative<SyncNativeToken>(syncing.body) || !std::holds_alternative<CloseTokenAccount>(closing.body))
        return {Code::invalid_instruction};
    const auto& made=std::get<CreateAssociatedTokenIdempotent>(create.body);
    const auto& wrapped=std::get<TransferLamports>(wrapping.body);
    const auto& closed=std::get<CloseTokenAccount>(closing.body);
    if(made.funder.key!=p.sponsor || made.associated_account.key!=p.owner_wsol || made.wallet.key!=p.owner || made.mint.key!=wrapped_sol ||
        wrapped.source.key!=p.owner || wrapped.destination.key!=p.owner_wsol || wrapped.lamports!=intent.base_input.value() ||
        std::get<SyncNativeToken>(syncing.body).account.key!=p.owner_wsol ||
        closed.account.key!=p.owner_wsol || closed.destination.key!=p.sponsor || closed.authority.key!=p.owner)
        return {Code::unsupported_topology};
    if(!compute_within_policy(d,r,priced,p))return {Code::invalid_instruction};
    const auto& ix=d.instructions[route_index];
    const std::array<SolanaAccountKey,9> fixed{kSolanaClassicTokenProgram,p.owner,p.owner_wsol,p.owner_usdc,jupiter,
        usdc,p.platform_fee,event_authority,jupiter};
    if(ix.accounts_count<=fixed.size())return {Code::unsupported_topology};
    for(std::size_t i=0;i<fixed.size();++i)if(account_of(d,r,route_index,i)!=fixed[i])return {Code::unsupported_topology};
    const auto decoded=decode_jupiter_direct_route_data(std::span(d.transaction).subspan(ix.data_offset,ix.data_size));
    const auto& route=decoded.data;
    const auto* contract=decoded.status==JupiterRouteStatus::decoded_data_only && route.step_count==1?contract_for(route.steps[0].kind):nullptr;
    bool program_present{};
    for(std::size_t i=fixed.size();contract && i<ix.accounts_count;++i)program_present|=account_of(d,r,route_index,i)==contract->program;
    if(!contract || !program_present || route.steps[0].percent!=100 || route.steps[0].input_index!=0 || route.steps[0].output_index!=1 ||
        !route.quoted_output_atoms || route.quoted_output_atoms>max_signed || route.slippage_bps>p.max_slippage_bps)
        return {Code::unsupported_route};
    if(route.platform_fee_bps!=20 || route.input_atoms!=intent.base_input.value())return {Code::debit_mismatch};
    const auto fee=route.quoted_output_atoms/499+(route.quoted_output_atoms%499?1:0);
    if(fee>intent.fee_cap.value())return {Code::debit_mismatch};
    const auto factor=10'000-route.slippage_bps;
    const auto floor=(route.quoted_output_atoms/10'000)*factor+((route.quoted_output_atoms%10'000)*factor)/10'000;
    SwapTransactionBounds bounds{0,fee,0,0,0,route.input_atoms,floor};
    if(floor<intent.min_quote_receive.value())return {Code::minimum_shortfall,bounds};
    return {Code::verified_under_program_contract,bounds,std::min(intent.sign_by,static_cast<std::uint64_t>(d.timestamp_ms)+p.max_envelope_age_ms)};
}
}
Digest256 swap_execution_policy_digest(const SwapExecutionPolicy& p) noexcept {
    if(!valid_policy(p))return {};
    Sha256 h;label(h,"velocita-swap-execution-policy-v2");binding(h,p.binding);
    for(const auto& k:owner_side(p))account(h,k);
    number(h,p.max_envelope_age_ms);number(h,p.max_compute_price);number(h,p.max_compute_units);
    number(h,p.max_slippage_bps);number(h,p.accepted_costs.max_output_deduction.value());
    account(h,jupiter);account(h,event_authority);account(h,wrapped_sol);account(h,lookup_authority);account(h,usdc);
    for(const auto& c:contracts){number(h,static_cast<std::uint8_t>(c.kind));account(h,c.program);}
    return h.finish();
}
Digest256 jupiter_lookup_prefix_digest(const JupiterLookupPrefix& p) noexcept {
    if(!p.count || p.count>p.addresses.size())return {};
    Sha256 h;label(h,"velocita-jupiter-lookup-prefix-v1");account(h,p.table);number(h,p.count);
    for(std::size_t i=0;i<p.count;++i)account(h,p.addresses[i]);
    return h.finish();
}
SolanaLookupEvidence jupiter_lookup_evidence(const JupiterLookupPrefix& p) noexcept {
    SolanaLookupEvidence e;e.evidence_digest=jupiter_lookup_prefix_digest(p);
    if(e.evidence_digest.is_zero())return {};
    e.table=p.table;e.observed_slot=1;e.address_count=p.count;
    std::copy_n(p.addresses.begin(),p.count,e.addresses.begin());
    return e;
}
std::optional<JupiterLookupRequirement> jupiter_lookup_requirement(std::string_view envelope,
    std::string_view organization,std::string_view signer) noexcept {
    DecodedSolanaPayload d;
    if(decode_solana_signing_envelope(envelope,organization,signer,d)!=SolanaParseError::none || d.lookup_count!=1)return {};
    const auto& l=d.lookups[0];
    std::size_t highest{};bool any{};
    for(const auto& [offset,count]:{std::pair{l.writable_offset,l.writable_count},std::pair{l.readonly_offset,l.readonly_count}})
        for(std::size_t i=0;i<count;++i){highest=std::max<std::size_t>(highest,d.transaction[offset+i]);any=true;}
    if(!any)return {};
    return JupiterLookupRequirement{key_at(d,l.key_offset),static_cast<std::uint16_t>(highest+1)};
}
std::optional<JupiterLookupPrefix> jupiter_lookup_prefix_from_account(const JupiterLookupRequirement& need,
    std::span<const std::uint8_t> data) noexcept {
    if(!need.count || need.count>kMaxLookupPrefix || data.size()<kLookupTableMetaBytes ||
        (data.size()-kLookupTableMetaBytes)%32 || (data.size()-kLookupTableMetaBytes)/32<need.count)return {};
    const auto little=[&](std::size_t at,std::size_t width){std::uint64_t v{};for(std::size_t i=0;i<width;++i)v|=std::uint64_t{data[at+i]}<<(8*i);return v;};
    if(little(0,4)!=1 || little(4,8)!=std::numeric_limits<std::uint64_t>::max() || data[21]!=1 ||
        !std::equal(lookup_authority.begin(),lookup_authority.end(),data.begin()+22) || data[54] || data[55])return {};
    JupiterLookupPrefix result;result.table=need.table;result.count=need.count;
    for(std::size_t i=0;i<need.count;++i)
        std::copy_n(data.begin()+static_cast<std::ptrdiff_t>(kLookupTableMetaBytes+32*i),32,result.addresses[i].begin());
    return result;
}
Digest256 verified_swap_intent_digest(const swap::Intent& i) noexcept {
    Sha256 h;label(h,"velocita-verified-swap-intent-v1");binding(h,i.binding);
    number(h,i.effect.value());number(h,static_cast<unsigned>(i.side));number(h,i.quote_input.value());
    number(h,i.max_quote_debit.value());number(h,i.min_quote_receive.value());number(h,i.fee_cap.value());
    number(h,i.base_input.value());number(h,i.min_base_receive.value());number(h,i.transient_base_hold.value());
    number(h,i.source_time);number(h,i.sign_by);number(h,i.kill_epoch);number(h,i.executor_generation);return h.finish();
}
SwapTransactionVerification verify_swap_transaction(std::string_view envelope,std::string_view organization,
    std::string_view signer,const swap::Pending& pending,const SwapExecutionPolicy& p,
    const JupiterLookupPrefix& lookup,std::uint64_t now) noexcept {
    const auto policy_digest=swap_execution_policy_digest(p);
    if(policy_digest.is_zero() || !now || organization.empty() || signer.empty())return {};
    const auto& intent=pending.intent;
    const bool selling=intent.side==Side::sell;
    const bool buy_shape=intent.side==Side::buy && intent.quote_input.value() && intent.max_quote_debit.value() &&
        intent.max_quote_debit.value()<=max_signed && intent.min_base_receive.value() && intent.min_base_receive.value()<=max_signed &&
        !intent.base_input.value() && !intent.min_quote_receive.value();
    const bool sell_shape=selling && intent.base_input.value() && intent.base_input.value()<=max_signed &&
        intent.min_quote_receive.value() && intent.min_quote_receive.value()<=max_signed &&
        !intent.quote_input.value() && !intent.max_quote_debit.value() && !intent.min_base_receive.value();
    if(pending.phase!=swap::Phase::prepared || pending.handle.is_zero() || intent.binding!=p.binding || !intent.effect.value() ||
        (!buy_shape && !sell_shape) || intent.transient_base_hold.value() ||
        !intent.source_time || now<intent.source_time || now>=intent.sign_by || !intent.kill_epoch || !intent.executor_generation)
        return {Code::invalid_intent,{},{}};
    const auto payload_digest=sha256_digest(std::as_bytes(std::span(envelope)));
    if(pending.payload!=payload_digest)return {Code::payload_mismatch,{},{}};
    DecodedSolanaPayload d;
    if(decode_solana_signing_envelope(envelope,organization,signer,d)!=SolanaParseError::none ||
        d.timestamp_ms<=0 || static_cast<std::uint64_t>(d.timestamp_ms)>now ||
        static_cast<std::uint64_t>(d.timestamp_ms)<intent.source_time ||
        now-static_cast<std::uint64_t>(d.timestamp_ms)>=p.max_envelope_age_ms || d.nonzero_signatures)
        return {Code::invalid_envelope,{},{}};
    const auto lookup_digest=jupiter_lookup_prefix_digest(lookup);
    const auto evidence=jupiter_lookup_evidence(lookup);
    ResolvedSolanaAccounts r;
    if(lookup_digest.is_zero() || resolve_solana_accounts(d,organization,signer,std::span(&evidence,1),r)!=SolanaResolutionError::none)
        return {Code::invalid_accounts,{},{}};
    if(!d.version_zero || d.required_signatures!=2 || d.signer_index!=1 || r.lookup_count!=1 ||
        r.accounts[0]!=ResolvedSolanaAccount{p.sponsor,true,true} || r.accounts[1]!=ResolvedSolanaAccount{p.owner,true,true} ||
        !static_writable(d,r,p.owner_usdc) || !static_writable(d,r,p.owner_wsol) || !static_writable(d,r,p.platform_fee))
        return {Code::account_mismatch,{},{}};
    if(selling){
        const auto sold=verify_sell(d,r,intent,p);
        if(sold.code!=Code::verified_under_program_contract)return {sold.code,sold.bounds,{}};
        return {sold.code,sold.bounds,VerifiedSwapTransaction{payload_digest,verified_swap_intent_digest(intent),policy_digest,lookup_digest,sold.bounds,sold.expires}};
    }
    const bool priced=d.instruction_count==6;
    if(d.instruction_count!=5 && !priced)return {Code::unsupported_topology,{},{}};
    const std::size_t ata=priced?2:1,route_index=ata+1,close=ata+2,pay=ata+3;
    if(program_of(d,r,0)!=kSolanaComputeBudgetProgram || (priced && program_of(d,r,1)!=kSolanaComputeBudgetProgram) ||
        program_of(d,r,ata)!=kSolanaAssociatedTokenProgram || program_of(d,r,route_index)!=jupiter ||
        program_of(d,r,close)!=kSolanaClassicTokenProgram || program_of(d,r,pay)!=kSolanaSystemProgram)
        return {Code::unsupported_topology,{},{}};
    const auto compute=small(d,r,0),create=small(d,r,ata),closing=small(d,r,close),payment=small(d,r,pay);
    if(!std::holds_alternative<ComputeUnitLimit>(compute.body) ||
        !std::holds_alternative<CreateAssociatedTokenIdempotent>(create.body) || !std::holds_alternative<CloseTokenAccount>(closing.body) ||
        !std::holds_alternative<TransferLamports>(payment.body))return {Code::invalid_instruction,{},{}};
    const auto& made=std::get<CreateAssociatedTokenIdempotent>(create.body);
    const auto& closed=std::get<CloseTokenAccount>(closing.body);
    const auto& repaid=std::get<TransferLamports>(payment.body);
    if(made.funder.key!=p.sponsor || made.associated_account.key!=p.owner_wsol || made.wallet.key!=p.owner || made.mint.key!=wrapped_sol ||
        closed.account.key!=p.owner_wsol || closed.destination.key!=p.owner || closed.authority.key!=p.owner ||
        repaid.source.key!=p.owner || repaid.destination.key!=p.sponsor)
        return {Code::unsupported_topology,{},{}};
    std::uint64_t micros{};
    if(priced){
        const auto price=small(d,r,1);
        if(!std::holds_alternative<ComputeUnitPrice>(price.body))return {Code::invalid_instruction,{},{}};
        micros=std::get<ComputeUnitPrice>(price.body).micro_lamports;
    }
    const auto units=std::get<ComputeUnitLimit>(compute.body).units;
    if(!units || units>p.max_compute_units || micros>p.max_compute_price || micros>std::numeric_limits<std::uint64_t>::max()/units)
        return {Code::invalid_instruction,{},{}};
    const auto& ix=d.instructions[route_index];
    const std::array<SolanaAccountKey,9> fixed{kSolanaClassicTokenProgram,p.owner,p.owner_usdc,p.owner_wsol,jupiter,
        wrapped_sol,p.platform_fee,event_authority,jupiter};
    if(ix.accounts_count<=fixed.size())return {Code::unsupported_topology,{},{}};
    for(std::size_t i=0;i<fixed.size();++i)if(account_of(d,r,route_index,i)!=fixed[i])return {Code::unsupported_topology,{},{}};
    const auto decoded=decode_jupiter_direct_route_data(std::span(d.transaction).subspan(ix.data_offset,ix.data_size));
    const auto& route=decoded.data;
    const auto* contract=decoded.status==JupiterRouteStatus::decoded_data_only && route.step_count==1?contract_for(route.steps[0].kind):nullptr;
    bool program_present{};
    for(std::size_t i=fixed.size();contract && i<ix.accounts_count;++i)program_present|=account_of(d,r,route_index,i)==contract->program;
    if(!contract || !program_present || route.steps[0].percent!=100 || route.steps[0].input_index!=0 || route.steps[0].output_index!=1 ||
        !route.quoted_output_atoms || route.quoted_output_atoms>max_signed || route.slippage_bps>p.max_slippage_bps)
        return {Code::unsupported_route,{},{}};
    if(route.platform_fee_bps!=20 || route.input_atoms!=intent.max_quote_debit.value() || route.input_atoms%10'000!=0)
        return {Code::debit_mismatch,{},{}};
    const auto fee=(route.input_atoms/10'000)*20;
    if(fee!=intent.fee_cap.value() || route.input_atoms-fee!=intent.quote_input.value())return {Code::debit_mismatch,{},{}};
    const auto factor=10'000-route.slippage_bps;
    const auto gross=(route.quoted_output_atoms/10'000)*factor+((route.quoted_output_atoms%10'000)*factor)/10'000;
    const auto net=bound_buy_net_cost(gross,repaid.lamports,0,intent.min_base_receive,p.accepted_costs);
    SwapTransactionBounds bounds{route.input_atoms,fee,gross,net.maximum_deduction,net.minimum_net_receipt};
    if(net.code==BuyNetCostCode::deduction_exceeded)return {Code::deduction_exceeded,bounds,{}};
    if(net.code!=BuyNetCostCode::within_arithmetic_bounds_only)return {Code::minimum_shortfall,bounds,{}};
    const auto expires=std::min(intent.sign_by,static_cast<std::uint64_t>(d.timestamp_ms)+p.max_envelope_age_ms);
    return {Code::verified_under_program_contract,bounds,VerifiedSwapTransaction{payload_digest,
        verified_swap_intent_digest(intent),policy_digest,lookup_digest,bounds,expires}};
}
}
