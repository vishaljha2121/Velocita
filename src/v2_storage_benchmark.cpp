// Measures write-ahead-log durability throughput.
#include "velocita/v2/wal_arena.hpp"
#include "velocita/v2/bound_wal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {

using namespace velocita::v2;
using Clock = std::chrono::steady_clock;

const VerifiedStaticIdentity& benchmark_identity() {
    static const auto result = [] {
        StaticIdentity value;
        value.incarnation[0] = std::byte{1};
        value.catalog_root.bytes[0] = std::byte{1};
        value.voter_count = 1;
        value.voters[0] = 1;
        value.local_node_id = 1;
        std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
        const auto encoded = encode_static_identity(value, bytes);
        VerifiedStaticIdentity verified;
        if (encoded.error != StaticIdentityCodecError::none ||
            VerifiedStaticIdentity::decode(std::span{bytes}.first(encoded.bytes_written), verified) !=
                StaticIdentityCodecError::none)
            throw std::runtime_error("benchmark identity fixture refused");
        return verified;
    }();
    return result;
}

std::vector<std::byte> command_sized_record(const std::uint64_t index) {
    velocita::WalRecord value;
    value.index = index;
    value.term = 1;
    value.entry_id = static_cast<std::int32_t>(index);
    value.entry_type = 0;
    Command command;
    command.identity = {CallerId{1}, RequestSequence{index}, {}};
    command.identity.payload_digest.bytes[0] = static_cast<std::byte>(index);
    command.identity.payload_digest.bytes[1] = std::byte{0x5a};
    command.body = AdvanceKillEpoch{KillEpoch{index}};
    std::array<std::byte, kMaxKernelCommandBytes> payload{};
    const auto encoded = encode_kernel_command(command, payload);
    if (encoded.error != KernelCodecError::none) throw std::runtime_error("benchmark command refused");
    value.payload.assign(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written));
    return encode_bound_wal_record(value, benchmark_identity());
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

}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      "velocita-v2-storage-benchmark";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::filesystem::create_directory(root / "inline");
    std::filesystem::create_directory(root / "threaded");

    constexpr std::uint64_t kRecords = 2000;

    std::vector<double> inline_samples;
    inline_samples.reserve(kRecords);
    {
        WalBlockArena arena;
        WalSegment segment(root / "inline", benchmark_identity());
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

    std::vector<double> owner_samples;
    owner_samples.reserve(kRecords);
    std::uint64_t refusals = 0;
    {
        WalBlockArena arena;
        WalSegment segment(root / "threaded", benchmark_identity());
        WalWriter writer(segment, arena);
        writer.start();
        for (std::uint64_t index = 1; index <= kRecords; ++index) {
            const auto record = command_sized_record(index);
            const auto start = Clock::now();
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
