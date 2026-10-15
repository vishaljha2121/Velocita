#pragma once

#include "velocita/v2/swap.hpp"

#include <cstddef>
#include <span>

namespace velocita::v2::swap {
inline constexpr std::size_t max_record_bytes = 512;
inline constexpr std::size_t legacy_state_bytes = 3756;
inline constexpr std::size_t max_state_bytes = legacy_state_bytes + 65;
[[nodiscard]] constexpr std::size_t encoded_state_size(const State& s) noexcept {
    return !s.initialized?1:s.buy_price_amendment==BuyPriceAmendment{}?legacy_state_bytes:max_state_bytes;
}
struct Record {
    Role role{};
    std::uint64_t event_time{};
    Command command{};
    constexpr bool operator==(const Record&) const noexcept = default;
};
enum class CodecError : std::uint8_t { none, short_buffer, format, checksum, invalid_value };
struct Encoded { CodecError error{CodecError::format}; std::size_t size{}; };
[[nodiscard]] Encoded encode(const Record& record, std::span<std::byte> out) noexcept;
[[nodiscard]] CodecError decode(std::span<const std::byte> bytes, Record& out) noexcept;
[[nodiscard]] Encoded encode_state(const State& state, std::span<std::byte> out) noexcept;
[[nodiscard]] CodecError decode_state(std::span<const std::byte> bytes, State& out) noexcept;
}
