// Request and response bodies between callers, the gateway and a voter.
#pragma once

#include "velocita/v2/auth_envelope.hpp"
#include "velocita/v2/authority_proof.hpp"
#include "velocita/v2/kernel_codec.hpp"
#include "velocita/v2/payload.hpp"
#include "velocita/v2/replication.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace velocita::v2 {

inline constexpr std::size_t kMaxIngressBodyBytes = 1 + kMaxKernelCommandBytes;
inline constexpr std::size_t kResponseAdmittedBytes = 57;
inline constexpr std::size_t kResponseRefusedBytes = 46;
inline constexpr std::size_t kResponseCommittedBytes = 57 + kKernelResultBytes;
inline constexpr std::size_t kMaxResponseBodyBytes = kResponseCommittedBytes;
static_assert(kKernelResultBytes == 128 && kResponseCommittedBytes == 185);

enum class IngressRefusal : std::uint8_t {
    caller_mismatch = 64,
    malformed_command = 65,
    not_ready = 66,
    busy = 67,
    not_gateway_caller = 68,
    authority_lost = 69,
    signer_read_only = 70,
};

enum class ResponseKind : std::uint8_t {
    admitted = 1, refused = 2, committed = 3,
    gateway_refused = 4, state_chunk = 5, authority_granted = 6, swap_authority_granted = 7,
};

struct ResponseCorrelation {
    std::uint64_t request_sequence{};
    Digest256 payload_digest{};
    constexpr auto operator<=>(const ResponseCorrelation&) const noexcept = default;
};

struct NodeResponse {
    ResponseKind kind{ResponseKind::refused};
    ResponseCorrelation correlation{};
    std::int64_t term{};
    std::int64_t index{};
    std::uint8_t reason{};
    std::int32_t leader_id{};
    std::array<std::byte, kKernelResultBytes> result{};
    constexpr auto operator<=>(const NodeResponse&) const noexcept = default;
};

enum class NodeProtocolError : std::uint8_t {
    none, output_too_small, truncated, length_mismatch, invalid_kind, invalid_lane, invalid_reason,
    empty_command, command_too_large, invalid_version, invalid_value, invalid_geometry,
};

struct IngressView {
    ProposeLane lane{ProposeLane::normal};
    std::span<const std::byte> command;
};

struct EncodeNodeProtocolResult {
    NodeProtocolError error{NodeProtocolError::none};
    std::size_t bytes_written{};
};

[[nodiscard]] EncodeNodeProtocolResult encode_ingress(
    ProposeLane lane, std::span<const std::byte> command, std::span<std::byte> output) noexcept;
[[nodiscard]] NodeProtocolError decode_ingress(
    std::span<const std::byte> input, IngressView& destination) noexcept;

[[nodiscard]] EncodeNodeProtocolResult encode_response(
    const NodeResponse& response, std::span<std::byte> output) noexcept;
[[nodiscard]] NodeProtocolError decode_response(
    std::span<const std::byte> input, NodeResponse& destination) noexcept;

enum class GatewayRequestKind : std::uint8_t { state_read = 0x10, authority = 0x11, swap_authority = 0x12 };
inline constexpr std::uint8_t kGatewayRequestVersion = 1;
inline constexpr std::size_t kGatewayRequestHeaderBytes = 26;
inline constexpr std::size_t kGatewayStateReadBytes = kGatewayRequestHeaderBytes;
inline constexpr std::size_t kGatewayAuthorityRequestBytes = kGatewayRequestHeaderBytes + kAuthorityBarrierBytes;
static_assert(kGatewayAuthorityRequestBytes == 212);

struct GatewayRequest {
    GatewayRequestKind kind{GatewayRequestKind::state_read};
    std::array<std::byte, 16> incarnation{};
    std::uint64_t request_id{};
    AuthorityBarrier barrier{};
    constexpr auto operator<=>(const GatewayRequest&) const noexcept = default;
};

[[nodiscard]] bool is_gateway_request(std::span<const std::byte> body) noexcept;
[[nodiscard]] EncodeNodeProtocolResult encode_gateway_request(
    const GatewayRequest& request, std::span<std::byte> output) noexcept;
[[nodiscard]] NodeProtocolError decode_gateway_request(
    std::span<const std::byte> input, GatewayRequest& destination) noexcept;

[[nodiscard]] ResponseCorrelation gateway_correlation(std::span<const std::byte> request_body) noexcept;

inline constexpr std::size_t kResponseGatewayRefusedBytes = 54;
struct GatewayRefusal {
    ResponseCorrelation correlation{};
    std::uint8_t reason{};
    std::int32_t leader_id{};
    std::int64_t term{};
    constexpr auto operator<=>(const GatewayRefusal&) const noexcept = default;
};

inline constexpr std::size_t kStateChunkHeaderBytes = 141;
inline constexpr std::size_t kStateChunkPayloadBytes = 24 * 1024;
inline constexpr std::size_t kMaxStateChunks =
    (kMaxStateSnapshotBytes + kStateChunkPayloadBytes - 1) / kStateChunkPayloadBytes;
inline constexpr std::size_t kMaxStateChunkResponseBytes = kStateChunkHeaderBytes + kStateChunkPayloadBytes;
static_assert(kMaxStateChunks == 3);
static_assert(kMaxStateChunkResponseBytes <= kMaxAuthBodyBytes);

struct StateChunkHeader {
    ResponseCorrelation correlation{};
    std::int32_t responder_id{};
    std::int32_t leader_id{};
    std::int64_t term{};
    std::int64_t applied_index{};
    Digest256 authority_domain{};
    std::uint32_t snapshot_bytes{};
    Digest256 snapshot_digest{};
    std::uint16_t chunk_index{};
    std::uint16_t chunk_count{};
    constexpr auto operator<=>(const StateChunkHeader&) const noexcept = default;
};

struct StateChunkView {
    StateChunkHeader header{};
    std::span<const std::byte> chunk;
};

inline constexpr std::size_t kResponseAuthorityGrantedBytes = 478;
inline constexpr std::size_t kResponseSwapAuthorityGrantedBytes=255;
[[nodiscard]] EncodeNodeProtocolResult encode_swap_authority_granted(const ResponseCorrelation&,const SwapAuthorityProofView&,std::span<std::byte>)noexcept;
[[nodiscard]] NodeProtocolError decode_swap_authority_granted(std::span<const std::byte>,ResponseCorrelation&,SwapAuthorityProofView&)noexcept;
static_assert(kResponseAuthorityGrantedBytes == 41 + kAuthorityBarrierBytes + kOrderEffectPayloadBytes + 32 + 8 + 8 + 4 + 8 * 4 + 1);

[[nodiscard]] EncodeNodeProtocolResult encode_gateway_refusal(
    const GatewayRefusal& refusal, std::span<std::byte> output) noexcept;
[[nodiscard]] NodeProtocolError decode_gateway_refusal(
    std::span<const std::byte> input, GatewayRefusal& destination) noexcept;

[[nodiscard]] EncodeNodeProtocolResult encode_state_chunk(
    const StateChunkHeader& header, std::span<const std::byte> chunk, std::span<std::byte> output) noexcept;
[[nodiscard]] NodeProtocolError decode_state_chunk(
    std::span<const std::byte> input, StateChunkView& destination) noexcept;

[[nodiscard]] EncodeNodeProtocolResult encode_authority_granted(
    const ResponseCorrelation& correlation, const AuthorityProofView& view, std::span<std::byte> output) noexcept;
[[nodiscard]] NodeProtocolError decode_authority_granted(
    std::span<const std::byte> input, ResponseCorrelation& correlation, AuthorityProofView& destination) noexcept;

[[nodiscard]] inline std::uint8_t response_kind_byte(std::span<const std::byte> input) noexcept {
    return input.empty() ? 0 : std::to_integer<std::uint8_t>(input[0]);
}

}
