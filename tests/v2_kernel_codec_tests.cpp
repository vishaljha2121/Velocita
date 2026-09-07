#include "velocita/v2/digest.hpp"
#include "velocita/v2/kernel_codec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>

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

velocita::v2::DecisionFrame frame() {
    using namespace velocita::v2;
    DecisionFrame value;
    value.policy_id = PolicyId{17};
    value.policy_version = PolicyVersion{2};
    value.feature_schema_id = FeatureSchemaId{3};
    value.venue_id = VenueId{8};
    value.venue_session_id = VenueSessionId{9};
    value.venue_sequence = VenueSequence{10};
    value.source_id = SourceId{10};
    value.source_generation = SourceGeneration{2};
    value.source_sequence = SourceSequence{3};
    value.event_time_ns = 150;
    value.receive_time_ns = 151;
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.feature_count = 1;
    value.features[0] = FeatureMicros::from_atoms(9);
    value.market_data_status = MarketDataStatus::ready;
    value.market_data_age_ns = 1;
    value.position_version = PositionVersion{11};
    value.risk_version = RiskVersion{12};
    value.raw_evidence_digest = digest(0x55);
    return value;
}

velocita::v2::DecisionFrame invalid_frame() {
    auto value = frame();
    value.account_id = velocita::v2::AccountId{};
    return value;
}

velocita::v2::StageExecutablePolicy staged_policy() {
    using namespace velocita::v2;
    StageExecutablePolicy value;
    value.feature_schema.schema_version = kFeatureSchemaArtifactSchemaVersion;
    value.feature_schema.feature_schema_id = FeatureSchemaId{3};
    value.feature_schema.feature_count = 8;
    for (std::size_t index = 0; index < 8; ++index) {
        value.feature_schema.features[index] =
            {FeatureEconomicKind::price, 1};
    }
    value.feature_schema.artifact_digest =
        compute_artifact_digest(value.feature_schema).digest;

    auto& executable = value.executable_policy;
    executable.schema_version = kExecutablePolicyArtifactSchemaVersion;
    executable.policy_id = PolicyId{17};
    executable.policy_version = PolicyVersion{2};
    executable.policy_artifact_digest = policy().artifact_digest;
    executable.feature_schema_id = FeatureSchemaId{3};
    executable.feature_schema_digest = value.feature_schema.artifact_digest;
    executable.template_count = 1;
    auto& action = executable.templates[0];
    action.schema_version = kActionTemplateSchemaVersion;
    action.directive = Directive::place_limit;
    action.side = Side::buy;
    action.time_in_force = TimeInForce::good_til_cancel;
    action.post_only = PostOnlyMode::required;
    action.order_slot = LogicalOrderSlot{1};
    action.instrument_spec_version = InstrumentSpecVersion{6};
    action.venue_capabilities_version = VenueCapabilitiesVersion{7};
    action.price_source = {EconomicValueSourceKind::feature, 7,
                           RoundingMode::toward_zero, {}, 1, {}};
    action.quantity_source = {EconomicValueSourceKind::fixed, 0,
                              RoundingMode::toward_zero,
                              QuantityLots::from_atoms(1), 0};
    action.safety_caps = {QuantityLots::from_atoms(2),
                          MoneyMicros::from_atoms(1000),
                          PriceTicks::from_atoms(2), PriceTicks::from_atoms(1),
                          100, action.price_source};
    executable.rule_binding_count = 1;
    executable.rule_template_slots[0] = ActionTemplateSlot{1};
    executable.artifact_digest = compute_artifact_digest(executable).digest;
    value.instrument = {kInstrumentExecutionProfileSchemaVersion, VenueId{8},
                        InstrumentId{4}, InstrumentSpecVersion{6},
                        MoneyMicros::from_atoms(10), PriceTicks::from_atoms(1),
                        PriceTicks::from_atoms(100), QuantityLots::from_atoms(1),
                        QuantityLots::from_atoms(10)};
    value.venue = {kVenueCapabilitiesSchemaVersion, VenueId{8},
                   VenueCapabilitiesVersion{7}, kVenueDirectivePlaceLimit,
                   kVenueTifGtc, 1};
    return value;
}

velocita::v2::Command command(const std::uint64_t sequence,
                              velocita::v2::CommandBody body) {
    using namespace velocita::v2;
    return Command{CommandIdentity{CallerId{12}, RequestSequence{sequence}, digest(0x44)},
                   std::move(body)};
}

void write_u16(std::span<std::byte> bytes, const std::size_t offset,
               const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void replace_command_digest(std::span<std::byte> frame) {
    using namespace velocita::v2;
    const auto digest_offset = frame.size() - kDigestBytes;
    const auto calculated = sha256_digest(frame.subspan(
        kKernelCommandDigestInputOffset,
        digest_offset - kKernelCommandDigestInputOffset));
    std::copy(calculated.bytes.begin(), calculated.bytes.end(),
              frame.begin() + static_cast<std::ptrdiff_t>(digest_offset));
}

void test_every_command_alternative_round_trips_canonically() {
    using namespace velocita::v2;
    const std::array commands{
        command(1, PublishPolicy{policy()}),
        command(2, ValidatePolicy{PolicyId{17}, PolicyVersion{2}}),
        command(3, ActivatePolicy{PolicyId{17}, PolicyVersion{2}}),
        command(4, SupersedePolicy{PolicyId{17}, PolicyVersion{2}}),
        command(5, SetStrategyMode{StrategyMode::cancel_only, KillEpoch{4},
                                   ExecutorGeneration{8}}),
        command(6, AdvanceKillEpoch{KillEpoch{5}}),
        command(7, SetExecutorGeneration{ExecutorGeneration{9}}),
        command(8, RecordSourceEvent{SourceId{10}, SourceGeneration{2},
                                     SourceSequence{3}, digest(0x55)}),
        command(9, EvaluateDecisionFrame{DecisionId{21},
                                         EvaluationMode::shadow, frame()}),
        command(10, EvaluateDecisionFrame{DecisionId{22},
                                          EvaluationMode::authoritative,
                                          invalid_frame()}),
        command(11, staged_policy()),
        command(12, ActivateExecutablePolicy{
                        PolicyId{17}, PolicyVersion{2},
                        staged_policy().executable_policy.artifact_digest}),
    };

    std::size_t command_index{};
    for (const auto& original : commands) {
        std::array<std::byte, kMaxKernelCommandBytes> bytes{};
        const auto encoded = encode_kernel_command(original, bytes);
        require(encoded.error == KernelCodecError::none,
                "valid command did not encode");

        Command restored;
        KernelCommandCodecScratch scratch;
        const auto decoded = decode_kernel_command(
            {bytes.data(), encoded.bytes_written}, restored, scratch);
        require(decoded.error == KernelCodecError::none,
                "canonical command did not decode at index " +
                    std::to_string(command_index));

        std::array<std::byte, kMaxKernelCommandBytes> round_trip{};
        const auto reencoded = encode_kernel_command(restored, round_trip);
        require(reencoded.error == KernelCodecError::none &&
                    reencoded.bytes_written == encoded.bytes_written &&
                    std::equal(bytes.begin(),
                               bytes.begin() + static_cast<std::ptrdiff_t>(
                                                   encoded.bytes_written),
                               round_trip.begin()),
                "command round trip changed canonical bytes");
        ++command_index;
    }
}

void test_command_identity_and_kind_are_explicit() {
    using namespace velocita::v2;
    const auto original = command(
        42, RecordSourceEvent{SourceId{10}, SourceGeneration{2}, SourceSequence{3},
                              digest(0x55)});
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(original, bytes);
    require(encoded.error == KernelCodecError::none,
            "source command fixture did not encode");

    const std::array expected_prefix{
        std::byte{'V'}, std::byte{'2'}, std::byte{'K'}, std::byte{'C'},
        std::byte{104}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{3}, std::byte{0},
        std::byte{8}, std::byte{0},
        std::byte{12}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{42}};
    require(std::equal(expected_prefix.begin(), expected_prefix.end(), bytes.begin()),
            "command framing, kind, or identity order changed");
    const auto frame_hash = to_hex(encoded.frame_digest);
    const auto full_hash =
        to_hex(sha256_digest({bytes.data(), encoded.bytes_written}));
    require(frame_hash ==
                "2357118cb0f0790c3b24c967b1256406ba789bfcf2e0ca7e6caacae666d1b4f6" &&
                full_hash ==
                    "050e042b897099999e111de5ff032f67010744c8a55137b74591bde2d22dd38f",
            "source-command golden corpus changed: frame=" + frame_hash +
                " full=" + full_hash);

    Command restored;
    KernelCommandCodecScratch scratch;
    const auto decoded = decode_kernel_command(
        {bytes.data(), encoded.bytes_written}, restored, scratch);
    require(decoded.error == KernelCodecError::none &&
                restored.identity == original.identity &&
                std::holds_alternative<RecordSourceEvent>(restored.body),
            "command identity or kind was not preserved");
}

void test_decision_kind_is_explicit_and_not_a_variant_ordinal() {
    using namespace velocita::v2;
    const auto original = command(
        43, EvaluateDecisionFrame{DecisionId{21}, EvaluationMode::authoritative,
                                  frame()});
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(original, bytes);
    require(encoded.error == KernelCodecError::none &&
                static_cast<std::uint16_t>(KernelCommandKind::evaluate_decision_frame) ==
                    9 &&
                bytes[kKernelCommandKindOffset] == std::byte{9} &&
                bytes[kKernelCommandKindOffset + 1] == std::byte{0},
            "decision command kind is not the reviewed explicit value 9");
}

void test_result_round_trip_is_canonical() {
    using namespace velocita::v2;
    const ApplyResult original{ApplyCode::source_sequence_gap,
                               ContractError::none,
                               PolicyId{17},
                               PolicyVersion{2},
                               PolicyLifecycle::active,
                               99,
                               StrategyMode::reduce_only,
                               KillEpoch{4},
                               ExecutorGeneration{8},
                               SourceId{10},
                               SourceGeneration{2},
                               SourceSequence{3},
                               DecisionId{21},
                               DecisionCode::control_suppressed,
                               Directive::place_limit,
                               Directive::no_action,
                               EvaluationMode::authoritative,
                               4,
                               5};
    std::array<std::byte, kKernelResultBytes> bytes{};
    const auto encoded = encode_kernel_result(original, bytes);
    require(encoded.error == KernelCodecError::none &&
                encoded.bytes_written == kKernelResultBytes,
            "valid result did not encode to its fixed size");

    ApplyResult restored;
    const auto decoded = decode_kernel_result(
        {bytes.data(), encoded.bytes_written}, restored);
    require(decoded.error == KernelCodecError::none && restored == original,
            "canonical result did not round trip exactly");
    const auto frame_hash = to_hex(encoded.frame_digest);
    const auto full_hash = to_hex(sha256_digest(bytes));
    require(frame_hash ==
                "bf4fa072530c18389d54d844dc571768cd11c6c617242f60543d6f4b8a54f2ec" &&
                full_hash ==
                    "23240d0e66b19b73b6801050b5e3c490c61168e6a9ac78dbd420a437f2cd5363",
            "apply-result golden corpus changed: frame=" + frame_hash +
                " full=" + full_hash);
}

void test_result_decode_is_transactional_and_fail_closed() {
    using namespace velocita::v2;
    const ApplyResult original{ApplyCode::applied,
                               ContractError::none,
                               {}, {}, PolicyLifecycle::none, 7,
                               StrategyMode::halted, {}, {}, {}, {}, {}};
    std::array<std::byte, kKernelResultBytes> bytes{};
    const auto encoded = encode_kernel_result(original, bytes);
    require(encoded.error == KernelCodecError::none, "result fixture did not encode");

    const ApplyResult sentinel{ApplyCode::policy_not_found,
                               ContractError::none,
                               PolicyId{99}, PolicyVersion{3},
                               PolicyLifecycle::none, 8,
                               StrategyMode::cancel_only, KillEpoch{2},
                               ExecutorGeneration{4}, {}, {}, {}};

    auto reject_without_publish = [&](const KernelCodecError expected,
                                       std::span<const std::byte> candidate,
                                       const std::string& context) {
        auto destination = sentinel;
        const auto result = decode_kernel_result(candidate, destination);
        require(result.error == expected && destination == sentinel,
                context + " was misclassified or partially published");
    };

    auto corrupt = bytes;
    corrupt[20] ^= std::byte{1};
    reject_without_publish(KernelCodecError::digest_mismatch, corrupt,
                           "result corruption");
    reject_without_publish(KernelCodecError::truncated,
                           std::span<const std::byte>{bytes}.first(bytes.size() - 1),
                           "result truncation");

    auto invalid_enum = bytes;
    invalid_enum[kKernelResultPayloadOffset] = std::byte{0xff};
    const auto digest_offset = invalid_enum.size() - kDigestBytes;
    const auto calculated = sha256_digest(std::span<const std::byte>{invalid_enum}.subspan(
        kKernelResultDigestInputOffset,
        digest_offset - kKernelResultDigestInputOffset));
    std::copy(calculated.bytes.begin(), calculated.bytes.end(),
              invalid_enum.begin() + static_cast<std::ptrdiff_t>(digest_offset));
    reject_without_publish(KernelCodecError::invalid_value, invalid_enum,
                           "unknown result enum");

    auto old_schema = bytes;
    write_u16(old_schema, kKernelResultSchemaOffset, 1);
    const auto old_digest_offset = old_schema.size() - kDigestBytes;
    const auto old_digest = sha256_digest(
        std::span<const std::byte>{old_schema}.subspan(
            kKernelResultDigestInputOffset,
            old_digest_offset - kKernelResultDigestInputOffset));
    std::copy(old_digest.bytes.begin(), old_digest.bytes.end(),
              old_schema.begin() +
                  static_cast<std::ptrdiff_t>(old_digest_offset));
    reject_without_publish(KernelCodecError::unsupported_schema, old_schema,
                           "old result schema");
}

void test_command_decode_is_transactional_and_fail_closed() {
    using namespace velocita::v2;
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(command(1, PublishPolicy{policy()}), bytes);
    require(encoded.error == KernelCodecError::none, "fixture did not encode");

    const auto sentinel = command(999, AdvanceKillEpoch{KillEpoch{7}});
    std::array<std::byte, kMaxKernelCommandBytes> sentinel_bytes{};
    const auto sentinel_encoded = encode_kernel_command(sentinel, sentinel_bytes);
    KernelCommandCodecScratch scratch;

    auto reject_without_publish = [&](const KernelCodecError expected,
                                       std::span<const std::byte> candidate,
                                       const std::string& context) {
        auto destination = sentinel;
        const auto result = decode_kernel_command(candidate, destination, scratch);
        require(result.error == expected, context + " returned wrong error");
        std::array<std::byte, kMaxKernelCommandBytes> after{};
        const auto after_encoded = encode_kernel_command(destination, after);
        require(after_encoded.error == KernelCodecError::none &&
                    after_encoded.bytes_written == sentinel_encoded.bytes_written &&
                    std::equal(sentinel_bytes.begin(),
                               sentinel_bytes.begin() + static_cast<std::ptrdiff_t>(
                                                            sentinel_encoded.bytes_written),
                               after.begin()),
                context + " partially published a command");
    };

    auto corrupt = bytes;
    corrupt[30] ^= std::byte{1};
    reject_without_publish(KernelCodecError::digest_mismatch,
                           {corrupt.data(), encoded.bytes_written}, "corruption");
    reject_without_publish(KernelCodecError::truncated,
                           {bytes.data(), encoded.bytes_written - 1}, "truncation");

    auto trailing = bytes;
    trailing[encoded.bytes_written] = std::byte{0};
    reject_without_publish(KernelCodecError::length_mismatch,
                           {trailing.data(), encoded.bytes_written + 1},
                           "trailing byte");

    auto unknown_schema = bytes;
    write_u16(unknown_schema, kKernelCommandSchemaOffset, 1);
    replace_command_digest({unknown_schema.data(), encoded.bytes_written});
    reject_without_publish(KernelCodecError::unsupported_schema,
                           {unknown_schema.data(), encoded.bytes_written},
                           "unknown schema");

    auto unknown_kind = bytes;
    write_u16(unknown_kind, kKernelCommandKindOffset, 99);
    replace_command_digest({unknown_kind.data(), encoded.bytes_written});
    reject_without_publish(KernelCodecError::unknown_kind,
                           {unknown_kind.data(), encoded.bytes_written},
                           "unknown command kind");
}

void test_encode_bounds_and_invalid_values() {
    using namespace velocita::v2;
    const auto valid = command(1, PublishPolicy{policy()});
    std::array<std::byte, 16> too_small{};
    require(encode_kernel_command(valid, too_small).error ==
                KernelCodecError::output_too_small,
            "undersized command output was accepted");

    auto invalid_identity = valid;
    invalid_identity.identity.caller_id = CallerId{};
    std::array<std::byte, kMaxKernelCommandBytes> command_bytes{};
    require(encode_kernel_command(invalid_identity, command_bytes).error ==
                KernelCodecError::invalid_value,
            "missing command identity was encoded");

    const ApplyResult invalid_result{static_cast<ApplyCode>(255)};
    std::array<std::byte, kKernelResultBytes> result_bytes{};
    require(encode_kernel_result(invalid_result, result_bytes).error ==
                KernelCodecError::invalid_value,
            "unknown result enum was encoded");
}

}  // namespace

int main() {
    try {
        test_every_command_alternative_round_trips_canonically();
        test_command_identity_and_kind_are_explicit();
        test_decision_kind_is_explicit_and_not_a_variant_ordinal();
        test_result_round_trip_is_canonical();
        test_result_decode_is_transactional_and_fail_closed();
        test_command_decode_is_transactional_and_fail_closed();
        test_encode_bounds_and_invalid_values();
        std::cout << "all V2 kernel codec tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 kernel codec test failed: " << error.what() << '\n';
        return 1;
    }
}
