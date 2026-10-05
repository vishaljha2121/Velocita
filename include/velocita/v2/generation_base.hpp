// The base of a storage generation: genesis or an installed snapshot.
#pragma once

#include "velocita/v2/bound_frame.hpp"
#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/state_snapshot.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace velocita::v2 {

inline constexpr std::uint16_t kGenerationBaseSchemaVersion = 1;
inline constexpr std::size_t kGenerationBaseSealBytes = 40;

enum class GenerationBaseKind : std::uint8_t { genesis = 0, snapshot = 1 };

struct GenerationBase {
    GenerationBaseKind kind{GenerationBaseKind::genesis};
    Digest256 digest{};
    std::uint64_t index{};
    std::int64_t term{};
    Digest256 state_digest{};
    constexpr auto operator<=>(const GenerationBase&) const noexcept = default;
};

enum class GenerationBaseError : std::uint8_t {
    none,
    output_too_small,
    length_mismatch,
    invalid_magic,
    unsupported_schema,
    invalid_kind,
    nonzero_reserved,
    digest_mismatch,
    invalid_identity,
    catalog_mismatch,
    invalid_generation,
    invalid_snapshot,
    catalog_dependency_missing,
};

struct GenerationBaseScratch {
    BoundFrameScratch bound;
    StateSnapshotScratch state_decode;
    TradingState state;
    std::array<std::byte, kMaxStaticIdentityBytes> identity_bytes{};
};

struct EncodeGenerationBaseResult {
    GenerationBaseError error{GenerationBaseError::none};
    std::size_t bytes_written{};
    GenerationBase base{};
};

[[nodiscard]] EncodeGenerationBaseResult encode_generation_base(
    GenerationBaseKind kind, std::uint64_t generation,
    const VerifiedStaticIdentity& expected_identity,
    const VerifiedExecutionCatalog& expected_catalog,
    std::span<const std::byte> snapshot, std::span<std::byte> output,
    GenerationBaseScratch& scratch) noexcept;

[[nodiscard]] GenerationBaseError decode_generation_base(
    std::span<const std::byte> seal, std::uint64_t generation,
    const VerifiedStaticIdentity& expected_identity,
    const VerifiedExecutionCatalog& expected_catalog,
    std::span<const std::byte> snapshot, GenerationBaseScratch& scratch,
    GenerationBase& destination) noexcept;

}
