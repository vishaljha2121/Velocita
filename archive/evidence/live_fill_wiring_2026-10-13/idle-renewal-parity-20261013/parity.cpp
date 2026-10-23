#include "v2_swap_transaction_verifier_test_support.hpp"
#include "velocita/v2/swap_runtime_config.hpp"
#include "velocita/v2/swap_signing_connection.hpp"
#include "velocita/v2/swap_signer_session.hpp"
#include <iostream>
#include <map>
using namespace velocita::v2;
template<class T>std::string hex(const T& bytes){const char*d="0123456789abcdef";std::string out;for(auto x:bytes){auto c=(unsigned char)x;out+=d[c>>4];out+=d[c&15];}return out;}
int main(){
 test_support::DirectBuyFixture f;SwapRuntimeBinding b;b.allocation=f.policy.binding;
 b.authority_domain.bytes.fill(std::byte{0x23});b.account_session.account_and_assets_digest.bytes.fill(std::byte{0x41});b.account_session.session_evidence_digest.bytes.fill(std::byte{0x26});
 for(unsigned i=0;i<32;++i)b.owner_wallet[i]=std::byte{f.policy.account_keys[1][i]};
 const std::string pub="036b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296";
 for(unsigned i=0;i<33;++i)b.signer_public_key[i]=std::byte{static_cast<unsigned char>(std::stoul(pub.substr(i*2,2),nullptr,16))};
 const std::string socket="/run/velocita-swap-signer/sign.sock";std::copy(socket.begin(),socket.end(),b.socket.begin());b.socket_size=socket.size();b.gateway_uid=29001;b.signer_uid=29002;b.gateway_caller=CallerId{22};b.signer_caller=CallerId{23};b.ipc_timeout_ms=1000;b.max_buy_output_deduction_lamports=550840;
 std::string accounts,addresses;for(auto k:f.policy.account_keys)accounts+=hex(k);for(unsigned i=0;i<f.lookup[0].address_count;++i)addresses+=hex(f.lookup[0].addresses[i]);
 std::map<std::string,std::string> t{{"schema","1"},{"account_keys",accounts},{"lookup_table",hex(f.lookup[0].table)},{"lookup_slot","777"},{"lookup_evidence",hex(f.lookup[0].evidence_digest.bytes)},{"lookup_address_count","8"},{"lookup_addresses",addresses},{"max_envelope_age_ms","100"},{"max_compute_price","71428"},{"max_compute_units","1400000"},{"max_slippage_bps","500"},{"expires_at_ms","2000"}};
 std::string tx;for(auto [k,v]:t){tx+=k+"="+v+"\n";std::cout<<"t."<<k<<"="<<v<<"\n";}b.transaction_review_digest=sha256_digest(std::as_bytes(std::span(tx)));
 std::map<std::string,std::string> p{{"incarnation",hex(b.allocation.incarnation.bytes)},{"catalog",hex(b.allocation.catalog.bytes)},{"custody",hex(b.allocation.custody.bytes)},{"authority_domain",hex(b.authority_domain.bytes)},{"account","2"},{"instrument","2"},{"policy","99"},{"policy_version","1"},{"risk_policy_version","1"},{"signer_public_key",pub},{"signer_instance",std::string(64,'5')},{"key_version","1"},{"max_buy_output_deduction_lamports","550840"},{"organization","org"},{"signer_id","signer"},{"account_assets",hex(b.account_session.account_and_assets_digest.bytes)},{"session_evidence",hex(b.account_session.session_evidence_digest.bytes)},{"owner_wallet",hex(b.owner_wallet)},{"gateway_uid","29001"},{"signer_uid","29002"},{"gateway_caller","22"},{"signer_caller","23"},{"ipc_timeout_ms","1000"},{"socket",socket},{"transaction_review_digest",hex(b.transaction_review_digest.bytes)}};
 for(auto [k,v]:p)std::cout<<"p."<<k<<"="<<v<<"\n";
 const std::string mapping="velocita.tm.fee-inclusive-principal-plus-cap.v1";SwapPrepareBinding prep{b.allocation,b.authority_domain,sha256_digest(std::as_bytes(std::span(mapping)))};
 SwapSignerVerificationContext ctx;ctx.policy=f.policy;ctx.lookup=f.lookup[0];ctx.organization=*truemarkets::tm_text("org");ctx.signer=*truemarkets::tm_text("signer");
 SwapSignerConfig cfg;cfg.binding=b.allocation;cfg.authority_domain=b.authority_domain;cfg.caller=CallerId{23};cfg.key_version=1;cfg.signer_instance.bytes.fill(std::byte{0x55});cfg.transaction_policy_digest=swap_signer_verification_digest(ctx);
 std::cout<<"x.signing="<<hex(swap_signing_connection_binding({prep,b.signer_public_key}).authority_domain.bytes)<<"\n";
 std::cout<<"x.protected="<<hex(swap_signer_journal_binding(cfg,b.signer_public_key).authority_domain.bytes)<<"\n";
 std::cout<<"x.runtime="<<hex(swap_runtime_identity_digest(b).bytes)<<"\n";
}
