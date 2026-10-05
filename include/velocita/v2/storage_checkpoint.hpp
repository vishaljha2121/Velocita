// Named points in storage operations where tests inject crashes.
#pragma once

namespace velocita::v2 {

enum class StorageCheckpoint {
    before_file_sync,
    rewrite_block,
    file_synced,
    file_renamed,
    directory_synced,
    before_block_write,
    compaction_archive_verified,
    compaction_snapshot_verified,
    compaction_before_wal_rewrite,
    compaction_wal_rewritten,
    history_recovery_before_publication,
    history_recovery_published,
    writer_idle_before_control_acquire,
};

struct StorageCheckpoints {
    void (*observe)(StorageCheckpoint, void*){};
    void* context{};
    void operator()(StorageCheckpoint checkpoint) const {
        if (observe) observe(checkpoint, context);
    }
};

}
