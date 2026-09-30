#pragma once

#include "velocita/v2/bound_frame.hpp"
#include "velocita/v2/digest.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace velocita::v2 {

// Owner-approved envelope (M2_NODE_PROCESS_REVIEW.md decision C). Little-endian:
// magic "VLA1" | version u16 | role u16 | sender i32 | recipient i32 |
// session_epoch u64 | sequence u64 | body_length u32 | body | HMAC-SHA256 tag.
// The tag covers every preceding byte.
inline constexpr std::array<std::byte, 4> kAuthEnvelopeMagic{
    std::byte{'V'}, std::byte{'L'}, std::byte{'A'}, std::byte{'1'}};
inline constexpr std::uint16_t kAuthEnvelopeVersion = 1;
inline constexpr std::size_t kAuthSecretBytes = 32;
inline constexpr std::size_t kAuthTagBytes = 32;
inline constexpr std::size_t kAuthHeaderBytes = 36;
inline constexpr std::size_t kAuthOverheadBytes = kAuthHeaderBytes + kAuthTagBytes;
inline constexpr std::size_t kMaxAuthBodyBytes = kMaxBoundPeerBytes;
inline constexpr std::size_t kMaxAuthEnvelopeBytes = kAuthOverheadBytes + kMaxAuthBodyBytes;
static_assert(kMaxAuthEnvelopeBytes == 32836);

enum class AuthRole : std::uint16_t { peer = 1, ingress = 2, response = 3 };

// Peer: voter -> voter. Ingress: caller key id -> voter. Response: voter ->
// caller key id. Ids are strictly positive.
struct AuthRoute {
    AuthRole role{AuthRole::peer};
    std::int32_t sender{};
    std::int32_t recipient{};
    constexpr auto operator<=>(const AuthRoute&) const noexcept = default;
};

struct AuthHeader {
    AuthRoute route{};
    std::uint64_t session_epoch{};
    std::uint64_t sequence{};
    constexpr auto operator<=>(const AuthHeader&) const noexcept = default;
};

enum class AuthEnvelopeError : std::uint8_t {
    none,
    invalid_argument,
    output_too_small,
    truncated,
    length_mismatch,
    invalid_magic,
    unsupported_version,
    invalid_role,
    invalid_route,
    invalid_counter,
    body_too_large,
    route_mismatch,
    tag_mismatch,
};

struct EncodeAuthEnvelopeResult {
    AuthEnvelopeError error{AuthEnvelopeError::none};
    std::size_t bytes_written{};
};

struct VerifiedAuthEnvelope {
    AuthHeader header{};
    std::span<const std::byte> body;  // borrows INPUT
};

// Secret material, move-only and zeroed on destruction so a key does not
// linger in a copied temporary. Loading it from a file is the caller's job.
class AuthSecret final {
public:
    [[nodiscard]] static std::optional<AuthSecret> from_bytes(
        std::span<const std::byte> bytes) noexcept;
    AuthSecret(AuthSecret&& other) noexcept;
    AuthSecret& operator=(AuthSecret&& other) noexcept;
    AuthSecret(const AuthSecret&) = delete;
    AuthSecret& operator=(const AuthSecret&) = delete;
    ~AuthSecret();

private:
    AuthSecret() noexcept = default;
    friend class AuthKey;
    std::array<std::byte, kAuthSecretBytes> bytes_{};
};

// A key is bound to exactly one route. Verification refuses a frame whose
// header names any other route, so a key cannot authenticate a reflected or
// cross-role frame even before the tag is compared.
class AuthKey final {
public:
    // HMAC-SHA256(secret, "velocita.auth.v1" || role u16 || sender i32 ||
    // recipient i32 || cluster incarnation). Nullopt for an invalid route.
    [[nodiscard]] static std::optional<AuthKey> derive(
        const AuthSecret& secret, const AuthRoute& route,
        std::span<const std::byte, 16> cluster_incarnation) noexcept;
    AuthKey(AuthKey&& other) noexcept;
    AuthKey& operator=(AuthKey&& other) noexcept;
    AuthKey(const AuthKey&) = delete;
    AuthKey& operator=(const AuthKey&) = delete;
    ~AuthKey();

    [[nodiscard]] const AuthRoute& route() const noexcept { return route_; }

private:
    AuthKey() noexcept = default;
    friend struct AuthEnvelopeAccess;  // test-only inspection; never defined in src/
    friend EncodeAuthEnvelopeResult encode_auth_envelope(
        const AuthKey&, std::uint64_t, std::uint64_t,
        std::span<const std::byte>, std::span<std::byte>) noexcept;
    friend AuthEnvelopeError verify_auth_envelope(
        const AuthKey&, std::span<const std::byte>, VerifiedAuthEnvelope&) noexcept;
    AuthRoute route_{};
    std::array<std::byte, 32> key_{};
};

// Arguments are validated before the first byte is written, so failure
// leaves OUTPUT unchanged. BODY must not overlap OUTPUT. Epoch and sequence
// are strictly positive; zero is reserved.
[[nodiscard]] EncodeAuthEnvelopeResult encode_auth_envelope(
    const AuthKey& key, std::uint64_t session_epoch, std::uint64_t sequence,
    std::span<const std::byte> body, std::span<std::byte> output) noexcept;

// UNAUTHENTICATED structural read, used only to choose which key to verify
// with. Nothing it returns may be trusted before verify_auth_envelope.
[[nodiscard]] AuthEnvelopeError peek_auth_route(
    std::span<const std::byte> input, AuthRoute& destination) noexcept;

// Constant-time tag comparison. Failure leaves DESTINATION unchanged. This
// proves origin and integrity only; replay ordering is the session fence's job.
[[nodiscard]] AuthEnvelopeError verify_auth_envelope(
    const AuthKey& key, std::span<const std::byte> input,
    VerifiedAuthEnvelope& destination) noexcept;

// RFC 2104 HMAC-SHA256, exposed so it can be pinned to RFC 4231 vectors.
[[nodiscard]] Digest256 hmac_sha256(std::span<const std::byte> key,
                                    std::span<const std::byte> message) noexcept;

}  // namespace velocita::v2
