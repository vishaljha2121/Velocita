#include "velocita/v2/spsc_ring.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace velocita::v2;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Entry {
    std::uint64_t sequence{};
    std::uint64_t payload{};
};

void test_it_refuses_when_full_rather_than_overwriting() {
    SpscRing<Entry, 4> ring;
    for (std::uint64_t i = 0; i < 4; ++i) {
        require(ring.push({i, i * 10}), "push failed below capacity");
    }
    require(ring.size() == 4, "the ring did not fill");

    // FULL MEANS FULL. Overwriting would discard a block whose bytes are not
    // yet on disk, which is the one thing the storage layer must never do.
    require(!ring.push({99, 990}), "a full ring accepted another entry");

    Entry out{};
    require(ring.pop(out), "pop failed");
    require(out.sequence == 0 && out.payload == 0,
            "a full ring did not preserve its oldest entry");
    require(ring.push({99, 990}), "the ring stayed closed after a pop");
}

void test_it_preserves_order() {
    SpscRing<Entry, 8> ring;
    for (std::uint64_t i = 0; i < 8; ++i) require(ring.push({i, i}), "push failed");
    for (std::uint64_t i = 0; i < 8; ++i) {
        Entry out{};
        require(ring.pop(out), "pop failed");
        require(out.sequence == i, "the ring reordered its entries");
    }
    Entry out{};
    require(!ring.pop(out), "an empty ring produced an entry");
}

void test_it_wraps_without_losing_entries() {
    SpscRing<Entry, 4> ring;
    std::uint64_t pushed = 0;
    std::uint64_t popped = 0;
    // Many more entries than the capacity, so the indices wrap repeatedly.
    for (int round = 0; round < 1000; ++round) {
        if (ring.push({pushed, pushed})) ++pushed;
        Entry out{};
        if (ring.pop(out)) {
            require(out.sequence == popped, "wrapping lost or reordered an entry");
            ++popped;
        }
    }
    require(pushed > 4 && popped > 4, "the fixture never wrapped");
}

void test_it_carries_every_entry_across_a_real_thread_boundary() {
    // The ordering claims in the header are about two threads. Asserting them
    // on one thread would be asserting nothing, so this runs a genuine
    // producer and consumer and checks that every entry arrives exactly once,
    // in order, with its payload intact.
    constexpr std::uint64_t kEntries = 200'000;
    SpscRing<Entry, 1024> ring;
    std::atomic<bool> producer_done{false};

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kEntries; ++i) {
            while (!ring.push({i, i ^ 0x5DEECE66DULL})) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::uint64_t received = 0;
    bool corrupted = false;
    bool misordered = false;
    while (received < kEntries) {
        Entry out{};
        if (ring.pop(out)) {
            if (out.sequence != received) misordered = true;
            if (out.payload != (out.sequence ^ 0x5DEECE66DULL)) corrupted = true;
            ++received;
        } else if (producer_done.load(std::memory_order_acquire) && ring.empty()) {
            break;
        }
    }
    producer.join();

    require(received == kEntries, "entries were lost crossing the thread boundary");
    require(!misordered, "entries arrived out of order across threads");
    require(!corrupted, "an entry was observed before it was fully written");
}

}  // namespace

int main() {
    try {
        test_it_refuses_when_full_rather_than_overwriting();
        test_it_preserves_order();
        test_it_wraps_without_losing_entries();
        test_it_carries_every_entry_across_a_real_thread_boundary();
        std::cout << "all V2 SPSC ring tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 SPSC ring test failed: " << error.what() << "\n";
        return 1;
    }
}
