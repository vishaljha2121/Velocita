#pragma once

#include "velocita/v2/retained_log.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace velocita::v2 {

struct SnapshotSuffixEntryView {
    std::uint64_t index{};
    std::int64_t term{};
    std::int32_t entry_id{};
    std::int32_t entry_type{};
    std::span<const std::byte> payload;
};

struct SnapshotInstallLocalView {
    std::uint64_t snapshot_index{};
    std::int64_t snapshot_term{};
    std::int64_t hard_term{};
    std::uint64_t commit_index{};
    std::uint64_t log_end{};
    std::uint64_t durable_log_end{};
    std::span<const SnapshotSuffixEntryView> retained;
};

enum class SnapshotInstallPreflightError : std::uint8_t {
    none,
    invalid_coordinate,
    capacity_exceeded,
    history_gap,
    term_regression,
    term_ahead_of_hard_state,
    log_end_mismatch,
    not_durable,
    committed_history_missing,
    committed_conflict,
    missing_boundary_evidence,
};

struct SnapshotInstallPreflightResult {
    SnapshotInstallPreflightError error{SnapshotInstallPreflightError::none};
    bool exact_boundary_match{};
    std::size_t suffix_offset{};
    std::size_t suffix_count{};
    std::uint64_t selected_log_end{};
};

// Pure classification of caller-validated local records. A successful result
// describes a borrowed suffix within `local.retained`; it is not permission to
// install, publish, delete history, release results, or serve traffic. The
// optional incoming boundary is already verified evidence from the incoming
// history. This function performs no decoding, I/O, allocation or mutation.
// On error, no result fields other than `error` are usable.
[[nodiscard]] SnapshotInstallPreflightResult preflight_snapshot_suffix(
    const SnapshotInstallLocalView& local,
    std::uint64_t incoming_snapshot_index,
    std::int64_t incoming_snapshot_term,
    const SnapshotSuffixEntryView* incoming_boundary) noexcept;

} // namespace velocita::v2
