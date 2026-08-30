#pragma once

#include "velocita/v2/contracts.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace velocita::v2 {

[[nodiscard]] Digest256 sha256_digest(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] std::string to_hex(const Digest256& digest);
[[nodiscard]] std::optional<Digest256> parse_digest256(std::string_view hex) noexcept;

}  // namespace velocita::v2
