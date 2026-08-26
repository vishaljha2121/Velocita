#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace velocita {

struct PersistentMetadata {
    std::int64_t term{};
    std::int32_t voted_for{-1};
    std::int64_t commit_index{};
};

struct WalRecord {
    std::uint64_t index{};
    std::int64_t term{};
    std::int32_t entry_id{};
    std::int32_t entry_type{};
    std::vector<std::byte> payload;
};

class MetadataStore {
public:
    explicit MetadataStore(std::filesystem::path data_dir);
    [[nodiscard]] PersistentMetadata load() const;
    void save(const PersistentMetadata& metadata) const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

class WriteAheadLog {
public:
    explicit WriteAheadLog(std::filesystem::path data_dir);
    void append(const WalRecord& record);
    void truncate_from(std::uint64_t first_removed_index);
    [[nodiscard]] std::vector<WalRecord> read_all() const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

std::uint32_t crc32(std::span<const std::byte> bytes);

}  // namespace velocita
