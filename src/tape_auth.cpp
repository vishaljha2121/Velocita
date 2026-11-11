#include "velocita/tape/auth.hpp"

#include "velocita/v2/digest.hpp"

#include <cstring>

namespace velocita::tape {
namespace {

constexpr std::array<std::byte, 4> kMagic{std::byte{'V'}, std::byte{'T'}, std::byte{'A'}, std::byte{'P'}};

void put(std::vector<std::byte>& out, std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) out.push_back(std::byte((v >> (8 * i)) & 0xFF));
}

std::uint64_t get(const std::byte* p, int n) {
    std::uint64_t v = 0;
    for (int i = 0; i < n; ++i) v |= std::uint64_t(std::to_integer<std::uint8_t>(p[i])) << (8 * i);
    return v;
}

bool same(std::span<const std::byte> a, std::span<const std::byte> b) noexcept {
    if (a.size() != b.size()) return false;
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff |= std::to_integer<std::uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

}

std::string Party::name() const {
    return std::string(role == Role::voter ? "voter-" : "recorder-") + std::to_string(id);
}

Digest hmac_sha256(std::span<const std::byte> key, std::span<const std::byte> message) noexcept {
    std::array<std::byte, 64> block{};
    if (key.size() > block.size()) {
        const auto d = v2::sha256_digest(key);
        std::memcpy(block.data(), d.bytes.data(), d.bytes.size());
    } else if (!key.empty()) {
        std::memcpy(block.data(), key.data(), key.size());
    }
    std::array<std::byte, 64> ipad{}, opad{};
    for (std::size_t i = 0; i < 64; ++i) {
        ipad[i] = block[i] ^ std::byte{0x36};
        opad[i] = block[i] ^ std::byte{0x5c};
    }
    v2::Sha256 inner;
    inner.update(ipad);
    inner.update(message);
    const auto inner_digest = inner.finish();
    v2::Sha256 outer;
    outer.update(opad);
    outer.update(inner_digest.bytes);
    return outer.finish();
}

Key derive_key(const Key& master, std::uint64_t incarnation, const Party& from, const Party& to) {
    const auto label = "velocita-tape/v1/" + std::to_string(incarnation) + "/" + from.name() + "->" + to.name();
    const auto d = hmac_sha256(master, std::as_bytes(std::span(label.data(), label.size())));
    Key k{};
    std::memcpy(k.data(), d.bytes.data(), k.size());
    return k;
}

std::vector<std::byte> seal(FrameKind kind, const Party& from, const Party& to, std::uint64_t boot,
                            std::uint64_t counter, std::span<const std::byte> payload, const Key& key) {
    std::vector<std::byte> out(kMagic.begin(), kMagic.end());
    out.reserve(kEnvelopeOverhead + payload.size());
    out.push_back(std::byte{1});
    out.push_back(static_cast<std::byte>(kind));
    out.push_back(static_cast<std::byte>(from.role));
    out.push_back(std::byte{from.id});
    out.push_back(static_cast<std::byte>(to.role));
    out.push_back(std::byte{to.id});
    put(out, boot, 8);
    put(out, counter, 8);
    put(out, payload.size(), 4);
    out.insert(out.end(), payload.begin(), payload.end());
    const auto mac = hmac_sha256(key, out);
    out.insert(out.end(), mac.bytes.begin(), mac.bytes.end());
    return out;
}

Sealer::Sealer(Key master, std::uint64_t incarnation, Party self, std::uint64_t boot)
    : master_(master), incarnation_(incarnation), self_(self), boot_(boot) {}

const Key& Sealer::key(const Party& from, const Party& to) {
    const auto k = std::make_pair(from, to);
    auto it = keys_.find(k);
    if (it == keys_.end()) it = keys_.emplace(k, derive_key(master_, incarnation_, from, to)).first;
    return it->second;
}

std::vector<std::byte> Sealer::seal_to(const Party& to, FrameKind kind, std::span<const std::byte> payload) {
    return seal(kind, self_, to, boot_, ++counter_, payload, key(self_, to));
}

std::optional<Opened> Sealer::open(std::span<const std::byte> frame) {
    if (frame.size() < kEnvelopeOverhead || frame.size() > kMaxEnvelopeBytes) return std::nullopt;
    if (!same(frame.first(4), kMagic) || frame[4] != std::byte{1}) return std::nullopt;
    Opened o;
    const auto kind = std::to_integer<std::uint8_t>(frame[5]);
    if (kind < 1 || kind > 3) return std::nullopt;
    o.kind = static_cast<FrameKind>(kind);
    const auto from_role = std::to_integer<std::uint8_t>(frame[6]), to_role = std::to_integer<std::uint8_t>(frame[8]);
    if (from_role < 1 || from_role > 2 || to_role < 1 || to_role > 2) return std::nullopt;
    o.from = {static_cast<Role>(from_role), std::to_integer<std::uint8_t>(frame[7])};
    o.to = {static_cast<Role>(to_role), std::to_integer<std::uint8_t>(frame[9])};
    if (o.to != self_ || o.from.id < 1 || o.from.id > 3) return std::nullopt;
    o.boot = get(frame.data() + 10, 8);
    o.counter = get(frame.data() + 18, 8);
    const auto length = get(frame.data() + 26, 4);
    if (frame.size() != kEnvelopeOverhead + length) return std::nullopt;
    const auto body = frame.first(frame.size() - 32);
    const auto expected = hmac_sha256(key(o.from, o.to), body);
    if (!same(expected.bytes, frame.last(32))) return std::nullopt;
    auto& seen = last_seen_[o.from];
    if (seen.first == o.boot && o.counter <= seen.second) return std::nullopt;
    seen = {o.boot, o.counter};
    o.payload.assign(frame.begin() + 30, frame.end() - 32);
    return o;
}

}
