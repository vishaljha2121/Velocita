#include "velocita/v2/swap_transaction_inspection.hpp"
#include "velocita/v2/solana_instruction_decode.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <limits>

#ifndef VELOCITA_SWAP_INSPECTION_TEST_ONLY
#error "Whole-transaction inspection has no production signing authority"
#endif

namespace velocita::v2::truemarkets {
namespace {
using Code=TransactionInspectionCode;
constexpr SolanaAccountKey jupiter{4,121,213,91,242,49,192,110,238,116,197,110,206,104,21,7,
    253,177,178,222,163,244,142,81,2,177,205,162,86,188,19,143};
constexpr auto wrapped_sol=SolanaAccountKey{6,155,136,87,254,171,129,132,251,104,127,99,70,24,192,53,218,196,57,220,26,235,59,85,152,160,240,0,0,0,0,1};
constexpr auto usdc=SolanaAccountKey{198,250,122,243,190,219,173,58,61,101,243,106,171,201,116,49,177,187,228,194,210,246,224,228,124,166,2,3,69,47,93,97};
constexpr std::array<std::uint8_t,41> route_accounts{11,12,1,3,4,2,5,29,26,6,10,14,10,32,12,30,29,18,21,25,
    20,23,24,19,22,7,4,11,11,28,12,17,15,16,2,7,11,11,27,31,10};
bool account_indices(const DecodedSolanaPayload& d,std::size_t ix,std::span<const std::uint8_t> expected) noexcept {
    const auto& v=d.instructions[ix];
    return v.accounts_count==expected.size() && std::equal(expected.begin(),expected.end(),d.transaction.begin()+v.accounts_offset);
}
SolanaInstructionDecodeResult small_instruction(const DecodedSolanaPayload& d,const ResolvedSolanaAccounts& r,
    std::size_t ix) noexcept {
    const auto& v=d.instructions[ix];std::array<SolanaInstructionAccount,256> accounts{};
    for(std::size_t i=0;i<v.accounts_count;++i) {
        const auto& a=r.accounts[d.transaction[v.accounts_offset+i]];
        accounts[i]={a.key,a.signer,a.writable};
    }
    return decode_solana_instruction(r.accounts[v.program_index].key,
        std::span(d.transaction).subspan(v.data_offset,v.data_size),std::span(accounts).first(v.accounts_count));
}
}
CapturedBuyInspection inspect_captured_buy_transaction(std::string_view envelope,std::string_view organization,
    std::string_view signer,const CapturedBuyProfile& p,std::span<const SolanaLookupEvidence> evidence) noexcept {
    if(organization.empty() || signer.empty() || p.now_ms==0 || p.max_envelope_age_ms==0 || p.max_envelope_age_ms>30'000 ||
        p.max_compute_units==0 || p.max_compute_units>1'400'000 || p.max_slippage_bps>10'000 || p.max_platform_fee_bps>100 ||
        p.intent.side!=Side::buy || !p.intent.effect.value() || !p.intent.quote_input.value() ||
        p.intent.quote_input.value()>p.intent.max_quote_debit.value() || !p.intent.min_base_receive.value() ||
        p.intent.max_quote_debit.value()>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        p.intent.max_quote_debit.value()-p.intent.quote_input.value()!=p.intent.fee_cap.value() ||
        p.intent.base_input.value()!=0 || p.intent.min_quote_receive.value()!=0) return {};
    if(p.account_keys[26]!=wrapped_sol || p.account_keys[29]!=usdc ||
        p.account_keys[8]!=kSolanaSystemProgram || p.account_keys[9]!=kSolanaComputeBudgetProgram ||
        p.account_keys[10]!=jupiter || p.account_keys[11]!=kSolanaClassicTokenProgram ||
        p.account_keys[13]!=kSolanaAssociatedTokenProgram) return {};
    DecodedSolanaPayload d;
    if(decode_solana_signing_envelope(envelope,organization,signer,d)!=SolanaParseError::none ||
        d.timestamp_ms<=0 || static_cast<std::uint64_t>(d.timestamp_ms)>p.now_ms ||
        p.now_ms-static_cast<std::uint64_t>(d.timestamp_ms)>p.max_envelope_age_ms || d.nonzero_signatures!=0)
        return {Code::invalid_envelope};
    ResolvedSolanaAccounts r;
    if(resolve_solana_accounts(d,organization,signer,evidence,r)!=SolanaResolutionError::none)
        return {Code::invalid_accounts};
    if(!d.version_zero || d.required_signatures!=2 || d.signer_index!=1 || d.static_key_count!=15 ||
        r.account_count!=33 || r.lookup_count!=2) return {Code::identity_mismatch};
    for(std::size_t i=0;i<r.account_count;++i)
        if(r.accounts[i].key!=p.account_keys[i] || r.accounts[i].signer!=(i<2) || r.accounts[i].writable!=(i<8 || (i>=15 && i<26)))
            return {Code::identity_mismatch};
    if(d.instruction_count!=6) return {Code::unsupported_topology};
    constexpr std::array<std::uint8_t,6> programs{9,9,13,10,11,8};
    for(std::size_t i=0;i<6;++i) if(d.instructions[i].program_index!=programs[i]) return {Code::unsupported_topology};
    constexpr std::array<std::uint8_t,6> ata{0,5,1,26,8,11};
    constexpr std::array<std::uint8_t,3> close{5,1,1};
    constexpr std::array<std::uint8_t,2> transfer{1,0};
    if(!account_indices(d,0,{}) || !account_indices(d,1,{}) || !account_indices(d,2,ata) ||
        !account_indices(d,3,route_accounts) || !account_indices(d,4,close) || !account_indices(d,5,transfer))
        return {Code::unsupported_topology};
    const auto compute=small_instruction(d,r,0), price=small_instruction(d,r,1), create=small_instruction(d,r,2),
        closing=small_instruction(d,r,4), payment=small_instruction(d,r,5);
    if(!std::holds_alternative<ComputeUnitLimit>(compute.body) || !std::holds_alternative<ComputeUnitPrice>(price.body) ||
        !std::holds_alternative<CreateAssociatedTokenIdempotent>(create.body) || !std::holds_alternative<CloseTokenAccount>(closing.body) ||
        !std::holds_alternative<TransferLamports>(payment.body)) return {Code::invalid_instruction};
    const auto units=std::get<ComputeUnitLimit>(compute.body).units;
    const auto micros=std::get<ComputeUnitPrice>(price.body).micro_lamports;
    if(units==0 || units>p.max_compute_units || micros>p.max_compute_price ||
        micros>std::numeric_limits<std::uint64_t>::max()/units) return {Code::invalid_instruction};
    const auto& ix=d.instructions[3];
    const auto decoded=decode_jupiter_shared_route_data(std::span(d.transaction).subspan(ix.data_offset,ix.data_size));
    if(decoded.status!=JupiterRouteStatus::decoded_data_only) return {Code::invalid_route};
    const auto& route=decoded.data;
    if(route.authority_id!=6 || route.step_count!=2 ||
        route.steps[0]!=JupiterRouteStep{JupiterSwapKind::deriverse,0,100,0,1,1} ||
        route.steps[1]!=JupiterRouteStep{JupiterSwapKind::bison_fi_v2,0,100,1,2,0}) return {Code::invalid_route};
    if(route.input_atoms!=p.intent.quote_input.value() || route.quoted_output_atoms==0 ||
        route.slippage_bps>p.max_slippage_bps || route.platform_fee_bps>p.max_platform_fee_bps)
        return {Code::route_bound_mismatch};
    CapturedBuyInspection result;
    result.code=Code::matched_capture_shape_only;
    result.blockers=route_economics_unproven|account_lifecycle_unproven|cpi_semantics_unproven|committed_authority_unproven|
        chain_validity_unproven|lookup_provenance_unproven;
    result.envelope_digest=sha256_digest(std::as_bytes(std::span(envelope.data(),envelope.size())));
    result.accounts=r;result.route=route;
    result.explicit_owner_payment=std::get<TransferLamports>(payment.body).lamports;
    result.compute_price=micros;result.compute_units=units;
    const auto product=micros*units;
    result.priority_fee_upper_lamports=product/1'000'000+(product%1'000'000!=0);
    const auto factor=10'000-route.slippage_bps;
    result.nominal_output_after_slippage=(route.quoted_output_atoms/10'000)*factor+
        ((route.quoted_output_atoms%10'000)*factor+9'999)/10'000;
    if(result.explicit_owner_payment>0) result.blockers|=auxiliary_sol_payment_conflict;
    if(result.nominal_output_after_slippage<p.intent.min_base_receive.value()) result.blockers|=nominal_slippage_shortfall;
    return result;
}
}
