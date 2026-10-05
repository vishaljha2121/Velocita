// Authenticated envelope for peer, ingress and response messages.
#pragma once

#include "velocita/v2/bound_frame.hpp"
#include "velocita/v2/digest.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace velocita::v2 {

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
    std::span<const std::byte> body;
};

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

class AuthKey final {
public:
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
    friend struct AuthEnvelopeAccess;
    friend EncodeAuthEnvelopeResult encode_auth_envelope(
        const AuthKey&, std::uint64_t, std::uint64_t,
        std::span<const std::byte>, std::span<std::byte>) noexcept;
    friend AuthEnvelopeError verify_auth_envelope(
        const AuthKey&, std::span<const std::byte>, VerifiedAuthEnvelope&) noexcept;
    AuthRoute route_{};
    std::array<std::byte, 32> key_{};
};

[[nodiscard]] EncodeAuthEnvelopeResult encode_auth_envelope(
    const AuthKey& key, std::uint64_t session_epoch, std::uint64_t sequence,
    std::span<const std::byte> body, std::span<std::byte> output) noexcept;

[[nodiscard]] AuthEnvelopeError peek_auth_route(
    std::span<const std::byte> input, AuthRoute& destination) noexcept;

[[nodiscard]] AuthEnvelopeError verify_auth_envelope(
    const AuthKey& key, std::span<const std::byte> input,
    VerifiedAuthEnvelope& destination) noexcept;

[[nodiscard]] Digest256 hmac_sha256(std::span<const std::byte> key,
                                    std::span<const std::byte> message) noexcept;

}
