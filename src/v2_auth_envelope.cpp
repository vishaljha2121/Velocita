#include "velocita/v2/auth_envelope.hpp"

#include <algorithm>

namespace velocita::v2 {
namespace {

constexpr std::size_t kBlockBytes = 64;
constexpr std::string_view kDeriveDomain = "velocita.auth.v1";

void wipe(std::span<std::byte> bytes) noexcept {
    volatile std::byte* cursor = bytes.data();
    for (std::size_t i = 0; i < bytes.size(); ++i) cursor[i] = std::byte{0};
}

class Hmac final {
public:
    explicit Hmac(std::span<const std::byte> key) noexcept {
        std::array<std::byte, kBlockBytes> block{};
        if (key.size() > kBlockBytes) {
            const auto hashed = sha256_digest(key);
            std::copy(hashed.bytes.begin(), hashed.bytes.end(), block.begin());
        } else {
            std::copy(key.begin(), key.end(), block.begin());
        }
        std::array<std::byte, kBlockBytes> pad{};
        for (std::size_t i = 0; i < kBlockBytes; ++i) pad[i] = block[i] ^ std::byte{0x36};
        inner_.update(pad);
        for (std::size_t i = 0; i < kBlockBytes; ++i) pad[i] = block[i] ^ std::byte{0x5c};
        outer_.update(pad);
        wipe(pad);
        wipe(block);
    }
    void update(std::span<const std::byte> bytes) noexcept { inner_.update(bytes); }
    [[nodiscard]] Digest256 finish() noexcept {
        const auto inner = inner_.finish();
        outer_.update(inner.bytes);
        return outer_.finish();
    }

private:
    Sha256 inner_;
    Sha256 outer_;
};

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

bool valid_role(std::uint16_t role) noexcept { return role >= 1 && role <= 3; }

bool valid_route(const AuthRoute& route) noexcept {
    return valid_role(static_cast<std::uint16_t>(route.role)) && route.sender > 0 &&
           route.recipient > 0 && !(route.role == AuthRole::peer && route.sender == route.recipient);
}

struct Parsed {
    AuthHeader header;
    std::size_t body_bytes{};
};

AuthEnvelopeError parse(std::span<const std::byte> input, Parsed& parsed) noexcept {
    if (input.size() < kAuthHeaderBytes) return AuthEnvelopeError::truncated;
    if (!std::equal(kAuthEnvelopeMagic.begin(), kAuthEnvelopeMagic.end(), input.begin()))
        return AuthEnvelopeError::invalid_magic;
    if (get<std::uint16_t>(input, 4) != kAuthEnvelopeVersion) return AuthEnvelopeError::unsupported_version;
    const auto role = get<std::uint16_t>(input, 6);
    if (!valid_role(role)) return AuthEnvelopeError::invalid_role;
    const AuthRoute route{static_cast<AuthRole>(role), get<std::int32_t>(input, 8),
                          get<std::int32_t>(input, 12)};
    if (!valid_route(route)) return AuthEnvelopeError::invalid_route;
    const auto epoch = get<std::uint64_t>(input, 16);
    const auto sequence = get<std::uint64_t>(input, 24);
    if (epoch == 0 || sequence == 0) return AuthEnvelopeError::invalid_counter;
    const auto body_bytes = get<std::uint32_t>(input, 32);
    if (body_bytes > kMaxAuthBodyBytes) return AuthEnvelopeError::body_too_large;
    if (input.size() < kAuthOverheadBytes) return AuthEnvelopeError::truncated;
    if (input.size() != kAuthOverheadBytes + body_bytes) return AuthEnvelopeError::length_mismatch;
    parsed = {{route, epoch, sequence}, body_bytes};
    return AuthEnvelopeError::none;
}

}

std::optional<AuthSecret> AuthSecret::from_bytes(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() != kAuthSecretBytes) return std::nullopt;
    AuthSecret secret;
    std::copy(bytes.begin(), bytes.end(), secret.bytes_.begin());
    return secret;
}

AuthSecret::AuthSecret(AuthSecret&& other) noexcept : bytes_(other.bytes_) { wipe(other.bytes_); }
AuthSecret& AuthSecret::operator=(AuthSecret&& other) noexcept {
    if (this != &other) {
        bytes_ = other.bytes_;
        wipe(other.bytes_);
    }
    return *this;
}
AuthSecret::~AuthSecret() { wipe(bytes_); }

std::optional<AuthKey> AuthKey::derive(const AuthSecret& secret, const AuthRoute& route,
                                       std::span<const std::byte, 16> cluster_incarnation) noexcept {
    if (!valid_route(route)) return std::nullopt;
    std::array<std::byte, kDeriveDomain.size() + 10 + 16> info{};
    for (std::size_t i = 0; i < kDeriveDomain.size(); ++i) info[i] = std::byte(kDeriveDomain[i]);
    put(info, kDeriveDomain.size(), static_cast<std::uint16_t>(route.role));
    put(info, kDeriveDomain.size() + 2, route.sender);
    put(info, kDeriveDomain.size() + 6, route.recipient);
    std::copy(cluster_incarnation.begin(), cluster_incarnation.end(),
              info.begin() + static_cast<std::ptrdiff_t>(kDeriveDomain.size() + 10));
    Hmac mac(secret.bytes_);
    mac.update(info);
    auto derived = mac.finish();
    AuthKey key;
    key.route_ = route;
    std::copy(derived.bytes.begin(), derived.bytes.end(), key.key_.begin());
    wipe(derived.bytes);
    return key;
}

AuthKey::AuthKey(AuthKey&& other) noexcept : route_(other.route_), key_(other.key_) { wipe(other.key_); }
AuthKey& AuthKey::operator=(AuthKey&& other) noexcept {
    if (this != &other) {
        route_ = other.route_;
        key_ = other.key_;
        wipe(other.key_);
    }
    return *this;
}
AuthKey::~AuthKey() { wipe(key_); }

EncodeAuthEnvelopeResult encode_auth_envelope(
    const AuthKey& key, const std::uint64_t session_epoch, const std::uint64_t sequence,
    const std::span<const std::byte> body, const std::span<std::byte> output) noexcept {
    if (!valid_route(key.route_)) return {AuthEnvelopeError::invalid_argument, 0};
    if (session_epoch == 0 || sequence == 0) return {AuthEnvelopeError::invalid_counter, 0};
    if (body.size() > kMaxAuthBodyBytes) return {AuthEnvelopeError::body_too_large, 0};
    const auto total = kAuthOverheadBytes + body.size();
    if (output.size() < total) return {AuthEnvelopeError::output_too_small, 0};

    std::copy(kAuthEnvelopeMagic.begin(), kAuthEnvelopeMagic.end(), output.begin());
    put(output, 4, kAuthEnvelopeVersion);
    put(output, 6, static_cast<std::uint16_t>(key.route_.role));
    put(output, 8, key.route_.sender);
    put(output, 12, key.route_.recipient);
    put(output, 16, session_epoch);
    put(output, 24, sequence);
    put(output, 32, static_cast<std::uint32_t>(body.size()));
    std::copy(body.begin(), body.end(), output.begin() + static_cast<std::ptrdiff_t>(kAuthHeaderBytes));
    Hmac mac(key.key_);
    mac.update(output.first(kAuthHeaderBytes + body.size()));
    const auto tag = mac.finish();
    std::copy(tag.bytes.begin(), tag.bytes.end(),
              output.begin() + static_cast<std::ptrdiff_t>(kAuthHeaderBytes + body.size()));
    return {AuthEnvelopeError::none, total};
}

AuthEnvelopeError peek_auth_route(std::span<const std::byte> input, AuthRoute& destination) noexcept {
    Parsed parsed;
    if (const auto error = parse(input, parsed); error != AuthEnvelopeError::none) return error;
    destination = parsed.header.route;
    return AuthEnvelopeError::none;
}

AuthEnvelopeError verify_auth_envelope(const AuthKey& key, std::span<const std::byte> input,
                                       VerifiedAuthEnvelope& destination) noexcept {
    Parsed parsed;
    if (const auto error = parse(input, parsed); error != AuthEnvelopeError::none) return error;
    if (parsed.header.route != key.route_) return AuthEnvelopeError::route_mismatch;
    const auto signed_bytes = kAuthHeaderBytes + parsed.body_bytes;
    Hmac mac(key.key_);
    mac.update(input.first(signed_bytes));
    const auto expected = mac.finish();
    unsigned difference = 0;
    for (std::size_t i = 0; i < kAuthTagBytes; ++i)
        difference |= std::to_integer<unsigned>(expected.bytes[i] ^ input[signed_bytes + i]);
    if (difference != 0) return AuthEnvelopeError::tag_mismatch;
    destination = {parsed.header, input.subspan(kAuthHeaderBytes, parsed.body_bytes)};
    return AuthEnvelopeError::none;
}

Digest256 hmac_sha256(std::span<const std::byte> key, std::span<const std::byte> message) noexcept {
    Hmac mac(key);
    mac.update(message);
    return mac.finish();
}

}
