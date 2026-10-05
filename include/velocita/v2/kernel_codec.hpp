// Canonical byte encoding of kernel commands and results.
#pragma once

#include "velocita/v2/kernel.hpp"
#include "velocita/v2/payload.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace velocita::v2 {

inline constexpr std::uint16_t kKernelCodecSchemaVersion = 9;
inline constexpr std::size_t kKernelCommandMagicBytes = 4;
inline constexpr std::size_t kKernelCommandLengthBytes = 4;
inline constexpr std::size_t kKernelCommandSchemaBytes = 2;
inline constexpr std::size_t kKernelCommandKindBytes = 2;
inline constexpr std::size_t kKernelCommandSchemaOffset = 8;
inline constexpr std::size_t kKernelCommandKindOffset = 10;
inline constexpr std::size_t kKernelCommandPayloadOffset = 12;
inline constexpr std::size_t kKernelCommandDigestInputOffset =
    kKernelCommandSchemaOffset;
inline constexpr std::size_t kKernelCommandIdentityBytes = 48;
inline constexpr std::size_t kInstallExecutionCatalogBodyBytes = 48;
inline constexpr std::size_t kInstrumentExecutionProfileEncodedBytes = 66;
inline constexpr std::size_t kVenueCapabilitiesEncodedBytes = 21;
inline constexpr std::size_t kMaxKernelCommandBodyBytes =
    kMaxFeatureSchemaPayloadBytes + kMaxExecutablePolicyPayloadBytes +
    kInstrumentExecutionProfileEncodedBytes + kVenueCapabilitiesEncodedBytes;
inline constexpr std::size_t kMaxKernelCommandPayloadBytes =
    kKernelCommandIdentityBytes + kMaxKernelCommandBodyBytes;
inline constexpr std::size_t kMaxKernelCommandBytes =
    kKernelCommandPayloadOffset + kMaxKernelCommandPayloadBytes + kDigestBytes;

inline constexpr std::size_t kKernelResultSchemaOffset = 8;
inline constexpr std::size_t kKernelResultPayloadOffset = 10;
inline constexpr std::size_t kKernelResultDigestInputOffset =
    kKernelResultSchemaOffset;
inline constexpr std::size_t kKernelResultPayloadBytes = 86;
inline constexpr std::size_t kKernelResultBytes =
    kKernelResultPayloadOffset + kKernelResultPayloadBytes + kDigestBytes;

enum class KernelCommandKind : std::uint16_t {
    publish_policy = 1,
    validate_policy = 2,
    activate_policy = 3,
    supersede_policy = 4,
    set_strategy_mode = 5,
    advance_kill_epoch = 6,
    set_executor_generation = 7,
    record_source_event = 8,
    evaluate_decision_frame = 9,
    stage_executable_policy = 10,
    activate_executable_policy = 11,
    authorize_decision = 12,
    observe_fill = 13,
    request_cancel = 14,
    observe_order_event = 15,
    request_replace = 16,
    configure_risk_limits = 17,
    acknowledge_gateway_results = 18,
    request_mass_cancel = 19,
    install_execution_catalog = 20,
};

enum class KernelCodecError : std::uint8_t {
    none = 0,
    output_too_small,
    truncated,
    invalid_magic,
    unsupported_schema,
    unknown_kind,
    length_mismatch,
    digest_mismatch,
    invalid_value,
};

struct EncodeKernelValueResult {
    KernelCodecError error{KernelCodecError::none};
    std::size_t bytes_written{};
    Digest256 frame_digest{};
};

struct DecodeKernelValueResult {
    KernelCodecError error{KernelCodecError::none};
    Digest256 frame_digest{};
};

struct KernelCommandCodecScratch final {
    Command candidate{};
};

[[nodiscard]] EncodeKernelValueResult
encode_kernel_command(const Command& command,
                      std::span<std::byte> output) noexcept;

[[nodiscard]] DecodeKernelValueResult
decode_kernel_command(std::span<const std::byte> bytes,
                      Command& destination,
                      KernelCommandCodecScratch& scratch) noexcept;

[[nodiscard]] EncodeKernelValueResult
encode_kernel_result(const ApplyResult& result,
                     std::span<std::byte> output) noexcept;

[[nodiscard]] DecodeKernelValueResult
decode_kernel_result(std::span<const std::byte> bytes,
                     ApplyResult& destination) noexcept;

static_assert(kMaxKernelCommandBytes <= kKernelCommandBudgetBytes,
              "kernel command exceeded its reviewed canonical byte budget");
static_assert(kKernelResultBytes <= kKernelResultBudgetBytes,
              "kernel result exceeded its reviewed canonical byte budget");

}
