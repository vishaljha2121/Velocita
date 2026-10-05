// Reads and verifies one archive segment file.
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

[[nodiscard]] ArchiveFileReadResult read_archive_segment_at(
    int directory_fd, std::string_view basename,
    const VerifiedStaticIdentity& expected_identity,
    std::span<std::byte> buffer, ArchiveSegmentScratch& scratch,
    ArchiveSegmentMetadata& destination) noexcept;

}
