// V1 write-ahead log and metadata; V2 reuses the record types.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
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

inline constexpr const char* kV1MetadataFileName = "metadata.bin";
inline constexpr const char* kV1WalFileName = "raft.wal";
inline constexpr const char* kV2MetadataFileName = "v2-metadata.bin";
inline constexpr const char* kV2WalFileName = "v2-raft.wal";

enum class MetadataCheckpoint {
    before_write,
    partial_write,
    file_written,
    file_synced,
    file_renamed,
    directory_synced,
};

struct MetadataCheckpoints {
    void (*observe)(MetadataCheckpoint, void*){};
    void* context{};
    void operator()(const MetadataCheckpoint checkpoint) const {
        if (observe) observe(checkpoint, context);
    }
};

class MetadataStore {
public:
    explicit MetadataStore(std::filesystem::path data_dir,
                           std::string file_name = kV1MetadataFileName,
                           MetadataCheckpoints checkpoints = {});
    [[nodiscard]] PersistentMetadata load() const;
    void save(const PersistentMetadata& metadata) const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    MetadataCheckpoints checkpoints_;
};

class WriteAheadLog {
public:
    explicit WriteAheadLog(std::filesystem::path data_dir,
                           std::string file_name = kV1WalFileName);
    void append(const WalRecord& record);
    void truncate_from(std::uint64_t first_removed_index);
    void discard_through(std::uint64_t last_removed_index);
    [[nodiscard]] std::vector<WalRecord> read_all() const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    void rewrite(const std::vector<WalRecord>& kept) const;

    std::filesystem::path path_;
};

std::uint32_t crc32(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_wal_record(const WalRecord& record);
[[nodiscard]] std::vector<WalRecord> decode_wal_records(std::span<const std::byte> bytes);

}
