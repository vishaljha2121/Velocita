#include "velocita/v2/bound_metadata.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace velocita::v2 {
namespace {

void put32(std::span<std::byte> out, std::size_t offset, std::uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i) out[offset + i] = static_cast<std::byte>(value >> (8U * i));
}
void put64(std::span<std::byte> out, std::size_t offset, std::uint64_t value) noexcept {
    for (unsigned i = 0; i < 8; ++i) out[offset + i] = static_cast<std::byte>(value >> (8U * i));
}
std::uint32_t get32(std::span<const std::byte> in, std::size_t offset) noexcept {
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(in[offset + i])) << (8U * i);
    return value;
}
std::uint64_t get64(std::span<const std::byte> in, std::size_t offset) noexcept {
    std::uint64_t value{};
    for (unsigned i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(in[offset + i])) << (8U * i);
    return value;
}
bool configured_voter(const VerifiedStaticIdentity& identity, std::int32_t voter) noexcept {
    if (voter == -1) return true;
    if (voter <= 0) return false;
    const auto& value = identity.value();
    return std::find(value.voters.begin(), value.voters.begin() + value.voter_count, voter) !=
           value.voters.begin() + value.voter_count;
}
bool valid(const PersistentMetadata& value, const VerifiedStaticIdentity& identity) noexcept {
    return value.term >= 0 && value.commit_index >= 0 && configured_voter(identity, value.voted_for) &&
           (value.term != 0 || (value.voted_for == -1 && value.commit_index == 0));
}
BoundMetadataError io(const std::string& operation) {
    return {BoundMetadataErrorKind::io, BoundFrameError::none,
            operation + ": " + std::strerror(errno)};
}
void close_checked(int& fd, const char* operation) {
    const auto closing = std::exchange(fd, -1);
    if (::close(closing) != 0) throw io(operation);
}
void write_all(int fd, std::span<const std::byte> bytes) {
    std::size_t done{};
    while (done < bytes.size()) {
        const auto result = ::write(fd, bytes.data() + done, bytes.size() - done);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) throw io("write bound metadata");
        done += static_cast<std::size_t>(result);
    }
}
int open_directory(const std::filesystem::path& path) {
    int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = ::open(path.c_str(), flags);
    if (fd < 0) throw io("open metadata directory " + path.string());
    return fd;
}

} // namespace

BoundMetadataError::BoundMetadataError(BoundMetadataErrorKind kind,
                                       BoundFrameError frame_error,
                                       const std::string& message)
    : std::runtime_error(message), kind_(kind), frame_error_(frame_error) {}

BoundFrameEncodeResult encode_bound_metadata(
    const PersistentMetadata& value, const VerifiedStaticIdentity& identity,
    std::span<std::byte> output, BoundFrameScratch& scratch) noexcept {
    if (!identity.valid()) return {BoundFrameError::invalid_identity, 0};
    if (!valid(value, identity)) return {BoundFrameError::invalid_payload, 0};
    std::array<std::byte, 24> payload{};
    put64(payload, 0, static_cast<std::uint64_t>(value.term));
    put32(payload, 8, static_cast<std::uint32_t>(value.voted_for));
    put64(payload, 16, static_cast<std::uint64_t>(value.commit_index));
    return encode_bound_frame(BoundFrameKind::metadata, identity, payload, output, scratch);
}

BoundFrameError decode_bound_metadata(
    std::span<const std::byte> input, const VerifiedStaticIdentity& identity,
    PersistentMetadata& destination, BoundFrameScratch& scratch) noexcept {
    BoundFrameView view{};
    const auto error = decode_bound_frame(input, BoundFrameKind::metadata, identity, view, scratch);
    if (error != BoundFrameError::none) return error;
    PersistentMetadata candidate;
    candidate.term = static_cast<std::int64_t>(get64(view.payload, 0));
    candidate.voted_for = static_cast<std::int32_t>(get32(view.payload, 8));
    if (get32(view.payload, 12) != 0) return BoundFrameError::invalid_payload;
    candidate.commit_index = static_cast<std::int64_t>(get64(view.payload, 16));
    if (!valid(candidate, identity)) return BoundFrameError::invalid_payload;
    destination = candidate;
    return BoundFrameError::none;
}

PersistentMetadata read_bound_metadata_at(int directory_fd, const char* name,
                                          const VerifiedStaticIdentity& identity,
                                          bool resync) {
    if (directory_fd < 0 || name == nullptr || *name == '\0' || std::strchr(name, '/') != nullptr)
        throw BoundMetadataError{BoundMetadataErrorKind::io, BoundFrameError::none,
                                 "invalid bound metadata directory entry"};
    int flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = ::openat(directory_fd, name, flags);
    if (fd < 0) {
        if (errno == ENOENT)
            throw BoundMetadataError{BoundMetadataErrorKind::absent, BoundFrameError::none,
                                     std::string("bound metadata absent: ") + name};
        throw io(std::string("open bound metadata ") + name);
    }
    try {
        struct stat status {};
        if (::fstat(fd, &status) != 0) throw io("stat bound metadata");
        if (!S_ISREG(status.st_mode) || status.st_nlink != 1)
            throw BoundMetadataError{BoundMetadataErrorKind::io, BoundFrameError::none,
                                     "bound metadata is not a singly-linked regular file"};
        std::array<std::byte, kBoundMetadataBytes + 1> bytes{};
        std::size_t used{};
        while (used < bytes.size()) {
            const auto result = ::read(fd, bytes.data() + used, bytes.size() - used);
            if (result < 0 && errno == EINTR) continue;
            if (result < 0) throw io("read bound metadata");
            if (result == 0) break;
            used += static_cast<std::size_t>(result);
        }
        PersistentMetadata result{};
        BoundFrameScratch scratch{};
        const auto frame_error = decode_bound_metadata(std::span{bytes}.first(used), identity,
                                                       result, scratch);
        if (frame_error != BoundFrameError::none)
            throw BoundMetadataError{BoundMetadataErrorKind::frame, frame_error,
                                     "invalid bound metadata frame"};
        if (resync && ::fsync(fd) != 0) throw io("fsync bound metadata");
        const int closing = std::exchange(fd, -1);
        if (::close(closing) != 0) throw io("close bound metadata");
        return result;
    } catch (...) {
        if (fd >= 0) ::close(fd);
        throw;
    }
}

BoundMetadataStore::BoundMetadataStore(std::filesystem::path data_dir,
                                       VerifiedStaticIdentity identity,
                                       MetadataCheckpoints checkpoints)
    : directory_(std::move(data_dir)), path_(directory_ / kV2MetadataFileName),
      identity_(identity), checkpoints_(checkpoints) {}

std::filesystem::path BoundMetadataStore::temporary_path() const {
    return directory_ / (std::string{kV2MetadataFileName} + ".tmp");
}

PersistentMetadata BoundMetadataStore::load() const {
    int directory_fd = open_directory(directory_);
    try {
        const auto result = read_bound_metadata_at(directory_fd, kV2MetadataFileName, identity_);
        struct stat temporary {};
        const std::string temporary_name = std::string{kV2MetadataFileName} + ".tmp";
        if (::fstatat(directory_fd, temporary_name.c_str(), &temporary, AT_SYMLINK_NOFOLLOW) == 0) {
            try {
                (void)read_bound_metadata_at(directory_fd, temporary_name.c_str(), identity_);
            } catch (const BoundMetadataError& error) {
                throw BoundMetadataError{BoundMetadataErrorKind::invalid_temp_lineage,
                                         error.frame_error(),
                                         "invalid bound metadata temporary lineage"};
            }
        } else if (errno != ENOENT) {
            throw io("stat bound metadata temporary");
        }
        close_checked(directory_fd, "close metadata directory");
        return result;
    } catch (...) {
        if (directory_fd >= 0) ::close(directory_fd);
        throw;
    }
}

void BoundMetadataStore::save(const PersistentMetadata& value) const {
    std::array<std::byte, kBoundMetadataBytes> bytes{};
    BoundFrameScratch scratch{};
    const auto encoded = encode_bound_metadata(value, identity_, bytes, scratch);
    if (encoded.error != BoundFrameError::none)
        throw BoundMetadataError{BoundMetadataErrorKind::frame, encoded.error,
                                 "invalid bound metadata value"};
    int directory_fd = open_directory(directory_);
    const std::string temporary_name = std::string{kV2MetadataFileName} + ".tmp";
    int fd = -1;
    try {
        // Runtime replacement is never permission to recreate lost or foreign
        // voting memory. Genesis publication is an explicit startup operation.
        (void)read_bound_metadata_at(directory_fd, kV2MetadataFileName, identity_);
        struct stat before {};
        const bool temporary_exists =
            ::fstatat(directory_fd, temporary_name.c_str(), &before, AT_SYMLINK_NOFOLLOW) == 0;
        if (!temporary_exists && errno != ENOENT) throw io("stat bound metadata temporary");
        if (temporary_exists) {
            if (!S_ISREG(before.st_mode))
                throw BoundMetadataError{BoundMetadataErrorKind::invalid_temp_lineage,
                                         BoundFrameError::none,
                                         "bound metadata temporary is not regular"};
            try {
                (void)read_bound_metadata_at(directory_fd, temporary_name.c_str(), identity_);
            } catch (const BoundMetadataError& error) {
                throw BoundMetadataError{BoundMetadataErrorKind::invalid_temp_lineage,
                                         error.frame_error(),
                                         "invalid bound metadata temporary lineage"};
            }
        }
        checkpoints_(MetadataCheckpoint::before_write);
        int flags = O_WRONLY | O_CLOEXEC;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        if (!temporary_exists) flags |= O_CREAT | O_EXCL;
        fd = ::openat(directory_fd, temporary_name.c_str(), flags, 0644);
        if (fd < 0) throw io("checked open bound metadata temporary");
        if (temporary_exists) {
            struct stat opened {};
            if (::fstat(fd, &opened) != 0) throw io("stat opened bound metadata temporary");
            if (opened.st_dev != before.st_dev || opened.st_ino != before.st_ino)
                throw BoundMetadataError{BoundMetadataErrorKind::invalid_temp_lineage,
                                         BoundFrameError::none,
                                         "bound metadata temporary changed during checked open"};
            if (::ftruncate(fd, 0) != 0) throw io("truncate bound metadata temporary");
        }
        if (checkpoints_.observe) {
            const auto midpoint = bytes.size() / 2;
            write_all(fd, std::span<const std::byte>{bytes}.first(midpoint));
            checkpoints_(MetadataCheckpoint::partial_write);
            write_all(fd, std::span<const std::byte>{bytes}.subspan(midpoint));
        } else {
            write_all(fd, bytes);
        }
        checkpoints_(MetadataCheckpoint::file_written);
        if (::fsync(fd) != 0) throw io("fsync bound metadata temporary");
        checkpoints_(MetadataCheckpoint::file_synced);
        close_checked(fd, "close bound metadata temporary");
        fd = -1;
        if (::renameat(directory_fd, temporary_name.c_str(), directory_fd,
                       kV2MetadataFileName) != 0)
            throw io("rename bound metadata");
        checkpoints_(MetadataCheckpoint::file_renamed);
        if (::fsync(directory_fd) != 0) throw io("fsync metadata directory");
        checkpoints_(MetadataCheckpoint::directory_synced);
        close_checked(directory_fd, "close metadata directory");
    } catch (...) {
        if (fd >= 0) ::close(fd);
        if (directory_fd >= 0) ::close(directory_fd);
        throw;
    }
}

} // namespace velocita::v2
