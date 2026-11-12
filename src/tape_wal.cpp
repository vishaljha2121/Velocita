#include "velocita/tape/wal.hpp"

#include "velocita/storage.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace velocita::tape {
namespace {

constexpr std::uint32_t kRecordMagic = 0x4C415754u;
constexpr std::uint32_t kMetaMagic = 0x4154454Du;
constexpr std::uint8_t kEntryRecord = 1;
constexpr std::uint8_t kTruncateRecord = 2;
constexpr std::size_t kHeaderBytes = 4 + 1 + 8 + 8 + 4 + 4;
constexpr std::uint32_t kMaxRecordBytes = 64 * 1024;

void put(std::vector<std::byte>& out, std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) out.push_back(std::byte((v >> (8 * i)) & 0xFF));
}

std::uint64_t get(const std::byte* p, int n) {
    std::uint64_t v = 0;
    for (int i = 0; i < n; ++i) v |= std::uint64_t(std::to_integer<std::uint8_t>(p[i])) << (8 * i);
    return v;
}

void full_sync(int fd, const char* what) {
#ifdef __APPLE__
    if (::fcntl(fd, F_FULLFSYNC) == 0) return;
#endif
    if (::fsync(fd) != 0) throw StorageError(std::string("fsync failed: ") + what + ": " + std::strerror(errno));
}

void sync_directory(const std::filesystem::path& directory) {
    const int fd = ::open(directory.c_str(), O_RDONLY);
    if (fd < 0) throw StorageError("cannot open directory for sync");
    full_sync(fd, "directory");
    ::close(fd);
}

void write_all(int fd, const std::byte* p, std::size_t n) {
    while (n > 0) {
        const auto w = ::write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            throw StorageError(std::string("write failed: ") + std::strerror(errno));
        }
        p += w;
        n -= static_cast<std::size_t>(w);
    }
}

}

namespace {

std::vector<WalRecord> scan(std::span<const std::byte> raw, bool allow_torn_tail, std::size_t& valid) {
    std::vector<WalRecord> records;
    const auto* data = raw.data();
    std::size_t at = 0;
    while (at < raw.size()) {
        const bool header_fits = raw.size() - at >= kHeaderBytes;
        std::uint32_t length = 0;
        if (header_fits) length = static_cast<std::uint32_t>(get(data + at + 25, 4));
        const bool fits = header_fits && length <= kMaxRecordBytes && raw.size() - at >= kHeaderBytes + length + 4;
        if (!fits) {
            if (allow_torn_tail) break;
            throw StorageError("write-ahead log ends in a partial record");
        }
        const auto magic = static_cast<std::uint32_t>(get(data + at, 4));
        const auto crc = static_cast<std::uint32_t>(get(data + at + kHeaderBytes + length, 4));
        const auto computed = velocita::crc32(std::span(data + at, kHeaderBytes + length));
        if (magic != kRecordMagic || crc != computed) {
            const bool last = at + kHeaderBytes + length + 4 == raw.size();
            if (allow_torn_tail && last) break;
            throw StorageError("write-ahead log record fails its checksum at byte " + std::to_string(at));
        }
        const auto kind = std::to_integer<std::uint8_t>(data[at + 4]);
        const auto index = get(data + at + 5, 8);
        const auto term = static_cast<std::int64_t>(get(data + at + 13, 8));
        const auto type = static_cast<std::int32_t>(get(data + at + 21, 4));
        if (kind == kTruncateRecord) {
            while (!records.empty() && records.back().index >= index) records.pop_back();
        } else if (kind == kEntryRecord) {
            while (!records.empty() && records.back().index >= index) records.pop_back();
            if ((records.empty() && index != 1) || (!records.empty() && records.back().index + 1 != index))
                throw StorageError("write-ahead log index sequence is broken at index " + std::to_string(index));
            records.push_back({index, term, type, at + kHeaderBytes, length});
        } else {
            throw StorageError("write-ahead log has an unknown record kind");
        }
        at += kHeaderBytes + length + 4;
    }
    valid = at;
    return records;
}

std::vector<std::byte> read_all(const std::filesystem::path& path) {
    std::vector<std::byte> raw;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return raw;
    raw.resize(static_cast<std::size_t>(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    return raw;
}

}

std::vector<WalEntry> read_wal_file(const std::filesystem::path& path, bool allow_torn_tail) {
    const auto raw = read_all(path);
    std::size_t valid = 0;
    std::vector<WalEntry> entries;
    for (const auto& r : scan(raw, allow_torn_tail, valid))
        entries.push_back({r.index, r.term, r.type, std::vector<std::byte>(raw.begin() + static_cast<std::ptrdiff_t>(r.offset),
                                                                           raw.begin() + static_cast<std::ptrdiff_t>(r.offset + r.length))});
    return entries;
}

TapeWal::TapeWal(std::filesystem::path directory)
    : directory_(std::move(directory)), log_path_(directory_ / "wal.log") {
    std::filesystem::create_directories(directory_);
}

TapeWal::~TapeWal() {
    if (fd_ >= 0) ::close(fd_);
}

WalRecovery TapeWal::recover() {
    WalRecovery out;
    out.raw = read_all(log_path_);
    std::size_t valid = 0;
    out.records = scan(out.raw, true, valid);
    fd_ = ::open(log_path_.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    if (fd_ < 0) throw StorageError("cannot open the write-ahead log");
    if (::ftruncate(fd_, static_cast<off_t>(valid)) != 0) throw StorageError("cannot trim a torn tail");
    if (::lseek(fd_, 0, SEEK_END) < 0) throw StorageError("cannot seek the write-ahead log");
    full_sync(fd_, "log");
    sync_directory(directory_);
    bytes_ = valid;
    written_index_ = synced_index_ = out.records.empty() ? 0 : out.records.back().index;
    return out;
}

void TapeWal::write_record(std::uint8_t kind, std::uint64_t index, std::int64_t term, std::int32_t type,
                           std::span<const std::byte> bytes) {
    if (fd_ < 0) throw StorageError("write-ahead log is not open");
    if (bytes.size() > kMaxRecordBytes) throw StorageError("record exceeds the write-ahead log limit");
    std::vector<std::byte> record;
    record.reserve(kHeaderBytes + bytes.size() + 4);
    put(record, kRecordMagic, 4);
    record.push_back(std::byte{kind});
    put(record, index, 8);
    put(record, static_cast<std::uint64_t>(term), 8);
    put(record, static_cast<std::uint32_t>(type), 4);
    put(record, bytes.size(), 4);
    record.insert(record.end(), bytes.begin(), bytes.end());
    put(record, velocita::crc32(record), 4);
    write_all(fd_, record.data(), record.size());
    bytes_ += record.size();
    dirty_ = true;
}

void TapeWal::append(std::uint64_t index, std::int64_t term, std::int32_t type, std::span<const std::byte> bytes) {
    write_record(kEntryRecord, index, term, type, bytes);
    written_index_ = index;
}

void TapeWal::truncate_from(std::uint64_t index) {
    write_record(kTruncateRecord, index, 0, 0, {});
    written_index_ = index == 0 ? 0 : index - 1;
    if (synced_index_ > written_index_) synced_index_ = written_index_;
}

void TapeWal::sync() {
    if (!dirty_) return;
    full_sync(fd_, "log");
    dirty_ = false;
    synced_index_ = written_index_;
}

Metadata TapeWal::load_metadata() const {
    Metadata m;
    std::ifstream in(directory_ / "meta", std::ios::binary);
    if (!in) return m;
    std::array<char, 20> raw{};
    if (!in.read(raw.data(), raw.size())) throw StorageError("Raft metadata is truncated");
    const auto* d = reinterpret_cast<const std::byte*>(raw.data());
    if (get(d, 4) != kMetaMagic || get(d + 16, 4) != velocita::crc32(std::span(d, 16)))
        throw StorageError("Raft metadata fails its checksum");
    m.term = static_cast<std::int64_t>(get(d + 4, 8));
    m.voted_for = static_cast<std::int32_t>(get(d + 12, 4));
    return m;
}

void TapeWal::store_metadata(const Metadata& metadata) const {
    std::vector<std::byte> out;
    put(out, kMetaMagic, 4);
    put(out, static_cast<std::uint64_t>(metadata.term), 8);
    put(out, static_cast<std::uint32_t>(metadata.voted_for), 4);
    put(out, velocita::crc32(out), 4);
    const auto tmp = directory_ / "meta.tmp";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) throw StorageError("cannot write Raft metadata");
    write_all(fd, out.data(), out.size());
    full_sync(fd, "metadata");
    ::close(fd);
    std::filesystem::rename(tmp, directory_ / "meta");
    sync_directory(directory_);
}

}
