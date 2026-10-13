#pragma once
#include "velocita/v2/contracts.hpp"
#include <algorithm>
#include <span>

namespace velocita::v2 {
inline constexpr std::int32_t kLeaderReadBarrierEntryType = 103;
inline bool valid_leader_read_barrier(std::span<const std::byte> payload, const Digest256& domain) noexcept {
    return !domain.is_zero() && payload.size() == domain.bytes.size() &&
        std::equal(payload.begin(), payload.end(), domain.bytes.begin());
}
}
