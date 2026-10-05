#include "velocita/v2/replay.hpp"

namespace velocita::v2 {

ReplayApplyResult apply_encoded_kernel_command(
    TradingState& state,
    const std::span<const std::byte> command_bytes,
    ApplyScratch& apply_scratch,
    KernelCommandCodecScratch& codec_scratch,
    const std::span<std::byte> result_output,
    const VerifiedExecutionCatalog* catalog) {
    if (result_output.size() < kKernelResultBytes) {
        return {.error = ReplayError::result_output_too_small};
    }

    const auto decoded =
        decode_kernel_command(command_bytes, codec_scratch.candidate, codec_scratch);
    if (decoded.error != KernelCodecError::none) {
        return {
            .error = ReplayError::command_decode_failed,
            .codec_error = decoded.error,
            .command_frame_digest = decoded.frame_digest,
        };
    }

    const auto applied = apply(state, codec_scratch.candidate, apply_scratch, catalog);
    const auto encoded = encode_kernel_result(applied, result_output);
    if (encoded.error != KernelCodecError::none) {
        return {
            .error = ReplayError::result_encode_failed,
            .codec_error = encoded.error,
            .apply_result = applied,
            .command_frame_digest = decoded.frame_digest,
        };
    }

    return {
        .error = ReplayError::none,
        .codec_error = KernelCodecError::none,
        .apply_result = applied,
        .result_bytes_written = encoded.bytes_written,
        .command_frame_digest = decoded.frame_digest,
        .result_frame_digest = encoded.frame_digest,
    };
}

}
