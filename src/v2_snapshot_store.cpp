#include "velocita/v2/snapshot_store.hpp"

#include "velocita/storage.hpp"

#include <array>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <span>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace velocita::v2 {
namespace {

// "VSN2". A distinct magic from V1's WAL1/MTA1 so a V1 file can never be read as
// a V2 snapshot, and the version is separate from the CANONICAL state schema
// carried inside the payload - the framing and the economics version
// independently.
constexpr std::uint32_t kSnapshotMagic = 0x324E5356U;
constexpr std::uint16_t kSnapshotFrameVersion = 1;
constexpr std::size_t kHeaderBytes = 34;

void append_le64(std::vector<std::byte>& out, const std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::byte>((value >> (i * 8U)) & 0xFFU));
    }
}

std::uint64_t read_le64(const std::vector<std::byte>& bytes, std::size_t& offset) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(
                     static_cast<unsigned char>(bytes[offset + i]))
                 << (i * 8U);
    }
    offset += 8;
    return value;
}

}  // namespace

SnapshotStore::SnapshotStore(std::filesystem::path data_dir, StorageCheckpoints checkpoints)
    : checkpoints_(checkpoints), path_(std::move(data_dir) / "v2-snapshot.bin") {}

void SnapshotStore::save(const TradingState& state,
                         const std::uint64_t last_included_index,
                         const std::int64_t last_included_term) const {
    std::array<std::byte, kMaxStateSnapshotBytes> payload{};
    const auto encoded = encode_state_snapshot(state, payload);
    if (encoded.error != StateSnapshotError::none) {
        throw std::runtime_error("refusing to persist a state the canonical "
                                 "encoder rejects");
    }

    std::vector<std::byte> bytes;
    bytes.reserve(kHeaderBytes + encoded.bytes_written + 4);
    for (std::size_t i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::byte>((kSnapshotMagic >> (i * 8U)) & 0xFFU));
    }
    bytes.push_back(static_cast<std::byte>(kSnapshotFrameVersion & 0xFFU));
    bytes.push_back(static_cast<std::byte>((kSnapshotFrameVersion >> 8U) & 0xFFU));
    append_le64(bytes, last_included_index);
    append_le64(bytes, static_cast<std::uint64_t>(last_included_term));
    append_le64(bytes, encoded.bytes_written);
    bytes.push_back(std::byte{0});
    bytes.push_back(std::byte{0});
    bytes.push_back(std::byte{0});
    bytes.push_back(std::byte{0});
    bytes.insert(bytes.end(), payload.begin(),
                 payload.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written));
    const auto checksum = crc32(bytes);
    for (std::size_t i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::byte>((checksum >> (i * 8U)) & 0xFFU));
    }

    const auto temporary = path_.string() + ".tmp";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) throw std::runtime_error("cannot open " + temporary);
    try {
        std::size_t written = 0;
        while (written < bytes.size()) {
            const auto result =
                ::write(fd, bytes.data() + written, bytes.size() - written);
            if (result <= 0) throw std::runtime_error("snapshot write failed");
            written += static_cast<std::size_t>(result);
        }
        if (::fsync(fd) != 0) throw std::runtime_error("snapshot fsync failed");
        if (::close(fd) != 0) throw std::runtime_error("snapshot close failed");
    } catch (...) {
        ::close(fd);
        throw;
    }
    if (::rename(temporary.c_str(), path_.c_str()) != 0) {
        throw std::runtime_error("snapshot rename failed");
    }
    checkpoints_(StorageCheckpoint::file_renamed);
    // The rename is only durable once the DIRECTORY is synced. Without this a
    // crash can leave the old snapshot visible after `save` returned, and the
    // caller has already reclaimed the log prefix on the strength of it.
    const int dir = ::open(path_.parent_path().c_str(), O_RDONLY);
    if (dir < 0) throw std::runtime_error("snapshot directory open failed");
    const int synced = ::fsync(dir);
    ::close(dir);
    if (synced != 0) throw std::runtime_error("snapshot directory fsync failed");
    checkpoints_(StorageCheckpoint::directory_synced);
}

std::optional<StoredSnapshot> SnapshotStore::load() const {
    std::ifstream input(path_, std::ios::binary);
    if (!input) return std::nullopt;
    const std::string raw((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(raw[i]));
    }
    if (bytes.size() < kHeaderBytes + 4) return std::nullopt;

    std::size_t offset = 0;
    std::uint32_t magic = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        magic |= static_cast<std::uint32_t>(
                     static_cast<unsigned char>(bytes[offset + i]))
                 << (i * 8U);
    }
    offset += 4;
    if (magic != kSnapshotMagic) return std::nullopt;
    const auto frame_version =
        static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset]) |
                                   (static_cast<unsigned char>(bytes[offset + 1]) << 8U));
    offset += 2;
    if (frame_version != kSnapshotFrameVersion) return std::nullopt;

    StoredSnapshot result;
    result.last_included_index = read_le64(bytes, offset);
    result.last_included_term = static_cast<std::int64_t>(read_le64(bytes, offset));
    const auto payload_bytes = read_le64(bytes, offset);
    offset += 4;
    if (payload_bytes > bytes.size() || offset + payload_bytes + 4 != bytes.size()) {
        return std::nullopt;
    }

    std::uint32_t stored = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        stored |= static_cast<std::uint32_t>(static_cast<unsigned char>(
                      bytes[bytes.size() - 4 + i]))
                  << (i * 8U);
    }
    const std::span<const std::byte> covered(bytes.data(), bytes.size() - 4);
    if (stored != crc32(covered)) return std::nullopt;

    StateSnapshotScratch scratch;
    const std::span<const std::byte> payload(bytes.data() + offset, payload_bytes);
    const auto decoded = decode_state_snapshot(payload, result.state, scratch);
    if (decoded.error != StateSnapshotError::none) return std::nullopt;
    result.state_digest = decoded.state_digest;
    return result;
}

}  // namespace velocita::v2
