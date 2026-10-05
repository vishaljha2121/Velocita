#include "velocita/v2/snapshot_store.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace velocita::v2 {
namespace {
constexpr std::size_t kCoordinatesBytes = 16;

void put64(std::span<std::byte> out, std::size_t at, std::uint64_t value) noexcept {
    for (std::size_t i = 0; i < 8; ++i)
        out[at + i] = static_cast<std::byte>(value >> (8U * i));
}
std::uint64_t get64(std::span<const std::byte> input, std::size_t at) noexcept {
    std::uint64_t value{};
    for (std::size_t i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(input[at + i]))
                 << (8U * i);
    return value;
}
[[noreturn]] void incompatible(BoundFrameError code, const char* message) {
    throw SnapshotStoreError(code, message);
}
bool optional_corruption(BoundFrameError error) noexcept {
    switch (error) {
        case BoundFrameError::truncated:
        case BoundFrameError::length_mismatch:
        case BoundFrameError::capacity_exceeded:
        case BoundFrameError::nonzero_reserved:
        case BoundFrameError::digest_mismatch:
        case BoundFrameError::invalid_payload:
            return true;
        default: return false;
    }
}
void write_all(int fd, std::span<const std::byte> bytes) {
    while (!bytes.empty()) {
        const auto count = ::write(fd, bytes.data(), bytes.size());
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("snapshot write failed");
        bytes = bytes.subspan(static_cast<std::size_t>(count));
    }
}
void close_checked(int& fd, const char* message) {
    const int closing = std::exchange(fd, -1);
    if (::close(closing) != 0) throw std::runtime_error(message);
}
}

SnapshotStore::SnapshotStore(std::filesystem::path data_dir,
                             const VerifiedStaticIdentity& identity,
                             StorageCheckpoints checkpoints)
    : identity_(identity), checkpoints_(checkpoints),
      path_(std::move(data_dir) / "v2-snapshot.bin") {
    if (!identity_.valid()) incompatible(BoundFrameError::invalid_identity,
                                         "snapshot store requires verified identity");
}

void SnapshotStore::save(const TradingState& state,
                         std::uint64_t last_included_index,
                         std::int64_t last_included_term) const {
    if (last_included_index == 0 ||
        last_included_index > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        last_included_term <= 0)
        incompatible(BoundFrameError::invalid_payload,
                     "snapshot coordinates must be positive and representable");

    struct SaveWorkspace {
        std::array<std::byte, kMaxStateSnapshotBytes> canonical{};
        std::array<std::byte, kCoordinatesBytes + kMaxStateSnapshotBytes> payload{};
        std::array<std::byte, kMaxBoundSnapshotBytes> frame{};
        BoundFrameScratch scratch{};
    };
    const auto workspace = std::make_unique<SaveWorkspace>();
    auto& [canonical, payload, frame, scratch] = *workspace;
    const auto encoded = encode_state_snapshot(state, canonical);
    if (encoded.error != StateSnapshotError::none)
        incompatible(BoundFrameError::invalid_payload,
                     "refusing to persist a state rejected by the canonical codec");

    put64(payload, 0, last_included_index);
    put64(payload, 8, static_cast<std::uint64_t>(last_included_term));
    std::copy(canonical.begin(), canonical.begin() +
                  static_cast<std::ptrdiff_t>(encoded.bytes_written),
              payload.begin() + static_cast<std::ptrdiff_t>(kCoordinatesBytes));

    const auto framed = encode_bound_frame(
        BoundFrameKind::snapshot, identity_,
        std::span<const std::byte>{payload}.first(kCoordinatesBytes + encoded.bytes_written),
        frame, scratch);
    if (framed.error != BoundFrameError::none)
        incompatible(framed.error, "snapshot bound-frame encoding failed");

    const auto temporary = path_.string() + ".tmp";
    struct stat named{};
    const bool existed = ::lstat(temporary.c_str(), &named) == 0;
    if (!existed && errno != ENOENT) throw std::runtime_error("snapshot temp stat failed");
    if (existed && (!S_ISREG(named.st_mode) || named.st_nlink != 1))
        throw std::runtime_error("snapshot temp is not a singly-linked regular file");
    int flags = O_WRONLY | O_CLOEXEC | O_NOFOLLOW;
    if (!existed) flags |= O_CREAT | O_EXCL;
    int fd = ::open(temporary.c_str(), flags, 0644);
    if (fd < 0) throw std::runtime_error("cannot open " + temporary);
    try {
        struct stat opened{};
        if (::fstat(fd, &opened) != 0 || !S_ISREG(opened.st_mode) || opened.st_nlink != 1 ||
            (existed && (opened.st_dev != named.st_dev || opened.st_ino != named.st_ino)))
            throw std::runtime_error("snapshot temp changed during checked open");
        if (existed && ::ftruncate(fd, 0) != 0)
            throw std::runtime_error("snapshot temp truncate failed");
        write_all(fd, std::span<const std::byte>{frame}.first(framed.bytes_written));
        if (::fsync(fd) != 0) throw std::runtime_error("snapshot fsync failed");
        close_checked(fd, "snapshot close failed");
    } catch (...) {
        if (fd >= 0) ::close(fd);
        throw;
    }
    if (::rename(temporary.c_str(), path_.c_str()) != 0)
        throw std::runtime_error("snapshot rename failed");
    checkpoints_(StorageCheckpoint::file_renamed);
    const int directory = ::open(path_.parent_path().c_str(), O_RDONLY);
    if (directory < 0) throw std::runtime_error("snapshot directory open failed");
    const int synced = ::fsync(directory);
    ::close(directory);
    if (synced != 0) throw std::runtime_error("snapshot directory fsync failed");
    checkpoints_(StorageCheckpoint::directory_synced);
}

std::optional<StoredSnapshot> SnapshotStore::load() const {
    struct LoadWorkspace {
        std::array<std::byte, kMaxBoundSnapshotBytes + 1> storage{};
        BoundFrameScratch scratch{};
        StateSnapshotScratch state_scratch{};
        StoredSnapshot result{};
    };
    const auto workspace = std::make_unique<LoadWorkspace>();
    auto& [storage, scratch, state_scratch, result] = *workspace;
    const int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
    int fd = ::open(path_.c_str(), flags);
    if (fd < 0) {
        if (errno == ENOENT) return std::nullopt;
        throw std::runtime_error("snapshot open failed");
    }
    std::size_t count{};
    try {
        struct stat status{};
        if (::fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_nlink != 1)
            throw std::runtime_error("snapshot is not a singly-linked regular file");
        while (count < storage.size()) {
            const auto got = ::read(fd, storage.data() + count, storage.size() - count);
            if (got < 0 && errno == EINTR) continue;
            if (got < 0) throw std::runtime_error("snapshot read failed");
            if (got == 0) break;
            count += static_cast<std::size_t>(got);
        }
        close_checked(fd, "snapshot close failed");
    } catch (...) {
        if (fd >= 0) ::close(fd);
        throw;
    }
    const auto bytes = std::span<const std::byte>{storage}.first(count);

    if (count < 4 || bytes[0] != std::byte{'V'} || bytes[1] != std::byte{'2'} ||
        bytes[2] != std::byte{'B'} || bytes[3] != std::byte{'F'})
        incompatible(BoundFrameError::invalid_magic,
                     "recovery refused: snapshot framing is legacy or unknown");

    BoundFrameView view{};
    const auto admitted = decode_bound_frame(bytes, BoundFrameKind::snapshot,
                                              identity_, view, scratch);
    if (admitted != BoundFrameError::none) {
        constexpr std::size_t canonical_offset = kBoundFrameHeaderBytes + kCoordinatesBytes;
        if (admitted == BoundFrameError::capacity_exceeded &&
            count > canonical_offset) {
            const auto probe = decode_state_snapshot(bytes.subspan(canonical_offset),
                                                     result.state, state_scratch);
            if (probe.error == StateSnapshotError::unsupported_schema)
                incompatible(BoundFrameError::unsupported_schema,
                             "recovery refused: unsupported snapshot state schema");
        }
        if (optional_corruption(admitted)) return std::nullopt;
        incompatible(admitted, "recovery refused: incompatible or foreign snapshot");
    }
    if (view.payload.size() < kCoordinatesBytes) return std::nullopt;

    const auto index = get64(view.payload, 0);
    const auto term_bits = get64(view.payload, 8);
    if (index == 0 || index > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        term_bits == 0 || term_bits > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;

    result.last_included_index = index;
    result.last_included_term = static_cast<std::int64_t>(term_bits);
    const auto decoded = decode_state_snapshot(view.payload.subspan(kCoordinatesBytes),
                                                result.state, state_scratch);
    if (decoded.error == StateSnapshotError::unsupported_schema)
        incompatible(BoundFrameError::unsupported_schema,
                     "recovery refused: unsupported snapshot state schema");
    if (decoded.error != StateSnapshotError::none) return std::nullopt;
    result.state_digest = decoded.state_digest;
    return result;
}
}
