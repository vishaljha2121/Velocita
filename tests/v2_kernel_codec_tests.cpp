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
#include <vector>

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

// Schema 9 changed only the ConfigureRiskLimits body and the admitted result
// enums. For every other frame, rewriting the schema byte back to 8 and
// resealing must reproduce the reviewed schema-8 golden exactly.
std::string schema8_full_hash(std::span<const std::byte> frame,
                              const std::size_t digest_input_offset) {
    using namespace velocita::v2;
    std::vector<std::byte> legacy(frame.begin(), frame.end());
    legacy[digest_input_offset] = std::byte{8};
    const auto digest_offset = legacy.size() - kDigestBytes;
    const auto calculated = sha256_digest(std::span<const std::byte>(legacy).subspan(
        digest_input_offset, digest_offset - digest_input_offset));
    std::copy(calculated.bytes.begin(), calculated.bytes.end(),
              legacy.begin() + static_cast<std::ptrdiff_t>(digest_offset));
    return to_hex(sha256_digest(legacy));
}

void test_execution_catalog_install_has_independent_canonical_bytes() {
    using namespace velocita::v2;
    Digest256 root;
    for (std::size_t index = 0; index < root.bytes.size(); ++index) {
        root.bytes[index] = static_cast<std::byte>(0x80U + index);
    }
    const auto original = command(
        0x0102030405060708ULL,
        InstallExecutionCatalog{root, KillEpoch{0x1112131415161718ULL},
                                ExecutorGeneration{0x2122232425262728ULL}});

    std::array<std::byte, 140> expected{};
    const std::array magic{std::byte{'V'}, std::byte{'2'}, std::byte{'K'},
                           std::byte{'C'}};
    std::copy(magic.begin(), magic.end(), expected.begin());
    auto put = [&](std::size_t offset, std::uint64_t value,
                   const std::size_t width) {
        for (std::size_t index = 0; index < width; ++index) {
            expected[offset + index] =
                static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
        }
    };
    put(4, 96, 4);  // identity 48 + install body 48
    put(8, kKernelCodecSchemaVersion, 2);
    put(10, 20, 2);
    put(12, 12, 8);
    put(20, 0x0102030405060708ULL, 8);
    expected[28] = std::byte{0x44};
    std::copy(root.bytes.begin(), root.bytes.end(), expected.begin() + 60);
    put(92, 0x1112131415161718ULL, 8);
    put(100, 0x2122232425262728ULL, 8);
    const auto expected_digest = sha256_digest(
        std::span<const std::byte>{expected}.subspan(8, 100));
    std::copy(expected_digest.bytes.begin(), expected_digest.bytes.end(),
              expected.begin() + 108);

    std::array<std::byte, kMaxKernelCommandBytes> actual{};
    const auto encoded = encode_kernel_command(original, actual);
    require(encoded.error == KernelCodecError::none &&
                encoded.bytes_written == expected.size() &&
                std::equal(expected.begin(), expected.end(), actual.begin()),
            "InstallExecutionCatalog bytes differ from independent kind-20 oracle");

    Command restored;
    KernelCommandCodecScratch scratch;
    require(decode_kernel_command({actual.data(), encoded.bytes_written}, restored,
                                  scratch).error == KernelCodecError::none &&
                std::holds_alternative<InstallExecutionCatalog>(restored.body) &&
                std::get<InstallExecutionCatalog>(restored.body).catalog_root == root,
            "InstallExecutionCatalog did not decode canonically");

    auto invalid = original;
    std::get<InstallExecutionCatalog>(invalid.body).catalog_root = {};
    require(encode_kernel_command(invalid, actual).error ==
                KernelCodecError::invalid_value,
            "zero execution-catalog root was encoded");
}

void test_catalog_apply_codes_are_appended_and_canonical() {
    using namespace velocita::v2;
    constexpr std::uint8_t first = 62;
    require(static_cast<std::uint8_t>(ApplyCode::reconciliation_required) == 61 &&
                static_cast<std::uint8_t>(ApplyCode::catalog_not_installed) == first &&
                static_cast<std::uint8_t>(ApplyCode::catalog_installation_blocked) ==
                    first + 1 &&
                static_cast<std::uint8_t>(ApplyCode::immutable_catalog_conflict) ==
                    first + 2 &&
                static_cast<std::uint8_t>(ApplyCode::catalog_row_not_found) ==
                    first + 3 &&
                static_cast<std::uint8_t>(ApplyCode::catalog_binding_mismatch) ==
                    first + 4,
            "catalog ApplyCode ordinals are not the approved append-only sequence");
    for (const auto code : {ApplyCode::catalog_not_installed,
                            ApplyCode::catalog_installation_blocked,
                            ApplyCode::immutable_catalog_conflict,
                            ApplyCode::catalog_row_not_found,
                            ApplyCode::catalog_binding_mismatch}) {
        ApplyResult result;
        result.code = code;
        std::array<std::byte, kKernelResultBytes> bytes{};
        ApplyResult restored;
        require(encode_kernel_result(result, bytes).error == KernelCodecError::none &&
                    bytes[kKernelResultPayloadOffset] ==
                        static_cast<std::byte>(static_cast<std::uint8_t>(code)) &&
                    decode_kernel_result(bytes, restored).error ==
                        KernelCodecError::none &&
                    restored.code == code,
                "catalog ApplyCode was not encoded and decoded canonically");
    }
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
        std::byte{9}, std::byte{0},
        std::byte{8}, std::byte{0},
        std::byte{12}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{42}};
    require(std::equal(expected_prefix.begin(), expected_prefix.end(), bytes.begin()),
            "command framing, kind, or identity order changed");
    const auto frame_hash = to_hex(encoded.frame_digest);
    const auto full_hash =
        to_hex(sha256_digest({bytes.data(), encoded.bytes_written}));
    require(schema8_full_hash({bytes.data(), encoded.bytes_written}, kKernelCommandSchemaOffset) ==
                "fa55af747a17df7a03daa48c43e3f982a05dfbd9b03f85e411728856810a9fcc",
            "V9 changed more than the schema byte of the source command");
    require(frame_hash ==
                "3458d2bef8d979a9d7d8ab20d61c4cab5033ae6b9a967ad0d6bd14d947c6b8da" &&
                full_hash ==
                    "a20adaad8354630d8024d97273607f186262564550e38ddc15048c791c78f4f6",
            "source-command golden corpus changed: frame=" + frame_hash +
                " full=" + full_hash);
    // Approved V5 migration changes only schema/digests for this old command.
    auto legacy = bytes;
    legacy[8] = std::byte{4};
    replace_command_digest({legacy.data(), encoded.bytes_written});
    require(to_hex(sha256_digest({legacy.data(), encoded.bytes_written})) ==
                "3d7c201012a0547d1ae27db4ade97703c3b63f2859281a452f2c40759a62a918",
            "V5 migration changed the legacy source-command payload");

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

void test_phase4_authorization_command_and_result_are_canonical_v4() {
    using namespace velocita::v2;
    require(kKernelCodecSchemaVersion == 9,
            "position limits did not move the codec to schema V9");

    const auto original = command(44, AuthorizeDecision{DecisionId{21}});
    std::array<std::byte, kMaxKernelCommandBytes> command_bytes{};
    const auto encoded = encode_kernel_command(original, command_bytes);
    require(encoded.error == KernelCodecError::none &&
                static_cast<std::uint16_t>(KernelCommandKind::authorize_decision) ==
                    12 &&
                command_bytes[kKernelCommandKindOffset] == std::byte{12} &&
                command_bytes[kKernelCommandKindOffset + 1] == std::byte{0},
            "AuthorizeDecision lacks explicit canonical command kind 12");
    Command restored;
    KernelCommandCodecScratch scratch;
    require(decode_kernel_command(
                {command_bytes.data(), encoded.bytes_written}, restored, scratch)
                    .error == KernelCodecError::none &&
                restored.identity == original.identity &&
                std::holds_alternative<AuthorizeDecision>(restored.body) &&
                std::get<AuthorizeDecision>(restored.body).decision_id ==
                    DecisionId{21},
            "AuthorizeDecision did not round trip canonically");

    ApplyResult authorization;
    authorization.code = ApplyCode::applied;
    authorization.state_version = 99;
    authorization.strategy_mode = StrategyMode::normal;
    authorization.kill_epoch = KillEpoch{4};
    authorization.executor_generation = ExecutorGeneration{8};
    authorization.decision_id = DecisionId{21};
    authorization.detail = ApplyResultDetail::authorization(
        ClusterOrderId{31}, EffectId{41}, MoneyMicros::from_atoms(2'206));
    authorization.detail_kind = ApplyResultDetailKind::authorization;

    std::array<std::byte, kKernelResultBytes> result_bytes{};
    const auto encoded_result = encode_kernel_result(authorization, result_bytes);
    ApplyResult restored_result;
    require(encoded_result.error == KernelCodecError::none &&
                encoded_result.bytes_written == 128 &&
                decode_kernel_result(result_bytes, restored_result).error ==
                    KernelCodecError::none &&
                restored_result == authorization,
            "authorization result detail did not round trip in fixed V4 bytes");
}

void test_result_round_trip_is_canonical() {
    using namespace velocita::v2;
    ApplyResult original;
    original.code = ApplyCode::source_sequence_gap;
    original.metadata_words = {17, 2};
    original.policy_lifecycle = PolicyLifecycle::active;
    original.state_version = 99;
    original.strategy_mode = StrategyMode::reduce_only;
    original.kill_epoch = KillEpoch{4};
    original.executor_generation = ExecutorGeneration{8};
    original.detail = ApplyResultDetail::source(
        SourceId{10}, SourceGeneration{2}, SourceSequence{3});
    original.detail_kind = ApplyResultDetailKind::source;
    original.decision_id = DecisionId{21};
    original.decision_code = DecisionCode::control_suppressed;
    original.proposed_directive = Directive::place_limit;
    original.effective_directive = Directive::no_action;
    original.evaluation_mode = EvaluationMode::authoritative;
    original.matched_rule_index = 4;
    original.rules_visited = 5;
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
    require(schema8_full_hash(bytes, kKernelResultSchemaOffset) ==
                "7e1cfff95c9f37025b1c3ccae5d5adb52399ed2dc7ca89427a9d41205d07c005",
            "V9 changed more than the schema byte of the apply result");
    require(frame_hash ==
                "b54c30fe22cc9cd1410d2968bb4b4ff0d1e27782ed5ccb051498b647b698cf0f" &&
                full_hash ==
                    "1f101f02441fee3ded2e94600e3d5261c2d91aa2ad4987d7035cd30e35b3da8c",
            "apply-result golden corpus changed: frame=" + frame_hash +
                " full=" + full_hash);
}

void test_fill_command_and_result_are_canonical_v4() {
    using namespace velocita::v2;
    require(static_cast<std::uint16_t>(KernelCommandKind::observe_fill) == 13,
            "ObserveFill command kind changed");
    VenueObservation observation;
    observation.kind = VenueObservationKind::fill;
    observation.venue_id = VenueId{9};
    observation.venue_session_id = VenueSessionId{2};
    observation.venue_sequence = VenueSequence{3};
    observation.cluster_order_id = ClusterOrderId{31};
    observation.effect_id = EffectId{41};
    observation.venue_order_id = VenueOrderId{51};
    observation.execution_id = ExecutionId{61};
    observation.side = Side::buy;
    observation.price = PriceTicks::from_atoms(11);
    observation.quantity = QuantityLots::from_atoms(1);
    observation.fee = FeeMicros::from_atoms(3);
    observation.evidence_digest = digest(0x66);
    const auto original = command(15, ObserveFill{observation});

    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(original, bytes);
    Command restored;
    KernelCommandCodecScratch scratch;
    const auto decoded = decode_kernel_command(
        {bytes.data(), encoded.bytes_written}, restored, scratch);
    require(encoded.error == KernelCodecError::none &&
                decoded.error == KernelCodecError::none &&
                restored.identity == original.identity &&
                std::holds_alternative<ObserveFill>(restored.body),
            "ObserveFill did not round trip canonically");
    const auto& actual = std::get<ObserveFill>(restored.body).observation;
    require(actual.kind == observation.kind &&
                actual.venue_id == observation.venue_id &&
                actual.venue_session_id == observation.venue_session_id &&
                actual.venue_sequence == observation.venue_sequence &&
                actual.cluster_order_id == observation.cluster_order_id &&
                actual.effect_id == observation.effect_id &&
                actual.venue_order_id == observation.venue_order_id &&
                actual.execution_id == observation.execution_id &&
                actual.side == observation.side && actual.price == observation.price &&
                actual.quantity == observation.quantity &&
                actual.fee == observation.fee &&
                actual.evidence_digest == observation.evidence_digest,
            "ObserveFill canonical body lost venue evidence");

    ApplyResult fill;
    fill.code = ApplyCode::applied;
    fill.state_version = 16;
    fill.strategy_mode = StrategyMode::normal;
    fill.detail = ApplyResultDetail::fill(
        VenueId{9}, ExecutionId{61}, QuantityLots::from_atoms(1));
    fill.detail_kind = ApplyResultDetailKind::fill;
    fill.metadata_words = {2, 60};
    fill.decision_id = DecisionId{21};
    std::array<std::byte, kKernelResultBytes> result_bytes{};
    ApplyResult restored_result;
    require(encode_kernel_result(fill, result_bytes).error ==
                    KernelCodecError::none &&
                decode_kernel_result(result_bytes, restored_result).error ==
                    KernelCodecError::none &&
                restored_result == fill,
            "fill result identity did not round trip in fixed V4 bytes");
    require(restored_result.fill_session_id() == VenueSessionId{2} &&
                restored_result.fill_sequence() == VenueSequence{60} &&
                restored_result.policy_id() == PolicyId{} &&
                restored_result.policy_version() == PolicyVersion{},
            "fill metadata was exposed as policy identity");
    fill.metadata_words = {};
    require(encode_kernel_result(fill, result_bytes).error != KernelCodecError::none,
            "successful fill result omitted stream identity");
}

void test_cancel_and_order_event_commands_are_canonical_v4() {
    using namespace velocita::v2;
    require(static_cast<std::uint16_t>(KernelCommandKind::request_cancel) == 14 &&
                static_cast<std::uint16_t>(
                    KernelCommandKind::observe_order_event) == 15,
            "cancel/order-event command kinds changed");

    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto cancel = command(16, RequestCancel{ClusterOrderId{31}});
    const auto encoded_cancel = encode_kernel_command(cancel, bytes);
    Command restored;
    KernelCommandCodecScratch scratch;
    require(encoded_cancel.error == KernelCodecError::none &&
                decode_kernel_command(
                    {bytes.data(), encoded_cancel.bytes_written}, restored, scratch)
                        .error == KernelCodecError::none &&
                std::holds_alternative<RequestCancel>(restored.body) &&
                std::get<RequestCancel>(restored.body).cluster_order_id ==
                    ClusterOrderId{31},
            "RequestCancel did not round trip canonically");

    VenueObservation observation;
    observation.kind = VenueObservationKind::effect_unknown;
    observation.venue_id = VenueId{9};
    observation.venue_session_id = VenueSessionId{2};
    observation.venue_sequence = VenueSequence{71};
    observation.cluster_order_id = ClusterOrderId{31};
    observation.effect_id = EffectId{41};
    observation.evidence_digest = digest(0x71);
    const auto event = command(17, ObserveOrderEvent{observation});
    const auto encoded_event = encode_kernel_command(event, bytes);
    require(encoded_event.error == KernelCodecError::none &&
                decode_kernel_command(
                    {bytes.data(), encoded_event.bytes_written}, restored, scratch)
                        .error == KernelCodecError::none &&
                std::holds_alternative<ObserveOrderEvent>(restored.body),
            "ObserveOrderEvent did not round trip canonically");
    const auto& actual =
        std::get<ObserveOrderEvent>(restored.body).observation;
    require(actual.kind == observation.kind &&
                actual.venue_id == observation.venue_id &&
                actual.venue_session_id == observation.venue_session_id &&
                actual.venue_sequence == observation.venue_sequence &&
                actual.cluster_order_id == observation.cluster_order_id &&
                actual.effect_id == observation.effect_id &&
                actual.evidence_digest == observation.evidence_digest,
            "order-event canonical body lost evidence identity");

    ApplyResult result;
    result.code = ApplyCode::applied;
    result.state_version = 17;
    result.strategy_mode = StrategyMode::normal;
    result.detail = ApplyResultDetail::venue_event(
        VenueId{9}, VenueSessionId{2}, VenueSequence{71});
    result.detail_kind = ApplyResultDetailKind::venue_event;
    result.metadata_words[0] = EffectId{41}.value();
    result.decision_id = DecisionId{21};
    std::array<std::byte, kKernelResultBytes> result_bytes{};
    ApplyResult restored_result;
    require(encode_kernel_result(result, result_bytes).error ==
                    KernelCodecError::none &&
                decode_kernel_result(result_bytes, restored_result).error ==
                    KernelCodecError::none &&
                restored_result == result &&
                restored_result.event_effect_id() == EffectId{41} &&
                restored_result.policy_id() == PolicyId{},
            "venue-event result identity did not round trip in V4 bytes");
}

void test_gateway_result_acknowledgement_is_canonical_v6() {
    using namespace velocita::v2;
    require(static_cast<std::uint16_t>(
                KernelCommandKind::acknowledge_gateway_results) == 18,
            "gateway result acknowledgement command kind changed");
    const auto original =
        command(90, AcknowledgeGatewayResults{RequestSequence{89}});
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(original, bytes);
    Command restored;
    KernelCommandCodecScratch scratch;
    require(encoded.error == KernelCodecError::none &&
                decode_kernel_command(
                    {bytes.data(), encoded.bytes_written}, restored, scratch)
                        .error == KernelCodecError::none &&
                std::holds_alternative<AcknowledgeGatewayResults>(restored.body) &&
                std::get<AcknowledgeGatewayResults>(restored.body)
                        .acknowledged_through == RequestSequence{89},
            "gateway result acknowledgement did not round trip canonically");

    const auto invalid =
        command(90, AcknowledgeGatewayResults{RequestSequence{90}});
    require(encode_kernel_command(invalid, bytes).error ==
                KernelCodecError::invalid_value,
            "a gateway acknowledged its own uncommitted command result");
}

void test_replacement_command_is_canonical_v4() {
    using namespace velocita::v2;
    require(static_cast<std::uint16_t>(KernelCommandKind::request_replace) == 16,
            "RequestReplace command kind changed");
    const auto original = command(
        18, RequestReplace{ClusterOrderId{31}, DecisionId{21}});
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(original, bytes);
    Command restored;
    KernelCommandCodecScratch scratch;
    const auto decoded = decode_kernel_command(
        {bytes.data(), encoded.bytes_written}, restored, scratch);
    require(encoded.error == KernelCodecError::none &&
                decoded.error == KernelCodecError::none &&
                restored.identity == original.identity &&
                std::holds_alternative<RequestReplace>(restored.body) &&
                std::get<RequestReplace>(restored.body).cluster_order_id ==
                    ClusterOrderId{31} &&
                std::get<RequestReplace>(restored.body).replacement_decision_id ==
                    DecisionId{21},
            "RequestReplace did not round trip canonically");
}

void test_result_decode_is_transactional_and_fail_closed() {
    using namespace velocita::v2;
    ApplyResult original;
    original.code = ApplyCode::applied;
    original.state_version = 7;
    original.strategy_mode = StrategyMode::halted;
    std::array<std::byte, kKernelResultBytes> bytes{};
    const auto encoded = encode_kernel_result(original, bytes);
    require(encoded.error == KernelCodecError::none, "result fixture did not encode");

    ApplyResult sentinel;
    sentinel.code = ApplyCode::policy_not_found;
    sentinel.metadata_words = {99, 3};
    sentinel.state_version = 8;
    sentinel.strategy_mode = StrategyMode::cancel_only;
    sentinel.kill_epoch = KillEpoch{2};
    sentinel.executor_generation = ExecutorGeneration{4};

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
        test_execution_catalog_install_has_independent_canonical_bytes();
        test_catalog_apply_codes_are_appended_and_canonical();
        test_every_command_alternative_round_trips_canonically();
        test_command_identity_and_kind_are_explicit();
        test_decision_kind_is_explicit_and_not_a_variant_ordinal();
        test_phase4_authorization_command_and_result_are_canonical_v4();
        test_fill_command_and_result_are_canonical_v4();
        test_cancel_and_order_event_commands_are_canonical_v4();
        test_replacement_command_is_canonical_v4();
        test_gateway_result_acknowledgement_is_canonical_v6();
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
