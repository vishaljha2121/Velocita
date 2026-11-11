// Reads the fields the tape needs from a venue frame, without allocating.
#pragma once

#include "velocita/tape/types.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace velocita::tape {

struct FrameHeader {
    Channel channel{Channel::none};
    UpdateKind kind{UpdateKind::none};
    bool has_seqnum{};
    std::uint64_t seqnum{};
    std::string_view symbol;
};

[[nodiscard]] std::optional<FrameHeader> classify_frame(std::string_view raw) noexcept;

enum class DecimalStatus : std::uint8_t { ok = 0, malformed = 1, precision = 2, overflow = 3 };

[[nodiscard]] DecimalStatus parse_atoms(std::string_view text, std::int64_t& out) noexcept;

struct LevelText {
    std::string_view price;
    std::string_view qty;
    std::string_view count;
};

using LevelVisitor = bool (*)(void* context, bool bid, const LevelText& level);

[[nodiscard]] bool for_each_level(std::string_view raw, LevelVisitor visit, void* context) noexcept;

struct BestText {
    bool has_bid{};
    bool has_ask{};
    LevelText bid;
    LevelText ask;
};

[[nodiscard]] bool read_ebbo(std::string_view raw, BestText& out) noexcept;
[[nodiscard]] bool read_trade(std::string_view raw, std::string_view& price, std::string_view& qty) noexcept;
[[nodiscard]] bool read_instrument_band(std::string_view raw, std::string_view& percent) noexcept;

}
