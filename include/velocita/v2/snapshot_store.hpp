// Durable storage of the kernel's state snapshot.
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

struct StoredSnapshot {
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

    [[nodiscard]] std::optional<StoredSnapshot> load() const;

    void save(const TradingState& state, std::uint64_t last_included_index,
              std::int64_t last_included_term) const;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    VerifiedStaticIdentity identity_;
    StorageCheckpoints checkpoints_;
    std::filesystem::path path_;
};

}
