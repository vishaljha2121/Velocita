#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

namespace velocita::v2 {

// A bounded single-producer single-consumer queue.
//
// SINGLE producer and SINGLE consumer is a precondition, not a hint. Two
// producers racing on `head_` would both believe they had the same slot, and
// nothing here would detect it. The callers are the owner loop and one writer
// thread, and that pairing is what makes the lock-free reasoning below valid at
// all.
//
// The capacity is fixed and a power of two so the wrap is a mask rather than a
// division, and full means FULL: `push` refuses rather than growing or
// overwriting. A ring that overwrote its oldest entry would silently discard a
// block whose bytes are not yet on disk, which is the one thing the storage
// layer must never do.
template <typename T, std::size_t Capacity>
class SpscRing {
    static_assert(Capacity >= 2, "a ring needs room for at least two entries");
    static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>,
                  "entries cross a thread boundary as plain bytes");

public:
    // Producer side. Returns false when the ring is full.
    [[nodiscard]] bool push(const T& value) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= Capacity) return false;
        slots_[head & (Capacity - 1)] = value;
        // RELEASE PAIRS WITH THE CONSUMER'S ACQUIRE ON `head_`. Everything the
        // producer wrote - this slot, and the block bytes it refers to - is
        // visible to the consumer before it can observe the new head. Without
        // the pairing the consumer could read a slot it has not been shown.
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false when the ring is empty.
    [[nodiscard]] bool pop(T& value) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        value = slots_[tail & (Capacity - 1)];
        // Release so the producer cannot reuse this slot until the consumer has
        // finished reading it.
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return static_cast<std::size_t>(head_.load(std::memory_order_acquire) -
                                        tail_.load(std::memory_order_acquire));
    }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    // Counters on separate cache lines. Sharing one would make every push
    // invalidate the consumer's line and every pop invalidate the producer's,
    // turning a lock-free queue into a slower one than a mutex.
    alignas(64) std::atomic<std::uint64_t> head_{0};
    alignas(64) std::atomic<std::uint64_t> tail_{0};
    alignas(64) std::array<T, Capacity> slots_{};
};

}  // namespace velocita::v2
