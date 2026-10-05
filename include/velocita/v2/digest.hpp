// SHA-256 and the domain-separated digests built on it.
#pragma once

#include "velocita/v2/contracts.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace velocita::v2 {

class Sha256 final {
public:
    Sha256() noexcept;
    void update(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] Digest256 finish() const noexcept;

private:
    std::array<std::uint32_t, 8> state_{};
    std::array<std::byte, 64> buffer_{};
    std::uint64_t total_bytes_{};
    std::size_t buffered_{};
};

[[nodiscard]] Digest256 sha256_digest(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] Digest256 observation_fingerprint(const VenueObservation& value) noexcept;
[[nodiscard]] std::string to_hex(const Digest256& digest);
[[nodiscard]] std::optional<Digest256> parse_digest256(std::string_view hex) noexcept;

}
