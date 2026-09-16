#include "velocita/v2/wal_arena.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace velocita::v2;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::byte> record(const std::size_t size, const std::uint8_t marker) {
    std::vector<std::byte> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::byte>(marker ^ static_cast<std::uint8_t>(i));
    }
    return bytes;
}

// Stand-in for the writer: takes the next sealed block, frames it, and reports
// completion. Split into two calls so a test can interleave anything it likes
// between the disk write and the release - which is where the interesting
// failures live.
struct Writer {
    WalBlockArena* arena;
    std::vector<std::vector<std::byte>> written;

    [[nodiscard]] std::optional<BlockDescriptor> take() { return arena->next_sealed(); }

    void write(const BlockDescriptor& descriptor) {
        written.push_back(frame_block(descriptor.generation, descriptor.sequence,
                                      arena->block_bytes(descriptor)));
    }

    [[nodiscard]] bool release(const BlockDescriptor& descriptor) {
        return arena->complete(descriptor);
    }

    [[nodiscard]] bool flush_one() {
        const auto descriptor = take();
        if (!descriptor) return false;
        write(*descriptor);
        return release(*descriptor);
    }
};

void test_a_record_never_straddles_a_block() {
    WalBlockArena arena;
    // Two records that cannot share a block: the second would overrun.
    const auto big = record(kWalBlockPayloadBytes - 16, 0x11);
    const auto also_big = record(64, 0x22);
    require(arena.stage(big, 1) == StageOutcome::staged, "first record refused");
    require(arena.stage(also_big, 2) == StageOutcome::staged, "second record refused");

    // The first block was sealed SHORT rather than filled to the brim, because
    // the alternative is splitting a record across a boundary where a torn write
    // makes half of it indistinguishable from a whole short one.
    require(arena.sealed_count() == 1,
            "the arena did not seal the first block before starting a second");

    Writer writer{&arena, {}};
    require(writer.flush_one(), "flushing the sealed block failed");
    const auto decoded = decode_block(writer.written.front());
    require(decoded.has_value(), "the sealed block did not decode");
    require(decoded->payload.size() == big.size(),
            "the sealed block carried a partial record");
}

void test_a_record_larger_than_a_block_is_refused_not_split() {
    WalBlockArena arena;
    const auto enormous = record(kWalBlockPayloadBytes + 1, 0x33);
    require(arena.stage(enormous, 1) == StageOutcome::record_too_large,
            "an unstageable record was accepted");
    require(arena.sealed_count() == 0, "a refused record still sealed a block");
    require(arena.durable_index() == 0, "a refused record moved the durable index");
}

void test_block_and_generation_transitions() {
    WalBlockArena arena;
    require(arena.free_blocks() == kWalArenaBlocks, "the arena did not start empty");

    require(arena.stage(record(128, 0x44), 1) == StageOutcome::staged, "stage failed");
    require(arena.state_of(0) == BlockState::open, "the first block is not open");
    const auto first_generation = arena.generation_of(0);
    require(first_generation != 0, "an open block has no generation");

    arena.seal_open_block();
    require(arena.state_of(0) == BlockState::sealed, "sealing did not take");

    Writer writer{&arena, {}};
    require(writer.flush_one(), "flush failed");
    require(arena.state_of(0) == BlockState::durable, "completion did not release");

    // Reuse must mint a NEW generation, or a completion from the previous use
    // could not be told apart from this one's.
    for (std::size_t i = 0; i < kWalArenaBlocks; ++i) {
        require(arena.stage(record(kWalBlockPayloadBytes, 0x55),
                            static_cast<std::uint64_t>(10 + i)) ==
                    StageOutcome::staged,
                "stage failed while cycling blocks");
    }
    require(arena.generation_of(0) != first_generation,
            "a reused block kept its old generation");
}

void test_a_stale_completion_cannot_release_a_reused_slot() {
    WalBlockArena arena;
    require(arena.stage(record(128, 0x66), 1) == StageOutcome::staged, "stage failed");
    arena.seal_open_block();

    Writer writer{&arena, {}};
    const auto first = writer.take();
    require(first.has_value(), "no sealed block");
    writer.write(*first);
    require(writer.release(*first), "the first release failed");

    // The slot is recycled and now holds a different record.
    require(arena.stage(record(256, 0x77), 2) == StageOutcome::staged, "stage failed");
    arena.seal_open_block();
    const auto second = arena.next_sealed();
    require(second.has_value(), "no second sealed block");
    require(second->slot == first->slot, "the fixture did not reuse the slot");
    require(second->generation != first->generation, "the slot kept its generation");

    // THE HAZARD. A completion for the previous use arrives late. Honouring it
    // would mark the NEW record durable on the strength of a flush that never
    // covered it.
    require(!arena.complete(*first),
            "a stale completion released a recycled slot");
    require(arena.durable_index() == 1,
            "a stale completion advanced the durable index past what was flushed");
    require(arena.state_of(second->slot) == BlockState::sealed,
            "a stale completion changed the block's state");

    require(writer.flush_one(), "the genuine flush failed");
    require(arena.durable_index() == 2, "the genuine flush did not advance durability");
}

void test_the_durable_index_advances_only_on_completion_and_only_in_order() {
    WalBlockArena arena;
    require(arena.stage(record(kWalBlockPayloadBytes, 0x88), 5) == StageOutcome::staged,
            "stage failed");
    require(arena.stage(record(kWalBlockPayloadBytes, 0x99), 9) == StageOutcome::staged,
            "stage failed");
    arena.seal_open_block();
    require(arena.sealed_count() == 2, "the fixture did not seal two blocks");

    // Staged is not durable. This is the whole distinction the phase exists to
    // introduce, so it is asserted rather than assumed.
    require(arena.durable_index() == 0, "staging alone advanced the durable index");

    Writer writer{&arena, {}};
    const auto first = arena.next_sealed();
    require(first.has_value() && first->highest_record_index == 5, "wrong order");

    // Completing the SECOND block first must be refused. Accepting it would let
    // the durable index cover record 9 while record 5 is still only staged - a
    // gap the index would then be claiming to have crossed.
    auto out_of_order = *first;
    out_of_order.slot = (first->slot + 1) % kWalArenaBlocks;
    out_of_order.generation = arena.generation_of(out_of_order.slot);
    out_of_order.highest_record_index = 9;
    require(!arena.complete(out_of_order),
            "a later block became durable before an earlier one");
    require(arena.durable_index() == 0, "an out-of-order completion advanced durability");

    require(writer.flush_one(), "first flush failed");
    require(arena.durable_index() == 5, "durability did not follow the first block");
    require(writer.flush_one(), "second flush failed");
    require(arena.durable_index() == 9, "durability did not follow the second block");
}

void test_a_stalled_writer_causes_bounded_refusal_not_overwrite() {
    WalBlockArena arena;
    // Fill every block without ever completing one, which is exactly what a
    // stalled or failing writer looks like from here.
    std::uint64_t index = 0;
    for (std::size_t i = 0; i < kWalArenaBlocks; ++i) {
        require(arena.stage(record(kWalBlockPayloadBytes, 0xAA), ++index) ==
                    StageOutcome::staged,
                "stage failed before the arena was full");
    }
    arena.seal_open_block();
    require(arena.free_blocks() == 0, "the arena did not actually fill");

    // REFUSAL, not overwrite. Overwriting would discard bytes that are not yet
    // durable, and the entry they belong to may already have been counted toward
    // a quorum.
    require(arena.stage(record(64, 0xBB), ++index) == StageOutcome::backpressure,
            "a full arena accepted another record");
    require(arena.durable_index() == 0, "a refused record advanced durability");

    // And it recovers. Backpressure that never lifted would be a deadlock with a
    // safety argument attached.
    Writer writer{&arena, {}};
    require(writer.flush_one(), "flush failed");
    require(arena.stage(record(64, 0xCC), ++index) == StageOutcome::staged,
            "the arena stayed closed after a block drained");
}

void test_a_torn_or_corrupt_block_is_detected_not_guessed() {
    WalBlockArena arena;
    require(arena.stage(record(512, 0xDD), 7) == StageOutcome::staged, "stage failed");
    arena.seal_open_block();
    Writer writer{&arena, {}};
    require(writer.flush_one(), "flush failed");

    const auto good = writer.written.front();
    require(decode_block(good).has_value(), "a good block did not decode");

    auto flipped = good;
    flipped[kWalBlockHeaderBytes + 3] ^= std::byte{0xFF};
    require(!decode_block(flipped).has_value(),
            "a corrupted payload still decoded, so the checksum proves nothing");

    auto truncated = good;
    truncated.resize(kWalBlockHeaderBytes + 4);
    require(!decode_block(truncated).has_value(), "a truncated block still decoded");

    auto header_damaged = good;
    header_damaged[1] ^= std::byte{0xFF};
    require(!decode_block(header_damaged).has_value(),
            "a block with a damaged magic still decoded");

    // Padding is outside the checksum by design, so scribbling it must NOT
    // invalidate the block - a block's validity cannot depend on bytes nothing
    // wrote.
    auto padded = good;
    padded[kWalBlockBytes - 1] ^= std::byte{0xFF};
    require(decode_block(padded).has_value(),
            "trailing padding was included in the checksum");
}

void test_framing_round_trips_exactly() {
    const auto payload = record(1024, 0xEE);
    const auto framed = frame_block(42, 7, payload);
    require(framed.size() == kWalBlockBytes, "a framed block is not block-sized");
    const auto decoded = decode_block(framed);
    require(decoded.has_value(), "a framed block did not decode");
    require(decoded->generation == 42 && decoded->sequence == 7,
            "framing lost the generation or sequence");
    require(decoded->payload == payload, "framing did not round-trip the payload");
}

}  // namespace

int main() {
    try {
        test_framing_round_trips_exactly();
        test_a_record_never_straddles_a_block();
        test_a_record_larger_than_a_block_is_refused_not_split();
        test_block_and_generation_transitions();
        test_a_stale_completion_cannot_release_a_reused_slot();
        test_the_durable_index_advances_only_on_completion_and_only_in_order();
        test_a_stalled_writer_causes_bounded_refusal_not_overwrite();
        test_a_torn_or_corrupt_block_is_detected_not_guessed();
        std::cout << "all V2 WAL arena tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 WAL arena test failed: " << error.what() << "\n";
        return 1;
    }
}
