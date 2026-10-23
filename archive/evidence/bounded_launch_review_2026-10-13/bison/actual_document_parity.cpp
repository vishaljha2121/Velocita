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
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("expected runtime.profile and transaction.review paths");
        const auto profile_bytes=bounded_read(argv[1],8192);
        const auto review_bytes=bounded_read(argv[2],32768);
        const auto profile=decode_swap_runtime_profile(profile_bytes);
        if(!profile)throw std::runtime_error("runtime profile decoder refused");
        const auto& b=profile->gateway.binding;
        const auto review=decode_swap_transaction_review(review_bytes,b.transaction_review_digest,b,profile->organization.view(),profile->signer_id.view());
        if(!review)throw std::runtime_error("transaction review decoder refused");
        if(review->policy.account_keys[0]!=profile->failure_policy.expected_external_fee_payer)throw std::runtime_error("sponsor pin differs");
        const std::string mapping="velocita.tm.fee-inclusive-principal-plus-cap.v1";
        const SwapPrepareBinding prepare{b.allocation,b.authority_domain,sha256_digest(std::as_bytes(std::span(mapping)))};
        SwapSignerConfig config{profile->signer.binding.allocation,profile->signer.binding.authority_domain,
            profile->signer.signer_instance,profile->signer.binding.signer_caller,profile->signer.key_version,profile->signer.state_timeout};
        config.transaction_policy_digest=swap_signer_verification_digest(*review);
        std::cout << "profile_document=" << hex(sha256_digest(std::as_bytes(std::span(profile_bytes)))) << '\n'
          << "transaction_document=" << hex(b.transaction_review_digest) << '\n'
          << "policy=" << hex(truemarkets::direct_buy_policy_digest(review->policy)) << '\n'
          << "lookup=" << hex(review->policy.lookup_image_digest) << '\n'
          << "verification=" << hex(config.transaction_policy_digest) << '\n'
          << "signing=" << hex(swap_signing_connection_binding({prepare,b.signer_public_key}).authority_domain) << '\n'
          << "protected=" << hex(swap_signer_journal_binding(config,b.signer_public_key).authority_domain) << '\n'
          << "runtime=" << hex(swap_runtime_identity_digest(b)) << '\n';
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "REFUSED: " << error.what() << '\n';return 64;
    }
}
