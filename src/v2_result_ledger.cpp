#include "velocita/v2/result_ledger.hpp"

#include <limits>
#include <type_traits>

namespace velocita::v2 {
namespace {
constexpr std::uint16_t kNoSlot = std::numeric_limits<std::uint16_t>::max();
static_assert(std::is_nothrow_copy_assignable_v<CommittedResult>);
}

ResultLedger::ResultLedger() noexcept {
    header_.ordinary_free_head = 0;
    header_.protected_free_head = static_cast<std::uint16_t>(kResultLedgerOrdinarySlots);
    header_.ready_head = kNoSlot;
    header_.ready_tail = kNoSlot;
    header_.ordinary_free_count = kResultLedgerOrdinarySlots;
    header_.protected_free_count = kResultLedgerProtectedSlots;
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        auto& slot = slots_[index];
        slot.pool = index < kResultLedgerOrdinarySlots ? SlotPool::ordinary
                                                       : SlotPool::protected_control;
        const auto last = index + 1 == kResultLedgerOrdinarySlots || index + 1 == slots_.size();
        slot.next = last ? kNoSlot : static_cast<std::uint16_t>(index + 1);
    }
}

std::optional<ResultHandle> ResultLedger::reserve(const ResultBinding& binding,
                                                  const bool control_eligible) noexcept {
    if (binding.term <= 0 || binding.index <= 0 || header_.next_generation == 0)
        return std::nullopt;
    std::uint16_t* head = nullptr;
    std::uint16_t* count = nullptr;
    if (control_eligible && header_.protected_free_count != 0) {
        head = &header_.protected_free_head;
        count = &header_.protected_free_count;
    } else if (header_.ordinary_free_count != 0) {
        head = &header_.ordinary_free_head;
        count = &header_.ordinary_free_count;
    } else {
        return std::nullopt;
    }
    const auto index = *head;
    auto& slot = slots_[index];
    *head = slot.next;
    --*count;
    slot.result = {};
    slot.result.identity = binding.identity;
    slot.result.term = binding.term;
    slot.result.index = binding.index;
    slot.command_digest = binding.command_digest;
    slot.generation = header_.next_generation;
    header_.next_generation = header_.next_generation == std::numeric_limits<std::uint64_t>::max()
                                  ? 0
                                  : header_.next_generation + 1;
    slot.next = kNoSlot;
    slot.state = SlotState::reserved;
    return ResultHandle{slot.generation, index};
}

bool ResultLedger::can_reserve(
    const std::span<const bool> control_eligible,
    const std::span<const ResultHandle> released) const noexcept {
    if (control_eligible.size() > kResultLedgerSlots || released.size() > kResultLedgerSlots)
        return false;

    std::size_t ordinary = header_.ordinary_free_count;
    std::size_t protected_control = header_.protected_free_count;
    std::array<bool, kResultLedgerSlots> seen{};
    for (std::size_t offset = 0; offset < released.size(); ++offset) {
        const auto handle = released[offset];
        if (handle.slot >= slots_.size() || handle.generation == 0) return false;
        const auto& slot = slots_[handle.slot];
        if (slot.state != SlotState::reserved || slot.generation != handle.generation)
            return false;
        if (seen[handle.slot]) return false;
        seen[handle.slot] = true;
        if (slot.pool == SlotPool::ordinary)
            ++ordinary;
        else
            ++protected_control;
    }

    if (control_eligible.empty()) return true;
    if (header_.next_generation == 0) return false;
    const auto generations_left = std::numeric_limits<std::uint64_t>::max() -
                                  header_.next_generation + 1;
    if (control_eligible.size() > generations_left) return false;

    for (const bool control : control_eligible) {
        if (control && protected_control != 0) {
            --protected_control;
        } else if (ordinary != 0) {
            --ordinary;
        } else {
            return false;
        }
    }
    return true;
}

bool ResultLedger::matches(const ResultHandle handle,
                           const ResultBinding& binding) const noexcept {
    if (handle.slot >= slots_.size() || handle.generation == 0) return false;
    const auto& slot = slots_[handle.slot];
    return slot.state == SlotState::reserved && slot.generation == handle.generation &&
           slot.result.identity == binding.identity && slot.command_digest == binding.command_digest &&
           slot.result.term == binding.term && slot.result.index == binding.index;
}

bool ResultLedger::publish(const ResultHandle handle,
                           const CommittedResult& result) noexcept {
    if (handle.slot >= slots_.size() || handle.generation == 0) return false;
    auto& slot = slots_[handle.slot];
    if (slot.state != SlotState::reserved || slot.generation != handle.generation ||
        slot.result.identity != result.identity || slot.result.term != result.term ||
        slot.result.index != result.index)
        return false;
    slot.result = result;
    slot.state = SlotState::ready;
    slot.next = kNoSlot;
    if (header_.ready_count == 0) {
        header_.ready_head = handle.slot;
    } else {
        slots_[header_.ready_tail].next = handle.slot;
    }
    header_.ready_tail = handle.slot;
    ++header_.ready_count;
    return true;
}

bool ResultLedger::release_reserved(const ResultHandle handle) noexcept {
    if (handle.slot >= slots_.size() || handle.generation == 0) return false;
    auto& slot = slots_[handle.slot];
    if (slot.state != SlotState::reserved || slot.generation != handle.generation) return false;
    slot.state = SlotState::free;
    if (slot.pool == SlotPool::ordinary) {
        slot.next = header_.ordinary_free_head;
        header_.ordinary_free_head = handle.slot;
        ++header_.ordinary_free_count;
    } else {
        slot.next = header_.protected_free_head;
        header_.protected_free_head = handle.slot;
        ++header_.protected_free_count;
    }
    return true;
}

std::optional<CommittedResult> ResultLedger::take() noexcept {
    if (header_.ready_count == 0) return std::nullopt;
    const auto index = header_.ready_head;
    auto& slot = slots_[index];
    const auto result = slot.result;
    header_.ready_head = slot.next;
    --header_.ready_count;
    if (header_.ready_count == 0) header_.ready_tail = kNoSlot;
    slot.state = SlotState::free;
    if (slot.pool == SlotPool::ordinary) {
        slot.next = header_.ordinary_free_head;
        header_.ordinary_free_head = index;
        ++header_.ordinary_free_count;
    } else {
        slot.next = header_.protected_free_head;
        header_.protected_free_head = index;
        ++header_.protected_free_count;
    }
    return result;
}

bool ResultLedger::visit_obligations(const ResultObligationVisitor visitor,
                                     void* const context) const noexcept {
    if (visitor == nullptr) return false;

    std::array<bool, kResultLedgerSlots> inventoried{};
    const auto validate_list = [&](std::uint16_t current, const std::size_t expected,
                                   const SlotState state, const SlotPool pool) noexcept {
        std::size_t count{};
        while (current != kNoSlot) {
            if (current >= slots_.size() || inventoried[current] || count == slots_.size())
                return false;
            const auto& slot = slots_[current];
            if (slot.state != state || slot.pool != pool) return false;
            inventoried[current] = true;
            ++count;
            current = slot.next;
        }
        return count == expected;
    };

    if (!validate_list(header_.ordinary_free_head, header_.ordinary_free_count,
                       SlotState::free, SlotPool::ordinary) ||
        !validate_list(header_.protected_free_head, header_.protected_free_count,
                       SlotState::free, SlotPool::protected_control))
        return false;

    std::uint16_t current = header_.ready_head;
    std::size_t ready_count{};
    std::uint16_t ready_tail = kNoSlot;
    while (current != kNoSlot) {
        if (current >= slots_.size() || inventoried[current] || ready_count == slots_.size())
            return false;
        const auto& slot = slots_[current];
        if (slot.state != SlotState::ready || slot.generation == 0 ||
            slot.result.term <= 0 || slot.result.index <= 0)
            return false;
        inventoried[current] = true;
        ++ready_count;
        ready_tail = current;
        current = slot.next;
    }
    if (ready_count != header_.ready_count ||
        (ready_count == 0 ? header_.ready_tail != kNoSlot
                          : header_.ready_tail != ready_tail))
        return false;

    for (std::size_t index = 0; index < slots_.size(); ++index) {
        const auto& slot = slots_[index];
        const auto expected_pool = index < kResultLedgerOrdinarySlots
            ? SlotPool::ordinary : SlotPool::protected_control;
        if (slot.pool != expected_pool) return false;
        if (slot.state == SlotState::reserved) {
            if (inventoried[index] || slot.generation == 0 ||
                slot.result.term <= 0 || slot.result.index <= 0 || slot.next != kNoSlot)
                return false;
            inventoried[index] = true;
        } else if (!inventoried[index]) {
            return false;
        }
    }

    current = header_.ready_head;
    while (current != kNoSlot) {
        const auto& slot = slots_[current];
        const ResultBinding binding{slot.result.identity, slot.command_digest,
                                    slot.result.term, slot.result.index};
        if (!visitor(context, {ResultObligationState::ready,
                               {slot.generation, current}, binding, slot.result}))
            return false;
        current = slot.next;
    }
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        const auto& slot = slots_[index];
        if (slot.state != SlotState::reserved) continue;
        const ResultBinding binding{slot.result.identity, slot.command_digest,
                                    slot.result.term, slot.result.index};
        if (!visitor(context, {ResultObligationState::reserved,
                               {slot.generation, static_cast<std::uint16_t>(index)},
                               binding, {}}))
            return false;
    }
    return true;
}

std::size_t ResultLedger::available_ordinary() const noexcept {
    return header_.ordinary_free_count;
}
std::size_t ResultLedger::available_protected() const noexcept {
    return header_.protected_free_count;
}
std::size_t ResultLedger::ready_count() const noexcept { return header_.ready_count; }
std::size_t ResultLedger::reserved_count() const noexcept {
    return slots_.size() - header_.ordinary_free_count - header_.protected_free_count -
           header_.ready_count;
}

}
