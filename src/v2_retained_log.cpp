#include "velocita/v2/retained_log.hpp"

#include <algorithm>
#include <limits>

namespace velocita::v2 {
namespace {
[[nodiscard]] constexpr std::size_t slot_for(const std::uint64_t index) noexcept {
    return static_cast<std::size_t>(index % kRetainedLogEntries);
}
}

RetainedLog::RetainedLog()
    : descriptors_(std::make_unique<RetainedLogDescriptor[]>(kRetainedLogEntries)),
      cells_(std::make_unique<Cell[]>(kRetainedLogEntries)) {}

RetainedLog::~RetainedLog() = default;

bool RetainedLog::append(const std::uint64_t index,
                         const std::span<const std::byte> bytes,
                         const bool control_eligible,
                         const std::optional<ResultHandle> result) noexcept {
    if (index == 0 || bytes.empty() || bytes.size() > kRetainedLogCellBytes ||
        count_ == kRetainedLogEntries)
        return false;
    if (count_ != 0 &&
        (last_index_ == std::numeric_limits<std::uint64_t>::max() ||
         index != last_index_ + 1))
        return false;

    RetainedLogPool pool{};
    if (control_eligible && protected_count_ < kRetainedLogProtectedEntries) {
        pool = RetainedLogPool::protected_control;
    } else if (ordinary_count_ < kRetainedLogOrdinaryEntries) {
        pool = RetainedLogPool::ordinary;
    } else {
        return false;
    }

    const auto slot = slot_for(index);
    if (descriptors_[slot].index != 0) return false;
    std::copy(bytes.begin(), bytes.end(), cells_[slot].bytes);
    descriptors_[slot] = RetainedLogDescriptor{
        index, static_cast<std::uint16_t>(bytes.size()), pool, result};
    if (count_ == 0) first_index_ = index;
    last_index_ = index;
    ++count_;
    live_bytes_ += bytes.size();
    if (pool == RetainedLogPool::ordinary)
        ++ordinary_count_;
    else
        ++protected_count_;
    return true;
}

bool RetainedLog::erase(const std::uint64_t index) noexcept {
    if (index == 0 || count_ == 0 || (index != first_index_ && index != last_index_))
        return false;
    const auto slot = slot_for(index);
    auto& descriptor = descriptors_[slot];
    if (descriptor.index != index || descriptor.length == 0 ||
        descriptor.length > kRetainedLogCellBytes)
        return false;

    live_bytes_ -= descriptor.length;
    if (descriptor.pool == RetainedLogPool::ordinary)
        --ordinary_count_;
    else
        --protected_count_;
    descriptor = {};
    --count_;
    if (count_ == 0) {
        first_index_ = 0;
        last_index_ = 0;
    } else if (index == first_index_) {
        ++first_index_;
    } else {
        --last_index_;
    }
    return true;
}

void RetainedLog::clear() noexcept {
    for (std::size_t slot = 0; slot < kRetainedLogEntries; ++slot)
        descriptors_[slot] = {};
    first_index_ = 0;
    last_index_ = 0;
    count_ = 0;
    live_bytes_ = 0;
    ordinary_count_ = 0;
    protected_count_ = 0;
}

RetainedLogDescriptor* RetainedLog::find(const std::uint64_t index) noexcept {
    if (index == 0 || count_ == 0 || index < first_index_ || index > last_index_)
        return nullptr;
    auto& descriptor = descriptors_[slot_for(index)];
    return descriptor.index == index ? &descriptor : nullptr;
}

const RetainedLogDescriptor* RetainedLog::find(const std::uint64_t index) const noexcept {
    if (index == 0 || count_ == 0 || index < first_index_ || index > last_index_)
        return nullptr;
    const auto& descriptor = descriptors_[slot_for(index)];
    return descriptor.index == index ? &descriptor : nullptr;
}

RetainedLogDescriptor* RetainedLog::at_offset(const std::size_t offset) noexcept {
    if (offset >= count_ || offset > std::numeric_limits<std::uint64_t>::max() - first_index_)
        return nullptr;
    return find(first_index_ + offset);
}

const RetainedLogDescriptor* RetainedLog::at_offset(const std::size_t offset) const noexcept {
    if (offset >= count_ || offset > std::numeric_limits<std::uint64_t>::max() - first_index_)
        return nullptr;
    return find(first_index_ + offset);
}

std::span<std::byte> RetainedLog::payload(const std::uint64_t index) noexcept {
    const auto* descriptor = find(index);
    if (descriptor == nullptr || descriptor->length > kRetainedLogCellBytes) return {};
    return {cells_[slot_for(index)].bytes, descriptor->length};
}

std::span<const std::byte> RetainedLog::payload(const std::uint64_t index) const noexcept {
    const auto* descriptor = find(index);
    if (descriptor == nullptr || descriptor->length > kRetainedLogCellBytes) return {};
    return {cells_[slot_for(index)].bytes, descriptor->length};
}

bool RetainedLog::can_admit(
    const std::span<const bool> control_eligible,
    const std::optional<std::uint64_t> suffix_start) const noexcept {
    if (control_eligible.size() > kRetainedLogEntries) return false;
    std::size_t ordinary = kRetainedLogOrdinaryEntries - ordinary_count_;
    std::size_t protected_control = kRetainedLogProtectedEntries - protected_count_;
    std::size_t physical = kRetainedLogEntries - count_;

    if (suffix_start) {
        if (count_ == 0 || *suffix_start == 0 || *suffix_start < first_index_ ||
            *suffix_start > last_index_ + (last_index_ != std::numeric_limits<std::uint64_t>::max()))
            return false;
        if (*suffix_start <= last_index_) {
            auto index = *suffix_start;
            for (;;) {
                const auto& descriptor = descriptors_[slot_for(index)];
                if (descriptor.index != index) return false;
                if (descriptor.pool == RetainedLogPool::ordinary)
                    ++ordinary;
                else
                    ++protected_control;
                ++physical;
                if (index == last_index_) break;
                ++index;
            }
        }
    }

    if (control_eligible.size() > physical) return false;
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

}
