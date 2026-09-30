#include "velocita/v2/wal_arena.hpp"
#include "v2_identity_test_support.hpp"

#include <algorithm>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {
using namespace velocita::v2;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct TempRoot {
    std::filesystem::path path;
    TempRoot() {
        auto pattern = (std::filesystem::temp_directory_path() /
                        "v2-wal-target-switch-XXXXXX").string();
        const auto created = ::mkdtemp(pattern.data());
        require(created != nullptr, "temporary root creation failed");
        path = created;
    }
    ~TempRoot() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

const VerifiedStaticIdentity& identity() {
    static const auto value = identity_test::verified_identity(
        identity_test::identity(1, {1, 2, 3}));
    return value;
}

std::vector<std::byte> command_payload(std::uint64_t index) {
    Command command;
    command.identity = {CallerId{7}, RequestSequence{index}, {}};
    command.identity.payload_digest.bytes[0] = static_cast<std::byte>(index);
    command.body = AdvanceKillEpoch{KillEpoch{index}};
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(command, bytes);
    require(encoded.error == KernelCodecError::none, "command fixture did not encode");
    return {bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written)};
}

std::vector<std::byte> payload(std::uint64_t index) {
    const auto command = command_payload(index);
    return encode_bound_wal_record(
        {index, 3, static_cast<std::int32_t>(index), RAFT_LOGTYPE_NORMAL,
         command}, identity());
}

void stage(WalBlockArena& arena, std::uint64_t index) {
    require(arena.stage(payload(index), index) == StageOutcome::staged,
            "WAL arena refused authored record");
    arena.seal_open_block();
}

struct SyncGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool armed{};
    bool reached{};
    bool released{};

    StorageCheckpoints checkpoints() {
        return {[](StorageCheckpoint point, void* context) {
            auto& self = *static_cast<SyncGate*>(context);
            if (point != StorageCheckpoint::before_file_sync) return;
            std::unique_lock lock(self.mutex);
            if (!self.armed) return;
            self.reached = true;
            self.changed.notify_all();
            self.changed.wait(lock, [&] { return self.released; });
        }, this};
    }
    void arm() { std::lock_guard lock(mutex); armed = true; }
    void await() {
        std::unique_lock lock(mutex);
        require(changed.wait_for(lock, std::chrono::seconds(5), [&] { return reached; }),
                "writer did not reach old-target file sync");
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
};

struct ReleaseGate {
    SyncGate* gate{};
    ~ReleaseGate() { if (gate) gate->release(); }
};

struct ActivationGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool reached{};
    bool released{};
    StorageCheckpoints checkpoints() {
        return {[](StorageCheckpoint point, void* context) {
            if (point != StorageCheckpoint::writer_idle_before_control_acquire) return;
            auto& self = *static_cast<ActivationGate*>(context);
            std::unique_lock lock(self.mutex);
            if (self.reached) return;
            self.reached = true;
            self.changed.notify_all();
            self.changed.wait(lock, [&] { return self.released; });
        }, this};
    }
};

struct ActivationCleanup {
    ActivationGate* gate{};
    WalWriter* writer{};
    ~ActivationCleanup() {
        if (gate) {
            std::lock_guard lock(gate->mutex);
            gate->released = true;
            gate->changed.notify_all();
        }
        if (writer) writer->stop();
    }
};

struct IdlePassGate {
    std::mutex mutex;
    std::condition_variable changed;
    unsigned reached{};
    unsigned released{};
    StorageCheckpoints checkpoints() {
        return {[](StorageCheckpoint point, void* context) {
            if (point != StorageCheckpoint::writer_idle_before_control_acquire) return;
            auto& self = *static_cast<IdlePassGate*>(context);
            std::unique_lock lock(self.mutex);
            ++self.reached;
            const auto hit = self.reached;
            self.changed.notify_all();
            if (hit <= 2) self.changed.wait(lock, [&] { return self.released >= hit; });
        }, this};
    }
    void await_hit(unsigned hit) {
        std::unique_lock lock(mutex);
        require(changed.wait_for(lock, std::chrono::seconds(5), [&] { return reached >= hit; }),
                "worker did not reach required idle control-acquire pass");
    }
    void release_hit(unsigned hit) {
        std::lock_guard lock(mutex);
        released = std::max(released, hit);
        changed.notify_all();
    }
};

struct IdlePassCleanup {
    IdlePassGate* gate{};
    WalWriter* writer{};
    ~IdlePassCleanup() {
        if (gate) gate->release_hit(2);
        if (writer) writer->stop();
    }
};

template <typename Predicate>
void await(Predicate predicate, const char* failure) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return;
        std::this_thread::yield();
    }
    throw std::runtime_error(failure);
}

void paused_old_write_must_drain_before_switch(const std::filesystem::path& root) {
    const auto old_path = root / "old";
    const auto next_path = root / "next";
    std::filesystem::create_directories(old_path);
    std::filesystem::create_directories(next_path);
    SyncGate gate;
    WalBlockArena old_arena;
    WalBlockArena next_arena;
    WalSegment old_segment(old_path, identity(), gate.checkpoints());
    WalSegment next_segment(next_path, identity());
    WalWriter writer(old_segment, old_arena, 11);
    stage(old_arena, 1);
    const auto old_descriptor = old_arena.next_unsubmitted();
    require(old_descriptor && writer.submit(*old_descriptor), "old block submission failed");
    gate.arm();
    writer.start();
    ReleaseGate release{&gate};
    gate.await();

    require(writer.submit_target_switch(11, 12, 91, next_segment, next_arena),
            "target switch was not admitted behind the old write");
    require(!writer.submit(*old_descriptor), "switch admitted another old-target block");
    int maintenance_context{};
    require(!writer.submit_maintenance([](void*) {}, &maintenance_context),
            "switch admitted maintenance against an uncertain target");
    WalWriter::TargetSwitchCompletion switched{};
    require(!writer.take_target_switch_completion(switched) &&
                WalSegment::read_existing_records(next_path, identity()).empty(),
            "target activated before the old write was durable");

    gate.release();
    release.gate = nullptr;
    BlockDescriptor completed{};
    await([&] { return writer.take_completion(completed); },
          "old write did not publish its durable completion");
    require(completed.slot == old_descriptor->slot &&
                completed.generation == old_descriptor->generation &&
                old_arena.complete(completed),
            "old completion did not release its exact arena ownership");
    await([&] { return writer.take_target_switch_completion(switched); },
          "target switch did not complete after old ownership drained");
    require(switched.succeeded && switched.previous_generation == 11 &&
                switched.generation == 12 && switched.sequence == 91,
            "target switch completion lost generation or sequence identity");

    stage(next_arena, 2);
    const auto next_descriptor = next_arena.next_unsubmitted();
    require(next_descriptor && writer.submit(*next_descriptor),
            "new-target block submission failed");
    await([&] { return writer.take_completion(completed); },
          "new-target write did not complete");
    require(next_arena.complete(completed), "new-target completion did not release arena");
    writer.stop();
    const auto old_records = WalSegment::read_existing_records(old_path, identity());
    const auto next_records = WalSegment::read_existing_records(next_path, identity());
    require(old_records.size() == 1 && old_records[0].index == 1 &&
                old_records[0].payload == command_payload(1) &&
                next_records.size() == 1 && next_records[0].index == 2 &&
                next_records[0].payload == command_payload(2),
            "target switch crossed WAL bytes between generations");
}

void submission_race_must_not_cross_target(const std::filesystem::path& root) {
    const auto old_path = root / "race-old";
    const auto next_path = root / "race-next";
    std::filesystem::create_directories(old_path);
    std::filesystem::create_directories(next_path);
    ActivationGate gate;
    WalBlockArena old_arena;
    WalBlockArena next_arena;
    WalSegment old_segment(old_path, identity());
    WalSegment next_segment(next_path, identity());
    WalWriter writer(old_segment, old_arena, 31, gate.checkpoints());
    writer.start();
    ActivationCleanup cleanup{&gate, &writer};
    {
        std::unique_lock lock(gate.mutex);
        require(gate.changed.wait_for(lock, std::chrono::seconds(5), [&] { return gate.reached; }),
                "worker did not reach empty-pop/switch-acquire interposition");
    }
    stage(old_arena, 4);
    const auto descriptor = old_arena.next_unsubmitted();
    require(descriptor && writer.submit(*descriptor),
            "interposed old-target submission failed");
    require(writer.submit_target_switch(31, 32, 101, next_segment, next_arena),
            "interposed switch submission failed");
    {
        std::lock_guard lock(gate.mutex);
        gate.released = true;
        gate.changed.notify_all();
    }
    BlockDescriptor completed{};
    await([&] { return writer.take_completion(completed); },
          "interposed old-target write did not drain");
    require(old_arena.complete(completed), "interposed old completion mismatched arena");
    WalWriter::TargetSwitchCompletion switched{};
    await([&] { return writer.take_target_switch_completion(switched); },
          "interposed target switch did not complete after drain");
    writer.stop();
    cleanup.writer = nullptr;
    const auto old_records = WalSegment::read_existing_records(old_path, identity());
    const auto next_records = WalSegment::read_existing_records(next_path, identity());
    require(switched.succeeded && switched.sequence == 101 &&
                old_records.size() == 1 && old_records[0].index == 4 &&
                old_records[0].payload == command_payload(4) && next_records.empty(),
            "empty-pop race wrote an old descriptor through the new target");
}

void undrained_completion_must_defer_switch(const std::filesystem::path& root) {
    const auto old_path = root / "completion-old";
    const auto next_path = root / "completion-next";
    std::filesystem::create_directories(old_path);
    std::filesystem::create_directories(next_path);
    IdlePassGate gate;
    WalBlockArena old_arena;
    WalBlockArena next_arena;
    WalSegment old_segment(old_path, identity());
    WalSegment next_segment(next_path, identity());
    WalWriter writer(old_segment, old_arena, 41, gate.checkpoints());
    stage(old_arena, 5);
    const auto descriptor = old_arena.next_unsubmitted();
    require(descriptor && writer.submit(*descriptor),
            "completion-guard old block submission failed");
    writer.start();
    IdlePassCleanup cleanup{&gate, &writer};

    // Hit one is after the worker published the old durable completion but
    // before it acquired any target-switch control state.
    gate.await_hit(1);
    require(writer.submit_target_switch(41, 42, 201, next_segment, next_arena),
            "completion-guard switch submission failed");
    gate.release_hit(1);
    // With the guard intact, that idle pass observes the queued switch but
    // refuses activation because the owner has not acquired the old completion.
    gate.await_hit(2);
    WalWriter::TargetSwitchCompletion switched{};
    require(!writer.take_target_switch_completion(switched),
            "target switched while an old completion still owned its arena slot");

    gate.release_hit(2);
    BlockDescriptor completed{};
    require(writer.take_completion(completed) && old_arena.complete(completed),
            "old completion could not be acquired after switch deferral");
    await([&] { return writer.take_target_switch_completion(switched); },
          "switch did not complete after old completion ownership drained");
    writer.stop();
    cleanup.writer = nullptr;
    require(switched.succeeded && switched.previous_generation == 41 &&
                switched.generation == 42 && switched.sequence == 201 &&
                WalSegment::read_existing_records(old_path, identity()).size() == 1 &&
                WalSegment::read_existing_records(next_path, identity()).empty(),
            "completion-guard switch changed target identity or WAL placement");
}

struct MaintenanceGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool reached{};
    bool released{};
    static void run(void* context) {
        auto& self = *static_cast<MaintenanceGate*>(context);
        std::unique_lock lock(self.mutex);
        self.reached = true;
        self.changed.notify_all();
        self.changed.wait(lock, [&] { return self.released; });
    }
};

struct MaintenanceCleanup {
    MaintenanceGate* gate{};
    WalWriter* writer{};
    ~MaintenanceCleanup() {
        if (gate) {
            std::lock_guard lock(gate->mutex);
            gate->released = true;
            gate->changed.notify_all();
        }
        if (writer) writer->stop();
    }
};

void maintenance_and_stale_generation_must_not_rebind(const std::filesystem::path& root) {
    const auto old_path = root / "maintenance-old";
    const auto next_path = root / "maintenance-next";
    std::filesystem::create_directories(old_path);
    std::filesystem::create_directories(next_path);
    WalBlockArena old_arena;
    WalBlockArena next_arena;
    WalSegment old_segment(old_path, identity());
    WalSegment next_segment(next_path, identity());
    MaintenanceGate maintenance;
    WalWriter writer(old_segment, old_arena, 20);
    MaintenanceCleanup cleanup{&maintenance, &writer};
    writer.start();

    require(writer.submit_maintenance(&MaintenanceGate::run, &maintenance),
            "maintenance submission failed");
    {
        std::unique_lock lock(maintenance.mutex);
        require(maintenance.changed.wait_for(lock, std::chrono::seconds(5),
                                             [&] { return maintenance.reached; }),
                "maintenance did not reach worker");
    }
    require(!writer.submit_target_switch(20, 21, 1, next_segment, next_arena),
            "target switch overlapped active maintenance");
    {
        std::lock_guard lock(maintenance.mutex);
        maintenance.released = true;
        maintenance.changed.notify_all();
    }
    await([&] { return writer.take_maintenance_completion(); },
          "maintenance completion was not acquired");

    require(writer.submit_target_switch(19, 21, 2, next_segment, next_arena),
            "stale-generation switch request was not classified");
    WalWriter::TargetSwitchCompletion stale{};
    await([&] { return writer.take_target_switch_completion(stale); },
          "stale-generation completion was not returned");
    require(!stale.succeeded && stale.previous_generation == 20 &&
                stale.generation == 21 && stale.sequence == 2,
            "stale-generation switch completion was not explicitly fenced");
    require(!writer.take_target_switch_completion(stale),
            "stale target-switch completion was delivered twice");

    stage(old_arena, 3);
    const auto descriptor = old_arena.next_unsubmitted();
    require(descriptor && writer.submit(*descriptor),
            "failed switch did not leave old target active");
    BlockDescriptor completed{};
    await([&] { return writer.take_completion(completed); },
          "old target did not accept work after stale switch refusal");
    require(old_arena.complete(completed), "old target completion ownership mismatch");
    require(!writer.submit_target_switch(20, 19, 3, next_segment, next_arena),
            "generation rollback was admitted");
    require(writer.submit_target_switch(20, 21, 4, next_segment, next_arena),
            "valid target switch was refused");
    WalWriter::TargetSwitchCompletion valid{};
    await([&] { return writer.take_target_switch_completion(valid); },
          "valid target switch completion missing");
    require(valid.succeeded && valid.sequence == 4,
            "valid switch completion identity mismatch");
    require(!writer.submit_target_switch(21, 22, 4, old_segment, old_arena),
            "reused target-switch sequence was admitted");
    writer.stop();
    cleanup.writer = nullptr;
    require(WalSegment::read_existing_records(old_path, identity()).size() == 1 &&
                WalSegment::read_existing_records(next_path, identity()).empty(),
            "stale-generation switch rebound or wrote the new target");

}
} // namespace

int main() {
    try {
        TempRoot root;
        paused_old_write_must_drain_before_switch(root.path);
        submission_race_must_not_cross_target(root.path);
        undrained_completion_must_defer_switch(root.path);
        maintenance_and_stale_generation_must_not_rebind(root.path);
        std::cout << "WAL target switch adversarial tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WAL target switch adversarial tests failed: " << error.what() << '\n';
        return 1;
    }
}
