// The five Raft log entry types and their canonical byte encoding.
#pragma once

#include "velocita/tape/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace velocita::tape {

inline constexpr std::uint8_t kEntryVersion = 1;

struct Symbol {
    std::array<char, kMaxSymbolBytes> bytes{};
    std::uint8_t size{};

    [[nodiscard]] std::string_view view() const noexcept { return {bytes.data(), size}; }
    [[nodiscard]] static std::optional<Symbol> from(std::string_view text) noexcept;
    friend bool operator==(const Symbol& a, const Symbol& b) noexcept { return a.view() == b.view(); }
};

struct RecorderHeader {
    std::uint8_t recorder{};
    std::uint64_t session{};
    std::uint64_t request_seq{};
};

struct Observation {
    RecorderHeader header;
    Channel channel{Channel::none};
    UpdateKind kind{UpdateKind::none};
    Symbol symbol;
    bool has_seqnum{};
    std::uint64_t seqnum{};
    Digest payload_hash;
    std::int64_t utc_recv_ns{};
    std::uint64_t mono_recv_ns{};
    std::uint32_t clock_error_us{kUnknownClockError};
    std::span<const std::byte> payload;
};

struct ObservationPart {
    RecorderHeader header;
    std::uint64_t first_request_seq{};
    std::uint16_t part_index{};
    std::uint16_t part_count{};
    std::uint32_t whole_size{};
    Digest whole_hash;
    std::span<const std::byte> chunk;
};

struct RecorderEvent {
    RecorderHeader header;
    EventKind kind{EventKind::session_start};
    Channel channel{Channel::none};
    Symbol symbol;
    std::uint64_t expected{};
    std::uint64_t received{};
    std::int64_t utc_ns{};
};

struct Tick {
    std::uint64_t number{};
    std::uint8_t proposer{};
    std::int64_t proposer_utc_ns{};
};

struct HeadSignature {
    std::uint64_t tick{};
    std::uint64_t tree_size{};
    Digest root;
    std::uint8_t voter{};
    std::array<std::byte, kSignatureBytes> signature{};
};

using Entry = std::variant<Observation, ObservationPart, RecorderEvent, Tick, HeadSignature>;

[[nodiscard]] EntryType entry_type(const Entry& entry) noexcept;

[[nodiscard]] std::size_t encode_entry(const Entry& entry, std::span<std::byte> out) noexcept;

[[nodiscard]] std::optional<Entry> decode_entry(std::span<const std::byte> bytes) noexcept;

[[nodiscard]] std::size_t encode_observation_whole(const Observation& observation, std::span<std::byte> out) noexcept;

}
