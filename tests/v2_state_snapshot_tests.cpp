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

velocita::v2::DecisionFrame frame() {
    using namespace velocita::v2;
    DecisionFrame value;
    value.policy_id = PolicyId{17};
    value.policy_version = PolicyVersion{2};
    value.feature_schema_id = FeatureSchemaId{3};
    value.venue_id = VenueId{7};
    value.venue_session_id = VenueSessionId{8};
    value.venue_sequence = VenueSequence{9};
    value.source_id = SourceId{10};
    value.source_generation = SourceGeneration{2};
    value.source_sequence = SourceSequence{3};
    value.event_time_ns = 150;
    value.receive_time_ns = 151;
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.feature_count = 1;
    value.features[0] = FeatureMicros::from_atoms(42);
    value.market_data_status = MarketDataStatus::ready;
    value.market_data_age_ns = 1;
    value.position_version = PositionVersion{11};
    value.risk_version = RiskVersion{12};
    value.raw_evidence_digest = digest(0x33);
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
    ApplyResult retained_result;
    retained_result.code = ApplyCode::applied;
    retained_result.metadata_words = {17, 2};
    retained_result.policy_lifecycle = PolicyLifecycle::active;
    retained_result.state_version = 9;
    retained_result.strategy_mode = StrategyMode::reduce_only;
    retained_result.kill_epoch = KillEpoch{4};
    retained_result.executor_generation = ExecutorGeneration{8};
    retained_result.detail = ApplyResultDetail::source(
        SourceId{10}, SourceGeneration{2}, SourceSequence{3});
    retained_result.detail_kind = ApplyResultDetailKind::source;
    state.receipts[0] = RequestReceipt{
        CommandIdentity{CallerId{12}, RequestSequence{13}, digest(0x44)},
        retained_result};

    state.decision_count = 1;
    state.decision_hot[0] = DecisionRecordHot{
        DecisionId{21}, PolicyId{17}, PolicyVersion{2}, 9, KillEpoch{4},
        ExecutorGeneration{8}, 0, 1, DecisionCode::control_suppressed,
        ContractError::none, Directive::place_limit, Directive::no_action,
        EvaluationMode::authoritative, StrategyMode::reduce_only};
    state.decision_cold[0] =
        DecisionRecordCold{DecisionId{21}, digest(0x44), frame()};
    return state;
}

velocita::v2::TradingState phase4_state() {
    using namespace velocita::v2;
    auto state = populated_state();
    state.state_version = 10;
    state.strategy_mode = StrategyMode::normal;
    state.decision_hot[0].code = DecisionCode::action;
    state.decision_hot[0].proposed_directive = Directive::place_limit;
    state.decision_hot[0].effective_directive = Directive::place_limit;
    state.decision_hot[0].strategy_mode = StrategyMode::normal;
    state.decision_hot[0].action_resolution_code = ActionResolutionCode::proposed;
    state.decision_cold[0].proposal = {
        kEconomicActionProposalSchemaVersion,
        DecisionId{21}, PolicyId{17}, PolicyVersion{2}, RiskPolicyVersion{6},
        InstrumentSpecVersion{1}, VenueCapabilitiesVersion{1}, AccountId{5},
        InstrumentId{4}, VenueId{7}, LogicalOrderSlot{1},
        Directive::place_limit, Side::buy, TimeInForce::good_til_cancel,
        PostOnlyMode::required, PriceTicks::from_atoms(42),
        QuantityLots::from_atoms(2), MoneyMicros::from_atoms(8'400)};

    state.risk_account_count = 1;
    state.risk_accounts[0] = {
        AccountId{5}, InstrumentId{4}, RiskPolicyVersion{6}, RiskVersion{12},
        PositionVersion{11}, QuantityLots::from_atoms(4),
        QuantityLots::from_atoms(8), QuantityLots::from_atoms(2),
        MoneyMicros::from_atoms(10'000), MoneyMicros::from_atoms(20'000),
        MoneyMicros::from_atoms(8'400), MoneyMicros::from_atoms(21),
        1'000'000, 2'500};
    state.reservation_count = 1;
    state.reservations[0] = {
        ClusterOrderId{31}, QuantityLots::from_atoms(2), {},
        QuantityLots::from_atoms(2), MoneyMicros::from_atoms(8'400),
        MoneyMicros::from_atoms(8'400), MoneyMicros::from_atoms(21),
        ReservationState::held};
    state.order_count = 1;
    state.orders[0] = {ClusterOrderId{31}, EffectId{41}, DecisionId{21}, 10, 0,
                       ClusterOrderState::dispatch_authorized};
    state.outbox_effect_count = 1;
    state.outbox_effects[0] = {EffectId{41}, ClusterOrderId{31}, DecisionId{21},
                               digest(0x77), 1, OutboxState::pending};

    ApplyResult authorization;
    authorization.code = ApplyCode::applied;
    authorization.state_version = 10;
    authorization.strategy_mode = StrategyMode::normal;
    authorization.kill_epoch = state.kill_epoch;
    authorization.executor_generation = state.executor_generation;
    authorization.decision_id = DecisionId{21};
    authorization.detail = ApplyResultDetail::authorization(
        ClusterOrderId{31}, EffectId{41}, MoneyMicros::from_atoms(8'421));
    authorization.detail_kind = ApplyResultDetailKind::authorization;
    state.receipts[state.receipt_count++] = {
        {CallerId{12}, RequestSequence{14}, digest(0x55)}, authorization};
    return state;
}

velocita::v2::ExecutablePolicyBundle maximum_bundle(
    const velocita::v2::PolicyArtifact& policy_value,
    const bool active) {
    using namespace velocita::v2;
    FeatureSchemaArtifact schema;
    schema.schema_version = kFeatureSchemaArtifactSchemaVersion;
    schema.feature_schema_id = policy_value.feature_schema_id;
    schema.feature_count = kMaxDecisionFeatures;
    for (std::size_t index = 0; index < schema.feature_count; ++index) {
        schema.features[index] = {FeatureEconomicKind::price, 1};
    }
    schema.artifact_digest = compute_artifact_digest(schema).digest;

    ExecutablePolicyArtifact executable;
    executable.schema_version = kExecutablePolicyArtifactSchemaVersion;
    executable.policy_id = policy_value.policy_id;
    executable.policy_version = policy_value.policy_version;
    executable.policy_artifact_digest = policy_value.artifact_digest;
    executable.feature_schema_id = schema.feature_schema_id;
    executable.feature_schema_digest = schema.artifact_digest;
    executable.template_count = kMaxExecutableActionTemplates;
    for (std::size_t index = 0; index < executable.template_count; ++index) {
        auto& action = executable.templates[index];
        action.schema_version = kActionTemplateSchemaVersion;
        action.directive = Directive::place_limit;
        action.side = Side::buy;
        action.time_in_force = TimeInForce::good_til_cancel;
        action.post_only = PostOnlyMode::required;
        action.order_slot = LogicalOrderSlot{
            static_cast<std::uint16_t>(index + 1)};
        action.instrument_spec_version = InstrumentSpecVersion{1};
        action.venue_capabilities_version = VenueCapabilitiesVersion{1};
        action.price_source = {EconomicValueSourceKind::feature, 0,
                               RoundingMode::toward_zero, {}, 1, {}};
        action.quantity_source = {EconomicValueSourceKind::fixed, 0,
                                  RoundingMode::toward_zero,
                                  QuantityLots::from_atoms(1), 0};
        action.safety_caps = {QuantityLots::from_atoms(1),
                              MoneyMicros::from_atoms(1'000),
                              PriceTicks::from_atoms(1),
                              PriceTicks::from_atoms(1), 100,
                              action.price_source};
    }
    executable.rule_binding_count = kMaxPolicyRules;
    for (std::size_t index = 0; index < executable.rule_binding_count; ++index) {
        executable.rule_template_slots[index] = ActionTemplateSlot{
            static_cast<std::uint8_t>(index % executable.template_count + 1)};
    }
    executable.artifact_digest = compute_artifact_digest(executable).digest;

    const InstrumentExecutionProfile instrument{
        kInstrumentExecutionProfileSchemaVersion, VenueId{7},
        policy_value.instrument_id, InstrumentSpecVersion{1},
        MoneyMicros::from_atoms(1), PriceTicks::from_atoms(1),
        PriceTicks::from_atoms(100), QuantityLots::from_atoms(1),
        QuantityLots::from_atoms(10)};
    const VenueCapabilities venue{
        kVenueCapabilitiesSchemaVersion, VenueId{7}, VenueCapabilitiesVersion{1},
        kVenueDirectivePlaceLimit, kVenueTifGtc, 1};
    return {true, schema, executable, instrument, venue, 1, active ? 2U : 0U};
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

void test_snapshot_rejects_unbacked_risk_aggregates() {
    using namespace velocita::v2;
    const auto original = phase4_state();
    std::array<std::byte, kMaxStateSnapshotBytes> storage{};
    const auto encoded = encode_state_snapshot(original, storage);
    require(encoded.error == StateSnapshotError::none, "risk fixture did not encode");
    // These fixed-width arenas are the snapshot suffix. Locate risk fields from
    // their canonical layout, not C++ object offsets or a byte-pattern search.
    const auto risk_offset = encoded.bytes_written - kDigestBytes -
        (2 + original.outbox_effect_count * kStateSnapshotOutboxEffectBytes) -
        (2 + original.order_count * kStateSnapshotClusterOrderBytes) -
        (2 + original.reservation_count * kStateSnapshotRiskReservationBytes) -
        original.risk_account_count * kStateSnapshotRiskAccountBytes;
    for (const auto offset : {56U, 80U, 88U}) {
        auto corrupt = storage;
        std::fill_n(corrupt.begin() + static_cast<std::ptrdiff_t>(risk_offset + offset),
                    8, std::byte{});
        replace_integrity_digest({corrupt.data(), encoded.bytes_written});
        auto destination = populated_state();
        std::array<std::byte, kMaxStateSnapshotBytes> before{}, after{};
        const auto before_encoded = encode_state_snapshot(destination, before);
        StateSnapshotScratch scratch;
        require(decode_state_snapshot({corrupt.data(), encoded.bytes_written},
                    destination, scratch).error == StateSnapshotError::invalid_state,
                "snapshot accepted an aggregate hold inconsistent with live reservations");
        const auto after_encoded = encode_state_snapshot(destination, after);
        require(before_encoded.error == StateSnapshotError::none &&
                    after_encoded.error == StateSnapshotError::none && before == after,
                "rejected risk snapshot changed destination state");
        auto invalid = original;
        if (offset == 56) invalid.risk_accounts[0].reserved_quantity = {};
        if (offset == 80) invalid.risk_accounts[0].reserved_margin = {};
        if (offset == 88) invalid.risk_accounts[0].reserved_fee = {};
        require(encode_state_snapshot(invalid, after).error == StateSnapshotError::invalid_state,
                "snapshot encoder accepted an unbacked aggregate hold");
    }
}

void test_snapshot_reservation_ownership_and_sum_overflow() {
    using namespace velocita::v2;
    auto state = phase4_state();
    // Two orders and two risk accounts. Keeping the global total correct must
    // not hide assigning a hold to the wrong account.
    state.risk_account_count = 2;
    state.risk_accounts[1] = state.risk_accounts[0];
    state.risk_accounts[1].account_id = AccountId{6};
    state.decision_count = 2;
    state.decision_hot[1] = state.decision_hot[0];
    state.decision_hot[1].decision_id = DecisionId{22};
    state.decision_cold[1] = state.decision_cold[0];
    state.decision_cold[1].decision_id = DecisionId{22};
    state.decision_cold[1].frame.account_id = AccountId{6};
    state.decision_cold[1].proposal.decision_id = DecisionId{22};
    state.decision_cold[1].proposal.account_id = AccountId{6};
    state.reservation_count = state.order_count = state.outbox_effect_count = 2;
    state.reservations[1] = state.reservations[0];
    state.reservations[1].cluster_order_id = ClusterOrderId{32};
    state.orders[1] = {ClusterOrderId{32}, EffectId{42}, DecisionId{22}, 10, 1,
                       ClusterOrderState::dispatch_authorized};
    state.outbox_effects[1] = {EffectId{42}, ClusterOrderId{32}, DecisionId{22},
                              digest(0x78), 1, OutboxState::pending};
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    require(encode_state_snapshot(state, bytes).error == StateSnapshotError::none,
            "valid multi-account holds failed validation");
    auto misplaced = state;
    misplaced.risk_accounts[0].reserved_quantity = QuantityLots::from_atoms(1);
    misplaced.risk_accounts[1].reserved_quantity = QuantityLots::from_atoms(3);
    require(encode_state_snapshot(misplaced, bytes).error == StateSnapshotError::invalid_state,
            "global totals hid incorrect per-account ownership");
    auto missing = state;
    missing.risk_accounts[1].account_id = AccountId{7};
    require(encode_state_snapshot(missing, bytes).error == StateSnapshotError::invalid_state,
            "snapshot accepted a reservation with no risk account");
    auto orphan = state;
    orphan.reservations[orphan.reservation_count++] = {
        ClusterOrderId{99}, QuantityLots::from_atoms(1), {}, QuantityLots::from_atoms(1),
        MoneyMicros::from_atoms(1), MoneyMicros::from_atoms(1), {}, ReservationState::held};
    require(encode_state_snapshot(orphan, bytes).error == StateSnapshotError::invalid_state,
            "snapshot accepted an orphan reservation");
    // Put both orders under account zero, then overflow each checked accumulator.
    state.decision_cold[1].frame.account_id = AccountId{5};
    state.decision_cold[1].proposal.account_id = AccountId{5};
    state.risk_accounts[0].reserved_quantity = QuantityLots::from_atoms(4);
    state.risk_accounts[0].reserved_margin = MoneyMicros::from_atoms(16'800);
    state.risk_accounts[0].reserved_fee = MoneyMicros::from_atoms(42);
    state.risk_accounts[1].reserved_quantity = {};
    state.risk_accounts[1].reserved_margin = {};
    state.risk_accounts[1].reserved_fee = {};
    require(encode_state_snapshot(state, bytes).error == StateSnapshotError::none,
            "valid two-order aggregate failed validation");
    for (int field = 0; field < 3; ++field) {
        auto overflow = state;
        constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
        if (field == 0) {
            overflow.reservations[0].original_quantity = QuantityLots::from_atoms(maximum);
            overflow.reservations[0].remaining_quantity = QuantityLots::from_atoms(maximum);
        } else if (field == 1) {
            overflow.reservations[0].margin_hold = MoneyMicros::from_atoms(maximum);
        } else {
            overflow.reservations[0].fee_hold = MoneyMicros::from_atoms(maximum);
        }
        require(encode_state_snapshot(overflow, bytes).error == StateSnapshotError::invalid_state,
                "snapshot accepted overflowing reservation totals");
    }
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
        std::byte{63}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{5}, std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{5}};
    require(std::equal(expected_prefix.begin(), expected_prefix.end(), storage.begin()),
            "empty state prefix changed");

    const auto digest_offset = encoded.bytes_written - kDigestBytes;
    require(std::equal(encoded.state_digest.bytes.begin(), encoded.state_digest.bytes.end(),
                       storage.begin() + static_cast<std::ptrdiff_t>(digest_offset)),
            "returned digest differs from framed digest");
    const auto hash = to_hex(encoded.state_digest);
    require(hash ==
                "a7c2a852eb7358b7472c9fdd78896be40898999396f8bfacd77eb94f2492816a",
            "empty state golden digest changed: " + hash);
    // V5 empty-payload size guard.
    //
    // This previously asserted V5's empty payload was byte-identical to V4's
    // apart from the schema byte, i.e. that B1 added no header field. That is
    // now deliberately false: orphan-settlement accounting appends
    // `settled_incomplete_effect_count`, so the empty payload grew by eight
    // bytes. Hashing the payload again would only restate the golden digest
    // above, so the guard keeps its real job in a form that cannot duplicate it -
    // pinning the empty-payload SIZE, which is what unreviewed header growth
    // actually moves.
    require(encoded.bytes_written ==
                kStateSnapshotPayloadOffset + kStateSnapshotEmptyPayloadBytes +
                    kDigestBytes,
            "empty snapshot length does not match the declared empty payload");
    require(kStateSnapshotEmptyPayloadBytes == 63,
            "V5 empty snapshot header grew without review");
}

void test_round_trip_is_canonical_and_excludes_unused_capacity() {
    using namespace velocita::v2;
    auto original = populated_state();
    auto same_logical_state = original;
    same_logical_state.policies[7].artifact.policy_id = PolicyId{999};
    same_logical_state.sources[9].source_id = SourceId{999};
    same_logical_state.source_events[31].event_digest = digest(0xee);
    same_logical_state.receipts[42].identity.payload_digest = digest(0xdd);
    same_logical_state.decision_hot[7].decision_id = DecisionId{999};
    same_logical_state.decision_cold[7].command_payload_digest = digest(0xcc);

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

void test_phase4_state_survives_canonical_snapshot() {
    using namespace velocita::v2;
    require(kStateSnapshotSchemaVersion == 5,
            "risk configuration receipts did not move snapshots to V5");
    const auto original = phase4_state();
    std::array<std::byte, kMaxStateSnapshotBytes> storage{};
    const auto encoded = encode_state_snapshot(original, storage);
    require(encoded.error == StateSnapshotError::none,
            "valid Phase 4 state did not encode");

    TradingState restored;
    StateSnapshotScratch scratch;
    const auto decoded = decode_state_snapshot(
        {storage.data(), encoded.bytes_written}, restored, scratch);
    require(decoded.error == StateSnapshotError::none &&
                restored.risk_account_count == 1 &&
                restored.risk_accounts[0] == original.risk_accounts[0] &&
                restored.reservation_count == 1 &&
                restored.reservations[0] == original.reservations[0] &&
                restored.order_count == 1 &&
                restored.orders[0] == original.orders[0] &&
                restored.outbox_effect_count == 1 &&
                restored.outbox_effects[0] == original.outbox_effects[0] &&
                restored.receipts[1].identity == original.receipts[1].identity &&
                restored.receipts[1].result == original.receipts[1].result,
            "snapshot lost Phase 4 economics, identity, or retained result");

    std::array<std::byte, kMaxStateSnapshotBytes> replayed{};
    const auto reencoded = encode_state_snapshot(restored, replayed);
    require(reencoded.error == StateSnapshotError::none &&
                reencoded.state_digest == encoded.state_digest &&
                reencoded.bytes_written == encoded.bytes_written &&
                std::equal(storage.begin(),
                           storage.begin() +
                               static_cast<std::ptrdiff_t>(encoded.bytes_written),
                           replayed.begin()),
            "restored Phase 4 state did not reproduce byte-identical snapshot");
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
    write_u16(unknown, kStateSnapshotSchemaOffset, 1);
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

    invalid = state;
    invalid.caller_cursor_count = kMaxCallerCursors + 1;
    require(encode_state_snapshot(invalid, storage).error ==
                StateSnapshotError::invalid_state,
            "caller cursor count escaped its fixed-capacity bound");

    invalid = state;
    invalid.caller_cursor_count = 1;
    invalid.caller_cursors[0] = {CallerId{}, RequestSequence{}};
    require(encode_state_snapshot(invalid, storage).error ==
                StateSnapshotError::invalid_state,
            "snapshot accepted a caller cursor with no stable identity");

    invalid = state;
    invalid.caller_cursor_count = 2;
    invalid.caller_cursors[0] = {CallerId{91}, RequestSequence{}};
    invalid.caller_cursors[1] = {CallerId{91}, RequestSequence{7}};
    require(encode_state_snapshot(invalid, storage).error ==
                StateSnapshotError::invalid_state,
            "snapshot accepted duplicate caller cursor identities");

    auto first_admission = state;
    first_admission.caller_cursor_count = 1;
    first_admission.caller_cursors[0] = {CallerId{91}, RequestSequence{}};
    require(encode_state_snapshot(first_admission, storage).error ==
                StateSnapshotError::none,
            "a registered caller with no expired prefix was not canonical");
}

void test_declared_maximum_is_exact_and_encodable() {
    using namespace velocita::v2;
    TradingState state;
    state.state_version = kMaxRequestReceipts;

    state.policy_count = kMaxPolicies;
    for (std::size_t index = 0; index < state.policy_count; ++index) {
        auto artifact = policy();
        artifact.policy_id = PolicyId{index + 1};
        artifact.account_id = AccountId{index < 2 ? 1 : index + 1};
        artifact.instrument_id = InstrumentId{index < 2 ? 1 : index + 1};
        artifact.rule_count = kMaxPolicyRules;
        for (std::size_t rule = 0; rule < artifact.rule_count; ++rule) {
            artifact.rules[rule] =
                PolicyRule{static_cast<std::uint16_t>(rule),
                           PredicateOperator::equal,
                           FeatureMicros::from_atoms(static_cast<std::int64_t>(rule)),
                           Directive::place_limit};
        }
        state.policies[index] = PolicyRecord{
            artifact, index == 0 ? PolicyLifecycle::active
                                 : (index == 1 ? PolicyLifecycle::validated
                                               : PolicyLifecycle::proposed)};
    }
    state.active_executable_policy = maximum_bundle(state.policies[0].artifact, true);
    state.staged_executable_policy = maximum_bundle(state.policies[1].artifact, false);

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
        ApplyResult retained;
        retained.code = ApplyCode::applied;
        retained.state_version = index + 1;
        retained.strategy_mode = StrategyMode::halted;
        state.receipts[index] = RequestReceipt{
            CommandIdentity{CallerId{1}, RequestSequence{index + 1}, digest(0xaa)},
            retained};
    }

    state.decision_count = kMaxDecisionRecords;
    for (std::size_t index = 0; index < state.decision_count; ++index) {
        auto decision_frame = frame();
        decision_frame.feature_count = kMaxDecisionFeatures;
        for (std::size_t feature = 0; feature < kMaxDecisionFeatures; ++feature) {
            decision_frame.features[feature] =
                FeatureMicros::from_atoms(static_cast<std::int64_t>(feature));
        }
        const auto id = DecisionId{index + 1};
        state.decision_hot[index] = DecisionRecordHot{
            id, PolicyId{17}, PolicyVersion{2}, index + 1, {}, {}, 0, 1,
            DecisionCode::control_suppressed, ContractError::none,
            Directive::place_limit, Directive::no_action,
            EvaluationMode::authoritative,
            StrategyMode::halted, ActionResolutionCode::proposed};
        state.decision_cold[index] =
            DecisionRecordCold{id, digest(0xbb), decision_frame};
        state.decision_cold[index].proposal = {
            kEconomicActionProposalSchemaVersion,
            id,
            decision_frame.policy_id,
            decision_frame.policy_version,
            RiskPolicyVersion{6},
            InstrumentSpecVersion{1},
            VenueCapabilitiesVersion{1},
            decision_frame.account_id,
            decision_frame.instrument_id,
            decision_frame.venue_id,
            LogicalOrderSlot{static_cast<std::uint16_t>(index + 1)},
            Directive::place_limit,
            Side::buy,
            TimeInForce::good_til_cancel,
            PostOnlyMode::required,
            PriceTicks::from_atoms(10),
            QuantityLots::from_atoms(1),
            MoneyMicros::from_atoms(100)};
    }

    state.risk_account_count = kMaxRiskAccounts;
    for (std::size_t index = 0; index < state.risk_account_count; ++index) {
        state.risk_accounts[index] = {
            AccountId{index + 5}, InstrumentId{index + 4}, RiskPolicyVersion{6},
            RiskVersion{12}, PositionVersion{11}, QuantityLots::from_atoms(10),
            QuantityLots::from_atoms(100),
            QuantityLots::from_atoms(index == 0 ? kMaxClusterOrders : 0),
            MoneyMicros::from_atoms(1'000), MoneyMicros::from_atoms(10'000),
            MoneyMicros::from_atoms(index == 0 ? 10 * kMaxClusterOrders : 0),
            MoneyMicros::from_atoms(index == 0 ? kMaxClusterOrders : 0),
            100'000, 10'000};
    }

    state.reservation_count = kMaxRiskReservations;
    state.order_count = kMaxClusterOrders;
    state.outbox_effect_count = kMaxOutboxEffects;
    for (std::size_t index = 0; index < kMaxClusterOrders; ++index) {
        const auto order_id = ClusterOrderId{index + 101};
        const auto effect_id = EffectId{index + 201};
        const auto decision_id = DecisionId{index + 1};
        state.reservations[index] = {
            order_id, QuantityLots::from_atoms(1), {},
            QuantityLots::from_atoms(1), MoneyMicros::from_atoms(100),
            MoneyMicros::from_atoms(10), MoneyMicros::from_atoms(1),
            ReservationState::held};
        state.orders[index] = {
            order_id, effect_id, decision_id, index + 1,
            static_cast<std::uint16_t>(index),
            ClusterOrderState::dispatch_authorized};
        state.outbox_effects[index] = {
            effect_id, order_id, decision_id,
            digest(static_cast<std::uint8_t>(index + 0x40)), 1,
            OutboxState::pending};
    }

    // Caller cursors are part of the maximum too: the declared bound is only
    // honest if the largest VALID state fills every arena, including this one.
    state.caller_cursor_count = kMaxCallerCursors;
    for (std::size_t index = 0; index < kMaxCallerCursors; ++index) {
        state.caller_cursors[index] = {CallerId{500 + index},
                                       RequestSequence{index + 1}};
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
        test_snapshot_rejects_unbacked_risk_aggregates();
        test_snapshot_reservation_ownership_and_sum_overflow();
        test_empty_snapshot_has_canonical_layout();
        test_round_trip_is_canonical_and_excludes_unused_capacity();
        test_phase4_state_survives_canonical_snapshot();
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
