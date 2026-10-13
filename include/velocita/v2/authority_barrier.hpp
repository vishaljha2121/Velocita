// The log entry a gateway commits to obtain permission for one send.
#pragma once

#include "velocita/v2/contracts.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace velocita::v2 {

inline constexpr std::int32_t kAuthorityBarrierEntryType = 101;
inline constexpr std::int32_t kSwapAuthorityBarrierEntryType = 102;
inline constexpr std::uint16_t kAuthorityBarrierSchemaVersion = 1;
inline constexpr std::size_t kAuthorityBarrierBodyBytes = 144;
inline constexpr std::size_t kAuthorityBarrierBytes = 186;

struct AuthorityBarrier {
    std::array<std::byte, 16> incarnation{};
    std::uint64_t request_sequence{};
    EffectId effect_id{};
    Digest256 expected_effect_payload_digest{};
    KillEpoch expected_kill_epoch{};
    ExecutorGeneration expected_executor_generation{};
    Digest256 catalog_root{};
    Digest256 authority_domain{};

    constexpr auto operator<=>(const AuthorityBarrier&) const noexcept = default;
};

enum class AuthorityBarrierCodecError : std::uint8_t {
    none = 0,
    wrong_entry_type,
    output_too_small,
    truncated,
    invalid_magic,
    unsupported_schema,
    length_mismatch,
    digest_mismatch,
    invalid_value,
};

struct EncodeAuthorityBarrierResult {
    AuthorityBarrierCodecError error{AuthorityBarrierCodecError::none};
    std::size_t bytes_written{};
    Digest256 frame_digest{};
};

struct DecodeAuthorityBarrierResult {
    AuthorityBarrierCodecError error{AuthorityBarrierCodecError::none};
    Digest256 frame_digest{};
};

[[nodiscard]] EncodeAuthorityBarrierResult
encode_authority_barrier(std::int32_t entry_type,
                         const AuthorityBarrier& value,
                         std::span<std::byte> output) noexcept;

[[nodiscard]] DecodeAuthorityBarrierResult
decode_authority_barrier(std::int32_t entry_type,
                         std::span<const std::byte> bytes,
                         AuthorityBarrier& destination) noexcept;

static_assert(sizeof(AuthorityBarrier) <= 160,
              "authority barrier exceeded its reviewed bounded representation");

}
