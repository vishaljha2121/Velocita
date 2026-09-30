#pragma once

#include "velocita/v2/archive_segment.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace velocita::v2 {

enum class ArchiveFileError : std::uint8_t {
    none,
    invalid_argument,
    open_failed,
    stat_failed,
    not_regular_single_link,
    length_out_of_range,
    buffer_too_small,
    read_failed,
    unexpected_eof,
    file_changed,
    codec_failed,
};

struct ArchiveFileReadResult {
    ArchiveFileError error{ArchiveFileError::none};
    ArchiveSegmentError codec_error{ArchiveSegmentError::none};
    std::size_t bytes_read{};
};

// Read-only verification of one caller-selected CP-028 object. `directory_fd`
// and `basename` refer to an exclusively owned, trusted archive directory; this
// function is not a defense against a malicious concurrent equal-size writer.
// It publishes no coverage, durability, backup, commitment, or deletion proof.
// BUFFER is caller-owned and mutable; only an actual file length at or below
// the approved per-object cap is ever read. BUFFER, SCRATCH, and DESTINATION
// must not alias. Buffer contents are unspecified on failure and must be
// discarded; DESTINATION remains unchanged. DIRECTORY_FD remains caller-owned.
// The function itself performs no allocation, but this is not a whole-process
// or caller-buffer memory claim. Success verifies the opened descriptor's
// content; it does not prove BASENAME still names that inode afterward. A later
// publication/coverage owner must retain or revalidate that binding under its
// exclusive directory protocol.
[[nodiscard]] ArchiveFileReadResult read_archive_segment_at(
    int directory_fd, std::string_view basename,
    const VerifiedStaticIdentity& expected_identity,
    std::span<std::byte> buffer, ArchiveSegmentScratch& scratch,
    ArchiveSegmentMetadata& destination) noexcept;

} // namespace velocita::v2
