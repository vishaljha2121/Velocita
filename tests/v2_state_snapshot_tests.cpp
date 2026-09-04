#include "velocita/v2/digest.hpp"
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
    value.policy_version = PolicyVersion{2};
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
    value.rules[0] = PolicyRule{7,
                                PredicateOperator::greater_than_or_equal,
                                FeatureMicros::from_atoms(-1'250'000),
                                Directive::place_limit};
    return value;
}

velocita::v2::TradingState populated_state() {
    using namespace velocita::v2;
    TradingState state;
    state.state_version = 9;
    state.strategy_mode = StrategyMode::reduce_only;
    state.kill_epoch = KillEpoch{4};
    state.executor_generation = ExecutorGeneration{8};

    state.policy_count = 1;
    state.policies[0] = PolicyRecord{policy(), PolicyLifecycle::active};

    state.source_count = 1;
    state.sources[0] = SourceCursor{SourceId{10}, SourceGeneration{2}, SourceSequence{3}};

    state.source_event_count = 1;
    state.source_events[0] = SourceEventReceipt{
        SourceId{10}, SourceGeneration{2}, SourceSequence{3}, digest(0x33)};

    state.receipt_count = 1;
    state.receipts[0] = RequestReceipt{
        CommandIdentity{CallerId{12}, RequestSequence{13}, digest(0x44)},
        ApplyResult{ApplyCode::applied,
                    ContractError::none,
                    PolicyId{17},
                    PolicyVersion{2},
                    PolicyLifecycle::active,
                    9,
                    StrategyMode::reduce_only,
                    KillEpoch{4},
                    ExecutorGeneration{8},
                    SourceId{10},
                    SourceGeneration{2},
                    SourceSequence{3}}};
    return state;
}

void write_u16(std::span<std::byte> bytes, const std::size_t offset,
               const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void replace_integrity_digest(std::span<std::byte> frame) {
    using namespace velocita::v2;
    const auto digest_offset = frame.size() - kDigestBytes;
    const auto calculated = sha256_digest(frame.subspan(kStateSnapshotDigestInputOffset,
                                                        digest_offset -
                                                            kStateSnapshotDigestInputOffset));
    std::copy(calculated.bytes.begin(), calculated.bytes.end(),
              frame.begin() + static_cast<std::ptrdiff_t>(digest_offset));
}

void test_empty_snapshot_has_canonical_layout() {
    using namespace velocita::v2;
    TradingState state;
    std::array<std::byte, kMaxStateSnapshotBytes> storage{};

    const auto encoded = encode_state_snapshot(state, storage);
    require(encoded.error == StateSnapshotError::none,
            "empty state did not encode");
    require(encoded.bytes_written == kStateSnapshotFixedBytes,
            "empty state snapshot length is not canonical");

    const std::array expected_prefix{
        std::byte{'V'}, std::byte{'2'}, std::byte{'S'}, std::byte{'T'},
        std::byte{33}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{1}, std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{5}};
    require(std::equal(expected_prefix.begin(), expected_prefix.end(), storage.begin()),
            "empty state prefix changed");

    const auto digest_offset = encoded.bytes_written - kDigestBytes;
    require(std::equal(encoded.state_digest.bytes.begin(), encoded.state_digest.bytes.end(),
                       storage.begin() + static_cast<std::ptrdiff_t>(digest_offset)),
            "returned digest differs from framed digest");
    require(to_hex(encoded.state_digest) ==
                "0e737df191c265e7d2a4825c229ffe5220a4fc0e9a9a679731ac475ea5e44eb8",
            "empty state golden digest changed without a schema migration");
}

void test_round_trip_is_canonical_and_excludes_unused_capacity() {
    using namespace velocita::v2;
    auto original = populated_state();
    auto same_logical_state = original;
    same_logical_state.policies[7].artifact.policy_id = PolicyId{999};
    same_logical_state.sources[9].source_id = SourceId{999};
    same_logical_state.source_events[31].event_digest = digest(0xee);
    same_logical_state.receipts[42].identity.payload_digest = digest(0xdd);

    std::array<std::byte, kMaxStateSnapshotBytes> first{};
    std::array<std::byte, kMaxStateSnapshotBytes> second{};
    const auto encoded = encode_state_snapshot(original, first);
    const auto same = encode_state_snapshot(same_logical_state, second);
    require(encoded.error == StateSnapshotError::none &&
                same.error == StateSnapshotError::none,
            "valid populated state did not encode");
    require(encoded.bytes_written == same.bytes_written &&
                encoded.state_digest == same.state_digest &&
                std::equal(first.begin(),
                           first.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written),
                           second.begin()),
            "unused array capacity leaked into canonical bytes");

    TradingState restored;
    restored.state_version = 777;
    StateSnapshotScratch scratch;
    const auto decoded = decode_state_snapshot(
        std::span<const std::byte>{first.data(), encoded.bytes_written}, restored, scratch);
    require(decoded.error == StateSnapshotError::none &&
                decoded.state_digest == encoded.state_digest,
            "populated state did not decode");

    std::array<std::byte, kMaxStateSnapshotBytes> replayed{};
    const auto reencoded = encode_state_snapshot(restored, replayed);
    require(reencoded.error == StateSnapshotError::none &&
                reencoded.bytes_written == encoded.bytes_written &&
                std::equal(first.begin(),
                           first.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written),
                           replayed.begin()),
            "decode followed by encode changed canonical state bytes");
}

void test_decode_is_transactional_for_corrupt_and_unknown_input() {
    using namespace velocita::v2;
    const auto state = populated_state();
    std::array<std::byte, kMaxStateSnapshotBytes> storage{};
    const auto encoded = encode_state_snapshot(state, storage);
    require(encoded.error == StateSnapshotError::none, "fixture did not encode");
    const auto valid = std::span<const std::byte>{storage.data(), encoded.bytes_written};

    const TradingState sentinel = [] {
        TradingState value;
        value.state_version = 0xfeed;
        value.strategy_mode = StrategyMode::halted;
        value.kill_epoch = KillEpoch{7};
        value.executor_generation = ExecutorGeneration{8};
        return value;
    }();
    std::array<std::byte, kMaxStateSnapshotBytes> sentinel_bytes{};
    const auto sentinel_encoded = encode_state_snapshot(sentinel, sentinel_bytes);
    require(sentinel_encoded.error == StateSnapshotError::none,
            "transactional sentinel did not encode");
    StateSnapshotScratch scratch;

    auto assert_rejected_without_publish = [&](const StateSnapshotError expected,
                                                std::span<const std::byte> bytes,
                                                const std::string& context) {
        auto destination = sentinel;
        const auto result = decode_state_snapshot(bytes, destination, scratch);
        require(result.error == expected, context + " returned the wrong error");
        std::array<std::byte, kMaxStateSnapshotBytes> after_bytes{};
        const auto after = encode_state_snapshot(destination, after_bytes);
        require(after.error == StateSnapshotError::none &&
                    after.bytes_written == sentinel_encoded.bytes_written &&
                    std::equal(sentinel_bytes.begin(),
                               sentinel_bytes.begin() + static_cast<std::ptrdiff_t>(
                                                           sentinel_encoded.bytes_written),
                               after_bytes.begin()),
                context + " changed destination state before validation completed");
    };

    auto corrupt = storage;
    corrupt[20] ^= std::byte{1};
    assert_rejected_without_publish(
        StateSnapshotError::digest_mismatch,
        {corrupt.data(), encoded.bytes_written}, "digest corruption");
    assert_rejected_without_publish(
        StateSnapshotError::truncated, valid.first(valid.size() - 1), "truncation");

    auto bad_magic = storage;
    bad_magic[0] = std::byte{'X'};
    assert_rejected_without_publish(
        StateSnapshotError::invalid_magic,
        {bad_magic.data(), encoded.bytes_written}, "bad magic");

    auto trailing = storage;
    trailing[encoded.bytes_written] = std::byte{0};
    assert_rejected_without_publish(
        StateSnapshotError::length_mismatch,
        {trailing.data(), encoded.bytes_written + 1}, "trailing bytes");

    auto unknown = storage;
    write_u16(unknown, kStateSnapshotSchemaOffset, 2);
    replace_integrity_digest({unknown.data(), encoded.bytes_written});
    assert_rejected_without_publish(
        StateSnapshotError::unsupported_schema,
        {unknown.data(), encoded.bytes_written}, "unknown schema");

    auto invalid_enum = storage;
    invalid_enum[kStateSnapshotPayloadOffset + sizeof(std::uint64_t)] = std::byte{99};
    replace_integrity_digest({invalid_enum.data(), encoded.bytes_written});
    assert_rejected_without_publish(
        StateSnapshotError::invalid_state,
        {invalid_enum.data(), encoded.bytes_written}, "unknown strategy mode");
}

void test_bounds_and_invalid_source_state_fail_closed() {
    using namespace velocita::v2;
    const auto state = populated_state();
    std::array<std::byte, kMaxStateSnapshotBytes> storage{};
    const auto encoded = encode_state_snapshot(state, storage);
    require(encoded.error == StateSnapshotError::none, "fixture did not encode");

    std::array<std::byte, 16> too_small{};
    const auto bounded = encode_state_snapshot(state, too_small);
    require(bounded.error == StateSnapshotError::output_too_small &&
                bounded.bytes_written == 0,
            "undersized output was partially reported as encoded");

    auto invalid = state;
    invalid.policy_count = kMaxPolicies + 1;
    const auto rejected = encode_state_snapshot(invalid, storage);
    require(rejected.error == StateSnapshotError::invalid_state &&
                rejected.bytes_written == 0,
            "invalid in-memory state was serialized");
}

void test_declared_maximum_is_exact_and_encodable() {
    using namespace velocita::v2;
    TradingState state;
    state.state_version = kMaxRequestReceipts;

    state.policy_count = kMaxPolicies;
    for (std::size_t index = 0; index < state.policy_count; ++index) {
        auto artifact = policy();
        artifact.policy_id = PolicyId{index + 1};
        artifact.account_id = AccountId{index + 1};
        artifact.instrument_id = InstrumentId{index + 1};
        artifact.rule_count = kMaxPolicyRules;
        for (std::size_t rule = 0; rule < artifact.rule_count; ++rule) {
            artifact.rules[rule] =
                PolicyRule{static_cast<std::uint16_t>(rule),
                           PredicateOperator::equal,
                           FeatureMicros::from_atoms(static_cast<std::int64_t>(rule)),
                           Directive::no_action};
        }
        state.policies[index] =
            PolicyRecord{artifact, PolicyLifecycle::proposed};
    }

    state.source_count = kMaxSources;
    for (std::size_t index = 0; index < state.source_count; ++index) {
        state.sources[index] = SourceCursor{SourceId{index + 1},
                                            SourceGeneration{1},
                                            SourceSequence{8}};
    }
    state.source_event_count = kMaxSourceEventReceipts;
    for (std::size_t index = 0; index < state.source_event_count; ++index) {
        state.source_events[index] =
            SourceEventReceipt{SourceId{index / 8 + 1},
                               SourceGeneration{1},
                               SourceSequence{index % 8 + 1},
                               digest(static_cast<std::uint8_t>(index + 1))};
    }

    state.receipt_count = kMaxRequestReceipts;
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        state.receipts[index] = RequestReceipt{
            CommandIdentity{CallerId{1}, RequestSequence{index + 1}, digest(0xaa)},
            ApplyResult{ApplyCode::applied,
                        ContractError::none,
                        {},
                        {},
                        PolicyLifecycle::none,
                        index + 1,
                        StrategyMode::halted,
                        {},
                        {},
                        {},
                        {},
                        {}}};
    }

    std::array<std::byte, kMaxStateSnapshotBytes> storage{};
    const auto encoded = encode_state_snapshot(state, storage);
    require(encoded.error == StateSnapshotError::none &&
                encoded.bytes_written == kMaxStateSnapshotBytes,
            "public maximum does not exactly bound the largest valid snapshot");
}

}  // namespace

int main() {
    try {
        test_empty_snapshot_has_canonical_layout();
        test_round_trip_is_canonical_and_excludes_unused_capacity();
        test_decode_is_transactional_for_corrupt_and_unknown_input();
        test_bounds_and_invalid_source_state_fail_closed();
        test_declared_maximum_is_exact_and_encodable();
        std::cout << "all V2 state snapshot tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 state snapshot test failed: " << error.what() << '\n';
        return 1;
    }
}
