#include "velocita/v2/snapshot_install_preflight.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace allocation_probe { thread_local bool enabled{}; thread_local std::size_t count{}; }
void* operator new(const std::size_t bytes) {
    if (allocation_probe::enabled) ++allocation_probe::count;
    if (void* value = std::malloc(bytes ? bytes : 1)) return value;
    throw std::bad_alloc{};
}
void* operator new[](const std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace velocita::v2;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::array<std::byte, 3> a{{std::byte{1}, std::byte{2}, std::byte{3}}};
std::array<std::byte, 3> b{{std::byte{4}, std::byte{5}, std::byte{6}}};

SnapshotSuffixEntryView entry(std::uint64_t index, std::int64_t term,
                              std::span<const std::byte> payload = a) {
    return {index, term, static_cast<std::int32_t>(index), 0, payload};
}

SnapshotInstallLocalView local(std::span<const SnapshotSuffixEntryView> records,
                               std::uint64_t old_snapshot = 5,
                               std::int64_t old_term = 2,
                               std::uint64_t commit = 7) {
    return {old_snapshot, old_term, 8, commit,
            records.empty() ? old_snapshot : std::max(old_snapshot, records.back().index),
            records.empty() ? old_snapshot : std::max(old_snapshot, records.back().index), records};
}

void matching_boundary_preserves_the_complete_unknown_suffix() {
    const std::array records{entry(4, 1), entry(5, 2), entry(6, 3),
                             entry(7, 3), entry(8, 4), entry(9, 6)};
    const auto boundary = entry(7, 3);
    allocation_probe::count = 0;
    allocation_probe::enabled = true;
    const auto result = preflight_snapshot_suffix(local(records), 7, 3, &boundary);
    allocation_probe::enabled = false;
    require(result.error == SnapshotInstallPreflightError::none &&
                result.exact_boundary_match && result.suffix_offset == 4 &&
                result.suffix_count == 2 && result.selected_log_end == 9,
            "exact boundary did not preserve the complete unknown suffix");
    require(records[result.suffix_offset].payload.data() == a.data() &&
                records[result.suffix_offset + 1].index == 9,
            "suffix description did not borrow the unchanged input records");
    require(allocation_probe::count == 0, "preflight allocated");
}

void exact_boundary_requires_metadata_and_bytes() {
    const std::array records{entry(6, 3), entry(7, 3), entry(8, 4)};
    auto metadata = entry(7, 3); metadata.entry_id = 99;
    auto bytes = entry(7, 3, b);
    require(preflight_snapshot_suffix(local(records), 7, 3, &metadata).error ==
                SnapshotInstallPreflightError::committed_conflict,
            "metadata-only boundary mismatch was accepted");
    require(preflight_snapshot_suffix(local(records), 7, 3, &bytes).error ==
                SnapshotInstallPreflightError::committed_conflict,
            "byte-only boundary mismatch was accepted");
    require(preflight_snapshot_suffix(local(records), 7, 3, nullptr).error ==
                SnapshotInstallPreflightError::missing_boundary_evidence,
            "suffix was blessed without exact incoming boundary evidence");
    auto unrelated = entry(8, 3);
    require(preflight_snapshot_suffix(local(records), 7, 3, &unrelated).error ==
                SnapshotInstallPreflightError::invalid_coordinate,
            "unrelated incoming boundary was interpreted as a fork");
}

void uncommitted_mismatch_describes_no_splice() {
    const std::array records{entry(6, 3), entry(7, 3), entry(8, 4)};
    auto view = local(records, 5, 2, 6);
    const auto incoming = entry(7, 4, b);
    const auto result = preflight_snapshot_suffix(view, 7, 4, &incoming);
    require(result.error == SnapshotInstallPreflightError::none &&
                !result.exact_boundary_match && result.suffix_count == 0 &&
                result.selected_log_end == 7,
            "uncommitted mismatch did not return a no-splice description");
    require(records[2].payload.data() == a.data(), "no-splice classification mutated evidence");
}

void covered_prefix_layout_and_tail_boundary_are_supported() {
    const std::array records{entry(3, 1), entry(4, 1), entry(5, 2),
                             entry(6, 3), entry(7, 3)};
    const auto boundary = entry(7, 3);
    const auto result = preflight_snapshot_suffix(local(records), 7, 3, &boundary);
    require(result.error == SnapshotInstallPreflightError::none &&
                result.exact_boundary_match && result.suffix_count == 0 &&
                result.selected_log_end == 7,
            "snapshot-covered WAL prefix or tail boundary was refused");
}

void durability_and_committed_history_fail_closed() {
    const std::array records{entry(6, 3), entry(7, 3), entry(8, 4)};
    auto volatile_tail = local(records); volatile_tail.durable_log_end = 7;
    require(preflight_snapshot_suffix(volatile_tail, 7, 3, nullptr).error ==
                SnapshotInstallPreflightError::not_durable,
            "volatile tail was silently omitted");
    auto beyond = local(records, 5, 2, 8);
    const auto beyond_result = preflight_snapshot_suffix(beyond, 9, 4, nullptr);
    require(beyond_result.error == SnapshotInstallPreflightError::none &&
                !beyond_result.exact_boundary_match &&
                beyond_result.selected_log_end == 9,
            "incoming snapshot beyond the complete local tail did not describe no splice");
    auto committed_term = local(records, 5, 2, 7);
    require(preflight_snapshot_suffix(committed_term, 9, 2, nullptr).error ==
                SnapshotInstallPreflightError::committed_conflict,
            "higher known committed term below incoming S was accepted");

    const std::array<SnapshotSuffixEntryView, 0> empty{};
    auto snapshot_only = local(empty, 5, 3, 0);
    require(preflight_snapshot_suffix(snapshot_only, 6, 2, nullptr).error ==
                SnapshotInstallPreflightError::committed_conflict,
            "lower-term incoming history crossed the committed snapshot prefix");

    const std::array covered{entry(2, 1), entry(3, 2)};
    auto covered_only = local(covered, 5, 3, 0);
    const auto covered_result = preflight_snapshot_suffix(covered_only, 6, 4, nullptr);
    require(covered_result.error == SnapshotInstallPreflightError::none &&
                covered_result.selected_log_end == 6,
            "snapshot-covered-only WAL prefix was not accepted");
}

void structural_terms_coordinates_and_capacity_are_bounded() {
    const std::array good{entry(6, 3), entry(7, 3), entry(8, 4)};
    auto gap = good; gap[1].index = 8;
    require(preflight_snapshot_suffix(local(gap), 7, 3, nullptr).error ==
                SnapshotInstallPreflightError::history_gap, "history gap accepted");
    auto regression = good; regression[2].term = 2;
    require(preflight_snapshot_suffix(local(regression), 7, 3, nullptr).error ==
                SnapshotInstallPreflightError::term_regression, "term regression accepted");
    auto ahead = local(good); ahead.hard_term = 3;
    require(preflight_snapshot_suffix(ahead, 7, 3, nullptr).error ==
                SnapshotInstallPreflightError::term_ahead_of_hard_state,
            "term beyond hard state accepted");
    auto wrong_end = local(good); wrong_end.log_end = 9; wrong_end.durable_log_end = 9;
    require(preflight_snapshot_suffix(wrong_end, 7, 3, nullptr).error ==
                SnapshotInstallPreflightError::log_end_mismatch, "invented log end accepted");
    auto overflow = good; overflow[2].index = std::uint64_t(std::numeric_limits<std::int64_t>::max()) + 1;
    require(preflight_snapshot_suffix(local(overflow), 7, 3, nullptr).error ==
                SnapshotInstallPreflightError::invalid_coordinate, "oversized index accepted");

    std::vector<SnapshotSuffixEntryView> maximum;
    maximum.reserve(kRetainedLogEntries + 1);
    for (std::size_t i = 0; i <= kRetainedLogEntries; ++i) maximum.push_back(entry(i + 1, 1));
    auto too_many = local(std::span<const SnapshotSuffixEntryView>(maximum), 0, 0, 0);
    require(preflight_snapshot_suffix(too_many, 1, 1, &maximum[0]).error ==
                SnapshotInstallPreflightError::capacity_exceeded, "one-over retained bound accepted");
    maximum.pop_back();
    auto bounded = local(std::span<const SnapshotSuffixEntryView>(maximum), 0, 0, 0);
    const auto result = preflight_snapshot_suffix(bounded, 1, 1, &maximum[0]);
    require(result.error == SnapshotInstallPreflightError::none &&
                result.suffix_count == kRetainedLogEntries - 1,
            "exact retained bound refused");
}

void payload_and_boundary_shapes_are_checked() {
    const std::array<std::byte, kRetainedLogCellBytes> maximum{};
    const std::array<std::byte, kRetainedLogCellBytes + 1> excessive{};
    std::array records{entry(1, 1, maximum)};
    auto boundary = records[0];
    require(preflight_snapshot_suffix(local(records, 0, 0, 0), 1, 1, &boundary).error ==
                SnapshotInstallPreflightError::none, "maximum payload refused");
    records[0].payload = {};
    require(preflight_snapshot_suffix(local(records, 0, 0, 0), 1, 1, &boundary).error ==
                SnapshotInstallPreflightError::invalid_coordinate, "empty payload accepted");
    records[0].payload = excessive; boundary = records[0];
    require(preflight_snapshot_suffix(local(records, 0, 0, 0), 1, 1, &boundary).error ==
                SnapshotInstallPreflightError::capacity_exceeded, "oversized payload accepted");
}

void supplied_evidence_is_checked_even_without_a_local_boundary() {
    const SnapshotInstallLocalView empty{0, 0, 4, 0, 0, 0, {}};
    const std::array<std::byte, kRetainedLogCellBytes + 1> excessive{};
    for (unsigned fault = 0; fault != 5; ++fault) {
        auto supplied = entry(1, 1);
        if (fault == 0) supplied.index = 2;
        if (fault == 1) supplied.term = 0;
        if (fault == 2) supplied.term = 2;
        if (fault == 3) supplied.payload = {};
        if (fault == 4) supplied.payload = excessive;
        const auto result = preflight_snapshot_suffix(empty, 1, 1, &supplied);
        const auto expected = fault == 4 ? SnapshotInstallPreflightError::capacity_exceeded
                                        : SnapshotInstallPreflightError::invalid_coordinate;
        require(result.error == expected && !result.exact_boundary_match &&
                    result.suffix_offset == 0 && result.suffix_count == 0 &&
                    result.selected_log_end == 0,
                "invalid incoming evidence was ignored or leaked a usable plan");
    }
    const std::array records{entry(1, 1), entry(2, 2)};
    auto boundary = records[0];
    boundary.entry_type = 1;
    require(preflight_snapshot_suffix(local(records, 0, 0, 1), 1, 1, &boundary).error ==
                SnapshotInstallPreflightError::committed_conflict,
            "different entry type at the committed boundary was accepted");

    const std::array missing_first{entry(2, 1), entry(3, 1)};
    const auto no_prefix = preflight_snapshot_suffix(local(missing_first, 0, 0, 0), 4, 1, nullptr);
    require(no_prefix.error == SnapshotInstallPreflightError::history_gap,
            "genesis view missing index one was accepted as complete");
}

}

int main() {
    try {
        matching_boundary_preserves_the_complete_unknown_suffix();
        exact_boundary_requires_metadata_and_bytes();
        uncommitted_mismatch_describes_no_splice();
        covered_prefix_layout_and_tail_boundary_are_supported();
        durability_and_committed_history_fail_closed();
        structural_terms_coordinates_and_capacity_are_bounded();
        payload_and_boundary_shapes_are_checked();
        supplied_evidence_is_checked_even_without_a_local_boundary();
        std::cout << "snapshot install preflight tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "snapshot install preflight tests failed: " << error.what() << '\n';
        return 1;
    }
}
