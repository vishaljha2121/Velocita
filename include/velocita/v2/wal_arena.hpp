// Bounded staging of write-ahead-log blocks between the Raft thread and the disk writer.
#pragma once

#include "velocita/storage.hpp"
#include "velocita/v2/kernel_codec.hpp"
#include "velocita/v2/bound_wal.hpp"
#include "velocita/v2/spsc_ring.hpp"
#include "velocita/v2/storage_checkpoint.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <span>
#include <vector>

namespace velocita::v2 {

inline constexpr std::size_t kWalBlockBytes = 4096;
inline constexpr std::size_t kWalBlockHeaderBytes = 32;
inline constexpr std::size_t kWalBlockPayloadBytes =
    kWalBlockBytes - kWalBlockHeaderBytes;

inline constexpr std::size_t kWalRecordHeaderBytes = 112;
static_assert(kMaxKernelCommandBytes + kWalRecordHeaderBytes <= kWalBlockPayloadBytes,
              "a canonical command frame must fit in one WAL block");
static_assert(2 * kMaxBoundWalRecordBytes <= kWalBlockPayloadBytes);
inline constexpr std::size_t kWalArenaBlocks = 64;
inline constexpr std::size_t kWalPrealloctionBlocks = 32;

enum class BlockState : std::uint8_t {
    open = 1,
    sealed,
    durable,
};

struct BlockDescriptor {
    std::size_t slot{};
    std::uint64_t generation{};
    std::uint64_t sequence{};
    std::uint64_t highest_record_index{};
    std::size_t byte_count{};
};

enum class StageOutcome : std::uint8_t {
    staged = 1,
    backpressure,
    record_too_large,
};

class WalBlockArena {
public:
    WalBlockArena();

    [[nodiscard]] StageOutcome stage(std::span<const std::byte> record_bytes,
                                     std::uint64_t record_index);

    void seal_open_block();

    [[nodiscard]] std::optional<BlockDescriptor> next_sealed();

    [[nodiscard]] std::optional<BlockDescriptor> next_unsubmitted();

    [[nodiscard]] std::span<const std::byte> block_bytes(
        const BlockDescriptor& descriptor) const;

    [[nodiscard]] bool complete(const BlockDescriptor& descriptor);

    [[nodiscard]] std::uint64_t durable_index() const noexcept {
        return durable_index_.load(std::memory_order_acquire);
    }

    void seed_durable_index(std::uint64_t index) noexcept;

    void rewind_durable_index(std::uint64_t surviving_index);

    [[nodiscard]] std::size_t sealed_count() const noexcept;
    [[nodiscard]] std::size_t free_blocks() const noexcept;
    [[nodiscard]] BlockState state_of(std::size_t slot) const noexcept;
    [[nodiscard]] std::uint64_t generation_of(std::size_t slot) const noexcept;

private:
    friend class WalSegment;

    struct Block {
        BlockState state{BlockState::durable};
        std::uint64_t generation{};
        std::uint64_t sequence{};
        std::uint64_t highest_record_index{};
        std::size_t used{};
        std::vector<std::byte> bytes;
    };

    void finish_open_block();
    [[nodiscard]] bool acquire_open_block();

    std::vector<Block> blocks_;
    std::optional<std::size_t> open_slot_;
    std::uint64_t next_sequence_{1};
    std::uint64_t next_generation_{1};
    std::vector<std::size_t> sealed_;
    std::size_t submitted_{0};
    std::atomic<std::uint64_t> durable_index_{0};
};

[[nodiscard]] std::vector<std::byte> frame_block(std::uint64_t generation,
                                                 std::uint64_t sequence,
                                                 std::span<const std::byte> payload);

struct DecodedBlock {
    std::uint64_t generation{};
    std::uint64_t sequence{};
    std::vector<std::byte> payload;
};
[[nodiscard]] std::optional<DecodedBlock> decode_block(
    std::span<const std::byte> bytes);

class WalSegment {
public:
    explicit WalSegment(std::filesystem::path data_dir, const VerifiedStaticIdentity& identity,
                        StorageCheckpoints checkpoints = {});
    ~WalSegment();
    WalSegment(const WalSegment&) = delete;
    WalSegment& operator=(const WalSegment&) = delete;

    std::size_t flush(WalBlockArena& arena);

    void write_block(const BlockDescriptor& descriptor,
                     std::span<const std::byte> payload);
    void sync();

    [[nodiscard]] std::vector<WalRecord> read_records() const;

    [[nodiscard]] static std::vector<WalRecord> read_existing_records(
        const std::filesystem::path& data_dir, const VerifiedStaticIdentity& identity);
    static void visit_existing_records(
        const std::filesystem::path& data_dir, const VerifiedStaticIdentity& identity,
        const std::function<void(const WalRecord&)>& visitor);

    void truncate_from(std::uint64_t first_removed_index);
    void discard_through(std::uint64_t last_removed_index);

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    void reserve_through(std::uint64_t offset);
    void rewrite_filtered(std::uint64_t boundary, bool keep_before_boundary);

    std::filesystem::path path_;
    VerifiedStaticIdentity identity_;
    StorageCheckpoints checkpoints_;
    int fd_{-1};
    std::uint64_t next_offset_{0};
    std::uint64_t reserved_through_{0};
};

struct WalMetrics {
    std::uint64_t blocks_submitted{};
    std::uint64_t blocks_durable{};
    std::uint64_t backpressure_refusals{};
    std::uint64_t highest_staged_index{};
    std::uint64_t durable_index{};

    [[nodiscard]] std::uint64_t durability_lag() const noexcept {
        return highest_staged_index > durable_index
                   ? highest_staged_index - durable_index
                   : 0;
    }
};

class WalWriter {
public:
    struct TargetSwitchCompletion {
        std::uint64_t previous_generation{};
        std::uint64_t generation{};
        std::uint64_t sequence{};
        bool succeeded{};
    };

    WalWriter(WalSegment& segment, const WalBlockArena& arena,
              std::uint64_t generation = 0,
              StorageCheckpoints checkpoints = {});
    ~WalWriter();
    WalWriter(const WalWriter&) = delete;
    WalWriter& operator=(const WalWriter&) = delete;

    [[nodiscard]] bool submit(const BlockDescriptor& descriptor) noexcept;
    [[nodiscard]] bool take_completion(BlockDescriptor& descriptor) noexcept;

    [[nodiscard]] bool submit_maintenance(void (*run)(void*), void* context) noexcept;
    [[nodiscard]] bool take_maintenance_completion() noexcept;

    [[nodiscard]] bool submit_target_switch(
        std::uint64_t expected_generation, std::uint64_t next_generation,
        std::uint64_t sequence, WalSegment& next_segment,
        const WalBlockArena& next_arena) noexcept;
    [[nodiscard]] bool take_target_switch_completion(
        TargetSwitchCompletion& completion) noexcept;

    std::size_t poll();

    void start();
    void stop();

    [[nodiscard]] bool failed() const noexcept {
        return failed_.load(std::memory_order_acquire);
    }
    void rethrow_failure() const;

    [[nodiscard]] std::uint64_t blocks_written() const noexcept {
        return blocks_written_.load(std::memory_order_acquire);
    }

private:
    WalSegment* segment_{};
    const WalBlockArena* arena_{};
    std::uint64_t target_generation_{};
    std::atomic<std::uint64_t> target_switch_sequence_{0};
    SpscRing<BlockDescriptor, 64> submissions_;
    SpscRing<BlockDescriptor, 64> completions_;
    std::atomic<std::uint64_t> blocks_written_{0};
    std::atomic<bool> running_{false};
    std::exception_ptr failure_;
    std::atomic<bool> failed_{false};
    std::thread thread_;
    void (*maintenance_run_)(void*){};
    void* maintenance_context_{};
    std::atomic<unsigned> maintenance_state_{0};
    WalSegment* switch_segment_{};
    const WalBlockArena* switch_arena_{};
    std::uint64_t switch_expected_generation_{};
    std::uint64_t switch_generation_{};
    std::uint64_t switch_sequence_{};
    TargetSwitchCompletion switch_completion_{};
    std::atomic<unsigned> switch_state_{0};
    StorageCheckpoints checkpoints_{};
};

}
