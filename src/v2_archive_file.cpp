#include "velocita/v2/archive_file.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace velocita::v2 {
namespace {

bool valid_basename(const std::string_view name) noexcept {
    return !name.empty() && name != "." && name != ".." &&
        name.size() <= NAME_MAX && name.find('/') == std::string_view::npos &&
        name.find('\0') == std::string_view::npos;
}

ArchiveFileReadResult fail(const ArchiveFileError error,
                           const ArchiveSegmentError codec = ArchiveSegmentError::none,
                           const std::size_t bytes = 0) noexcept {
    return {error, codec, bytes};
}

int stat_retry(const int fd, struct stat& value) noexcept {
    int result;
    do {
        result = ::fstat(fd, &value);
    } while (result != 0 && errno == EINTR);
    return result;
}

} // namespace

ArchiveFileReadResult read_archive_segment_at(
    const int directory_fd, const std::string_view basename,
    const VerifiedStaticIdentity& expected_identity,
    const std::span<std::byte> buffer, ArchiveSegmentScratch& scratch,
    ArchiveSegmentMetadata& destination) noexcept {
    if (directory_fd < 0 || !valid_basename(basename) || buffer.empty())
        return fail(ArchiveFileError::invalid_argument);

    std::array<char, NAME_MAX + 1> name{};
    std::copy(basename.begin(), basename.end(), name.begin());
    int fd;
    do {
        fd = ::openat(directory_fd, name.data(),
                      O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) return fail(ArchiveFileError::open_failed);

    struct stat before {};
    if (stat_retry(fd, before) != 0) {
        (void)::close(fd);
        return fail(ArchiveFileError::stat_failed);
    }
    if (!S_ISREG(before.st_mode) || before.st_nlink != 1) {
        (void)::close(fd);
        return fail(ArchiveFileError::not_regular_single_link);
    }
    if (before.st_size <= 0 ||
        static_cast<std::uintmax_t>(before.st_size) > kMaxArchiveSegmentBytes) {
        (void)::close(fd);
        return fail(ArchiveFileError::length_out_of_range);
    }
    const auto length = static_cast<std::size_t>(before.st_size);
    if (buffer.size() < length) {
        (void)::close(fd);
        return fail(ArchiveFileError::buffer_too_small);
    }

    std::size_t offset = 0;
    while (offset < length) {
        const auto count = ::read(fd, buffer.data() + offset, length - offset);
        if (count < 0) {
            if (errno == EINTR) continue;
            (void)::close(fd);
            return fail(ArchiveFileError::read_failed, ArchiveSegmentError::none, offset);
        }
        if (count == 0) {
            (void)::close(fd);
            return fail(ArchiveFileError::unexpected_eof, ArchiveSegmentError::none, offset);
        }
        offset += static_cast<std::size_t>(count);
    }

    std::byte extra{};
    for (;;) {
        const auto count = ::read(fd, &extra, 1);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            (void)::close(fd);
            return fail(ArchiveFileError::read_failed, ArchiveSegmentError::none, offset);
        }
        if (count != 0) {
            (void)::close(fd);
            return fail(ArchiveFileError::file_changed, ArchiveSegmentError::none, offset);
        }
        break;
    }

    struct stat after {};
    const bool stat_ok = stat_retry(fd, after) == 0;
    const bool unchanged = stat_ok && S_ISREG(after.st_mode) && after.st_nlink == 1 &&
        before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
        before.st_size == after.st_size;
    if (::close(fd) != 0) return fail(ArchiveFileError::read_failed,
                                      ArchiveSegmentError::none, offset);
    if (!stat_ok) return fail(ArchiveFileError::stat_failed,
                              ArchiveSegmentError::none, offset);
    if (!unchanged) return fail(ArchiveFileError::file_changed,
                                ArchiveSegmentError::none, offset);

    ArchiveSegmentMetadata decoded;
    const auto codec = decode_archive_segment(buffer.first(length), expected_identity,
                                              nullptr, nullptr, scratch, decoded);
    if (codec.error != ArchiveSegmentError::none)
        return fail(ArchiveFileError::codec_failed, codec.error, offset);
    destination = decoded;
    return {ArchiveFileError::none, ArchiveSegmentError::none, offset};
}

} // namespace velocita::v2
