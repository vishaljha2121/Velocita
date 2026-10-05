// Applies a recorded command frame and emits its result frame.
#pragma once

#include "velocita/v2/kernel_codec.hpp"

#include <cstddef>
#include <span>

namespace velocita::v2 {

enum class ReplayError {
    none,
    result_output_too_small,
    command_decode_failed,
    result_encode_failed,
};

struct ReplayApplyResult {
    ReplayError error{ReplayError::none};
    KernelCodecError codec_error{KernelCodecError::none};
    ApplyResult apply_result{};
    std::size_t result_bytes_written{};
    Digest256 command_frame_digest{};
    Digest256 result_frame_digest{};
};

[[nodiscard]] ReplayApplyResult apply_encoded_kernel_command(
    TradingState& state,
    std::span<const std::byte> command_bytes,
    ApplyScratch& apply_scratch,
    KernelCommandCodecScratch& codec_scratch,
    std::span<std::byte> result_output,
    const VerifiedExecutionCatalog* catalog = nullptr);

}
