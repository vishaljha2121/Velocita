// Authenticated frames between tape processes: per-direction HMAC keys, envelopes and replay counters.
#pragma once

#include "velocita/tape/types.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace velocita::tape {

enum class Role : std::uint8_t { voter = 1, recorder = 2 };
enum class FrameKind : std::uint8_t { peer = 1, submit = 2, ack = 3 };

struct Party {
    Role role{Role::voter};
    std::uint8_t id{};
    [[nodiscard]] std::string name() const;
    friend auto operator<=>(const Party&, const Party&) = default;
};

using Key = std::array<std::byte, 32>;

[[nodiscard]] Digest hmac_sha256(std::span<const std::byte> key, std::span<const std::byte> message) noexcept;
[[nodiscard]] Key derive_key(const Key& master, std::uint64_t incarnation, const Party& from, const Party& to);

struct Opened {
    FrameKind kind{FrameKind::peer};
    Party from;
    Party to;
    std::uint64_t boot{};
    std::uint64_t counter{};
    std::vector<std::byte> payload;
};

inline constexpr std::size_t kEnvelopeOverhead = 4 + 1 + 1 + 4 + 8 + 8 + 4 + 32;
inline constexpr std::size_t kMaxEnvelopeBytes = 512 * 1024;

[[nodiscard]] std::vector<std::byte> seal(FrameKind kind, const Party& from, const Party& to, std::uint64_t boot,
                                          std::uint64_t counter, std::span<const std::byte> payload, const Key& key);

class Sealer {
public:
    Sealer(Key master, std::uint64_t incarnation, Party self, std::uint64_t boot);
    [[nodiscard]] std::vector<std::byte> seal_to(const Party& to, FrameKind kind, std::span<const std::byte> payload);
    [[nodiscard]] std::optional<Opened> open(std::span<const std::byte> frame);
    [[nodiscard]] const Party& self() const noexcept { return self_; }

private:
    const Key& key(const Party& from, const Party& to);
    Key master_;
    std::uint64_t incarnation_;
    Party self_;
    std::uint64_t boot_;
    std::uint64_t counter_{};
    std::map<std::pair<Party, Party>, Key> keys_;
    std::map<Party, std::pair<std::uint64_t, std::uint64_t>> last_seen_;
};

}
