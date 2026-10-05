// Checksummed envelope for canonical messages.
#pragma once

#include "velocita/v2/domain.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace velocita::v2 {

inline constexpr std::uint32_t kEnvelopeMagic = 0x32544C56U;
inline constexpr std::uint16_t kEnvelopeVersion = 1;
inline constexpr std::uint32_t kSupportedEnvelopeFlags = 0;
inline constexpr std::size_t kEnvelopeHeaderBytes = 80;
inline constexpr std::size_t kEnvelopeChecksumBytes = 4;
inline constexpr std::size_t kEnvelopeFixedBytes =
    kEnvelopeHeaderBytes + kEnvelopeChecksumBytes;
inline constexpr std::size_t kMaxEnvelopePayloadBytes = 64U * 1024U;

enum class MessageKind : std::uint16_t {
    publish_policy = 1,
    activate_policy = 2,
    supersede_policy = 3,
    evaluate_decision_frame = 4,
    set_strategy_mode = 5,
    advance_kill_epoch = 6,
    set_executor_generation = 7,
    observe_order_ack = 8,
    observe_order_reject = 9,
    observe_fill = 10,
    observe_cancel = 11,
    observe_effect_unknown = 12,
    apply_reconciliation = 13,
    command_result = 14,
    order_effect = 15,
};

enum class EnvelopeError : std::uint8_t {
    none = 0,
    truncated,
    length_mismatch,
    invalid_magic,
    unsupported_version,
    unknown_message_kind,
    unsupported_flags,
    payload_too_large,
    checksum_mismatch,
    invalid_identity,
};

struct Envelope {
    MessageKind kind{};
    std::uint32_t flags{};
    CallerId caller_id{};
    RequestSequence request_sequence{};
    CorrelationId correlation_id{};
    SourceId source_id{};
    SourceGeneration source_generation{};
    SourceSequence source_sequence{};
    std::int64_t event_time_ns{};
    std::int64_t receive_time_ns{};
    std::vector<std::byte> payload;
};

struct EncodeEnvelopeResult {
    EnvelopeError error{EnvelopeError::none};
    std::vector<std::byte> bytes;
};

struct DecodeEnvelopeResult {
    EnvelopeError error{EnvelopeError::none};
    std::optional<Envelope> envelope;
};

[[nodiscard]] EncodeEnvelopeResult encode_envelope(const Envelope& envelope);
[[nodiscard]] DecodeEnvelopeResult decode_envelope(std::span<const std::byte> bytes);
[[nodiscard]] std::uint32_t crc32_checksum(std::span<const std::byte> bytes) noexcept;

}
