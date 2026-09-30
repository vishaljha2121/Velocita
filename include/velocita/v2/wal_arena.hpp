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

// Phase 7b. Bounded, block-structured staging for the write-ahead log.
//
// THE PROBLEM. `WriteAheadLog::append` opens, writes, fsyncs and closes per
// record, synchronously, from inside a Raft log callback and therefore from
// inside the owner loop. Every proposal pays a full sync before anything else
// can happen.
//
// THE CONTRACT THAT MUST NOT BREAK. A voter may contribute to quorum only for
// entries it has made DURABLE. Synchronous appends satisfy that by accident -
// the log index and the durable index are the same number. Splitting them is the
// whole point of this file, so from here they are tracked separately and the
// difference is where every remaining hazard lives.
//
// WHY BLOCKS, AND WHY A RECORD NEVER STRADDLES ONE. A torn write at a block
// boundary can otherwise split a record in half, and a half record whose length
// prefix survived is indistinguishable from a short one. Sealing a block early
// wastes a few bytes and makes every torn write detectable as a bad block rather
// than a plausible record.
//
// NO HIDDEN THREAD. The writer is driven by an explicit `poll`, so a test drives
// it deterministically and a production loop calls it from whichever thread it
// chooses. The same discipline the simulator uses for time: nothing that decides
// behaviour is allowed to be implicit.

// Page-sized, and that is a write-amplification decision rather than an
// alignment one.
//
// `flush_durability` seals whatever is open on every call, because leaving a
// record staged to save a write is exactly the latency this phase exists to
// remove. The consequence is that a quiet system writes ONE BLOCK PER RECORD, so
// the block size is the cost of a single small command reaching the disk. At
// 64 KiB a ~200-byte command cost 64 KiB; at 4 KiB it costs 4 KiB, which on an
// SD-backed deployment is the difference between plausible and not.
//
// The floor is the largest record that must never be refused: a canonical
// command frame plus its record header. The static assertion below keeps that
// relationship enforced rather than remembered.
inline constexpr std::size_t kWalBlockBytes = 4096;
inline constexpr std::size_t kWalBlockHeaderBytes = 32;
inline constexpr std::size_t kWalBlockPayloadBytes =
    kWalBlockBytes - kWalBlockHeaderBytes;

// A record that cannot fit a block is refused rather than split, so the block
// must be large enough for the biggest command the codec can produce. Checked
// here so growing the command budget fails the BUILD rather than silently
// refusing records at runtime, which would look like backpressure and would not
// be.
inline constexpr std::size_t kWalRecordHeaderBytes = 112;
static_assert(kMaxKernelCommandBytes + kWalRecordHeaderBytes <= kWalBlockPayloadBytes,
              "a canonical command frame must fit in one WAL block");
static_assert(2 * kMaxBoundWalRecordBytes <= kWalBlockPayloadBytes);
// Fixed capacity. The arena is a bounded budget, not a queue that grows: when it
// is full, admission is refused, which is the only behaviour that cannot lose an
// entry that is not yet durable.
// Sized as a TOTAL staging budget, not as a block count.
//
// This was eight when a block was 64 KiB. Shrinking the block to 4 KiB to cut
// write amplification shrank the buffer sixteen-fold with it, and the storage
// benchmark found the consequence immediately: four fifths of records refused,
// because only eight could ever be in flight. Sixty-four blocks restores a
// quarter-megabyte of staging, which is the number that was actually intended.
inline constexpr std::size_t kWalArenaBlocks = 64;
// Blocks preallocated ahead of the write frontier. Extending a file is a
// metadata operation the filesystem may itself have to journal, so doing it on
// the write path puts an unpredictable cost exactly where the phase is trying to
// remove one. Reserving ahead turns almost every write into an overwrite of
// space that already exists.
inline constexpr std::size_t kWalPrealloctionBlocks = 32;

enum class BlockState : std::uint8_t {
    // Owned by the producer, accepting records.
    open = 1,
    // Handed to the writer. The producer must not touch its bytes.
    sealed,
    // Bytes are on disk and fsynced. The slot may be reused, which is what makes
    // the generation below necessary.
    durable,
};

// A sealed block handed to the writer. The generation distinguishes THIS use of
// the slot from every later one, so a completion that arrives after the slot has
// been recycled can be recognised and ignored rather than releasing work that a
// different record now occupies.
struct BlockDescriptor {
    std::size_t slot{};
    std::uint64_t generation{};
    std::uint64_t sequence{};
    std::uint64_t highest_record_index{};
    std::size_t byte_count{};
};

enum class StageOutcome : std::uint8_t {
    staged = 1,
    // The arena is full because the writer has not drained it. Bounded refusal,
    // never an overwrite: the caller must slow down or fail closed.
    backpressure,
    // A single record larger than one block's payload can never be staged
    // without straddling, so it is refused rather than split.
    record_too_large,
};

class WalBlockArena {
public:
    WalBlockArena();

    // Copy one record's canonical bytes into the open block, sealing and
    // rotating first when it would not fit. Returns what happened; `staged` is
    // the only outcome that consumed the record.
    [[nodiscard]] StageOutcome stage(std::span<const std::byte> record_bytes,
                                     std::uint64_t record_index);

    // Seal the open block even if it is not full, so a quiet period cannot leave
    // recent records staged indefinitely. This is the space-and-time flush
    // trigger's time half; `stage` supplies the space half.
    void seal_open_block();

    // Next sealed block for the writer, in sequence order. Peeks: the block
    // stays queued until `complete`.
    [[nodiscard]] std::optional<BlockDescriptor> next_sealed();

    // Next sealed block NOT YET HANDED to a writer, advancing the hand-off
    // marker. Distinct from `next_sealed` because a block stays queued until it
    // completes, so peeking alone would submit the same block on every pass.
    [[nodiscard]] std::optional<BlockDescriptor> next_unsubmitted();

    // The bytes the writer must put on disk, framed and checksummed.
    [[nodiscard]] std::span<const std::byte> block_bytes(
        const BlockDescriptor& descriptor) const;

    // Report a completed flush. A descriptor whose generation no longer matches
    // the slot is IGNORED and reported as false: it describes a previous use of
    // a slot that has since been recycled, and releasing on it would free bytes
    // that are not yet durable.
    [[nodiscard]] bool complete(const BlockDescriptor& descriptor);

    // Highest record index whose bytes are durable. Advances only through
    // `complete`, and only in order.
    [[nodiscard]] std::uint64_t durable_index() const noexcept {
        return durable_index_.load(std::memory_order_acquire);
    }

    // Adopt a durable index established elsewhere - specifically, records
    // recovered from disk at startup. Those bytes ARE durable; the arena simply
    // did not stage them. Without this a restarted node would report durability
    // of zero and refuse to count itself toward quorum for its own recovered
    // log. Never lowers the index.
    void seed_durable_index(std::uint64_t index) noexcept;

    // Caller has exclusively drained the writer and durably removed the suffix.
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
    // Sealed blocks awaiting the writer, oldest first. Ordering matters: a later
    // block may not become durable before an earlier one, or the durable index
    // would cover a gap.
    std::vector<std::size_t> sealed_;
    // How many of `sealed_` have been handed to a writer. Front-relative, so it
    // decreases as blocks complete off the front.
    std::size_t submitted_{0};
    std::atomic<std::uint64_t> durable_index_{0};
};

// Frames one block: magic, version, generation, sequence, byte count, checksum,
// then payload. Exposed so the recovery reader and the writer cannot disagree
// about the layout.
[[nodiscard]] std::vector<std::byte> frame_block(std::uint64_t generation,
                                                 std::uint64_t sequence,
                                                 std::span<const std::byte> payload);

// Low-level framing: incompatible versions throw. Nullopt alone is NOT a recovery
// decision; the segment reader distinguishes zero preallocation from corruption.
struct DecodedBlock {
    std::uint64_t generation{};
    std::uint64_t sequence{};
    std::vector<std::byte> payload;
};
[[nodiscard]] std::optional<DecodedBlock> decode_block(
    std::span<const std::byte> bytes);

// The durable side of the arena: a preopened append-only segment of fixed-size
// blocks.
//
// Preopened and held open, because the cost this phase exists to remove is a
// per-record open/write/fsync/close. `flush` writes whole blocks at a block
// stride and syncs once per drain rather than once per record.
class WalSegment {
public:
    explicit WalSegment(std::filesystem::path data_dir, const VerifiedStaticIdentity& identity,
                        StorageCheckpoints checkpoints = {});
    ~WalSegment();
    WalSegment(const WalSegment&) = delete;
    WalSegment& operator=(const WalSegment&) = delete;

    // Drain every sealed block: write, sync once, then release them in order.
    // The sync happens BEFORE any release, so a block is only ever reported
    // durable after the bytes it describes are on disk.
    std::size_t flush(WalBlockArena& arena);

    // Split so a writer thread can batch many blocks behind ONE sync. Together
    // they are `flush`; apart they are what makes a single sync per pass
    // possible.
    void write_block(const BlockDescriptor& descriptor,
                     std::span<const std::byte> payload);
    void sync();

    // Recover complete bound records before zero preallocation or a physical
    // short tail. A full corrupt/unsupported/foreign block refuses, never skips.
    [[nodiscard]] std::vector<WalRecord> read_records() const;

    // Startup preflight must not create a segment before deciding compatibility.
    [[nodiscard]] static std::vector<WalRecord> read_existing_records(
        const std::filesystem::path& data_dir, const VerifiedStaticIdentity& identity);
    // Production recovery path. The callback is synchronous and each record is
    // valid only for the call; scanning retains no more than one decoded block.
    static void visit_existing_records(
        const std::filesystem::path& data_dir, const VerifiedStaticIdentity& identity,
        const std::function<void(const WalRecord&)>& visitor);

    // Rewrite the segment to exactly the records that survive a filter, as
    // fresh blocks. Both are rare (truncation on a leader change, discard after
    // a snapshot), so a rewrite is the right cost for the simplest correctness.
    //
    // `truncate_from` removing a SUFFIX is not housekeeping - an entry Raft has
    // popped must not come back on restart, and a segment that kept it would
    // resurrect a log the cluster has already rejected.
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
    // How far the file has been extended. Distinct from `next_offset_`: the
    // space exists, nothing has been written into it, and recovery must not read
    // it as data.
    std::uint64_t reserved_through_{0};
};

// What the storage layer is doing, in numbers an operator can act on. Kept here
// rather than in the trading metrics because none of it is economic: a full
// arena is a disk problem, not a risk problem.
struct WalMetrics {
    // Blocks handed to the writer and blocks made durable. The difference is
    // work in flight, and a difference that stops shrinking is a stalled writer.
    std::uint64_t blocks_submitted{};
    std::uint64_t blocks_durable{};
    // Records refused because the arena was full. Non-zero means the log could
    // not keep up and admission was correctly failing closed, which is a
    // capacity signal rather than an error.
    std::uint64_t backpressure_refusals{};
    // Highest index appended, and highest made durable. The gap between them is
    // the exposure asynchronous durability buys, expressed as a number.
    std::uint64_t highest_staged_index{};
    std::uint64_t durable_index{};

    [[nodiscard]] std::uint64_t durability_lag() const noexcept {
        return highest_staged_index > durable_index
                   ? highest_staged_index - durable_index
                   : 0;
    }
};

// Moves sealed blocks to disk, off the owner's thread.
//
// THE OWNER OWNS THE ARENA; THE WRITER ONLY READS SEALED BLOCKS. A sealed slot
// is not reused until its completion is applied BY THE OWNER, so the bytes the
// writer reads are stable for exactly as long as it holds the descriptor. That
// is what lets this be lock-free rather than merely lock-light.
//
// Descriptors cross in an SPSC ring in each direction. `poll` does one pass and
// is public, so a test drives the writer deterministically and a thread is an
// option rather than an assumption.
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

    // Owner side: hand over a sealed block. False means the ring is full, which
    // the caller must treat as backpressure rather than retry forever.
    [[nodiscard]] bool submit(const BlockDescriptor& descriptor) noexcept;
    // Owner side: collect a finished block, to be applied to the arena.
    [[nodiscard]] bool take_completion(BlockDescriptor& descriptor) noexcept;

    // One owner-fenced maintenance job, serialized on the SAME WAL writer.
    // Context stays alive and exclusively worker-accessible until completion;
    // take_maintenance_completion() acquires its writes back to the owner.
    // The protocol owner must drain staged blocks before submitting and pause
    // all further submissions until completion. No synchronous fallback.
    [[nodiscard]] bool submit_maintenance(void (*run)(void*), void* context) noexcept;
    [[nodiscard]] bool take_maintenance_completion() noexcept;

    // Caller must first seal and submit ALL old staging blocks; the writer
    // cannot discover records never handed to its queue.
    // Serial generation handoff. Once admitted, new block and maintenance
    // submissions pause. The worker drains and syncs every older block, then
    // waits for the owner to acquire their completions, then changes its target
    // without an owner-thread join or filesystem call.
    // Both old and new targets must outlive the acquired switch completion.
    // This completion is process-local writer ownership only: it is not durable
    // selector publication, generation activation, or permission to retire an
    // old arena before the owner has applied every dequeued block completion.
    [[nodiscard]] bool submit_target_switch(
        std::uint64_t expected_generation, std::uint64_t next_generation,
        std::uint64_t sequence, WalSegment& next_segment,
        const WalBlockArena& next_arena) noexcept;
    [[nodiscard]] bool take_target_switch_completion(
        TargetSwitchCompletion& completion) noexcept;

    // Writer side: one pass. Writes every submitted block, syncs ONCE, then
    // publishes completions. The single sync per pass is the whole economy of
    // the design, and publishing only after it is what makes a completion mean
    // durable.
    std::size_t poll();

    void start();
    void stop();

    // A worker failure is permanent for this writer. The owner observes the
    // latch at its next boundary and must stop serving from ambiguous storage.
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
    // idle -> queued -> completed -> idle; failure is the writer's permanent latch.
    std::atomic<unsigned> maintenance_state_{0};
    WalSegment* switch_segment_{};
    const WalBlockArena* switch_arena_{};
    std::uint64_t switch_expected_generation_{};
    std::uint64_t switch_generation_{};
    std::uint64_t switch_sequence_{};
    TargetSwitchCompletion switch_completion_{};
    // idle -> queued -> completed -> idle.
    std::atomic<unsigned> switch_state_{0};
    StorageCheckpoints checkpoints_{};
};

}  // namespace velocita::v2
