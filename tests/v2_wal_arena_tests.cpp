#include "velocita/v2/wal_arena.hpp"
#include "velocita/v2/bound_wal.hpp"
#include "v2_identity_test_support.hpp"

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

const VerifiedStaticIdentity& wal_identity() {
    static const auto value = identity_test::verified_identity(
        identity_test::identity(1, {1, 2, 3}));
    return value;
}

std::vector<std::byte> command_payload(const std::uint64_t sequence) {
    Command command;
    command.identity = {CallerId{1}, RequestSequence{sequence}, {}};
    command.identity.payload_digest.bytes[0] = static_cast<std::byte>(sequence);
    command.body = AdvanceKillEpoch{KillEpoch{sequence}};
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(command, bytes);
    require(encoded.error == KernelCodecError::none, "command fixture encode failed");
    return {bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written)};
}

std::vector<std::byte> bound_record(const std::uint64_t index, const std::int64_t term = 7) {
    return encode_bound_wal_record(
        {index, term, static_cast<std::int32_t>(index), 0, command_payload(index)},
        wal_identity());
}

void test_bound_record_codec_and_lineage_refusals() {
    const auto bytes = bound_record(7, 3);
    const auto decoded = decode_bound_wal_record(bytes, wal_identity());
    require(decoded.index == 7 && decoded.term == 3 && decoded.entry_id == 7 &&
                decoded.entry_type == 0 && decoded.payload == command_payload(7),
            "bound WAL codec changed record semantics");

    const auto expect = [&](const VerifiedStaticIdentity& identity, BoundFrameError expected,
                            const char* context) {
        try {
            (void)decode_bound_wal_record(bytes, identity);
        } catch (const BoundWalError& error) {
            require(error.code() == expected, context);
            return;
        }
        throw std::runtime_error(context);
    };
    expect(identity_test::verified_identity(identity_test::identity(2, {1, 2, 3})),
           BoundFrameError::wrong_owner, "wrong-owner WAL error was not typed");
    expect(identity_test::verified_identity(identity_test::identity(1, {1, 2, 3},
               catalog_test::standard_catalog(), 0xBC)),
           BoundFrameError::foreign_manifest, "foreign-manifest WAL error was not typed");

    auto partial = bytes;
    partial.pop_back();
    try {
        (void)decode_bound_wal_records(partial, wal_identity());
    } catch (const BoundWalError& error) {
        require(error.code() == BoundFrameError::length_mismatch,
                "partial inner record error was not typed");
        return;
    }
    throw std::runtime_error("partial inner record was accepted");
}

void test_independent_bound_wal_golden_and_block_version() {
    const auto hex = [](std::string_view text) {
        std::vector<std::byte> bytes;
        for (std::size_t at = 0; at < text.size(); at += 2)
            bytes.push_back(std::byte(std::stoul(std::string(text.substr(at, 2)), nullptr, 16)));
        return bytes;
    };
    const auto identity_bytes = hex(
        "563249447c000000010000000102030405060708090a0b0c0d0e0f10"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
        "0a000a0001000100010001000101030011000000010100000100010011000000"
        "08a4324b57b69508e3785b815ea368a65919f96695b0afcdb09ae5a85503f21d");
    VerifiedStaticIdentity identity;
    require(VerifiedStaticIdentity::decode(identity_bytes, identity) == StaticIdentityCodecError::none,
            "golden WAL identity failed");
    const auto golden = hex(
        "5632424601000200d40000007c000000599621a2f6f5336362106d12208b6491336fcb75e596af59b534a91019493ee0"
        "1100000000000000070000000000000003000000000000000700000000000000"
        "56324b43380000000a000600010000000000000001000000000000000100000000000000000000000000000000000000000000000000000000000000"
        "01000000000000004c4d7905b073c3f1141a95f18113055108729d74a81dc69a6d6bb6327d3241cd"
        "466d26bb7b0016f16c44203240e512d790571c6f55bea3730f7e78f6da7ce82c");
    require(encode_bound_wal_record({7, 3, 7, 0, command_payload(1)}, identity) == golden,
            "bound WAL encoder disagrees with independent golden");
    const auto decoded = decode_bound_wal_record(golden, identity);
    require(decoded.index == 7 && decoded.term == 3 && decoded.entry_id == 7 &&
            decoded.entry_type == 0 && decoded.payload == command_payload(1), "golden WAL fields changed");
    const auto block = frame_block(1, 1, golden);
    require(block[4] == std::byte{2} && block[28] == std::byte{0x97} &&
            block[29] == std::byte{0xee} && block[30] == std::byte{0x7b} && block[31] == std::byte{0x9e},
            "block-v2 version/independent CRC changed");
    for (const auto version : {1, 3}) {
        auto foreign_version = block;
        foreign_version[4] = std::byte(version);
        bool refused{};
        try { (void)decode_block(foreign_version); }
        catch (const BoundWalError& error) { refused = error.code() == BoundFrameError::unsupported_schema; }
        require(refused, "unsupported WAL block version collapsed to ordinary corrupt tail");
    }
}

void test_complete_corruption_is_not_an_empty_segment(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    auto bytes = frame_block(1, 1, bound_record(1));
    bytes[kWalBlockHeaderBytes + 60] ^= std::byte{1};
    const auto path = root / "v2-wal-blocks.bin";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    bool refused{};
    try { (void)WalSegment::read_existing_records(root, wal_identity()); }
    catch (const BoundWalError& error) { refused = error.code() == BoundFrameError::invalid_payload; }
    require(refused, "a complete corrupt WAL block was accepted as an empty segment");
    refused = false;
    try { WalSegment segment(root, wal_identity()); }
    catch (const BoundWalError& error) { refused = error.code() == BoundFrameError::invalid_payload; }
    require(refused, "writable open accepted a complete corrupt WAL block");
    std::ifstream input(path, std::ios::binary);
    std::vector<std::byte> after(bytes.size());
    input.read(reinterpret_cast<char*>(after.data()), after.size());
    require(after == bytes && std::filesystem::file_size(path) == bytes.size(),
            "corrupt block refusal changed diagnostic evidence");
}

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
    const auto big = record(kWalBlockPayloadBytes - 16, 0x11);
    const auto also_big = record(64, 0x22);
    require(arena.stage(big, 1) == StageOutcome::staged, "first record refused");
    require(arena.stage(also_big, 2) == StageOutcome::staged, "second record refused");

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

    require(arena.stage(record(256, 0x77), 2) == StageOutcome::staged, "stage failed");
    arena.seal_open_block();
    const auto second = arena.next_sealed();
    require(second.has_value(), "no second sealed block");
    require(second->slot == first->slot, "the fixture did not reuse the slot");
    require(second->generation != first->generation, "the slot kept its generation");

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

    require(arena.durable_index() == 0, "staging alone advanced the durable index");

    Writer writer{&arena, {}};
    const auto first = arena.next_sealed();
    require(first.has_value() && first->highest_record_index == 5, "wrong order");

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
    std::uint64_t index = 0;
    for (std::size_t i = 0; i < kWalArenaBlocks; ++i) {
        require(arena.stage(record(kWalBlockPayloadBytes, 0xAA), ++index) ==
                    StageOutcome::staged,
                "stage failed before the arena was full");
    }
    arena.seal_open_block();
    require(arena.free_blocks() == 0, "the arena did not actually fill");

    require(arena.stage(record(64, 0xBB), ++index) == StageOutcome::backpressure,
            "a full arena accepted another record");
    require(arena.durable_index() == 0, "a refused record advanced durability");

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
    std::filesystem::create_directories(root);

    const auto staged_record = [](const std::uint64_t index) { return bound_record(index, 1); };

    struct Observation {
        WalBlockArena* arena;
        std::size_t calls;
        std::uint64_t durable_at_sync;
    };

    WalBlockArena arena;
    require(arena.stage(staged_record(1), 1) == StageOutcome::staged, "stage failed");
    arena.seal_open_block();

    Observation seen{&arena, 0, 0};
    WalSegment segment(root, wal_identity(), {[](StorageCheckpoint at, void* context) {
                                  if (at != StorageCheckpoint::before_file_sync) return;
                                  auto& observation = *static_cast<Observation*>(context);
                                  ++observation.calls;
                                  observation.durable_at_sync =
                                      observation.arena->durable_index();
                              },
                              &seen});

    require(segment.flush(arena) == 1, "flush wrote nothing");
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

void test_synchronous_flush_writes_each_sealed_block_once_in_order(
    const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    WalBlockArena arena;
    for (std::uint64_t index = 1; index <= 3; ++index) {
        require(arena.stage(bound_record(index), index) ==
                    StageOutcome::staged,
                "multi-block fixture stage refused");
        arena.seal_open_block();
    }
    require(arena.sealed_count() == 3,
            "multi-block fixture did not seal three distinct blocks");

    struct Observation {
        WalBlockArena* arena;
        std::size_t sync_calls{};
        std::uint64_t durable_at_sync{};
    } seen{&arena};
    WalSegment segment(root, wal_identity(), {[](StorageCheckpoint at, void* context) {
                                  if (at != StorageCheckpoint::before_file_sync) return;
                                  auto& observation =
                                      *static_cast<Observation*>(context);
                                  ++observation.sync_calls;
                                  observation.durable_at_sync =
                                      observation.arena->durable_index();
                              },
                              &seen});
    require(segment.flush(arena) == 3,
            "synchronous batch did not flush every sealed block");
    require(seen.sync_calls == 1 && seen.durable_at_sync == 0,
            "synchronous batch did not use one sync before all durable progress");
    require(arena.sealed_count() == 0,
            "synchronous batch left a sealed block behind");
    require(arena.durable_index() == 3,
            "synchronous batch did not advance durability through its last record");

    const auto recovered = segment.read_records();
    require(recovered.size() == 3,
            "synchronous batch recovered the wrong record count");
    for (std::size_t offset = 0; offset < recovered.size(); ++offset) {
        const auto expected_index = static_cast<std::uint64_t>(offset + 1);
        require(recovered[offset].index == expected_index,
                "synchronous batch duplicated or reordered a record");
        require(recovered[offset].payload == command_payload(expected_index),
                "synchronous batch changed distinct record bytes");
    }
}

void test_failed_synchronous_batch_can_retry_without_duplicates(
    const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    WalBlockArena arena;
    for (std::uint64_t index = 1; index <= 2; ++index) {
        require(arena.stage(bound_record(index), index) ==
                    StageOutcome::staged,
                "retry fixture stage refused");
        arena.seal_open_block();
    }

    bool fail_once = true;
    WalSegment segment(root, wal_identity(), {[](StorageCheckpoint at, void* context) {
                                  auto& armed = *static_cast<bool*>(context);
                                  if (at == StorageCheckpoint::before_file_sync && armed) {
                                      armed = false;
                                      throw std::runtime_error("injected sync failure");
                                  }
                              },
                              &fail_once});
    bool failed = false;
    try {
        (void)segment.flush(arena);
    } catch (const std::runtime_error& error) {
        failed = std::string(error.what()) == "injected sync failure";
    }
    require(failed, "synchronous batch did not exercise the injected sync failure");
    require(arena.sealed_count() == 2 && arena.durable_index() == 0,
            "failed synchronous batch released or lost staged blocks");

    require(segment.flush(arena) == 2,
            "synchronous batch could not retry after a sync failure");
    const auto recovered = segment.read_records();
    require(recovered.size() == 2 && recovered[0].index == 1 &&
                recovered[1].index == 2,
            "retry duplicated, lost, or reordered synchronous WAL records");
    require(recovered[0].payload == command_payload(1) &&
                recovered[1].payload == command_payload(2),
            "retry changed synchronous WAL record bytes");
    require(arena.durable_index() == 2,
            "retry did not advance durability through the batch");
}

void test_failed_second_block_write_can_retry_without_duplicates(
    const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    WalBlockArena arena;
    for (std::uint64_t index = 1; index <= 3; ++index) {
        require(arena.stage(bound_record(index), index) ==
                    StageOutcome::staged,
                "write-failure fixture stage refused");
        arena.seal_open_block();
    }

    std::size_t writes = 0;
    WalSegment segment(root, wal_identity(), {[](StorageCheckpoint at, void* context) {
                                  if (at != StorageCheckpoint::before_block_write) return;
                                  auto& count = *static_cast<std::size_t*>(context);
                                  if (++count == 2) {
                                      throw std::runtime_error("injected second-block write failure");
                                  }
                              },
                              &writes});
    bool failed = false;
    try {
        (void)segment.flush(arena);
    } catch (const std::runtime_error& error) {
        failed = std::string(error.what()) ==
                 "injected second-block write failure";
    }
    require(failed, "synchronous batch did not fail on the second block write");
    require(arena.sealed_count() == 3 && arena.durable_index() == 0,
            "failed block write released or lost staged blocks");

    require(segment.flush(arena) == 3,
            "synchronous batch could not retry after a block write failure");
    const auto recovered = segment.read_records();
    require(recovered.size() == 3,
            "write-failure retry recovered the wrong record count");
    for (std::size_t offset = 0; offset < recovered.size(); ++offset) {
        const auto expected = static_cast<std::uint64_t>(offset + 1);
        require(recovered[offset].index == expected &&
                    recovered[offset].payload == command_payload(expected),
                "write-failure retry duplicated, reordered, or changed record bytes");
    }
    require(arena.durable_index() == 3,
            "write-failure retry did not advance durability through the batch");
}

void test_synchronous_flush_refuses_submitted_writer_work(
    const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    WalBlockArena arena;
    require(arena.stage(bound_record(1), 1) ==
                StageOutcome::staged,
            "mixed-owner fixture stage refused");
    arena.seal_open_block();
    require(arena.next_unsubmitted().has_value(),
            "mixed-owner fixture did not submit its block");

    WalSegment segment(root, wal_identity());
    bool refused = false;
    try {
        (void)segment.flush(arena);
    } catch (const std::logic_error& error) {
        refused = std::string(error.what()) ==
                  "synchronous WAL flush cannot mix with submitted writer work";
    }
    require(refused, "synchronous flush silently mixed with writer-owned work");
    require(segment.read_records().empty() && arena.durable_index() == 0,
            "mixed-owner refusal wrote or credited WAL bytes");
}

void test_preallocated_space_does_not_read_back_as_data(
    const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    const auto staged_record = [](const std::uint64_t index) { return bound_record(index, 1); };

    WalBlockArena arena;
    require(arena.stage(staged_record(1), 1) == StageOutcome::staged, "stage failed");
    arena.seal_open_block();

    {
        WalSegment segment(root, wal_identity());
        require(segment.flush(arena) == 1, "flush wrote nothing");
    }

    const auto size = std::filesystem::file_size(root / "v2-wal-blocks.bin");
    require(size > kWalBlockBytes,
            "the segment was not preallocated beyond the block it wrote");

    WalSegment reopened(root, wal_identity());
    const auto records = reopened.read_records();
    require(records.size() == 1,
            "recovery read " + std::to_string(records.size()) +
                " records from a segment holding one, so reserved space is being "
                "read as data");
    require(records.front().index == 1, "the recovered record is not the one written");

    WalBlockArena more;
    require(more.stage(staged_record(2), 2) == StageOutcome::staged, "stage failed");
    more.seal_open_block();
    require(reopened.flush(more) == 1, "second flush wrote nothing");

    WalSegment again(root, wal_identity());
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
            WalSegment segment(dir, wal_identity());
            for (std::uint64_t index = 1; index <= 3; ++index) {
                require(arena.stage(bound_record(index, 1), index) == StageOutcome::staged,
                        "rewrite fixture stage refused");
                arena.seal_open_block();
                segment.flush(arena);
            }
        }
        auto selected = point;
        const auto child = ::fork();
        require(child >= 0, "fork failed");
        if (child == 0) {
            WalSegment segment(dir, wal_identity(), {[](StorageCheckpoint at, void* context) {
                if (at == *static_cast<StorageCheckpoint*>(context)) ::_exit(77);
            }, &selected});
            segment.discard_through(1);
            ::_exit(0);
        }
        int status = 0;
        require(::waitpid(child, &status, 0) == child && WIFEXITED(status), "child failed");
        require(WEXITSTATUS(status) == 77, "rewrite checkpoint was not exercised");
        const auto recovered = WalSegment(dir, wal_identity()).read_records();
        const bool old = recovered.size() == 3 && recovered[0].index == 1 &&
                         recovered[1].index == 2 && recovered[2].index == 3;
        const bool replaced = recovered.size() == 2 && recovered[0].index == 2 &&
                              recovered[1].index == 3;
        require(old || replaced, "rewrite crash recovered a mixed/duplicated history");
        for (const auto& value : recovered)
            require(value.payload == command_payload(value.index),
                    "rewrite crash changed record bytes");
    }
}

}

int main() {
    try {
        test_bound_record_codec_and_lineage_refusals();
        test_independent_bound_wal_golden_and_block_version();
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
            test_synchronous_flush_writes_each_sealed_block_once_in_order(
                root / "multi-block-flush");
            test_complete_corruption_is_not_an_empty_segment(root / "corrupt-complete");
            test_failed_synchronous_batch_can_retry_without_duplicates(
                root / "multi-block-retry");
            test_failed_second_block_write_can_retry_without_duplicates(
                root / "multi-block-write-retry");
            test_synchronous_flush_refuses_submitted_writer_work(
                root / "mixed-writer-ownership");
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
