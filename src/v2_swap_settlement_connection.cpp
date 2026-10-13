#include "velocita/v2/swap_settlement_connection.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <utility>

#if !defined(VELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY) && !defined(VELOCITA_SWAP_RUNTIME_WIRING)
#error "Settlement connection requires isolated qualification before release integration"
#endif

namespace velocita::v2 {
SwapSettlementConnectionResult collect_retained_swap_settlement(
    const SwapPrepareStore& preparation,const SwapSubmissionStore& submission,const SwapPrepareBinding& binding,
    SwapFinalizedReader& reader,std::uint64_t request_id,std::uint64_t minimum_slot,std::string_view organization,std::string_view signer,
    std::span<const truemarkets::SolanaLookupEvidence> lookups,const truemarkets::SolanaAccountKey& owner){
    if(preparation.status()!=SwapPrepareStoreCode::none || submission.status()!=SwapSubmissionCode::none ||
        preparation.latest().phase!=SwapPreparePhase::prepared || preparation.latest().binding!=binding ||
        submission.latest().phase!=SwapSubmissionPhase::execute_intent ||
        submission.latest().binding!=SwapSubmissionBinding{binding.allocation.incarnation,binding.authority_domain})return {};
    // Owned snapshots remain stable even while the RPC blocks.
    const auto retained=preparation.latest();const auto sent=submission.latest();const auto& p=retained.prepared;
    if(sent.identity.effect!=retained.operation.effect || sent.identity.payload!=p.payload_digest ||
        sent.transaction==SwapSubmissionTransaction{} || !p.handle.length || p.handle.length>p.handle.bytes.size() ||
        !p.payload_size || p.payload_size>p.payload.size() || sent.committed_index<=retained.committed_index ||
        sent.identity.handle_size!=p.handle.length)return {};
    const auto handle=std::as_bytes(std::span(p.handle.bytes.data(),p.handle.length));
    if(sent.identity.handle_digest!=sha256_digest(handle) ||
        !std::equal(handle.begin(),handle.end(),sent.identity.handle.begin()) ||
        p.payload_digest!=sha256_digest(std::as_bytes(std::span(p.payload.data(),p.payload_size))))return {};
    const auto read=reader.read(sent.transaction,request_id,minimum_slot);
    if(preparation.status()!=SwapPrepareStoreCode::none || submission.status()!=SwapSubmissionCode::none ||
        preparation.latest().checksum!=retained.checksum || submission.latest().checksum!=sent.checksum)return {};
    if(read.code!=FinalizedReadCode::attributed_finalized_response || !read.evidence)
        return {SwapSettlementConnectionCode::rpc_unresolved,read.code,{}};
    auto evidence=decode_settlement_evidence(*read.evidence,p.payload_view(),organization,signer,lookups,owner);
    if((evidence.code!=SettlementEvidenceCode::observed_transaction_needs_account_lifecycle && evidence.code!=SettlementEvidenceCode::observed_failed_transaction) ||
        evidence.original_payload_digest!=sent.identity.payload || evidence.signature!=sent.transaction)
        return {SwapSettlementConnectionCode::evidence_refused,read.code,{}};
    const auto code=evidence.code==SettlementEvidenceCode::observed_failed_transaction?SwapSettlementConnectionCode::observed_failed_needs_accounting:SwapSettlementConnectionCode::observed_needs_account_lifecycle;
    return {code,read.code,std::move(evidence),read.evidence};
}
}
