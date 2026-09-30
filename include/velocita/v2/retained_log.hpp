#pragma once

#include "velocita/v2/result_ledger.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace velocita::v2 {

inline constexpr std::size_t kRetainedLogEntries = 8192;
inline constexpr std::size_t kRetainedLogOrdinaryEntries = 8128;
inline constexpr std::size_t kRetainedLogProtectedEntries = 64;
inline constexpr std::size_t kRetainedLogCellBytes = 2048;
static_assert(kRetainedLogOrdinaryEntries + kRetainedLogProtectedEntries ==
              kRetainedLogEntries);

enum class RetainedLogPool : std::uint8_t { ordinary, protected_control };

struct RetainedLogDescriptor {
    std::uint64_t index{};
    std::uint16_t length{};
    RetainedLogPool pool{RetainedLogPool::ordinary};
    std::optional<ResultHandle> result{};
};
static_assert(sizeof(RetainedLogDescriptor) == 40);

// Owner-thread-only fixed retained history. Construction is the only allocating
// operation; callers normally heap-own this object because its startup backing
// is deliberately large.
class RetainedLog final {
public:
    RetainedLog();
    ~RetainedLog();
    RetainedLog(const RetainedLog&) = delete;
    RetainedLog& operator=(const RetainedLog&) = delete;
    RetainedLog(RetainedLog&&) = delete;
    RetainedLog& operator=(RetainedLog&&) = delete;

    [[nodiscard]] bool append(
        std::uint64_t index, std::span<const std::byte> payload,
        bool control_eligible,
        std::optional<ResultHandle> result = std::nullopt) noexcept;
    // Only interval endpoints may be erased. This makes durable prefix and
    // tail-first suffix release explicit and prevents holes in modulo lookup.
    [[nodiscard]] bool erase(std::uint64_t index) noexcept;
    void clear() noexcept;

    [[nodiscard]] RetainedLogDescriptor* find(std::uint64_t index) noexcept;
    [[nodiscard]] const RetainedLogDescriptor* find(std::uint64_t index) const noexcept;
    [[nodiscard]] RetainedLogDescriptor* at_offset(std::size_t offset) noexcept;
    [[nodiscard]] const RetainedLogDescriptor* at_offset(std::size_t offset) const noexcept;
    [[nodiscard]] std::span<std::byte> payload(std::uint64_t index) noexcept;
    [[nodiscard]] std::span<const std::byte> payload(std::uint64_t index) const noexcept;

    // Simulates removal of the existing suffix starting at SUFFIX_START before
    // adding CLASSES. Stored actual pool tags, not command classification, grant
    // hypothetical token credit. No ownership is released by this call.
    [[nodiscard]] bool can_admit(
        std::span<const bool> control_eligible,
        std::optional<std::uint64_t> suffix_start = std::nullopt) const noexcept;

    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t live_bytes() const noexcept { return live_bytes_; }
    [[nodiscard]] std::size_t ordinary_count() const noexcept { return ordinary_count_; }
    [[nodiscard]] std::size_t protected_count() const noexcept { return protected_count_; }
    [[nodiscard]] std::uint64_t first_index() const noexcept { return first_index_; }
    [[nodiscard]] std::uint64_t last_index() const noexcept { return last_index_; }

private:
    struct Cell { std::byte bytes[kRetainedLogCellBytes]; };

    std::unique_ptr<RetainedLogDescriptor[]> descriptors_;
    std::unique_ptr<Cell[]> cells_;
    std::uint64_t first_index_{};
    std::uint64_t last_index_{};
    std::size_t count_{};
    std::size_t live_bytes_{};
    std::size_t ordinary_count_{};
    std::size_t protected_count_{};
};

} // namespace velocita::v2
