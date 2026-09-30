#include "velocita/v2/retained_log.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace allocation_probe { thread_local bool enabled{}; thread_local std::size_t count{}; }
void* operator new(std::size_t bytes) {
    if (allocation_probe::enabled) ++allocation_probe::count;
    if (void* value = std::malloc(bytes ? bytes : 1)) return value;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace velocita::v2;

void require(const bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::array<std::byte, 3> bytes(const unsigned value) {
    return {std::byte{static_cast<unsigned char>(value)}, std::byte{0x5a}, std::byte{0xa5}};
}

void test_exact_capacity_and_one_more() {
    RetainedLog log;
    const std::array<std::byte, 1> payload{std::byte{0x31}};
    for (std::uint64_t index = 1; index <= kRetainedLogOrdinaryEntries; ++index)
        require(log.append(index, payload, false), "ordinary exact capacity refused");
    require(!log.append(kRetainedLogOrdinaryEntries + 1, payload, false),
            "ordinary entry borrowed protected capacity");
    for (std::uint64_t index = kRetainedLogOrdinaryEntries + 1;
         index <= kRetainedLogEntries; ++index)
        require(log.append(index, payload, true), "protected exact capacity refused");
    require(!log.append(kRetainedLogEntries + 1, payload, true),
            "retained log exceeded physical capacity");
    require(log.size() == kRetainedLogEntries && log.live_bytes() == kRetainedLogEntries &&
                log.ordinary_count() == kRetainedLogOrdinaryEntries &&
                log.protected_count() == kRetainedLogProtectedEntries,
            "saturated counters are wrong");
}

void test_control_priority_spill_and_actual_pool_reuse() {
    RetainedLog log;
    for (std::uint64_t index = 1; index <= kRetainedLogProtectedEntries; ++index)
        require(log.append(index, bytes(static_cast<unsigned>(index)), true),
                "protected fill failed");
    require(log.append(kRetainedLogProtectedEntries + 1, bytes(65), true),
            "control did not spill into ordinary pool");
    require(log.find(1)->pool == RetainedLogPool::protected_control &&
                log.find(kRetainedLogProtectedEntries + 1)->pool == RetainedLogPool::ordinary,
            "descriptor did not retain actual pool ownership");
    require(log.erase(1), "head protected erase failed");
    require(log.append(kRetainedLogProtectedEntries + 2, bytes(66), true),
            "control did not reuse released protected token");
    require(log.find(kRetainedLogProtectedEntries + 2)->pool ==
                RetainedLogPool::protected_control,
            "released actual pool was not reused");
}

void test_hypothetical_suffix_is_read_only_and_pool_exact() {
    RetainedLog log;
    const std::array<std::byte, 1> payload{std::byte{0x7f}};
    for (std::uint64_t index = 1; index <= kRetainedLogOrdinaryEntries; ++index)
        require(log.append(index, payload, false), "ordinary fixture fill failed");
    for (std::uint64_t index = kRetainedLogOrdinaryEntries + 1;
         index <= kRetainedLogEntries; ++index)
        require(log.append(index, payload, true), "protected fixture fill failed");

    const std::array<bool, 1> ordinary{false};
    const std::array<bool, 1> control{true};
    const std::array<bool, 3> three_ordinary{false, false, false};
    const auto suffix = kRetainedLogOrdinaryEntries + 1;
    const auto before_bytes = log.live_bytes();
    const auto* before = log.payload(suffix).data();
    require(!log.can_admit(ordinary, suffix),
            "ordinary plan borrowed hypothetically released protected token");
    require(log.can_admit(control, suffix),
            "control plan ignored hypothetically released protected tokens");
    require(!log.can_admit(three_ordinary, suffix - 2),
            "plan larger than hypothetically available physical cells was accepted");
    require(log.size() == kRetainedLogEntries && log.live_bytes() == before_bytes &&
                log.find(suffix) != nullptr && log.payload(suffix).data() == before,
            "admission preflight mutated retained history");
    require(!log.can_admit({}, kRetainedLogEntries + 2),
            "nonexistent hypothetical suffix was accepted");
}

void test_modulo_wrap_stable_spans_and_mutable_lookup() {
    RetainedLog log;
    const auto first_bytes = bytes(1);
    const auto second_bytes = bytes(2);
    require(log.append(kRetainedLogEntries - 1, first_bytes, false), "wrap first append");
    require(log.append(kRetainedLogEntries, second_bytes, false,
                       ResultHandle{9, 7}), "wrap second append");
    auto second = log.payload(kRetainedLogEntries);
    auto* stable = second.data();
    require(second.size() == second_bytes.size() && second[0] == second_bytes[0],
            "payload lookup changed bytes");
    auto* descriptor = log.find(kRetainedLogEntries);
    require(descriptor && descriptor->result == ResultHandle{9, 7},
            "result handle was not retained");
    descriptor->result = ResultHandle{10, 8};
    require(log.find(kRetainedLogEntries)->result == ResultHandle{10, 8},
            "mutable descriptor result lookup failed");
    require(log.erase(kRetainedLogEntries - 1), "wrapped head erase failed");
    for (std::uint64_t index = kRetainedLogEntries + 1;
         index <= 2 * kRetainedLogEntries - 1; ++index)
        require(log.append(index, bytes(static_cast<unsigned>(index)),
                           index > kRetainedLogEntries + kRetainedLogOrdinaryEntries - 1),
                "wrapped cell-reuse fill failed");
    require(log.payload(kRetainedLogEntries).data() == stable &&
                log.payload(kRetainedLogEntries)[0] == second_bytes[0],
            "unreleased payload address or bytes moved across modulo reuse");
    require(log.payload(2 * kRetainedLogEntries - 1).data() != nullptr &&
                log.payload(2 * kRetainedLogEntries - 1)[0] ==
                    bytes(static_cast<unsigned>(2 * kRetainedLogEntries - 1))[0] &&
                first_bytes[0] == std::byte{1},
            "freed physical cell was not safely reused with copied new bytes");
    require(log.find(kRetainedLogEntries + 1) != nullptr &&
                log.find(1) == nullptr,
            "lookup failed full-index verification");
    require(log.at_offset(0)->index == kRetainedLogEntries &&
                log.at_offset(1)->index == kRetainedLogEntries + 1 &&
                log.at_offset(kRetainedLogEntries - 1)->index ==
                    2 * kRetainedLogEntries - 1 &&
                log.at_offset(kRetainedLogEntries) == nullptr,
            "bounded retained-entry iteration changed order or bounds");
}

void test_invalid_index_payload_contiguity_and_erase() {
    RetainedLog log;
    const auto payload = bytes(4);
    std::array<std::byte, kRetainedLogCellBytes + 1> oversized{};
    require(!log.append(0, payload, false), "entry zero accepted");
    require(!log.append(1, {}, false), "empty payload accepted");
    require(!log.append(1, oversized, false), "oversized payload accepted");
    require(log.append(7, payload, false), "first arbitrary positive index refused");
    require(!log.append(9, payload, false), "noncontiguous append accepted");
    require(log.append(8, payload, false), "contiguous append refused");
    require(log.append(9, payload, false), "third append refused");
    require(!log.erase(8), "interior erase accepted");
    require(!log.erase(0) && !log.erase(10), "invalid erase acted");
    require(log.erase(9) && log.erase(7), "tail/head erase failed");
    require(log.size() == 1 && log.first_index() == 8 && log.last_index() == 8,
            "head/tail erase corrupted interval");
    require(log.erase(8) && log.empty() && log.live_bytes() == 0,
            "last erase did not clear counters");

    require(log.append(11, payload, true), "clear fixture append failed");
    log.clear();
    require(log.empty() && log.live_bytes() == 0 && log.ordinary_count() == 0 &&
                log.protected_count() == 0 && log.find(11) == nullptr,
            "clear did not reset retained ownership");

    RetainedLog overflow;
    require(overflow.append(std::numeric_limits<std::uint64_t>::max(), payload, false),
            "maximum full index refused as first entry");
    require(!overflow.append(1, payload, false), "full index append wrapped");
}

void test_no_postconstruction_allocations() {
    static_assert(!std::is_copy_constructible_v<RetainedLog> &&
                  !std::is_move_constructible_v<RetainedLog>);
    static_assert(noexcept(std::declval<RetainedLog&>().append(
        1, std::declval<std::span<const std::byte>>(), false)));
    static_assert(noexcept(std::declval<const RetainedLog&>().can_admit(
        std::declval<std::span<const bool>>(), std::nullopt)));

    RetainedLog log;
    const auto value = bytes(8);
    const std::array<bool, 1> one_control{true};
    allocation_probe::count = 0;
    allocation_probe::enabled = true;
    const bool appended = log.append(1, value, true);
    const bool found = log.find(1) != nullptr && !log.payload(1).empty();
    const bool feasible = log.can_admit(one_control, 1);
    const bool erased = log.erase(1);
    log.clear();
    allocation_probe::enabled = false;
    require(appended && found && feasible && erased && allocation_probe::count == 0,
            "retained-log operation allocated after construction");
}
}

int main() {
    try {
        test_exact_capacity_and_one_more();
        test_control_priority_spill_and_actual_pool_reuse();
        test_hypothetical_suffix_is_read_only_and_pool_exact();
        test_modulo_wrap_stable_spans_and_mutable_lookup();
        test_invalid_index_payload_contiguity_and_erase();
        test_no_postconstruction_allocations();
        std::cout << "retained log tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "retained log tests failed: " << error.what() << '\n';
        return 1;
    }
}
