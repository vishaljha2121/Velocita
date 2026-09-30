#include "velocita/v2/auth_envelope.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Oracles are independent of the code under test: RFC 4231 vectors for HMAC,
// and tests/v2_auth_envelope_goldens.py (Python hmac/hashlib/struct) for key
// derivation and complete envelopes.

namespace velocita::v2 {
struct AuthEnvelopeAccess {
    static std::span<const std::byte, 32> key(const AuthKey& key) { return key.key_; }
};
}  // namespace velocita::v2

namespace {

using namespace velocita::v2;
using Bytes = std::vector<std::byte>;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Bytes hex(std::string_view text) {
    require(text.size() % 2 == 0, "odd hex fixture");
    Bytes out;
    for (std::size_t i = 0; i < text.size(); i += 2)
        out.push_back(static_cast<std::byte>(std::stoi(std::string(text.substr(i, 2)), nullptr, 16)));
    return out;
}

Bytes ascii(std::string_view text) {
    Bytes out;
    for (const char c : text) out.push_back(static_cast<std::byte>(c));
    return out;
}

Bytes digest_bytes(const Digest256& digest) { return {digest.bytes.begin(), digest.bytes.end()}; }

const Bytes kSecretBytes = [] {
    Bytes out;
    for (int i = 0x40; i < 0x60; ++i) out.push_back(static_cast<std::byte>(i));
    return out;
}();
const std::array<std::byte, 16> kIncarnation{std::byte{0xAB}, std::byte{0x5A}};

AuthSecret secret(const Bytes& bytes = kSecretBytes) {
    auto value = AuthSecret::from_bytes(bytes);
    require(value.has_value(), "fixture secret refused");
    return std::move(*value);
}

AuthKey key(AuthRole role, std::int32_t sender, std::int32_t recipient,
            const Bytes& secret_bytes = kSecretBytes,
            const std::array<std::byte, 16>& incarnation = kIncarnation) {
    auto value = AuthKey::derive(secret(secret_bytes), {role, sender, recipient}, incarnation);
    require(value.has_value(), "fixture key derivation refused a valid route");
    return std::move(*value);
}

const Bytes kGoldenPeer = hex(
    "564c413101000100010000000200000007000000000000002a0000000000000010000000"
    "626f756e642d706565722d6672616d6572efa84fe7e80a2bb7fc635c00f61033f1b223df"
    "04a213a44bb457b63e108b65");
const Bytes kGoldenEmptyResponse = hex(
    "564c41310100030001000000090000000100000000000000010000000000000000000000"
    "c33ad8ee329cd3f1fb66e6b9f6e411d41a62ae56970902d6aff1f63c378b5ca5");

void hmac_matches_rfc4231() {
    require(digest_bytes(hmac_sha256(Bytes(20, std::byte{0x0b}), ascii("Hi There"))) ==
                hex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"),
            "RFC 4231 case 1");
    require(digest_bytes(hmac_sha256(ascii("Jefe"), ascii("what do ya want for nothing?"))) ==
                hex("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"),
            "RFC 4231 case 2");
    require(digest_bytes(hmac_sha256(Bytes(131, std::byte{0xaa}),
                                     ascii("Test Using Larger Than Block-Size Key - Hash Key First"))) ==
                hex("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"),
            "RFC 4231 case 6 (key longer than one block)");
}

void derived_keys_match_independent_oracle() {
    const auto check = [](AuthRole role, std::int32_t s, std::int32_t r, std::string_view golden) {
        const auto derived = key(role, s, r);
        const auto bytes = AuthEnvelopeAccess::key(derived);
        require(Bytes(bytes.begin(), bytes.end()) == hex(golden),
                "derived key differs from the Python oracle");
        require(derived.route() == AuthRoute{role, s, r}, "derived key lost its route");
    };
    check(AuthRole::peer, 1, 2, "4e58c406486a6ec606b7b669507636fe8a3ad166e0b050265b5312ec5a0eca21");
    check(AuthRole::peer, 2, 1, "dc1e2408b08438fe9975dfbf7b45eadeaa3d8ea3f11034d616d5c3fcac0d2086");
    check(AuthRole::ingress, 9, 1, "4e724e46d7de84225c0a7b6985db13a9005da87c1817488c6644192d717172ff");
}

void invalid_secrets_and_routes_refuse() {
    require(!AuthSecret::from_bytes(Bytes(31)).has_value(), "short secret accepted");
    require(!AuthSecret::from_bytes(Bytes(33)).has_value(), "long secret accepted");
    const auto refuses = [](AuthRoute route) {
        return !AuthKey::derive(secret(), route, kIncarnation).has_value();
    };
    require(refuses({AuthRole::peer, 0, 2}), "zero sender accepted");
    require(refuses({AuthRole::peer, 1, -2}), "negative recipient accepted");
    require(refuses({AuthRole::peer, 3, 3}), "peer self-route accepted");
    require(refuses({static_cast<AuthRole>(0), 1, 2}), "role zero accepted");
    require(refuses({static_cast<AuthRole>(4), 1, 2}), "unknown role accepted");
    require(!refuses({AuthRole::ingress, 1, 1}), "caller key id equal to node id is a valid ingress route");
}

void encoding_matches_goldens() {
    std::array<std::byte, kMaxAuthEnvelopeBytes> out{};
    const auto peer = encode_auth_envelope(key(AuthRole::peer, 1, 2), 7, 42, ascii("bound-peer-frame"), out);
    require(peer.error == AuthEnvelopeError::none, "golden peer envelope refused");
    require(Bytes(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(peer.bytes_written)) == kGoldenPeer,
            "peer envelope bytes differ from the Python oracle");
    const auto empty = encode_auth_envelope(key(AuthRole::response, 1, 9), 1, 1, {}, out);
    require(empty.error == AuthEnvelopeError::none && empty.bytes_written == kAuthOverheadBytes,
            "empty-body response has the wrong size");
    require(Bytes(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(empty.bytes_written)) ==
                kGoldenEmptyResponse,
            "response envelope bytes differ from the Python oracle");
}

void goldens_verify_to_their_fields() {
    VerifiedAuthEnvelope verified;
    require(verify_auth_envelope(key(AuthRole::peer, 1, 2), kGoldenPeer, verified) == AuthEnvelopeError::none,
            "golden peer envelope did not verify");
    require(verified.header == AuthHeader{{AuthRole::peer, 1, 2}, 7, 42}, "verified header fields differ");
    require(Bytes(verified.body.begin(), verified.body.end()) == ascii("bound-peer-frame"),
            "verified body differs");
    require(verified.body.data() == kGoldenPeer.data() + kAuthHeaderBytes, "body does not borrow the input");
    AuthRoute route;
    require(peek_auth_route(kGoldenPeer, route) == AuthEnvelopeError::none &&
                route == AuthRoute{AuthRole::peer, 1, 2},
            "peek did not report the framed route");
}

void every_bit_flip_and_truncation_refuses() {
    const auto verifier = key(AuthRole::peer, 1, 2);
    for (std::size_t at = 0; at < kGoldenPeer.size(); ++at) {
        for (int bit = 0; bit < 8; ++bit) {
            auto copy = kGoldenPeer;
            copy[at] ^= static_cast<std::byte>(1U << bit);
            VerifiedAuthEnvelope out;
            require(verify_auth_envelope(verifier, copy, out) != AuthEnvelopeError::none,
                    "bit flip accepted at byte " + std::to_string(at));
        }
    }
    for (std::size_t length = 0; length < kGoldenPeer.size(); ++length) {
        VerifiedAuthEnvelope out;
        require(verify_auth_envelope(verifier, std::span(kGoldenPeer).first(length), out) !=
                    AuthEnvelopeError::none,
                "truncation accepted at length " + std::to_string(length));
    }
    auto extended = kGoldenPeer;
    extended.push_back(std::byte{0});
    VerifiedAuthEnvelope out;
    require(verify_auth_envelope(verifier, extended, out) == AuthEnvelopeError::length_mismatch,
            "trailing byte not reported as a length mismatch");
}

void keys_bind_route_role_secret_and_incarnation() {
    VerifiedAuthEnvelope out;
    require(verify_auth_envelope(key(AuthRole::peer, 2, 1), kGoldenPeer, out) == AuthEnvelopeError::route_mismatch,
            "reflected frame not refused by route");
    require(verify_auth_envelope(key(AuthRole::ingress, 1, 2), kGoldenPeer, out) ==
                AuthEnvelopeError::route_mismatch,
            "cross-role key not refused by route");
    auto other_secret = kSecretBytes;
    other_secret[0] ^= std::byte{1};
    require(verify_auth_envelope(key(AuthRole::peer, 1, 2, other_secret), kGoldenPeer, out) ==
                AuthEnvelopeError::tag_mismatch,
            "wrong secret not refused by tag");
    auto other_incarnation = kIncarnation;
    other_incarnation[15] = std::byte{1};
    require(verify_auth_envelope(key(AuthRole::peer, 1, 2, kSecretBytes, other_incarnation), kGoldenPeer, out) ==
                AuthEnvelopeError::tag_mismatch,
            "foreign cluster incarnation not refused by tag");
}

// Structural checks run before any tag work and name the exact reason.
void structural_refusals_name_their_reason() {
    const auto verifier = key(AuthRole::peer, 1, 2);
    const auto expect = [&](std::size_t at, std::byte value, AuthEnvelopeError error, const char* what) {
        auto copy = kGoldenPeer;
        copy[at] = value;
        VerifiedAuthEnvelope out;
        out.header.sequence = 99;
        require(verify_auth_envelope(verifier, copy, out) == error, what);
        require(out.header.sequence == 99 && out.body.empty(), "refusal modified the destination");
    };
    expect(0, std::byte{'X'}, AuthEnvelopeError::invalid_magic, "magic");
    expect(4, std::byte{2}, AuthEnvelopeError::unsupported_version, "version");
    expect(6, std::byte{0}, AuthEnvelopeError::invalid_role, "role zero");
    expect(6, std::byte{4}, AuthEnvelopeError::invalid_role, "role four");
    expect(8, std::byte{0}, AuthEnvelopeError::invalid_route, "sender zero");
    expect(15, std::byte{0x80}, AuthEnvelopeError::invalid_route, "negative recipient");
    expect(16, std::byte{0}, AuthEnvelopeError::invalid_counter, "epoch zero");
    expect(24, std::byte{0}, AuthEnvelopeError::invalid_counter, "sequence zero");
    expect(34, std::byte{1}, AuthEnvelopeError::body_too_large, "body length above the cap");
    expect(32, std::byte{17}, AuthEnvelopeError::length_mismatch, "body length lies");
}

void encode_refusals_leave_output_unchanged() {
    const auto encoder = key(AuthRole::peer, 1, 2);
    std::array<std::byte, kMaxAuthEnvelopeBytes + 1> out{};
    const auto untouched = [&] {
        return std::all_of(out.begin(), out.end(), [](std::byte b) { return b == std::byte{0xEE}; });
    };
    const auto expect = [&](std::uint64_t epoch, std::uint64_t sequence, std::size_t body_bytes,
                            std::size_t output_bytes, AuthEnvelopeError error, const char* what) {
        out.fill(std::byte{0xEE});
        const Bytes body(body_bytes, std::byte{0x11});
        const auto result = encode_auth_envelope(encoder, epoch, sequence, body, std::span(out).first(output_bytes));
        require(result.error == error && result.bytes_written == 0, what);
        require(untouched(), std::string(what) + " wrote output on refusal");
    };
    expect(0, 1, 4, out.size(), AuthEnvelopeError::invalid_counter, "epoch zero");
    expect(1, 0, 4, out.size(), AuthEnvelopeError::invalid_counter, "sequence zero");
    expect(1, 1, kMaxAuthBodyBytes + 1, out.size(), AuthEnvelopeError::body_too_large, "oversized body");
    expect(1, 1, 4, kAuthOverheadBytes + 3, AuthEnvelopeError::output_too_small, "short output");
}

void maximum_body_round_trips() {
    const auto both = key(AuthRole::peer, 1, 2);
    Bytes body(kMaxAuthBodyBytes);
    for (std::size_t i = 0; i < body.size(); ++i) body[i] = static_cast<std::byte>(i * 31);
    Bytes out(kMaxAuthEnvelopeBytes);
    const auto encoded = encode_auth_envelope(both, UINT64_MAX, UINT64_MAX, body, out);
    require(encoded.error == AuthEnvelopeError::none && encoded.bytes_written == kMaxAuthEnvelopeBytes,
            "maximum envelope refused or mis-sized");
    VerifiedAuthEnvelope verified;
    require(verify_auth_envelope(both, out, verified) == AuthEnvelopeError::none &&
                Bytes(verified.body.begin(), verified.body.end()) == body &&
                verified.header.session_epoch == UINT64_MAX && verified.header.sequence == UINT64_MAX,
            "maximum envelope did not round-trip");
}

}  // namespace

int main() {
    try {
        hmac_matches_rfc4231();
        derived_keys_match_independent_oracle();
        invalid_secrets_and_routes_refuse();
        encoding_matches_goldens();
        goldens_verify_to_their_fields();
        every_bit_flip_and_truncation_refuses();
        keys_bind_route_role_secret_and_incarnation();
        structural_refusals_name_their_reason();
        encode_refusals_leave_output_unchanged();
        maximum_body_round_trips();
        std::cout << "auth envelope tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "auth envelope tests failed: " << error.what() << '\n';
        return 1;
    }
}
