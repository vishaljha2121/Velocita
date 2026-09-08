#include "velocita/v2/digest.hpp"

#include <array>
#include <bit>
#include <cstdint>

namespace velocita::v2 {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU,
    0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U, 0xD807AA98U, 0x12835B01U,
    0x243185BEU, 0x550C7DC3U, 0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U,
    0xC19BF174U, 0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
    0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU, 0x983E5152U,
    0xA831C66DU, 0xB00327C8U, 0xBF597FC7U, 0xC6E00BF3U, 0xD5A79147U,
    0x06CA6351U, 0x14292967U, 0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU,
    0x53380D13U, 0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
    0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U, 0xD192E819U,
    0xD6990624U, 0xF40E3585U, 0x106AA070U, 0x19A4C116U, 0x1E376C08U,
    0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU,
    0x682E6FF3U, 0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
};

using HashState = std::array<std::uint32_t, 8>;

void process_block(HashState& state, const std::span<const std::byte, 64> block) noexcept {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        const auto offset = index * 4U;
        words[index] =
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[offset])) << 24U) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[offset + 1U])) << 16U) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[offset + 2U])) << 8U) |
            static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[offset + 3U]));
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const auto s0 = std::rotr(words[index - 15U], 7) ^
                        std::rotr(words[index - 15U], 18) ^
                        (words[index - 15U] >> 3U);
        const auto s1 = std::rotr(words[index - 2U], 17) ^
                        std::rotr(words[index - 2U], 19) ^
                        (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    auto a = state[0];
    auto b = state[1];
    auto c = state[2];
    auto d = state[3];
    auto e = state[4];
    auto f = state[5];
    auto g = state[6];
    auto h = state[7];

    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const auto choose = (e & f) ^ ((~e) & g);
        const auto temporary1 = h + sum1 + choose + kRoundConstants[index] + words[index];
        const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temporary2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

std::optional<std::uint8_t> hex_nibble(const char character) noexcept {
    if (character >= '0' && character <= '9') {
        return static_cast<std::uint8_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return static_cast<std::uint8_t>(character - 'a' + 10);
    }
    if (character >= 'A' && character <= 'F') {
        return static_cast<std::uint8_t>(character - 'A' + 10);
    }
    return std::nullopt;
}

}  // namespace

Digest256 sha256_digest(const std::span<const std::byte> bytes) noexcept {
    HashState state{
        0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
        0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U,
    };

    std::size_t offset{};
    while ((bytes.size() - offset) >= 64U) {
        process_block(state, std::span<const std::byte, 64>{bytes.subspan(offset, 64U)});
        offset += 64U;
    }

    std::array<std::byte, 128> tail{};
    const auto remaining = bytes.size() - offset;
    for (std::size_t index = 0; index < remaining; ++index) {
        tail[index] = bytes[offset + index];
    }
    tail[remaining] = std::byte{0x80};
    const std::size_t padded_size = remaining < 56U ? 64U : 128U;
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;
    for (std::size_t index = 0; index < 8U; ++index) {
        tail[padded_size - 1U - index] =
            static_cast<std::byte>((bit_length >> (index * 8U)) & 0xFFU);
    }
    process_block(state, std::span<const std::byte, 64>{tail.data(), 64U});
    if (padded_size == 128U) {
        process_block(state, std::span<const std::byte, 64>{tail.data() + 64U, 64U});
    }

    Digest256 digest;
    for (std::size_t word = 0; word < state.size(); ++word) {
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            digest.bytes[word * 4U + byte] =
                static_cast<std::byte>((state[word] >> ((3U - byte) * 8U)) & 0xFFU);
        }
    }
    return digest;
}

std::string to_hex(const Digest256& digest) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string hex(kDigestBytes * 2U, '0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        const auto value = std::to_integer<std::uint8_t>(digest.bytes[index]);
        hex[index * 2U] = alphabet[value >> 4U];
        hex[index * 2U + 1U] = alphabet[value & 0x0FU];
    }
    return hex;
}

std::optional<Digest256> parse_digest256(const std::string_view hex) noexcept {
    if (hex.size() != kDigestBytes * 2U) return std::nullopt;
    Digest256 digest;
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        const auto upper = hex_nibble(hex[index * 2U]);
        const auto lower = hex_nibble(hex[index * 2U + 1U]);
        if (!upper || !lower) return std::nullopt;
        digest.bytes[index] = static_cast<std::byte>((*upper << 4U) | *lower);
    }
    return digest;
}

Digest256 observation_fingerprint(const VenueObservation& value) noexcept {
    constexpr std::string_view domain = "velocita/venue-observation/v2";
    // Canonical V2 observation: schema(2), kind(1), seven IDs(56), side(1),
    // price/quantity/fee(24), evidence digest(32). Never hash object padding.
    std::array<std::byte, domain.size() + 116> bytes{};
    std::size_t offset = 0;
    for (const char c : domain) bytes[offset++] = static_cast<std::byte>(c);
    const auto append = [&](const std::uint64_t word, const std::size_t width) {
        for (std::size_t i = 0; i < width; ++i)
            bytes[offset++] = static_cast<std::byte>((word >> (8U * i)) & 0xffU);
    };
    append(kVenueObservationSchemaVersion, 2);
    append(static_cast<std::uint8_t>(value.kind), 1);
    append(value.venue_id.value(), 8);
    append(value.venue_session_id.value(), 8);
    append(value.venue_sequence.value(), 8);
    append(value.cluster_order_id.value(), 8);
    append(value.effect_id.value(), 8);
    append(value.venue_order_id.value(), 8);
    append(value.execution_id.value(), 8);
    append(static_cast<std::uint8_t>(value.side), 1);
    append(static_cast<std::uint64_t>(value.price.atoms()), 8);
    append(static_cast<std::uint64_t>(value.quantity.atoms()), 8);
    append(static_cast<std::uint64_t>(value.fee.atoms()), 8);
    for (const auto byte : value.evidence_digest.bytes) bytes[offset++] = byte;
    return sha256_digest(bytes);
}

}  // namespace velocita::v2
