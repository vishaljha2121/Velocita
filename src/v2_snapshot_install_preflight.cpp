#include "velocita/v2/snapshot_install_preflight.hpp"

#include <algorithm>
#include <limits>

namespace velocita::v2 {
namespace {

using Error = SnapshotInstallPreflightError;

SnapshotInstallPreflightResult refuse(const Error error) noexcept {
    return {error, false, 0, 0, 0};
}

bool same_entry(const SnapshotSuffixEntryView& left,
                const SnapshotSuffixEntryView& right) noexcept {
    return left.index == right.index && left.term == right.term &&
        left.entry_id == right.entry_id && left.entry_type == right.entry_type &&
        left.payload.size() == right.payload.size() &&
        std::equal(left.payload.begin(), left.payload.end(), right.payload.begin());
}

} // namespace

SnapshotInstallPreflightResult preflight_snapshot_suffix(
    const SnapshotInstallLocalView& local,
    const std::uint64_t incoming_snapshot_index,
    const std::int64_t incoming_snapshot_term,
    const SnapshotSuffixEntryView* incoming_boundary) noexcept {
    constexpr auto max_index =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (incoming_snapshot_index == 0 || incoming_snapshot_index > max_index ||
        incoming_snapshot_term <= 0 || local.snapshot_index > max_index ||
        local.log_end > max_index || local.durable_log_end > max_index ||
        local.commit_index > max_index || local.hard_term <= 0 ||
        (local.snapshot_index == 0) != (local.snapshot_term == 0) ||
        local.snapshot_term < 0 || incoming_snapshot_index <= local.snapshot_index ||
        local.log_end < local.snapshot_index || local.commit_index > local.log_end)
        return refuse(Error::invalid_coordinate);
    if (local.retained.size() > kRetainedLogEntries)
        return refuse(Error::capacity_exceeded);
    if (incoming_boundary != nullptr) {
        if (incoming_boundary->index != incoming_snapshot_index ||
            incoming_boundary->term != incoming_snapshot_term ||
            incoming_boundary->payload.empty())
            return refuse(Error::invalid_coordinate);
        if (incoming_boundary->payload.size() > kRetainedLogCellBytes)
            return refuse(Error::capacity_exceeded);
    }
    if (local.durable_log_end != local.log_end)
        return refuse(Error::not_durable);
    if (local.snapshot_term > local.hard_term || incoming_snapshot_term > local.hard_term)
        return refuse(Error::term_ahead_of_hard_state);

    std::int64_t previous_term{};
    std::uint64_t previous_index{};
    std::size_t boundary_offset = local.retained.size();
    for (std::size_t offset = 0; offset < local.retained.size(); ++offset) {
        const auto& record = local.retained[offset];
        if (record.index == 0 || record.index > max_index || record.term <= 0 ||
            record.payload.empty())
            return refuse(Error::invalid_coordinate);
        if (record.payload.size() > kRetainedLogCellBytes)
            return refuse(Error::capacity_exceeded);
        if (record.term > local.hard_term)
            return refuse(Error::term_ahead_of_hard_state);
        if (offset != 0) {
            if (previous_index == max_index || record.index != previous_index + 1)
                return refuse(Error::history_gap);
            if (record.term < previous_term)
                return refuse(Error::term_regression);
        }
        if (local.snapshot_index != 0) {
            if ((record.index == local.snapshot_index &&
                 record.term != local.snapshot_term) ||
                (record.index < local.snapshot_index &&
                 record.term > local.snapshot_term) ||
                (record.index > local.snapshot_index &&
                 record.term < local.snapshot_term))
                return refuse(Error::term_regression);
        }
        if (record.index == incoming_snapshot_index) boundary_offset = offset;
        previous_index = record.index;
        previous_term = record.term;
    }

    const auto effective_end = local.retained.empty()
        ? local.snapshot_index
        : std::max(local.snapshot_index, local.retained.back().index);
    if (effective_end != local.log_end)
        return refuse(Error::log_end_mismatch);
    if (local.log_end > local.snapshot_index) {
        if (local.retained.empty() ||
            local.retained.front().index > local.snapshot_index + 1 ||
            local.retained.back().index != local.log_end)
            return refuse(Error::history_gap);
    }

    // A known committed term cannot be replaced by an incoming prefix whose
    // term is already lower, even when incoming S lies beyond the local end.
    if (local.snapshot_index != 0 &&
        local.snapshot_index <= incoming_snapshot_index &&
        local.snapshot_term > incoming_snapshot_term)
        return refuse(Error::committed_conflict);
    for (const auto& record : local.retained) {
        if (record.index <= local.commit_index &&
            record.index <= incoming_snapshot_index &&
            record.term > incoming_snapshot_term)
            return refuse(Error::committed_conflict);
    }

    if (boundary_offset == local.retained.size()) {
        if (local.commit_index > incoming_snapshot_index)
            return refuse(Error::committed_history_missing);
        return {Error::none, false, 0, 0, incoming_snapshot_index};
    }

    const auto& local_boundary = local.retained[boundary_offset];
    if (incoming_boundary == nullptr)
        return refuse(Error::missing_boundary_evidence);
    const bool exact = local_boundary.term == incoming_snapshot_term &&
        same_entry(local_boundary, *incoming_boundary);
    if (!exact) {
        if (incoming_snapshot_index <= local.commit_index)
            return refuse(Error::committed_conflict);
        return {Error::none, false, 0, 0, incoming_snapshot_index};
    }

    const auto suffix_offset = boundary_offset + 1;
    return {Error::none, true, suffix_offset,
            local.retained.size() - suffix_offset, local.log_end};
}

} // namespace velocita::v2
