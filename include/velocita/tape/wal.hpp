// The tape's write-ahead log and Raft metadata: append-only CRC-checked records, synced in groups.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace velocita::tape {

class StorageError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct WalEntry {
    std::uint64_t index{};
    std::int64_t term{};
    std::int32_t type{};
    std::vector<std::byte> bytes;
};

struct Metadata {
    std::int64_t term{};
    std::int32_t voted_for{-1};
};

class TapeWal {
public:
    explicit TapeWal(std::filesystem::path directory);
    ~TapeWal();
    TapeWal(const TapeWal&) = delete;
    TapeWal& operator=(const TapeWal&) = delete;

    [[nodiscard]] std::vector<WalEntry> recover();
    void append(std::uint64_t index, std::int64_t term, std::int32_t type, std::span<const std::byte> bytes);
    void truncate_from(std::uint64_t index);
    void sync();
    [[nodiscard]] std::uint64_t written_index() const noexcept { return written_index_; }
    [[nodiscard]] std::uint64_t synced_index() const noexcept { return synced_index_; }
    [[nodiscard]] std::uint64_t bytes() const noexcept { return bytes_; }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }

    [[nodiscard]] Metadata load_metadata() const;
    void store_metadata(const Metadata& metadata) const;

    [[nodiscard]] const std::filesystem::path& log_path() const noexcept { return log_path_; }

private:
    void write_record(std::uint8_t kind, std::uint64_t index, std::int64_t term, std::int32_t type,
                      std::span<const std::byte> bytes);
    std::filesystem::path directory_;
    std::filesystem::path log_path_;
    int fd_{-1};
    std::uint64_t written_index_{};
    std::uint64_t synced_index_{};
    std::uint64_t bytes_{};
    bool dirty_{};
};

[[nodiscard]] std::vector<WalEntry> read_wal_file(const std::filesystem::path& path, bool allow_torn_tail);

}
