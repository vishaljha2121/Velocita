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

// FILE NAMES ARE A PARAMETER because two milestones share these layers.
//
// V2's replicated kernel reuses this storage unchanged - it is payload-generic,
// and duplicating it would have been worse. But both milestones then wrote the
// SAME filenames with the SAME magic and version constants, so a V2 node aimed
// at a V1 data directory accepted V1's hard state: the values it checks are
// literally the values V1 wrote. It failed later and only by accident, when a
// V1 payload reached the V2 command codec, and nothing at all stopped a V1 node
// reading V2 storage.
//
// Distinct names make the two formats unable to occupy the same path, which is
// a stronger and cheaper guarantee than teaching each frame to reject the other
// - and it changes no persisted frame's bytes. See G-051.
inline constexpr const char* kV1MetadataFileName = "metadata.bin";
inline constexpr const char* kV1WalFileName = "raft.wal";
inline constexpr const char* kV2MetadataFileName = "v2-metadata.bin";
inline constexpr const char* kV2WalFileName = "v2-raft.wal";

class MetadataStore {
public:
    // The default is V1's name, so V1 is unchanged by construction rather than
    // by review. V2 passes `kV2MetadataFileName`.
    explicit MetadataStore(std::filesystem::path data_dir,
                           std::string file_name = kV1MetadataFileName);
    [[nodiscard]] PersistentMetadata load() const;
    void save(const PersistentMetadata& metadata) const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

class WriteAheadLog {
public:
    // See `MetadataStore`. V2 passes `kV2WalFileName`.
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
