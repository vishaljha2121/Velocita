// Offline diagnostic only. Reads reviewed public documents; no keys, network or startup.
#include "velocita/v2/swap_runtime_production.hpp"
#include "velocita/v2/swap_transaction_review.hpp"
#include "velocita/v2/swap_signing_connection.hpp"
#include "velocita/v2/swap_signer_session.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace velocita::v2;
std::string bounded_read(const char* path, std::size_t maximum) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("unreadable document");
    std::string bytes(maximum+1,'\0');
    input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    const auto n=input.gcount();
    if(n<=0 || static_cast<std::size_t>(n)>maximum || input.bad())throw std::runtime_error("document size or read failure");
    bytes.resize(static_cast<std::size_t>(n));return bytes;
}
std::string hex(const Digest256& digest) {
    constexpr char d[]="0123456789abcdef";std::string out;
    for(auto byte:digest.bytes){const auto b=std::to_integer<unsigned>(byte);out+=d[b>>4];out+=d[b&15];}return out;
}

#include "velocita/v2/solana_payload_view.hpp"
#include "velocita/v2/solana_instruction_decode.hpp"
#include "velocita/v2/jupiter_route_decode.hpp"
#include <array>
using namespace velocita::v2::truemarkets;
int main(int argc,char** argv) {
 try {
  if(argc!=4)throw std::runtime_error("expected profile, transaction review, envelope paths");
  const auto profile_bytes=bounded_read(argv[1],8192),review_bytes=bounded_read(argv[2],32768),envelope=bounded_read(argv[3],8192);
  const auto profile=decode_swap_runtime_profile(profile_bytes);
  if(!profile)throw std::runtime_error("runtime profile refused");
  const auto& b=profile->gateway.binding;
  const auto review=decode_swap_transaction_review(review_bytes,b.transaction_review_digest,b,profile->organization.view(),profile->signer_id.view());
  if(!review || review->policy.account_keys[0]!=profile->failure_policy.expected_external_fee_payer || b.max_buy_output_deduction_lamports!=2039280)throw std::runtime_error("review or economics refused");
  DecodedSolanaPayload decoded;
  const auto parse=decode_solana_signing_envelope(envelope,profile->organization.view(),profile->signer_id.view(),decoded);
  if(parse!=SolanaParseError::none || decoded.timestamp_ms<=0 || (decoded.instruction_count!=6 && decoded.instruction_count!=5))throw std::runtime_error("envelope or expected instruction count refused");
  const auto& ix=decoded.instructions[decoded.instruction_count==5?2:3];
  const auto route=decode_jupiter_direct_route_data(std::span(decoded.transaction).subspan(ix.data_offset,ix.data_size));
  if(route.status!=JupiterRouteStatus::decoded_data_only || route.data.slippage_bps>10000)throw std::runtime_error("direct route decode refused");
  const auto factor=10000-route.data.slippage_bps;
  const auto quoted=route.data.quoted_output_atoms;
  if(quoted>static_cast<std::uint64_t>(INT64_MAX))throw std::runtime_error("quote exceeds integer bound");
  const auto gross=(quoted/10000)*factor+((quoted%10000)*factor+9999)/10000;
  ResolvedSolanaAccounts resolved;
  const auto resolution=resolve_solana_accounts(decoded,profile->organization.view(),profile->signer_id.view(),std::span(&review->lookup,1),resolved);
  std::uint64_t repayment=0;bool transfer_decoded=false;
  if(resolution==SolanaResolutionError::none){
   const auto& pay=decoded.instructions[decoded.instruction_count-1];std::array<SolanaInstructionAccount,256> ac{};
   for(std::size_t i=0;i<pay.accounts_count;++i){const auto& account=resolved.accounts[decoded.transaction[pay.accounts_offset+i]];ac[i]={account.key,account.signer,account.writable};}
   const auto payment=decode_solana_instruction(resolved.accounts[pay.program_index].key,std::span(decoded.transaction).subspan(pay.data_offset,pay.data_size),std::span(ac).first(pay.accounts_count));
   if(std::holds_alternative<TransferLamports>(payment.body)){repayment=std::get<TransferLamports>(payment.body).lamports;transfer_decoded=true;}
  }
  swap::Pending pending;pending.phase=swap::Phase::prepared;pending.handle.bytes[0]=std::byte{1};pending.payload=sha256_digest(std::as_bytes(std::span(envelope)));
  auto& intent=pending.intent;intent.binding=b.allocation;intent.effect=EffectId{1};intent.side=Side::buy;
  intent.quote_input=swap::QuoteAtoms{998000};intent.max_quote_debit=swap::QuoteAtoms{1000000};intent.fee_cap=swap::QuoteAtoms{2000};
  intent.min_base_receive=swap::BaseAtoms{1};intent.source_time=static_cast<std::uint64_t>(decoded.timestamp_ms);intent.sign_by=intent.source_time+1000;intent.kill_epoch=1;intent.executor_generation=1;
  const auto clock=intent.source_time+1;
  const auto result=verify_swap_transaction(envelope,profile->organization.view(),profile->signer_id.view(),pending,review->policy,std::span(&review->lookup,1),clock);
  unsigned floor_code=255,above_code=255;
  if(result.verified){
   intent.min_base_receive=swap::BaseAtoms{result.bounds.net_sol_floor};
   floor_code=static_cast<unsigned>(verify_swap_transaction(envelope,profile->organization.view(),profile->signer_id.view(),pending,review->policy,std::span(&review->lookup,1),clock).code);
   intent.min_base_receive=swap::BaseAtoms{result.bounds.net_sol_floor+1};
   above_code=static_cast<unsigned>(verify_swap_transaction(envelope,profile->organization.view(),profile->signer_id.view(),pending,review->policy,std::span(&review->lookup,1),clock).code);
  }
  std::cout << "payload_sha256=" << hex(pending.payload) << '\n'
   << "policy_sha256=" << hex(direct_buy_policy_digest(review->policy)) << '\n'
   << "transaction_review_sha256=" << hex(b.transaction_review_digest) << '\n'
   << "code=" << static_cast<unsigned>(result.code) << '\n'
   << "verified=" << result.verified.has_value() << '\n'
   << "resolution_code=" << static_cast<unsigned>(resolution) << '\n'
   << "native_route_kind=" << static_cast<unsigned>(route.data.steps[0].kind) << '\n'
   << "native_step_count=" << static_cast<unsigned>(route.data.step_count) << '\n'
   << "quoted_output_lamports=" << quoted << '\n'
   << "native_slippage_bps=" << route.data.slippage_bps << '\n'
   << "native_platform_fee_bps=" << static_cast<unsigned>(route.data.platform_fee_bps) << '\n'
   << "native_input_atoms=" << route.data.input_atoms << '\n'
   << "native_gross_floor_lamports=" << gross << '\n'
   << "native_repayment_decoded=" << transfer_decoded << '\n'
   << "native_repayment_lamports=" << repayment << '\n'
   << "native_conservative_net_candidate=" << ((transfer_decoded && gross>repayment)?gross-repayment:0) << '\n'
   << "verified_net_floor_lamports=" << result.bounds.net_sol_floor << '\n'
   << "verified_debit_usdc_atoms=" << result.bounds.total_usdc_debit << '\n'
   << "verified_fee_usdc_atoms=" << result.bounds.explicit_usdc_fee << '\n'
   << "exact_floor_verification_code=" << floor_code << '\n'
   << "floor_plus_one_verification_code=" << above_code << '\n'
   << "diagnostic_clock_ms=" << clock << '\n'
   << "policy_expiry_ms=" << review->policy.expires_at_ms << '\n';
  return result.verified && floor_code==12 && above_code==11 ? 0:2;
 }catch(const std::exception& error){std::cerr << "REFUSED: " << error.what() << '\n';return 64;}
}
