#pragma once

#include "velocita/v2/kernel.hpp"
#include "velocita/v2/bound_frame.hpp"
#include "velocita/v2/state_snapshot.hpp"
#include "velocita/v2/storage_checkpoint.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>

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
    // The last log entry INCLUDED in this snapshot. This is recovery coverage,
    // not audit-archive permission to reclaim the last historical copy.
    std::uint64_t last_included_index{};
    std::int64_t last_included_term{};
    TradingState state{};
    Digest256 state_digest{};
};

class SnapshotStoreError final : public std::runtime_error {
public:
    SnapshotStoreError(BoundFrameError code, const char* message)
        : std::runtime_error(message), code_(code) {}
    [[nodiscard]] BoundFrameError code() const noexcept { return code_; }
private:
    BoundFrameError code_;
};

class SnapshotStore {
public:
    explicit SnapshotStore(std::filesystem::path data_dir,
                           const VerifiedStaticIdentity& identity,
                           StorageCheckpoints checkpoints = {});

    // A corrupt supported snapshot may fall back to complete WAL replay.
    // Unsupported framing/state schema throws: replay is not a schema migration.
    [[nodiscard]] std::optional<StoredSnapshot> load() const;

    // DURABLE ON RETURN. Temp file, fsync, rename, fsync the directory, so a
    // crash leaves either the previous snapshot or this one and never a partial
    // one. This establishes recovery coverage only; log reclamation also needs
    // verified audit coverage and release of live references (ADR 0014).
    void save(const TradingState& state, std::uint64_t last_included_index,
              std::int64_t last_included_term) const;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    VerifiedStaticIdentity identity_;
    StorageCheckpoints checkpoints_;
    std::filesystem::path path_;
};

}  // namespace velocita::v2
