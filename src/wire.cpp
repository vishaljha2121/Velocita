#include "velocita/wire.hpp"

#include <array>
#include <bit>
#include <stdexcept>
#include <type_traits>

namespace velocita {
namespace {

constexpr std::uint32_t kCommandMagic = 0x31444D43U;   // CMD1 on little-endian hosts
constexpr std::uint32_t kResponseMagic = 0x31505352U;  // RSP1
constexpr std::uint32_t kPeerMagic = 0x31524550U;      // PER1
constexpr std::uint32_t kMaxPeerEntries = 4096;
constexpr std::uint32_t kMaxPeerPayload = 8U * 1024U * 1024U;

template <typename T>
void append_le(std::vector<std::byte>& out, T value) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    U bits = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<std::byte>((bits >> (i * 8U)) & 0xFFU));
    }
}

template <typename T>
T read_le(std::span<const std::byte> bytes, std::size_t& offset) {
    static_assert(std::is_integral_v<T>);
    if (offset + sizeof(T) > bytes.size()) {
        throw std::runtime_error("truncated wire message");
    }
    using U = std::make_unsigned_t<T>;
    U value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<U>(std::to_integer<unsigned char>(bytes[offset + i])) << (i * 8U);
    }
    offset += sizeof(T);
    return static_cast<T>(value);
}

template <typename T>
void write_le(std::span<std::byte> bytes, std::size_t& offset, T value) {
    static_assert(std::is_integral_v<T>);
    if (offset + sizeof(T) > bytes.size()) throw std::logic_error("fixed wire buffer is too small");
    using U = std::make_unsigned_t<T>;
    const U bits = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes[offset + i] = static_cast<std::byte>((bits >> (i * 8U)) & 0xFFU);
    }
    offset += sizeof(T);
}

void require_consumed(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset != bytes.size()) {
        throw std::runtime_error("wire message has unexpected trailing bytes");
    }
}

PeerMessageType payload_type(const PeerPayload& payload) {
    if (std::holds_alternative<RequestVote>(payload)) return PeerMessageType::request_vote;
    if (std::holds_alternative<RequestVoteResponse>(payload)) return PeerMessageType::request_vote_response;
    if (std::holds_alternative<AppendEntries>(payload)) return PeerMessageType::append_entries;
    return PeerMessageType::append_entries_response;
}

}  // namespace

std::vector<std::byte> encode_command(const Command& command) {
    const auto fixed = encode_command_fixed(command);
    return {fixed.begin(), fixed.end()};
}

std::array<std::byte, kEncodedCommandSize> encode_command_fixed(const Command& command) {
    std::array<std::byte, kEncodedCommandSize> fixed{};
    std::size_t offset = 0;
    write_le(fixed, offset, kCommandMagic);
    write_le(fixed, offset, kWireVersion);
    write_le(fixed, offset, static_cast<std::uint16_t>(command.type));
    write_le(fixed, offset, command.request_id);
    write_le(fixed, offset, command.amount);
    return fixed;
}

Command decode_command(std::span<const std::byte> bytes) {
    std::size_t offset = 0;
    if (read_le<std::uint32_t>(bytes, offset) != kCommandMagic) {
        throw std::runtime_error("invalid command magic");
    }
    if (read_le<std::uint16_t>(bytes, offset) != kWireVersion) {
        throw std::runtime_error("unsupported command version");
    }
    const auto type = static_cast<CommandType>(read_le<std::uint16_t>(bytes, offset));
    if (type != CommandType::increment && type != CommandType::get) {
        throw std::runtime_error("unknown command type");
    }
    Command command{type, read_le<std::uint64_t>(bytes, offset), read_le<std::int64_t>(bytes, offset)};
    require_consumed(bytes, offset);
    return command;
}

std::vector<std::byte> encode_response(const Response& response) {
    std::vector<std::byte> out;
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    out.reserve(88);
#else
    out.reserve(40);
#endif
    append_le(out, kResponseMagic);
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    append_le<std::uint16_t>(out, 2);
#else
    append_le(out, kWireVersion);
#endif
    append_le(out, static_cast<std::uint16_t>(response.status));
    append_le(out, response.request_id);
    append_le(out, response.value);
    append_le(out, response.term);
    append_le(out, response.index);
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    append_le(out, response.benchmark.flags);
    append_le(out, response.benchmark.ingress_to_admission_ns);
    append_le(out, response.benchmark.local_wal_append_ns);
    append_le(out, response.benchmark.quorum_append_ns);
    append_le(out, response.benchmark.admission_to_commit_ns);
    append_le(out, response.benchmark.commit_to_apply_ns);
#endif
    return out;
}

Response decode_response(std::span<const std::byte> bytes) {
    std::size_t offset = 0;
    if (read_le<std::uint32_t>(bytes, offset) != kResponseMagic) {
        throw std::runtime_error("invalid response magic");
    }
    const auto version = read_le<std::uint16_t>(bytes, offset);
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    if (version != kWireVersion && version != 2) {
#else
    if (version != kWireVersion) {
#endif
        throw std::runtime_error("unsupported response version");
    }
    Response response;
    response.status = static_cast<ResponseStatus>(read_le<std::uint16_t>(bytes, offset));
    response.request_id = read_le<std::uint64_t>(bytes, offset);
    response.value = read_le<std::int64_t>(bytes, offset);
    response.term = read_le<std::int64_t>(bytes, offset);
    response.index = read_le<std::int64_t>(bytes, offset);
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    if (version == 2) {
        response.benchmark.flags = read_le<std::uint64_t>(bytes, offset);
        response.benchmark.ingress_to_admission_ns = read_le<std::uint64_t>(bytes, offset);
        response.benchmark.local_wal_append_ns = read_le<std::uint64_t>(bytes, offset);
        response.benchmark.quorum_append_ns = read_le<std::uint64_t>(bytes, offset);
        response.benchmark.admission_to_commit_ns = read_le<std::uint64_t>(bytes, offset);
        response.benchmark.commit_to_apply_ns = read_le<std::uint64_t>(bytes, offset);
    }
#endif
    require_consumed(bytes, offset);
    return response;
}

std::vector<std::byte> encode_peer_message(const PeerMessage& message) {
    std::vector<std::byte> out;
    out.reserve(64);
    append_le(out, kPeerMagic);
    append_le(out, kWireVersion);
    const auto type = payload_type(message.payload);
    append_le(out, static_cast<std::uint16_t>(type));
    append_le(out, message.sender_id);
    append_le(out, message.recipient_id);

    switch (type) {
        case PeerMessageType::request_vote: {
            const auto& value = std::get<RequestVote>(message.payload);
            append_le(out, value.term);
            append_le(out, value.candidate_id);
            append_le<std::uint32_t>(out, 0);
            append_le(out, value.last_log_index);
            append_le(out, value.last_log_term);
            break;
        }
        case PeerMessageType::request_vote_response: {
            const auto& value = std::get<RequestVoteResponse>(message.payload);
            append_le(out, value.term);
            append_le<std::uint8_t>(out, value.vote_granted ? 1 : 0);
            break;
        }
        case PeerMessageType::append_entries: {
            const auto& value = std::get<AppendEntries>(message.payload);
            if (value.entries.size() > kMaxPeerEntries) throw std::runtime_error("too many peer log entries");
            append_le(out, value.term);
            append_le(out, value.previous_log_index);
            append_le(out, value.previous_log_term);
            append_le(out, value.leader_commit);
            append_le(out, static_cast<std::uint32_t>(value.entries.size()));
            for (const auto& entry : value.entries) {
                if (entry.payload.size() > kMaxPeerPayload) throw std::runtime_error("peer entry payload too large");
                append_le(out, entry.term);
                append_le(out, entry.id);
                append_le(out, entry.type);
                append_le(out, static_cast<std::uint32_t>(entry.payload.size()));
                out.insert(out.end(), entry.payload.begin(), entry.payload.end());
            }
            break;
        }
        case PeerMessageType::append_entries_response: {
            const auto& value = std::get<AppendEntriesResponse>(message.payload);
            append_le(out, value.term);
            append_le<std::uint8_t>(out, value.success ? 1 : 0);
            append_le(out, value.current_index);
            append_le(out, value.first_index);
            break;
        }
    }
    return out;
}

PeerMessage decode_peer_message(std::span<const std::byte> bytes) {
    std::size_t offset = 0;
    if (read_le<std::uint32_t>(bytes, offset) != kPeerMagic) throw std::runtime_error("invalid peer magic");
    if (read_le<std::uint16_t>(bytes, offset) != kWireVersion) throw std::runtime_error("unsupported peer version");
    const auto type = static_cast<PeerMessageType>(read_le<std::uint16_t>(bytes, offset));
    PeerMessage message;
    message.sender_id = read_le<std::int32_t>(bytes, offset);
    message.recipient_id = read_le<std::int32_t>(bytes, offset);

    switch (type) {
        case PeerMessageType::request_vote: {
            RequestVote value;
            value.term = read_le<std::int64_t>(bytes, offset);
            value.candidate_id = read_le<std::int32_t>(bytes, offset);
            if (read_le<std::uint32_t>(bytes, offset) != 0) throw std::runtime_error("peer reserved field is nonzero");
            value.last_log_index = read_le<std::int64_t>(bytes, offset);
            value.last_log_term = read_le<std::int64_t>(bytes, offset);
            message.payload = value;
            break;
        }
        case PeerMessageType::request_vote_response: {
            RequestVoteResponse value;
            value.term = read_le<std::int64_t>(bytes, offset);
            const auto granted = read_le<std::uint8_t>(bytes, offset);
            if (granted > 1) throw std::runtime_error("invalid vote response boolean");
            value.vote_granted = granted != 0;
            message.payload = value;
            break;
        }
        case PeerMessageType::append_entries: {
            AppendEntries value;
            value.term = read_le<std::int64_t>(bytes, offset);
            value.previous_log_index = read_le<std::int64_t>(bytes, offset);
            value.previous_log_term = read_le<std::int64_t>(bytes, offset);
            value.leader_commit = read_le<std::int64_t>(bytes, offset);
            const auto count = read_le<std::uint32_t>(bytes, offset);
            if (count > kMaxPeerEntries) throw std::runtime_error("too many peer log entries");
            value.entries.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                PeerLogEntry entry;
                entry.term = read_le<std::int64_t>(bytes, offset);
                entry.id = read_le<std::int32_t>(bytes, offset);
                entry.type = read_le<std::int32_t>(bytes, offset);
                const auto length = read_le<std::uint32_t>(bytes, offset);
                if (length > kMaxPeerPayload || offset + length > bytes.size()) {
                    throw std::runtime_error("invalid peer entry payload length");
                }
                entry.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                     bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
                offset += length;
                value.entries.push_back(std::move(entry));
            }
            message.payload = std::move(value);
            break;
        }
        case PeerMessageType::append_entries_response: {
            AppendEntriesResponse value;
            value.term = read_le<std::int64_t>(bytes, offset);
            const auto success = read_le<std::uint8_t>(bytes, offset);
            if (success > 1) throw std::runtime_error("invalid append response boolean");
            value.success = success != 0;
            value.current_index = read_le<std::int64_t>(bytes, offset);
            value.first_index = read_le<std::int64_t>(bytes, offset);
            message.payload = value;
            break;
        }
        default:
            throw std::runtime_error("unknown peer message type");
    }
    require_consumed(bytes, offset);
    return message;
}

std::string to_string(CommandType type) {
    switch (type) {
        case CommandType::increment: return "increment";
        case CommandType::get: return "get";
    }
    return "unknown";
}

std::string to_string(ResponseStatus status) {
    switch (status) {
        case ResponseStatus::ok: return "ok";
        case ResponseStatus::not_leader: return "not_leader";
        case ResponseStatus::invalid_request: return "invalid_request";
        case ResponseStatus::internal_error: return "internal_error";
    }
    return "unknown";
}

}  // namespace velocita
