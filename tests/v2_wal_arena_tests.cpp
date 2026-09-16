#include "velocita/v2/wal_arena.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

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

void test_the_durable_index_does_not_advance_before_the_sync(
    const std::filesystem::path& root) {
    // The narrowest window in the whole durability path, and the one that was
    // wrong. `flush` writes blocks, syncs, and releases them; the release is
    // what raises the durable index, and the durable index is what this node
    // reports to Raft for its own quorum vote. Releasing before the sync means
    // counting a vote for bytes that are in the page cache and nowhere else - a
    // power cut in that window loses entries a majority already promised.
    //
    // Observed at the `before_file_sync` checkpoint rather than by crashing,
    // because a crash cannot show it: `_exit` does not drop the page cache, so
    // the file comes back complete either way and the bug is invisible on disk.
    // What distinguishes correct from incorrect is the INDEX at that instant.
    std::filesystem::create_directories(root);

    const auto staged_record = [](const std::uint64_t index) {
        velocita::WalRecord value;
        value.index = index;
        value.term = 1;
        value.entry_id = static_cast<std::int32_t>(index);
        value.entry_type = 0;
        value.payload = record(64, static_cast<std::uint8_t>(0x70 + index));
        return velocita::encode_wal_record(value);
    };

    struct Observation {
        WalBlockArena* arena;
        std::size_t calls;
        std::uint64_t durable_at_sync;
    };

    WalBlockArena arena;
    require(arena.stage(staged_record(1), 1) == StageOutcome::staged, "stage failed");
    arena.seal_open_block();

    Observation seen{&arena, 0, 0};
    WalSegment segment(root, {[](StorageCheckpoint at, void* context) {
                                  if (at != StorageCheckpoint::before_file_sync) return;
                                  auto& observation = *static_cast<Observation*>(context);
                                  ++observation.calls;
                                  observation.durable_at_sync =
                                      observation.arena->durable_index();
                              },
                              &seen});

    require(segment.flush(arena) == 1, "flush wrote nothing");
    // Without this the assertion below would hold vacuously on any build where
    // the checkpoint stopped firing.
    require(seen.calls == 1,
            "the pre-sync checkpoint fired " + std::to_string(seen.calls) +
                " times, so the window was never observed");
    require(seen.durable_at_sync == 0,
            "the durable index was already " + std::to_string(seen.durable_at_sync) +
                " when the sync began, so a block was credited as durable before "
                "its bytes reached the disk");
    require(arena.durable_index() == 1,
            "the durable index did not advance after a successful sync");
}

void test_preallocated_space_does_not_read_back_as_data(
    const std::filesystem::path& root) {
    // Preallocation extends the file well past the write frontier. That space
    // must read as the END of the log, not as blocks - otherwise recovery would
    // walk into reserved zeros and, at best, stop for the wrong reason.
    std::filesystem::create_directories(root);
    // REAL records, not arbitrary bytes. The framing tests above only exercise
    // the block layer, so they pass on any payload; this one walks the whole
    // path back out through the record decoder and needs records that decode.
    const auto staged_record = [](const std::uint64_t index) {
        velocita::WalRecord value;
        value.index = index;
        value.term = 1;
        value.entry_id = static_cast<std::int32_t>(index);
        value.entry_type = 0;
        value.payload = record(64, static_cast<std::uint8_t>(0x40 + index));
        return velocita::encode_wal_record(value);
    };

    WalBlockArena arena;
    require(arena.stage(staged_record(1), 1) == StageOutcome::staged, "stage failed");
    arena.seal_open_block();

    {
        WalSegment segment(root);
        require(segment.flush(arena) == 1, "flush wrote nothing");
    }

    const auto size = std::filesystem::file_size(root / "v2-wal-blocks.bin");
    require(size > kWalBlockBytes,
            "the segment was not preallocated beyond the block it wrote");

    WalSegment reopened(root);
    const auto records = reopened.read_records();
    require(records.size() == 1,
            "recovery read " + std::to_string(records.size()) +
                " records from a segment holding one, so reserved space is being "
                "read as data");
    require(records.front().index == 1, "the recovered record is not the one written");

    // And appending after a reopen must continue AFTER the existing block, not
    // overwrite it - the resume point comes from the written frontier, which is
    // not the file size once space is reserved ahead of it.
    WalBlockArena more;
    require(more.stage(staged_record(2), 2) == StageOutcome::staged, "stage failed");
    more.seal_open_block();
    require(reopened.flush(more) == 1, "second flush wrote nothing");

    WalSegment again(root);
    const auto both = again.read_records();
    require(both.size() == 2,
            "appending after a reopen lost or duplicated a record: got " +
                std::to_string(both.size()));
    require(both[0].index == 1 && both[1].index == 2,
            "records came back in the wrong order after a reopen");
}

void test_rewrite_crashes_preserve_one_complete_history(const std::filesystem::path& root) {
    for (const auto point : {StorageCheckpoint::rewrite_block, StorageCheckpoint::file_synced,
                             StorageCheckpoint::file_renamed, StorageCheckpoint::directory_synced}) {
        const auto dir = root / std::to_string(static_cast<int>(point));
        std::filesystem::create_directories(dir);
        {
            WalBlockArena arena;
            WalSegment segment(dir);
            for (std::uint64_t index = 1; index <= 3; ++index) {
                velocita::WalRecord value{index, 1, static_cast<std::int32_t>(index), 0,
                    record(3000, static_cast<std::uint8_t>(index))};
                require(arena.stage(velocita::encode_wal_record(value), index) == StageOutcome::staged,
                        "rewrite fixture stage refused");
                arena.seal_open_block();
                segment.flush(arena);
            }
        }
        auto selected = point;
        const auto child = ::fork();
        require(child >= 0, "fork failed");
        if (child == 0) {
            WalSegment segment(dir, {[](StorageCheckpoint at, void* context) {
                if (at == *static_cast<StorageCheckpoint*>(context)) ::_exit(77);
            }, &selected});
            segment.discard_through(1);
            ::_exit(0);
        }
        int status = 0;
        require(::waitpid(child, &status, 0) == child && WIFEXITED(status), "child failed");
        require(WEXITSTATUS(status) == 77, "rewrite checkpoint was not exercised");
        const auto recovered = WalSegment(dir).read_records();
        const bool old = recovered.size() == 3 && recovered[0].index == 1 &&
                         recovered[1].index == 2 && recovered[2].index == 3;
        const bool replaced = recovered.size() == 2 && recovered[0].index == 2 &&
                              recovered[1].index == 3;
        require(old || replaced, "rewrite crash recovered a mixed/duplicated history");
        for (const auto& value : recovered)
            require(value.payload == record(3000, static_cast<std::uint8_t>(value.index)),
                    "rewrite crash changed record bytes");
    }
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
        {
            const auto root = std::filesystem::temp_directory_path() /
                              "velocita-v2-wal-arena-tests";
            std::filesystem::remove_all(root);
            test_rewrite_crashes_preserve_one_complete_history(root / "rewrite");
            test_the_durable_index_does_not_advance_before_the_sync(root / "presync");
            test_preallocated_space_does_not_read_back_as_data(root);
            std::filesystem::remove_all(root);
        }
        std::cout << "all V2 WAL arena tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 WAL arena test failed: " << error.what() << "\n";
        return 1;
    }
}
