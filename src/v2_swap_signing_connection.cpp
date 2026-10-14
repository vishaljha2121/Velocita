#include "velocita/v2/swap_signing_connection.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>

#if !defined(VELOCITA_SWAP_SIGNING_CONNECTION_TEST_ONLY) && !defined(VELOCITA_SWAP_RUNTIME_WIRING)
#error "Durable signer connection requires isolated qualification before release integration"
#endif

namespace velocita::v2 {
namespace {
void put64(std::span<std::byte> out,std::uint64_t value)noexcept{for(unsigned n=0;n<8;++n)out[n]=static_cast<std::byte>(value>>(8*n));}
std::uint64_t get64(std::span<const std::byte> in)noexcept{std::uint64_t v{};for(unsigned n=0;n<8;++n)v|=std::uint64_t(std::to_integer<unsigned>(in[n]))<<(8*n);return v;}
}
SwapSubmissionBinding swap_signing_connection_binding(const SwapSigningConnectionConfig& c)noexcept{
    constexpr std::string_view domain="velocita.gateway.signer-request.v1";
    std::array<std::byte,265> bytes{};std::size_t at{};
    for(const auto& d:{sha256_digest(std::as_bytes(std::span(domain.data(),domain.size()))),c.preparation.allocation.incarnation,
        c.preparation.allocation.catalog,c.preparation.allocation.custody,c.preparation.authority_domain,c.preparation.mapping_identity}){
        std::copy(d.bytes.begin(),d.bytes.end(),bytes.begin()+static_cast<std::ptrdiff_t>(at));at+=32;}
    std::copy(c.signer_public_key.begin(),c.signer_public_key.end(),bytes.begin()+static_cast<std::ptrdiff_t>(at));at+=33;
    for(const auto value:{c.preparation.allocation.account.value(),c.preparation.allocation.instrument.value(),c.preparation.allocation.policy.value(),
        c.preparation.allocation.policy_version.value(),c.preparation.allocation.risk_policy_version.value()}){put64(std::span(bytes).subspan(at),value);at+=8;}
    return {c.preparation.allocation.incarnation,sha256_digest(bytes)};
}
SwapSigningConnection::SwapSigningConnection(const SwapPrepareStore& p,SwapSubmissionStore& j,swap_signer::Client& client,SwapSigningConnectionConfig c,
    SwapLookupSource& lookups)
    :preparation_(p),journal_(j),client_(client),config_(c),lookups_(lookups){
    if(p.status()!=SwapPrepareStoreCode::none || p.latest().phase!=SwapPreparePhase::prepared || p.latest().binding!=c.preparation ||
        j.status()!=SwapSubmissionCode::none || j.latest().binding!=swap_signing_connection_binding(c) ||
        (c.signer_public_key[0]!=std::byte{2} && c.signer_public_key[0]!=std::byte{3}))return;
    preparation_checksum_=p.latest().checksum;
    if(j.latest().phase==SwapSubmissionPhase::idle)status_=SwapSigningConnectionStatus::idle;
    if(j.latest().phase==SwapSubmissionPhase::execute_intent)status_=SwapSigningConnectionStatus::unresolved;
}
bool SwapSigningConnection::assemble(std::uint64_t sequence,std::int64_t index,std::uint64_t expiry,swap_signer::Request& request,SwapSubmissionIdentity& identity)const{
    if(preparation_.status()!=SwapPrepareStoreCode::none || preparation_.latest().checksum!=preparation_checksum_ ||
        preparation_.latest().binding!=config_.preparation || preparation_.latest().phase!=SwapPreparePhase::prepared ||
        index<=preparation_.latest().committed_index)return false;
    const auto& record=preparation_.latest();const auto& p=record.prepared;
    if(!p.handle.length || p.handle.length>request.handle.size() || !p.payload_size || p.payload_size>request.payload.size())return false;
    request={};request.operation=swap_signer::Operation::sign;request.effect=record.operation.effect;
    request.sequence=sequence;request.minimum_index=index;request.sign_by_ms=expiry;
    request.handle_size=p.handle.length;request.payload_size=p.payload_size;
    std::copy_n(p.handle.bytes.begin(),p.handle.length,request.handle.begin());
    std::copy_n(reinterpret_cast<const std::byte*>(p.payload.data()),p.payload_size,request.payload.begin());
    if(sha256_digest(request.payload_view())!=p.payload_digest)return false;
    const auto lookup=lookups_.prefix(request.payload_view());if(!lookup)return false;request.lookup=*lookup;
    std::array<std::byte,swap_signer::kRequestBytes> frame{};const auto n=swap_signer::encode_request(request,frame);if(!n)return false;
    identity={};identity.effect=request.effect;identity.payload=p.payload_digest;identity.execution_request=sha256_digest(std::span(frame).first(n));
    // Index is the durable record's committed_index; sequence and expiry precede
    // the original handle. This record has a separate cryptographic domain.
    put64(identity.handle,sequence);put64(std::span(identity.handle).subspan(8),expiry);
    std::copy_n(reinterpret_cast<const std::byte*>(request.handle.data()),request.handle_size,identity.handle.begin()+16);
    identity.handle_size=16+request.handle_size;identity.handle_digest=sha256_digest(std::span(identity.handle).first(identity.handle_size));return true;
}
bool SwapSigningConnection::sign(std::uint64_t sequence,std::int64_t index,std::uint64_t expiry){
    if(status_!=SwapSigningConnectionStatus::idle || journal_.status()!=SwapSubmissionCode::none ||
        journal_.latest().binding!=swap_signing_connection_binding(config_) || journal_.latest().phase!=SwapSubmissionPhase::idle)return false;
    swap_signer::Request request;SwapSubmissionIdentity identity;
    if(!assemble(sequence,index,expiry,request,identity))return false;
    if(journal_.begin(identity,index)!=SwapSubmissionCode::none){status_=SwapSigningConnectionStatus::closed;return false;}
    request_checksum_=journal_.latest().checksum;status_=SwapSigningConnectionStatus::unresolved;
    return exchange(request);
}
bool SwapSigningConnection::retrieve(){
    if(status_!=SwapSigningConnectionStatus::unresolved || journal_.status()!=SwapSubmissionCode::none ||
        journal_.latest().binding!=swap_signing_connection_binding(config_) || journal_.latest().phase!=SwapSubmissionPhase::execute_intent)return false;
    const auto saved=journal_.latest();if(saved.identity.handle_size<=16 || saved.identity.handle_size>112)return false;
    swap_signer::Request request;SwapSubmissionIdentity identity;
    if(!assemble(get64(saved.identity.handle),saved.committed_index,get64(std::span(saved.identity.handle).subspan(8)),request,identity) ||
        identity!=saved.identity)return false;
    request.operation=swap_signer::Operation::retrieve;request_checksum_=saved.checksum;return exchange(request);
}
bool SwapSigningConnection::exchange(swap_signer::Request request){
    swap_signer::Reply reply;
    try{reply=client_.request(request);}catch(...){return false;}
    if(preparation_.status()!=SwapPrepareStoreCode::none || preparation_.latest().checksum!=preparation_checksum_ ||
        journal_.status()!=SwapSubmissionCode::none || journal_.latest().checksum!=request_checksum_ ||
        reply.public_key!=config_.signer_public_key || reply.stamp.empty() ||
        reply.code!=(request.operation==swap_signer::Operation::sign?swap_signer::ReplyCode::signed_once:swap_signer::ReplyCode::retrieved))return false;
    SwapSignedExecution result;const auto& p=preparation_.latest().prepared;
    if(truemarkets::encode_swap_rest_execute(p.handle,reply.stamp,result.request)!=truemarkets::SwapRestCode::encoded)return false;
    result.identity.effect=request.effect;result.identity.payload=p.payload_digest;result.identity.handle_size=p.handle.length;
    std::copy_n(reinterpret_cast<const std::byte*>(p.handle.bytes.data()),p.handle.length,result.identity.handle.begin());
    result.identity.handle_digest=sha256_digest(std::span(result.identity.handle).first(result.identity.handle_size));
    result.identity.execution_request=sha256_digest(std::as_bytes(std::span(result.request.body.data(),result.request.body_size)));
    execution_=result;status_=SwapSigningConnectionStatus::signed_once;return true;
}
const SwapSignedExecution* SwapSigningConnection::execution()const noexcept{
    if(status_!=SwapSigningConnectionStatus::signed_once || preparation_.status()!=SwapPrepareStoreCode::none ||
        preparation_.latest().checksum!=preparation_checksum_ || journal_.status()!=SwapSubmissionCode::none ||
        journal_.latest().checksum!=request_checksum_)return nullptr;
    return &execution_;
}
}
