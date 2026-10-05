// Decides when a follower reports newly durable entries to its leader.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace velocita::v2 {

class FollowerAckTracker {
public:
    void record(const std::int64_t term, const std::int32_t leader, const bool success,
                const std::int64_t verified, const std::int64_t reported) noexcept {
        if (term != term_ || leader != leader_) {
            term_ = term;
            leader_ = leader;
            verified_ = 0;
            reported_ = 0;
        }
        if (!success) {
            reported_ = std::max(reported_, verified_);
            return;
        }
        verified_ = std::max(verified_, verified);
        reported_ = std::max(reported_, reported);
    }

    [[nodiscard]] std::optional<std::int64_t> due(const std::int64_t term, const std::int32_t leader,
                                                  const bool follower,
                                                  const std::int64_t durable) const noexcept {
        if (!follower || term != term_ || leader != leader_ || term_ <= 0) return std::nullopt;
        const auto index = std::min(verified_, durable);
        if (index <= reported_) return std::nullopt;
        return index;
    }

    void sent(const std::int64_t index) noexcept { reported_ = std::max(reported_, index); }

    [[nodiscard]] std::int64_t term() const noexcept { return term_; }
    [[nodiscard]] std::int32_t leader() const noexcept { return leader_; }

private:
    std::int64_t term_{0};
    std::int32_t leader_{-1};
    std::int64_t verified_{0};
    std::int64_t reported_{0};
};

}
