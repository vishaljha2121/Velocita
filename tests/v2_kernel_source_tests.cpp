#include "v2_catalog_test_support.hpp"
#include "velocita/v2/kernel.hpp"
#include "velocita/v2/kernel_codec.hpp"
#include "velocita/v2/state_snapshot.hpp"

#include <array>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

velocita::v2::Digest256 digest(const std::uint8_t marker) {
    velocita::v2::Digest256 result;
    result.bytes[0] = static_cast<std::byte>(marker);
    return result;
}

velocita::v2::Command source_command(const std::uint64_t request_sequence,
                                     const std::uint8_t command_marker,
                                     const std::uint64_t source,
                                     const std::uint64_t generation,
                                     const std::uint64_t sequence,
                                     const std::uint8_t event_marker) {
    using namespace velocita::v2;
    return Command{CommandIdentity{CallerId{11},
                                   RequestSequence{request_sequence},
                                   digest(command_marker)},
                   RecordSourceEvent{SourceId{source},
                                     SourceGeneration{generation},
                                     SourceSequence{sequence},
                                     digest(event_marker)}};
}

void test_contiguous_events_and_exact_event_duplicates() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;

    const auto first = catalog_test::apply(state, source_command(1, 0x11, 7, 1, 1, 0xa1), scratch);
    require(first.code == ApplyCode::applied && first.source_id() == SourceId{7} &&
                first.source_generation() == SourceGeneration{1} &&
                first.source_sequence() == SourceSequence{1},
            "first source event was not accepted");

    const auto duplicate =
        catalog_test::apply(state, source_command(2, 0x12, 7, 1, 1, 0xa1), scratch);
    require(duplicate.code == ApplyCode::already_applied &&
                state.source_event_count == 1 && state.state_version == 2,
            "same source event delivered under another request was not a no-op");

    const auto second = catalog_test::apply(state, source_command(3, 0x13, 7, 1, 2, 0xa2), scratch);
    const auto* cursor = find_source_cursor(state, SourceId{7});
    require(second.code == ApplyCode::applied && cursor != nullptr &&
                cursor->generation == SourceGeneration{1} &&
                cursor->sequence == SourceSequence{2} && state.source_event_count == 2,
            "contiguous source sequence did not advance its cursor");
}

void test_conflicting_event_identity_is_retained_without_cursor_mutation() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;
    require(catalog_test::apply(state, source_command(1, 0x21, 3, 1, 1, 0xb1), scratch).code ==
                ApplyCode::applied,
            "source setup failed");

    const auto conflict_command = source_command(2, 0x22, 3, 1, 1, 0xb2);
    const auto conflict = catalog_test::apply(state, conflict_command, scratch);
    const auto* cursor = find_source_cursor(state, SourceId{3});
    require(conflict.code == ApplyCode::source_event_conflict && cursor != nullptr &&
                cursor->sequence == SourceSequence{1} && state.source_event_count == 1,
            "conflicting source identity mutated the cursor");
    require(catalog_test::apply(state, conflict_command, scratch) == conflict && state.state_version == 2,
            "conflicting source result was not retained exactly");
}

void test_gaps_and_regressions_fail_closed() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;

    require(catalog_test::apply(state, source_command(1, 0x31, 5, 2, 1, 0xc1), scratch).code ==
                ApplyCode::source_generation_gap,
            "unbootstrapped generation was accepted");
    require(catalog_test::apply(state, source_command(2, 0x32, 5, 1, 2, 0xc2), scratch).code ==
                ApplyCode::source_sequence_gap,
            "unbootstrapped sequence was accepted");
    require(catalog_test::apply(state, source_command(3, 0x33, 5, 1, 1, 0xc3), scratch).code ==
                ApplyCode::applied,
            "valid source bootstrap failed");
    require(catalog_test::apply(state, source_command(4, 0x34, 5, 1, 3, 0xc4), scratch).code ==
                ApplyCode::source_sequence_gap,
            "sequence gap advanced source state");
    require(catalog_test::apply(state, source_command(5, 0x35, 5, 2, 1, 0xc5), scratch).code ==
                ApplyCode::applied,
            "next source generation did not roll over");
    require(catalog_test::apply(state, source_command(6, 0x36, 5, 1, 2, 0xc6), scratch).code ==
                ApplyCode::source_generation_regression,
            "old unseen generation event was accepted");
    require(catalog_test::apply(state, source_command(7, 0x37, 5, 4, 1, 0xc7), scratch).code ==
                ApplyCode::source_generation_gap,
            "generation gap advanced source state");
    require(catalog_test::apply(state, source_command(8, 0x38, 5, 1, 1, 0xc3), scratch).code ==
                ApplyCode::already_applied,
            "retained old-generation duplicate was reinterpreted as a regression");

    const auto* cursor = find_source_cursor(state, SourceId{5});
    require(cursor != nullptr && cursor->generation == SourceGeneration{2} &&
                cursor->sequence == SourceSequence{1},
            "rejected source event changed the accepted watermark");
}

void test_source_event_requires_identity_and_digest() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;

    const auto missing_source =
        catalog_test::apply(state, source_command(1, 0x41, 0, 1, 1, 0xd1), scratch);
    const auto missing_digest =
        catalog_test::apply(state, source_command(2, 0x42, 1, 1, 1, 0), scratch);
    require(missing_source.code == ApplyCode::invalid_command &&
                missing_source.contract_error == ContractError::invalid_identity &&
                missing_digest.code == ApplyCode::invalid_command &&
                missing_digest.contract_error == ContractError::missing_digest &&
                state.source_count == 0 && state.source_event_count == 0,
            "malformed source evidence entered replicated source state");
}

void test_source_capacities_fail_before_cursor_mutation() {
    using namespace velocita::v2;
    TradingState source_full = catalog_test::state();
    ApplyScratch scratch;
    for (std::size_t index = 0; index < kMaxSources; ++index) {
        const auto ordinal = static_cast<std::uint64_t>(index + 1);
        require(catalog_test::apply(source_full,
                      source_command(ordinal,
                                     static_cast<std::uint8_t>(index + 1),
                                     ordinal,
                                     1,
                                     1,
                                     static_cast<std::uint8_t>(index + 1)),
                      scratch).code == ApplyCode::applied,
                "source table setup failed");
    }
    const auto source_overflow =
        catalog_test::apply(source_full,
              source_command(kMaxSources + 1, 0x71, kMaxSources + 1, 1, 1, 0x72),
              scratch);
    require(source_overflow.code == ApplyCode::source_capacity_exceeded &&
                source_full.source_count == kMaxSources &&
                source_full.source_event_count == kMaxSources,
            "source capacity overflow partially mutated state");

    TradingState event_full = catalog_test::state();
    for (std::size_t index = 0; index < kMaxSourceEventReceipts; ++index) {
        const auto sequence = static_cast<std::uint64_t>(index + 1);
        require(catalog_test::apply(event_full,
                      source_command(sequence,
                                     0x73,
                                     1,
                                     1,
                                     sequence,
                                     static_cast<std::uint8_t>(index + 1)),
                      scratch).code == ApplyCode::applied,
                "source-event receipt setup failed");
    }
    for (std::size_t index = 0; index < kMaxSourceEventReceipts; ++index) {
        event_full.source_events[index] = {
            SourceId{999}, SourceGeneration{1}, SourceSequence{index + 1},
            digest(static_cast<std::uint8_t>((index % 250U) + 1U))};
    }

    const auto event_overflow =
        catalog_test::apply(event_full,
              source_command(kMaxSourceEventReceipts + 1,
                             0x74,
                             1,
                             1,
                             kMaxSourceEventReceipts + 1,
                             0x75),
              scratch);
    const auto* cursor = find_source_cursor(event_full, SourceId{1});
    require(event_overflow.code == ApplyCode::source_event_capacity_exceeded &&
                event_full.source_event_count == kMaxSourceEventReceipts &&
                cursor != nullptr &&
                cursor->sequence == SourceSequence{kMaxSourceEventReceipts},
            "source-event receipt overflow advanced the cursor");
}

void test_generation_gap_reports_the_source_cursor() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;

    const auto unbootstrapped = catalog_test::apply(state, source_command(1, 0x51, 6, 3, 1, 0xe1), scratch);
    require(unbootstrapped.code == ApplyCode::source_generation_gap &&
                unbootstrapped.detail_kind == ApplyResultDetailKind::source_state &&
                unbootstrapped.detail.words == std::array<std::uint64_t, 3>{6, 0, 0},
            "a gap on a source with no cursor did not report the empty cursor");

    require(catalog_test::apply(state, source_command(2, 0x52, 6, 1, 1, 0xe2), scratch).code ==
                    ApplyCode::applied &&
                catalog_test::apply(state, source_command(3, 0x53, 6, 1, 2, 0xe3), scratch).code ==
                    ApplyCode::applied,
            "source setup failed");
    const auto before = state;
    const auto ahead_command = source_command(4, 0x54, 6, 5, 1, 0xe4);
    const auto ahead = catalog_test::apply(state, ahead_command, scratch);
    require(ahead.code == ApplyCode::source_generation_gap &&
                ahead.detail_kind == ApplyResultDetailKind::source_state &&
                ahead.detail.words == std::array<std::uint64_t, 3>{6, 1, 2} &&
                ahead.source_cursor_id() == SourceId{6} &&
                ahead.source_cursor_generation() == SourceGeneration{1} &&
                ahead.source_cursor_sequence() == SourceSequence{2},
            "a generation gap did not report the source's current cursor");
    const auto* cursor = find_source_cursor(state, SourceId{6});
    require(state.source_count == before.source_count && state.source_event_count == before.source_event_count &&
                cursor != nullptr && cursor->generation == SourceGeneration{1} &&
                cursor->sequence == SourceSequence{2},
            "a reported gap changed source state");

    const auto conflict = catalog_test::apply(state, source_command(5, 0x55, 6, 1, 1, 0xe5), scratch);
    const auto sequence_gap = catalog_test::apply(state, source_command(6, 0x56, 6, 1, 4, 0xe6), scratch);
    require(conflict.code == ApplyCode::source_event_conflict &&
                conflict.detail_kind == ApplyResultDetailKind::source &&
                sequence_gap.code == ApplyCode::source_sequence_gap &&
                sequence_gap.detail_kind == ApplyResultDetailKind::source &&
                sequence_gap.detail.words == std::array<std::uint64_t, 3>{6, 1, 4},
            "a refusal other than a generation gap carried the cursor report");

    require(catalog_test::apply(state, source_command(7, 0x57, 6, 2, 1, 0xe7), scratch).code == ApplyCode::applied,
            "the generation after the reported cursor was refused");
    const auto regression = catalog_test::apply(state, source_command(8, 0x58, 6, 1, 3, 0xe8), scratch);
    require(regression.code == ApplyCode::source_generation_regression &&
                regression.detail_kind == ApplyResultDetailKind::source &&
                regression.detail.words == std::array<std::uint64_t, 3>{6, 1, 3},
            "a generation regression carried the cursor report");

    require(catalog_test::apply(state, ahead_command, scratch) == ahead,
            "the exact retry of a reported gap answered differently");
    std::array<std::byte, kKernelResultBytes> encoded_result{};
    const auto encoded = encode_kernel_result(ahead, encoded_result);
    ApplyResult decoded;
    require(encoded.error == KernelCodecError::none &&
                decode_kernel_result({encoded_result.data(), encoded.bytes_written}, decoded).error ==
                    KernelCodecError::none &&
                decoded == ahead,
            "the cursor report did not survive the canonical result codec");
    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    const auto saved = encode_state_snapshot(state, snapshot);
    TradingState restored = catalog_test::state();
    StateSnapshotScratch snapshot_scratch;
    require(saved.error == StateSnapshotError::none &&
                decode_state_snapshot({snapshot.data(), saved.bytes_written}, restored, snapshot_scratch).error ==
                    StateSnapshotError::none,
            "state retaining a cursor report did not round-trip");
    require(catalog_test::apply(restored, ahead_command, scratch) == ahead,
            "a restored replica answered the gap retry differently");
}

void test_source_state_detail_is_validated_by_both_codecs() {
    using namespace velocita::v2;
    ApplyResult result;
    result.code = ApplyCode::source_generation_gap;
    result.detail_kind = ApplyResultDetailKind::source_state;
    result.detail = ApplyResultDetail::source_state(SourceId{41}, SourceGeneration{2}, SourceSequence{9});
    const auto retaining = [](const ApplyResult& retained_result) {
        TradingState value = catalog_test::state();
        value.state_version = 5;
        value.caller_cursor_count = 1;
        value.caller_cursors[0] = {CallerId{1}, {}, {}};
        value.receipt_count = 1;
        value.receipts[0] = {{CallerId{1}, RequestSequence{77}, digest(0x31)}, retained_result};
        return value;
    };
    std::array<std::byte, kKernelResultBytes> bytes{};
    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    const auto admitted = [&](const ApplyResult& value, const std::string& what) {
        ApplyResult decoded;
        const auto encoded = encode_kernel_result(value, bytes);
        require(encoded.error == KernelCodecError::none &&
                    decode_kernel_result({bytes.data(), encoded.bytes_written}, decoded).error ==
                        KernelCodecError::none &&
                    decoded == value,
                "result codec refused " + what);
        const auto state = retaining(value);
        require(encode_state_snapshot(state, snapshot).error == StateSnapshotError::none,
                "snapshot refused " + what);
    };
    admitted(result, "a cursor report");
    auto empty = result;
    empty.detail.words = {41, 0, 0};
    admitted(empty, "the report of a source with no cursor");

    const auto refused = [&](auto change, const std::string& what) {
        auto invalid = result;
        change(invalid);
        require(encode_kernel_result(invalid, bytes).error == KernelCodecError::invalid_value,
                "result codec accepted " + what);
        const auto state = retaining(invalid);
        require(encode_state_snapshot(state, snapshot).error == StateSnapshotError::invalid_state,
                "snapshot accepted " + what);
    };
    refused([](ApplyResult& r) { r.code = ApplyCode::source_generation_regression; },
            "a cursor report on a generation regression");
    refused([](ApplyResult& r) { r.code = ApplyCode::source_sequence_gap; }, "a cursor report on a sequence gap");
    refused([](ApplyResult& r) { r.code = ApplyCode::applied; }, "a cursor report on an applied event");
    refused([](ApplyResult& r) { r.detail.words[0] = 0; }, "a cursor report without a source");
    refused([](ApplyResult& r) { r.detail.words[1] = 0; }, "a cursor with a sequence but no generation");
    refused([](ApplyResult& r) { r.detail.words[2] = 0; }, "a cursor with a generation but no sequence");
}

}

int main() {
    try {
        test_contiguous_events_and_exact_event_duplicates();
        test_conflicting_event_identity_is_retained_without_cursor_mutation();
        test_gaps_and_regressions_fail_closed();
        test_source_event_requires_identity_and_digest();
        test_source_capacities_fail_before_cursor_mutation();
        test_generation_gap_reports_the_source_cursor();
        test_source_state_detail_is_validated_by_both_codecs();
        std::cout << "all V2 kernel source tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 kernel source test failed: " << error.what() << '\n';
        return 1;
    }
}
