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

// V1 and historical evidence retain these generic codecs. Current V2 recovery
// uses identity-bound stores; the legacy V2 flat-WAL name is explicitly refused.
inline constexpr const char* kV1MetadataFileName = "metadata.bin";
inline constexpr const char* kV1WalFileName = "raft.wal";
inline constexpr const char* kV2MetadataFileName = "v2-metadata.bin";
inline constexpr const char* kV2WalFileName = "v2-raft.wal";

// Deterministic test seam for the atomic metadata replacement protocol. These
// checkpoints describe completed userspace/syscall boundaries; they are not a
// persisted contract. With no observer (the production default), save follows
// the same write/fsync/rename/directory-fsync path without an extra partial
// write boundary.
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
    // Default remains V1; filename overrides support historical evidence tools.
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
    // Not the current V2 block-WAL recovery path.
    explicit WriteAheadLog(std::filesystem::path data_dir,
                           std::string file_name = kV1WalFileName);
    void append(const WalRecord& record);
    // Remove a SUFFIX: every record whose index is at or above this one.
    void truncate_from(std::uint64_t first_removed_index);
    // Remove a PREFIX: every record whose index is at or below this one. Used
    // after a snapshot covering that prefix is durable, never before.
    void discard_through(std::uint64_t last_removed_index);
    [[nodiscard]] std::vector<WalRecord> read_all() const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    void rewrite(const std::vector<WalRecord>& kept) const;

    std::filesystem::path path_;
};

std::uint32_t crc32(std::span<const std::byte> bytes);

// Record framing, exposed so block staging and file recovery cannot drift apart
// about what a record looks like. `decode_wal_records` parses as many COMPLETE
// records as the span holds and stops at the first malformed one, because a
// partial tail is exactly what a torn write leaves and guessing past it is how a
// half record becomes a whole wrong one.
[[nodiscard]] std::vector<std::byte> encode_wal_record(const WalRecord& record);
[[nodiscard]] std::vector<WalRecord> decode_wal_records(std::span<const std::byte> bytes);

}  // namespace velocita
