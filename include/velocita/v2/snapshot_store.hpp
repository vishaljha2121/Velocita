#pragma once

#include "velocita/v2/kernel.hpp"
#include "velocita/v2/state_snapshot.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace velocita::v2 {

// A durable, checksummed, versioned V2 snapshot.
//
// The STATE bytes are the canonical snapshot the reducer already defines, so
// this adds framing and durability and invents no economics. That matters twice
// over: the canonical encoder already refuses an impossible state, and it
// already carries its own digest, so a snapshot that decodes is a snapshot the
// reducer would have accepted.
//
// A new magic and a new filename, deliberately. V1's WAL and metadata files are
// never reinterpreted in place - a V1 directory read by this class finds no
// snapshot rather than a misparsed one.

struct StoredSnapshot {
    // The last log entry INCLUDED in this snapshot. Everything at or below it is
    // reclaimable from the log; everything above it must still be replayed.
    std::uint64_t last_included_index{};
    std::int64_t last_included_term{};
    TradingState state{};
    Digest256 state_digest{};
};

class SnapshotStore {
public:
    explicit SnapshotStore(std::filesystem::path data_dir);

    // Absent, or unreadable for any reason, reads as absent rather than
    // throwing: a corrupt snapshot must degrade to "replay the whole log",
    // which is always correct, rather than to a refusal to start.
    [[nodiscard]] std::optional<StoredSnapshot> load() const;

    // DURABLE ON RETURN. Temp file, fsync, rename, fsync the directory, so a
    // crash leaves either the previous snapshot or this one and never a partial
    // one. The caller may reclaim the log prefix only after this returns.
    void save(const TradingState& state, std::uint64_t last_included_index,
              std::int64_t last_included_term) const;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace velocita::v2
