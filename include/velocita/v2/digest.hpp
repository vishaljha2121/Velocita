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
    // Precondition: cumulative input across all updates is less than 2^61
    // bytes, so SHA-256's 64-bit encoded bit length cannot overflow.
    void update(std::span<const std::byte> bytes) noexcept;
    // Nondestructive: repeated calls are identical, and update() may continue
    // the same digest after an intermediate finish().
    [[nodiscard]] Digest256 finish() const noexcept;

private:
    std::array<std::uint32_t, 8> state_{};
    std::array<std::byte, 64> buffer_{};
    std::uint64_t total_bytes_{};
    std::size_t buffered_{};
};

[[nodiscard]] Digest256 sha256_digest(std::span<const std::byte> bytes) noexcept;
// SHA-256 of ASCII "velocita/venue-observation/v2" (no terminator) followed
// by canonical VenueObservation schema V2 bytes. No request-envelope fields.
// Content fingerprint only: does not validate or authenticate the observation.
[[nodiscard]] Digest256 observation_fingerprint(const VenueObservation& value) noexcept;
[[nodiscard]] std::string to_hex(const Digest256& digest);
[[nodiscard]] std::optional<Digest256> parse_digest256(std::string_view hex) noexcept;

}  // namespace velocita::v2
