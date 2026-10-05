#include "velocita/v2/wire.hpp"

#include <type_traits>
#include <utility>

namespace velocita::v2 {
namespace {

template <typename T>
void append_le(std::vector<std::byte>& out, const T value) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    const U bits = static_cast<U>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        out.push_back(static_cast<std::byte>((bits >> (index * 8U)) & 0xFFU));
    }
}

template <typename T>
T read_le(const std::span<const std::byte> bytes, std::size_t& offset) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    U bits{};
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bits |= static_cast<U>(std::to_integer<unsigned char>(bytes[offset + index]))
                << (index * 8U);
    }
    offset += sizeof(T);
    return static_cast<T>(bits);
}

bool known_kind(const MessageKind kind) noexcept {
    switch (kind) {
        case MessageKind::publish_policy:
        case MessageKind::activate_policy:
        case MessageKind::supersede_policy:
        case MessageKind::evaluate_decision_frame:
        case MessageKind::set_strategy_mode:
        case MessageKind::advance_kill_epoch:
        case MessageKind::set_executor_generation:
        case MessageKind::observe_order_ack:
        case MessageKind::observe_order_reject:
        case MessageKind::observe_fill:
        case MessageKind::observe_cancel:
        case MessageKind::observe_effect_unknown:
        case MessageKind::apply_reconciliation:
        case MessageKind::command_result:
        case MessageKind::order_effect:
            return true;
    }
    return false;
}

bool valid_identity(const Envelope& envelope) noexcept {
    return envelope.caller_id.value() != 0 &&
           envelope.request_sequence.value() != 0 &&
           envelope.correlation_id.value() != 0 &&
           envelope.source_id.value() != 0 &&
           envelope.source_generation.value() != 0;
}

}

std::uint32_t crc32_checksum(const std::span<const std::byte> bytes) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

EncodeEnvelopeResult encode_envelope(const Envelope& envelope) {
    if (!known_kind(envelope.kind)) return {EnvelopeError::unknown_message_kind, {}};
    if ((envelope.flags & ~kSupportedEnvelopeFlags) != 0) {
        return {EnvelopeError::unsupported_flags, {}};
    }
    if (!valid_identity(envelope)) return {EnvelopeError::invalid_identity, {}};
    if (envelope.payload.size() > kMaxEnvelopePayloadBytes) {
        return {EnvelopeError::payload_too_large, {}};
    }

    EncodeEnvelopeResult result;
    result.bytes.reserve(kEnvelopeFixedBytes + envelope.payload.size());
    append_le(result.bytes, kEnvelopeMagic);
    append_le(result.bytes, kEnvelopeVersion);
    append_le(result.bytes, static_cast<std::uint16_t>(envelope.kind));
    append_le(result.bytes, envelope.flags);
    append_le(result.bytes, static_cast<std::uint32_t>(envelope.payload.size()));
    append_le(result.bytes, envelope.caller_id.value());
    append_le(result.bytes, envelope.request_sequence.value());
    append_le(result.bytes, envelope.correlation_id.value());
    append_le(result.bytes, envelope.source_id.value());
    append_le(result.bytes, envelope.source_generation.value());
    append_le(result.bytes, envelope.source_sequence.value());
    append_le(result.bytes, envelope.event_time_ns);
    append_le(result.bytes, envelope.receive_time_ns);
    result.bytes.insert(result.bytes.end(), envelope.payload.begin(), envelope.payload.end());
    append_le(result.bytes, crc32_checksum(result.bytes));
    return result;
}

DecodeEnvelopeResult decode_envelope(const std::span<const std::byte> bytes) {
    if (bytes.size() < kEnvelopeFixedBytes) return {EnvelopeError::truncated, std::nullopt};

    std::size_t offset{};
    if (read_le<std::uint32_t>(bytes, offset) != kEnvelopeMagic) {
        return {EnvelopeError::invalid_magic, std::nullopt};
    }
    if (read_le<std::uint16_t>(bytes, offset) != kEnvelopeVersion) {
        return {EnvelopeError::unsupported_version, std::nullopt};
    }
    const auto kind = static_cast<MessageKind>(read_le<std::uint16_t>(bytes, offset));
    if (!known_kind(kind)) return {EnvelopeError::unknown_message_kind, std::nullopt};

    const auto flags = read_le<std::uint32_t>(bytes, offset);
    if ((flags & ~kSupportedEnvelopeFlags) != 0) {
        return {EnvelopeError::unsupported_flags, std::nullopt};
    }
    const auto payload_size = read_le<std::uint32_t>(bytes, offset);
    if (payload_size > kMaxEnvelopePayloadBytes) {
        return {EnvelopeError::payload_too_large, std::nullopt};
    }

    const auto expected_size = kEnvelopeFixedBytes + static_cast<std::size_t>(payload_size);
    if (bytes.size() < expected_size) return {EnvelopeError::truncated, std::nullopt};
    if (bytes.size() > expected_size) return {EnvelopeError::length_mismatch, std::nullopt};

    const auto stored_checksum_offset = expected_size - kEnvelopeChecksumBytes;
    std::size_t checksum_offset = stored_checksum_offset;
    const auto stored_checksum = read_le<std::uint32_t>(bytes, checksum_offset);
    if (stored_checksum != crc32_checksum(bytes.first(stored_checksum_offset))) {
        return {EnvelopeError::checksum_mismatch, std::nullopt};
    }

    Envelope envelope;
    envelope.kind = kind;
    envelope.flags = flags;
    envelope.caller_id = CallerId{read_le<std::uint64_t>(bytes, offset)};
    envelope.request_sequence = RequestSequence{read_le<std::uint64_t>(bytes, offset)};
    envelope.correlation_id = CorrelationId{read_le<std::uint64_t>(bytes, offset)};
    envelope.source_id = SourceId{read_le<std::uint64_t>(bytes, offset)};
    envelope.source_generation = SourceGeneration{read_le<std::uint64_t>(bytes, offset)};
    envelope.source_sequence = SourceSequence{read_le<std::uint64_t>(bytes, offset)};
    envelope.event_time_ns = read_le<std::int64_t>(bytes, offset);
    envelope.receive_time_ns = read_le<std::int64_t>(bytes, offset);

    const auto payload_end = offset + payload_size;
    envelope.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                            bytes.begin() + static_cast<std::ptrdiff_t>(payload_end));

    if (!valid_identity(envelope)) return {EnvelopeError::invalid_identity, std::nullopt};
    return {EnvelopeError::none, std::move(envelope)};
}

}
