#pragma once

#include "velocita/v2/digest.hpp"
#include "velocita/v2/static_identity.hpp"

#include <cstddef>
#include <compare>
#include <cstdint>
#include <span>

namespace velocita::v2 {

inline constexpr std::uint16_t kArchiveCatalogSchemaVersion = 1;
inline constexpr std::size_t kArchiveCatalogHeaderBytes = 96;
inline constexpr std::size_t kArchiveCatalogRowBytes = 112;
inline constexpr std::size_t kArchiveCatalogDigestBytes = 32;
inline constexpr std::size_t kMaxArchiveCatalogRows = 4096;
inline constexpr std::size_t kMaxArchiveCatalogBytes = 458880;

struct ArchiveCatalogHeader {
    std::uint64_t generation{};
    std::uint64_t publication_storage_generation{};
    std::uint64_t observed_commit_index{};
    std::uint64_t observed_durable_index{};
    std::int64_t observed_current_term{};
    constexpr auto operator<=>(const ArchiveCatalogHeader&) const noexcept = default;
};

struct ArchiveCatalogRow {
    std::uint64_t first_index{};
    std::uint64_t last_index{};
    std::int64_t predecessor_term{};
    std::int64_t last_term{};
    std::uint32_t record_count{};
    std::uint32_t segment_file_length{};
    std::uint64_t source_storage_generation{};
    Digest256 segment_digest{};
    Digest256 terminal_record_digest{};
    constexpr auto operator<=>(const ArchiveCatalogRow&) const noexcept = default;
};

enum class ArchiveCatalogError : std::uint8_t {
    none,
    output_too_small,
    truncated,
    invalid_magic,
    unsupported_schema,
    length_mismatch,
    capacity_exceeded,
    nonzero_flags,
    invalid_identity,
    foreign_manifest,
    foreign_publisher,
    digest_mismatch,
    invalid_generation,
    invalid_coordinates,
};

struct ArchiveCatalogEncodeResult {
    ArchiveCatalogError error{ArchiveCatalogError::none};
    std::size_t bytes_written{};
    Digest256 catalog_digest{};
};

// Validation and capacity failures leave output unchanged.
[[nodiscard]] ArchiveCatalogEncodeResult encode_archive_catalog(
    const ArchiveCatalogHeader& header, std::span<const ArchiveCatalogRow> rows,
    const VerifiedStaticIdentity& identity, std::span<std::byte> output) noexcept;

// Fully validates the immutable input before copying. On refusal header,
// destination_rows and destination_count are unchanged.
[[nodiscard]] ArchiveCatalogError decode_archive_catalog(
    std::span<const std::byte> input, const VerifiedStaticIdentity& identity,
    ArchiveCatalogHeader& header, std::span<ArchiveCatalogRow> destination_rows,
    std::size_t& destination_count) noexcept;

} // namespace velocita::v2
