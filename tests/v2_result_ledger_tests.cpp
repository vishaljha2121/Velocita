#include "velocita/v2/result_ledger.hpp"

#include <cstdlib>
#include <array>
#include <iostream>
#include <limits>
#include <new>
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

namespace velocita::v2 {
struct ResultLedgerTestAccess {
    static void next_generation(ResultLedger& ledger, std::uint64_t value) {
        ledger.header_.next_generation = value;
    }
};
}

namespace {
using namespace velocita::v2;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
CommandIdentity identity(std::uint64_t sequence) {
    CommandIdentity value{CallerId{7}, RequestSequence{sequence}, {}};
    value.payload_digest.bytes[0] = static_cast<std::byte>(sequence);
    return value;
}
ResultBinding binding(std::int64_t index) {
    ResultBinding value{identity(static_cast<std::uint64_t>(index)), {}, 3, index};
    value.command_digest.bytes[0] = static_cast<std::byte>(index);
    return value;
}
CommittedResult result(const ResultBinding& owner, std::uint64_t version) {
    CommittedResult value;
    value.identity = owner.identity; value.term = owner.term; value.index = owner.index;
    value.result.state_version = version;
    return value;
}

void test_capacity_and_protection() {
    {
        ResultLedger priority;
        const auto first = priority.reserve(binding(1), true);
        require(first && first->slot >= kResultLedgerOrdinarySlots &&
                    priority.available_protected() == 63 &&
                    priority.available_ordinary() == 960,
                "control did not consume protected pool first");
        for (std::int64_t i = 2; i <= 64; ++i)
            require(priority.reserve(binding(i), true).has_value(), "protected fill");
        const auto spill = priority.reserve(binding(65), true);
        require(spill && spill->slot < kResultLedgerOrdinarySlots &&
                    priority.available_ordinary() == 959,
                "control did not spill into ordinary pool");
    }
    ResultLedger ledger;
    require(ledger.available_ordinary() == 960 && ledger.available_protected() == 64,
            "initial capacities");
    for (std::int64_t i = 1; i <= 960; ++i) require(ledger.reserve(binding(i), false).has_value(), "ordinary reserve");
    require(!ledger.reserve(binding(961), false), "ordinary borrowed protected slot");
    for (std::int64_t i = 961; i <= 1024; ++i) require(ledger.reserve(binding(i), true).has_value(), "protected reserve");
    require(!ledger.reserve(binding(1025), true), "ledger exceeded 1024 slots");
    require(ledger.reserved_count() == 1024 && ledger.ready_count() == 0, "saturation counts");
}

void test_binding_generation_fifo_and_copies() {
    ResultLedger ledger;
    auto a = binding(1); auto b = binding(2);
    const auto ah = ledger.reserve(a, false); const auto bh = ledger.reserve(b, false);
    require(ah && bh && ledger.matches(*ah, a) && !ledger.matches(*ah, b), "binding match");
    auto wrong_digest = a; wrong_digest.command_digest.bytes[1] = std::byte{1};
    require(!ledger.matches(*ah, wrong_digest), "digest-only mismatch matched");
    auto wrong_identity = result(a, 10); wrong_identity.identity = identity(9);
    require(!ledger.publish(*ah, wrong_identity) && ledger.reserved_count() == 2 &&
                ledger.ready_count() == 0, "wrong identity published or changed counts");
    auto wrong_term = result(a, 10); wrong_term.term = 9;
    require(!ledger.publish(*ah, wrong_term) && ledger.reserved_count() == 2 &&
                ledger.ready_count() == 0, "wrong term published or changed counts");
    auto wrong_index = result(a, 10); wrong_index.index = 9;
    require(!ledger.publish(*ah, wrong_index) && ledger.reserved_count() == 2 &&
                ledger.ready_count() == 0, "wrong index published or changed counts");
    require(!ledger.matches({0, ah->slot}, a) &&
                !ledger.matches({ah->generation, std::numeric_limits<std::uint16_t>::max()}, a) &&
                !ledger.publish({0, ah->slot}, result(a, 1)) &&
                !ledger.release_reserved({ah->generation, std::numeric_limits<std::uint16_t>::max()}),
            "invalid handle acted");
    require(ledger.publish(*ah, result(a, 10)) && ledger.publish(*bh, result(b, 20)), "publish");
    require(!ledger.matches(*ah, a), "ready result still matched as reservation");
    require(!ledger.release_reserved(*ah), "ready result rolled back");
    auto first = ledger.take(); auto second = ledger.take();
    require(first && second && first->index == 1 && second->index == 2, "FIFO order");
    const auto old_copy = *first;
    const auto reused = ledger.reserve(b, false);
    require(reused && reused->slot == bh->slot && reused->generation != bh->generation,
            "slot did not recycle with new generation");
    require(!ledger.matches(*bh, b) && ledger.matches(*reused, b),
            "stale and current identical bindings were not distinguished");
    require(!ledger.publish(*bh, result(b, 30)),
            "stale publish acted on identical binding");
    require(!ledger.release_reserved(*bh),
            "stale release acted on identical binding");
    require(ledger.publish(*reused, result(b, 30)), "current identical binding could not publish");
    require(old_copy.index == 1 && old_copy.result.state_version == 10, "taken copy changed after reuse");
}

void test_full_fifo_and_pool_recycling() {
    ResultLedger ledger;
    std::array<ResultBinding, kResultLedgerSlots> owners{};
    std::array<ResultHandle, kResultLedgerSlots> handles{};
    for (std::size_t offset = 0; offset < owners.size(); ++offset) {
        owners[offset] = binding(static_cast<std::int64_t>(offset + 1));
        const bool control = offset < kResultLedgerProtectedSlots;
        const auto handle = ledger.reserve(owners[offset], control);
        require(handle.has_value(), "full FIFO reserve failed");
        handles[offset] = *handle;
    }
    for (std::size_t offset = 0; offset < owners.size(); ++offset)
        require(ledger.publish(handles[offset], result(owners[offset], offset + 1)),
                "full FIFO publish failed");
    require(ledger.ready_count() == kResultLedgerSlots && ledger.reserved_count() == 0,
            "full FIFO counts before take");
    for (std::size_t offset = 0; offset < owners.size(); ++offset) {
        const auto taken = ledger.take();
        require(taken && taken->index == static_cast<std::int64_t>(offset + 1) &&
                    taken->result.state_version == offset + 1,
                "full FIFO order changed");
    }
    require(ledger.available_ordinary() == kResultLedgerOrdinarySlots &&
                ledger.available_protected() == kResultLedgerProtectedSlots &&
                ledger.ready_count() == 0 && ledger.reserved_count() == 0,
            "drain did not return slots to their original pools");
}

void test_release_invalid_binding_exhaustion_and_no_allocations() {
    ResultLedger ledger;
    require(!ledger.reserve(ResultBinding{identity(1), {}, 0, 1}, false), "zero term accepted");
    require(!ledger.reserve(ResultBinding{identity(1), {}, 1, 0}, false), "zero index accepted");
    auto owner = binding(1); auto handle = ledger.reserve(owner, false);
    require(handle && ledger.release_reserved(*handle) && !ledger.release_reserved(*handle), "reserved release");
    ResultLedgerTestAccess::next_generation(ledger, std::numeric_limits<std::uint64_t>::max());
    auto last = ledger.reserve(binding(2), false);
    require(last && last->generation == std::numeric_limits<std::uint64_t>::max(), "last generation unavailable");
    require(ledger.publish(*last, result(binding(2), 2)), "last generation could not publish");
    const auto last_result = ledger.take();
    require(last_result && last_result->index == 2 && last_result->result.state_version == 2,
            "generation exhaustion prevented ready result drain");
    require(!ledger.reserve(binding(3), false), "generation wrapped");
    require(ledger.available_ordinary() == 960 && ledger.available_protected() == 64 &&
                ledger.reserved_count() == 0 && ledger.ready_count() == 0,
            "exhausted reservation changed pool ownership");

    ResultLedger fresh;
    allocation_probe::count = 0; allocation_probe::enabled = true;
    const auto allocation_owner = binding(4);
    const auto h = fresh.reserve(allocation_owner, true);
    const bool matched = h && fresh.matches(*h, allocation_owner);
    const bool published = h && fresh.publish(*h, result(allocation_owner, 4));
    const auto taken = fresh.take();
    const auto released_handle = fresh.reserve(binding(5), false);
    const bool released = released_handle && fresh.release_reserved(*released_handle);
    allocation_probe::enabled = false;
    require(matched && published && taken && released && allocation_probe::count == 0,
            "ledger operation allocated");
}

void test_reservation_preflight() {
    ResultLedger ledger;
    std::array<bool, kResultLedgerOrdinarySlots> ordinary{};
    require(ledger.can_reserve(ordinary), "fresh ordinary preflight refused capacity");
    std::array<bool, kResultLedgerOrdinarySlots + 1> too_many_ordinary{};
    require(!ledger.can_reserve(too_many_ordinary), "ordinary preflight borrowed protected capacity");
    std::array<bool, kResultLedgerSlots> all_control{};
    all_control.fill(true);
    require(ledger.can_reserve(all_control), "control preflight did not include spill capacity");
    std::array<bool, kResultLedgerSlots + 1> oversized{};
    oversized.fill(true);
    require(!ledger.can_reserve(oversized), "oversized new reservation plan accepted");

    std::array<ResultHandle, kResultLedgerOrdinarySlots> ordinary_handles{};
    for (std::size_t i = 0; i < ordinary_handles.size(); ++i) {
        const auto handle = ledger.reserve(binding(static_cast<std::int64_t>(i + 1)), false);
        require(handle.has_value(), "ordinary preflight fixture reserve");
        ordinary_handles[i] = *handle;
    }
    const std::array<bool, 1> one_ordinary{false};
    const std::array<bool, 2> two_ordinary{false, false};
    const std::array<ResultHandle, 1> release_one{ordinary_handles[17]};
    require(ledger.can_reserve(one_ordinary, release_one), "released ordinary slot not simulated");
    require(!ledger.can_reserve(two_ordinary, release_one), "one released slot counted twice");
    require(ledger.matches(ordinary_handles[17], binding(18)) && ledger.reserved_count() == 960,
            "preflight mutated released reservation");

    std::array<ResultHandle, kResultLedgerProtectedSlots> protected_handles{};
    for (std::size_t i = 0; i < protected_handles.size(); ++i) {
        const auto handle = ledger.reserve(binding(static_cast<std::int64_t>(961 + i)), true);
        require(handle.has_value(), "protected preflight fixture reserve");
        protected_handles[i] = *handle;
    }
    const std::array<ResultHandle, 1> release_protected{protected_handles[3]};
    require(!ledger.can_reserve(one_ordinary, release_protected),
            "ordinary plan borrowed released protected slot");
    const std::array<bool, 1> one_control{true};
    require(ledger.can_reserve(one_control, release_protected),
            "control plan ignored released protected slot");
    require(ledger.can_reserve(one_control, release_one),
            "control plan did not spill into released ordinary slot");

    const std::array<ResultHandle, 2> duplicate_release{ordinary_handles[17], ordinary_handles[17]};
    require(!ledger.can_reserve({}, duplicate_release), "duplicate release handles accepted");
    require(!ledger.can_reserve({}, std::array<ResultHandle, 1>{{{0, ordinary_handles[0].slot}}}),
            "zero-generation release accepted");
    require(!ledger.can_reserve({}, std::array<ResultHandle, 1>{{
                {ordinary_handles[0].generation, std::numeric_limits<std::uint16_t>::max()}}}),
            "out-of-range release accepted");

    require(ledger.publish(ordinary_handles[0], result(binding(1), 1)), "ready fixture publish");
    require(!ledger.can_reserve({}, std::array<ResultHandle, 1>{{ordinary_handles[0]}}),
            "ready handle accepted as future release");
    require(ledger.release_reserved(ordinary_handles[1]), "stale fixture release");
    require(!ledger.can_reserve({}, std::array<ResultHandle, 1>{{ordinary_handles[1]}}),
            "stale free handle accepted as future release");

    std::array<ResultHandle, kResultLedgerSlots + 1> oversized_releases{};
    require(!ledger.can_reserve({}, oversized_releases), "oversized release plan accepted");
}

void test_preflight_generation_budget_and_no_allocations() {
    ResultLedger ledger;
    ResultLedgerTestAccess::next_generation(ledger,
        std::numeric_limits<std::uint64_t>::max() - 1);
    const std::array<bool, 2> two{false, true};
    const std::array<bool, 3> three{false, true, false};
    require(ledger.can_reserve(two) && !ledger.can_reserve(three),
            "preflight ignored remaining generation budget");
    require(ledger.available_ordinary() == kResultLedgerOrdinarySlots &&
                ledger.available_protected() == kResultLedgerProtectedSlots,
            "generation preflight mutated capacity");
    ResultLedgerTestAccess::next_generation(ledger, 0);
    require(ledger.can_reserve({}) && !ledger.can_reserve(std::array<bool, 1>{false}),
            "exhausted generation handling changed empty/nonempty plan semantics");

    ResultLedger fresh;
    const auto handle = fresh.reserve(binding(1), false);
    require(handle.has_value(), "allocation preflight fixture");
    const std::array<ResultHandle, 1> released{*handle};
    allocation_probe::count = 0;
    allocation_probe::enabled = true;
    const bool feasible = fresh.can_reserve(two, released);
    allocation_probe::enabled = false;
    require(feasible && allocation_probe::count == 0, "preflight allocated");
}
}

static_assert(!std::is_copy_constructible_v<ResultLedger> && !std::is_move_constructible_v<ResultLedger>);
static_assert(noexcept(std::declval<ResultLedger&>().publish({}, std::declval<const CommittedResult&>())));
static_assert(noexcept(std::declval<ResultLedger&>().reserve({}, false)));
static_assert(noexcept(std::declval<ResultLedger&>().matches({}, {})));
static_assert(noexcept(std::declval<ResultLedger&>().release_reserved({})));
static_assert(noexcept(std::declval<ResultLedger&>().take()));
static_assert(noexcept(std::declval<const ResultLedger&>().can_reserve(
    std::declval<std::span<const bool>>(), std::declval<std::span<const ResultHandle>>())));

int main() {
    try {
        test_capacity_and_protection();
        test_binding_generation_fifo_and_copies();
        test_full_fifo_and_pool_recycling();
        test_release_invalid_binding_exhaustion_and_no_allocations();
        test_reservation_preflight();
        test_preflight_generation_budget_and_no_allocations();
        std::cout << "result ledger tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "result ledger tests failed: " << error.what() << '\n';
        return 1;
    }
}
