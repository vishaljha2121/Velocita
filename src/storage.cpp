#include "velocita/storage.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <unistd.h>

namespace velocita {
namespace {

constexpr std::uint32_t kMetadataMagic = 0x3141544DU;  // MTA1
constexpr std::uint32_t kWalMagic = 0x314C4157U;       // WAL1
constexpr std::uint16_t kStorageVersion = 1;
constexpr std::uint16_t kWalHeaderSize = 44;
constexpr std::size_t kMaxPayload = 1024 * 1024;

template <typename T>
void append_le(std::vector<std::byte>& out, T value) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    const U bits = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<std::byte>((bits >> (i * 8U)) & 0xFFU));
    }
}

template <typename T>
T read_le(std::span<const std::byte> bytes, std::size_t& offset) {
    static_assert(std::is_integral_v<T>);
    if (offset + sizeof(T) > bytes.size()) {
        throw std::runtime_error("truncated storage record");
    }
    using U = std::make_unsigned_t<T>;
    U value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<U>(std::to_integer<unsigned char>(bytes[offset + i])) << (i * 8U);
    }
    offset += sizeof(T);
    return static_cast<T>(value);
}

std::runtime_error io_error(const std::string& operation, const std::filesystem::path& path) {
    return std::runtime_error(operation + " " + path.string() + ": " + std::strerror(errno));
}

void write_all(int fd, std::span<const std::byte> bytes, const std::filesystem::path& path) {
    std::size_t written = 0;
    while (written < bytes.size()) {
        const auto result = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw io_error("write", path);
        }
        written += static_cast<std::size_t>(result);
    }
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return {};
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("open " + path.string() + " failed");
    }
    const auto size = input.tellg();
    if (size < 0) {
        throw std::runtime_error("size " + path.string() + " failed");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) {
        throw std::runtime_error("read " + path.string() + " failed");
    }
    return bytes;
}

void fsync_directory(const std::filesystem::path& directory) {
#ifdef O_DIRECTORY
    const int fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY);
#else
    const int fd = ::open(directory.c_str(), O_RDONLY);
#endif
    if (fd < 0) {
        throw io_error("open directory", directory);
    }
    if (::fsync(fd) != 0) {
        const auto error = io_error("fsync directory", directory);
        ::close(fd);
        throw error;
    }
    ::close(fd);
}

}  // namespace

std::uint32_t crc32(std::span<const std::byte> bytes) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

MetadataStore::MetadataStore(std::filesystem::path data_dir) : path_(std::move(data_dir) / "metadata.bin") {
    std::filesystem::create_directories(path_.parent_path());
}

PersistentMetadata MetadataStore::load() const {
    const auto bytes = read_file(path_);
    if (bytes.empty()) {
        return {};
    }
    if (bytes.size() != 36) {
        throw std::runtime_error("metadata has invalid length: " + path_.string());
    }
    std::size_t offset = 0;
    if (read_le<std::uint32_t>(bytes, offset) != kMetadataMagic ||
        read_le<std::uint16_t>(bytes, offset) != kStorageVersion) {
        throw std::runtime_error("metadata magic/version mismatch: " + path_.string());
    }
    (void)read_le<std::uint16_t>(bytes, offset);
    PersistentMetadata metadata;
    metadata.term = read_le<std::int64_t>(bytes, offset);
    metadata.voted_for = read_le<std::int32_t>(bytes, offset);
    (void)read_le<std::int32_t>(bytes, offset);
    metadata.commit_index = read_le<std::int64_t>(bytes, offset);
    const auto stored_crc = read_le<std::uint32_t>(bytes, offset);
    if (stored_crc != crc32(std::span<const std::byte>(bytes).first(bytes.size() - sizeof(stored_crc)))) {
        throw std::runtime_error("metadata checksum mismatch: " + path_.string());
    }
    return metadata;
}

void MetadataStore::save(const PersistentMetadata& metadata) const {
    std::vector<std::byte> bytes;
    bytes.reserve(36);
    append_le(bytes, kMetadataMagic);
    append_le(bytes, kStorageVersion);
    append_le<std::uint16_t>(bytes, 0);
    append_le(bytes, metadata.term);
    append_le(bytes, metadata.voted_for);
    append_le<std::int32_t>(bytes, 0);
    append_le(bytes, metadata.commit_index);
    append_le(bytes, crc32(bytes));

    const auto temporary = path_.string() + ".tmp";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        throw io_error("open", temporary);
    }
    try {
        write_all(fd, bytes, temporary);
        if (::fsync(fd) != 0) {
            throw io_error("fsync", temporary);
        }
        if (::close(fd) != 0) {
            throw io_error("close", temporary);
        }
    } catch (...) {
        ::close(fd);
        throw;
    }
    if (::rename(temporary.c_str(), path_.c_str()) != 0) {
        throw io_error("rename", path_);
    }
    fsync_directory(path_.parent_path());
}

WriteAheadLog::WriteAheadLog(std::filesystem::path data_dir) : path_(std::move(data_dir) / "raft.wal") {
    std::filesystem::create_directories(path_.parent_path());
}

void WriteAheadLog::append(const WalRecord& record) {
    if (record.payload.size() > kMaxPayload) {
        throw std::runtime_error("WAL payload exceeds 1 MiB limit");
    }
    std::vector<std::byte> bytes;
    bytes.reserve(kWalHeaderSize + record.payload.size());
    append_le(bytes, kWalMagic);
    append_le(bytes, kStorageVersion);
    append_le(bytes, kWalHeaderSize);
    append_le(bytes, static_cast<std::uint32_t>(record.payload.size()));
    append_le<std::uint32_t>(bytes, 0);
    append_le(bytes, record.index);
    append_le(bytes, record.term);
    append_le(bytes, record.entry_id);
    append_le(bytes, record.entry_type);
    const auto crc_offset = bytes.size();
    append_le<std::uint32_t>(bytes, 0);
    bytes.insert(bytes.end(), record.payload.begin(), record.payload.end());
    const auto checksum = crc32(bytes);
    for (std::size_t i = 0; i < sizeof(checksum); ++i) {
        bytes[crc_offset + i] = static_cast<std::byte>((checksum >> (i * 8U)) & 0xFFU);
    }

    const int fd = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        throw io_error("open", path_);
    }
    try {
        write_all(fd, bytes, path_);
        if (::fsync(fd) != 0) {
            throw io_error("fsync", path_);
        }
        if (::close(fd) != 0) {
            throw io_error("close", path_);
        }
    } catch (...) {
        ::close(fd);
        throw;
    }
}

// One encoder for every path that writes a record - the two file rewrites and
// block staging - so they cannot drift apart about the format.
std::vector<std::byte> encode_wal_record(const WalRecord& record) {
    std::vector<std::byte> bytes;
    bytes.reserve(kWalHeaderSize + record.payload.size());
    append_le(bytes, kWalMagic);
    append_le(bytes, kStorageVersion);
    append_le(bytes, kWalHeaderSize);
    append_le(bytes, static_cast<std::uint32_t>(record.payload.size()));
    append_le<std::uint32_t>(bytes, 0);
    append_le(bytes, record.index);
    append_le(bytes, record.term);
    append_le(bytes, record.entry_id);
    append_le(bytes, record.entry_type);
    const auto crc_offset = bytes.size();
    append_le<std::uint32_t>(bytes, 0);
    bytes.insert(bytes.end(), record.payload.begin(), record.payload.end());
    const auto checksum = crc32(bytes);
    for (std::size_t byte = 0; byte < sizeof(checksum); ++byte) {
        bytes[crc_offset + byte] =
            static_cast<std::byte>((checksum >> (byte * 8U)) & 0xFFU);
    }
    return bytes;
}

std::vector<WalRecord> decode_wal_records(const std::span<const std::byte> bytes) {
    std::vector<WalRecord> records;
    std::size_t position = 0;
    while (position + kWalHeaderSize <= bytes.size()) {
        std::size_t offset = position;
        if (read_le<std::uint32_t>(bytes, offset) != kWalMagic) break;
        if (read_le<std::uint16_t>(bytes, offset) != kStorageVersion) break;
        if (read_le<std::uint16_t>(bytes, offset) != kWalHeaderSize) break;
        const auto payload_size = read_le<std::uint32_t>(bytes, offset);
        (void)read_le<std::uint32_t>(bytes, offset);
        if (payload_size > kMaxPayload) break;
        const auto total = kWalHeaderSize + payload_size;
        if (position + total > bytes.size()) break;

        WalRecord record;
        record.index = read_le<std::uint64_t>(bytes, offset);
        record.term = read_le<std::int64_t>(bytes, offset);
        record.entry_id = read_le<std::int32_t>(bytes, offset);
        record.entry_type = read_le<std::int32_t>(bytes, offset);
        const auto stored_crc = read_le<std::uint32_t>(bytes, offset);

        std::vector<std::byte> covered(
            bytes.begin() + static_cast<std::ptrdiff_t>(position),
            bytes.begin() + static_cast<std::ptrdiff_t>(position + total));
        const auto crc_position = kWalHeaderSize - sizeof(std::uint32_t);
        for (std::size_t i = 0; i < sizeof(std::uint32_t); ++i) {
            covered[crc_position + i] = std::byte{0};
        }
        if (crc32(covered) != stored_crc) break;

        record.payload.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(position + kWalHeaderSize),
            bytes.begin() + static_cast<std::ptrdiff_t>(position + total));
        records.push_back(std::move(record));
        position += total;
    }
    return records;
}

// Rewrite the log to exactly `kept`, atomically. Temp file, fsync, rename,
// fsync the directory: a crash at any point leaves either the old log or the
// new one, never a partial one.
void WriteAheadLog::rewrite(const std::vector<WalRecord>& kept) const {
    const auto temporary = path_.string() + ".tmp";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) throw io_error("open", temporary);
    try {
        for (const auto& record : kept) {
            write_all(fd, encode_wal_record(record), temporary);
        }
        if (::fsync(fd) != 0) throw io_error("fsync", temporary);
        if (::close(fd) != 0) throw io_error("close", temporary);
    } catch (...) {
        ::close(fd);
        throw;
    }
    if (::rename(temporary.c_str(), path_.c_str()) != 0) throw io_error("rename", path_);
    fsync_directory(path_.parent_path());
}

void WriteAheadLog::truncate_from(std::uint64_t first_removed_index) {
    auto records = read_all();
    if (first_removed_index == 0) {
        throw std::runtime_error("WAL truncation index is outside the log");
    }
    // Selected by the record's OWN index rather than by position. Position and
    // index coincide only while no prefix has been reclaimed, and compaction
    // ends that.
    std::vector<WalRecord> kept;
    for (auto& record : records) {
        if (record.index < first_removed_index) kept.push_back(std::move(record));
    }
    if (kept.size() == records.size()) return;
    rewrite(kept);
}

void WriteAheadLog::discard_through(const std::uint64_t last_removed_index) {
    if (last_removed_index == 0) return;
    auto records = read_all();
    std::vector<WalRecord> kept;
    for (auto& record : records) {
        if (record.index > last_removed_index) kept.push_back(std::move(record));
    }
    if (kept.size() == records.size()) return;
    rewrite(kept);
}

std::vector<WalRecord> WriteAheadLog::read_all() const {
    const auto bytes = read_file(path_);
    std::vector<WalRecord> records;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto record_start = offset;
        if (bytes.size() - offset < kWalHeaderSize) {
            throw std::runtime_error("WAL ends with a partial header: " + path_.string());
        }
        if (read_le<std::uint32_t>(bytes, offset) != kWalMagic ||
            read_le<std::uint16_t>(bytes, offset) != kStorageVersion ||
            read_le<std::uint16_t>(bytes, offset) != kWalHeaderSize) {
            throw std::runtime_error("WAL magic/version/header mismatch at offset " + std::to_string(record_start));
        }
        const auto payload_size = read_le<std::uint32_t>(bytes, offset);
        (void)read_le<std::uint32_t>(bytes, offset);
        if (payload_size > kMaxPayload || offset + 8 + 8 + 4 + 4 + 4 + payload_size > bytes.size()) {
            throw std::runtime_error("WAL has invalid payload length at offset " + std::to_string(record_start));
        }
        WalRecord record;
        record.index = read_le<std::uint64_t>(bytes, offset);
        record.term = read_le<std::int64_t>(bytes, offset);
        record.entry_id = read_le<std::int32_t>(bytes, offset);
        record.entry_type = read_le<std::int32_t>(bytes, offset);
        const auto stored_crc = read_le<std::uint32_t>(bytes, offset);
        record.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                              bytes.begin() + static_cast<std::ptrdiff_t>(offset + payload_size));
        offset += payload_size;

        auto checksum_bytes = std::vector<std::byte>(bytes.begin() + static_cast<std::ptrdiff_t>(record_start),
                                                     bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        const auto crc_position = kWalHeaderSize - sizeof(std::uint32_t);
        for (std::size_t i = 0; i < sizeof(std::uint32_t); ++i) {
            checksum_bytes[crc_position + i] = std::byte{0};
        }
        if (stored_crc != crc32(checksum_bytes)) {
            throw std::runtime_error("WAL checksum mismatch at offset " + std::to_string(record_start));
        }
        // Contiguous WITHIN ITSELF, not necessarily starting at one. A log whose
        // prefix a snapshot has reclaimed legitimately begins above one, and
        // requiring index == position + 1 would make compaction unreadable.
        if (!records.empty() && record.index != records.back().index + 1U) {
            throw std::runtime_error("WAL index is not contiguous at offset " + std::to_string(record_start));
        }
        records.push_back(std::move(record));
    }
    return records;
}

}  // namespace velocita
