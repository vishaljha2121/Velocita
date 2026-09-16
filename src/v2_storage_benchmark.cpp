// Phase 7 storage measurement.
//
// Phase 7's gate puts correctness and power-cut evidence before throughput, and
// both now pass, so this measures rather than claims. What it measures is the
// thing the phase set out to change: how long the OWNER LOOP is held by the
// storage path, separated from how long the disk takes.
//
// It deliberately reports both a synchronous and an off-thread figure, because
// the number that matters is the difference between them. A single absolute
// throughput number would say more about this laptop than about the design, and
// the deployment targets are a Pi and a NUC.

#include "velocita/v2/wal_arena.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

using namespace velocita::v2;
using Clock = std::chrono::steady_clock;

std::vector<std::byte> command_sized_record(const std::uint64_t index) {
    velocita::WalRecord value;
    value.index = index;
    value.term = 1;
    value.entry_id = static_cast<std::int32_t>(index);
    value.entry_type = 0;
    // Representative of a real canonical command frame rather than a token
    // payload; a benchmark on 8-byte records would measure the framing, not the
    // work.
    value.payload.assign(512, std::byte{0x5A});
    return velocita::encode_wal_record(value);
}

struct Summary {
    double median_us{};
    double p99_us{};
    double max_us{};
};

Summary summarise(std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    const auto at = [&](const double q) {
        const auto position = static_cast<std::size_t>(q * static_cast<double>(
                                                               samples.size() - 1));
        return samples[position];
    };
    return {at(0.5), at(0.99), samples.back()};
}

void report(const char* label, const Summary& summary) {
    std::cout << label << "_median_us=" << summary.median_us << "\n"
              << label << "_p99_us=" << summary.p99_us << "\n"
              << label << "_max_us=" << summary.max_us << "\n";
}

}  // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      "velocita-v2-storage-benchmark";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    constexpr std::uint64_t kRecords = 2000;

    // 1. SYNCHRONOUS: the owner does the write and the fsync itself. This is
    //    what the owner loop paid before the writer thread existed.
    std::vector<double> inline_samples;
    inline_samples.reserve(kRecords);
    {
        WalBlockArena arena;
        WalSegment segment(root / "inline");
        for (std::uint64_t index = 1; index <= kRecords; ++index) {
            const auto record = command_sized_record(index);
            const auto start = Clock::now();
            (void)arena.stage(record, index);
            arena.seal_open_block();
            (void)segment.flush(arena);
            const auto elapsed = Clock::now() - start;
            inline_samples.push_back(
                std::chrono::duration<double, std::micro>(elapsed).count());
        }
    }

    // 2. OFF-THREAD: the owner stages, hands over, and collects completions.
    //    The fsync happens on the writer. The sample is the OWNER's time only,
    //    which is the number this phase set out to reduce.
    std::vector<double> owner_samples;
    owner_samples.reserve(kRecords);
    std::uint64_t refusals = 0;
    {
        WalBlockArena arena;
        WalSegment segment(root / "threaded");
        WalWriter writer(segment, arena);
        writer.start();
        for (std::uint64_t index = 1; index <= kRecords; ++index) {
            const auto record = command_sized_record(index);
            const auto start = Clock::now();
            // RETRY ON BACKPRESSURE, because a real caller must. Counting a
            // refusal as a fast sample would measure the arena declining work
            // rather than the owner loop doing it - the first run of this
            // benchmark did exactly that and reported a number four fifths of
            // which was refusals.
            while (arena.stage(record, index) != StageOutcome::staged) {
                ++refusals;
                BlockDescriptor spin{};
                while (writer.take_completion(spin)) (void)arena.complete(spin);
                while (const auto descriptor = arena.next_unsubmitted()) {
                    if (!writer.submit(*descriptor)) break;
                }
            }
            arena.seal_open_block();
            while (const auto descriptor = arena.next_unsubmitted()) {
                if (!writer.submit(*descriptor)) break;
            }
            BlockDescriptor finished{};
            while (writer.take_completion(finished)) (void)arena.complete(finished);
            const auto elapsed = Clock::now() - start;
            owner_samples.push_back(
                std::chrono::duration<double, std::micro>(elapsed).count());
        }
        // Drain, so the durable index below is the real one.
        for (int spin = 0; spin < 100'000 && arena.durable_index() < kRecords; ++spin) {
            BlockDescriptor finished{};
            while (writer.take_completion(finished)) (void)arena.complete(finished);
            while (const auto descriptor = arena.next_unsubmitted()) {
                if (!writer.submit(*descriptor)) break;
            }
        }
        writer.stop();
        BlockDescriptor finished{};
        while (writer.take_completion(finished)) (void)arena.complete(finished);

        std::cout << "records=" << kRecords << "\n"
                  << "block_bytes=" << kWalBlockBytes << "\n"
                  << "arena_blocks=" << kWalArenaBlocks << "\n"
                  << "backpressure_waits=" << refusals << "\n"
                  << "durable_index=" << arena.durable_index() << "\n";
    }

    const auto inline_summary = summarise(inline_samples);
    const auto owner_summary = summarise(owner_samples);
    report("owner_loop_synchronous", inline_summary);
    report("owner_loop_offthread", owner_summary);
    std::cout << "median_speedup="
              << (owner_summary.median_us > 0
                      ? inline_summary.median_us / owner_summary.median_us
                      : 0)
              << "\n";

    std::filesystem::remove_all(root);
    return 0;
}
