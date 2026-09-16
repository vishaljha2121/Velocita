#pragma once

#include "velocita/storage.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
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

inline constexpr std::size_t kWalBlockBytes = 64 * 1024;
inline constexpr std::size_t kWalBlockHeaderBytes = 32;
inline constexpr std::size_t kWalBlockPayloadBytes =
    kWalBlockBytes - kWalBlockHeaderBytes;
// Fixed capacity. The arena is a bounded budget, not a queue that grows: when it
// is full, admission is refused, which is the only behaviour that cannot lose an
// entry that is not yet durable.
inline constexpr std::size_t kWalArenaBlocks = 8;

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

    // Next sealed block for the writer, in sequence order.
    [[nodiscard]] std::optional<BlockDescriptor> next_sealed();

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

    [[nodiscard]] std::size_t sealed_count() const noexcept;
    [[nodiscard]] std::size_t free_blocks() const noexcept;
    [[nodiscard]] BlockState state_of(std::size_t slot) const noexcept;
    [[nodiscard]] std::uint64_t generation_of(std::size_t slot) const noexcept;

private:
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
    std::atomic<std::uint64_t> durable_index_{0};
};

// Frames one block: magic, version, generation, sequence, byte count, checksum,
// then payload. Exposed so the recovery reader and the writer cannot disagree
// about the layout.
[[nodiscard]] std::vector<std::byte> frame_block(std::uint64_t generation,
                                                 std::uint64_t sequence,
                                                 std::span<const std::byte> payload);

// Validates framing and checksum. Absent means the block is torn, truncated or
// corrupt - never a guess at what it might have contained.
struct DecodedBlock {
    std::uint64_t generation{};
    std::uint64_t sequence{};
    std::vector<std::byte> payload;
};
[[nodiscard]] std::optional<DecodedBlock> decode_block(
    std::span<const std::byte> bytes);

}  // namespace velocita::v2
