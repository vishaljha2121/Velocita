#include "velocita/v2/wal_arena.hpp"
#include "v2_identity_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace allocation_probe {
thread_local bool enabled{};
thread_local std::size_t largest{};
}

void* operator new(const std::size_t bytes) {
    if (allocation_probe::enabled) allocation_probe::largest = std::max(allocation_probe::largest, bytes);
    if (void* value = std::malloc(bytes ? bytes : 1)) return value;
    throw std::bad_alloc{};
}
void* operator new[](const std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace velocita::v2;
constexpr std::size_t kAllocationCeiling = 2 * kWalBlockBytes;
constexpr std::uint64_t kRecordCount = 512;

void require(const bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

const VerifiedStaticIdentity& identity() {
    static const auto value = identity_test::verified_identity(
        identity_test::identity(1, {1, 2, 3}));
    return value;
}

std::vector<std::byte> record(const std::uint64_t index) {
    Command command;
    command.identity = {CallerId{1}, RequestSequence{index}, {}};
    command.identity.payload_digest.bytes[0] = std::byte{1};
    command.identity.payload_digest.bytes[1] = static_cast<std::byte>(index);
    command.body = AdvanceKillEpoch{KillEpoch{index}};
    std::array<std::byte, kMaxKernelCommandBytes> command_bytes{};
    const auto encoded = encode_kernel_command(command, command_bytes);
    require(encoded.error == KernelCodecError::none, "fixture command encode failed");
    return encode_bound_wal_record(
        {index, 7, static_cast<std::int32_t>(index), 0,
         {command_bytes.begin(), command_bytes.begin() +
              static_cast<std::ptrdiff_t>(encoded.bytes_written)}},
        identity());
}

void write_history(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    WalSegment segment(root, identity());
    WalBlockArena arena;
    for (std::uint64_t index = 1; index <= kRecordCount; ++index) {
        const auto bytes = record(index);
        require(arena.stage(bytes, index) == StageOutcome::staged, "fixture stage failed");
        arena.seal_open_block();
        require(segment.flush(arena) == 1, "fixture flush failed");
    }
}

std::filesystem::path unique_test_root() {
    auto pattern = (std::filesystem::temp_directory_path() /
                    "velocita-v2-wal-streaming-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    const auto* created = ::mkdtemp(writable.data());
    if (created == nullptr) throw std::runtime_error("mkdtemp failed");
    return created;
}

void require_exact_history(const std::filesystem::path& root,
                           const std::uint64_t first,
                           const std::uint64_t last) {
    const auto records = WalSegment::read_existing_records(root, identity());
    const auto expected_count = first <= last ? last - first + 1 : 0;
    require(records.size() == expected_count, "rewrite changed surviving record count");
    for (std::size_t offset = 0; offset < records.size(); ++offset) {
        const auto index = first + offset;
        require(records[offset].index == index &&
                    encode_bound_wal_record(records[offset], identity()) == record(index),
                "rewrite changed surviving canonical record bytes or order");
    }
}

void test_constructor_and_rewrite_have_bounded_single_allocations(
    const std::filesystem::path& root) {
    write_history(root);

    allocation_probe::largest = 0;
    allocation_probe::enabled = true;
    {
        WalSegment reopened(root, identity());
    }
    allocation_probe::enabled = false;
    require(allocation_probe::largest <= kAllocationCeiling,
            "WAL constructor retained whole history in one growing allocation");

    std::uint64_t visited = 0;
    allocation_probe::largest = 0;
    allocation_probe::enabled = true;
    WalSegment::visit_existing_records(root, identity(), [&](const velocita::WalRecord& value) {
        require(value.index == ++visited, "streaming visitor changed record order");
    });
    allocation_probe::enabled = false;
    require(visited == kRecordCount && allocation_probe::largest <= kAllocationCeiling,
            "WAL visitor exceeded the bounded single-allocation threshold");

    WalSegment segment(root, identity());
    allocation_probe::largest = 0;
    allocation_probe::enabled = true;
    segment.truncate_from(kRecordCount / 2 + 1);
    allocation_probe::enabled = false;
    require(allocation_probe::largest <= kAllocationCeiling,
            "WAL rewrite retained whole history in one growing allocation");

    require_exact_history(root, 1, kRecordCount / 2);

    // A no-op suffix request must leave every canonical survivor unchanged.
    segment.truncate_from(kRecordCount + 1);
    require_exact_history(root, 1, kRecordCount / 2);

    segment.discard_through(kRecordCount / 4);
    require_exact_history(root, kRecordCount / 4 + 1, kRecordCount / 2);

    // A no-op prefix request likewise preserves the complete canonical bytes.
    segment.discard_through(1);
    require_exact_history(root, kRecordCount / 4 + 1, kRecordCount / 2);

    segment.discard_through(kRecordCount / 2);
    require_exact_history(root, 1, 0);
}
}

int main() {
    try {
        const auto root = unique_test_root();
        test_constructor_and_rewrite_have_bounded_single_allocations(root);
        std::filesystem::remove_all(root);
        std::cout << "WAL streaming tests passed\n";
    } catch (const std::exception& error) {
        allocation_probe::enabled = false;
        std::cerr << "WAL streaming tests failed: " << error.what() << '\n';
        return 1;
    }
}
