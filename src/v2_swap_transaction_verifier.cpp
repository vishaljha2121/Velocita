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
constexpr SolanaAccountKey instructions_sysvar{6,167,213,23,24,123,209,102,53,218,212,4,85,253,194,192,193,36,198,143,33,86,117,165,219,186,203,95,8,0,0,0};
constexpr SolanaAccountKey kipseli{36,116,252,210,179,53,119,156,91,10,143,10,199,77,189,139,146,186,52,103,172,51,237,99,161,80,185,53,130,139,213,137};
constexpr SolanaAccountKey usdc{198,250,122,243,190,219,173,58,61,101,243,106,171,201,116,49,177,187,228,194,210,246,224,228,124,166,2,3,69,47,93,97};
constexpr SolanaAccountKey route_auxiliary{92,182,230,237,49,47,49,220,185,242,92,42,64,40,72,4,223,226,119,102,59,98,177,153,135,151,250,201,221,234,197,87};
constexpr SolanaAccountKey route_readonly{252,209,61,160,65,85,144,19,207,74,235,161,168,78,215,2,58,212,255,33,168,71,71,255,59,5,171,48,250,130,7,195};
constexpr SolanaAccountKey byreal{6,52,210,15,27,245,109,209,100,227,193,60,120,54,164,34,158,197,171,33,255,84,82,131,144,165,23,137,243,205,187,219};
constexpr SolanaAccountKey token2022{6,221,246,225,238,117,143,222,24,66,93,188,228,108,205,218,182,26,252,77,131,185,13,39,254,189,249,40,216,161,139,252};
constexpr SolanaAccountKey memo{5,74,83,90,153,41,33,6,77,36,232,113,96,218,56,124,124,53,181,221,188,146,187,129,228,31,168,64,65,5,68,141};

constexpr SolanaAccountKey obsidian{240,107,131,98,69,250,70,54,116,160,2,96,141,142,220,212,232,74,255,176,164,188,146,113,210,228,153,27,192,16,138,171};

constexpr SolanaAccountKey bison{159,50,218,156,31,108,18,80,192,155,115,57,35,236,195,103,13,40,99,231,212,106,156,28,244,125,253,187,208,213,254,87};
constexpr SolanaAccountKey bison_readonly{53,155,99,136,184,36,81,130,108,187,131,187,148,33,122,249,101,134,8,98,159,129,99,79,10,36,73,222,107,80,156,118};

void number(Sha256& hash,std::uint64_t value) noexcept {
    std::array<std::byte,8> bytes{};
    for(unsigned i=0;i<8;++i)bytes[i]=static_cast<std::byte>(value>>(8*i));
    hash.update(bytes);
}
void label(Sha256& hash,std::string_view text) noexcept {hash.update(std::as_bytes(std::span(text)));}
void binding(Sha256& h,const swap::Binding& b) noexcept {
    h.update(b.incarnation.bytes);h.update(b.catalog.bytes);h.update(b.custody.bytes);
    number(h,b.account.value());number(h,b.instrument.value());number(h,b.policy.value());
    number(h,b.policy_version.value());number(h,b.risk_policy_version.value());
}
bool valid_binding(const swap::Binding& b) noexcept {
    return !b.incarnation.is_zero() && !b.catalog.is_zero() && !b.custody.is_zero() &&
        b.account.value() && b.instrument.value() && b.policy.value() && b.policy_version.value() && b.risk_policy_version.value();
}
bool valid_policy(const DirectBuyVerificationPolicy& p) noexcept {
    const auto& a=p.account_keys;
    if(!valid_binding(p.binding) || !p.expires_at_ms || !p.max_envelope_age_ms || p.max_envelope_age_ms>30'000 ||
        !p.max_compute_units || p.max_compute_units>1'400'000 || p.max_slippage_bps>10'000 ||
        p.accepted_costs.max_output_deduction.value()>max_signed || p.lookup_image_digest.is_zero())return false;
    if(p.route!=DirectBuyRoute::kipseli_v3 && p.route!=DirectBuyRoute::byreal_dynamic_v3 && p.route!=DirectBuyRoute::obsidian && p.route!=DirectBuyRoute::bison_market_backed)return false;
    for(std::size_t i=0;i<p.account_count();++i)for(std::size_t j=0;j<i;++j)
        if(p.account_key(i)==p.account_key(j))return false;
    if(p.route==DirectBuyRoute::byreal_dynamic_v3)
        return a[8]==kSolanaSystemProgram && a[9]==kSolanaComputeBudgetProgram && a[10]==jupiter &&
            a[11]==kSolanaClassicTokenProgram && a[13]==kSolanaAssociatedTokenProgram && a[14]==event_authority &&
            p.account_key(20)==memo && p.account_key(21)==byreal && p.account_key(22)==wrapped_sol &&
            p.account_key(23)==token2022 && p.account_key(25)==usdc;
    if(std::any_of(p.additional_account_keys.begin(),p.additional_account_keys.end(),
        [](const auto& k){return k!=SolanaAccountKey{};}))return false;
    if(p.route==DirectBuyRoute::obsidian) {
        if(std::any_of(a.begin()+17,a.end(),[](const auto& k){return k!=SolanaAccountKey{};}))return false;
        return a[5]==kSolanaSystemProgram && a[6]==kSolanaComputeBudgetProgram && a[7]==jupiter &&
            a[8]==kSolanaClassicTokenProgram && a[9]==kSolanaAssociatedTokenProgram && a[10]==event_authority &&
            a[13]==wrapped_sol && a[14]==instructions_sysvar && a[16]==obsidian;
    }
    return a[5]==kSolanaSystemProgram && a[6]==kSolanaComputeBudgetProgram && a[7]==jupiter &&
        a[8]==kSolanaClassicTokenProgram && a[9]==(p.route==DirectBuyRoute::bison_market_backed?bison_readonly:route_auxiliary) && a[10]==kSolanaAssociatedTokenProgram &&
        a[11]==event_authority && a[15]==wrapped_sol && a[16]==instructions_sysvar && (p.route==DirectBuyRoute::bison_market_backed?a[17]==bison:a[17]==kipseli) &&
        a[18]==usdc && a[19]==route_readonly;
}
bool indices(const DecodedSolanaPayload& d,std::size_t n,std::span<const std::uint8_t> expected) noexcept {
    const auto& ix=d.instructions[n];return ix.accounts_count==expected.size() &&
        std::equal(expected.begin(),expected.end(),d.transaction.begin()+ix.accounts_offset);
}
SolanaInstructionDecodeResult small(const DecodedSolanaPayload& d,const ResolvedSolanaAccounts& r,std::size_t n) noexcept {
    const auto& ix=d.instructions[n];std::array<SolanaInstructionAccount,6> a{};
    if(ix.accounts_count>a.size())return {};
    for(std::size_t i=0;i<ix.accounts_count;++i){const auto& x=r.accounts[d.transaction[ix.accounts_offset+i]];a[i]={x.key,x.signer,x.writable};}
    return decode_solana_instruction(r.accounts[ix.program_index].key,
        std::span(d.transaction).subspan(ix.data_offset,ix.data_size),std::span(a).first(ix.accounts_count));
}
}
Digest256 direct_buy_policy_digest(const DirectBuyVerificationPolicy& p) noexcept {
    if(!valid_policy(p))return {};
    Sha256 h;label(h,p.route==DirectBuyRoute::bison_market_backed?"velocita-direct-bison-market-backed-buy-v1":p.route==DirectBuyRoute::obsidian?"velocita-direct-obsidian-buy-v1":p.route==DirectBuyRoute::kipseli_v3?"velocita-direct-kipseli-buy-v1":"velocita-direct-byreal-v3-buy-v1");binding(h,p.binding);
    for(std::size_t i=0;i<p.account_count();++i)h.update(std::as_bytes(std::span(p.account_key(i))));
    number(h,p.max_envelope_age_ms);number(h,p.max_compute_price);number(h,p.max_compute_units);
    number(h,p.max_slippage_bps);number(h,p.accepted_costs.max_output_deduction.value());
    h.update(p.lookup_image_digest.bytes);number(h,p.expires_at_ms);return h.finish();
}
Digest256 direct_buy_lookup_digest(std::span<const SolanaLookupEvidence> e) noexcept {
    if(e.size()!=1 || !e[0].address_count || e[0].address_count>256 || !e[0].observed_slot || e[0].evidence_digest.is_zero())return {};
    Sha256 h;label(h,"velocita-direct-buy-lookup-v1");
    h.update(std::as_bytes(std::span(e[0].table)));number(h,e[0].observed_slot);h.update(e[0].evidence_digest.bytes);
    number(h,e[0].address_count);
    for(std::size_t i=0;i<e[0].address_count;++i)h.update(std::as_bytes(std::span(e[0].addresses[i])));
    return h.finish();
}
Digest256 verified_swap_intent_digest(const swap::Intent& i) noexcept {
    Sha256 h;label(h,"velocita-verified-swap-intent-v1");binding(h,i.binding);
    number(h,i.effect.value());number(h,static_cast<unsigned>(i.side));number(h,i.quote_input.value());
    number(h,i.max_quote_debit.value());number(h,i.min_quote_receive.value());number(h,i.fee_cap.value());
    number(h,i.base_input.value());number(h,i.min_base_receive.value());number(h,i.transient_base_hold.value());
    number(h,i.source_time);number(h,i.sign_by);number(h,i.kill_epoch);number(h,i.executor_generation);return h.finish();
}
SwapTransactionVerification verify_swap_transaction(std::string_view envelope,std::string_view organization,
    std::string_view signer,const swap::Pending& pending,const DirectBuyVerificationPolicy& p,
    std::span<const SolanaLookupEvidence> evidence,std::uint64_t now) noexcept {
    const auto policy_digest=direct_buy_policy_digest(p);
    if(policy_digest.is_zero() || !now || now>=p.expires_at_ms || organization.empty() || signer.empty())return {};
    const auto& intent=pending.intent;
    if(pending.phase!=swap::Phase::prepared || pending.handle.is_zero() || intent.binding!=p.binding || !intent.effect.value() ||
        intent.side!=Side::buy || !intent.quote_input.value() || !intent.max_quote_debit.value() ||
        intent.max_quote_debit.value()>max_signed || !intent.min_base_receive.value() || intent.min_base_receive.value()>max_signed ||
        intent.base_input.value() || intent.min_quote_receive.value() || intent.transient_base_hold.value() ||
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
    if(direct_buy_lookup_digest(evidence)!=p.lookup_image_digest)return {Code::invalid_accounts,{},{}};
    ResolvedSolanaAccounts accounts;
    if(resolve_solana_accounts(d,organization,signer,evidence,accounts)!=SolanaResolutionError::none)
        return {Code::invalid_accounts,{},{}};
    const bool is_bison=p.route==DirectBuyRoute::bison_market_backed;
    const bool is_obsidian=p.route==DirectBuyRoute::obsidian;
    const bool is_byreal=p.route==DirectBuyRoute::byreal_dynamic_v3;
    if(!d.version_zero || d.required_signatures!=2 || d.signer_index!=1 || d.static_key_count!=(is_obsidian?11:is_byreal?16:12) ||
        accounts.account_count!=p.account_count() || accounts.lookup_count!=1)return {Code::account_mismatch,{},{}};
    for(std::size_t i=0;i<p.account_count();++i){
        const bool writable=is_obsidian?(i<5 || (i>=11 && i<13)):is_byreal?(i<8 || (i>=16 && i<20)):(i<5 || (i>=12 && i<15));
        if(accounts.accounts[i].key!=p.account_key(i) || accounts.accounts[i].signer!=(i<2) ||
            accounts.accounts[i].writable!=writable)return {Code::account_mismatch,{},{}};
    }
    constexpr std::array<std::uint8_t,6> legacy_programs{6,6,10,7,8,5},byreal_programs{9,9,13,10,11,8};
    constexpr std::array<std::uint8_t,6> legacy_ata{0,3,1,15,5,8},byreal_ata{0,4,1,22,8,11};
    constexpr std::array<std::uint8_t,3> legacy_close{3,1,1},byreal_close{4,1,1};
    constexpr std::array<std::uint8_t,2> transfer{1,0};
    constexpr std::array<std::uint8_t,25> legacy_route{8,1,2,3,7,15,4,11,7,17,1,12,14,13,15,18,3,2,8,5,1,9,9,19,16};
    constexpr std::array<std::uint8_t,30> byreal_route{11,1,3,4,10,22,5,14,10,21,1,24,18,3,4,19,17,16,11,23,20,25,22,26,7,2,6,12,15,10};
    constexpr std::array<std::uint8_t,6> obsidian_programs{6,6,9,7,8,5};
    constexpr std::array<std::uint8_t,6> obsidian_ata{0,3,1,13,5,8};
    constexpr std::array<std::uint8_t,18> obsidian_route{8,1,2,3,7,13,4,10,7,16,1,15,12,11,2,3,8,14};
    constexpr std::array<std::uint8_t,23> bison_route{8,1,2,3,7,15,4,11,7,17,1,13,14,12,3,2,8,8,15,18,16,19,9};
    const auto& programs=is_obsidian?obsidian_programs:is_byreal?byreal_programs:legacy_programs;
    const auto route_accounts=is_bison?std::span<const std::uint8_t>(bison_route):is_obsidian?std::span<const std::uint8_t>(obsidian_route):
        is_byreal?std::span<const std::uint8_t>(byreal_route):std::span<const std::uint8_t>(legacy_route);
    // Only the observed Obsidian layout may omit compute-price. No other
    // instruction can be skipped, reordered or added; absent price means zero.
    const bool omit_price=is_obsidian && d.instruction_count==5;
    if(d.instruction_count!=6 && !omit_price)return {Code::unsupported_topology,{},{}};
    for(std::size_t i=0;i<programs.size()-(omit_price?1:0);++i)
        if(d.instructions[i].program_index!=programs[i+(omit_price && i>0?1:0)])return {Code::unsupported_topology,{},{}};
    const std::size_t ata_index=omit_price?1:2,route_index=ata_index+1,close_index=ata_index+2,pay_index=ata_index+3;
    if(!indices(d,0,{}) || (!omit_price && !indices(d,1,{})) ||
        !indices(d,ata_index,is_obsidian?obsidian_ata:is_byreal?byreal_ata:legacy_ata) ||
        !indices(d,route_index,route_accounts) || !indices(d,close_index,is_byreal?byreal_close:legacy_close) || !indices(d,pay_index,transfer))
        return {Code::unsupported_topology,{},{}};
    const auto compute=small(d,accounts,0),create=small(d,accounts,ata_index),
        closing=small(d,accounts,close_index),payment=small(d,accounts,pay_index);
    if(!std::holds_alternative<ComputeUnitLimit>(compute.body) ||
        !std::holds_alternative<CreateAssociatedTokenIdempotent>(create.body) || !std::holds_alternative<CloseTokenAccount>(closing.body) ||
        !std::holds_alternative<TransferLamports>(payment.body))return {Code::invalid_instruction,{},{}};
    std::uint64_t micros{};
    if(!omit_price) {
        const auto price=small(d,accounts,1);
        if(!std::holds_alternative<ComputeUnitPrice>(price.body))return {Code::invalid_instruction,{},{}};
        micros=std::get<ComputeUnitPrice>(price.body).micro_lamports;
    }
    const auto units=std::get<ComputeUnitLimit>(compute.body).units;
    if(!units || units>p.max_compute_units || micros>p.max_compute_price || micros>std::numeric_limits<std::uint64_t>::max()/units)
        return {Code::invalid_instruction,{},{}};
    const auto& ix=d.instructions[route_index];
    const auto decoded=decode_jupiter_direct_route_data(std::span(d.transaction).subspan(ix.data_offset,ix.data_size));
    const auto& route=decoded.data;
    if(decoded.status!=JupiterRouteStatus::decoded_data_only || route.step_count!=1 ||
        route.steps[0]!=JupiterRouteStep{is_bison?JupiterSwapKind::bison_fi_market_backed:is_obsidian?JupiterSwapKind::obsidian:is_byreal?JupiterSwapKind::byreal_dynamic_v3:JupiterSwapKind::kipseli_v3,0,100,0,1,0} ||
        !route.quoted_output_atoms || route.quoted_output_atoms>max_signed || route.slippage_bps>p.max_slippage_bps)
        return {Code::unsupported_route,{},{}};
    if(route.platform_fee_bps!=20 || route.input_atoms!=intent.max_quote_debit.value() || route.input_atoms%10'000!=0)
        return {Code::debit_mismatch,{},{}};
    const auto fee=(route.input_atoms/10'000)*20;
    if(fee!=intent.fee_cap.value() || route.input_atoms-fee!=intent.quote_input.value())return {Code::debit_mismatch,{},{}};
    const auto factor=10'000-route.slippage_bps;
    const auto gross=(route.quoted_output_atoms/10'000)*factor+
        ((route.quoted_output_atoms%10'000)*factor+9'999)/10'000;
    const auto repayment=std::get<TransferLamports>(payment.body).lamports;
    const auto net=bound_buy_net_cost(gross,repayment,0,intent.min_base_receive,p.accepted_costs);
    SwapTransactionBounds bounds{route.input_atoms,fee,gross,net.maximum_deduction,net.minimum_net_receipt};
    if(net.code==BuyNetCostCode::deduction_exceeded)return {Code::deduction_exceeded,bounds,{}};
    if(net.code!=BuyNetCostCode::within_arithmetic_bounds_only)return {Code::minimum_shortfall,bounds,{}};
    const auto expires=std::min({intent.sign_by,p.expires_at_ms,static_cast<std::uint64_t>(d.timestamp_ms)+p.max_envelope_age_ms});
    return {Code::verified_under_program_contract,bounds,VerifiedSwapTransaction{payload_digest,
        verified_swap_intent_digest(intent),policy_digest,bounds,expires}};
}
}
