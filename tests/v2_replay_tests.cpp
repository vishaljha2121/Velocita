#include "v2_catalog_test_support.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/replay.hpp"
#include "velocita/v2/state_snapshot.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

velocita::v2::Digest256 digest(const std::uint8_t marker) {
    velocita::v2::Digest256 value;
    value.bytes[0] = static_cast<std::byte>(marker);
    return value;
}

velocita::v2::PolicyArtifact policy() {
    using namespace velocita::v2;
    PolicyArtifact value;
    value.policy_id = PolicyId{17};
    value.policy_version = PolicyVersion{1};
    value.schema_version = kPolicyArtifactSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{3};
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.model_digest = digest(0x11);
    value.artifact_digest = digest(0x22);
    value.valid_from_event_time_ns = 100;
    value.valid_until_event_time_ns = 200;
    value.risk_policy_version = RiskPolicyVersion{6};
    value.rule_count = 1;
    value.rules[0] = PolicyRule{0, PredicateOperator::greater_than,
                                FeatureMicros::from_atoms(7),
                                Directive::place_limit};
    return value;
}

velocita::v2::DecisionFrame frame() {
    using namespace velocita::v2;
    DecisionFrame value;
    value.policy_id = PolicyId{17};
    value.policy_version = PolicyVersion{1};
    value.feature_schema_id = FeatureSchemaId{3};
    value.venue_id = VenueId{7};
    value.venue_session_id = VenueSessionId{8};
    value.venue_sequence = VenueSequence{9};
    value.source_id = SourceId{9};
    value.source_generation = SourceGeneration{1};
    value.source_sequence = SourceSequence{1};
    value.event_time_ns = 150;
    value.receive_time_ns = 151;
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.feature_count = 1;
    value.features[0] = FeatureMicros::from_atoms(8);
    value.market_data_status = MarketDataStatus::ready;
    value.market_data_age_ns = 1;
    value.position_version = PositionVersion{10};
    value.risk_version = RiskVersion{11};
    value.raw_evidence_digest = digest(0x61);
    return value;
}

// These fixtures issue control commands, so their caller has to be one the edge
// would have authenticated - see `kMaxOperatorCallerId`. The number is otherwise
// arbitrary.
constexpr velocita::v2::CallerId kFixtureOperator{7};

velocita::v2::Command command(const std::uint64_t sequence,
                              const std::uint8_t marker,
                              velocita::v2::CommandBody body) {
    using namespace velocita::v2;
    return Command{CommandIdentity{kFixtureOperator, RequestSequence{sequence},
                                   digest(marker)},
                   std::move(body)};
}

struct EncodedCommand {
    std::array<std::byte, velocita::v2::kMaxKernelCommandBytes> bytes{};
    std::size_t size{};
};

EncodedCommand encoded(const velocita::v2::Command& command) {
    using namespace velocita::v2;
    EncodedCommand value;
    const auto result = encode_kernel_command(command, value.bytes);
    require(result.error == KernelCodecError::none, "corpus command did not encode");
    value.size = result.bytes_written;
    return value;
}

auto corpus() {
    using namespace velocita::v2;
    auto invalid = frame();
    invalid.account_id = AccountId{};
    std::array<EncodedCommand, 12> records{
        encoded(command(1, 0x31, PublishPolicy{policy()})),
        encoded(command(2, 0x32, ValidatePolicy{PolicyId{17}, PolicyVersion{1}})),
        encoded(command(3, 0x33, ActivatePolicy{PolicyId{17}, PolicyVersion{1}})),
        encoded(command(4, 0x34, SetExecutorGeneration{ExecutorGeneration{1}})),
        encoded(command(5, 0x35, AdvanceKillEpoch{KillEpoch{1}})),
        encoded(command(6, 0x36,
                        RecordSourceEvent{SourceId{9}, SourceGeneration{1},
                                          SourceSequence{1}, digest(0x61)})),
        {},
        encoded(command(7, 0x37,
                        RecordSourceEvent{SourceId{9}, SourceGeneration{1},
                                          SourceSequence{2}, digest(0x62)})),
        encoded(command(8, 0x38,
                        RecordSourceEvent{SourceId{9}, SourceGeneration{1},
                                          SourceSequence{4}, digest(0x64)})),
        encoded(command(9, 0x39,
                        SetStrategyMode{StrategyMode::normal, KillEpoch{1},
                                        ExecutorGeneration{1}})),
        encoded(command(10, 0x3a,
                        EvaluateDecisionFrame{DecisionId{20},
                                              EvaluationMode::authoritative,
                                              frame()})),
        encoded(command(11, 0x3b,
                        EvaluateDecisionFrame{DecisionId{21},
                                              EvaluationMode::authoritative,
                                              invalid})),
    };
    records[6] = records[5];  // Exact retry crosses the snapshot boundary.
    return records;
}

struct EncodedResult {
    std::array<std::byte, velocita::v2::kKernelResultBytes> bytes{};
    std::size_t size{};
};

EncodedResult apply_record(velocita::v2::TradingState& state,
                           const EncodedCommand& command,
                           velocita::v2::ApplyScratch& apply_scratch,
                           velocita::v2::KernelCommandCodecScratch& codec_scratch) {
    using namespace velocita::v2;
    EncodedResult encoded_result;
    const auto replayed = catalog_test::replay(
        state, {command.bytes.data(), command.size}, apply_scratch, codec_scratch,
        encoded_result.bytes);
    require(replayed.error == ReplayError::none,
            "encoded command did not apply through replay boundary");
    encoded_result.size = replayed.result_bytes_written;
    return encoded_result;
}

void test_snapshot_plus_suffix_equals_full_byte_replay() {
    using namespace velocita::v2;
    constexpr std::size_t cut = 6;
    const auto records = corpus();

    TradingState full = catalog_test::state();
    ApplyScratch full_apply_scratch;
    KernelCommandCodecScratch full_codec_scratch;
    std::array<EncodedResult, 12> full_results{};
    std::array<std::byte, kMaxStateSnapshotBytes> midpoint_bytes{};
    std::size_t midpoint_size{};

    for (std::size_t index = 0; index < records.size(); ++index) {
        if (index == cut) {
            const auto midpoint = encode_state_snapshot(full, midpoint_bytes);
            require(midpoint.error == StateSnapshotError::none,
                    "midpoint state did not snapshot");
            midpoint_size = midpoint.bytes_written;
        }
        full_results[index] =
            apply_record(full, records[index], full_apply_scratch, full_codec_scratch);
    }

    TradingState restored = catalog_test::state();
    StateSnapshotScratch snapshot_scratch;
    const auto restored_result = decode_state_snapshot(
        {midpoint_bytes.data(), midpoint_size}, restored, snapshot_scratch);
    require(restored_result.error == StateSnapshotError::none,
            "midpoint snapshot did not restore");

    ApplyScratch suffix_apply_scratch;
    KernelCommandCodecScratch suffix_codec_scratch;
    for (std::size_t index = cut; index < records.size(); ++index) {
        const auto suffix_result = apply_record(
            restored, records[index], suffix_apply_scratch, suffix_codec_scratch);
        require(suffix_result.size == full_results[index].size &&
                    std::equal(suffix_result.bytes.begin(),
                               suffix_result.bytes.begin() +
                                   static_cast<std::ptrdiff_t>(suffix_result.size),
                               full_results[index].bytes.begin()),
                "snapshot-plus-suffix returned different canonical result bytes");
    }

    std::array<std::byte, kMaxStateSnapshotBytes> full_bytes{};
    std::array<std::byte, kMaxStateSnapshotBytes> restored_bytes{};
    const auto full_snapshot = encode_state_snapshot(full, full_bytes);
    const auto suffix_snapshot = encode_state_snapshot(restored, restored_bytes);
    require(full_snapshot.error == StateSnapshotError::none &&
                suffix_snapshot.error == StateSnapshotError::none &&
                full_snapshot.state_digest == suffix_snapshot.state_digest &&
                full_snapshot.bytes_written == suffix_snapshot.bytes_written &&
                std::equal(full_bytes.begin(),
                           full_bytes.begin() + static_cast<std::ptrdiff_t>(
                                                    full_snapshot.bytes_written),
                           restored_bytes.begin()),
            "full replay and snapshot-plus-suffix did not converge byte-for-byte");

    const auto final_digest = to_hex(full_snapshot.state_digest);
    // RE-PINNED 2026-09-16 for a reviewed fixture change. Control actions are now
    // accepted only from the reserved operator caller band
    // (`kMaxOperatorCallerId`), and this corpus issues them from a caller that
    // was outside it, so the corpus's caller moved into the band. The commands,
    // their order and the reducer are unchanged; what moved is the identity
    // every command carries, and the receipts that record it.
    require(to_hex(catalog_test::schema7_projection(full)) ==
                "3be6f42d0748bb6fa7b549febf59ffd6da70363865748e430340f32a38c9359d",
            "replay corpus final state changed without reviewed migration: " +
                final_digest);
}

void test_replay_preflight_failure_does_not_mutate_state() {
    using namespace velocita::v2;
    const auto record = corpus()[0];
    TradingState state = catalog_test::state();
    ApplyScratch apply_scratch;
    KernelCommandCodecScratch codec_scratch;
    std::array<std::byte, 8> too_small{};

    const auto rejected = catalog_test::replay(
        state, {record.bytes.data(), record.size}, apply_scratch, codec_scratch,
        too_small);
    require(rejected.error == ReplayError::result_output_too_small &&
                state.state_version == 0 && state.receipt_count == 0,
            "result-buffer preflight failed after mutating state");

    auto corrupt = record;
    corrupt.bytes[20] ^= std::byte{1};
    std::array<std::byte, kKernelResultBytes> result_bytes{};
    const auto invalid = catalog_test::replay(
        state, {corrupt.bytes.data(), corrupt.size}, apply_scratch, codec_scratch,
        result_bytes);
    require(invalid.error == ReplayError::command_decode_failed &&
                invalid.codec_error == KernelCodecError::digest_mismatch &&
                state.state_version == 0 && state.receipt_count == 0,
            "invalid replay record mutated authoritative state");
}

}  // namespace

int main() {
    try {
        test_snapshot_plus_suffix_equals_full_byte_replay();
        test_replay_preflight_failure_does_not_mutate_state();
        std::cout << "all V2 replay tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 replay test failed: " << error.what() << '\n';
        return 1;
    }
}
