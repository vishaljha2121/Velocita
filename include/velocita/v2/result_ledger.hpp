// Bounded ownership of command results until they are delivered.
#pragma once

#include "velocita/v2/committed_result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace velocita::v2 {

inline constexpr std::size_t kResultLedgerSlots = 1024;
inline constexpr std::size_t kResultLedgerOrdinarySlots = 960;
inline constexpr std::size_t kResultLedgerProtectedSlots = 64;
static_assert(kResultLedgerOrdinarySlots + kResultLedgerProtectedSlots ==
              kResultLedgerSlots);
static_assert(kResultLedgerSlots < std::numeric_limits<std::uint16_t>::max());

struct ResultBinding {
    CommandIdentity identity{};
    Digest256 command_digest{};
    std::int64_t term{};
    std::int64_t index{};
    constexpr auto operator<=>(const ResultBinding&) const noexcept = default;
};

struct ResultHandle {
    std::uint64_t generation{};
    std::uint16_t slot{};
    constexpr auto operator<=>(const ResultHandle&) const noexcept = default;
};

enum class ResultObligationState : std::uint8_t { reserved, ready };

struct ResultObligationView {
    ResultObligationState state{ResultObligationState::reserved};
    ResultHandle handle{};
    ResultBinding binding{};
    CommittedResult ready_result{};
};

using ResultObligationVisitor = bool (*)(void*, ResultObligationView) noexcept;

struct ResultLedgerTestAccess;

class ResultLedger final {
public:
    ResultLedger() noexcept;
    ResultLedger(const ResultLedger&) = delete;
    ResultLedger& operator=(const ResultLedger&) = delete;
    ResultLedger(ResultLedger&&) = delete;
    ResultLedger& operator=(ResultLedger&&) = delete;

    [[nodiscard]] std::optional<ResultHandle> reserve(
        const ResultBinding& binding, bool control_eligible) noexcept;
    [[nodiscard]] bool can_reserve(
        std::span<const bool> control_eligible,
        std::span<const ResultHandle> released = {}) const noexcept;
    [[nodiscard]] bool matches(ResultHandle handle,
                               const ResultBinding& binding) const noexcept;
    [[nodiscard]] bool publish(ResultHandle handle,
                               const CommittedResult& result) noexcept;
    [[nodiscard]] bool release_reserved(ResultHandle handle) noexcept;
    [[nodiscard]] std::optional<CommittedResult> take() noexcept;

    [[nodiscard]] bool visit_obligations(ResultObligationVisitor visitor,
                                         void* context) const noexcept;

    [[nodiscard]] std::size_t available_ordinary() const noexcept;
    [[nodiscard]] std::size_t available_protected() const noexcept;
    [[nodiscard]] std::size_t ready_count() const noexcept;
    [[nodiscard]] std::size_t reserved_count() const noexcept;

private:
    enum class SlotState : std::uint8_t { free, reserved, ready };
    enum class SlotPool : std::uint8_t { ordinary, protected_control };
    struct Slot {
        CommittedResult result{};
        Digest256 command_digest{};
        std::uint64_t generation{};
        std::uint16_t next{};
        SlotState state{SlotState::free};
        SlotPool pool{SlotPool::ordinary};
    };
    struct Header {
        std::uint64_t next_generation{1};
        std::uint16_t ordinary_free_head{};
        std::uint16_t protected_free_head{};
        std::uint16_t ready_head{};
        std::uint16_t ready_tail{};
        std::uint16_t ordinary_free_count{};
        std::uint16_t protected_free_count{};
        std::uint16_t ready_count{};
    };
    static_assert(sizeof(Slot) == 224 && alignof(Slot) == 8);
    static_assert(sizeof(Header) == 24 && alignof(Header) == 8);

    Header header_{};
    std::array<Slot, kResultLedgerSlots> slots_{};

    friend struct ResultLedgerTestAccess;
};

static_assert(sizeof(ResultHandle) == 16);
static_assert(sizeof(ResultLedger) == 229400);

}
