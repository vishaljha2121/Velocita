#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace velocita::v2 {

// When may a follower tell its leader "durable up to N" without being asked?
//
// A follower's reply to AppendEntries may only claim entries already on disk
// (the clamp in ReplicatedKernel::receive_peer). Its writer usually finishes a
// few milliseconds later, but the follower then stays silent until the
// leader's next heartbeat, so every commit waited up to one heartbeat interval.
// This tracker lets the follower send that news as soon as it exists, while
// claiming nothing a normal reply in the same term could not:
//
//   ack = min(verified, durable)   where `verified` is the highest index the
//   CURRENT leader has checked in the CURRENT term (a successful reply's
//   current_idx: prev_log_idx plus that packet's entries).
//
// Entries a follower holds from an older term, durable or not, are never
// covered: they may differ from the new leader's log, and helping an old-term
// leader commit after this follower moved on could lose them. Any change of
// term or leader therefore starts from nothing. Runtime log truncation first
// rewinds the durable index, and runtime snapshot install is forbidden, so the
// same verified prefix cannot silently change under a term.
class FollowerAckTracker {
public:
    // Every reply this follower actually sent: `verified` is the library's
    // current_idx before the clamp, `reported` the index the reply claimed.
    // The library only produces a reply once its term is durable (persist_term
    // defers it otherwise), so every recorded term is already on disk.
    void record(const std::int64_t term, const std::int32_t leader, const bool success,
                const std::int64_t verified, const std::int64_t reported) noexcept {
        if (term != term_ || leader != leader_) {
            term_ = term;
            leader_ = leader;
            verified_ = 0;
            reported_ = 0;
        }
        if (!success) {
            // A failed append verifies nothing, and its reply may still sit in
            // the one-per-kind outbound slot: an early ack would replace it and
            // hide the mismatch. Stay silent until the next success.
            reported_ = std::max(reported_, verified_);
            return;
        }
        verified_ = std::max(verified_, verified);
        reported_ = std::max(reported_, reported);
    }

    // The index worth acknowledging now, if any: only while still a follower
    // of the same leader in the same term, and only if it says something new.
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

}  // namespace velocita::v2
