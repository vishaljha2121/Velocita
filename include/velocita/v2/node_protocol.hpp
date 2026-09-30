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

// Ingress/response bodies carried inside an authenticated envelope
// (M2_NODE_PROCESS_REVIEW.md decision D; exact bytes in "D encoding").
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
    // Gateway requests only (reason byte of a gateway_refused response).
    not_gateway_caller = 68,
    // Admitted barrier that can no longer yield a proof: leadership or the
    // node-side bound was lost before the proof existed.
    authority_lost = 69,
};

// The gateway kinds are outside the command envelope's reply set, so a
// NodeClientSession decodes them as invalid_kind and ignores them.
enum class ResponseKind : std::uint8_t {
    admitted = 1, refused = 2, committed = 3,
    gateway_refused = 4, state_chunk = 5, authority_granted = 6,
};

// The request a response answers: copied from the command's own identity.
struct ResponseCorrelation {
    std::uint64_t request_sequence{};
    Digest256 payload_digest{};
    constexpr auto operator<=>(const ResponseCorrelation&) const noexcept = default;
};

struct NodeResponse {
    ResponseKind kind{ResponseKind::refused};
    ResponseCorrelation correlation{};
    std::int64_t term{};                 // admitted, committed
    std::int64_t index{};                // admitted, committed
    std::uint8_t reason{};               // refused: ProposeStatus 2..9 or IngressRefusal
    std::int32_t leader_id{};            // refused: meaningful for not_leader only
    std::array<std::byte, kKernelResultBytes> result{};  // committed: canonical result bytes
    constexpr auto operator<=>(const NodeResponse&) const noexcept = default;
};

enum class NodeProtocolError : std::uint8_t {
    none, output_too_small, truncated, length_mismatch, invalid_kind, invalid_lane, invalid_reason,
    empty_command, command_too_large, invalid_version, invalid_value, invalid_geometry,
};

struct IngressView {
    ProposeLane lane{ProposeLane::normal};
    std::span<const std::byte> command;  // borrows INPUT; not yet decoded
};

struct EncodeNodeProtocolResult {
    NodeProtocolError error{NodeProtocolError::none};
    std::size_t bytes_written{};
};

// Failure leaves OUTPUT unchanged. COMMAND must not overlap OUTPUT.
[[nodiscard]] EncodeNodeProtocolResult encode_ingress(
    ProposeLane lane, std::span<const std::byte> command, std::span<std::byte> output) noexcept;
// Structure only: the command bytes are still untrusted canonical input.
[[nodiscard]] NodeProtocolError decode_ingress(
    std::span<const std::byte> input, IngressView& destination) noexcept;

[[nodiscard]] EncodeNodeProtocolResult encode_response(
    const NodeResponse& response, std::span<std::byte> output) noexcept;
// Failure leaves DESTINATION unchanged. Fields not used by the kind decode as
// zero; the committed result bytes are opaque here (see decode_kernel_result).
[[nodiscard]] NodeProtocolError decode_response(
    std::span<const std::byte> input, NodeResponse& destination) noexcept;

// ---------------------------------------------------------------------------
// Gateway requests (Phase 2). Same authenticated ingress envelope; the first
// body byte is outside the lane range, so decode_ingress refuses these bytes.
//   0 kind u8 (0x10 state read | 0x11 authority) | 1 version u8 = 1 |
//   2 gateway process incarnation [16] | 18 request id u64 (nonzero) |
//   26 authority only: canonical AuthorityBarrier frame [186]
enum class GatewayRequestKind : std::uint8_t { state_read = 0x10, authority = 0x11 };
inline constexpr std::uint8_t kGatewayRequestVersion = 1;
inline constexpr std::size_t kGatewayRequestHeaderBytes = 26;
inline constexpr std::size_t kGatewayStateReadBytes = kGatewayRequestHeaderBytes;
inline constexpr std::size_t kGatewayAuthorityRequestBytes = kGatewayRequestHeaderBytes + kAuthorityBarrierBytes;
static_assert(kGatewayAuthorityRequestBytes == 212);

struct GatewayRequest {
    GatewayRequestKind kind{GatewayRequestKind::state_read};
    std::array<std::byte, 16> incarnation{};
    std::uint64_t request_id{};
    AuthorityBarrier barrier{};  // authority only
    constexpr auto operator<=>(const GatewayRequest&) const noexcept = default;
};

[[nodiscard]] bool is_gateway_request(std::span<const std::byte> body) noexcept;
[[nodiscard]] EncodeNodeProtocolResult encode_gateway_request(
    const GatewayRequest& request, std::span<std::byte> output) noexcept;
// Authority requests also require barrier.incarnation == incarnation.
[[nodiscard]] NodeProtocolError decode_gateway_request(
    std::span<const std::byte> input, GatewayRequest& destination) noexcept;

// What every gateway response echoes: the request id and SHA-256 over
// "velocita.v2.gateway-request.1" || the exact request body. The body carries
// the process incarnation, so a response can never match another process.
[[nodiscard]] ResponseCorrelation gateway_correlation(std::span<const std::byte> request_body) noexcept;

// gateway_refused, 54 bytes:
//   0 kind=4 | 1 correlation (u64 + [32]) | 41 reason u8 | 42 leader_id i32 | 46 term i64
inline constexpr std::size_t kResponseGatewayRefusedBytes = 54;
struct GatewayRefusal {
    ResponseCorrelation correlation{};
    std::uint8_t reason{};  // ProposeStatus 2..9 or IngressRefusal 64..69
    std::int32_t leader_id{};
    std::int64_t term{};
    constexpr auto operator<=>(const GatewayRefusal&) const noexcept = default;
};

// state_chunk, 141-byte header then chunk bytes:
//   0 kind=5 | 1 correlation | 41 responder_id i32 | 45 leader_id i32 | 49 term i64 |
//   57 applied_index i64 | 65 authority_domain [32] | 97 snapshot_bytes u32 |
//   101 snapshot SHA-256 [32] | 133 chunk_index u16 | 135 chunk_count u16 |
//   137 chunk_bytes u32 | 141 chunk
// Chunk i carries snapshot bytes [i * kStateChunkPayloadBytes, ...); every
// chunk but the last is full. The snapshot is the responder's APPLIED state.
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
    std::span<const std::byte> chunk;  // borrows INPUT
};

// authority_granted, 478 bytes:
//   0 kind=6 | 1 correlation | 41 barrier frame [186] | 227 OrderEffect payload [166] |
//   393 catalog_root [32] | 425 instrument_spec_version u64 | 433 venue_capabilities_version u64 |
//   441 issuer_id i32 | 445 term i64 | 453 barrier_index i64 | 461 applied_index i64 |
//   469 state_version u64 | 477 strategy_mode u8
inline constexpr std::size_t kResponseAuthorityGrantedBytes = 478;
static_assert(kResponseAuthorityGrantedBytes == 41 + kAuthorityBarrierBytes + kOrderEffectPayloadBytes + 32 + 8 + 8 + 4 + 8 * 4 + 1);

[[nodiscard]] EncodeNodeProtocolResult encode_gateway_refusal(
    const GatewayRefusal& refusal, std::span<std::byte> output) noexcept;
[[nodiscard]] NodeProtocolError decode_gateway_refusal(
    std::span<const std::byte> input, GatewayRefusal& destination) noexcept;

// Failure leaves OUTPUT unchanged. CHUNK must not overlap OUTPUT.
[[nodiscard]] EncodeNodeProtocolResult encode_state_chunk(
    const StateChunkHeader& header, std::span<const std::byte> chunk, std::span<std::byte> output) noexcept;
// Structure and geometry only (index < count <= kMaxStateChunks, lengths
// consistent with snapshot_bytes); integrity is checked after reassembly.
[[nodiscard]] NodeProtocolError decode_state_chunk(
    std::span<const std::byte> input, StateChunkView& destination) noexcept;

// A decoded view is DATA, not permission: only GatewayLink may turn one into an
// AuthorityProof, and only from a verified, fenced, correlated frame.
[[nodiscard]] EncodeNodeProtocolResult encode_authority_granted(
    const ResponseCorrelation& correlation, const AuthorityProofView& view, std::span<std::byte> output) noexcept;
[[nodiscard]] NodeProtocolError decode_authority_granted(
    std::span<const std::byte> input, ResponseCorrelation& correlation, AuthorityProofView& destination) noexcept;

[[nodiscard]] inline std::uint8_t response_kind_byte(std::span<const std::byte> input) noexcept {
    return input.empty() ? 0 : std::to_integer<std::uint8_t>(input[0]);
}

}  // namespace velocita::v2
