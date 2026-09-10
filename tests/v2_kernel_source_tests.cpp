#include "velocita/v2/kernel.hpp"

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
    TradingState state;
    ApplyScratch scratch;

    const auto first = apply(state, source_command(1, 0x11, 7, 1, 1, 0xa1), scratch);
    require(first.code == ApplyCode::applied && first.source_id() == SourceId{7} &&
                first.source_generation() == SourceGeneration{1} &&
                first.source_sequence() == SourceSequence{1},
            "first source event was not accepted");

    const auto duplicate =
        apply(state, source_command(2, 0x12, 7, 1, 1, 0xa1), scratch);
    require(duplicate.code == ApplyCode::already_applied &&
                state.source_event_count == 1 && state.state_version == 2,
            "same source event delivered under another request was not a no-op");

    const auto second = apply(state, source_command(3, 0x13, 7, 1, 2, 0xa2), scratch);
    const auto* cursor = find_source_cursor(state, SourceId{7});
    require(second.code == ApplyCode::applied && cursor != nullptr &&
                cursor->generation == SourceGeneration{1} &&
                cursor->sequence == SourceSequence{2} && state.source_event_count == 2,
            "contiguous source sequence did not advance its cursor");
}

void test_conflicting_event_identity_is_retained_without_cursor_mutation() {
    using namespace velocita::v2;
    TradingState state;
    ApplyScratch scratch;
    require(apply(state, source_command(1, 0x21, 3, 1, 1, 0xb1), scratch).code ==
                ApplyCode::applied,
            "source setup failed");

    const auto conflict_command = source_command(2, 0x22, 3, 1, 1, 0xb2);
    const auto conflict = apply(state, conflict_command, scratch);
    const auto* cursor = find_source_cursor(state, SourceId{3});
    require(conflict.code == ApplyCode::source_event_conflict && cursor != nullptr &&
                cursor->sequence == SourceSequence{1} && state.source_event_count == 1,
            "conflicting source identity mutated the cursor");
    require(apply(state, conflict_command, scratch) == conflict && state.state_version == 2,
            "conflicting source result was not retained exactly");
}

void test_gaps_and_regressions_fail_closed() {
    using namespace velocita::v2;
    TradingState state;
    ApplyScratch scratch;

    require(apply(state, source_command(1, 0x31, 5, 2, 1, 0xc1), scratch).code ==
                ApplyCode::source_generation_gap,
            "unbootstrapped generation was accepted");
    require(apply(state, source_command(2, 0x32, 5, 1, 2, 0xc2), scratch).code ==
                ApplyCode::source_sequence_gap,
            "unbootstrapped sequence was accepted");
    require(apply(state, source_command(3, 0x33, 5, 1, 1, 0xc3), scratch).code ==
                ApplyCode::applied,
            "valid source bootstrap failed");
    require(apply(state, source_command(4, 0x34, 5, 1, 3, 0xc4), scratch).code ==
                ApplyCode::source_sequence_gap,
            "sequence gap advanced source state");
    require(apply(state, source_command(5, 0x35, 5, 2, 1, 0xc5), scratch).code ==
                ApplyCode::applied,
            "next source generation did not roll over");
    require(apply(state, source_command(6, 0x36, 5, 1, 2, 0xc6), scratch).code ==
                ApplyCode::source_generation_regression,
            "old unseen generation event was accepted");
    require(apply(state, source_command(7, 0x37, 5, 4, 1, 0xc7), scratch).code ==
                ApplyCode::source_generation_gap,
            "generation gap advanced source state");
    require(apply(state, source_command(8, 0x38, 5, 1, 1, 0xc3), scratch).code ==
                ApplyCode::already_applied,
            "retained old-generation duplicate was reinterpreted as a regression");

    const auto* cursor = find_source_cursor(state, SourceId{5});
    require(cursor != nullptr && cursor->generation == SourceGeneration{2} &&
                cursor->sequence == SourceSequence{1},
            "rejected source event changed the accepted watermark");
}

void test_source_event_requires_identity_and_digest() {
    using namespace velocita::v2;
    TradingState state;
    ApplyScratch scratch;

    const auto missing_source =
        apply(state, source_command(1, 0x41, 0, 1, 1, 0xd1), scratch);
    const auto missing_digest =
        apply(state, source_command(2, 0x42, 1, 1, 1, 0), scratch);
    require(missing_source.code == ApplyCode::invalid_command &&
                missing_source.contract_error == ContractError::invalid_identity &&
                missing_digest.code == ApplyCode::invalid_command &&
                missing_digest.contract_error == ContractError::missing_digest &&
                state.source_count == 0 && state.source_event_count == 0,
            "malformed source evidence entered replicated source state");
}

void test_source_capacities_fail_before_cursor_mutation() {
    using namespace velocita::v2;
    TradingState source_full;
    ApplyScratch scratch;
    for (std::size_t index = 0; index < kMaxSources; ++index) {
        const auto ordinal = static_cast<std::uint64_t>(index + 1);
        require(apply(source_full,
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
        apply(source_full,
              source_command(kMaxSources + 1, 0x71, kMaxSources + 1, 1, 1, 0x72),
              scratch);
    require(source_overflow.code == ApplyCode::source_capacity_exceeded &&
                source_full.source_count == kMaxSources &&
                source_full.source_event_count == kMaxSources,
            "source capacity overflow partially mutated state");

    TradingState event_full;
    for (std::size_t index = 0; index < kMaxSourceEventReceipts; ++index) {
        const auto sequence = static_cast<std::uint64_t>(index + 1);
        require(apply(event_full,
                      source_command(sequence,
                                     0x73,
                                     1,
                                     1,
                                     sequence,
                                     static_cast<std::uint8_t>(index + 1)),
                      scratch).code == ApplyCode::applied,
                "source-event receipt setup failed");
    }
    // Source-event receipts are reclaimable once their source cursor has advanced
    // past them (architecture 8.6), so the events admitted above would all be
    // swept and the arena would never overflow. Replace them with events for a
    // source that has NO cursor: nothing can vouch for those, so they are never
    // reclaimed and this stays a test of the capacity path, not of the sweep.
    for (std::size_t index = 0; index < kMaxSourceEventReceipts; ++index) {
        event_full.source_events[index] = {
            SourceId{999}, SourceGeneration{1}, SourceSequence{index + 1},
            digest(static_cast<std::uint8_t>((index % 250U) + 1U))};
    }

    const auto event_overflow =
        apply(event_full,
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

}  // namespace

int main() {
    try {
        test_contiguous_events_and_exact_event_duplicates();
        test_conflicting_event_identity_is_retained_without_cursor_mutation();
        test_gaps_and_regressions_fail_closed();
        test_source_event_requires_identity_and_digest();
        test_source_capacities_fail_before_cursor_mutation();
        std::cout << "all V2 kernel source tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 kernel source test failed: " << error.what() << '\n';
        return 1;
    }
}
