#include "velocita/v2/node_protocol.hpp"

#include "velocita/v2/digest.hpp"

#include <algorithm>

namespace velocita::v2 {
namespace {

static_assert(static_cast<int>(ProposeStatus::not_leader) == 2 &&
                  static_cast<int>(ProposeStatus::persistence_busy) == 9,
              "response refusal reasons 2..9 mirror ProposeStatus; extend the layout review first");
static_assert(static_cast<int>(ProposeLane::normal) == 0 && static_cast<int>(ProposeLane::control) == 1);

constexpr std::size_t kCorrelationBytes = 8 + 32;

bool valid_reason(std::uint8_t reason) noexcept {
    return (reason >= 2 && reason <= 9) || (reason >= 64 && reason <= 67) || reason == static_cast<std::uint8_t>(IngressRefusal::signer_read_only);
}

std::size_t response_size(ResponseKind kind) noexcept {
    switch (kind) {
        case ResponseKind::admitted: return kResponseAdmittedBytes;
        case ResponseKind::refused: return kResponseRefusedBytes;
        case ResponseKind::committed: return kResponseCommittedBytes;
        case ResponseKind::gateway_refused:
        case ResponseKind::state_chunk:
        case ResponseKind::authority_granted:
        case ResponseKind::swap_authority_granted: return 0;
    }
    return 0;
}

template <typename T>
void put(std::span<std::byte> out, std::size_t at, T value) noexcept {
    auto raw = static_cast<std::make_unsigned_t<T>>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) out[at + i] = std::byte((raw >> (8 * i)) & 0xff);
}

template <typename T>
T get(std::span<const std::byte> in, std::size_t at) noexcept {
    std::make_unsigned_t<T> raw{};
    for (std::size_t i = 0; i < sizeof(T); ++i)
        raw |= static_cast<std::make_unsigned_t<T>>(std::to_integer<unsigned>(in[at + i])) << (8 * i);
    return static_cast<T>(raw);
}

}

EncodeNodeProtocolResult encode_ingress(ProposeLane lane, std::span<const std::byte> command,
                                        std::span<std::byte> output) noexcept {
    if (lane != ProposeLane::normal && lane != ProposeLane::control) return {NodeProtocolError::invalid_lane, 0};
    if (command.empty()) return {NodeProtocolError::empty_command, 0};
    if (command.size() > kMaxKernelCommandBytes) return {NodeProtocolError::command_too_large, 0};
    if (output.size() < 1 + command.size()) return {NodeProtocolError::output_too_small, 0};
    output[0] = static_cast<std::byte>(lane);
    std::copy(command.begin(), command.end(), output.begin() + 1);
    return {NodeProtocolError::none, 1 + command.size()};
}

NodeProtocolError decode_ingress(std::span<const std::byte> input, IngressView& destination) noexcept {
    if (input.empty()) return NodeProtocolError::truncated;
    if (input.size() > kMaxIngressBodyBytes) return NodeProtocolError::command_too_large;
    const auto lane = std::to_integer<std::uint8_t>(input[0]);
    if (lane > 1) return NodeProtocolError::invalid_lane;
    if (input.size() == 1) return NodeProtocolError::empty_command;
    destination = {static_cast<ProposeLane>(lane), input.subspan(1)};
    return NodeProtocolError::none;
}

EncodeNodeProtocolResult encode_response(const NodeResponse& response, std::span<std::byte> output) noexcept {
    const auto size = response_size(response.kind);
    if (size == 0) return {NodeProtocolError::invalid_kind, 0};
    if (response.kind == ResponseKind::refused && !valid_reason(response.reason))
        return {NodeProtocolError::invalid_reason, 0};
    if (output.size() < size) return {NodeProtocolError::output_too_small, 0};
    output[0] = static_cast<std::byte>(response.kind);
    put(output, 1, response.correlation.request_sequence);
    std::copy(response.correlation.payload_digest.bytes.begin(), response.correlation.payload_digest.bytes.end(),
              output.begin() + 9);
    constexpr std::size_t body = 1 + kCorrelationBytes;
    if (response.kind == ResponseKind::refused) {
        output[body] = static_cast<std::byte>(response.reason);
        put(output, body + 1, response.leader_id);
    } else {
        put(output, body, response.term);
        put(output, body + 8, response.index);
        if (response.kind == ResponseKind::committed)
            std::copy(response.result.begin(), response.result.end(), output.begin() + body + 16);
    }
    return {NodeProtocolError::none, size};
}

NodeProtocolError decode_response(std::span<const std::byte> input, NodeResponse& destination) noexcept {
    if (input.empty()) return NodeProtocolError::truncated;
    const auto kind = static_cast<ResponseKind>(std::to_integer<std::uint8_t>(input[0]));
    const auto size = response_size(kind);
    if (size == 0) return NodeProtocolError::invalid_kind;
    if (input.size() < size) return NodeProtocolError::truncated;
    if (input.size() != size) return NodeProtocolError::length_mismatch;
    NodeResponse decoded;
    decoded.kind = kind;
    decoded.correlation.request_sequence = get<std::uint64_t>(input, 1);
    std::copy(input.begin() + 9, input.begin() + 41, decoded.correlation.payload_digest.bytes.begin());
    constexpr std::size_t body = 1 + kCorrelationBytes;
    if (kind == ResponseKind::refused) {
        decoded.reason = std::to_integer<std::uint8_t>(input[body]);
        if (!valid_reason(decoded.reason)) return NodeProtocolError::invalid_reason;
        decoded.leader_id = get<std::int32_t>(input, body + 1);
    } else {
        decoded.term = get<std::int64_t>(input, body);
        decoded.index = get<std::int64_t>(input, body + 8);
        if (kind == ResponseKind::committed)
            std::copy(input.begin() + body + 16, input.end(), decoded.result.begin());
    }
    destination = decoded;
    return NodeProtocolError::none;
}

namespace {

constexpr std::size_t kBody = 1 + kCorrelationBytes;

bool valid_gateway_reason(std::uint8_t reason) noexcept {
    return (reason >= 2 && reason <= 9) || (reason >= 64 && reason <= 70);
}

void put_correlation(std::span<std::byte> out, const ResponseCorrelation& correlation) noexcept {
    put(out, 1, correlation.request_sequence);
    std::copy(correlation.payload_digest.bytes.begin(), correlation.payload_digest.bytes.end(), out.begin() + 9);
}

ResponseCorrelation get_correlation(std::span<const std::byte> in) noexcept {
    ResponseCorrelation correlation;
    correlation.request_sequence = get<std::uint64_t>(in, 1);
    std::copy(in.begin() + 9, in.begin() + 41, correlation.payload_digest.bytes.begin());
    return correlation;
}

void put_digest(std::span<std::byte> out, std::size_t at, const Digest256& digest) noexcept {
    std::copy(digest.bytes.begin(), digest.bytes.end(), out.begin() + static_cast<std::ptrdiff_t>(at));
}

Digest256 get_digest(std::span<const std::byte> in, std::size_t at) noexcept {
    Digest256 digest;
    std::copy_n(in.begin() + static_cast<std::ptrdiff_t>(at), digest.bytes.size(), digest.bytes.begin());
    return digest;
}

bool valid_mode(std::uint8_t mode) noexcept { return mode >= 1 && mode <= 6; }

}

bool is_gateway_request(std::span<const std::byte> body) noexcept {
    const auto kind = response_kind_byte(body);
    return kind == static_cast<std::uint8_t>(GatewayRequestKind::state_read) ||
           kind == static_cast<std::uint8_t>(GatewayRequestKind::authority) ||
           kind == static_cast<std::uint8_t>(GatewayRequestKind::swap_authority);
}

EncodeNodeProtocolResult encode_gateway_request(const GatewayRequest& request, std::span<std::byte> output) noexcept {
    const bool authority = request.kind == GatewayRequestKind::authority || request.kind == GatewayRequestKind::swap_authority;
    if (!authority && request.kind != GatewayRequestKind::state_read) return {NodeProtocolError::invalid_kind, 0};
    if (request.request_id == 0) return {NodeProtocolError::invalid_value, 0};
    if (authority && request.barrier.incarnation != request.incarnation) return {NodeProtocolError::invalid_value, 0};
    const auto size = authority ? kGatewayAuthorityRequestBytes : kGatewayStateReadBytes;
    if (output.size() < size) return {NodeProtocolError::output_too_small, 0};
    std::array<std::byte, kGatewayAuthorityRequestBytes> frame{};
    frame[0] = static_cast<std::byte>(request.kind);
    frame[1] = static_cast<std::byte>(kGatewayRequestVersion);
    std::copy(request.incarnation.begin(), request.incarnation.end(), frame.begin() + 2);
    put(frame, 18, request.request_id);
    if (authority &&
        encode_authority_barrier(kAuthorityBarrierEntryType, request.barrier,
                                 std::span(frame).subspan(kGatewayRequestHeaderBytes)).error !=
            AuthorityBarrierCodecError::none)
        return {NodeProtocolError::invalid_value, 0};
    std::copy_n(frame.begin(), size, output.begin());
    return {NodeProtocolError::none, size};
}

NodeProtocolError decode_gateway_request(std::span<const std::byte> input, GatewayRequest& destination) noexcept {
    if (input.size() < kGatewayRequestHeaderBytes) return NodeProtocolError::truncated;
    if (!is_gateway_request(input)) return NodeProtocolError::invalid_kind;
    if (std::to_integer<std::uint8_t>(input[1]) != kGatewayRequestVersion) return NodeProtocolError::invalid_version;
    GatewayRequest decoded;
    decoded.kind = static_cast<GatewayRequestKind>(std::to_integer<std::uint8_t>(input[0]));
    const bool authority = decoded.kind == GatewayRequestKind::authority || decoded.kind == GatewayRequestKind::swap_authority;
    const auto size = authority ? kGatewayAuthorityRequestBytes : kGatewayStateReadBytes;
    if (input.size() < size) return NodeProtocolError::truncated;
    if (input.size() != size) return NodeProtocolError::length_mismatch;
    std::copy_n(input.begin() + 2, decoded.incarnation.size(), decoded.incarnation.begin());
    decoded.request_id = get<std::uint64_t>(input, 18);
    if (decoded.request_id == 0) return NodeProtocolError::invalid_value;
    if (authority) {
        if (decode_authority_barrier(kAuthorityBarrierEntryType, input.subspan(kGatewayRequestHeaderBytes),
                                     decoded.barrier).error != AuthorityBarrierCodecError::none ||
            decoded.barrier.incarnation != decoded.incarnation)
            return NodeProtocolError::invalid_value;
    }
    destination = decoded;
    return NodeProtocolError::none;
}

ResponseCorrelation gateway_correlation(std::span<const std::byte> request_body) noexcept {
    static constexpr std::string_view kTag = "velocita.v2.gateway-request.1";
    Sha256 hash;
    hash.update(std::as_bytes(std::span(kTag.data(), kTag.size())));
    hash.update(request_body);
    return {request_body.size() >= kGatewayRequestHeaderBytes ? get<std::uint64_t>(request_body, 18) : 0,
            hash.finish()};
}

EncodeNodeProtocolResult encode_gateway_refusal(const GatewayRefusal& refusal, std::span<std::byte> output) noexcept {
    if (!valid_gateway_reason(refusal.reason)) return {NodeProtocolError::invalid_reason, 0};
    if (output.size() < kResponseGatewayRefusedBytes) return {NodeProtocolError::output_too_small, 0};
    output[0] = static_cast<std::byte>(ResponseKind::gateway_refused);
    put_correlation(output, refusal.correlation);
    output[kBody] = static_cast<std::byte>(refusal.reason);
    put(output, kBody + 1, refusal.leader_id);
    put(output, kBody + 5, refusal.term);
    return {NodeProtocolError::none, kResponseGatewayRefusedBytes};
}

NodeProtocolError decode_gateway_refusal(std::span<const std::byte> input, GatewayRefusal& destination) noexcept {
    if (response_kind_byte(input) != static_cast<std::uint8_t>(ResponseKind::gateway_refused))
        return input.empty() ? NodeProtocolError::truncated : NodeProtocolError::invalid_kind;
    if (input.size() < kResponseGatewayRefusedBytes) return NodeProtocolError::truncated;
    if (input.size() != kResponseGatewayRefusedBytes) return NodeProtocolError::length_mismatch;
    GatewayRefusal decoded;
    decoded.correlation = get_correlation(input);
    decoded.reason = std::to_integer<std::uint8_t>(input[kBody]);
    if (!valid_gateway_reason(decoded.reason)) return NodeProtocolError::invalid_reason;
    decoded.leader_id = get<std::int32_t>(input, kBody + 1);
    decoded.term = get<std::int64_t>(input, kBody + 5);
    destination = decoded;
    return NodeProtocolError::none;
}

namespace {

bool valid_geometry(const StateChunkHeader& header, std::size_t chunk_bytes) noexcept {
    if (header.snapshot_bytes == 0 || header.snapshot_bytes > kMaxStateSnapshotBytes) return false;
    const auto count = (header.snapshot_bytes + kStateChunkPayloadBytes - 1) / kStateChunkPayloadBytes;
    if (header.chunk_count != count || header.chunk_index >= header.chunk_count) return false;
    const auto offset = static_cast<std::size_t>(header.chunk_index) * kStateChunkPayloadBytes;
    return chunk_bytes == std::min(kStateChunkPayloadBytes, header.snapshot_bytes - offset);
}

}

EncodeNodeProtocolResult encode_state_chunk(const StateChunkHeader& header, std::span<const std::byte> chunk,
                                            std::span<std::byte> output) noexcept {
    if (!valid_geometry(header, chunk.size())) return {NodeProtocolError::invalid_geometry, 0};
    const auto size = kStateChunkHeaderBytes + chunk.size();
    if (output.size() < size) return {NodeProtocolError::output_too_small, 0};
    output[0] = static_cast<std::byte>(ResponseKind::state_chunk);
    put_correlation(output, header.correlation);
    put(output, 41, header.responder_id);
    put(output, 45, header.leader_id);
    put(output, 49, header.term);
    put(output, 57, header.applied_index);
    put_digest(output, 65, header.authority_domain);
    put(output, 97, header.snapshot_bytes);
    put_digest(output, 101, header.snapshot_digest);
    put(output, 133, header.chunk_index);
    put(output, 135, header.chunk_count);
    put(output, 137, static_cast<std::uint32_t>(chunk.size()));
    std::copy(chunk.begin(), chunk.end(), output.begin() + kStateChunkHeaderBytes);
    return {NodeProtocolError::none, size};
}

NodeProtocolError decode_state_chunk(std::span<const std::byte> input, StateChunkView& destination) noexcept {
    if (response_kind_byte(input) != static_cast<std::uint8_t>(ResponseKind::state_chunk))
        return input.empty() ? NodeProtocolError::truncated : NodeProtocolError::invalid_kind;
    if (input.size() < kStateChunkHeaderBytes) return NodeProtocolError::truncated;
    StateChunkHeader header;
    header.correlation = get_correlation(input);
    header.responder_id = get<std::int32_t>(input, 41);
    header.leader_id = get<std::int32_t>(input, 45);
    header.term = get<std::int64_t>(input, 49);
    header.applied_index = get<std::int64_t>(input, 57);
    header.authority_domain = get_digest(input, 65);
    header.snapshot_bytes = get<std::uint32_t>(input, 97);
    header.snapshot_digest = get_digest(input, 101);
    header.chunk_index = get<std::uint16_t>(input, 133);
    header.chunk_count = get<std::uint16_t>(input, 135);
    const auto length = get<std::uint32_t>(input, 137);
    if (input.size() - kStateChunkHeaderBytes != length) return NodeProtocolError::length_mismatch;
    if (!valid_geometry(header, length)) return NodeProtocolError::invalid_geometry;
    destination = {header, input.subspan(kStateChunkHeaderBytes)};
    return NodeProtocolError::none;
}

EncodeNodeProtocolResult encode_authority_granted(const ResponseCorrelation& correlation,
                                                  const AuthorityProofView& view,
                                                  std::span<std::byte> output) noexcept {
    if (output.size() < kResponseAuthorityGrantedBytes) return {NodeProtocolError::output_too_small, 0};
    if (!valid_mode(static_cast<std::uint8_t>(view.strategy_mode))) return {NodeProtocolError::invalid_value, 0};
    std::array<std::byte, kResponseAuthorityGrantedBytes> frame{};
    frame[0] = static_cast<std::byte>(ResponseKind::authority_granted);
    put_correlation(frame, correlation);
    if (encode_authority_barrier(kAuthorityBarrierEntryType, view.request, std::span(frame).subspan(41)).error !=
        AuthorityBarrierCodecError::none)
        return {NodeProtocolError::invalid_value, 0};
    const auto effect = encode_payload(view.binding.effect);
    if (effect.error != PayloadError::none || effect.bytes.size() != kOrderEffectPayloadBytes)
        return {NodeProtocolError::invalid_value, 0};
    std::copy(effect.bytes.begin(), effect.bytes.end(), frame.begin() + 227);
    put_digest(frame, 393, view.binding.catalog_root);
    put(frame, 425, view.binding.instrument_spec_version.value());
    put(frame, 433, view.binding.venue_capabilities_version.value());
    put(frame, 441, view.issuer_id);
    put(frame, 445, view.term);
    put(frame, 453, view.barrier_index);
    put(frame, 461, view.applied_index);
    put(frame, 469, view.state_version);
    frame[477] = static_cast<std::byte>(view.strategy_mode);
    std::copy(frame.begin(), frame.end(), output.begin());
    return {NodeProtocolError::none, kResponseAuthorityGrantedBytes};
}

EncodeNodeProtocolResult encode_swap_authority_granted(const ResponseCorrelation& correlation,const SwapAuthorityProofView& view,std::span<std::byte> output)noexcept{
    if(output.size()<kResponseSwapAuthorityGrantedBytes)return {NodeProtocolError::output_too_small,0};
    if(!correlation.request_sequence || correlation.payload_digest.is_zero() || view.issuer_id<=0 || view.term<=0 ||
        view.barrier_index<=0 || view.applied_index<view.barrier_index)return {NodeProtocolError::invalid_value,0};
    std::array<std::byte,kResponseSwapAuthorityGrantedBytes> frame{};
    frame[0]=static_cast<std::byte>(ResponseKind::swap_authority_granted);put_correlation(frame,correlation);
    if(encode_authority_barrier(kAuthorityBarrierEntryType,view.request,std::span(frame).subspan(41,kAuthorityBarrierBytes)).error!=AuthorityBarrierCodecError::none)
        return {NodeProtocolError::invalid_value,0};
    put(frame,227,view.issuer_id);put(frame,231,view.term);put(frame,239,view.barrier_index);put(frame,247,view.applied_index);
    std::copy(frame.begin(),frame.end(),output.begin());return {NodeProtocolError::none,frame.size()};
}
NodeProtocolError decode_swap_authority_granted(std::span<const std::byte> input,ResponseCorrelation& correlation,SwapAuthorityProofView& destination)noexcept{
    if(input.size()<kResponseSwapAuthorityGrantedBytes)return NodeProtocolError::truncated;
    if(input.size()!=kResponseSwapAuthorityGrantedBytes)return NodeProtocolError::length_mismatch;
    if(response_kind_byte(input)!=static_cast<std::uint8_t>(ResponseKind::swap_authority_granted))return NodeProtocolError::invalid_kind;
    SwapAuthorityProofView view;
    if(decode_authority_barrier(kAuthorityBarrierEntryType,input.subspan(41,kAuthorityBarrierBytes),view.request).error!=AuthorityBarrierCodecError::none)
        return NodeProtocolError::invalid_value;
    view.issuer_id=get<std::int32_t>(input,227);view.term=get<std::int64_t>(input,231);
    view.barrier_index=get<std::int64_t>(input,239);view.applied_index=get<std::int64_t>(input,247);
    const auto found=get_correlation(input);
    if(!found.request_sequence || found.payload_digest.is_zero() || view.issuer_id<=0 || view.term<=0 ||
        view.barrier_index<=0 || view.applied_index<view.barrier_index)return NodeProtocolError::invalid_value;
    correlation=found;destination=view;return NodeProtocolError::none;
}

NodeProtocolError decode_authority_granted(std::span<const std::byte> input, ResponseCorrelation& correlation,
                                           AuthorityProofView& destination) noexcept {
    if (response_kind_byte(input) != static_cast<std::uint8_t>(ResponseKind::authority_granted))
        return input.empty() ? NodeProtocolError::truncated : NodeProtocolError::invalid_kind;
    if (input.size() < kResponseAuthorityGrantedBytes) return NodeProtocolError::truncated;
    if (input.size() != kResponseAuthorityGrantedBytes) return NodeProtocolError::length_mismatch;
    AuthorityProofView view;
    if (decode_authority_barrier(kAuthorityBarrierEntryType, input.subspan(41, kAuthorityBarrierBytes), view.request)
            .error != AuthorityBarrierCodecError::none)
        return NodeProtocolError::invalid_value;
    const auto effect = decode_order_effect(input.subspan(227, kOrderEffectPayloadBytes));
    if (effect.error != PayloadError::none || !effect.value) return NodeProtocolError::invalid_value;
    view.binding.effect = *effect.value;
    view.binding.catalog_root = get_digest(input, 393);
    view.binding.instrument_spec_version = InstrumentSpecVersion{get<std::uint64_t>(input, 425)};
    view.binding.venue_capabilities_version = VenueCapabilitiesVersion{get<std::uint64_t>(input, 433)};
    view.issuer_id = get<std::int32_t>(input, 441);
    view.term = get<std::int64_t>(input, 445);
    view.barrier_index = get<std::int64_t>(input, 453);
    view.applied_index = get<std::int64_t>(input, 461);
    view.state_version = get<std::uint64_t>(input, 469);
    const auto mode = std::to_integer<std::uint8_t>(input[477]);
    if (!valid_mode(mode)) return NodeProtocolError::invalid_value;
    view.strategy_mode = static_cast<StrategyMode>(mode);
    correlation = get_correlation(input);
    destination = view;
    return NodeProtocolError::none;
}

}
