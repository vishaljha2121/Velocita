#pragma once

#include "velocita/v2/static_identity.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace velocita::v2 {

inline constexpr std::uint16_t kLocalGenerationSchemaVersion = 1;
inline constexpr std::size_t kLocalGenerationSelectorBytes = 160;

// Native diagnostic only; never serialized or used as selection authority.
enum class SelectorTemporaryStatus : std::uint8_t { absent, valid, invalid };

struct LocalGenerationSelector {
    std::uint64_t generation{};
    std::uint64_t selection_sequence{};
    std::uint64_t previous_generation{};
    Digest256 prepared_base_digest{};
    constexpr auto operator<=>(const LocalGenerationSelector&) const noexcept = default;
};

enum class LocalGenerationError : std::uint8_t {
    none,
    output_too_small,
    length_mismatch,
    invalid_magic,
    unsupported_schema,
    nonzero_reserved,
    digest_mismatch,
    invalid_identity,
    foreign_manifest,
    wrong_owner,
    invalid_value,
    invalid_transition,
};

struct EncodeLocalGenerationResult {
    LocalGenerationError error{LocalGenerationError::none};
    std::size_t bytes_written{};
};

// Failure leaves OUTPUT unchanged. VALUE is copied before publication, so its
// object representation may overlap OUTPUT. EXPECTED_IDENTITY must be a valid,
// separately verified identity; its values are also copied before publication.
[[nodiscard]] EncodeLocalGenerationResult encode_local_generation_selector(
    const LocalGenerationSelector& value,
    const VerifiedStaticIdentity& expected_identity,
    std::span<std::byte> output) noexcept;

// Failure leaves DESTINATION unchanged. INPUT is borrowed only for this call.
[[nodiscard]] LocalGenerationError decode_local_generation_selector(
    std::span<const std::byte> input,
    const VerifiedStaticIdentity& expected_identity,
    LocalGenerationSelector& destination) noexcept;

// Pure value transition only. This does not prove directory freshness, ownership,
// publication durability, or that GENERATION has never existed on disk.
[[nodiscard]] LocalGenerationError validate_local_generation_transition(
    const LocalGenerationSelector& old_value,
    const LocalGenerationSelector& new_value) noexcept;

} // namespace velocita::v2
