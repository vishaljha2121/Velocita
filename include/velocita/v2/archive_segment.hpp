#pragma once

#include "velocita/v2/authority_barrier.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/kernel_codec.hpp"
#include "velocita/v2/static_identity.hpp"

#include <cstddef>
#include <cstdint>
#include <compare>
#include <span>

namespace velocita::v2 {

inline constexpr std::uint16_t kArchiveSegmentSchemaVersion = 1;
inline constexpr std::size_t kArchiveSegmentHeaderBytes = 96;
inline constexpr std::size_t kArchiveRecordHeaderBytes = 32;
inline constexpr std::size_t kArchiveDigestBytes = 32;
inline constexpr std::size_t kMaxArchiveSegmentBytes = 4U * 1024U * 1024U;
inline constexpr std::size_t kMaxArchiveRecords = 8192;
inline constexpr std::size_t kMaxArchiveRecordBytes =
    kArchiveRecordHeaderBytes + kMaxKernelCommandBytes + kArchiveDigestBytes;

enum class ArchiveSegmentError : std::uint8_t {
    none,
    output_too_small,
    truncated,
    invalid_magic,
    unsupported_schema,
    length_mismatch,
    capacity_exceeded,
    nonzero_reserved,
    invalid_identity,
    foreign_manifest,
    digest_mismatch,
    invalid_coordinates,
    invalid_entry_type,
    invalid_payload,
    source_read_failed,
    visitor_refused,
};

struct ArchiveRecordView {
    std::uint64_t index{};
    std::int64_t term{};
    std::int32_t entry_id{};
    std::int32_t entry_type{};
    std::span<const std::byte> payload;
};

// record_at may return a borrowed payload valid until its next invocation. The
// source must return identical records across both encoder passes.
struct ArchiveRecordSource {
    void* context{};
    std::uint32_t count{};
    bool (*record_at)(void*, std::uint32_t, ArchiveRecordView&) noexcept{};
};

struct ArchiveSegmentMetadata {
    std::uint64_t first_index{};
    std::uint64_t last_index{};
    std::int64_t predecessor_term{};
    std::int64_t first_term{};
    std::int64_t last_term{};
    std::uint32_t record_count{};
    std::uint32_t total_bytes{};
    Digest256 segment_digest{};
    constexpr auto operator<=>(const ArchiveSegmentMetadata&) const noexcept = default;
};

struct ArchiveSegmentScratch {
    KernelCommandCodecScratch command;
};

struct ArchiveSegmentEncodeResult {
    ArchiveSegmentError error{ArchiveSegmentError::none};
    std::size_t bytes_written{};
    Digest256 segment_digest{};
};

struct ArchiveSegmentDecodeResult {
    ArchiveSegmentError error{ArchiveSegmentError::none};
};

using ArchiveRecordVisitor = bool (*)(const ArchiveRecordView&, void*) noexcept;

// OUTPUT must not overlap any source payload. Prevalidation and output-capacity
// failures leave it unchanged. A source failure during pass two may leave a
// partial, unpublishable output; this codec is not an atomic publication API.
[[nodiscard]] ArchiveSegmentEncodeResult encode_archive_segment(
    const ArchiveRecordSource& source, std::int64_t predecessor_term,
    const VerifiedStaticIdentity& identity, std::span<std::byte> output,
    ArchiveSegmentScratch& scratch) noexcept;

// INPUT is immutable for both passes. Visitor payload spans borrow INPUT only
// for the synchronous callback and must not escape. Callback side effects are
// not rolled back if a later callback refuses.
[[nodiscard]] ArchiveSegmentDecodeResult decode_archive_segment(
    std::span<const std::byte> input, const VerifiedStaticIdentity& identity,
    ArchiveRecordVisitor visitor, void* visitor_context,
    ArchiveSegmentScratch& scratch, ArchiveSegmentMetadata& destination) noexcept;

} // namespace velocita::v2
