#pragma once

namespace velocita::v2 {

enum class StorageCheckpoint {
    before_file_sync,
    rewrite_block,
    file_synced,
    file_renamed,
    directory_synced,
};

// Optional deterministic crash/failure seam. No callback on ordinary appends.
struct StorageCheckpoints {
    void (*observe)(StorageCheckpoint, void*){};
    void* context{};
    void operator()(StorageCheckpoint checkpoint) const {
        if (observe) observe(checkpoint, context);
    }
};

}  // namespace velocita::v2
