#include "velocita/v2/wal_arena.hpp"
#include "v2_identity_test_support.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace {
using namespace velocita::v2;
using namespace std::chrono_literals;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

const VerifiedStaticIdentity& identity() {
    static const auto value = identity_test::verified_identity(
        identity_test::identity(1, {1, 2, 3}));
    return value;
}

std::vector<std::byte> record(std::uint64_t index) {
    Command command;
    command.identity = {CallerId{1}, RequestSequence{index}, {}};
    command.identity.payload_digest.bytes[0] = std::byte{1};
    command.identity.payload_digest.bytes[1] = static_cast<std::byte>(index);
    command.body = AdvanceKillEpoch{KillEpoch{index}};
    std::array<std::byte, kMaxKernelCommandBytes> command_bytes{};
    const auto encoded = encode_kernel_command(command, command_bytes);
    require(encoded.error == KernelCodecError::none, "command encode failed");
    return encode_bound_wal_record(
        {index, 7, static_cast<std::int32_t>(index), 0,
         {command_bytes.begin(), command_bytes.begin() +
             static_cast<std::ptrdiff_t>(encoded.bytes_written)}}, identity());
}

template <typename Predicate>
void wait_until(Predicate predicate, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error(message);
        std::this_thread::yield();
    }
}

struct TempRoot {
    std::filesystem::path path;
    TempRoot() {
        auto pattern = (std::filesystem::temp_directory_path() /
                        "v2-wal-target-switch-XXXXXX").string();
        std::vector<char> bytes(pattern.begin(), pattern.end());
        bytes.push_back('\0');
        const auto* made = ::mkdtemp(bytes.data());
        require(made != nullptr, "mkdtemp failed");
        path = made;
    }
    ~TempRoot() { std::error_code error; std::filesystem::remove_all(path, error); }
};

void stage_one(WalBlockArena& arena, std::uint64_t index) {
    const auto bytes = record(index);
    require(arena.stage(bytes, index) == StageOutcome::staged, "record stage failed");
    arena.seal_open_block();
}

struct WriteGate {
    std::atomic<bool> entered{};
    std::atomic<bool> release{};
    static void observe(StorageCheckpoint point, void* raw) {
        auto& gate = *static_cast<WriteGate*>(raw);
        if (point != StorageCheckpoint::before_block_write ||
            gate.entered.exchange(true, std::memory_order_acq_rel)) return;
        while (!gate.release.load(std::memory_order_acquire))
            std::this_thread::yield();
    }
};

void target_switch_waits_for_owner_drain() {
    TempRoot root;
    const auto old_path = root.path / "old";
    const auto next_path = root.path / "next";
    const auto third_path = root.path / "third";
    std::filesystem::create_directories(old_path);
    std::filesystem::create_directories(next_path);
    std::filesystem::create_directories(third_path);
    WalBlockArena old_arena;
    WalBlockArena next_arena;
    WalSegment old_segment(old_path, identity());
    WalSegment next_segment(next_path, identity());
    WalSegment third_segment(third_path, identity());
    WalBlockArena third_arena;
    WalWriter writer(old_segment, old_arena, 11);
    writer.start();
    struct Stop { WalWriter& writer; ~Stop() { writer.stop(); } } stop{writer};

    stage_one(old_arena, 1);
    const auto old = old_arena.next_unsubmitted();
    require(old && writer.submit(*old), "old block submission failed");
    require(writer.submit_target_switch(11, 12, 41, next_segment, next_arena),
            "target switch admission failed");
    require(!writer.submit_target_switch(11, 13, 42, next_segment, next_arena),
            "second target switch was admitted");
    require(!writer.submit(*old), "block was admitted behind target switch");
    wait_until([&] { return writer.blocks_written() == 1; },
               "old block did not become durable");

    WalWriter::TargetSwitchCompletion switched{99, 99, 99, true};
    require(!writer.take_target_switch_completion(switched),
            "target switched before owner acquired old write completion");
    require(switched.previous_generation == 99 && switched.generation == 99,
            "empty switch poll changed caller output");
    BlockDescriptor completed{};
    require(writer.take_completion(completed) && completed.slot == old->slot &&
                completed.generation == old->generation &&
                completed.sequence == old->sequence,
            "old durable completion was missing");
    require(old_arena.complete(completed), "old arena completion failed");
    wait_until([&] { return writer.take_target_switch_completion(switched); },
               "target switch completion was not published after drain");
    require(switched.succeeded && switched.previous_generation == 11 &&
                switched.generation == 12 && switched.sequence == 41,
            "target switch completion identity mismatch");

    stage_one(next_arena, 2);
    const auto next = next_arena.next_unsubmitted();
    require(next && writer.submit(*next), "new target block submission failed");
    wait_until([&] { return writer.take_completion(completed); },
               "new target completion missing");
    require(next_arena.complete(completed), "new arena completion failed");
    require(!writer.submit_target_switch(12, 11, 42, third_segment, third_arena),
            "generation rollback target switch was admitted");
    require(!writer.submit_target_switch(12, 13, 41, third_segment, third_arena),
            "reused target switch sequence was admitted");
    require(writer.submit_target_switch(12, 13, 42, third_segment, third_arena),
            "monotonic target switch was refused");
    wait_until([&] { return writer.take_target_switch_completion(switched); },
               "monotonic target switch did not complete");
    require(switched.succeeded && switched.previous_generation == 12 &&
                switched.generation == 13 && switched.sequence == 42,
            "monotonic target switch completion mismatch");
    stage_one(third_arena, 3);
    const auto third = third_arena.next_unsubmitted();
    require(third && writer.submit(*third), "third target block submission failed");
    wait_until([&] { return writer.take_completion(completed); },
               "third target completion missing");
    require(third_arena.complete(completed), "third arena completion failed");
    const auto old_records = old_segment.read_records();
    const auto next_records = next_segment.read_records();
    const auto third_records = third_segment.read_records();
    require(old_records.size() == 1 && old_records[0].index == 1,
            "switch rewrote or appended through old target");
    require(next_records.size() == 1 && next_records[0].index == 2,
            "post-switch block missed the new target");
    require(third_records.size() == 1 && third_records[0].index == 3,
            "monotonic switch did not target the third WAL");
}

void saturated_real_blocks_drain_before_switch() {
    TempRoot root;
    const auto old_path = root.path / "saturated-old";
    const auto next_path = root.path / "saturated-next";
    std::filesystem::create_directories(old_path);
    std::filesystem::create_directories(next_path);
    WriteGate gate;
    WalBlockArena old_arena;
    WalBlockArena next_arena;
    WalSegment old_segment(old_path, identity(), {&WriteGate::observe, &gate});
    WalSegment next_segment(next_path, identity());
    WalWriter writer(old_segment, old_arena, 21);
    writer.start();
    struct Stop {
        WriteGate& gate;
        WalWriter& writer;
        ~Stop() { gate.release.store(true, std::memory_order_release); writer.stop(); }
    } stop{gate, writer};

    stage_one(old_arena, 1);
    auto descriptor = old_arena.next_unsubmitted();
    require(descriptor && writer.submit(*descriptor), "first saturated block refused");
    wait_until([&] { return gate.entered.load(std::memory_order_acquire); },
               "old writer did not park on first real block");
    for (std::uint64_t index = 2; index <= kWalArenaBlocks; ++index) {
        stage_one(old_arena, index);
        descriptor = old_arena.next_unsubmitted();
        require(descriptor && writer.submit(*descriptor),
                "saturated real block submission refused");
    }
    require(writer.submit_target_switch(21, 22, 51, next_segment, next_arena),
            "saturated target switch refused");
    gate.release.store(true, std::memory_order_release);

    std::size_t drained{};
    BlockDescriptor completed{};
    WalWriter::TargetSwitchCompletion switched{};
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (drained != kWalArenaBlocks && std::chrono::steady_clock::now() < deadline) {
        while (writer.take_completion(completed)) {
            require(old_arena.complete(completed),
                    "saturated old arena completion failed");
            ++drained;
        }
        if (drained != kWalArenaBlocks)
            require(!writer.take_target_switch_completion(switched),
                    "saturated switch completed before all old completions drained");
        std::this_thread::yield();
    }
    require(drained == kWalArenaBlocks, "saturated old writes did not all drain");
    wait_until([&] { return writer.take_target_switch_completion(switched); },
               "saturated target switch did not complete after drain");
    require(switched.succeeded && switched.previous_generation == 21 &&
                switched.generation == 22 && switched.sequence == 51,
            "saturated target switch identity mismatch");
    const auto records = old_segment.read_records();
    require(records.size() == kWalArenaBlocks && records.front().index == 1 &&
                records.back().index == kWalArenaBlocks,
            "saturated switch lost or redirected old WAL blocks");
    for (std::size_t offset = 0; offset < records.size(); ++offset) {
        require(encode_bound_wal_record(records[offset], identity()) == record(offset + 1),
                "saturated switch changed an authored record's tuple or payload");
    }
    require(next_segment.read_records().empty(),
            "saturated switch redirected an old record into the new WAL");
}

} // namespace

int main() {
    try {
        target_switch_waits_for_owner_drain();
        saturated_real_blocks_drain_before_switch();
        std::cout << "WAL target switch tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WAL target switch tests failed: " << error.what() << '\n';
        return 1;
    }
}
