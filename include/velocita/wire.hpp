#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
#include "velocita/benchmark_timing.hpp"
#endif

namespace velocita {

inline constexpr std::uint16_t kWireVersion = 1;
inline constexpr std::uint16_t kResponseWireVersion = 3;
inline constexpr std::uint32_t kResponseBenchmarkTimingExtension = 1U << 0U;
inline constexpr std::size_t kEncodedCommandSize = 24;

enum class CommandType : std::uint16_t {
    increment = 1,
    get = 2,
};

enum class ResponseStatus : std::uint16_t {
    ok = 0,
    not_leader = 1,
    invalid_request = 2,
    internal_error = 3,
};

struct Command {
    CommandType type{};
    std::uint64_t request_id{};
    std::int64_t amount{};
};

struct Response {
    ResponseStatus status{};
    std::int32_t leader_id{-1};
    std::uint64_t request_id{};
    std::int64_t value{};
    std::int64_t term{};
    std::int64_t index{};
    std::uint16_t wire_version{kResponseWireVersion};
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    BenchmarkTiming benchmark;
#endif
};

enum class PeerMessageType : std::uint16_t {
    request_vote = 1,
    request_vote_response = 2,
    append_entries = 3,
    append_entries_response = 4,
};

struct PeerLogEntry {
    std::int64_t term{};
    std::int32_t id{};
    std::int32_t type{};
    std::vector<std::byte> payload;
};

struct RequestVote {
    std::int64_t term{};
    std::int32_t candidate_id{};
    std::int64_t last_log_index{};
    std::int64_t last_log_term{};
};

struct RequestVoteResponse {
    std::int64_t term{};
    bool vote_granted{};
};

struct AppendEntries {
    std::int64_t term{};
    std::int64_t previous_log_index{};
    std::int64_t previous_log_term{};
    std::int64_t leader_commit{};
    std::vector<PeerLogEntry> entries;
    // V2 only (the V1 codec ignores it): the lowest index every voter holds,
    // below which any voter may compact without stranding another.
    std::int64_t compaction_floor{};
};

struct AppendEntriesResponse {
    std::int64_t term{};
    bool success{};
    std::int64_t current_index{};
    std::int64_t first_index{};
};

using PeerPayload = std::variant<RequestVote, RequestVoteResponse, AppendEntries, AppendEntriesResponse>;

struct PeerMessage {
    std::int32_t sender_id{};
    std::int32_t recipient_id{};
    PeerPayload payload;
};

std::vector<std::byte> encode_command(const Command& command);
std::array<std::byte, kEncodedCommandSize> encode_command_fixed(const Command& command);
Command decode_command(std::span<const std::byte> bytes);
std::vector<std::byte> encode_response(const Response& response);
Response decode_response(std::span<const std::byte> bytes);
std::vector<std::byte> encode_peer_message(const PeerMessage& message);
PeerMessage decode_peer_message(std::span<const std::byte> bytes);
std::string to_string(CommandType type);
std::string to_string(ResponseStatus status);

}  // namespace velocita
