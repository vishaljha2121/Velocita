// Shared limits, identifiers and enumerations of the verifiable tape.
#pragma once

#include "velocita/v2/contracts.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace velocita::tape {

using Digest = v2::Digest256;

inline constexpr std::size_t kRecorders = 3;
inline constexpr std::size_t kVoters = 3;
inline constexpr std::size_t kMaxSymbols = 16;
inline constexpr std::size_t kMaxSymbolBytes = 31;
inline constexpr std::size_t kChannels = 4;
inline constexpr std::size_t kMaxStreams = kChannels * kMaxSymbols;
inline constexpr std::size_t kLevelsPerSide = 64;
inline constexpr std::size_t kHeldPerBook = 8;
inline constexpr std::size_t kChangesPerHeld = 32;
inline constexpr std::size_t kOpenVerdicts = 256;
inline constexpr std::size_t kRecentClosed = 1024;
inline constexpr std::size_t kAssemblies = 8;
inline constexpr std::size_t kMaxInlinePayload = 4096;
inline constexpr std::size_t kMaxParts = 8;
inline constexpr std::size_t kMaxFrameBytes = kMaxInlinePayload * kMaxParts;
inline constexpr std::size_t kMaxEntryBytes = kMaxInlinePayload + 256;
inline constexpr std::size_t kMerkleFrontier = 64;
inline constexpr std::size_t kSignatureBytes = 64;
inline constexpr std::size_t kPendingHeads = 4;
inline constexpr std::int64_t kAtomScale = 1'000'000'000;
inline constexpr std::uint32_t kUnknownClockError = 0xFFFF'FFFFu;

enum class Channel : std::uint8_t { none = 0, instrument = 1, ebbo = 2, depth = 3, trade = 4, heartbeat = 5, control = 6 };
enum class UpdateKind : std::uint8_t { none = 0, snapshot = 1, update = 2, snapshot_complete = 3, heartbeat = 4, control = 5, unparsed = 6 };
enum class EntryType : std::uint8_t { observation = 1, observation_part = 2, recorder_event = 3, tick = 4, head_signature = 5 };
enum class EventKind : std::uint8_t {
    session_start = 1, gap = 2, regression = 3, overflow = 4, dead_connection = 5, reconnect = 6, resubscribe = 7, oversize = 8
};
enum class BookState : std::uint8_t { unknown = 0, building = 1, valid = 2 };
enum class Reason : std::uint8_t {
    none = 0, not_started = 1, divergence = 2, malformed = 3, precision = 4, overflow = 5, capacity = 6, gap = 7, snapshot_incomplete = 8
};
enum class Outcome : std::uint8_t { agree_3_of_3 = 1, agree_2_of_3 = 2, only_1_of_3 = 3, divergent = 4 };
enum class CloseReason : std::uint8_t { all_reported = 1, n_ticks = 2, capacity = 3 };

enum class ApplyCode : std::uint8_t {
    applied = 0,
    duplicate_request = 1,
    out_of_order_request = 2,
    unknown_recorder = 3,
    malformed_entry = 4,
    classification_mismatch = 5,
    part_stored = 6,
    part_refused = 7,
    tick_refused = 8,
    head_refused = 9,
    symbol_capacity = 10,
};

inline constexpr std::uint16_t kFlagCrossed = 1u << 0;
inline constexpr std::uint16_t kFlagLocked = 1u << 1;
inline constexpr std::uint16_t kFlagEbboMismatch = 1u << 2;
inline constexpr std::uint16_t kFlagTradeOutsideBook = 1u << 3;
inline constexpr std::uint16_t kFlagTradeOutsideBand = 1u << 4;
inline constexpr std::uint16_t kFlagSnapshotDiffers = 1u << 5;
inline constexpr std::uint16_t kFlagLateCopy = 1u << 6;
inline constexpr std::uint16_t kFlagLateDivergence = 1u << 7;
inline constexpr std::uint16_t kFlagStaleSnapshot = 1u << 8;

struct Config {
    std::uint64_t verdict_ticks{5};
    std::uint64_t head_every_ticks{60};
    bool integrity_checks{true};
};

[[nodiscard]] constexpr std::size_t channel_slot(Channel channel) noexcept {
    switch (channel) {
        case Channel::instrument: return 0;
        case Channel::ebbo: return 1;
        case Channel::depth: return 2;
        case Channel::trade: return 3;
        default: return kChannels;
    }
}

[[nodiscard]] std::string_view to_string(Channel value) noexcept;
[[nodiscard]] std::string_view to_string(UpdateKind value) noexcept;
[[nodiscard]] std::string_view to_string(EventKind value) noexcept;
[[nodiscard]] std::string_view to_string(BookState value) noexcept;
[[nodiscard]] std::string_view to_string(Reason value) noexcept;
[[nodiscard]] std::string_view to_string(Outcome value) noexcept;
[[nodiscard]] std::string_view to_string(CloseReason value) noexcept;

}
