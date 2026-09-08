#pragma once

#include "velocita/v2/contracts.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace velocita::v2 {

[[nodiscard]] Digest256 sha256_digest(std::span<const std::byte> bytes) noexcept;
// SHA-256 of ASCII "velocita/venue-observation/v2" (no terminator) followed
// by canonical VenueObservation schema V2 bytes. No request-envelope fields.
// Content fingerprint only: does not validate or authenticate the observation.
[[nodiscard]] Digest256 observation_fingerprint(const VenueObservation& value) noexcept;
[[nodiscard]] std::string to_hex(const Digest256& digest);
[[nodiscard]] std::optional<Digest256> parse_digest256(std::string_view hex) noexcept;

}  // namespace velocita::v2
