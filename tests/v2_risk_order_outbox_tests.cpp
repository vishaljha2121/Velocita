#include "velocita/v2/replay.hpp"
#include "velocita/v2/state_snapshot.hpp"
#include "velocita/v2/payload.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
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
    value.schema_version = kPolicyArtifactSchemaVersion;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.feature_schema_id = FeatureSchemaId{3};
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.model_digest = digest(0x11);
    value.artifact_digest = digest(0x12);
    value.valid_from_event_time_ns = 100;
    value.valid_until_event_time_ns = 200;
    value.risk_policy_version = RiskPolicyVersion{6};
    value.rule_count = 1;
    value.rules[0] = {0, PredicateOperator::greater_than,
                      FeatureMicros::from_atoms(10), Directive::place_limit};
    return value;
}

velocita::v2::FeatureSchemaArtifact feature_schema() {
    using namespace velocita::v2;
    FeatureSchemaArtifact value;
    value.schema_version = kFeatureSchemaArtifactSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{3};
    value.feature_count = 1;
    value.features[0] = {FeatureEconomicKind::price, 1};
    value.artifact_digest = compute_artifact_digest(value).digest;
    return value;
}

velocita::v2::ActionTemplate action_template() {
    using namespace velocita::v2;
    ActionTemplate value;
    value.schema_version = kActionTemplateSchemaVersion;
    value.directive = Directive::place_limit;
    value.side = Side::buy;
    value.time_in_force = TimeInForce::good_til_cancel;
    value.post_only = PostOnlyMode::required;
    value.order_slot = LogicalOrderSlot{1};
    value.instrument_spec_version = InstrumentSpecVersion{7};
    value.venue_capabilities_version = VenueCapabilitiesVersion{8};
    value.price_source = {EconomicValueSourceKind::feature, 0,
                          RoundingMode::toward_zero, {}, 1, {}};
    value.quantity_source = {EconomicValueSourceKind::fixed, 0,
                             RoundingMode::toward_zero,
                             QuantityLots::from_atoms(2), 0};
    value.safety_caps = {QuantityLots::from_atoms(2),
                         MoneyMicros::from_atoms(10'000),
                         PriceTicks::from_atoms(2), PriceTicks::from_atoms(1),
                         100, value.price_source};
    return value;
}

velocita::v2::ExecutablePolicyArtifact executable_policy(
    const velocita::v2::FeatureSchemaArtifact& schema) {
    using namespace velocita::v2;
    ExecutablePolicyArtifact value;
    value.schema_version = kExecutablePolicyArtifactSchemaVersion;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.policy_artifact_digest = policy().artifact_digest;
    value.feature_schema_id = schema.feature_schema_id;
    value.feature_schema_digest = schema.artifact_digest;
    value.template_count = 1;
    value.templates[0] = action_template();
    value.rule_binding_count = 1;
    value.rule_template_slots[0] = ActionTemplateSlot{1};
    value.artifact_digest = compute_artifact_digest(value).digest;
    return value;
}

velocita::v2::DecisionFrame frame() {
    using namespace velocita::v2;
    DecisionFrame value;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.feature_schema_id = FeatureSchemaId{3};
    value.venue_id = VenueId{9};
    value.venue_session_id = VenueSessionId{1};
    value.venue_sequence = VenueSequence{1};
    value.source_id = SourceId{12};
    value.source_generation = SourceGeneration{1};
    value.source_sequence = SourceSequence{1};
    value.event_time_ns = 150;
    value.receive_time_ns = 151;
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.feature_count = 1;
    value.features[0] = FeatureMicros::from_atoms(11);
    value.market_data_status = MarketDataStatus::ready;
    value.market_data_age_ns = 1;
    value.position_version = PositionVersion{1};
    value.risk_version = RiskVersion{1};
    value.raw_evidence_digest = digest(0x44);
    return value;
}

velocita::v2::TradingState ready_state(
    const velocita::v2::EvaluationMode mode =
        velocita::v2::EvaluationMode::authoritative,
    const velocita::v2::FeatureMicros feature =
        velocita::v2::FeatureMicros::from_atoms(11),
    const velocita::v2::MarketDataStatus market_status =
        velocita::v2::MarketDataStatus::ready) {
    using namespace velocita::v2;
    TradingState state;
    state.strategy_mode = StrategyMode::normal;
    state.kill_epoch = KillEpoch{2};
    state.executor_generation = ExecutorGeneration{3};
    state.policy_count = 1;
    state.policies[0] = {policy(), PolicyLifecycle::active};
    const auto schema = feature_schema();
    state.active_executable_policy = {
        true,
        schema,
        executable_policy(schema),
        {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4},
         InstrumentSpecVersion{7}, MoneyMicros::from_atoms(100),
         PriceTicks::from_atoms(1), PriceTicks::from_atoms(100),
         QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)},
        {kVenueCapabilitiesSchemaVersion, VenueId{9},
         VenueCapabilitiesVersion{8}, kVenueDirectivePlaceLimit, kVenueTifGtc,
         1},
        1,
        2};
    state.source_count = 1;
    state.sources[0] = {SourceId{12}, SourceGeneration{1}, SourceSequence{1}};
    state.source_event_count = 1;
    state.source_events[0] = {SourceId{12}, SourceGeneration{1},
                              SourceSequence{1}, digest(0x44)};
    state.risk_account_count = 1;
    state.risk_accounts[0] = {
        AccountId{5}, InstrumentId{4}, RiskPolicyVersion{6}, RiskVersion{1},
        PositionVersion{1}, QuantityLots::from_atoms(4),
        QuantityLots::from_atoms(8), {}, MoneyMicros::from_atoms(4'000),
        MoneyMicros::from_atoms(8'000), {}, {}, 1'000'000, 2'500};

    auto decision_frame = frame();
    decision_frame.features[0] = feature;
    decision_frame.market_data_status = market_status;
    ApplyScratch scratch;
    const Command decision{{CallerId{1}, RequestSequence{1}, digest(0x51)},
                           EvaluateDecisionFrame{DecisionId{20}, mode,
                                                 decision_frame}};
    const auto result = apply(state, decision, scratch);
    require(result.code == ApplyCode::applied,
            "Phase 3 fixture command was not retained");
    return state;
}

velocita::v2::Command authorize(const std::uint64_t request,
                                const std::uint8_t marker) {
    using namespace velocita::v2;
    return {{CallerId{1}, RequestSequence{request}, digest(marker)},
            AuthorizeDecision{DecisionId{20}}};
}

velocita::v2::Command observe_fill(
    const std::uint64_t request,
    const std::uint8_t marker,
    const velocita::v2::ClusterOrderId order_id,
    const velocita::v2::EffectId effect_id,
    const velocita::v2::ExecutionId execution_id,
    const std::int64_t quantity = 1) {
    using namespace velocita::v2;
    VenueObservation observation;
    observation.kind = VenueObservationKind::fill;
    observation.venue_id = VenueId{9};
    observation.venue_session_id = VenueSessionId{2};
    observation.venue_sequence = VenueSequence{execution_id.value()};
    observation.cluster_order_id = order_id;
    observation.effect_id = effect_id;
    observation.venue_order_id = VenueOrderId{70};
    observation.execution_id = execution_id;
    observation.side = Side::buy;
    observation.price = PriceTicks::from_atoms(11);
    observation.quantity = QuantityLots::from_atoms(quantity);
    observation.fee = FeeMicros::from_atoms(3);
    observation.evidence_digest = digest(marker);
    return {{CallerId{2}, RequestSequence{request}, observation_fingerprint(observation)},
            ObserveFill{observation}};
}

velocita::v2::Command request_cancel(
    const std::uint64_t request,
    const std::uint8_t marker,
    const velocita::v2::ClusterOrderId order_id) {
    using namespace velocita::v2;
    return {{CallerId{3}, RequestSequence{request}, digest(marker)},
            RequestCancel{order_id}};
}

velocita::v2::Command observe_order_event(
    const std::uint64_t request,
    const std::uint8_t marker,
    const velocita::v2::VenueObservationKind kind,
    const velocita::v2::ClusterOrderId order_id,
    const velocita::v2::EffectId effect_id,
    const std::uint64_t venue_sequence) {
    using namespace velocita::v2;
    VenueObservation observation;
    observation.kind = kind;
    observation.venue_id = VenueId{9};
    observation.venue_session_id = VenueSessionId{2};
    observation.venue_sequence = VenueSequence{venue_sequence};
    observation.cluster_order_id = order_id;
    observation.effect_id = effect_id;
    if (kind == VenueObservationKind::order_acknowledged ||
        kind == VenueObservationKind::cancelled) {
        observation.venue_order_id = VenueOrderId{70};
    }
    observation.evidence_digest = digest(marker);
    return {{CallerId{4}, RequestSequence{request}, observation_fingerprint(observation)},
            ObserveOrderEvent{observation}};
}

void append_replacement_decision(
    velocita::v2::TradingState& state,
    const velocita::v2::DecisionId decision_id,
    const std::int64_t price,
    const std::int64_t quantity) {
    using namespace velocita::v2;
    const auto index = state.decision_count++;
    auto replacement_frame = state.decision_cold[0].frame;
    replacement_frame.position_version = state.risk_accounts[0].position_version;
    replacement_frame.risk_version = state.risk_accounts[0].risk_version;
    state.decision_hot[index] = {
        decision_id, replacement_frame.policy_id, replacement_frame.policy_version,
        state.state_version, state.kill_epoch, state.executor_generation, 0, 1,
        DecisionCode::action, ContractError::none, Directive::replace,
        Directive::replace, EvaluationMode::authoritative, state.strategy_mode,
        ActionResolutionCode::proposed};
    state.decision_cold[index] = {
        decision_id, digest(static_cast<std::uint8_t>(decision_id.value())),
        replacement_frame,
        {kEconomicActionProposalSchemaVersion,
         decision_id,
         replacement_frame.policy_id,
         replacement_frame.policy_version,
         RiskPolicyVersion{6},
         InstrumentSpecVersion{7},
         VenueCapabilitiesVersion{8},
         replacement_frame.account_id,
         replacement_frame.instrument_id,
         replacement_frame.venue_id,
         LogicalOrderSlot{1},
         Directive::replace,
         Side::buy,
         TimeInForce::good_til_cancel,
         PostOnlyMode::required,
         PriceTicks::from_atoms(price),
         QuantityLots::from_atoms(quantity),
         MoneyMicros::from_atoms(price * quantity * 100)}};
}

// Drive one decision through the REAL admission path: record the source event,
// then evaluate the frame. Writing decision records directly would bypass the
// capacity checks where reclamation lives, so a harness shortcut there would
// test nothing.
velocita::v2::ApplyResult evaluate_new_decision(
    velocita::v2::TradingState& state,
    velocita::v2::ApplyScratch& scratch,
    const velocita::v2::DecisionId decision_id,
    const std::uint64_t source_sequence,
    const std::uint64_t request) {
    using namespace velocita::v2;
    const auto evidence = digest(static_cast<std::uint8_t>(0x40 + (source_sequence % 0x30)));
    const Command record{{CallerId{7}, RequestSequence{request}, digest(0x21)},
                         RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                           SourceSequence{source_sequence}, evidence}};
    require(apply(state, record, scratch).code == ApplyCode::applied,
            "source event admission failed");

    auto value = frame();
    value.venue_sequence = VenueSequence{source_sequence};
    value.source_sequence = SourceSequence{source_sequence};
    value.raw_evidence_digest = evidence;
    value.position_version = state.risk_accounts[0].position_version;
    value.risk_version = state.risk_accounts[0].risk_version;
    const Command evaluate{{CallerId{8}, RequestSequence{request},
                            digest(static_cast<std::uint8_t>(decision_id.value()))},
                           EvaluateDecisionFrame{decision_id,
                                                 EvaluationMode::authoritative, value}};
    return apply(state, evaluate, scratch);
}

velocita::v2::Command authorize_decision(const std::uint64_t request,
                                         const std::uint8_t marker,
                                         const velocita::v2::DecisionId decision_id) {
    using namespace velocita::v2;
    return {{CallerId{1}, RequestSequence{request}, digest(marker)},
            AuthorizeDecision{decision_id}};
}

// A second authoritative place_limit decision targeting the SAME logical order
// slot as the Phase 3 fixture decision. Slot exclusivity, not directive class,
// is what must refuse the resulting authorization.
void append_submit_decision(velocita::v2::TradingState& state,
                            const velocita::v2::DecisionId decision_id,
                            const std::int64_t price,
                            const std::int64_t quantity,
                            const velocita::v2::LogicalOrderSlot slot =
                                velocita::v2::LogicalOrderSlot{1}) {
    using namespace velocita::v2;
    // Bounds-check in the harness itself: without reclamation the decision arena
    // fills at the same rate as orders, and an unchecked append would write past
    // the array and make the failure undefined rather than diagnosable.
    require(state.decision_count < kMaxDecisionRecords,
            "decision arena is full: retained decisions were not reclaimed");
    const auto index = state.decision_count++;
    auto submit_frame = state.decision_cold[0].frame;
    submit_frame.position_version = state.risk_accounts[0].position_version;
    submit_frame.risk_version = state.risk_accounts[0].risk_version;
    state.decision_hot[index] = {
        decision_id, submit_frame.policy_id, submit_frame.policy_version,
        state.state_version, state.kill_epoch, state.executor_generation, 0, 1,
        DecisionCode::action, ContractError::none, Directive::place_limit,
        Directive::place_limit, EvaluationMode::authoritative, state.strategy_mode,
        ActionResolutionCode::proposed};
    state.decision_cold[index] = {
        decision_id, digest(static_cast<std::uint8_t>(decision_id.value())),
        submit_frame,
        {kEconomicActionProposalSchemaVersion,
         decision_id,
         submit_frame.policy_id,
         submit_frame.policy_version,
         RiskPolicyVersion{6},
         InstrumentSpecVersion{7},
         VenueCapabilitiesVersion{8},
         submit_frame.account_id,
         submit_frame.instrument_id,
         submit_frame.venue_id,
         slot,
         Directive::place_limit,
         Side::buy,
         TimeInForce::good_til_cancel,
         PostOnlyMode::required,
         PriceTicks::from_atoms(price),
         QuantityLots::from_atoms(quantity),
         MoneyMicros::from_atoms(price * quantity * 100)}};
}

velocita::v2::Command request_replace(
    const std::uint64_t request,
    const std::uint8_t marker,
    const velocita::v2::ClusterOrderId order_id,
    const velocita::v2::DecisionId decision_id) {
    using namespace velocita::v2;
    return {{CallerId{5}, RequestSequence{request}, digest(marker)},
            RequestReplace{order_id, decision_id}};
}

void require_economics_unchanged(const velocita::v2::TradingState& before,
                                 const velocita::v2::TradingState& after,
                                 const std::string& context) {
    require(after.reservation_count == before.reservation_count &&
                after.order_count == before.order_count &&
                after.outbox_effect_count == before.outbox_effect_count &&
                after.risk_accounts[0].reserved_quantity ==
                    before.risk_accounts[0].reserved_quantity &&
                after.risk_accounts[0].reserved_margin ==
                    before.risk_accounts[0].reserved_margin &&
                after.risk_accounts[0].reserved_fee ==
                    before.risk_accounts[0].reserved_fee &&
                after.risk_accounts[0].position_quantity ==
                    before.risk_accounts[0].position_quantity &&
                after.risk_accounts[0].position_version ==
                    before.risk_accounts[0].position_version,
            context);
}

void test_submit_authorization_is_atomic_and_idempotent() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto command = authorize(2, 0x52);
    const auto outcome = apply(state, command, scratch);

    require(outcome.code == ApplyCode::applied &&
                outcome.cluster_order_id() != ClusterOrderId{} &&
                outcome.effect_id() != EffectId{},
            "valid authorization did not return stable identities");
    require(state.reservation_count == 1 && state.order_count == 1 &&
                state.outbox_effect_count == 1,
            "authorization did not create exactly one atomic triple");
    require(state.reservations[0].cluster_order_id ==
                outcome.cluster_order_id() &&
                state.orders[0].cluster_order_id == outcome.cluster_order_id() &&
                state.outbox_effects[0].cluster_order_id ==
                    outcome.cluster_order_id() &&
                state.outbox_effects[0].effect_id == outcome.effect_id() &&
                state.outbox_effects[0].decision_id == DecisionId{20},
            "reservation, order, and effect are not linked by stable identity");

    // Notional is 11 ticks * 2 lots * 100 quote micros = 2,200.
    // Full margin is 2,200 and ceil(2,200 * 2,500 / 1,000,000) is 6.
    require(state.risk_accounts[0].reserved_quantity ==
                QuantityLots::from_atoms(2) &&
                state.risk_accounts[0].reserved_margin ==
                    MoneyMicros::from_atoms(2'200) &&
                state.risk_accounts[0].reserved_fee ==
                    MoneyMicros::from_atoms(6) &&
                outcome.reserved_exposure() == MoneyMicros::from_atoms(2'206),
            "authorization did not use the reviewed conservative formula");

    const auto replay = apply(state, command, scratch);
    require(replay == outcome && state.reservation_count == 1 &&
                state.order_count == 1 && state.outbox_effect_count == 1,
            "request replay changed economics or lost retained identities");

    const auto conflict = apply(state, authorize(2, 0x53), scratch);
    require(conflict.code == ApplyCode::duplicate_conflict &&
                state.reservation_count == 1 && state.order_count == 1 &&
                state.outbox_effect_count == 1,
            "same request identity with different payload was not a conflict");
}

void test_risk_and_capacity_fail_before_economic_mutation() {
    using namespace velocita::v2;
    ApplyScratch scratch;

    auto insufficient = ready_state();
    insufficient.risk_accounts[0].max_reserved_notional =
        MoneyMicros::from_atoms(2'205);
    const auto insufficient_before = insufficient;
    require(apply(insufficient, authorize(2, 0x61), scratch).code ==
                ApplyCode::insufficient_risk_capacity,
            "insufficient risk was not rejected");
    require_economics_unchanged(insufficient_before, insufficient,
                                "insufficient risk partially mutated economics");

    auto stale = ready_state();
    stale.risk_accounts[0].risk_version = RiskVersion{2};
    const auto stale_before = stale;
    require(apply(stale, authorize(2, 0x62), scratch).code ==
                ApplyCode::stale_risk_state,
            "stale risk version was not rejected");
    require_economics_unchanged(stale_before, stale,
                                "stale risk partially mutated economics");

    auto full = ready_state();
    full.outbox_effect_count = kMaxOutboxEffects;
    const auto full_before = full;
    require(apply(full, authorize(2, 0x63), scratch).code ==
                ApplyCode::outbox_capacity_exceeded,
            "full outbox did not fail closed");
    require_economics_unchanged(full_before, full,
                                "full outbox partially mutated economics");
}

void test_non_authoritative_decisions_cannot_create_economics() {
    using namespace velocita::v2;
    ApplyScratch scratch;

    auto shadow = ready_state(EvaluationMode::shadow);
    const auto shadow_before = shadow;
    require(apply(shadow, authorize(2, 0x71), scratch).code ==
                ApplyCode::decision_not_authorizable,
            "shadow decision entered authorization");
    require_economics_unchanged(shadow_before, shadow,
                                "shadow decision mutated economics");

    auto no_action = ready_state(EvaluationMode::authoritative,
                                 FeatureMicros::from_atoms(10));
    const auto no_action_before = no_action;
    require(apply(no_action, authorize(2, 0x72), scratch).code ==
                ApplyCode::decision_not_authorizable,
            "no-action decision entered authorization");
    require_economics_unchanged(no_action_before, no_action,
                                "no-action decision mutated economics");

    auto rejected = ready_state(EvaluationMode::authoritative,
                                FeatureMicros::from_atoms(11),
                                MarketDataStatus::stale);
    const auto rejected_before = rejected;
    require(apply(rejected, authorize(2, 0x73), scratch).code ==
                ApplyCode::decision_not_authorizable,
            "rejected decision entered authorization");
    require_economics_unchanged(rejected_before, rejected,
                                "rejected decision mutated economics");
}

void test_partial_fills_conserve_risk_position_and_identity() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto authorization = apply(state, authorize(2, 0x81), scratch);
    require(authorization.code == ApplyCode::applied,
            "fill fixture was not authorized");

    const auto first_command = observe_fill(
        1, 0x82, authorization.cluster_order_id(), authorization.effect_id(),
        ExecutionId{101});
    const auto first = apply(state, first_command, scratch);
    require(first.code == ApplyCode::applied &&
                first.execution_id() == ExecutionId{101} &&
                first.filled_quantity() == QuantityLots::from_atoms(1),
            "first partial fill was not accepted with retained identity");
    require(state.risk_accounts[0].position_quantity ==
                    QuantityLots::from_atoms(1) &&
                state.risk_accounts[0].position_version == PositionVersion{2} &&
                state.risk_accounts[0].reserved_quantity ==
                    QuantityLots::from_atoms(1) &&
                state.risk_accounts[0].reserved_margin ==
                    MoneyMicros::from_atoms(1'100) &&
                state.risk_accounts[0].reserved_fee ==
                    MoneyMicros::from_atoms(3),
            "partial fill did not atomically move quantity from reservation to position");
    require(state.reservations[0].original_quantity ==
                    QuantityLots::from_atoms(2) &&
                state.reservations[0].filled_quantity ==
                    QuantityLots::from_atoms(1) &&
                state.reservations[0].remaining_quantity ==
                    QuantityLots::from_atoms(1) &&
                state.reservations[0].remaining_notional ==
                    MoneyMicros::from_atoms(1'100) &&
                state.reservations[0].margin_hold ==
                    MoneyMicros::from_atoms(1'100) &&
                state.reservations[0].fee_hold == MoneyMicros::from_atoms(3) &&
                state.reservations[0].state ==
                    ReservationState::partially_consumed &&
                state.orders[0].state == ClusterOrderState::partially_filled &&
                state.outbox_effects[0].state == OutboxState::acknowledged,
            "partial fill broke reservation/order/outbox conservation");

    const auto after_first = state;
    require(apply(state, first_command, scratch) == first &&
                state.risk_accounts[0] == after_first.risk_accounts[0] &&
                state.reservations[0] == after_first.reservations[0] &&
                state.orders[0] == after_first.orders[0] &&
                state.outbox_effects[0] == after_first.outbox_effects[0],
            "same request replay applied a partial fill twice");

    const auto duplicate = apply(
        state,
        observe_fill(2, 0x82, authorization.cluster_order_id(),
                     authorization.effect_id(), ExecutionId{101}),
        scratch);
    require(duplicate.code == ApplyCode::already_applied &&
                state.risk_accounts[0].position_quantity ==
                    QuantityLots::from_atoms(1),
            "same execution under a new request duplicated economics");

    const auto conflict_before = state;
    const auto conflict = apply(
        state,
        observe_fill(3, 0x83, authorization.cluster_order_id(),
                     authorization.effect_id(), ExecutionId{101}, 2),
        scratch);
    require(conflict.code == ApplyCode::fill_identity_conflict &&
                state.risk_accounts[0] ==
                    conflict_before.risk_accounts[0] &&
                state.reservations[0] == conflict_before.reservations[0],
            "conflicting duplicate execution changed economics");

    const auto terminal = apply(
        state,
        observe_fill(4, 0x84, authorization.cluster_order_id(),
                     authorization.effect_id(), ExecutionId{102}),
        scratch);
    require(terminal.code == ApplyCode::applied &&
                state.risk_accounts[0].position_quantity ==
                    QuantityLots::from_atoms(2) &&
                state.risk_accounts[0].position_version == PositionVersion{3} &&
                state.risk_accounts[0].reserved_quantity == QuantityLots{} &&
                state.risk_accounts[0].reserved_margin == MoneyMicros{} &&
                state.risk_accounts[0].reserved_fee == MoneyMicros{} &&
                state.reservations[0].filled_quantity ==
                    state.reservations[0].original_quantity &&
                state.reservations[0].remaining_quantity == QuantityLots{} &&
                state.reservations[0].state == ReservationState::released &&
                state.orders[0].state == ClusterOrderState::filled,
            "terminal fill did not release the remaining hold exactly once");
}

void test_fill_replay_and_snapshot_are_byte_identical() {
    using namespace velocita::v2;
    auto direct = ready_state();
    ApplyScratch direct_scratch;
    const auto authorization = apply(direct, authorize(2, 0x91), direct_scratch);
    const auto fill = observe_fill(
        1, 0x92, authorization.cluster_order_id(), authorization.effect_id(),
        ExecutionId{201});
    auto replayed = direct;

    std::array<std::byte, kMaxKernelCommandBytes> command_bytes{};
    const auto encoded_command = encode_kernel_command(fill, command_bytes);
    const auto direct_result = apply(direct, fill, direct_scratch);
    ApplyScratch replay_scratch;
    KernelCommandCodecScratch codec_scratch;
    std::array<std::byte, kKernelResultBytes> result_bytes{};
    const auto replay_result = apply_encoded_kernel_command(
        replayed, {command_bytes.data(), encoded_command.bytes_written},
        replay_scratch, codec_scratch, result_bytes);
    require(encoded_command.error == KernelCodecError::none &&
                direct_result.code == ApplyCode::applied &&
                replay_result.error == ReplayError::none &&
                replay_result.apply_result == direct_result,
            "canonical fill replay diverged from direct apply");

    std::array<std::byte, kMaxStateSnapshotBytes> direct_bytes{};
    std::array<std::byte, kMaxStateSnapshotBytes> replay_bytes{};
    const auto direct_snapshot = encode_state_snapshot(direct, direct_bytes);
    const auto replay_snapshot = encode_state_snapshot(replayed, replay_bytes);
    require(direct_snapshot.error == StateSnapshotError::none &&
                replay_snapshot.error == StateSnapshotError::none &&
                direct_snapshot.state_digest == replay_snapshot.state_digest &&
                direct_snapshot.bytes_written == replay_snapshot.bytes_written &&
                std::equal(direct_bytes.begin(),
                           direct_bytes.begin() + static_cast<std::ptrdiff_t>(
                                                      direct_snapshot.bytes_written),
                           replay_bytes.begin()),
            "fill replay did not reconstruct byte-identical economics");

    TradingState restored;
    StateSnapshotScratch snapshot_scratch;
    const auto decoded = decode_state_snapshot(
        {direct_bytes.data(), direct_snapshot.bytes_written}, restored,
        snapshot_scratch);
    std::array<std::byte, kMaxStateSnapshotBytes> restored_bytes{};
    const auto restored_snapshot = encode_state_snapshot(restored, restored_bytes);
    require(decoded.error == StateSnapshotError::none &&
                restored.risk_accounts[0].position_quantity ==
                    QuantityLots::from_atoms(1) &&
                restored.receipts[2].result.execution_id() == ExecutionId{201} &&
                restored_snapshot.error == StateSnapshotError::none &&
                restored_snapshot.state_digest == direct_snapshot.state_digest &&
                std::equal(direct_bytes.begin(),
                           direct_bytes.begin() + static_cast<std::ptrdiff_t>(
                                                      direct_snapshot.bytes_written),
                           restored_bytes.begin()),
            "snapshot lost fill identity, position, or canonical bytes");
}

void test_fill_failures_are_atomic_and_bounded() {
    using namespace velocita::v2;
    ApplyScratch scratch;

    auto overfill = ready_state();
    const auto overfill_auth = apply(overfill, authorize(2, 0xa1), scratch);
    const auto overfill_before = overfill;
    require(apply(overfill,
                  observe_fill(1, 0xa2, overfill_auth.cluster_order_id(),
                               overfill_auth.effect_id(), ExecutionId{301}, 3),
                  scratch)
                    .code == ApplyCode::fill_quantity_conflict,
            "fill above remaining quantity was accepted");
    require_economics_unchanged(overfill_before, overfill,
                                "overfill partially mutated economics");

    auto overflow = ready_state();
    const auto overflow_auth = apply(overflow, authorize(2, 0xa3), scratch);
    overflow.risk_accounts[0].position_quantity = QuantityLots::from_atoms(
        std::numeric_limits<std::int64_t>::max());
    const auto overflow_before = overflow;
    require(apply(overflow,
                  observe_fill(1, 0xa4, overflow_auth.cluster_order_id(),
                               overflow_auth.effect_id(), ExecutionId{302}),
                  scratch)
                    .code == ApplyCode::position_arithmetic_overflow,
            "overflowing position update was accepted");
    require_economics_unchanged(overflow_before, overflow,
                                "position overflow partially mutated economics");

    auto full = ready_state();
    const auto full_auth = apply(full, authorize(2, 0xa5), scratch);
    for (std::size_t index = 0; index < kMaxVenueEventReceipts; ++index) {
        ApplyResult retained;
        retained.code = ApplyCode::applied;
        retained.state_version = full.state_version;
        retained.strategy_mode = full.strategy_mode;
        retained.kill_epoch = full.kill_epoch;
        retained.executor_generation = full.executor_generation;
        retained.detail = ApplyResultDetail::fill(
            VenueId{9}, ExecutionId{index + 1}, QuantityLots::from_atoms(1));
        retained.detail_kind = ApplyResultDetailKind::fill;
        full.receipts[full.receipt_count++] = {
            {CallerId{9}, RequestSequence{index + 1}, digest(0xb0)}, retained};
    }
    const auto full_before = full;
    require(apply(full,
                  observe_fill(1, 0xa6, full_auth.cluster_order_id(),
                               full_auth.effect_id(), ExecutionId{401}),
                  scratch)
                    .code == ApplyCode::fill_capacity_exceeded,
            "full fill/event receipt policy did not fail closed");
    require_economics_unchanged(full_before, full,
                                "full fill/event policy partially mutated economics");
}

void test_cancel_and_unknown_remain_risk_bearing_until_evidence() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = apply(state, authorize(2, 0xc1), scratch);
    const auto risk_before_cancel = state.risk_accounts[0];

    const auto cancel = apply(
        state, request_cancel(1, 0xc2, submit.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied &&
                cancel.cluster_order_id() == submit.cluster_order_id() &&
                cancel.effect_id() != submit.effect_id() &&
                state.order_count == 1 && state.outbox_effect_count == 2 &&
                state.orders[0].state == ClusterOrderState::cancel_pending &&
                state.outbox_effects[1].operation == EffectOperation::cancel &&
                state.outbox_effects[1].state == OutboxState::pending &&
                state.risk_accounts[0] == risk_before_cancel,
            "cancel intent released risk or failed to create one committed effect");

    const auto unknown = apply(
        state,
        observe_order_event(1, 0xc3, VenueObservationKind::effect_unknown,
                            submit.cluster_order_id(), cancel.effect_id(), 301),
        scratch);
    require(unknown.code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::submission_unknown &&
                state.outbox_effects[1].state == OutboxState::unknown &&
                state.risk_accounts[0] == risk_before_cancel &&
                state.reservations[0].state == ReservationState::held,
            "unknown cancel submission released or changed reserved economics");

    auto cancelled = ready_state();
    const auto cancelled_submit = apply(cancelled, authorize(2, 0xc4), scratch);
    const auto cancelled_intent = apply(
        cancelled, request_cancel(1, 0xc5, cancelled_submit.cluster_order_id()),
        scratch);
    const auto accepted_cancel = apply(
        cancelled,
        observe_order_event(1, 0xc6, VenueObservationKind::cancelled,
                            cancelled_submit.cluster_order_id(),
                            cancelled_intent.effect_id(), 302),
        scratch);
    require(accepted_cancel.code == ApplyCode::applied &&
                cancelled.orders[0].state == ClusterOrderState::reconciling &&
                cancelled.outbox_effects[1].state == OutboxState::reconciling &&
                cancelled.reservations[0].state == ReservationState::held &&
                cancelled.risk_accounts[0].reserved_quantity == QuantityLots::from_atoms(2) &&
                cancelled.risk_accounts[0].reserved_margin == MoneyMicros::from_atoms(2'200) &&
                cancelled.risk_accounts[0].reserved_fee == MoneyMicros::from_atoms(6),
            "cancel without fill completeness evidence released risk");
    std::array<std::byte, kMaxStateSnapshotBytes> cancelled_snapshot{};
    require(encode_state_snapshot(cancelled, cancelled_snapshot).error ==
                StateSnapshotError::none,
            "cancelled quantity conservation did not survive state validation");
}

void test_fill_cancel_race_consumes_one_reservation() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = apply(state, authorize(2, 0xd1), scratch);
    require(apply(state,
                  observe_fill(1, 0xd2, submit.cluster_order_id(),
                               submit.effect_id(), ExecutionId{501}),
                  scratch)
                    .code == ApplyCode::applied,
            "race fixture partial fill failed");
    const auto cancel = apply(
        state, request_cancel(1, 0xd3, submit.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::cancel_pending &&
                state.reservations[0].remaining_quantity ==
                    QuantityLots::from_atoms(1),
            "cancel race did not preserve residual risk");

    require(apply(state,
                  observe_fill(2, 0xd4, submit.cluster_order_id(),
                               submit.effect_id(), ExecutionId{502}),
                  scratch)
                    .code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::filled &&
                state.reservations[0].state == ReservationState::released &&
                state.risk_accounts[0].position_quantity ==
                    QuantityLots::from_atoms(2) &&
                state.risk_accounts[0].reserved_quantity == QuantityLots{},
            "fill racing cancel did not consume exactly the residual reservation");

    require(apply(state,
                  observe_order_event(1, 0xd5,
                                      VenueObservationKind::cancelled,
                                      submit.cluster_order_id(),
                                      cancel.effect_id(), 503),
                  scratch)
                    .code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::filled &&
                state.risk_accounts[0].position_quantity ==
                    QuantityLots::from_atoms(2) &&
                state.risk_accounts[0].reserved_quantity == QuantityLots{},
            "late cancel evidence overwrote the fill terminal outcome");
}

void test_delayed_observations_cannot_regress_order_knowledge() {
    using namespace velocita::v2;
    ApplyScratch scratch;
    auto filled = ready_state();
    const auto submit = apply(filled, authorize(2, 0x41), scratch);
    require(apply(filled, observe_fill(1, 0x42, submit.cluster_order_id(),
                                      submit.effect_id(), ExecutionId{801}, 2),
                  scratch).code == ApplyCode::applied,
            "terminal observation fixture failed");
    for (const auto kind : {VenueObservationKind::order_acknowledged,
                            VenueObservationKind::effect_unknown,
                            VenueObservationKind::order_rejected}) {
        auto candidate = filled;
        const auto result = apply(candidate, observe_order_event(
            1, 0x43, kind, submit.cluster_order_id(), submit.effect_id(), 802), scratch);
        require(result.code == ApplyCode::venue_observation_not_applicable &&
                    candidate.orders[0] == filled.orders[0] &&
                    candidate.outbox_effects[0] == filled.outbox_effects[0] &&
                    candidate.reservations[0] == filled.reservations[0] &&
                    candidate.risk_accounts[0] == filled.risk_accounts[0],
                "delayed observation regressed a filled order or its evidence");
    }

    auto partial = ready_state();
    const auto partial_submit = apply(partial, authorize(2, 0x44), scratch);
    require(apply(partial, observe_fill(1, 0x45, partial_submit.cluster_order_id(),
                                       partial_submit.effect_id(), ExecutionId{803}),
                  scratch).code == ApplyCode::applied, "partial fixture failed");
    const auto before = partial;
    static_cast<void>(apply(partial, observe_order_event(
        1, 0x46, VenueObservationKind::order_acknowledged,
        partial_submit.cluster_order_id(), partial_submit.effect_id(), 804), scratch));
    require(partial.orders[0] == before.orders[0] &&
                partial.reservations[0] == before.reservations[0],
            "late submit ack erased partial-fill knowledge");

    auto cancelling = ready_state();
    const auto cancel_submit = apply(cancelling, authorize(2, 0x47), scratch);
    require(apply(cancelling, request_cancel(1, 0x48,
                                            cancel_submit.cluster_order_id()),
                  scratch).code == ApplyCode::applied, "cancel fixture failed");
    const auto cancel_before = cancelling;
    static_cast<void>(apply(cancelling, observe_order_event(
        1, 0x49, VenueObservationKind::order_acknowledged,
        cancel_submit.cluster_order_id(), cancel_submit.effect_id(), 805), scratch));
    require(cancelling.orders[0] == cancel_before.orders[0] &&
                cancelling.risk_accounts[0] == cancel_before.risk_accounts[0],
            "historical submit ack erased pending cancel intent");
}

void test_authorization_rechecks_current_controls() {
    using namespace velocita::v2;
    ApplyScratch scratch;
    for (const auto mode : {StrategyMode::halted, StrategyMode::no_new_risk,
                            StrategyMode::cancel_only, StrategyMode::reduce_only}) {
        auto state = ready_state();
        state.strategy_mode = mode;
        const auto before = state;
        require(apply(state, authorize(2, 0x61), scratch).code ==
                    ApplyCode::stale_control_generation,
                "old actionable decision bypassed current risk controls");
        require_economics_unchanged(before, state, "control refusal mutated economics");
    }
    auto stale = ready_state();
    stale.kill_epoch = KillEpoch{stale.kill_epoch.value() + 1};
    require(apply(stale, authorize(2, 0x62), scratch).code ==
                ApplyCode::stale_control_generation,
            "decision from a previous kill epoch was authorized");

    // Replacement previously had to recheck controls because it could create an
    // effect. It now refuses before reaching any control check, which is a
    // strictly stronger property: no control state permits it. Asserting the
    // unconditional refusal keeps the original intent - stale controls cannot
    // slip a replacement through - without asserting a check that no longer runs.
    auto replacement = ready_state();
    const auto submit = apply(replacement, authorize(2, 0x63), scratch);
    append_replacement_decision(replacement, DecisionId{21}, 12, 3);
    replacement.executor_generation = ExecutorGeneration{
        replacement.executor_generation.value() + 1};
    const auto before = replacement;
    require(apply(replacement, request_replace(1, 0x64,
                  submit.cluster_order_id(), DecisionId{21}), scratch).code ==
                ApplyCode::replacement_not_supported,
            "replacement was not refused under a changed executor generation");
    require_economics_unchanged(before, replacement,
                                "refused replacement mutated economics");
}

void test_decision_retry_retains_original_authorization_after_fill() {
    using namespace velocita::v2;
    ApplyScratch scratch;
    auto state = ready_state();
    const auto submit = apply(state, authorize(2, 0x81), scratch);
    require(apply(state, observe_fill(1, 0x82, submit.cluster_order_id(),
                  submit.effect_id(), ExecutionId{1001}, 2), scratch).code ==
                ApplyCode::applied, "decision retry fixture fill failed");
    const auto before = state;
    const auto retry = apply(state, authorize(3, 0x83), scratch);
    require(retry.code == ApplyCode::already_applied &&
                retry.cluster_order_id() == submit.cluster_order_id() &&
                retry.effect_id() == submit.effect_id() &&
                retry.reserved_exposure() == submit.reserved_exposure(),
            "decision retry reconstructed its response from a consumed hold");
    require_economics_unchanged(before, state, "decision retry created new economics");
    std::array<std::byte, kKernelResultBytes> bytes{};
    require(encode_kernel_result(retry, bytes).error == KernelCodecError::none,
            "post-fill decision retry produced an unencodable result");
}

void test_cancel_fill_final_evidence_permutations() {
    using namespace velocita::v2;
    for (const std::int64_t filled : {0, 1, 2}) {
        std::array<int, 3> permutation{0, 1, 2};
        do {
            auto state = ready_state();
            ApplyScratch scratch;
            const auto submit = apply(state, authorize(2, 0x71), scratch);
            const auto cancel = apply(state,
                request_cancel(1, 0x72, submit.cluster_order_id()), scratch);
            auto final = observe_order_event(2, 0x75,
                VenueObservationKind::cancel_reconciled,
                submit.cluster_order_id(), cancel.effect_id(), 1703);
            auto& final_observation = std::get<ObserveOrderEvent>(final.body).observation;
            final_observation.venue_order_id = VenueOrderId{70};
            final_observation.quantity = QuantityLots::from_atoms(filled);
            final.identity.payload_digest = observation_fingerprint(final_observation);
            const std::array<Command, 3> commands{
                observe_fill(1, 0x73, submit.cluster_order_id(), submit.effect_id(),
                    ExecutionId{1701}, filled),
                observe_order_event(1, 0x74, VenueObservationKind::cancelled,
                    submit.cluster_order_id(), cancel.effect_id(), 1702), final};
            bool fill_seen = false;
            for (const auto step : permutation) {
                if (filled == 0 && step == 0) continue; // No execution exists in the zero-fill case.
                const auto result = apply(state, commands[step], scratch);
                if (step == 0) {
                    require(result.code == ApplyCode::applied, "permutation lost actual fill");
                    fill_seen = true;
                }
                require(state.risk_accounts[0].position_quantity ==
                    QuantityLots::from_atoms(fill_seen ? filled : 0),
                    "permutation inferred a position from terminal evidence");
                if (!fill_seen && filled != 0) {
                    require(state.risk_accounts[0].reserved_quantity == QuantityLots::from_atoms(2),
                            "early final evidence released unaccounted exposure");
                }
                std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
                const auto encoded = encode_state_snapshot(state, bytes);
                require(encoded.error == StateSnapshotError::none, "permutation snapshot failed");
                TradingState restored;
                StateSnapshotScratch snapshot_scratch;
                require(decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                    snapshot_scratch).error == StateSnapshotError::none,
                    "permutation snapshot restore failed");
                state = restored;
            }
            // A refused early proof is a stable command outcome. A new request
            // may re-present the same venue evidence once its prerequisites exist.
            final.identity.request_sequence = RequestSequence{3};
            const auto final_result = apply(state, final, scratch);
            require((final_result.code == ApplyCode::applied ||
                     final_result.code == ApplyCode::already_applied) &&
                    state.risk_accounts[0].position_quantity == QuantityLots::from_atoms(filled) &&
                    state.risk_accounts[0].reserved_quantity == QuantityLots{} &&
                    state.reservations[0].state == ReservationState::released &&
                    state.orders[0].state == (filled == 2 ? ClusterOrderState::filled
                                                          : ClusterOrderState::cancelled),
                    "evidence permutation did not converge to reconciled economics");
        } while (std::next_permutation(permutation.begin(), permutation.end()));
    }
}

void test_cancel_before_earlier_fill_does_not_lose_fill_evidence() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = apply(state, authorize(2, 0xf1), scratch);
    const auto cancel = apply(state,
        request_cancel(1, 0xf2, submit.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied, "late-fill cancel fixture failed");
    // Venue order: fill at 1601, cancellation at 1602. Delivery is reversed.
    require(apply(state, observe_order_event(1, 0xf3, VenueObservationKind::cancelled,
        submit.cluster_order_id(), cancel.effect_id(), 1602), scratch).code ==
            ApplyCode::applied, "late-fill cancellation fixture failed");
    require(state.orders[0].state == ClusterOrderState::reconciling &&
                state.reservations[0].remaining_quantity == QuantityLots::from_atoms(2),
            "cancellation released risk before missing fills arrived");
    std::array<std::byte, kMaxStateSnapshotBytes> pending_snapshot{};
    const auto pending = encode_state_snapshot(state, pending_snapshot);
    require(pending.error == StateSnapshotError::none, "pending reconciliation snapshot failed");
    TradingState restored;
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({pending_snapshot.data(), pending.bytes_written},
        restored, snapshot_scratch).error == StateSnapshotError::none,
        "pending reconciliation restore failed");
    state = restored;
    const auto late_fill = apply(state, observe_fill(1, 0xf4,
        submit.cluster_order_id(), submit.effect_id(), ExecutionId{1601}), scratch);
    require(late_fill.code == ApplyCode::applied &&
                state.risk_accounts[0].position_quantity == QuantityLots::from_atoms(1) &&
                state.reservations[0].filled_quantity == QuantityLots::from_atoms(1),
            "earlier venue fill delivered after cancel was lost from accounting");
    require(state.orders[0].state == ClusterOrderState::reconciling &&
                state.reservations[0].remaining_quantity == QuantityLots::from_atoms(1),
            "late fill reopened the order or released unproven residual");
    auto proof = observe_order_event(2, 0xf5, VenueObservationKind::cancel_reconciled,
        submit.cluster_order_id(), cancel.effect_id(), 1603);
    auto& evidence = std::get<ObserveOrderEvent>(proof.body).observation;
    evidence.venue_order_id = VenueOrderId{70};
    evidence.quantity = QuantityLots::from_atoms(2);
    proof.identity.payload_digest = observation_fingerprint(evidence);
    const auto before = state;
    require(apply(state, proof, scratch).code == ApplyCode::venue_observation_not_applicable,
            "unseen fills were inferred from a reconciliation total");
    require_economics_unchanged(before, state, "mismatched reconciliation released risk");
    proof.identity.request_sequence = RequestSequence{3};
    evidence.quantity = QuantityLots::from_atoms(1);
    proof.identity.payload_digest = observation_fingerprint(evidence);
    auto replayed = state;
    std::array<std::byte, kMaxKernelCommandBytes> command_bytes{};
    const auto encoded_command = encode_kernel_command(proof, command_bytes);
    require(encoded_command.error == KernelCodecError::none, "reconciliation command did not encode");
    KernelCommandCodecScratch codec_scratch;
    std::array<std::byte, kKernelResultBytes> result_bytes{};
    const auto replay = apply_encoded_kernel_command(replayed,
        {command_bytes.data(), encoded_command.bytes_written}, scratch, codec_scratch, result_bytes);
    const auto reconciled = apply(state, proof, scratch);
    require(replay.error == ReplayError::none && replay.apply_result == reconciled,
            "reconciliation canonical replay diverged");
    require(reconciled.code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::cancelled &&
                state.reservations[0].state == ReservationState::released &&
                state.risk_accounts[0].reserved_quantity == QuantityLots{} &&
                state.risk_accounts[0].position_quantity == QuantityLots::from_atoms(1),
            "matching final evidence did not close residual accounting");
    require(apply(state, proof, scratch) == reconciled,
            "reconciliation retry did not retain its outcome");
    std::array<std::byte, kMaxStateSnapshotBytes> direct_bytes{}, replay_bytes{};
    const auto direct_snapshot = encode_state_snapshot(state, direct_bytes);
    const auto replay_snapshot = encode_state_snapshot(replayed, replay_bytes);
    require(direct_snapshot.error == StateSnapshotError::none &&
                replay_snapshot.error == StateSnapshotError::none &&
                direct_snapshot.bytes_written == replay_snapshot.bytes_written &&
                std::equal(direct_bytes.begin(), direct_bytes.begin() +
                    static_cast<std::ptrdiff_t>(direct_snapshot.bytes_written), replay_bytes.begin()),
            "reconciliation replay produced different snapshot bytes");
    proof.identity.request_sequence = RequestSequence{4};
    require(apply(state, proof, scratch).code == ApplyCode::already_applied,
            "cross-request final proof retry mutated economics twice");
    auto payload = encode_payload(evidence);
    require(payload.error == PayloadError::none && payload.bytes.size() == 116,
            "V2 reconciliation boundary payload invalid or grew");
    payload.bytes[0] = std::byte{1};
    require(decode_venue_observation(payload.bytes).error == PayloadError::unsupported_schema,
            "V1 observation was silently reinterpreted");
    proof.identity.request_sequence = RequestSequence{5};
    evidence.quantity = QuantityLots::from_atoms(-1);
    proof.identity.payload_digest = observation_fingerprint(evidence);
    const auto invalid = apply(state, proof, scratch);
    require(invalid.code == ApplyCode::invalid_command &&
                invalid.contract_error == ContractError::non_canonical_value,
            "negative final filled quantity was accepted");
    require(encode_kernel_result(invalid, result_bytes).error == KernelCodecError::none,
            "reconciliation refusal could not be serialized");
}

void test_fill_cannot_reuse_an_order_event_identity() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = apply(state, authorize(2, 0xd1), scratch);
    require(apply(state, observe_order_event(1, 0xd2,
        VenueObservationKind::order_acknowledged, submit.cluster_order_id(),
        submit.effect_id(), 1401), scratch).code == ApplyCode::applied,
        "shared identity acknowledgement fixture failed");
    const auto before = state;
    require(apply(state, observe_fill(1, 0xd3, submit.cluster_order_id(),
        submit.effect_id(), ExecutionId{1401}), scratch).code ==
            ApplyCode::venue_event_identity_conflict,
        "fill reused a retained acknowledgement venue/session/sequence");
    require_economics_unchanged(before, state, "shared identity conflict booked a fill");
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(before, bytes);
    require(encoded.error == StateSnapshotError::none, "shared identity snapshot failed");
    TradingState restored;
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                snapshot_scratch).error == StateSnapshotError::none,
            "shared identity restore failed");
    require(apply(restored, observe_fill(1, 0xd3, submit.cluster_order_id(),
        submit.effect_id(), ExecutionId{1401}), scratch).code ==
            ApplyCode::venue_event_identity_conflict,
        "shared identity conflict disappeared after restore");
}

void test_order_event_cannot_reuse_a_fill_identity() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = apply(state, authorize(2, 0xe1), scratch);
    require(apply(state, observe_fill(1, 0xe2, submit.cluster_order_id(),
        submit.effect_id(), ExecutionId{1501}), scratch).code == ApplyCode::applied,
        "reverse shared identity fill fixture failed");
    const auto cancel = apply(state,
        request_cancel(1, 0xe3, submit.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied, "reverse shared identity cancel fixture failed");
    const auto before = state;
    require(apply(state, observe_order_event(1, 0xe4,
        VenueObservationKind::cancelled, submit.cluster_order_id(),
        cancel.effect_id(), 1501), scratch).code == ApplyCode::venue_event_identity_conflict,
        "cancel evidence reused a fill venue/session/sequence and released risk");
    require_economics_unchanged(before, state, "shared identity conflict released risk");
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(before, bytes);
    require(encoded.error == StateSnapshotError::none, "fill identity snapshot failed");
    TradingState restored;
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                snapshot_scratch).error == StateSnapshotError::none,
            "fill identity snapshot restore failed");
    auto event = observe_order_event(1, 0xe4, VenueObservationKind::cancelled,
        submit.cluster_order_id(), cancel.effect_id(), 1501);
    require(apply(restored, event, scratch).code == ApplyCode::venue_event_identity_conflict,
            "fill stream identity disappeared after restore");
    auto other_fill = observe_fill(2, 0xe5, submit.cluster_order_id(),
        submit.effect_id(), ExecutionId{1502});
    auto& observation = std::get<ObserveFill>(other_fill.body).observation;
    observation.venue_sequence = VenueSequence{1501};
    other_fill.identity.payload_digest = observation_fingerprint(observation);
    require(apply(restored, other_fill, scratch).code == ApplyCode::venue_event_identity_conflict,
            "different execution reused one fill stream identity");
    auto& event_observation = std::get<ObserveOrderEvent>(event.body).observation;
    event_observation.venue_session_id = VenueSessionId{3};
    event.identity.request_sequence = RequestSequence{2};
    event.identity.payload_digest = observation_fingerprint(event_observation);
    require(apply(restored, event, scratch).code == ApplyCode::applied,
            "same sequence in a different venue session falsely conflicted");
}

void test_fill_digest_cannot_hide_changed_economics() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = apply(state, authorize(2, 0xb1), scratch);
    const auto first = observe_fill(1, 0xb2, submit.cluster_order_id(),
        submit.effect_id(), ExecutionId{1301}, 1);
    require(apply(state, first, scratch).code == ApplyCode::applied,
            "fingerprint fixture fill failed");
    auto changed = first;
    changed.identity.request_sequence = RequestSequence{2};
    std::get<ObserveFill>(changed.body).observation.quantity = QuantityLots::from_atoms(2);
    const auto before = state;
    const auto outcome = apply(state, changed, scratch);
    require(outcome.code != ApplyCode::already_applied && outcome.code != ApplyCode::applied,
            "copied caller digest hid changed fill quantity as an accepted duplicate");
    require_economics_unchanged(before, state, "changed fill mutated economics");
    require(state.state_version == before.state_version &&
                state.receipt_count == before.receipt_count,
            "bad fingerprint consumed admission state");
    changed.identity.request_sequence = first.identity.request_sequence;
    require(apply(state, changed, scratch).code == ApplyCode::invalid_command,
            "same-request lookup bypassed fingerprint verification");
    auto wrong_wrapper = first;
    wrong_wrapper.body = ObserveOrderEvent{std::get<ObserveFill>(first.body).observation};
    require(apply(state, wrong_wrapper, scratch).code == ApplyCode::invalid_command,
            "wrong observation command wrapper bypassed admission validation");
    changed.identity.request_sequence = RequestSequence{2};
    changed.identity.payload_digest = observation_fingerprint(
        std::get<ObserveFill>(changed.body).observation);
    require(apply(state, changed, scratch).code == ApplyCode::fill_identity_conflict,
            "valid fingerprint concealed conflicting execution content");
    state = ready_state();
    require(apply(state, authorize(2, 0xb1), scratch).code == ApplyCode::applied,
            "event fingerprint submit fixture failed");
    auto event = observe_order_event(1, 0xb3, VenueObservationKind::order_acknowledged,
        submit.cluster_order_id(), submit.effect_id(), 1302);
    const auto accepted_event = apply(state, event, scratch);
    require(accepted_event.code == ApplyCode::applied,
            "event fingerprint fixture failed");
    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    const auto encoded = encode_state_snapshot(state, snapshot);
    require(encoded.error == StateSnapshotError::none, "fingerprint snapshot failed");
    TradingState restored;
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({snapshot.data(), encoded.bytes_written}, restored,
                snapshot_scratch).error == StateSnapshotError::none,
            "fingerprint snapshot restore failed");
    state = restored;
    require(apply(state, event, scratch) == accepted_event,
            "verified request receipt did not survive restore");
    auto retry_event = event;
    retry_event.identity.request_sequence = RequestSequence{9};
    require(apply(state, retry_event, scratch).code == ApplyCode::already_applied,
            "verified venue-event fingerprint did not survive restore");
    auto changed_event = event;
    std::get<ObserveOrderEvent>(changed_event.body).observation.kind =
        VenueObservationKind::effect_unknown;
    require(apply(state, changed_event, scratch).code == ApplyCode::invalid_command,
            "same-request event lookup bypassed fingerprint verification");
    changed_event.identity.request_sequence = RequestSequence{2};
    require(apply(state, changed_event, scratch).code == ApplyCode::invalid_command,
            "cross-request event lookup bypassed fingerprint verification");
    changed_event.identity.payload_digest = observation_fingerprint(
        std::get<ObserveOrderEvent>(changed_event.body).observation);
    require(apply(state, changed_event, scratch).code == ApplyCode::venue_event_identity_conflict,
            "valid fingerprint concealed conflicting venue-event content");
}

void test_cancel_effect_can_be_reconstructed() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = apply(state, authorize(2, 0xa1), scratch);
    const auto cancel = apply(state,
        request_cancel(1, 0xa2, submit.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied, "cancel reconstruction fixture failed");
    require(materialize_order_effect(state, cancel.effect_id()).has_value(),
            "retained cancel effect cannot reconstruct its outbound payload");
    auto expected = *materialize_order_effect(state, submit.effect_id());
    expected.operation = EffectOperation::cancel;
    expected.effect_id = cancel.effect_id();
    expected.committed_directive_index = cancel.state_version;
    expected.executor_generation = cancel.executor_generation;
    expected.kill_epoch = cancel.kill_epoch;
    expected.price = {};
    expected.quantity = {};
    expected.payload_digest = state.outbox_effects[1].payload_digest;
    const auto expected_bytes = encode_payload(expected).bytes;
    require(expected_bytes.size() == kOrderEffectPayloadBytes,
            "expected cancel payload invalid");
    const auto verify = [&] {
        const auto effect = materialize_order_effect(state, cancel.effect_id());
        require(effect && encode_payload(*effect).bytes == expected_bytes,
                "cancel bytes changed with live state");
        std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
        const auto encoded = encode_state_snapshot(state, bytes);
        require(encoded.error == StateSnapshotError::none, "cancel snapshot failed");
        TradingState restored;
        StateSnapshotScratch snapshot_scratch;
        require(decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                    snapshot_scratch).error == StateSnapshotError::none,
                "cancel snapshot restore failed");
        const auto restored_effect = materialize_order_effect(restored, cancel.effect_id());
        require(restored_effect && encode_payload(*restored_effect).bytes == expected_bytes,
                "cancel bytes changed across snapshot restore");
    };
    verify();
    require(apply(state, observe_fill(1, 0xa3, submit.cluster_order_id(),
        submit.effect_id(), ExecutionId{1201}, 1), scratch).code == ApplyCode::applied,
        "cancel reconstruction fill failed");
    require(state.reservations[0].remaining_quantity == QuantityLots::from_atoms(1),
            "cancel normalized quantity incorrectly removed residual risk");
    verify();
    require(apply(state, observe_order_event(1, 0xa4, VenueObservationKind::cancelled,
        submit.cluster_order_id(), cancel.effect_id(), 1202), scratch).code ==
            ApplyCode::applied, "cancel reconstruction completion failed");
    state.executor_generation = ExecutorGeneration{99};
    state.kill_epoch = KillEpoch{98};
    verify();
    expected.quantity = QuantityLots::from_atoms(1);
    require(encode_payload(expected).error == PayloadError::invalid_contract,
            "whole-order cancel accepted a nonzero quantity");
}

void test_outbox_reconstructs_full_immutable_payload() {
    using namespace velocita::v2;
    ApplyScratch scratch;
    auto state = ready_state();
    const auto submit = apply(state, authorize(2, 0x91), scratch);
    // The second effect is a cancel rather than a replace: direct replacement is
    // refused, so a submit/cancel pair is the only way one order accumulates two
    // effects. The test still proves what it was written to prove - that every
    // retained effect reconstructs its exact historical bytes regardless of how
    // live state moves afterwards.
    const auto cancel = apply(state,
        request_cancel(1, 0x92, submit.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied, "outbox fixture cancel failed");
    std::array<std::vector<std::byte>, 2> expected;
    {
        const auto& proposal = state.decision_cold[0].proposal;
        const auto& frame = state.decision_cold[0].frame;
        OrderEffect value;
        value.effect_id = submit.effect_id();
        value.cluster_order_id = submit.cluster_order_id();
        value.committed_directive_index = submit.state_version;
        value.executor_generation = submit.executor_generation;
        value.kill_epoch = submit.kill_epoch;
        value.venue_id = proposal.venue_id;
        value.account_id = proposal.account_id;
        value.instrument_id = proposal.instrument_id;
        value.operation = EffectOperation::submit;
        value.side = proposal.side;
        value.price = proposal.price;
        value.quantity = proposal.quantity;
        value.time_in_force = proposal.time_in_force;
        value.post_only = proposal.post_only;
        value.policy_id = proposal.policy_id;
        value.policy_version = proposal.policy_version;
        value.decision_id = proposal.decision_id;
        value.risk_version = frame.risk_version;
        value.source_generation = frame.source_generation;
        value.source_sequence = frame.source_sequence;
        value.payload_digest = state.outbox_effects[0].payload_digest;
        const auto encoded = encode_payload(value);
        require(encoded.error == PayloadError::none, "expected submit effect invalid");
        expected[0] = encoded.bytes;

        // Whole-order cancel: zero price/quantity, provenance and
        // authorization-time controls retained from the cancel receipt.
        value.effect_id = cancel.effect_id();
        value.committed_directive_index = cancel.state_version;
        value.executor_generation = cancel.executor_generation;
        value.kill_epoch = cancel.kill_epoch;
        value.operation = EffectOperation::cancel;
        value.price = {};
        value.quantity = {};
        value.payload_digest = state.outbox_effects[1].payload_digest;
        const auto encoded_cancel = encode_payload(value);
        require(encoded_cancel.error == PayloadError::none,
                "expected cancel effect invalid");
        expected[1] = encoded_cancel.bytes;
    }
    const auto verify = [&](const TradingState& candidate) {
        for (std::size_t i = 0; i < 2; ++i) {
            const auto effect = materialize_order_effect(candidate,
                candidate.outbox_effects[i].effect_id);
            require(effect.has_value() && encode_payload(*effect).bytes == expected[i],
                    "full historical outbox bytes changed");
        }
        std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
        const auto encoded = encode_state_snapshot(candidate, snapshot);
        require(encoded.error == StateSnapshotError::none,
                "lifecycle snapshot encoding failed");
        TradingState decoded;
        StateSnapshotScratch decode_scratch;
        require(decode_state_snapshot({snapshot.data(), encoded.bytes_written},
                    decoded, decode_scratch).error == StateSnapshotError::none,
                "lifecycle snapshot decoding failed");
        for (std::size_t i = 0; i < 2; ++i) {
            const auto effect = materialize_order_effect(decoded,
                decoded.outbox_effects[i].effect_id);
            require(effect.has_value() && encode_payload(*effect).bytes == expected[i],
                    "lifecycle snapshot changed full outbox bytes");
        }
    };
    verify(state);
    require(apply(state, observe_order_event(2, 0x94,
        VenueObservationKind::effect_unknown, submit.cluster_order_id(),
        cancel.effect_id(), 1100), scratch).code == ApplyCode::applied,
        "outbox fixture unknown observation failed");
    verify(state);
    require(apply(state, observe_order_event(1, 0x93,
        VenueObservationKind::cancelled, submit.cluster_order_id(),
        cancel.effect_id(), 1101), scratch).code == ApplyCode::applied,
        "outbox fixture cancellation failed");
    state.executor_generation = ExecutorGeneration{99};
    state.kill_epoch = KillEpoch{98};
    state.strategy_mode = StrategyMode::halted;
    verify(state);
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(state, bytes);
    require(encoded.error == StateSnapshotError::none, "outbox snapshot failed");
    TradingState restored;
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                snapshot_scratch).error == StateSnapshotError::none,
            "outbox snapshot restore failed");
    verify(restored);
    auto corrupted = restored;
    corrupted.outbox_effects[0].payload_digest = digest(0xee);
    require(!materialize_order_effect(corrupted, submit.effect_id()).has_value(),
            "corrupt outbox digest was accepted");
    corrupted = restored;
    corrupted.decision_cold[0].proposal.post_only =
        corrupted.decision_cold[0].proposal.post_only == PostOnlyMode::required
            ? PostOnlyMode::prohibited : PostOnlyMode::required;
    require(!materialize_order_effect(corrupted, submit.effect_id()).has_value(),
            "changed post-only instruction was accepted");
    corrupted = restored;
    for (std::size_t i = 0; i < corrupted.receipt_count; ++i) {
        if (corrupted.receipts[i].result.effect_id() == submit.effect_id())
            corrupted.receipts[i].result.executor_generation = ExecutorGeneration{77};
    }
    require(!materialize_order_effect(corrupted, submit.effect_id()).has_value(),
            "changed authorization generation was accepted");
    require(!materialize_order_effect(restored, EffectId{0}).has_value(),
            "missing effect identity was accepted");
    restored.receipt_count = 0;
    require(!materialize_order_effect(restored, submit.effect_id()).has_value(),
            "missing authorization receipt reconstructed an effect");
}

}  // namespace

// G-018 route: direct amend/replace is not offered, so no path can hold old and
// new terms at once. The refusal must be typed and must leave every economic
// balance byte-identical, including after snapshot restore.
void test_request_replace_fails_closed_without_economics() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = apply(state, authorize(2, 0xe1), scratch);
    require(submit.code == ApplyCode::applied, "submit fixture did not authorize");
    append_replacement_decision(state, DecisionId{21}, 12, 3);

    const auto before = state;
    const auto command = request_replace(
        1, 0xe2, submit.cluster_order_id(), DecisionId{21});
    const auto refused = apply(state, command, scratch);

    require(refused.code == ApplyCode::replacement_not_supported,
            "RequestReplace did not return the typed unsupported refusal");
    require(state.outbox_effect_count == before.outbox_effect_count &&
                state.order_count == before.order_count &&
                state.reservation_count == before.reservation_count &&
                state.risk_accounts[0] == before.risk_accounts[0] &&
                state.reservations[0] == before.reservations[0] &&
                state.orders[0] == before.orders[0],
            "refused replacement mutated economics");
    require(state.orders[0].state == ClusterOrderState::dispatch_authorized,
            "refused replacement moved the order out of its authorized state");

    // The refusal must survive a canonical snapshot round trip unchanged: a
    // refused command must not be a state the snapshot cannot express.
    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    const auto encoded = encode_state_snapshot(state, snapshot);
    require(encoded.error == StateSnapshotError::none,
            "state after refused replacement failed snapshot invariants");
    TradingState restored;
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({snapshot.data(), encoded.bytes_written},
                                  restored, snapshot_scratch)
                    .error == StateSnapshotError::none,
            "refused-replacement snapshot did not decode");
    const auto repeated = apply(restored, command, scratch);
    require(repeated.code == ApplyCode::replacement_not_supported &&
                restored.outbox_effect_count == before.outbox_effect_count,
            "replacement refusal did not hold after snapshot restore");
}

// The other half of G-018. Deferring RequestReplace is not sufficient: a fresh
// authorization on an occupied logical slot reaches the same double-reserved
// state through cancel-then-submit, because ClusterOrderId is derived from the
// varying DecisionId and therefore never collides.
void test_fresh_submit_cannot_bypass_unresolved_cancelled_slot() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = apply(state, authorize(2, 0xf1), scratch);
    require(submit.code == ApplyCode::applied, "submit fixture did not authorize");

    const auto fill = apply(
        state,
        observe_fill(1, 0xf2, submit.cluster_order_id(), submit.effect_id(),
                     ExecutionId{801}),
        scratch);
    require(fill.code == ApplyCode::applied, "partial fill fixture failed");

    const auto cancel = apply(
        state, request_cancel(1, 0xf3, submit.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::cancel_pending &&
                state.reservations[0].state != ReservationState::released,
            "cancel fixture did not leave a risk-bearing cancel-pending order");
    require(apply(state,
                  observe_order_event(1, 0xf4, VenueObservationKind::cancelled,
                                      submit.cluster_order_id(),
                                      cancel.effect_id(), 902),
                  scratch)
                    .code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::reconciling &&
                state.reservations[0].state != ReservationState::released,
            "venue cancellation released risk before final evidence arrived");

    append_submit_decision(state, DecisionId{22}, 11, 2);
    const auto occupied = state;
    const auto blocked = apply(state, authorize_decision(3, 0xf5, DecisionId{22}),
                               scratch);

    require(blocked.code == ApplyCode::order_slot_occupied,
            "a fresh submit was admitted while the slot held unresolved risk");
    require(state.order_count == occupied.order_count &&
                state.reservation_count == occupied.reservation_count &&
                state.outbox_effect_count == occupied.outbox_effect_count &&
                state.risk_accounts[0] == occupied.risk_accounts[0] &&
                state.reservations[0] == occupied.reservations[0],
            "refused slot reuse mutated economics");

    // Final reconciliation proves the cumulative filled quantity and releases the
    // residual. Only then may the slot be reused. This sequencing is what G-018
    // requires: the old order's risk is gone before the new order's risk exists.
    auto proof = observe_order_event(2, 0xf6,
                                     VenueObservationKind::cancel_reconciled,
                                     submit.cluster_order_id(),
                                     cancel.effect_id(), 903);
    auto& evidence = std::get<ObserveOrderEvent>(proof.body).observation;
    evidence.venue_order_id = VenueOrderId{70};
    evidence.quantity = state.reservations[0].filled_quantity;
    proof.identity.payload_digest = observation_fingerprint(evidence);
    require(apply(state, proof, scratch).code == ApplyCode::applied &&
                state.reservations[0].state == ReservationState::released,
            "final cancel reconciliation did not release the residual hold");

    const auto admitted = apply(state, authorize_decision(4, 0xf7, DecisionId{22}),
                                scratch);
    require(admitted.code == ApplyCode::applied &&
                state.order_count == occupied.order_count + 1,
            "slot was not reusable after its residual risk was resolved");
}

// ---------------------------------------------------------------------------
// B2: bounded arenas must survive continuous requoting.
//
// Every completed requote cycle consumes one decision record, one cluster order,
// one reservation, and one outbox effect. All four arenas hold 16. Without
// reclamation the kernel therefore stops authorizing after 16 lifetime cycles on
// a slot, whatever the market is doing - and slot exclusivity guarantees each
// requote consumes a fresh set.
//
// A cycle here is the real one: authorize, fill completely, reservation released,
// slot free. Nothing is left referenced afterwards, so nothing in these records
// can be needed again.
// ---------------------------------------------------------------------------
void test_completed_requote_cycles_do_not_exhaust_bounded_arenas() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;

    std::uint64_t request = 2;
    std::uint64_t execution = 3'000;
    // The Phase 3 fixture already retained DecisionId{20}; drive that one first,
    // then keep requoting well past every arena bound.
    for (std::uint64_t cycle = 0; cycle < 20; ++cycle) {
        const DecisionId decision{20 + cycle};
        if (cycle > 0) {
            require(evaluate_new_decision(state, scratch, decision, cycle + 1,
                                          request++)
                            .code == ApplyCode::applied,
                    "requote cycle could not retain a decision: "
                    "bounded decision arena was not reclaimed");
        }
        const auto submit = apply(
            state, authorize_decision(request++, 0xb0, decision), scratch);
        require(submit.code == ApplyCode::applied,
                "requote cycle could not authorize: bounded arena was not reclaimed");
        const auto fill = apply(
            state,
            observe_fill(request++, 0xb1, submit.cluster_order_id(),
                         submit.effect_id(), ExecutionId{execution++}, 2),
            scratch);
        require(fill.code == ApplyCode::applied, "requote cycle fill failed");
        require(state.reservations[state.reservation_count - 1].state ==
                    ReservationState::released,
                "full fill did not release the reservation");
    }

    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    require(encode_state_snapshot(state, snapshot).error == StateSnapshotError::none,
            "state after arena reclamation failed snapshot invariants");
}

// The ordering hazard. A terminal order whose effect is still unresolved must
// survive the sweep, because materialize_order_effect needs its decision record
// and authorization receipt to reproduce the exact bytes already sent.
// Reclaiming it would leave a pending external effect whose payload can never be
// reconstructed, breaking G-020 and stranding the gateway.
void test_sweep_preserves_records_an_unresolved_effect_still_needs() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;

    // The realistic shape: a cancel is in flight when the fill lands. The ORDER
    // resolves - filled, reservation released, slot free - while its CANCEL
    // effect is still pending, so the gateway must remain able to reproduce the
    // exact bytes it sent for that cancel.
    //
    // Note a fill cannot leave a SUBMIT effect unknown: being filled is proof the
    // order existed at the venue, and the kernel correctly resolves it.
    const auto stranded = apply(state, authorize_decision(2, 0xc0, DecisionId{20}),
                                scratch);
    require(stranded.code == ApplyCode::applied, "stranded fixture failed");
    require(apply(state,
                  observe_order_event(1, 0xc1,
                                      VenueObservationKind::order_acknowledged,
                                      stranded.cluster_order_id(),
                                      stranded.effect_id(), 1500),
                  scratch)
                    .code == ApplyCode::applied,
            "stranded fixture acknowledgement failed");
    const auto stranded_cancel = apply(
        state, request_cancel(1, 0xc2, stranded.cluster_order_id()), scratch);
    require(stranded_cancel.code == ApplyCode::applied, "stranded cancel failed");
    require(apply(state,
                  observe_fill(3, 0xc3, stranded.cluster_order_id(),
                               stranded.effect_id(), ExecutionId{4'900}, 2),
                  scratch)
                    .code == ApplyCode::applied,
            "stranded fixture fill failed");
    require(state.orders[0].state == ClusterOrderState::filled &&
                state.reservations[0].state == ReservationState::released &&
                state.outbox_effects[1].state == OutboxState::pending,
            "stranded fixture did not reach a filled order with a pending cancel");
    const auto stranded_bytes =
        encode_payload(*materialize_order_effect(state, stranded_cancel.effect_id()))
            .bytes;

    // The slot is free because the reservation released, so requoting continues
    // on it and drives every arena past its bound.
    std::uint64_t request = 4;
    std::uint64_t execution = 5'000;
    for (std::uint64_t cycle = 1; cycle < 20; ++cycle) {
        const DecisionId decision{20 + cycle};
        require(evaluate_new_decision(state, scratch, decision, cycle + 1, request++)
                        .code == ApplyCode::applied,
                "requote around a stranded effect could not retain a decision");
        const auto submit = apply(
            state, authorize_decision(request++, 0xc3, decision), scratch);
        require(submit.code == ApplyCode::applied,
                "requote around a stranded effect could not authorize");
        for (std::size_t index = 0; index < state.order_count; ++index) {
            const auto& order = state.orders[index];
            require(order.reservation_index < state.reservation_count &&
                        state.reservations[order.reservation_index].cluster_order_id ==
                            order.cluster_order_id,
                    "compaction broke order/reservation pairing mid-cycle");
        }
        require(apply(state,
                      observe_fill(request++, 0xc4, submit.cluster_order_id(),
                                   submit.effect_id(), ExecutionId{execution++}, 2),
                      scratch)
                        .code == ApplyCode::applied,
                "requote around a stranded effect failed to fill");
    }

    const auto survivor = materialize_order_effect(state, stranded_cancel.effect_id());
    require(survivor.has_value() && encode_payload(*survivor).bytes == stranded_bytes,
            "sweep reclaimed records an unresolved effect still needed");
}

// conflict is terminal but NOT resolved: internal state and venue evidence
// disagree, so the exposure is of unknown size and its hold must stand. Nothing
// sets this state yet - B4 reconciliation will - so it is constructed directly
// to pin the sweep invariant before its producer exists.
//
// A conflicted order keeps its reservation, so its slot stays occupied and no
// further orders can be created on it. The decision arena is therefore filled
// instead, which is enough to trigger a sweep and prove it declines to sacrifice
// the conflict for capacity.
void test_conflicted_orders_are_never_swept() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;

    const auto conflicted = apply(state, authorize_decision(2, 0xd0, DecisionId{20}),
                                  scratch);
    require(conflicted.code == ApplyCode::applied, "conflict fixture failed");
    state.orders[0].state = ClusterOrderState::conflict;
    const auto held_quantity = state.risk_accounts[0].reserved_quantity;
    const auto held_margin = state.risk_accounts[0].reserved_margin;
    const auto effect_bytes =
        encode_payload(*materialize_order_effect(state, conflicted.effect_id())).bytes;

    bool refused = false;
    std::uint64_t request = 3;
    for (std::uint64_t cycle = 1; cycle < 40 && !refused; ++cycle) {
        const auto evaluated = evaluate_new_decision(
            state, scratch, DecisionId{20 + cycle}, cycle + 1, request++);
        if (evaluated.code == ApplyCode::decision_capacity_exceeded) refused = true;
    }
    require(refused,
            "decision arena never refused, so no sweep was triggered around the conflict");

    require(state.order_count >= 1 &&
                state.orders[0].cluster_order_id == conflicted.cluster_order_id() &&
                state.orders[0].state == ClusterOrderState::conflict,
            "a conflicted order was reclaimed by the sweep");
    require(state.risk_accounts[0].reserved_quantity == held_quantity &&
                state.risk_accounts[0].reserved_margin == held_margin,
            "reclaiming released risk still held by a conflicted order");
    const auto retained = materialize_order_effect(state, conflicted.effect_id());
    require(retained.has_value() && encode_payload(*retained).bytes == effect_bytes,
            "conflicted order lost the bytes an operator needs to resolve it");
}

// Reservations are addressed by ARRAY INDEX from ClusterOrder. Dense compaction
// that moves reservations without repairing those indices would silently point
// an order at another order's risk - no compile error, no validation failure.
void test_sweep_preserves_reservation_index_pairing() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;

    std::uint64_t request = 2;
    std::uint64_t execution = 6'000;
    for (std::uint64_t cycle = 0; cycle < 20; ++cycle) {
        const DecisionId decision{20 + cycle};
        if (cycle > 0) {
            require(evaluate_new_decision(state, scratch, decision, cycle + 1,
                                          request++)
                            .code == ApplyCode::applied,
                    "pairing fixture could not retain a decision");
        }
        const auto submit = apply(
            state, authorize_decision(request++, 0xe0, decision), scratch);
        require(submit.code == ApplyCode::applied, "pairing fixture authorize failed");
        require(apply(state,
                      observe_fill(request++, 0xe1, submit.cluster_order_id(),
                                   submit.effect_id(), ExecutionId{execution++}, 2),
                      scratch)
                        .code == ApplyCode::applied,
                "pairing fixture fill failed");
    }

    for (std::size_t index = 0; index < state.order_count; ++index) {
        const auto& order = state.orders[index];
        require(order.reservation_index < state.reservation_count,
                "compaction left an order pointing outside the reservation arena");
        require(state.reservations[order.reservation_index].cluster_order_id ==
                    order.cluster_order_id,
                "compaction repointed an order at another order's reservation");
    }
}

int main() {
    try {
        test_completed_requote_cycles_do_not_exhaust_bounded_arenas();
        test_sweep_preserves_records_an_unresolved_effect_still_needs();
        test_conflicted_orders_are_never_swept();
        test_sweep_preserves_reservation_index_pairing();
        test_request_replace_fails_closed_without_economics();
        test_fresh_submit_cannot_bypass_unresolved_cancelled_slot();
        test_cancel_fill_final_evidence_permutations();
        test_cancel_before_earlier_fill_does_not_lose_fill_evidence();
        test_fill_cannot_reuse_an_order_event_identity();
        test_order_event_cannot_reuse_a_fill_identity();
        test_fill_digest_cannot_hide_changed_economics();
        test_cancel_effect_can_be_reconstructed();
        test_outbox_reconstructs_full_immutable_payload();
        test_delayed_observations_cannot_regress_order_knowledge();
        test_authorization_rechecks_current_controls();
        test_decision_retry_retains_original_authorization_after_fill();
        test_submit_authorization_is_atomic_and_idempotent();
        test_risk_and_capacity_fail_before_economic_mutation();
        test_non_authoritative_decisions_cannot_create_economics();
        test_partial_fills_conserve_risk_position_and_identity();
        test_fill_replay_and_snapshot_are_byte_identical();
        test_fill_failures_are_atomic_and_bounded();
        test_cancel_and_unknown_remain_risk_bearing_until_evidence();
        test_fill_cancel_race_consumes_one_reservation();
        std::cout << "all V2 risk/order/outbox tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 risk/order/outbox test failed: " << error.what() << '\n';
        return 1;
    }
}
