#include "v2_catalog_test_support.hpp"
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
        velocita::v2::MarketDataStatus::ready,
    const velocita::v2::KillEpoch kill_epoch = velocita::v2::KillEpoch{2},
    const velocita::v2::ExecutorGeneration executor_generation =
        velocita::v2::ExecutorGeneration{3}) {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    state.strategy_mode = StrategyMode::normal;
    state.kill_epoch = kill_epoch;
    state.executor_generation = executor_generation;
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
         VenueCapabilitiesVersion{8}, (kVenueDirectivePlaceLimit | kVenueDirectiveCancel), kVenueTifGtc,
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
        MoneyMicros::from_atoms(8'000), {}, {}, 1'000'000, 2'500, {},
        QuantityLots::from_atoms(1'000), true};

    auto decision_frame = frame();
    decision_frame.features[0] = feature;
    decision_frame.market_data_status = market_status;
    ApplyScratch scratch;
    const Command decision{{CallerId{1}, RequestSequence{1}, digest(0x51)},
                           EvaluateDecisionFrame{DecisionId{20}, mode,
                                                 decision_frame}};
    const auto result = catalog_test::apply(state, decision, scratch);
    require(result.code == ApplyCode::applied,
            "Phase 3 fixture command was not retained");
    return state;
}


// A genuinely two-sided fixture: a bid on logical slot 1 and an ask on slot 2.
//
// Slot exclusivity only earns its keep if the two sides never contend, and every
// other fixture here quotes a single slot, so that has never actually been shown.
// Two ordered rules select two templates: first-match sends a rich feature to the
// buy template and anything above the floor to the sell template, which is the
// simplest arrangement that exercises both slots from one policy without a second
// policy artifact.
//
// Prices are kept small deliberately. The shared instrument profile caps price at
// 100 ticks and per-order gross notional at 4,000, and notional is price * qty *
// 100, so a two-lot order above 20 ticks would be refused for reasons unrelated
// to slotting.
velocita::v2::PolicyArtifact two_sided_policy() {
    using namespace velocita::v2;
    auto value = policy();
    value.rule_count = 2;
    value.rules[0] = {0, PredicateOperator::greater_than_or_equal,
                      FeatureMicros::from_atoms(15), Directive::place_limit};
    value.rules[1] = {0, PredicateOperator::greater_than,
                      FeatureMicros::from_atoms(5), Directive::place_limit};
    value.artifact_digest = digest(0x12);
    return value;
}

velocita::v2::ExecutablePolicyArtifact two_sided_executable_policy(
    const velocita::v2::FeatureSchemaArtifact& schema) {
    using namespace velocita::v2;
    auto value = executable_policy(schema);
    value.policy_artifact_digest = two_sided_policy().artifact_digest;
    value.template_count = 2;
    value.templates[0] = action_template();
    value.templates[1] = action_template();
    value.templates[1].side = Side::sell;
    value.templates[1].order_slot = LogicalOrderSlot{2};
    value.rule_binding_count = 2;
    value.rule_template_slots[0] = ActionTemplateSlot{1};
    value.rule_template_slots[1] = ActionTemplateSlot{2};
    value.artifact_digest = compute_artifact_digest(value).digest;
    return value;
}

velocita::v2::TradingState two_sided_state() {
    using namespace velocita::v2;
    auto state = ready_state();
    state.policies[0] = {two_sided_policy(), PolicyLifecycle::active};
    const auto schema = feature_schema();
    state.active_executable_policy.executable_policy =
        two_sided_executable_policy(schema);
    // ready_state() evaluates DecisionId{20} before this swap, so that decision
    // was resolved against the single-rule policy and its proposal targets slot 1
    // regardless of the two-sided bindings. It is deliberately left unused: a
    // caller must evaluate its own decisions AFTER this function returns, or it
    // will authorize a proposal that predates the policy it thinks it is testing.
    return state;
}

// Evaluate a decision at an explicit feature value, so a caller can steer
// first-match rule selection and therefore which slot the proposal targets.
velocita::v2::ApplyResult evaluate_at_feature(
    velocita::v2::TradingState& state,
    velocita::v2::ApplyScratch& scratch,
    const velocita::v2::DecisionId decision_id,
    const std::int64_t feature_atoms,
    const std::uint64_t source_sequence,
    const std::uint64_t request) {
    using namespace velocita::v2;
    const auto evidence = digest(static_cast<std::uint8_t>(0x40 + (source_sequence % 0x30)));
    const auto admitted = catalog_test::apply(state,
                                {{CallerId{7}, RequestSequence{request}, digest(0x21)},
                                 RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                                   SourceSequence{source_sequence},
                                                   evidence}},
                                scratch);
    require(admitted.code == ApplyCode::applied,
            "source event admission failed");









    auto value = frame();
    value.venue_sequence = VenueSequence{source_sequence};
    value.source_sequence = SourceSequence{source_sequence};
    value.raw_evidence_digest = evidence;
    value.features[0] = FeatureMicros::from_atoms(feature_atoms);
    value.position_version = state.risk_accounts[0].position_version;
    value.risk_version = state.risk_accounts[0].risk_version;
    return catalog_test::apply(state,
                 {{CallerId{8}, RequestSequence{request},
                   digest(static_cast<std::uint8_t>(1 + decision_id.value() % 255))},
                  EvaluateDecisionFrame{decision_id, EvaluationMode::authoritative,
                                        value}},
                 scratch);
}

velocita::v2::Command authorize(const std::uint64_t request,
                                const std::uint8_t marker) {
    using namespace velocita::v2;
    return {{CallerId{1}, RequestSequence{request}, digest(marker)},
            AuthorizeDecision{DecisionId{20}}};
}

void test_authorization_requires_initialized_execution_authority() {
    using namespace velocita::v2;
    ApplyScratch scratch;
    const std::array<std::pair<KillEpoch, ExecutorGeneration>, 3> incomplete{{
        {KillEpoch{}, ExecutorGeneration{}},
        {KillEpoch{}, ExecutorGeneration{3}},
        {KillEpoch{2}, ExecutorGeneration{}},
    }};
    for (std::size_t index = 0; index < incomplete.size(); ++index) {
        auto state = ready_state(EvaluationMode::authoritative,
                                 FeatureMicros::from_atoms(11),
                                 MarketDataStatus::ready,
                                 incomplete[index].first,
                                 incomplete[index].second);
        const auto before = state;

        const auto refused = catalog_test::apply(
            state, authorize(2 + index, static_cast<std::uint8_t>(0x52 + index)),
            scratch);

        require(refused.code == ApplyCode::stale_control_generation,
                "incomplete execution authority authorized an outbound effect");
        require(state.reservation_count == before.reservation_count &&
                    state.order_count == before.order_count &&
                    state.outbox_effect_count == before.outbox_effect_count &&
                    state.risk_accounts[0].reserved_quantity ==
                        before.risk_accounts[0].reserved_quantity &&
                    state.risk_accounts[0].reserved_margin ==
                        before.risk_accounts[0].reserved_margin &&
                    state.risk_accounts[0].reserved_fee ==
                        before.risk_accounts[0].reserved_fee,
                "incomplete-authority refusal partially created risk or outbox state");
    }
}

velocita::v2::Command observe_fill(
    const std::uint64_t request,
    const std::uint8_t marker,
    const velocita::v2::ClusterOrderId order_id,
    const velocita::v2::EffectId effect_id,
    const velocita::v2::ExecutionId execution_id,
    const std::int64_t quantity = 1,
    const velocita::v2::Side side = velocita::v2::Side::buy,
    const std::int64_t price = 11) {
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
    // Side and price must match the resolved proposal: a fill whose side
    // disagrees is refused as not applicable, which is correct but reads as a
    // mysterious fixture failure on a two-sided book.
    observation.side = side;
    observation.price = PriceTicks::from_atoms(price);
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
        kind == VenueObservationKind::cancelled ||
        kind == VenueObservationKind::order_reconciled) {
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
        decision_id, digest(static_cast<std::uint8_t>(1 + decision_id.value() % 255)),
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
    const std::uint64_t request,
    std::vector<velocita::v2::Command>* history = nullptr) {
    using namespace velocita::v2;
    const auto evidence = digest(static_cast<std::uint8_t>(0x40 + (source_sequence % 0x30)));
    const Command record{{CallerId{7}, RequestSequence{request}, digest(0x21)},
                         RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                           SourceSequence{source_sequence}, evidence}};
    const auto admitted = catalog_test::apply(state, record, scratch);
    require(admitted.code == ApplyCode::applied,
            "source event admission failed");
    if (history != nullptr) history->push_back(record);

    auto value = frame();
    value.venue_sequence = VenueSequence{source_sequence};
    value.source_sequence = SourceSequence{source_sequence};
    value.raw_evidence_digest = evidence;
    value.position_version = state.risk_accounts[0].position_version;
    value.risk_version = state.risk_accounts[0].risk_version;
    const Command evaluate{{CallerId{8}, RequestSequence{request},
                            digest(static_cast<std::uint8_t>(1 + decision_id.value() % 255))},
                           EvaluateDecisionFrame{decision_id,
                                                 EvaluationMode::authoritative, value}};
    if (history != nullptr) history->push_back(evaluate);
    return catalog_test::apply(state, evaluate, scratch);
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
        decision_id, digest(static_cast<std::uint8_t>(1 + decision_id.value() % 255)),
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
    const auto outcome = catalog_test::apply(state, command, scratch);

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

    const auto replay = catalog_test::apply(state, command, scratch);
    require(replay == outcome && state.reservation_count == 1 &&
                state.order_count == 1 && state.outbox_effect_count == 1,
            "request replay changed economics or lost retained identities");

    const auto conflict = catalog_test::apply(state, authorize(2, 0x53), scratch);
    require(conflict.code == ApplyCode::duplicate_conflict &&
                state.reservation_count == 1 && state.order_count == 1 &&
                state.outbox_effect_count == 1,
            "same request identity with different payload was not a conflict");
}

void test_risk_and_capacity_fail_before_economic_mutation() {
    using namespace velocita::v2;
    ApplyScratch scratch;

    auto insufficient = ready_state();
    insufficient.risk_accounts[0].max_reserved_exposure =
        MoneyMicros::from_atoms(2'205);
    const auto insufficient_before = insufficient;
    require(catalog_test::apply(insufficient, authorize(2, 0x61), scratch).code ==
                ApplyCode::insufficient_risk_capacity,
            "insufficient risk was not rejected");
    require_economics_unchanged(insufficient_before, insufficient,
                                "insufficient risk partially mutated economics");

    auto stale = ready_state();
    stale.risk_accounts[0].risk_version = RiskVersion{2};
    const auto stale_before = stale;
    require(catalog_test::apply(stale, authorize(2, 0x62), scratch).code ==
                ApplyCode::stale_risk_state,
            "stale risk version was not rejected");
    require_economics_unchanged(stale_before, stale,
                                "stale risk partially mutated economics");

    auto full = ready_state();
    full.outbox_effect_count = kMaxOutboxEffects;
    const auto full_before = full;
    require(catalog_test::apply(full, authorize(2, 0x63), scratch).code ==
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
    require(catalog_test::apply(shadow, authorize(2, 0x71), scratch).code ==
                ApplyCode::decision_not_authorizable,
            "shadow decision entered authorization");
    require_economics_unchanged(shadow_before, shadow,
                                "shadow decision mutated economics");

    auto no_action = ready_state(EvaluationMode::authoritative,
                                 FeatureMicros::from_atoms(10));
    const auto no_action_before = no_action;
    require(catalog_test::apply(no_action, authorize(2, 0x72), scratch).code ==
                ApplyCode::decision_not_authorizable,
            "no-action decision entered authorization");
    require_economics_unchanged(no_action_before, no_action,
                                "no-action decision mutated economics");

    auto rejected = ready_state(EvaluationMode::authoritative,
                                FeatureMicros::from_atoms(11),
                                MarketDataStatus::stale);
    const auto rejected_before = rejected;
    require(catalog_test::apply(rejected, authorize(2, 0x73), scratch).code ==
                ApplyCode::decision_not_authorizable,
            "rejected decision entered authorization");
    require_economics_unchanged(rejected_before, rejected,
                                "rejected decision mutated economics");
}

void test_partial_fills_conserve_risk_position_and_identity() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto authorization = catalog_test::apply(state, authorize(2, 0x81), scratch);
    require(authorization.code == ApplyCode::applied,
            "fill fixture was not authorized");

    const auto first_command = observe_fill(
        1, 0x82, authorization.cluster_order_id(), authorization.effect_id(),
        ExecutionId{101});
    const auto first = catalog_test::apply(state, first_command, scratch);
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
    require(catalog_test::apply(state, first_command, scratch) == first &&
                state.risk_accounts[0] == after_first.risk_accounts[0] &&
                state.reservations[0] == after_first.reservations[0] &&
                state.orders[0] == after_first.orders[0] &&
                state.outbox_effects[0] == after_first.outbox_effects[0],
            "same request replay applied a partial fill twice");

    const auto duplicate = catalog_test::apply(
        state,
        observe_fill(2, 0x82, authorization.cluster_order_id(),
                     authorization.effect_id(), ExecutionId{101}),
        scratch);
    require(duplicate.code == ApplyCode::already_applied &&
                state.risk_accounts[0].position_quantity ==
                    QuantityLots::from_atoms(1),
            "same execution under a new request duplicated economics");

    const auto conflict_before = state;
    const auto conflict = catalog_test::apply(
        state,
        observe_fill(3, 0x83, authorization.cluster_order_id(),
                     authorization.effect_id(), ExecutionId{101}, 2),
        scratch);
    require(conflict.code == ApplyCode::fill_identity_conflict &&
                state.risk_accounts[0] ==
                    conflict_before.risk_accounts[0] &&
                state.reservations[0] == conflict_before.reservations[0],
            "conflicting duplicate execution changed economics");

    const auto terminal = catalog_test::apply(
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
    const auto authorization = catalog_test::apply(direct, authorize(2, 0x91), direct_scratch);
    const auto fill = observe_fill(
        1, 0x92, authorization.cluster_order_id(), authorization.effect_id(),
        ExecutionId{201});
    auto replayed = direct;

    std::array<std::byte, kMaxKernelCommandBytes> command_bytes{};
    const auto encoded_command = encode_kernel_command(fill, command_bytes);
    const auto direct_result = catalog_test::apply(direct, fill, direct_scratch);
    ApplyScratch replay_scratch;
    KernelCommandCodecScratch codec_scratch;
    std::array<std::byte, kKernelResultBytes> result_bytes{};
    const auto replay_result = catalog_test::replay(
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

    TradingState restored = catalog_test::state();
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
    const auto overfill_auth = catalog_test::apply(overfill, authorize(2, 0xa1), scratch);
    const auto overfill_before = overfill;
    require(catalog_test::apply(overfill,
                  observe_fill(1, 0xa2, overfill_auth.cluster_order_id(),
                               overfill_auth.effect_id(), ExecutionId{301}, 3),
                  scratch)
                    .code == ApplyCode::fill_quantity_conflict,
            "fill above remaining quantity was accepted");
    require_economics_unchanged(overfill_before, overfill,
                                "overfill partially mutated economics");

    auto overflow = ready_state();
    const auto overflow_auth = catalog_test::apply(overflow, authorize(2, 0xa3), scratch);
    overflow.risk_accounts[0].position_quantity = QuantityLots::from_atoms(
        std::numeric_limits<std::int64_t>::max());
    const auto overflow_before = overflow;
    require(catalog_test::apply(overflow,
                  observe_fill(1, 0xa4, overflow_auth.cluster_order_id(),
                               overflow_auth.effect_id(), ExecutionId{302}),
                  scratch)
                    .code == ApplyCode::position_arithmetic_overflow,
            "overflowing position update was accepted");
    require_economics_unchanged(overflow_before, overflow,
                                "position overflow partially mutated economics");

    auto full = ready_state();
    const auto full_auth = catalog_test::apply(full, authorize(2, 0xa5), scratch);
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
    require(catalog_test::apply(full,
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
    const auto submit = catalog_test::apply(state, authorize(2, 0xc1), scratch);
    const auto risk_before_cancel = state.risk_accounts[0];

    const auto cancel = catalog_test::apply(
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

    const auto unknown = catalog_test::apply(
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
    const auto cancelled_submit = catalog_test::apply(cancelled, authorize(2, 0xc4), scratch);
    const auto cancelled_intent = catalog_test::apply(
        cancelled, request_cancel(1, 0xc5, cancelled_submit.cluster_order_id()),
        scratch);
    const auto accepted_cancel = catalog_test::apply(
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
    const auto submit = catalog_test::apply(state, authorize(2, 0xd1), scratch);
    require(catalog_test::apply(state,
                  observe_fill(1, 0xd2, submit.cluster_order_id(),
                               submit.effect_id(), ExecutionId{501}),
                  scratch)
                    .code == ApplyCode::applied,
            "race fixture partial fill failed");
    const auto cancel = catalog_test::apply(
        state, request_cancel(1, 0xd3, submit.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::cancel_pending &&
                state.reservations[0].remaining_quantity ==
                    QuantityLots::from_atoms(1),
            "cancel race did not preserve residual risk");

    require(catalog_test::apply(state,
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

    require(catalog_test::apply(state,
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
    const auto submit = catalog_test::apply(filled, authorize(2, 0x41), scratch);
    require(catalog_test::apply(filled, observe_fill(1, 0x42, submit.cluster_order_id(),
                                      submit.effect_id(), ExecutionId{801}, 2),
                  scratch).code == ApplyCode::applied,
            "terminal observation fixture failed");
    for (const auto kind : {VenueObservationKind::order_acknowledged,
                            VenueObservationKind::effect_unknown,
                            VenueObservationKind::order_rejected}) {
        auto candidate = filled;
        const auto result = catalog_test::apply(candidate, observe_order_event(
            1, 0x43, kind, submit.cluster_order_id(), submit.effect_id(), 802), scratch);
        require(result.code == ApplyCode::venue_observation_not_applicable &&
                    candidate.orders[0] == filled.orders[0] &&
                    candidate.outbox_effects[0] == filled.outbox_effects[0] &&
                    candidate.reservations[0] == filled.reservations[0] &&
                    candidate.risk_accounts[0] == filled.risk_accounts[0],
                "delayed observation regressed a filled order or its evidence");
    }

    auto partial = ready_state();
    const auto partial_submit = catalog_test::apply(partial, authorize(2, 0x44), scratch);
    require(catalog_test::apply(partial, observe_fill(1, 0x45, partial_submit.cluster_order_id(),
                                       partial_submit.effect_id(), ExecutionId{803}),
                  scratch).code == ApplyCode::applied, "partial fixture failed");
    const auto before = partial;
    static_cast<void>(catalog_test::apply(partial, observe_order_event(
        1, 0x46, VenueObservationKind::order_acknowledged,
        partial_submit.cluster_order_id(), partial_submit.effect_id(), 804), scratch));
    require(partial.orders[0] == before.orders[0] &&
                partial.reservations[0] == before.reservations[0],
            "late submit ack erased partial-fill knowledge");

    auto cancelling = ready_state();
    const auto cancel_submit = catalog_test::apply(cancelling, authorize(2, 0x47), scratch);
    require(catalog_test::apply(cancelling, request_cancel(1, 0x48,
                                            cancel_submit.cluster_order_id()),
                  scratch).code == ApplyCode::applied, "cancel fixture failed");
    const auto cancel_before = cancelling;
    static_cast<void>(catalog_test::apply(cancelling, observe_order_event(
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
        require(catalog_test::apply(state, authorize(2, 0x61), scratch).code ==
                    ApplyCode::stale_control_generation,
                "old actionable decision bypassed current risk controls");
        require_economics_unchanged(before, state, "control refusal mutated economics");
    }
    auto stale = ready_state();
    stale.kill_epoch = KillEpoch{stale.kill_epoch.value() + 1};
    require(catalog_test::apply(stale, authorize(2, 0x62), scratch).code ==
                ApplyCode::stale_control_generation,
            "decision from a previous kill epoch was authorized");

    // Replacement previously had to recheck controls because it could create an
    // effect. It now refuses before reaching any control check, which is a
    // strictly stronger property: no control state permits it. Asserting the
    // unconditional refusal keeps the original intent - stale controls cannot
    // slip a replacement through - without asserting a check that no longer runs.
    auto replacement = ready_state();
    const auto submit = catalog_test::apply(replacement, authorize(2, 0x63), scratch);
    append_replacement_decision(replacement, DecisionId{21}, 12, 3);
    replacement.executor_generation = ExecutorGeneration{
        replacement.executor_generation.value() + 1};
    const auto before = replacement;
    require(catalog_test::apply(replacement, request_replace(1, 0x64,
                  submit.cluster_order_id(), DecisionId{21}), scratch).code ==
                ApplyCode::replacement_not_supported,
            "replacement was not refused under a changed executor generation");
    require_economics_unchanged(before, replacement,
                                "refused replacement mutated economics");
}

void test_authorization_requires_current_latest_intent() {
    using namespace velocita::v2;
    ApplyScratch scratch;

    auto superseded_slot = ready_state();
    append_submit_decision(superseded_slot, DecisionId{21}, 12, 2);
    const auto superseded_before = superseded_slot;
    const auto superseded = catalog_test::apply(
        superseded_slot, authorize_decision(2, 0x65, DecisionId{20}), scratch);
    require(superseded.code == ApplyCode::decision_not_authorizable,
            "an older same-slot proposal remained authorizable after a newer intent");
    require_economics_unchanged(
        superseded_before, superseded_slot,
        "refusing a superseded same-slot proposal mutated economics");

    auto retired_policy = ready_state();
    const Command supersede{
        {CallerId{6}, RequestSequence{1}, digest(0x66)},
        SupersedePolicy{PolicyId{10}, PolicyVersion{1}}};
    require(catalog_test::apply(retired_policy, supersede, scratch).code == ApplyCode::applied,
            "policy-retirement fixture failed");
    const auto retired_before = retired_policy;
    const auto retired = catalog_test::apply(
        retired_policy, authorize_decision(2, 0x67, DecisionId{20}), scratch);
    require(retired.code == ApplyCode::decision_not_authorizable,
            "a decision outlived the active executable policy that produced it");
    require_economics_unchanged(
        retired_before, retired_policy,
        "refusing a retired-policy decision mutated economics");
}

void test_cancel_effect_refuses_submit_ack_and_repeated_unknown() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = catalog_test::apply(state, authorize(2, 0x68), scratch);
    const auto cancel = catalog_test::apply(
        state, request_cancel(1, 0x69, submit.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied, "cancel-event fixture failed");

    const auto before_wrong_ack = state;
    const auto wrong_ack = catalog_test::apply(
        state,
        observe_order_event(1, 0x6a, VenueObservationKind::order_acknowledged,
                            submit.cluster_order_id(), cancel.effect_id(), 806),
        scratch);
    require(wrong_ack.code == ApplyCode::venue_observation_not_applicable,
            "a submit-only acknowledgement resolved a cancel effect");
    require(state.orders[0] == before_wrong_ack.orders[0] &&
                state.outbox_effects[1] == before_wrong_ack.outbox_effects[1] &&
                state.reservations[0] == before_wrong_ack.reservations[0] &&
                state.risk_accounts[0] == before_wrong_ack.risk_accounts[0],
            "refusing a mismatched cancel acknowledgement mutated lifecycle or risk");

    const auto first_unknown = catalog_test::apply(
        state,
        observe_order_event(2, 0x6b, VenueObservationKind::effect_unknown,
                            submit.cluster_order_id(), cancel.effect_id(), 807),
        scratch);
    require(first_unknown.code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::submission_unknown &&
                state.outbox_effects[1].state == OutboxState::unknown,
            "first unknown observation did not retain unresolved cancel risk");
    const auto after_first_unknown = state;
    const auto repeated_unknown = catalog_test::apply(
        state,
        observe_order_event(3, 0x6c, VenueObservationKind::effect_unknown,
                            submit.cluster_order_id(), cancel.effect_id(), 808),
        scratch);
    require(repeated_unknown.code == ApplyCode::already_applied,
            "a semantically repeated unknown event was accepted as a new transition");
    require(state.orders[0] == after_first_unknown.orders[0] &&
                state.outbox_effects[1] == after_first_unknown.outbox_effects[1] &&
                state.reservations[0] == after_first_unknown.reservations[0] &&
                state.risk_accounts[0] == after_first_unknown.risk_accounts[0],
            "repeated unknown evidence changed lifecycle or risk");
}

void test_fill_proves_only_the_submit_effect_it_names() {
    using namespace velocita::v2;
    ApplyScratch scratch;

    // A fill is positive evidence that the submit reached the venue. If the
    // submit outcome was previously unknown, a partial fill must therefore
    // advance the order to partially_filled and acknowledge that submit.
    auto unknown_submit = ready_state();
    const auto submit = catalog_test::apply(unknown_submit, authorize(2, 0x6d), scratch);
    require(catalog_test::apply(unknown_submit,
                  observe_order_event(1, 0x6e,
                                      VenueObservationKind::effect_unknown,
                                      submit.cluster_order_id(),
                                      submit.effect_id(), 809),
                  scratch)
                    .code == ApplyCode::applied &&
                unknown_submit.orders[0].state ==
                    ClusterOrderState::submission_unknown &&
                unknown_submit.outbox_effects[0].state == OutboxState::unknown,
            "submit-unknown fixture failed");
    require(catalog_test::apply(unknown_submit,
                  observe_fill(1, 0x6f, submit.cluster_order_id(),
                               submit.effect_id(), ExecutionId{810}),
                  scratch)
                    .code == ApplyCode::applied &&
                unknown_submit.orders[0].state ==
                    ClusterOrderState::partially_filled &&
                unknown_submit.outbox_effects[0].state ==
                    OutboxState::acknowledged,
            "a partial fill did not resolve the submit uncertainty it proved");

    // Once cancel is the current effect, a fill naming that cancel must be
    // refused: it cannot turn cancel delivery into execution evidence. A fill
    // naming the historical submit remains valid, but it must leave the cancel
    // unknown until cancel-specific evidence arrives.
    auto unknown_cancel = ready_state();
    const auto cancel_submit = catalog_test::apply(unknown_cancel, authorize(2, 0x70), scratch);
    const auto cancel = catalog_test::apply(
        unknown_cancel,
        request_cancel(1, 0x71, cancel_submit.cluster_order_id()), scratch);
    require(catalog_test::apply(unknown_cancel,
                  observe_order_event(1, 0x72,
                                      VenueObservationKind::effect_unknown,
                                      cancel_submit.cluster_order_id(),
                                      cancel.effect_id(), 811),
                  scratch)
                    .code == ApplyCode::applied &&
                unknown_cancel.orders[0].state ==
                    ClusterOrderState::submission_unknown &&
                unknown_cancel.outbox_effects[1].state == OutboxState::unknown,
            "cancel-unknown fixture failed");

    const auto before_wrong_effect = unknown_cancel;
    require(catalog_test::apply(unknown_cancel,
                  observe_fill(1, 0x73, cancel_submit.cluster_order_id(),
                               cancel.effect_id(), ExecutionId{812}),
                  scratch)
                    .code == ApplyCode::venue_observation_not_applicable,
            "a fill incorrectly treated a cancel effect as its submit identity");
    require_economics_unchanged(before_wrong_effect, unknown_cancel,
                                "a fill scoped to cancel mutated economics");
    require(unknown_cancel.orders[0] == before_wrong_effect.orders[0] &&
                unknown_cancel.outbox_effects[0] ==
                    before_wrong_effect.outbox_effects[0] &&
                unknown_cancel.outbox_effects[1] ==
                    before_wrong_effect.outbox_effects[1],
            "refusing a fill scoped to cancel changed lifecycle state");

    require(catalog_test::apply(unknown_cancel,
                  observe_fill(2, 0x74, cancel_submit.cluster_order_id(),
                               cancel_submit.effect_id(), ExecutionId{813}),
                  scratch)
                    .code == ApplyCode::applied &&
                unknown_cancel.orders[0].state ==
                    ClusterOrderState::submission_unknown &&
                unknown_cancel.outbox_effects[0].state ==
                    OutboxState::acknowledged &&
                unknown_cancel.outbox_effects[1].state == OutboxState::unknown,
            "a fill erased the still-unknown cancel outcome");
}

void test_authorization_reserves_completion_and_cancel_capacity() {
    using namespace velocita::v2;
    ApplyScratch scratch;

    // Fifty-eight retained observations leave six slots. A new two-atom order
    // can require two one-atom fills plus bounded submit/cancel reconciliation
    // transitions, so it must be refused before it can strand existing risk.
    auto event_pressure = ready_state();
    for (std::size_t index = 0; index < 58; ++index) {
        ApplyResult retained;
        retained.code = ApplyCode::applied;
        retained.detail = ApplyResultDetail::fill(
            VenueId{9}, ExecutionId{10'000 + index}, QuantityLots::from_atoms(1));
        retained.detail_kind = ApplyResultDetailKind::fill;
        retained.metadata_words = {2, 10'000 + index};
        event_pressure.receipts[event_pressure.receipt_count++] = {
            {CallerId{9}, RequestSequence{index + 1}, digest(0x70)}, retained};
    }
    const auto event_before = event_pressure;
    require(catalog_test::apply(event_pressure, authorize(2, 0x71), scratch).code ==
                ApplyCode::venue_event_capacity_exceeded,
            "authorization consumed venue identity capacity needed for completion");
    require_economics_unchanged(
        event_before, event_pressure,
        "venue completion-capacity refusal mutated economics");

    // Sixteen effect slots can support at most eight unresolved orders when
    // every order owns a submit record and must retain one cancel record. The
    // ninth order must stop at authorization, while all eight admitted orders
    // must still be able to enter cancellation.
    auto outbox_pressure = ready_state();
    auto& risk = outbox_pressure.risk_accounts[0];
    risk.max_order_quantity = QuantityLots::from_atoms(16);
    risk.max_reserved_quantity = QuantityLots::from_atoms(16);
    risk.max_order_gross_notional = MoneyMicros::from_atoms(100'000);
    risk.max_reserved_exposure = MoneyMicros::from_atoms(100'000);
    std::array<ClusterOrderId, 8> order_ids{};
    std::array<EffectId, 8> cancel_ids{};
    for (std::size_t index = 0; index < order_ids.size(); ++index) {
        const DecisionId decision{20 + index};
        if (index != 0) {
            append_submit_decision(outbox_pressure, decision, 11, 1,
                                   LogicalOrderSlot{
                                       static_cast<std::uint16_t>(index + 1)});
        }
        const auto submitted = catalog_test::apply(
            outbox_pressure,
            authorize_decision(2 + index, 0x72, decision), scratch);
        require(submitted.code == ApplyCode::applied,
                "outbox-reserve fixture could not authorize eight orders");
        order_ids[index] = submitted.cluster_order_id();
    }
    append_submit_decision(outbox_pressure, DecisionId{28}, 11, 1,
                           LogicalOrderSlot{9});
    const auto ninth_before = outbox_pressure;
    require(catalog_test::apply(outbox_pressure,
                  authorize_decision(10, 0x73, DecisionId{28}), scratch).code ==
                ApplyCode::outbox_capacity_exceeded,
            "authorization consumed the last cancel headroom in the outbox");
    require_economics_unchanged(
        ninth_before, outbox_pressure,
        "outbox cancel-headroom refusal mutated economics");

    for (std::size_t index = 0; index < order_ids.size(); ++index) {
        const auto cancelled = catalog_test::apply(
            outbox_pressure,
            request_cancel(index + 1, 0x74, order_ids[index]), scratch);
        require(cancelled.code == ApplyCode::applied,
                "reserved outbox slot was unavailable to a live order");
        cancel_ids[index] = cancelled.effect_id();
    }
    require(outbox_pressure.outbox_effect_count == kMaxOutboxEffects,
            "cancel-reserve fixture did not consume the bounded outbox");

    require(catalog_test::apply(outbox_pressure,
                  observe_order_event(1, 0x75,
                                      VenueObservationKind::order_rejected,
                                      order_ids[0], cancel_ids[0], 1'900),
                  scratch).code == ApplyCode::applied,
            "cancel-rejection fixture failed");
    // A rejected cancel no longer proves the order is still live (architecture
    // 10.3.5). The venue refuses a cancel for "already cancelled", "already
    // filled", and "no such order" as readily as for "still working", and once
    // cancels may be redelivered a rejection of the DUPLICATE is the common case.
    // So the order goes to `reconciling` and a fresh cancel needs reconciliation
    // first. The arena-pressure guarantee this test exists for is unchanged - the
    // reconciliation is an observation and allocates no outbox slot - it just
    // takes one more step to reach.
    require(catalog_test::apply(outbox_pressure,
                  request_cancel(9, 0x76, order_ids[0]), scratch).code ==
                ApplyCode::order_not_cancellable,
            "a fresh cancel was authorized before reconciliation established "
            "whether the order was still working");
    require(catalog_test::apply(outbox_pressure,
                  observe_order_event(2, 0x77,
                                      VenueObservationKind::order_reconciled,
                                      order_ids[0], cancel_ids[0], 1'901),
                  scratch).code == ApplyCode::applied,
            "reconciliation could not report the order still working");
    // A fresh request identity: the refused attempt above retained its receipt,
    // so re-presenting the same identity would correctly return that refusal.
    const auto retried = catalog_test::apply(
        outbox_pressure, request_cancel(10, 0x78, order_ids[0]), scratch);
    require(retried.code == ApplyCode::applied &&
                retried.effect_id() != cancel_ids[0] &&
                outbox_pressure.outbox_effect_count == kMaxOutboxEffects,
            "a rejected cancel could not reuse its settled outbox slot safely");
}

void test_caller_prefix_registration_is_atomic_and_bounded() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;
    for (std::uint64_t caller = 1; caller <= kMaxCallerCursors; ++caller) {
        const Command invalid{
            {CallerId{caller}, RequestSequence{1},
             digest(static_cast<std::uint8_t>(0x80 + caller))},
            AuthorizeDecision{DecisionId{}}};
        require(catalog_test::apply(state, invalid, scratch).code == ApplyCode::invalid_command,
                "caller-registration fixture was not admitted");
        require(state.caller_cursor_count == caller &&
                    state.caller_cursors[caller - 1].caller_id == CallerId{caller} &&
                    state.caller_cursors[caller - 1].expired_below ==
                        RequestSequence{},
                "first admission did not atomically register a zero-prefix cursor");
    }
    const auto before = state;
    const Command ninth{
        {CallerId{kMaxCallerCursors + 1}, RequestSequence{1}, digest(0x99)},
        AuthorizeDecision{DecisionId{}}};
    require(catalog_test::apply(state, ninth, scratch).code ==
                ApplyCode::receipt_capacity_exceeded,
            "a ninth stable caller escaped the bounded cursor arena");
    require(state.state_version == before.state_version &&
                state.receipt_count == before.receipt_count &&
                state.caller_cursor_count == before.caller_cursor_count,
            "caller-capacity refusal partially admitted the ninth caller");
}

void test_invalid_observation_refusal_is_canonical_state() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = catalog_test::apply(state, authorize(2, 0x9a), scratch);
    auto invalid = observe_fill(1, 0x9b, submit.cluster_order_id(),
                                submit.effect_id(), ExecutionId{901});
    auto& observation = std::get<ObserveFill>(invalid.body).observation;
    observation.execution_id = {};
    invalid.identity.payload_digest = observation_fingerprint(observation);

    const auto refused = catalog_test::apply(state, invalid, scratch);
    require(refused.code == ApplyCode::invalid_command &&
                refused.contract_error == ContractError::invalid_identity,
            "malformed fill did not produce the intended typed refusal");
    std::array<std::byte, kKernelResultBytes> result_bytes{};
    require(encode_kernel_result(refused, result_bytes).error ==
                KernelCodecError::none,
            "an admitted invalid-fill refusal was not canonically encodable");
    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    require(encode_state_snapshot(state, snapshot).error ==
                StateSnapshotError::none,
            "an admitted invalid-fill refusal made TradingState unsnapshotable");
}

void test_decision_retry_retains_original_authorization_after_fill() {
    using namespace velocita::v2;
    ApplyScratch scratch;
    auto state = ready_state();
    const auto submit = catalog_test::apply(state, authorize(2, 0x81), scratch);
    require(catalog_test::apply(state, observe_fill(1, 0x82, submit.cluster_order_id(),
                  submit.effect_id(), ExecutionId{1001}, 2), scratch).code ==
                ApplyCode::applied, "decision retry fixture fill failed");
    const auto before = state;
    const auto retry = catalog_test::apply(state, authorize(3, 0x83), scratch);
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
            const auto submit = catalog_test::apply(state, authorize(2, 0x71), scratch);
            const auto cancel = catalog_test::apply(state,
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
                // Sequences are assigned at APPLY time, not at construction.
                // The permutation deliberately applies these commands in every
                // order, so fixed identities would send a caller's sequence
                // backwards, which architecture 8.6 now correctly refuses as
                // expired. This test is about evidence ordering, not request
                // deduplication, so each application gets a fresh identity.
                auto command = commands[step];
                command.identity.request_sequence =
                    RequestSequence{state.state_version + 500};
                const auto result = catalog_test::apply(state, command, scratch);
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
                TradingState restored = catalog_test::state();
                StateSnapshotScratch snapshot_scratch;
                require(decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                    snapshot_scratch).error == StateSnapshotError::none,
                    "permutation snapshot restore failed");
                state = restored;
            }
            // A refused early proof is a stable command outcome. A new request
            // may re-present the same venue evidence once its prerequisites exist.
            // Derived from the monotonic state version rather than fixed:
            // RequestSequence must be monotonic per caller (architecture 8.6),
            // and a fixed sequence re-presented after later permutations have
            // advanced this caller's watermark is now correctly refused as
            // expired rather than executed.
            final.identity.request_sequence =
                RequestSequence{state.state_version + 1'000};
            const auto final_result = catalog_test::apply(state, final, scratch);
            // `venue_observation_not_applicable` joins the accepted set because
            // `cancel_reconciled` is a SYNTHETIC observation: it no longer holds
            // a place in the venue's sequence domain, so a re-presentation after
            // the order has already converged is re-adjudicated and refused on
            // the order's state rather than on identity. That refusal is the
            // correct answer - there is nothing left for the evidence to say -
            // and the convergence asserted below is what this test is actually
            // about.
            require((final_result.code == ApplyCode::applied ||
                     final_result.code == ApplyCode::already_applied ||
                     final_result.code ==
                         ApplyCode::venue_observation_not_applicable) &&
                    state.risk_accounts[0].position_quantity == QuantityLots::from_atoms(filled) &&
                    state.risk_accounts[0].reserved_quantity == QuantityLots{} &&
                    state.reservations[0].state == ReservationState::released &&
                    state.orders[0].state == (filled == 2 ? ClusterOrderState::filled
                                                          : ClusterOrderState::cancelled),
                    "evidence permutation did not converge: code=" +
                        std::to_string(static_cast<int>(final_result.code)) +
                        " pos=" + std::to_string(state.risk_accounts[0].position_quantity.atoms()) +
                        " resv=" + std::to_string(state.risk_accounts[0].reserved_quantity.atoms()) +
                        " cursors=" + std::to_string(state.caller_cursor_count));
        } while (std::next_permutation(permutation.begin(), permutation.end()));
    }
}

void test_cancel_before_earlier_fill_does_not_lose_fill_evidence() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = catalog_test::apply(state, authorize(2, 0xf1), scratch);
    const auto cancel = catalog_test::apply(state,
        request_cancel(1, 0xf2, submit.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied, "late-fill cancel fixture failed");
    // Venue order: fill at 1601, cancellation at 1602. Delivery is reversed.
    require(catalog_test::apply(state, observe_order_event(1, 0xf3, VenueObservationKind::cancelled,
        submit.cluster_order_id(), cancel.effect_id(), 1602), scratch).code ==
            ApplyCode::applied, "late-fill cancellation fixture failed");
    require(state.orders[0].state == ClusterOrderState::reconciling &&
                state.reservations[0].remaining_quantity == QuantityLots::from_atoms(2),
            "cancellation released risk before missing fills arrived");
    std::array<std::byte, kMaxStateSnapshotBytes> pending_snapshot{};
    const auto pending = encode_state_snapshot(state, pending_snapshot);
    require(pending.error == StateSnapshotError::none, "pending reconciliation snapshot failed");
    TradingState restored = catalog_test::state();
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({pending_snapshot.data(), pending.bytes_written},
        restored, snapshot_scratch).error == StateSnapshotError::none,
        "pending reconciliation restore failed");
    state = restored;
    const auto late_fill = catalog_test::apply(state, observe_fill(1, 0xf4,
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
    require(catalog_test::apply(state, proof, scratch).code == ApplyCode::venue_observation_not_applicable,
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
    const auto replay = catalog_test::replay(replayed,
        {command_bytes.data(), encoded_command.bytes_written}, scratch, codec_scratch, result_bytes);
    const auto reconciled = catalog_test::apply(state, proof, scratch);
    require(replay.error == ReplayError::none && replay.apply_result == reconciled,
            "reconciliation canonical replay diverged");
    require(reconciled.code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::cancelled &&
                state.reservations[0].state == ReservationState::released &&
                state.risk_accounts[0].reserved_quantity == QuantityLots{} &&
                state.risk_accounts[0].position_quantity == QuantityLots::from_atoms(1),
            "matching final evidence did not close residual accounting");
    require(catalog_test::apply(state, proof, scratch) == reconciled,
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
    // A cross-request retry of a SYNTHETIC final proof. It is no longer refused
    // on venue sequence identity - that domain belongs to the venue - so it is
    // re-adjudicated and refused on the order, which has already converged.
    // Asserted as the property the message names rather than as a code: the
    // retry must not move economics a second time.
    const auto before_retry = state;
    proof.identity.request_sequence = RequestSequence{4};
    const auto retried = catalog_test::apply(state, proof, scratch);
    require(retried.code != ApplyCode::venue_event_identity_conflict,
            "a synthetic final proof was refused on a venue sequence identity");
    require(state.orders[0].state == before_retry.orders[0].state &&
                state.reservations[0] == before_retry.reservations[0] &&
                state.risk_accounts[0].position_quantity ==
                    before_retry.risk_accounts[0].position_quantity &&
                state.risk_accounts[0].reserved_quantity ==
                    before_retry.risk_accounts[0].reserved_quantity,
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
    const auto invalid = catalog_test::apply(state, proof, scratch);
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
    const auto submit = catalog_test::apply(state, authorize(2, 0xd1), scratch);
    require(catalog_test::apply(state, observe_order_event(1, 0xd2,
        VenueObservationKind::order_acknowledged, submit.cluster_order_id(),
        submit.effect_id(), 1401), scratch).code == ApplyCode::applied,
        "shared identity acknowledgement fixture failed");
    const auto before = state;
    require(catalog_test::apply(state, observe_fill(1, 0xd3, submit.cluster_order_id(),
        submit.effect_id(), ExecutionId{1401}), scratch).code ==
            ApplyCode::venue_event_identity_conflict,
        "fill reused a retained acknowledgement venue/session/sequence");
    require_economics_unchanged(before, state, "shared identity conflict booked a fill");
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(before, bytes);
    require(encoded.error == StateSnapshotError::none, "shared identity snapshot failed");
    TradingState restored = catalog_test::state();
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                snapshot_scratch).error == StateSnapshotError::none,
            "shared identity restore failed");
    require(catalog_test::apply(restored, observe_fill(1, 0xd3, submit.cluster_order_id(),
        submit.effect_id(), ExecutionId{1401}), scratch).code ==
            ApplyCode::venue_event_identity_conflict,
        "shared identity conflict disappeared after restore");
}

void test_order_event_cannot_reuse_a_fill_identity() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = catalog_test::apply(state, authorize(2, 0xe1), scratch);
    require(catalog_test::apply(state, observe_fill(1, 0xe2, submit.cluster_order_id(),
        submit.effect_id(), ExecutionId{1501}), scratch).code == ApplyCode::applied,
        "reverse shared identity fill fixture failed");
    const auto cancel = catalog_test::apply(state,
        request_cancel(1, 0xe3, submit.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied, "reverse shared identity cancel fixture failed");
    const auto before = state;
    require(catalog_test::apply(state, observe_order_event(1, 0xe4,
        VenueObservationKind::cancelled, submit.cluster_order_id(),
        cancel.effect_id(), 1501), scratch).code == ApplyCode::venue_event_identity_conflict,
        "cancel evidence reused a fill venue/session/sequence and released risk");
    require_economics_unchanged(before, state, "shared identity conflict released risk");
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(before, bytes);
    require(encoded.error == StateSnapshotError::none, "fill identity snapshot failed");
    TradingState restored = catalog_test::state();
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                snapshot_scratch).error == StateSnapshotError::none,
            "fill identity snapshot restore failed");
    auto event = observe_order_event(1, 0xe4, VenueObservationKind::cancelled,
        submit.cluster_order_id(), cancel.effect_id(), 1501);
    require(catalog_test::apply(restored, event, scratch).code == ApplyCode::venue_event_identity_conflict,
            "fill stream identity disappeared after restore");
    auto other_fill = observe_fill(2, 0xe5, submit.cluster_order_id(),
        submit.effect_id(), ExecutionId{1502});
    auto& observation = std::get<ObserveFill>(other_fill.body).observation;
    observation.venue_sequence = VenueSequence{1501};
    other_fill.identity.payload_digest = observation_fingerprint(observation);
    require(catalog_test::apply(restored, other_fill, scratch).code == ApplyCode::venue_event_identity_conflict,
            "different execution reused one fill stream identity");
    auto& event_observation = std::get<ObserveOrderEvent>(event.body).observation;
    event_observation.venue_session_id = VenueSessionId{3};
    event.identity.request_sequence = RequestSequence{2};
    event.identity.payload_digest = observation_fingerprint(event_observation);
    require(catalog_test::apply(restored, event, scratch).code == ApplyCode::applied,
            "same sequence in a different venue session falsely conflicted");
}

void test_fill_digest_cannot_hide_changed_economics() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = catalog_test::apply(state, authorize(2, 0xb1), scratch);
    const auto first = observe_fill(1, 0xb2, submit.cluster_order_id(),
        submit.effect_id(), ExecutionId{1301}, 1);
    require(catalog_test::apply(state, first, scratch).code == ApplyCode::applied,
            "fingerprint fixture fill failed");
    auto changed = first;
    changed.identity.request_sequence = RequestSequence{2};
    std::get<ObserveFill>(changed.body).observation.quantity = QuantityLots::from_atoms(2);
    const auto before = state;
    const auto outcome = catalog_test::apply(state, changed, scratch);
    require(outcome.code != ApplyCode::already_applied && outcome.code != ApplyCode::applied,
            "copied caller digest hid changed fill quantity as an accepted duplicate");
    require_economics_unchanged(before, state, "changed fill mutated economics");
    require(state.state_version == before.state_version &&
                state.receipt_count == before.receipt_count,
            "bad fingerprint consumed admission state");
    changed.identity.request_sequence = first.identity.request_sequence;
    require(catalog_test::apply(state, changed, scratch).code == ApplyCode::invalid_command,
            "same-request lookup bypassed fingerprint verification");
    auto wrong_wrapper = first;
    wrong_wrapper.body = ObserveOrderEvent{std::get<ObserveFill>(first.body).observation};
    require(catalog_test::apply(state, wrong_wrapper, scratch).code == ApplyCode::invalid_command,
            "wrong observation command wrapper bypassed admission validation");
    changed.identity.request_sequence = RequestSequence{2};
    changed.identity.payload_digest = observation_fingerprint(
        std::get<ObserveFill>(changed.body).observation);
    require(catalog_test::apply(state, changed, scratch).code == ApplyCode::fill_identity_conflict,
            "valid fingerprint concealed conflicting execution content");
    state = ready_state();
    require(catalog_test::apply(state, authorize(2, 0xb1), scratch).code == ApplyCode::applied,
            "event fingerprint submit fixture failed");
    auto event = observe_order_event(1, 0xb3, VenueObservationKind::order_acknowledged,
        submit.cluster_order_id(), submit.effect_id(), 1302);
    const auto accepted_event = catalog_test::apply(state, event, scratch);
    require(accepted_event.code == ApplyCode::applied,
            "event fingerprint fixture failed");
    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    const auto encoded = encode_state_snapshot(state, snapshot);
    require(encoded.error == StateSnapshotError::none, "fingerprint snapshot failed");
    TradingState restored = catalog_test::state();
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({snapshot.data(), encoded.bytes_written}, restored,
                snapshot_scratch).error == StateSnapshotError::none,
            "fingerprint snapshot restore failed");
    state = restored;
    require(catalog_test::apply(state, event, scratch) == accepted_event,
            "verified request receipt did not survive restore");
    auto retry_event = event;
    retry_event.identity.request_sequence = RequestSequence{9};
    require(catalog_test::apply(state, retry_event, scratch).code == ApplyCode::already_applied,
            "verified venue-event fingerprint did not survive restore");
    // Mutated to another NATIVE kind on purpose. The venue's sequence domain is
    // what this case is about, and only a native observation belongs to it: the
    // venue said sequence 1302 was an acknowledgement, and it cannot also have
    // been a rejection.
    auto changed_event = event;
    std::get<ObserveOrderEvent>(changed_event.body).observation.kind =
        VenueObservationKind::order_rejected;
    require(catalog_test::apply(state, changed_event, scratch).code == ApplyCode::invalid_command,
            "same-request event lookup bypassed fingerprint verification");
    changed_event.identity.request_sequence = RequestSequence{2};
    require(catalog_test::apply(state, changed_event, scratch).code == ApplyCode::invalid_command,
            "cross-request event lookup bypassed fingerprint verification");
    changed_event.identity.payload_digest = observation_fingerprint(
        std::get<ObserveOrderEvent>(changed_event.body).observation);
    require(catalog_test::apply(state, changed_event, scratch).code == ApplyCode::venue_event_identity_conflict,
            "valid fingerprint concealed conflicting venue-event content");

    // And the counterpart. A SYNTHETIC observation carrying the same number is
    // not a contradiction, because that number was never the venue's to give:
    // the gateway chose it. Refusing here would discard the gateway's only way
    // of telling the cluster an effect's outcome is unknown.
    auto synthetic_event = event;
    auto& synthetic_observation =
        std::get<ObserveOrderEvent>(synthetic_event.body).observation;
    synthetic_observation.kind = VenueObservationKind::effect_unknown;
    synthetic_observation.venue_order_id = VenueOrderId{};
    synthetic_event.identity.request_sequence = RequestSequence{3};
    synthetic_event.identity.payload_digest =
        observation_fingerprint(synthetic_observation);
    require(catalog_test::apply(state, synthetic_event, scratch).code !=
                ApplyCode::venue_event_identity_conflict,
            "a gateway-authored observation was refused on a venue sequence it "
            "never owned");
}

void test_cancel_effect_can_be_reconstructed() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = catalog_test::apply(state, authorize(2, 0xa1), scratch);
    const auto cancel = catalog_test::apply(state,
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
        TradingState restored = catalog_test::state();
        StateSnapshotScratch snapshot_scratch;
        require(decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                    snapshot_scratch).error == StateSnapshotError::none,
                "cancel snapshot restore failed");
        const auto restored_effect = materialize_order_effect(restored, cancel.effect_id());
        require(restored_effect && encode_payload(*restored_effect).bytes == expected_bytes,
                "cancel bytes changed across snapshot restore");
    };
    verify();
    require(catalog_test::apply(state, observe_fill(1, 0xa3, submit.cluster_order_id(),
        submit.effect_id(), ExecutionId{1201}, 1), scratch).code == ApplyCode::applied,
        "cancel reconstruction fill failed");
    require(state.reservations[0].remaining_quantity == QuantityLots::from_atoms(1),
            "cancel normalized quantity incorrectly removed residual risk");
    verify();
    require(catalog_test::apply(state, observe_order_event(1, 0xa4, VenueObservationKind::cancelled,
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
    const auto submit = catalog_test::apply(state, authorize(2, 0x91), scratch);
    // The second effect is a cancel rather than a replace: direct replacement is
    // refused, so a submit/cancel pair is the only way one order accumulates two
    // effects. The test still proves what it was written to prove - that every
    // retained effect reconstructs its exact historical bytes regardless of how
    // live state moves afterwards.
    const auto cancel = catalog_test::apply(state,
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
        TradingState decoded = catalog_test::state();
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
    require(catalog_test::apply(state, observe_order_event(2, 0x94,
        VenueObservationKind::effect_unknown, submit.cluster_order_id(),
        cancel.effect_id(), 1100), scratch).code == ApplyCode::applied,
        "outbox fixture unknown observation failed");
    verify(state);
    require(catalog_test::apply(state, observe_order_event(1, 0x93,
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
    TradingState restored = catalog_test::state();
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
    const auto submit = catalog_test::apply(state, authorize(2, 0xe1), scratch);
    require(submit.code == ApplyCode::applied, "submit fixture did not authorize");
    append_replacement_decision(state, DecisionId{21}, 12, 3);

    const auto before = state;
    const auto command = request_replace(
        1, 0xe2, submit.cluster_order_id(), DecisionId{21});
    const auto refused = catalog_test::apply(state, command, scratch);

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
    TradingState restored = catalog_test::state();
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({snapshot.data(), encoded.bytes_written},
                                  restored, snapshot_scratch)
                    .error == StateSnapshotError::none,
            "refused-replacement snapshot did not decode");
    const auto repeated = catalog_test::apply(restored, command, scratch);
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
    const auto submit = catalog_test::apply(state, authorize(2, 0xf1), scratch);
    require(submit.code == ApplyCode::applied, "submit fixture did not authorize");

    const auto fill = catalog_test::apply(
        state,
        observe_fill(1, 0xf2, submit.cluster_order_id(), submit.effect_id(),
                     ExecutionId{801}),
        scratch);
    require(fill.code == ApplyCode::applied, "partial fill fixture failed");

    const auto cancel = catalog_test::apply(
        state, request_cancel(1, 0xf3, submit.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::cancel_pending &&
                state.reservations[0].state != ReservationState::released,
            "cancel fixture did not leave a risk-bearing cancel-pending order");
    require(catalog_test::apply(state,
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
    const auto blocked = catalog_test::apply(state, authorize_decision(3, 0xf5, DecisionId{22}),
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
    require(catalog_test::apply(state, proof, scratch).code == ApplyCode::applied &&
                state.reservations[0].state == ReservationState::released,
            "final cancel reconciliation did not release the residual hold");

    const auto admitted = catalog_test::apply(state, authorize_decision(4, 0xf7, DecisionId{22}),
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
// slot free. Payload records can be reclaimed, but retained receipts must still
// prevent a historical decision from authorizing another economic mutation.
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
        const auto submit = catalog_test::apply(
            state, authorize_decision(request++, 0xb0, decision), scratch);
        require(submit.code == ApplyCode::applied,
                "requote cycle could not authorize: bounded arena was not reclaimed");
        const auto fill = catalog_test::apply(
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
    const auto stranded = catalog_test::apply(state, authorize_decision(2, 0xc0, DecisionId{20}),
                                scratch);
    require(stranded.code == ApplyCode::applied, "stranded fixture failed");
    require(catalog_test::apply(state,
                  observe_order_event(1, 0xc1,
                                      VenueObservationKind::order_acknowledged,
                                      stranded.cluster_order_id(),
                                      stranded.effect_id(), 1500),
                  scratch)
                    .code == ApplyCode::applied,
            "stranded fixture acknowledgement failed");
    const auto stranded_cancel = catalog_test::apply(
        state, request_cancel(1, 0xc2, stranded.cluster_order_id()), scratch);
    require(stranded_cancel.code == ApplyCode::applied, "stranded cancel failed");
    require(catalog_test::apply(state,
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
        const auto submit = catalog_test::apply(
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
        require(catalog_test::apply(state,
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
    auto state = two_sided_state();
    ApplyScratch scratch;

    // A conflicted order on slot 1. Once superseded proposals became
    // reclaimable, the decision arena can no longer be filled to force a sweep,
    // so pressure now comes from the ORDER arena instead - which is the more
    // realistic source anyway. That needs a second slot to cycle on, hence the
    // two-sided fixture.
    require(evaluate_at_feature(state, scratch, DecisionId{30}, 16, 2, 3).code ==
                ApplyCode::applied,
            "conflict fixture decision failed");
    const auto conflicted = catalog_test::apply(state, authorize_decision(4, 0xd0, DecisionId{30}),
                                  scratch);
    require(conflicted.code == ApplyCode::applied, "conflict fixture failed");
    state.orders[0].state = ClusterOrderState::conflict;
    const auto held_quantity = state.risk_accounts[0].reserved_quantity;
    const auto held_margin = state.risk_accounts[0].reserved_margin;
    const auto effect_bytes =
        encode_payload(*materialize_order_effect(state, conflicted.effect_id())).bytes;

    std::uint64_t request = 5;
    std::uint64_t sequence = 3;
    std::uint64_t execution = 5'000;
    for (std::uint64_t cycle = 1; cycle < 24; ++cycle) {
        const DecisionId decision{40 + cycle};
        require(evaluate_at_feature(state, scratch, decision, 11, sequence++,
                                    request++)
                        .code == ApplyCode::applied,
                "slot-2 requote decision was not retained");
        const auto submit = catalog_test::apply(
            state, authorize_decision(request++, 0xd1, decision), scratch);
        require(submit.code == ApplyCode::applied,
                "slot-2 requote could not authorize beside a conflicted order");
        require(catalog_test::apply(state,
                      observe_fill(request++, 0xd2, submit.cluster_order_id(),
                                   submit.effect_id(), ExecutionId{execution++}, 2,
                                   Side::sell, 11),
                      scratch)
                        .code == ApplyCode::applied,
                "slot-2 requote fill failed");
    }
    require(state.reclaimed_group_count > 0,
            "order-arena pressure never triggered a sweep");

    bool retained = false;
    for (std::size_t index = 0; index < state.order_count; ++index) {
        if (state.orders[index].cluster_order_id == conflicted.cluster_order_id()) {
            retained = state.orders[index].state == ClusterOrderState::conflict;
        }
    }
    require(retained, "a conflicted order was reclaimed by the sweep");
    require(state.risk_accounts[0].reserved_quantity == held_quantity &&
                state.risk_accounts[0].reserved_margin == held_margin,
            "reclaiming released risk still held by a conflicted order");
    const auto survivor = materialize_order_effect(state, conflicted.effect_id());
    require(survivor.has_value() && encode_payload(*survivor).bytes == effect_bytes,
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
        const auto submit = catalog_test::apply(
            state, authorize_decision(request++, 0xe0, decision), scratch);
        require(submit.code == ApplyCode::applied, "pairing fixture authorize failed");
        require(catalog_test::apply(state,
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

void test_reclaimed_decision_retains_identity_and_authorization() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    ApplyResult original;
    std::uint64_t request = 2;
    for (std::uint64_t cycle = 0; cycle < 20; ++cycle) {
        const DecisionId decision{20 + cycle};
        if (cycle != 0) {
            require(evaluate_new_decision(state, scratch, decision, cycle + 1,
                                         request++).code == ApplyCode::applied,
                    "identity fixture evaluation failed");
        }
        const auto submit = catalog_test::apply(state, authorize_decision(request++, 0xb0, decision), scratch);
        require(submit.code == ApplyCode::applied, "identity fixture authorization failed");
        if (cycle == 0) original = submit;
        require(catalog_test::apply(state, observe_fill(request++, 0xb1, submit.cluster_order_id(),
                    submit.effect_id(), ExecutionId{3000 + cycle}, 2), scratch).code ==
                    ApplyCode::applied, "identity fixture fill failed");
    }
    require(state.reclaimed_group_count > 0 &&
                !materialize_order_effect(state, original.effect_id()).has_value(),
            "identity fixture did not reclaim the original effect");
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(state, bytes);
    require(encoded.error == StateSnapshotError::none, "identity fixture snapshot failed");
    TradingState restored = catalog_test::state();
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                snapshot_scratch).error == StateSnapshotError::none,
            "identity fixture restore failed");
    for (auto* candidate : {&state, &restored}) {
        const auto risk = candidate->risk_accounts[0];
        const auto orders = candidate->order_count;
        const auto decisions = candidate->decision_count;
        const auto effects = candidate->outbox_effect_count;
        const auto retry = catalog_test::apply(*candidate, authorize_decision(request, 0xb0, DecisionId{20}), scratch);
        auto expected = original;
        expected.code = ApplyCode::already_applied;
        expected.state_version = retry.state_version;
        require(retry == expected,
                "reclaimed decision retry lost its original authorization receipt");
        const Command same{{CallerId{8}, RequestSequence{request + 1}, digest(0x51)},
            EvaluateDecisionFrame{DecisionId{20}, EvaluationMode::authoritative, frame()}};
        require(catalog_test::apply(*candidate, same, scratch).code == ApplyCode::already_applied,
                "reclaimed decision evaluation was admitted a second time");
        require(evaluate_new_decision(*candidate, scratch, DecisionId{20}, 21,
                    request + 2).code == ApplyCode::decision_id_conflict,
                "reclaimed DecisionId accepted different evaluation content");
        require(candidate->risk_accounts[0] == risk && candidate->order_count == orders &&
                    candidate->decision_count == decisions && candidate->outbox_effect_count == effects,
                "reclaimed identity retry mutated economics or recreated payload records");
        // A genuinely fresh identity still works and has exactly one materializable effect.
        require(evaluate_new_decision(*candidate, scratch, DecisionId{40}, 22,
                    request + 3).code == ApplyCode::applied, "fresh decision was blocked");
        const auto fresh = catalog_test::apply(*candidate,
            authorize_decision(request + 4, 0xb2, DecisionId{40}), scratch);
        require(fresh.code == ApplyCode::applied && fresh.effect_id() != original.effect_id() &&
                    materialize_order_effect(*candidate, fresh.effect_id()).has_value(),
                "fresh authorization aliased a reclaimed effect or cannot reconstruct");
    }
    const auto fresh_id = state.outbox_effects[state.outbox_effect_count - 1].effect_id;
    const auto direct_effect = materialize_order_effect(state, fresh_id);
    const auto restored_effect = materialize_order_effect(restored, fresh_id);
    require(direct_effect && restored_effect &&
                encode_payload(*direct_effect).bytes == encode_payload(*restored_effect).bytes,
            "post-reclamation authorization changed full effect bytes across restore");
    std::array<std::byte, kMaxStateSnapshotBytes> direct_bytes{}, restored_bytes{};
    require(encode_state_snapshot(state, direct_bytes).error == StateSnapshotError::none &&
                encode_state_snapshot(restored, restored_bytes).error == StateSnapshotError::none &&
                direct_bytes == restored_bytes,
            "identical post-reclamation commands diverged after snapshot restore");
}

velocita::v2::Command configure_risk(const velocita::v2::TradingState& state,
                                    const std::uint64_t request) {
    using namespace velocita::v2;
    const auto& risk = state.risk_accounts[0];
    return {{CallerId{kMaxOperatorCallerId}, RequestSequence{request}, digest(0xe2)},
        ConfigureRiskLimits{risk.account_id, risk.instrument_id, risk.risk_policy_version,
            risk.risk_version, state.kill_epoch, state.executor_generation,
            risk.max_order_quantity, risk.max_reserved_quantity,
            risk.max_order_gross_notional, risk.max_reserved_exposure,
            risk.initial_margin_ppm, risk.maximum_fee_ppm,
            risk.max_position_quantity, risk.allow_short, 1}};
}

void test_risk_configuration_codec_and_restore() {
    using namespace velocita::v2;
    auto state = ready_state();
    auto command = configure_risk(state, 1);
    std::array<std::byte, kMaxKernelCommandBytes> bytes{}, again{};
    const auto encoded = encode_kernel_command(command, bytes);
    require(encoded.error == KernelCodecError::none && encoded.bytes_written == 193,
            "risk configuration lacks its canonical 101-byte body");
    require(bytes[8] == std::byte{9} && bytes[10] == std::byte{17},
            "risk configuration schema/kind is not V9/17");
    std::vector<std::byte> expected;
    const auto put = [&](const std::uint64_t value, const unsigned width) {
        for (unsigned shift = 0; shift < width; shift += 8)
            expected.push_back(static_cast<std::byte>((value >> shift) & 0xff));
    };
    for (const std::uint64_t value : {5ULL, 4ULL, 6ULL, 1ULL, 2ULL, 3ULL, 4ULL, 8ULL, 4000ULL, 8000ULL})
        put(value, 64);
    for (const std::uint32_t value : {1'000'000U, 2'500U}) put(value, 32);
    put(1'000, 64);  // max_position_quantity
    put(1, 8);       // allow_short
    put(1, 32);      // reason_code
    require(expected.size() == 101 &&
                std::equal(expected.begin(), expected.end(), bytes.begin() + 60),
            "risk configuration canonical field order or integer widths changed");
    Command decoded;
    KernelCommandCodecScratch codec_scratch;
    require(decode_kernel_command({bytes.data(), encoded.bytes_written}, decoded, codec_scratch).error ==
                KernelCodecError::none && encode_kernel_command(decoded, again).error == KernelCodecError::none &&
                bytes == again, "configuration command round trip changed bytes");
    for (std::size_t cut = 0; cut < encoded.bytes_written; ++cut) {
        require(decode_kernel_command({bytes.data(), cut}, decoded, codec_scratch).error !=
                    KernelCodecError::none, "truncated configuration was accepted");
    }
    auto malformed = bytes;
    std::fill_n(malformed.begin() + 60 + 48, 8, std::byte{}); // zero max_order_quantity
    const auto integrity = sha256_digest(std::span<const std::byte>{malformed.data() + 8,
        encoded.bytes_written - kDigestBytes - 8});
    std::copy(integrity.bytes.begin(), integrity.bytes.end(),
        malformed.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written - kDigestBytes));
    require(decode_kernel_command({malformed.data(), encoded.bytes_written}, decoded, codec_scratch).error ==
                KernelCodecError::invalid_value && encode_kernel_command(decoded, again).error ==
                KernelCodecError::none && again == bytes,
            "malformed configuration passed semantic validation or changed decode destination");
    // Each appended field is validated on decode, not only on encode: a byte
    // the encoder can never produce must not be admitted from the log.
    const auto reject_body = [&](const std::size_t offset, const std::vector<std::byte>& patch,
                                 const std::string& what) {
        auto corrupt = bytes;
        std::copy(patch.begin(), patch.end(), corrupt.begin() + 60 + static_cast<std::ptrdiff_t>(offset));
        const auto resealed = sha256_digest(std::span<const std::byte>{corrupt.data() + 8,
            encoded.bytes_written - kDigestBytes - 8});
        std::copy(resealed.bytes.begin(), resealed.bytes.end(),
            corrupt.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written - kDigestBytes));
        require(decode_kernel_command({corrupt.data(), encoded.bytes_written}, decoded, codec_scratch).error ==
                    KernelCodecError::invalid_value, what);
    };
    reject_body(96, {std::byte{2}}, "allow_short outside {0,1} was decoded");
    reject_body(96, {std::byte{0xff}}, "allow_short 0xff was decoded");
    reject_body(97, {std::byte{}, std::byte{}, std::byte{}, std::byte{}},
                "a zero reason_code was decoded");
    reject_body(88, {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
                     std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}},
                "a negative max_position_quantity was decoded");
    auto old = bytes;
    old[8] = std::byte{8};
    require(decode_kernel_command({old.data(), encoded.bytes_written}, decoded, codec_scratch).error ==
                KernelCodecError::unsupported_schema, "V8 command was reinterpreted as V9");
    ApplyScratch scratch;
    const auto configured = catalog_test::apply(state, command, scratch);
    std::array<std::byte, kKernelResultBytes> result_bytes{};
    ApplyResult restored_result;
    const auto result_encoded = encode_kernel_result(configured, result_bytes);
    require(result_encoded.error == KernelCodecError::none &&
                decode_kernel_result({result_bytes.data(), result_encoded.bytes_written}, restored_result).error ==
                    KernelCodecError::none && restored_result == configured &&
                configured.detail_kind == ApplyResultDetailKind::risk_configuration &&
                configured.detail.words == std::array<std::uint64_t, 3>{5, 4, 2},
            "configuration result lost scope or resulting version");
    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    const auto saved = encode_state_snapshot(state, snapshot);
    TradingState restored = catalog_test::state();
    StateSnapshotScratch snapshot_scratch;
    require(saved.error == StateSnapshotError::none, "cfg: encode failed");
    const auto restore_outcome = decode_state_snapshot({snapshot.data(), saved.bytes_written}, restored, snapshot_scratch);
    require(restore_outcome.error == StateSnapshotError::none,
            "cfg: decode failed code=" +
                std::to_string(static_cast<int>(restore_outcome.error)));
    const auto retried_config = catalog_test::apply(restored, command, scratch);
    require(retried_config == configured,
            "cfg: retry differed code=" + std::to_string(static_cast<int>(retried_config.code)));
    require(restored.risk_accounts == state.risk_accounts, "cfg: ledger differed");
    snapshot[8] = std::byte{8};
    require(decode_state_snapshot({snapshot.data(), saved.bytes_written}, restored, snapshot_scratch).error ==
                StateSnapshotError::unsupported_schema, "V8 snapshot was reinterpreted as V9");
}

void test_risk_configuration_updates_preserve_economics() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = catalog_test::apply(state, authorize(2, 0x91), scratch);
    require(submit.code == ApplyCode::applied, "configuration live-order fixture failed");
    const auto old = state.risk_accounts[0];
    const auto effect_before = encode_payload(*materialize_order_effect(state, submit.effect_id())).bytes;
    auto update = configure_risk(state, 1);
    auto& body = std::get<ConfigureRiskLimits>(update.body);
    body.max_order_quantity = QuantityLots::from_atoms(1);
    body.max_order_gross_notional = MoneyMicros::from_atoms(1);
    require(old.allow_short && old.max_position_quantity == QuantityLots::from_atoms(1'000),
            "fixture drifted: the update below must change both position fields");
    body.max_position_quantity = QuantityLots::from_atoms(7);
    body.allow_short = false;
    const auto changed = catalog_test::apply(state, update, scratch);
    require(changed.code == ApplyCode::applied && state.risk_accounts[0].risk_version == RiskVersion{2},
            "risk configuration update did not advance exactly one version");
    require(state.risk_accounts[0].max_order_quantity == body.max_order_quantity &&
                state.risk_accounts[0].max_order_gross_notional == body.max_order_gross_notional,
            "prospective per-order cap update was ignored");
    require(state.risk_accounts[0].max_position_quantity == QuantityLots::from_atoms(7) &&
                !state.risk_accounts[0].allow_short,
            "position cap or short permission update was not stored");
    require(state.risk_accounts[0].reserved_quantity == old.reserved_quantity &&
                state.risk_accounts[0].reserved_margin == old.reserved_margin &&
                state.risk_accounts[0].reserved_fee == old.reserved_fee &&
                state.risk_accounts[0].position_quantity == old.position_quantity &&
                state.risk_accounts[0].position_version == old.position_version &&
                state.order_count == 1 && state.outbox_effect_count == 1 &&
                encode_payload(*materialize_order_effect(state, submit.effect_id())).bytes == effect_before,
            "configuration rewrote balances or a historical effect");
    require(catalog_test::apply(state, update, scratch) == changed &&
                state.risk_accounts[0].risk_version == RiskVersion{2},
            "same-request retry advanced risk version twice");
    update.identity.request_sequence = RequestSequence{2};
    require(catalog_test::apply(state, update, scratch).code == ApplyCode::stale_risk_state,
            "fresh stale update overwrote a newer version");
    const auto refuse = [&](auto change, const ApplyCode expected, const std::string& label = {}) {
        auto candidate = state;
        auto command = configure_risk(candidate, 3);
        change(std::get<ConfigureRiskLimits>(command.body));
        require(catalog_test::apply(candidate, command, scratch).code == expected &&
                    candidate.risk_accounts == state.risk_accounts &&
                    candidate.orders == state.orders && candidate.reservations == state.reservations &&
                    candidate.outbox_effects == state.outbox_effects,
                "configuration refusal changed economics or returned the wrong code " + label);
    };
    refuse([](auto& c) { c.max_reserved_quantity = QuantityLots::from_atoms(1); }, ApplyCode::insufficient_risk_capacity);
    refuse([](auto& c) { c.max_reserved_exposure = MoneyMicros::from_atoms(1); }, ApplyCode::insufficient_risk_capacity);
    refuse([](auto& c) { c.initial_margin_ppm = 500'000; c.risk_policy_version = RiskPolicyVersion{7}; }, ApplyCode::risk_reconfiguration_blocked);
    refuse([](auto& c) { c.risk_policy_version = RiskPolicyVersion{7}; }, ApplyCode::risk_reconfiguration_blocked);
    refuse([](auto& c) { c.risk_policy_version = RiskPolicyVersion{5}; }, ApplyCode::invalid_command);
    refuse([](auto& c) { c.initial_margin_ppm = 500'000; }, ApplyCode::invalid_command);
    refuse([](auto& c) { c.max_order_quantity = QuantityLots{}; }, ApplyCode::invalid_command);
    refuse([](auto& c) { c.max_order_quantity = QuantityLots::from_atoms(9); }, ApplyCode::invalid_command);
    refuse([](auto& c) { c.max_order_gross_notional = MoneyMicros{}; }, ApplyCode::invalid_command);
    refuse([](auto& c) { c.initial_margin_ppm = 0; }, ApplyCode::invalid_command);
    refuse([](auto& c) { c.maximum_fee_ppm = 1'000'001; }, ApplyCode::invalid_command);
    refuse([](auto& c) { c.initial_margin_ppm = 1'000'001; }, ApplyCode::invalid_command);
    refuse([](auto& c) { c.max_position_quantity = QuantityLots::from_atoms(-1); }, ApplyCode::invalid_command,
           "(negative position cap)");
    refuse([](auto& c) { c.reason_code = 0; }, ApplyCode::invalid_command, "(zero reason code)");
    refuse([](auto& c) { c.account_id = AccountId{}; }, ApplyCode::invalid_command);
    refuse([](auto& c) { c.instrument_id = InstrumentId{}; }, ApplyCode::invalid_command);
    refuse([](auto& c) { c.expected_risk_version = RiskVersion{99}; }, ApplyCode::stale_risk_state);
    refuse([](auto& c) { c.expected_kill_epoch = KillEpoch{99}; }, ApplyCode::stale_control_generation);
    refuse([](auto& c) { c.expected_executor_generation = ExecutorGeneration{99}; }, ApplyCode::stale_control_generation);
    auto exhausted = state;
    exhausted.risk_accounts[0].risk_version = RiskVersion{std::numeric_limits<std::uint64_t>::max()};
    require(catalog_test::apply(exhausted, configure_risk(exhausted, 3), scratch).code == ApplyCode::risk_version_exhausted,
            "configuration risk version wrapped");
    auto quiet = ready_state();
    quiet.risk_accounts[0].position_quantity = QuantityLots::from_atoms(3);
    auto rates = configure_risk(quiet, 1);
    auto& new_rates = std::get<ConfigureRiskLimits>(rates.body);
    new_rates.risk_policy_version = RiskPolicyVersion{7};
    new_rates.initial_margin_ppm = 100'000;
    new_rates.maximum_fee_ppm = 0;
    new_rates.max_reserved_exposure = MoneyMicros::from_atoms(500);
    require(catalog_test::apply(quiet, rates, scratch).code == ApplyCode::applied &&
                quiet.risk_accounts[0].position_quantity == QuantityLots::from_atoms(3) &&
                quiet.risk_accounts[0].initial_margin_ppm == 100'000 &&
                quiet.risk_accounts[0].maximum_fee_ppm == 0 &&
                quiet.risk_accounts[0].risk_policy_version == RiskPolicyVersion{7} &&
                quiet.risk_accounts[0].max_reserved_exposure == MoneyMicros::from_atoms(500),
            "quiescent rate update failed or overwrote position");
}

void test_risk_configuration_admission_and_capacity() {
    using namespace velocita::v2;
    ApplyScratch scratch;
    auto state = ready_state();
    auto configuration = configure_risk(state, 1);
    auto& c = std::get<ConfigureRiskLimits>(configuration.body);
    c.expected_risk_version = RiskVersion{};
    // Configure below full notional, but above margin+fee: notional and holds
    // are intentionally distinct limits. No fixture-injected risk survives.
    c.initial_margin_ppm = 100'000;
    c.max_reserved_exposure = MoneyMicros::from_atoms(300);
    state.risk_accounts = {};
    state.risk_account_count = 0;
    auto replayed = state;
    std::array<std::byte, kMaxKernelCommandBytes> command_bytes{};
    std::array<std::byte, kKernelResultBytes> result_bytes{};
    KernelCommandCodecScratch codec_scratch;
    const auto encoded = encode_kernel_command(configuration, command_bytes);
    require(encoded.error == KernelCodecError::none, "configuration admission encode failed");
    const auto configured = catalog_test::apply(state, configuration, scratch);
    require(configured.code == ApplyCode::applied &&
                catalog_test::replay(replayed, {command_bytes.data(), encoded.bytes_written},
                    scratch, codec_scratch, result_bytes).apply_result == configured &&
                replayed.risk_accounts == state.risk_accounts,
            "encoded configuration replay diverged from direct admission");
    const auto update = configure_risk(state, 2);
    require(catalog_test::apply(state, update, scratch).code == ApplyCode::applied &&
                state.risk_accounts[0].risk_version == RiskVersion{2},
            "fresh no-op configuration did not advance exactly one version");
    require(catalog_test::apply(state, authorize(2, 0xf1), scratch).code == ApplyCode::stale_risk_state,
            "configuration failed to invalidate an old decision's risk version");
    require(evaluate_new_decision(state, scratch, DecisionId{21}, 2, 3).code == ApplyCode::applied &&
                catalog_test::apply(state, authorize_decision(4, 0xf2, DecisionId{21}), scratch).code == ApplyCode::applied,
            "fresh authorization did not use command-configured risk limits");
    require(state.risk_accounts[0].reserved_margin.atoms() > 0 &&
                state.risk_accounts[0].reserved_margin.atoms() <
                    state.reservations[0].remaining_notional.atoms(),
            "hold budget was treated as gross notional");
    auto duplicate = configuration;
    duplicate.identity.payload_digest = digest(0xff);
    require(catalog_test::apply(state, duplicate, scratch).code == ApplyCode::duplicate_conflict,
            "configuration request accepted a conflicting payload digest");
    for (std::uint64_t i = 1; i < kMaxRiskAccounts; ++i) {
        auto create = configuration;
        create.identity.request_sequence = RequestSequence{10 + i};
        std::get<ConfigureRiskLimits>(create.body).account_id = AccountId{5 + i};
        require(catalog_test::apply(state, create, scratch).code == ApplyCode::applied,
                "risk account capacity filled prematurely");
    }
    auto full = configuration;
    full.identity.request_sequence = RequestSequence{20};
    std::get<ConfigureRiskLimits>(full.body).account_id = AccountId{99};
    const auto accounts = state.risk_accounts;
    require(catalog_test::apply(state, full, scratch).code == ApplyCode::risk_account_capacity_exceeded &&
                state.risk_accounts == accounts, "full risk arena mutated on refused creation");
    auto receipts_full = state;
    // Receipts are now reclaimable (architecture 8.6), so a filler of default
    // records would simply be swept and capacity would never be exceeded. Fill
    // with PINNED receipts instead - applied fill results, which are the
    // deduplication for a venue execution identity and are never forgotten while
    // an order could still accept quantity. That makes the arena genuinely full
    // and keeps this test about the capacity path rather than the sweep.
    for (std::size_t index = 0; index < kMaxRequestReceipts; ++index) {
        ApplyResult pinned;
        pinned.code = ApplyCode::applied;
        pinned.detail = ApplyResultDetail::fill(VenueId{9},
                                                ExecutionId{20'000 + index},
                                                QuantityLots::from_atoms(1));
        pinned.detail_kind = ApplyResultDetailKind::fill;
        pinned.metadata_words = {2, 20'000 + index};
        receipts_full.receipts[index] = {
            {CallerId{40}, RequestSequence{index + 1}, digest(0x77)}, pinned};
    }
    receipts_full.receipt_count = kMaxRequestReceipts;
    require(catalog_test::apply(receipts_full, configure_risk(receipts_full, 21), scratch).code ==
                ApplyCode::receipt_capacity_exceeded && receipts_full.risk_accounts == accounts,
            "configuration committed without room for its receipt");
}

// ---------------------------------------------------------------------------
// Serial replacement acceptance: the full requote lifecycle with a risk-limit
// update landing inside the cancel/reconcile window.
//
// Refusing native amend does not by itself satisfy G-018. G-018 constrains the
// amend/cancel-replace TRANSITION, and with native amend disabled the serial
// cancel -> reconcile -> fresh-authorize path IS that transition. It therefore
// has to be proven, not assumed, and proven while replicated risk configuration
// is changing underneath it - the case an operator actually creates when
// retuning limits on a live book.
//
// The matrix, in order:
//   1. a rate/policy change is refused while the cancelled order still holds risk;
//   2. an aggregate cap that would not cover the retained hold is refused;
//   3. a prospective per-order cap change is accepted and rewrites no balance,
//      no historical effect, and no position version;
//   4. a successor evaluated before the bump is refused as stale, not admitted
//      and not silently re-based onto the new version;
//   5. only final reconciliation releases the residual and frees the slot;
//   6. a freshly evaluated successor authorizes against the new configuration; and
//   7. every historical and current effect still reconstructs byte-for-byte
//      across snapshot restore and canonical command replay.
// ---------------------------------------------------------------------------
void test_serial_replacement_across_a_risk_limit_update() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;

    // Canonical replay leg. EVERY command that reaches apply is recorded,
    // including refused ones: an admitted refusal retains a receipt and advances
    // the global StateVersion, and `committed_directive_index` in an effect's
    // canonical bytes is exactly that StateVersion at authorization time. A
    // replay that skips refusals therefore reproduces different effect bytes,
    // which is a fidelity bug in the replay rather than in the kernel.
    //
    // Snapshot restore proves the state can be rebuilt; replay proves it can be
    // re-derived from the commands that produced it, which is the stronger
    // property and the one the outbox depends on.
    std::vector<Command> history;
    const auto admitted = [&](const Command& command) {
        const auto outcome = catalog_test::apply(state, command, scratch);
        history.push_back(command);
        return outcome;
    };

    const auto submit = admitted(authorize(2, 0x71));
    require(submit.code == ApplyCode::applied, "serial replacement submit failed");
    const auto submit_bytes =
        encode_payload(*materialize_order_effect(state, submit.effect_id())).bytes;

    // A successor evaluated NOW binds RiskVersion 1. The configuration update
    // below makes it stale, and it must never be quietly re-based.
    require(evaluate_new_decision(state, scratch, DecisionId{21}, 2, 3, &history)
                    .code == ApplyCode::applied,
            "successor decision was not retained");

    const auto cancel = admitted(request_cancel(1, 0x72, submit.cluster_order_id()));
    require(cancel.code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::cancel_pending &&
                state.reservations[0].state != ReservationState::released,
            "cancel did not leave a risk-bearing cancel-pending order");
    const auto cancel_bytes =
        encode_payload(*materialize_order_effect(state, cancel.effect_id())).bytes;

    const auto economics_unchanged = [&](const TradingState& before,
                                         const TradingState& after,
                                         const std::string& what) {
        require(before.risk_accounts == after.risk_accounts &&
                    before.reservations == after.reservations &&
                    before.orders == after.orders &&
                    before.outbox_effects == after.outbox_effects,
                what);
    };

    // 1. Rates and policy version cannot move while the reservation is unresolved.
    //    A cancel-pending order still owns real exposure, and re-rating it would
    //    revalue a hold taken under different terms.
    {
        const auto before = state;
        auto rates = configure_risk(state, 1);
        auto& body = std::get<ConfigureRiskLimits>(rates.body);
        body.risk_policy_version = RiskPolicyVersion{7};
        body.initial_margin_ppm = 500'000;
        require(admitted(rates).code == ApplyCode::risk_reconfiguration_blocked,
                "rate change was admitted while a cancelled order still held risk");
        economics_unchanged(before, state, "blocked rate change mutated economics");
    }

    // 2. An aggregate cap below the retained hold is refused, not clamped.
    {
        const auto before = state;
        auto tighten = configure_risk(state, 2);
        std::get<ConfigureRiskLimits>(tighten.body).max_reserved_exposure =
            MoneyMicros::from_atoms(1);
        require(admitted(tighten).code == ApplyCode::insufficient_risk_capacity,
                "aggregate cap below the retained hold was admitted");
        economics_unchanged(before, state, "refused cap change mutated economics");
    }

    // 3. A prospective per-order cap change is accepted mid-cancellation.
    const auto before_update = state;
    auto update = configure_risk(state, 3);
    std::get<ConfigureRiskLimits>(update.body).max_order_quantity =
        QuantityLots::from_atoms(3);
    require(admitted(update).code == ApplyCode::applied &&
                state.risk_accounts[0].risk_version == RiskVersion{2},
            "prospective cap change during cancellation was refused");
    require(state.risk_accounts[0].reserved_quantity ==
                    before_update.risk_accounts[0].reserved_quantity &&
                state.risk_accounts[0].reserved_margin ==
                    before_update.risk_accounts[0].reserved_margin &&
                state.risk_accounts[0].reserved_fee ==
                    before_update.risk_accounts[0].reserved_fee &&
                state.risk_accounts[0].position_version ==
                    before_update.risk_accounts[0].position_version &&
                state.reservations == before_update.reservations &&
                state.orders == before_update.orders,
            "configuration rewrote balances or order state mid-cancellation");
    require(encode_payload(*materialize_order_effect(state, submit.effect_id()))
                        .bytes == submit_bytes &&
                encode_payload(*materialize_order_effect(state, cancel.effect_id()))
                        .bytes == cancel_bytes,
            "configuration rewrote a historical effect's bytes");

    // 4. The successor bound RiskVersion 1 and must be refused as stale. This
    //    precedes the slot check on purpose: the specific economic reason is
    //    more useful than "the slot is busy", and the precedence is fixed so the
    //    reported reason is stable on every replica.
    {
        const auto before = state;
        require(admitted(authorize_decision(4, 0x73, DecisionId{21})).code ==
                    ApplyCode::stale_risk_state,
                "a successor bound to the old risk version was not refused");
        economics_unchanged(before, state, "stale successor mutated economics");
    }

    // 5. Only final reconciliation releases the residual.
    require(admitted(observe_order_event(1, 0x74, VenueObservationKind::cancelled,
                                         submit.cluster_order_id(),
                                         cancel.effect_id(), 2100))
                    .code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::reconciling &&
                state.reservations[0].state != ReservationState::released,
            "venue cancellation released risk before final evidence");
    {
        const auto before = state;
        require(admitted(authorize_decision(5, 0x75, DecisionId{21})).code ==
                    ApplyCode::stale_risk_state,
                "successor was admitted while reconciliation was outstanding");
        economics_unchanged(before, state, "successor attempt mutated economics");
    }

    auto proof = observe_order_event(2, 0x76,
                                     VenueObservationKind::cancel_reconciled,
                                     submit.cluster_order_id(), cancel.effect_id(),
                                     2101);
    auto& evidence = std::get<ObserveOrderEvent>(proof.body).observation;
    evidence.venue_order_id = VenueOrderId{70};
    evidence.quantity = state.reservations[0].filled_quantity;
    proof.identity.payload_digest = observation_fingerprint(evidence);
    require(admitted(proof).code == ApplyCode::applied &&
                state.reservations[0].state == ReservationState::released,
            "final reconciliation did not release the residual hold");

    // 6. A freshly evaluated successor authorizes against the new configuration.
    require(evaluate_new_decision(state, scratch, DecisionId{22}, 3, 6, &history)
                    .code == ApplyCode::applied,
            "fresh successor decision was not retained");
    const auto successor = admitted(authorize_decision(7, 0x77, DecisionId{22}));
    require(successor.code == ApplyCode::applied &&
                successor.cluster_order_id() != submit.cluster_order_id(),
            "fresh successor did not authorize after reconciliation");
    const auto successor_bytes =
        encode_payload(*materialize_order_effect(state, successor.effect_id())).bytes;

    // 7. Every effect in the lifecycle survives snapshot restore byte-for-byte.
    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    const auto encoded = encode_state_snapshot(state, snapshot);
    require(encoded.error == StateSnapshotError::none,
            "serial replacement state failed snapshot invariants");
    TradingState restored = catalog_test::state();
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({snapshot.data(), encoded.bytes_written}, restored,
                                  snapshot_scratch)
                    .error == StateSnapshotError::none,
            "serial replacement snapshot did not decode");
    require(encode_payload(*materialize_order_effect(restored, submit.effect_id()))
                        .bytes == submit_bytes &&
                encode_payload(*materialize_order_effect(restored, cancel.effect_id()))
                        .bytes == cancel_bytes &&
                encode_payload(*materialize_order_effect(restored,
                                                         successor.effect_id()))
                        .bytes == successor_bytes,
            "snapshot restore changed effect bytes across the serial lifecycle");
    require(restored.risk_accounts == state.risk_accounts &&
                restored.reservations == state.reservations &&
                restored.orders == state.orders,
            "snapshot restore changed serial-lifecycle economics");

    // 8. Canonical replay. Re-drive the recorded lifecycle through the encoded
    //    command path from an identical starting state, then require the same
    //    historical effect bytes AND the same canonical snapshot.
    auto replayed = ready_state();
    ApplyScratch replay_scratch;
    KernelCommandCodecScratch codec_scratch;
    std::array<std::byte, kMaxKernelCommandBytes> command_bytes{};
    std::array<std::byte, kKernelResultBytes> result_bytes{};
    for (const auto& command : history) {
        const auto encoded_command = encode_kernel_command(command, command_bytes);
        require(encoded_command.error == KernelCodecError::none,
                "serial lifecycle command did not encode canonically");
        require(catalog_test::replay(
                    replayed, {command_bytes.data(), encoded_command.bytes_written},
                    replay_scratch, codec_scratch, result_bytes)
                        .error == ReplayError::none,
                "serial lifecycle command failed canonical replay");
    }
    require(encode_payload(*materialize_order_effect(replayed, submit.effect_id()))
                    .bytes == submit_bytes &&
                encode_payload(*materialize_order_effect(replayed, cancel.effect_id()))
                        .bytes == cancel_bytes &&
                encode_payload(*materialize_order_effect(replayed,
                                                         successor.effect_id()))
                        .bytes == successor_bytes,
            "canonical replay changed historical effect bytes");
    std::array<std::byte, kMaxStateSnapshotBytes> replay_snapshot{};
    const auto replay_encoded = encode_state_snapshot(replayed, replay_snapshot);
    require(replay_encoded.error == StateSnapshotError::none &&
                replay_encoded.bytes_written == encoded.bytes_written &&
                std::equal(replay_snapshot.begin(),
                           replay_snapshot.begin() +
                               static_cast<std::ptrdiff_t>(replay_encoded.bytes_written),
                           snapshot.begin()),
            "canonical replay did not converge to the direct snapshot");
}

// ---------------------------------------------------------------------------
// Reclamation landing inside the cancel/reconcile window.
//
// The production shape: a cancel is outstanding at a slow venue while the edge
// keeps producing frames, so the arenas fill mid-cancellation. Compaction and
// the cancel lifecycle have only been tested apart; here they interact.
//
// The question that matters is not whether the cancelled order survives the
// sweep - it is unresolved, so it must - but whether a stuck slot can still be
// RECOVERED once the arenas are full. If the reconciliation that releases the
// residual itself needed the capacity that is exhausted, one slow venue response
// would wedge the kernel instead of degrading it, and the operator path in
// G-074a would become mandatory rather than a safety net.
// ---------------------------------------------------------------------------
void test_reclamation_during_cancellation_preserves_the_recovery_path() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;

    const auto submit = catalog_test::apply(state, authorize(2, 0x81), scratch);
    require(submit.code == ApplyCode::applied, "stuck-slot fixture failed");
    const auto submit_bytes =
        encode_payload(*materialize_order_effect(state, submit.effect_id())).bytes;
    const auto cancel = catalog_test::apply(
        state, request_cancel(1, 0x82, submit.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied, "stuck-slot cancel failed");
    const auto cancel_bytes =
        encode_payload(*materialize_order_effect(state, cancel.effect_id())).bytes;
    require(catalog_test::apply(state,
                  observe_order_event(1, 0x83, VenueObservationKind::cancelled,
                                      submit.cluster_order_id(), cancel.effect_id(),
                                      3100),
                  scratch)
                    .code == ApplyCode::applied &&
                state.orders[0].state == ClusterOrderState::reconciling,
            "stuck-slot fixture did not reach reconciling");
    const auto held = state.risk_accounts[0];

    // The edge keeps evaluating because market data does not stop. Once
    // unauthorized decisions became reclaimable this stopped being able to
    // exhaust the arena at all: each new actionable proposal for the occupied
    // slot supersedes the previous one, so at most the newest survives. The
    // property is now the stronger one - a stuck order cannot wedge evaluation -
    // rather than the earlier "evaluation halts but can still recover".
    std::uint64_t request = 3;
    std::uint64_t sequence = 2;
    for (std::uint64_t cycle = 1; cycle < 40; ++cycle) {
        require(evaluate_new_decision(state, scratch, DecisionId{40 + cycle},
                                      sequence++, request++)
                        .code == ApplyCode::applied,
                "a stuck order wedged evaluation: superseded proposals for its "
                "occupied slot were not reclaimed");
    }

    // The reconciling order is untouched by compaction pressure.
    require(state.order_count >= 1 &&
                state.orders[0].cluster_order_id == submit.cluster_order_id() &&
                state.orders[0].state == ClusterOrderState::reconciling &&
                state.reservations[0].state != ReservationState::released &&
                state.risk_accounts[0].reserved_quantity == held.reserved_quantity &&
                state.risk_accounts[0].reserved_margin == held.reserved_margin &&
                state.risk_accounts[0].reserved_fee == held.reserved_fee,
            "reclamation pressure disturbed a reconciling order's risk");
    require(state.orders[0].reservation_index < state.reservation_count &&
                state.reservations[state.orders[0].reservation_index]
                        .cluster_order_id == state.orders[0].cluster_order_id,
            "reclamation pressure broke the reconciling order's reservation pairing");
    require(encode_payload(*materialize_order_effect(state, submit.effect_id()))
                        .bytes == submit_bytes &&
                encode_payload(*materialize_order_effect(state, cancel.effect_id()))
                        .bytes == cancel_bytes,
            "reclamation pressure changed a reconciling order's effect bytes");

    // THE PROPERTY UNDER TEST: recovery does not depend on the arena that filled.
    // Final reconciliation consumes a request receipt and a venue-event receipt,
    // neither of which is the decision arena, so a stuck slot stays recoverable
    // while evaluation is failing closed.
    auto proof = observe_order_event(2, 0x84,
                                     VenueObservationKind::cancel_reconciled,
                                     submit.cluster_order_id(), cancel.effect_id(),
                                     3101);
    auto& evidence = std::get<ObserveOrderEvent>(proof.body).observation;
    evidence.venue_order_id = VenueOrderId{70};
    evidence.quantity = state.reservations[0].filled_quantity;
    proof.identity.payload_digest = observation_fingerprint(evidence);
    require(catalog_test::apply(state, proof, scratch).code == ApplyCode::applied &&
                state.reservations[0].state == ReservationState::released,
            "a stuck slot could not be reconciled while the arenas were full");
    // The orphaned submit effect is recorded as `incomplete` - outcome never
    // established - and must NOT be recorded as `acknowledged`, which would
    // claim evidence the venue never gave us. Checked before reclamation
    // removes the record.
    {
        bool saw_incomplete = false;
        for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
            const auto& effect = state.outbox_effects[index];
            if (effect.effect_id != submit.effect_id()) continue;
            saw_incomplete = effect.state == OutboxState::incomplete;
        }
        require(saw_incomplete,
                "orphaned effect was not settled honestly as incomplete");
    }

    // Once the residual is released the group becomes reclaimable, and the slot
    // is free for a fresh quote.
    require(evaluate_new_decision(state, scratch, DecisionId{99}, sequence++,
                                  request++)
                    .code == ApplyCode::applied,
            "evaluation did not continue after the stuck slot was reconciled");
    // Reclamation is on-demand: it runs only when an arena is full, and now that
    // superseded proposals are reclaimed the arena no longer fills here. So the
    // assertion is that the group is RECLAIMABLE - terminal, released, and with
    // every effect settled - not that a sweep happened to run. On-demand
    // reclamation itself is covered by the requote-cycle test.
    require(state.orders[0].state == ClusterOrderState::cancelled &&
                state.reservations[0].state == ReservationState::released,
            "the stuck slot did not become reclaimable after reconciliation");
    require(state.settled_incomplete_effect_count == 1,
            "final reconciliation did not settle exactly the one orphaned effect");

    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    require(encode_state_snapshot(state, snapshot).error == StateSnapshotError::none,
            "recovered state failed snapshot invariants");
}

// ---------------------------------------------------------------------------
// Risk configuration against every unresolved order state.
//
// Architecture section 8.1 requires that a rate or policy-version change be
// refused while ANY unresolved reservation exists for the account and
// instrument, naming unknown, reconciling and conflict explicitly. The
// implementing predicate keys on the reservation not being released rather than
// on the order's lifecycle state, which is the right axis - risk is what the
// rule protects - but it means the named states are only covered transitively.
// Pin each one, because a future refactor that switches the predicate to a state
// list would silently drop whichever state it forgot.
//
// The economic reason: re-rating an account with live exposure would revalue a
// hold taken under different terms. Caps are prospective and may move; rates are
// retroactive in effect and may not.
// ---------------------------------------------------------------------------
void test_risk_configuration_respects_every_unresolved_order_state() {
    using namespace velocita::v2;
    ApplyScratch scratch;

    const auto blocked_in = [&](const char* what, auto&& reach_state) {
        auto state = ready_state();
        const auto submit = catalog_test::apply(state, authorize(2, 0x91), scratch);
        require(submit.code == ApplyCode::applied, "unresolved-state fixture failed");
        reach_state(state, submit);
        require(state.reservations[0].state != ReservationState::released,
                std::string{what} + ": fixture did not leave risk unresolved");
        const auto before = state;

        auto rates = configure_risk(state, 1);
        auto& body = std::get<ConfigureRiskLimits>(rates.body);
        body.risk_policy_version = RiskPolicyVersion{7};
        body.initial_margin_ppm = 500'000;
        require(catalog_test::apply(state, rates, scratch).code ==
                    ApplyCode::risk_reconfiguration_blocked,
                std::string{what} + ": rate change was admitted over unresolved risk");
        require(state.risk_accounts == before.risk_accounts &&
                    state.reservations == before.reservations &&
                    state.orders == before.orders &&
                    state.outbox_effects == before.outbox_effects,
                std::string{what} + ": blocked rate change mutated economics");

        // A policy-version bump alone is equally blocked: it is the version that
        // gates which rates may later apply, so letting it move would strand the
        // hold on a version its own policy no longer describes.
        auto policy_only = configure_risk(state, 2);
        std::get<ConfigureRiskLimits>(policy_only.body).risk_policy_version =
            RiskPolicyVersion{7};
        require(catalog_test::apply(state, policy_only, scratch).code ==
                    ApplyCode::risk_reconfiguration_blocked,
                std::string{what} + ": policy-version bump was admitted");

        // Prospective per-order caps remain configurable: they bind future
        // authorizations and never revalue an existing hold.
        auto caps = configure_risk(state, 3);
        std::get<ConfigureRiskLimits>(caps.body).max_order_quantity =
            QuantityLots::from_atoms(3);
        require(catalog_test::apply(state, caps, scratch).code == ApplyCode::applied,
                std::string{what} + ": prospective cap change was refused");
        require(state.risk_accounts[0].reserved_margin ==
                        before.risk_accounts[0].reserved_margin &&
                    state.risk_accounts[0].reserved_fee ==
                        before.risk_accounts[0].reserved_fee &&
                    state.reservations == before.reservations,
                std::string{what} + ": cap change rewrote a live hold");

        // An aggregate cap that would not cover the retained hold is refused
        // rather than clamped, in every unresolved state.
        auto shrink = configure_risk(state, 4);
        std::get<ConfigureRiskLimits>(shrink.body).max_reserved_exposure =
            MoneyMicros::from_atoms(1);
        require(catalog_test::apply(state, shrink, scratch).code ==
                    ApplyCode::insufficient_risk_capacity,
                std::string{what} + ": aggregate cap below the hold was admitted");
        return state;
    };

    blocked_in("cancel_pending", [&](TradingState& state, const ApplyResult& submit) {
        require(catalog_test::apply(state, request_cancel(1, 0x92, submit.cluster_order_id()),
                      scratch)
                        .code == ApplyCode::applied,
                "cancel_pending fixture failed");
    });

    blocked_in("submission_unknown", [&](TradingState& state,
                                         const ApplyResult& submit) {
        require(catalog_test::apply(state,
                      observe_order_event(1, 0x93,
                                          VenueObservationKind::effect_unknown,
                                          submit.cluster_order_id(),
                                          submit.effect_id(), 4100),
                      scratch)
                        .code == ApplyCode::applied &&
                    state.orders[0].state == ClusterOrderState::submission_unknown,
                "submission_unknown fixture failed");
    });

    blocked_in("reconciling", [&](TradingState& state, const ApplyResult& submit) {
        const auto cancel = catalog_test::apply(
            state, request_cancel(1, 0x94, submit.cluster_order_id()), scratch);
        require(cancel.code == ApplyCode::applied &&
                    catalog_test::apply(state,
                          observe_order_event(1, 0x95,
                                              VenueObservationKind::cancelled,
                                              submit.cluster_order_id(),
                                              cancel.effect_id(), 4200),
                          scratch)
                            .code == ApplyCode::applied &&
                    state.orders[0].state == ClusterOrderState::reconciling,
                "reconciling fixture failed");
    });

    // `conflict` has no producer yet - B4 reconciliation will add one - so it is
    // constructed directly to pin the rule before its producer exists.
    blocked_in("conflict", [](TradingState& state, const ApplyResult&) {
        state.orders[0].state = ClusterOrderState::conflict;
    });

    // Once the risk is genuinely resolved, rates become configurable again.
    auto resolved = ready_state();
    const auto submit = catalog_test::apply(resolved, authorize(2, 0x96), scratch);
    require(catalog_test::apply(resolved,
                  observe_fill(1, 0x97, submit.cluster_order_id(),
                               submit.effect_id(), ExecutionId{4'300}, 2),
                  scratch)
                    .code == ApplyCode::applied &&
                resolved.reservations[0].state == ReservationState::released,
            "resolution fixture did not release the hold");
    auto rates = configure_risk(resolved, 1);
    auto& body = std::get<ConfigureRiskLimits>(rates.body);
    body.risk_policy_version = RiskPolicyVersion{7};
    body.initial_margin_ppm = 500'000;
    require(catalog_test::apply(resolved, rates, scratch).code == ApplyCode::applied,
            "rate change was refused after the hold was released");
    require(resolved.risk_accounts[0].position_quantity ==
                QuantityLots::from_atoms(2),
            "rate change disturbed the filled position");
}

// ---------------------------------------------------------------------------
// Multi-slot serial requoting: a bid and an ask cycling independently.
//
// This is what slot exclusivity was actually built for. A single-slot maker
// stops quoting entirely while a cancel reconciles; a two-sided maker must keep
// the other side live throughout, because the alternative - one stuck side
// silencing the book - is the failure that makes the whole G-018 trade
// unacceptable in practice.
//
// The properties that must hold:
//   * the two slots do not contend: occupying one never blocks the other;
//   * each slot's exclusivity is enforced independently;
//   * both orders' risk is held simultaneously and summed, not netted;
//   * reclamation of one side never disturbs the other's pairing or bytes; and
//   * the whole interleaving survives snapshot restore.
// ---------------------------------------------------------------------------
void test_two_sided_slots_requote_independently() {
    using namespace velocita::v2;
    auto state = two_sided_state();
    ApplyScratch scratch;

    // Both decisions are evaluated AFTER the two-sided policy is installed.
    // Feature 11 falls below rule 0's threshold of 15, so first-match falls
    // through to rule 1 and its sell template on slot 2. Feature 16 matches rule
    // 0 and its buy template on slot 1.
    require(evaluate_at_feature(state, scratch, DecisionId{30}, 11, 2, 3).code ==
                ApplyCode::applied,
            "ask decision was not retained");
    require(evaluate_at_feature(state, scratch, DecisionId{31}, 16, 3, 4).code ==
                ApplyCode::applied,
            "bid decision was not retained");

    const auto ask = catalog_test::apply(state, authorize_decision(5, 0xa1, DecisionId{30}),
                           scratch);
    require(ask.code == ApplyCode::applied, "ask slot did not authorize");
    const auto bid = catalog_test::apply(state, authorize_decision(6, 0xa2, DecisionId{31}),
                           scratch);
    require(bid.code == ApplyCode::applied,
            "bid slot was blocked by the occupied ask slot");

    const auto ask_bytes =
        encode_payload(*materialize_order_effect(state, ask.effect_id())).bytes;
    require(bid.cluster_order_id() != ask.cluster_order_id() &&
                state.order_count == 2 && state.reservation_count == 2,
            "two-sided quoting did not create two independent orders");
    // The premise the rest of the test rests on: first-match really did select
    // two different templates, so these are opposite sides on distinct slots and
    // not two copies of one quote.
    {
        const auto& first = state.decision_cold[1].proposal;
        const auto& second = state.decision_cold[2].proposal;
        require(first.order_slot != second.order_slot && first.side != second.side,
                "the two decisions did not resolve to distinct slots and sides");
    }

    // Both sides hold risk at once. Exposure sums; it is not netted across
    // sides, because two resting orders can both fill.
    require(state.risk_accounts[0].reserved_quantity ==
                QuantityLots::from_atoms(4),
            "two-sided reserved quantity was netted instead of summed");

    // Each slot is exclusive on its own. Re-authorizing either side is refused
    // for slot occupancy, and the other side is untouched.
    {
        const auto before = state;
        // Same request identity: receipt deduplication returns the original
        // result verbatim, before authorization logic is reached at all.
        require(catalog_test::apply(state, authorize_decision(5, 0xa1, DecisionId{30}), scratch) ==
                    ask,
                "ask request retry did not return its retained result");
        // Different request identity, same decision: authorization recognises the
        // decision was already consumed and replays its identities rather than
        // creating a second order on the slot.
        const auto replayed =
            catalog_test::apply(state, authorize_decision(9, 0xa3, DecisionId{30}), scratch);
        require(replayed.code == ApplyCode::already_applied &&
                    replayed.cluster_order_id() == ask.cluster_order_id() &&
                    replayed.effect_id() == ask.effect_id(),
                "re-authorizing a consumed decision did not replay its identities");
        require(state.orders == before.orders &&
                    state.reservations == before.reservations,
                "retry mutated economics");
    }

    // Cancel and reconcile the ask while the bid keeps quoting.
    const auto cancel = catalog_test::apply(
        state, request_cancel(1, 0xa4, ask.cluster_order_id()), scratch);
    require(cancel.code == ApplyCode::applied, "ask cancel failed");
    require(catalog_test::apply(state,
                  observe_order_event(1, 0xa5, VenueObservationKind::cancelled,
                                      ask.cluster_order_id(), cancel.effect_id(),
                                      5100),
                  scratch)
                    .code == ApplyCode::applied,
            "ask cancellation failed");

    // The bid is entirely unaffected by the ask's reconciliation.
    const auto bid_index = state.orders[0].cluster_order_id == bid.cluster_order_id()
                               ? 0u : 1u;
    require(state.orders[bid_index].state == ClusterOrderState::dispatch_authorized &&
                state.reservations[state.orders[bid_index].reservation_index]
                        .cluster_order_id == bid.cluster_order_id(),
            "reconciling the ask disturbed the bid");

    auto proof = observe_order_event(2, 0xa6,
                                     VenueObservationKind::cancel_reconciled,
                                     ask.cluster_order_id(), cancel.effect_id(),
                                     5101);
    auto& evidence = std::get<ObserveOrderEvent>(proof.body).observation;
    evidence.venue_order_id = VenueOrderId{70};
    evidence.quantity = QuantityLots{};
    proof.identity.payload_digest = observation_fingerprint(evidence);
    require(catalog_test::apply(state, proof, scratch).code == ApplyCode::applied,
            "ask reconciliation failed while the bid was live");

    // Only the ask's risk was released; the bid's hold is untouched.
    require(state.risk_accounts[0].reserved_quantity ==
                QuantityLots::from_atoms(2),
            "reconciling one side released the other side's risk");
    require(materialize_order_effect(state, bid.effect_id()).has_value(),
            "bid effect became unreconstructable while the ask reconciled");
    require(encode_payload(*materialize_order_effect(state, ask.effect_id()))
                    .bytes == ask_bytes,
            "reconciling the ask changed its historical effect bytes");

    // The ask slot is now free and requotes while the bid still rests.
    require(evaluate_at_feature(state, scratch, DecisionId{32}, 11, 4, 7).code ==
                ApplyCode::applied,
            "ask requote decision was not retained");
    // Sequence 10, not 8: RequestSequence must be monotonic per caller
    // (architecture 8.6), and this caller has already reached 9. A lower
    // sequence is now correctly refused as expired rather than executed, which
    // is exactly the protection the watermark exists to give.
    const auto requote = catalog_test::apply(state, authorize_decision(10, 0xa7, DecisionId{32}),
                               scratch);
    require(requote.code == ApplyCode::applied &&
                requote.cluster_order_id() != ask.cluster_order_id(),
            "ask slot did not requote after reconciliation");
    require(state.risk_accounts[0].reserved_quantity ==
                QuantityLots::from_atoms(4),
            "two-sided exposure did not return after the ask requoted");

    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    const auto encoded = encode_state_snapshot(state, snapshot);
    require(encoded.error == StateSnapshotError::none,
            "two-sided state failed snapshot invariants");
    TradingState restored = catalog_test::state();
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({snapshot.data(), encoded.bytes_written}, restored,
                                  snapshot_scratch)
                    .error == StateSnapshotError::none,
            "two-sided snapshot did not decode");
    require(restored.orders == state.orders &&
                restored.reservations == state.reservations &&
                restored.risk_accounts == state.risk_accounts,
            "snapshot restore changed two-sided economics");
}

// ---------------------------------------------------------------------------
// Unauthorized-decision retention.
//
// A live maker evaluates continuously and most frames produce no_action, so the
// decision arena fills with records no order will ever reference. Reclaiming
// order groups does not help: there may be no resolved group to free.
//
// Two distinct cases, with different justifications:
//
//   * STRUCTURALLY NON-AUTHORIZABLE. Every condition in the authorizability
//     predicate is an immutable property of the retained record - evaluation
//     mode, decision code, effective directive, resolution code, proposal
//     validity and self-consistent identity. A record failing any of them can
//     never become authorizable, whatever happens later, so it is safe to
//     reclaim with no horizon and no clock.
//
//   * SUPERSEDED. An actionable decision never authorized, for a slot where a
//     NEWER actionable decision exists. A maker only ever wants the latest quote
//     for a slot; resurrecting an older proposal would rest a price computed
//     from older market data, the same objection that already justifies refusing
//     a stale RiskVersion.
//
// Identity survives either way: applied evaluation receipts outlive the decision
// arena and retain the DecisionId, payload digest and original outcome, so
// conflict detection and exact replay still work after reclamation.
// ---------------------------------------------------------------------------
void test_unauthorizable_decisions_do_not_fill_the_arena() {
    using namespace velocita::v2;
    // Feature 5 is below the single rule's threshold of 10, so no rule matches
    // and every evaluation retains a no_action decision.
    auto state = ready_state(EvaluationMode::authoritative,
                             FeatureMicros::from_atoms(5));
    ApplyScratch scratch;
    require(state.decision_hot[0].effective_directive == Directive::no_action,
            "fixture did not produce a non-authorizable decision");

    std::uint64_t request = 2;
    for (std::uint64_t cycle = 1; cycle < 40; ++cycle) {
        require(evaluate_at_feature(state, scratch, DecisionId{100 + cycle}, 5,
                                    cycle + 1, request++)
                        .code == ApplyCode::applied,
                "no_action evaluation was refused: the decision arena filled with "
                "records no order can ever reference");
    }
    require(state.order_count == 0 && state.outbox_effect_count == 0,
            "no_action evaluations created economics");

    // Identity survives reclamation: re-presenting a reclaimed DecisionId with a
    // different payload must still conflict rather than read as fresh.
    auto altered = frame();
    altered.source_sequence = SourceSequence{1};
    altered.venue_sequence = VenueSequence{1};
    altered.raw_evidence_digest = digest(0x44);
    const auto conflicting = catalog_test::apply(
        state,
        {{CallerId{8}, RequestSequence{900}, digest(0xEE)},
         EvaluateDecisionFrame{DecisionId{101}, EvaluationMode::authoritative,
                               altered}},
        scratch);
    require(conflicting.code == ApplyCode::decision_id_conflict,
            "a reclaimed DecisionId lost its conflict detection");
}

void test_superseded_slot_decisions_are_reclaimed() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;

    // Occupy slot 1 so no later actionable decision for it can be authorized.
    const auto submit = catalog_test::apply(state, authorize(2, 0x51), scratch);
    require(submit.code == ApplyCode::applied, "supersession fixture failed");

    // Each of these is authorizable in shape and targets the occupied slot, so
    // each is retained and none can ever be consumed.
    std::uint64_t request = 3;
    for (std::uint64_t cycle = 1; cycle < 40; ++cycle) {
        require(evaluate_at_feature(state, scratch, DecisionId{200 + cycle}, 11,
                                    cycle + 1, request++)
                        .code == ApplyCode::applied,
                "actionable evaluation was refused: superseded proposals for an "
                "occupied slot were never reclaimed");
    }

    // The live order and its decision are untouched: a referenced decision is
    // never superseded.
    require(state.order_count == 1 &&
                state.orders[0].cluster_order_id == submit.cluster_order_id() &&
                materialize_order_effect(state, submit.effect_id()).has_value(),
            "supersession reclaimed a decision a live order still needs");
    require(state.decision_count > 0,
            "supersession reclaimed every decision including the newest");
}

// ---------------------------------------------------------------------------
// Finite identity horizons (architecture 8.6).
//
// Nothing reclaimed a request receipt, so the kernel accepted exactly
// kMaxRequestReceipts commands in its lifetime and then refused everything
// permanently. A second ceiling inside the same arena stopped fills being
// accepted, which strands existing risk rather than merely stopping new work.
//
// The safety rule that shapes the fix: an expired request must be REFUSED, never
// re-executed, because re-execution would be a second economic mutation. Telling
// "expired" apart from "never seen" is what the per-caller watermark is for.
// ---------------------------------------------------------------------------
void test_request_receipts_do_not_bound_the_kernel_lifetime() {
    using namespace velocita::v2;
    auto state = ready_state(EvaluationMode::authoritative,
                             FeatureMicros::from_atoms(5));
    ApplyScratch scratch;

    // Well past kMaxRequestReceipts. Each cycle emits two commands, so this
    // drives roughly three times the old lifetime ceiling.
    std::uint64_t request = 2;
    for (std::uint64_t cycle = 1; cycle < 400; ++cycle) {
        const auto outcome = evaluate_at_feature(
            state, scratch, DecisionId{5'000 + cycle}, 5, cycle + 1, request++);
        require(outcome.code == ApplyCode::applied,
                "the kernel stopped accepting commands at cycle " +
                    std::to_string(cycle) + " with code " +
                    std::to_string(static_cast<int>(outcome.code)) +
                    ": an identity arena still bounds its lifetime");
    }
    require(state.receipt_count <= kMaxRequestReceipts,
            "receipt arena exceeded its bound");
}

void test_an_expired_request_is_refused_never_re_executed() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;

    // Authorize once, then age its receipt out by driving unrelated traffic.
    const auto submit = catalog_test::apply(state, authorize(2, 0x61), scratch);
    require(submit.code == ApplyCode::applied, "expiry fixture failed");
    const auto orders_after_submit = state.order_count;

    std::uint64_t request = 3;
    for (std::uint64_t cycle = 1; cycle < 400; ++cycle) {
        require(evaluate_at_feature(state, scratch, DecisionId{6'000 + cycle}, 5,
                                    cycle + 1, request++)
                        .code == ApplyCode::applied,
                "expiry fixture could not drive the receipt arena");
    }

    // Re-present the original request identity. Whatever the kernel answers, it
    // must not create a second order: that is the property, and the answer is
    // secondary.
    const auto replayed = catalog_test::apply(state, authorize(2, 0x61), scratch);
    require(state.order_count == orders_after_submit,
            "re-presenting an aged request created a second order");
    // Three acceptable answers, all safe. The receipt may have survived, in
    // which case deduplication returns the retained ORIGINAL result verbatim -
    // authorization receipts are pinned while their order lives, because
    // materialize_order_effect needs them. It may have been reclaimed, in which
    // case the watermark refuses. Or authorization's own order-based
    // deduplication answers already_applied. What must never happen is a second
    // order, which is asserted above and is the actual property.
    require(replayed == submit || replayed.code == ApplyCode::already_applied ||
                replayed.code == ApplyCode::request_expired,
            "an aged request was neither deduplicated nor refused as expired");

    // A request BELOW its caller's watermark that was never seen must be refused
    // rather than treated as fresh, or expiry would silently permit replay.
    const auto never_seen = catalog_test::apply(state, authorize(1, 0x62), scratch);
    require(never_seen.code == ApplyCode::request_expired,
            "a request below the caller watermark was treated as fresh");
    require(state.order_count == orders_after_submit,
            "a below-watermark request mutated state");
}

// Resolved groups must release their venue-identity receipts. The result's
// existing DecisionId field supplies the zero-byte ownership link: while that
// decision still owns an order the receipt is pinned; after group reclamation
// the same receipt can be swept without allowing a duplicate to book against a
// live reservation.
void test_resolved_orders_release_venue_identity_capacity() {
    using namespace velocita::v2;
    auto state = two_sided_state();
    ApplyScratch scratch;

    std::uint64_t request = 3;
    std::uint64_t sequence = 2;
    std::uint64_t execution = 9'000;
    std::size_t completed = 0;
    for (std::uint64_t cycle = 1; cycle < 120; ++cycle) {
        const DecisionId decision{7'000 + cycle};
        require(evaluate_at_feature(state, scratch, decision, 11, sequence++, request++)
                    .code == ApplyCode::applied,
                "sustained venue-identity fixture could not evaluate");
        const auto submit = catalog_test::apply(
            state, authorize_decision(request++, 0x63, decision), scratch);
        require(submit.code == ApplyCode::applied,
                "sustained venue-identity fixture could not authorize cycle " +
                    std::to_string(cycle) + " with code " +
                    std::to_string(static_cast<int>(submit.code)));
        const auto fill_request = request++;
        const auto filled = catalog_test::apply(
            state,
            observe_fill(fill_request, 0x64, submit.cluster_order_id(),
                         submit.effect_id(), ExecutionId{execution++}, 2,
                         Side::sell, 11),
            scratch);
        require(filled.code == ApplyCode::applied &&
                    filled.decision_id == decision,
                "resolved orders did not release venue identity capacity or "
                "the fill lost its owning decision");
        const auto ack_request = request++;
        const Command acknowledge{
            {CallerId{2}, RequestSequence{ack_request}, digest(0x65)},
            AcknowledgeGatewayResults{RequestSequence{fill_request}}};
        require(catalog_test::apply(state, acknowledge, scratch).code == ApplyCode::applied,
                "sustained venue-identity fixture could not acknowledge its fill");
        ++completed;
    }
    const auto retained_venue_identities = std::count_if(
        state.receipts.begin(),
        state.receipts.begin() + static_cast<std::ptrdiff_t>(state.receipt_count),
        [](const RequestReceipt& receipt) {
            return receipt.result.code == ApplyCode::applied &&
                   (receipt.result.detail_kind == ApplyResultDetailKind::fill ||
                    receipt.result.detail_kind == ApplyResultDetailKind::venue_event);
        });
    require(completed == 119 &&
                retained_venue_identities <
                    static_cast<std::ptrdiff_t>(kMaxVenueEventReceipts),
            "resolved venue identities accumulated into a lifetime ceiling");
}

void test_gateway_acknowledgement_can_drain_a_full_receipt_arena() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;

    for (std::uint64_t sequence = 1; sequence <= kMaxRequestReceipts;
         ++sequence) {
        const auto refused = catalog_test::apply(
            state,
            observe_fill(sequence, 0x67, ClusterOrderId{91}, EffectId{92},
                         ExecutionId{10'000 + sequence}),
            scratch);
        require(refused.code == ApplyCode::venue_observation_not_applicable,
                "the acknowledgement saturation fixture changed refusal class");
    }
    require(state.receipt_count == kMaxRequestReceipts,
            "the acknowledgement saturation fixture did not fill the arena");

    const auto version_before_invalid = state.state_version;
    const Command invalid{
        {CallerId{2}, RequestSequence{kMaxRequestReceipts + 1}, digest(0x68)},
        AcknowledgeGatewayResults{
            RequestSequence{kMaxRequestReceipts + 1}}};
    require(catalog_test::apply(state, invalid, scratch).code ==
                ApplyCode::receipt_capacity_exceeded &&
                state.state_version == version_before_invalid &&
                state.receipt_count == kMaxRequestReceipts,
            "an invalid acknowledgement received privileged admission");

    const Command acknowledge{
        {CallerId{2}, RequestSequence{kMaxRequestReceipts + 1}, digest(0x69)},
        AcknowledgeGatewayResults{RequestSequence{kMaxRequestReceipts}}};
    require(catalog_test::apply(state, acknowledge, scratch).code == ApplyCode::applied,
            "a full receipt arena refused the acknowledgement needed to drain it");
    require(state.receipt_count < kMaxRequestReceipts,
            "the acknowledgement advanced but released no receipt capacity");
}

void test_acknowledged_duplicate_fills_do_not_exhaust_receipts() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = catalog_test::apply(state, authorize(2, 0x69), scratch);
    require(submit.code == ApplyCode::applied,
            "duplicate-fill fixture could not authorize");

    std::uint64_t request = 1'000;
    const auto original = observe_fill(
        request++, 0x6a, submit.cluster_order_id(), submit.effect_id(),
        ExecutionId{9'900}, 1, Side::buy, 11);
    require(catalog_test::apply(state, original, scratch).code == ApplyCode::applied,
            "duplicate-fill fixture could not book its first fill");
    const auto acknowledge = [&](const std::uint64_t through) {
        const auto sequence = request++;
        const Command command{
            {CallerId{2}, RequestSequence{sequence}, digest(0x6b)},
            AcknowledgeGatewayResults{RequestSequence{through}}};
        return catalog_test::apply(state, command, scratch).code;
    };
    require(acknowledge(original.identity.request_sequence.value()) ==
                ApplyCode::applied,
            "duplicate-fill fixture could not acknowledge its first fill");

    for (std::size_t duplicate = 0;
         duplicate < kMaxRequestReceipts + 16; ++duplicate) {
        auto replay = original;
        replay.identity.request_sequence = RequestSequence{request++};
        const auto result = catalog_test::apply(state, replay, scratch);
        require(result.code == ApplyCode::already_applied,
                "acknowledged duplicate fills exhausted receipt capacity");
        require(acknowledge(replay.identity.request_sequence.value()) ==
                    ApplyCode::applied,
                "duplicate-fill acknowledgement could not drain receipts");
    }

    const auto final = catalog_test::apply(
        state,
        observe_fill(request++, 0x6c, submit.cluster_order_id(),
                     submit.effect_id(), ExecutionId{9'901}, 1, Side::buy, 11),
        scratch);
    require(final.code == ApplyCode::applied,
            "duplicate redelivery prevented a fresh execution from booking");
}


// An `order_reconciled` answer meaning THE VENUE HOLDS NO SUCH ORDER. A zero
// `venue_order_id` is what expresses absence, and `validate` enforces that
// absence implies zero cumulative fill.
//
// The venue sequence is NOT optional even though this kind is synthetic - the
// identity check in `validate` requires one of every observation, and leaving it
// zero produced an `invalid_identity` refusal that looked exactly like an
// applicability refusal. That cost a long detour through the applicability gates
// before the cause turned out to be the fixture.
velocita::v2::Command reconciled_absent(
    const std::uint64_t request,
    const std::uint8_t marker,
    const velocita::v2::ClusterOrderId order_id,
    const velocita::v2::EffectId effect_id,
    const std::uint64_t venue_sequence) {
    using namespace velocita::v2;
    VenueObservation observation;
    observation.kind = VenueObservationKind::order_reconciled;
    observation.venue_id = VenueId{9};
    observation.venue_session_id = VenueSessionId{2};
    observation.venue_sequence = VenueSequence{venue_sequence};
    observation.cluster_order_id = order_id;
    observation.effect_id = effect_id;
    observation.venue_order_id = VenueOrderId{};
    observation.quantity = QuantityLots::from_atoms(0);
    observation.evidence_digest = digest(marker);
    return {{CallerId{4}, RequestSequence{request},
             observation_fingerprint(observation)},
            ObserveOrderEvent{observation}};
}

void test_a_resting_order_the_venue_does_not_hold_is_resolved() {
    using namespace velocita::v2;
    // ADR 0008, rules 2 and 3. Asking about a resting order is pointless if the
    // answer is refused, and it was: `order_reconciled` applied only to
    // `submission_unknown` and `dispatch_authorized`. An order the venue
    // acknowledged and then lost - a cancellation whose event never arrived -
    // could be queried forever and never resolved.
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = catalog_test::apply(state, authorize(2, 0xf1), scratch);
    require(submit.code == ApplyCode::applied, "submit fixture did not authorize");
    require(catalog_test::apply(state,
                  observe_order_event(1, 0xf2, VenueObservationKind::order_acknowledged,
                                      submit.cluster_order_id(), submit.effect_id(), 901),
                  scratch)
                    .code == ApplyCode::applied,
            "the fixture could not get the order acknowledged");
    require(state.orders[0].state == ClusterOrderState::live,
            "the fixture order is not resting");

    const auto answered = catalog_test::apply(
        state,
        reconciled_absent(2, 0xf7, submit.cluster_order_id(), submit.effect_id(), 902),
        scratch);
    require(answered.code == ApplyCode::applied,
            "the venue answered that it does not hold a resting order and the "
            "cluster refused to hear it");
    require(state.orders[0].state == ClusterOrderState::cancelled,
            "an absent resting order with no booked fills did not resolve");
    require(state.reservations[0].state == ReservationState::released,
            "an absent resting order with no booked fills kept its reservation");

    // And a resting order the venue DOES still hold is left alone - the query is
    // a confirmation, not an excuse to move something that is fine.
    auto healthy = ready_state();
    ApplyScratch other;
    const auto held = catalog_test::apply(healthy, authorize(2, 0xe1), other);
    require(catalog_test::apply(healthy,
                  observe_order_event(1, 0xe2, VenueObservationKind::order_acknowledged,
                                      held.cluster_order_id(), held.effect_id(), 801),
                  other)
                    .code == ApplyCode::applied,
            "the second fixture could not be acknowledged");
    const auto before = healthy;
    require(catalog_test::apply(healthy,
                  observe_order_event(2, 0xe3, VenueObservationKind::order_reconciled,
                                      held.cluster_order_id(), held.effect_id(), 802),
                  other)
                    .code == ApplyCode::applied,
            "a confirmation that the venue still holds the order was refused");
    require(healthy.orders[0].state == ClusterOrderState::live,
            "confirming a healthy resting order moved it");
    require_economics_unchanged(before, healthy,
                                "confirming a healthy resting order changed economics");
}

void test_an_unrequested_venue_cancellation_is_evidence_not_noise() {
    using namespace velocita::v2;
    // THE OTHER HALF OF G-060, and it is narrower than ADR 0007 first claimed.
    //
    // The accepted reading is that no emergency action reaches a venue except
    // through a committed command. Nothing in this system can PREVENT someone
    // acting at the venue's own console, so the ADR said such an action would
    // reconcile back through `ObserveOrderEvent`. Writing this test showed that
    // is false: `order_reconciled` can only say the venue STILL HOLDS an order,
    // and `cancel_reconciled` is tied to a cancel the cluster authored. There is
    // no evidence kind that says "the venue cancelled something you never asked
    // to cancel". See ADR 0007's amendment.
    //
    // What IS true, and is what this pins: the cluster refuses to act on that
    // evidence rather than absorbing it. A venue cannot unilaterally free
    // reserved risk by announcing a cancellation the cluster never authored.
    // That is the safety half. The visibility half is an open gap, recorded as
    // one.
    auto state = ready_state();
    ApplyScratch scratch;
    const auto submit = catalog_test::apply(state, authorize(2, 0xf1), scratch);
    require(submit.code == ApplyCode::applied, "submit fixture did not authorize");

    // The venue must have ACKNOWLEDGED the order first. Cancelling something the
    // venue never admitted to holding is not an out-of-band action, it is an
    // inapplicable event, and refusing it proves nothing about this case.
    require(catalog_test::apply(state,
                  observe_order_event(1, 0xf2, VenueObservationKind::order_acknowledged,
                                      submit.cluster_order_id(), submit.effect_id(), 901),
                  scratch)
                    .code == ApplyCode::applied,
            "the fixture could not get the order acknowledged");
    require(state.orders[0].state == ClusterOrderState::live,
            "the fixture order is not resting at the venue");

    const auto before = state;
    const auto outcome = catalog_test::apply(
        state,
        observe_order_event(2, 0xf4, VenueObservationKind::cancelled,
                            submit.cluster_order_id(), submit.effect_id(), 902),
        scratch);

    // ADR 0008. The venue telling us unprompted is EVIDENCE, and refusing it left
    // the cluster believing a dead order was resting - reservation held, slot
    // occupied, book silently wrong. Exchange-initiated cancellation is routine
    // (a post-only order that would cross, self-trade prevention, a halt), so
    // this fires in normal operation and not only from a console.
    //
    // It moves to `reconciling`, which is exactly what an AUTHORED cancel
    // produces: `cancelled` is a notification in this design, not terminal
    // evidence, and settlement comes from cumulative evidence afterwards. Not
    // `conflict` - that is terminal-but-stuck and blocks a resume under G-059,
    // so a routine post-only rejection would wedge trading until a human cleared
    // it, and a safety rule that fires constantly gets disabled.
    require(outcome.code == ApplyCode::applied,
            "an unrequested venue cancellation was refused, leaving the cluster "
            "believing a dead order was still resting");
    require(state.orders[0].state == ClusterOrderState::reconciling,
            "an unrequested venue cancellation did not put the order in doubt");

    // THE SAFETY HALF IS UNCHANGED AND IS THE POINT. Accepting the notification
    // must not release anything: the venue cannot free reserved risk by
    // announcing a cancellation, only cumulative evidence can settle it.
    require_economics_unchanged(before, state,
                                "an unrequested venue cancellation released "
                                "reserved risk on the venue's word alone");
    require(state.reservations[0].state != ReservationState::released,
            "an unrequested venue cancellation released the reservation");

    // And it is now in doubt, so a resume is blocked until it is settled - the
    // divergence is visible rather than silent, which is the whole change.
    state.strategy_mode = StrategyMode::halted;
    const Command resume{{CallerId{9}, RequestSequence{77}, digest(0x5A)},
                         SetStrategyMode{StrategyMode::normal, state.kill_epoch,
                                         state.executor_generation}};
    require(catalog_test::apply(state, resume, scratch).code == ApplyCode::reconciliation_required,
            "trading could resume while an order's fate was unknown");
}

velocita::v2::Command mass_cancel(const velocita::v2::TradingState& state,
                                  const std::uint64_t request,
                                  const std::uint8_t marker,
                                  const velocita::v2::MassCancelScope scope,
                                  const velocita::v2::AccountId account = {},
                                  const velocita::v2::InstrumentId instrument = {}) {
    using namespace velocita::v2;
    return {{CallerId{9}, RequestSequence{request}, digest(marker)},
            RequestMassCancel{scope, account, instrument, state.kill_epoch,
                              state.executor_generation}};
}

void test_mass_cancel_pulls_every_cancellable_order_in_scope() {
    using namespace velocita::v2;
    // ADR 0007. One command, one log entry, applied atomically - because the
    // alternative (N individual cancels on the control lane) can drain the
    // four-block reserve it shares with the kill path, and a mass-cancel that
    // stops half way leaves orders live while the operator believes the book is
    // flat.
    auto state = ready_state();
    ApplyScratch scratch;
    std::vector<ClusterOrderId> live;
    for (std::uint64_t slot = 1; slot <= 3; ++slot) {
        const DecisionId decision{20 + slot};
        append_submit_decision(state, decision, 11, 1, LogicalOrderSlot{static_cast<std::uint16_t>(slot)});
        const auto authorized = catalog_test::apply(
            state, authorize_decision(40 + slot, static_cast<std::uint8_t>(0x90 + slot),
                                      decision),
            scratch);
        require(authorized.code == ApplyCode::applied,
                "the fixture could not authorize order " + std::to_string(slot));
        live.push_back(authorized.cluster_order_id());
    }
    require(state.order_count == 3, "the fixture did not produce three orders");

    const auto before_effects = state.outbox_effect_count;
    const auto outcome = catalog_test::apply(state, mass_cancel(state, 90, 0xA1, MassCancelScope::all),
                               scratch);
    require(outcome.code == ApplyCode::applied,
            "a mass cancel over three live orders was refused");
    require(outcome.mass_cancelled() == 3,
            "the result reported " + std::to_string(outcome.mass_cancelled()) +
                " cancels rather than three");

    for (const auto id : live) {
        const ClusterOrder* order = nullptr;
        for (std::size_t i = 0; i < state.order_count; ++i) {
            if (state.orders[i].cluster_order_id == id) order = &state.orders[i];
        }
        require(order != nullptr && order->state == ClusterOrderState::cancel_pending,
                "an order in scope was not left cancel-pending");
    }
    require(state.outbox_effect_count == before_effects + 3,
            "the mass cancel did not produce one cancel effect per order");
    for (std::size_t i = before_effects; i < state.outbox_effect_count; ++i) {
        require(state.outbox_effects[i].operation == EffectOperation::cancel,
                "the mass cancel emitted an effect that was not a cancel");
    }
}

// The kill path's answer has to leave the reducer: a committed mass-cancel
// result must encode for the operator, and a replica retaining its receipt must
// still snapshot, restore and answer an exact retry identically.
void test_mass_cancel_result_survives_codec_snapshot_and_retry() {
    using namespace velocita::v2;
    for (const std::uint64_t orders : {std::uint64_t{0}, std::uint64_t{2}}) {
        auto state = ready_state();
        ApplyScratch scratch;
        for (std::uint64_t slot = 1; slot <= orders; ++slot) {
            const DecisionId decision{20 + slot};
            append_submit_decision(state, decision, 11, 1,
                                   LogicalOrderSlot{static_cast<std::uint16_t>(slot)});
            require(catalog_test::apply(state, authorize_decision(40 + slot,
                        static_cast<std::uint8_t>(0x90 + slot), decision), scratch).code ==
                        ApplyCode::applied, "mass-cancel codec fixture could not authorize");
        }
        // Every fixture order is live and in scope for `all`, so each is cancelled.
        const std::array<std::uint64_t, 3> expected{orders, orders, 0};
        const auto command = mass_cancel(state, 90, 0xA5, MassCancelScope::all);
        const auto outcome = catalog_test::apply(state, command, scratch);
        const auto context = " (" + std::to_string(orders) + " orders)";
        require(outcome.code == ApplyCode::applied &&
                    outcome.detail_kind == ApplyResultDetailKind::mass_cancel &&
                    outcome.detail.words == expected,
                "mass-cancel fixture did not report its in-scope/cancelled/skipped counts" + context);

        std::array<std::byte, kKernelResultBytes> result_bytes{};
        const auto encoded = encode_kernel_result(outcome, result_bytes);
        ApplyResult decoded;
        require(encoded.error == KernelCodecError::none &&
                    decode_kernel_result({result_bytes.data(), encoded.bytes_written}, decoded).error ==
                        KernelCodecError::none && decoded == outcome,
                "a committed mass-cancel result did not round-trip the result codec" + context);

        std::array<std::byte, kMaxStateSnapshotBytes> first{}, second{};
        const auto saved = encode_state_snapshot(state, first);
        require(saved.error == StateSnapshotError::none,
                "state retaining a mass-cancel receipt did not snapshot" + context);
        TradingState restored = catalog_test::state();
        StateSnapshotScratch snapshot_scratch;
        require(decode_state_snapshot({first.data(), saved.bytes_written}, restored,
                                      snapshot_scratch).error == StateSnapshotError::none,
                "a mass-cancel snapshot did not restore" + context);
        const auto again = encode_state_snapshot(restored, second);
        require(again.error == StateSnapshotError::none && again.bytes_written == saved.bytes_written &&
                    std::equal(first.begin(), first.begin() + static_cast<std::ptrdiff_t>(saved.bytes_written),
                               second.begin()),
                "a restored mass-cancel snapshot was not byte-identical" + context);
        const auto version = restored.state_version;
        require(catalog_test::apply(restored, command, scratch) == outcome &&
                    restored.state_version == version,
                "a restored replica answered the exact mass-cancel retry differently" + context);
    }
}

void test_mass_cancel_detail_is_validated_by_both_codecs() {
    using namespace velocita::v2;
    ApplyResult result;
    result.code = ApplyCode::applied;
    result.detail_kind = ApplyResultDetailKind::mass_cancel;
    result.detail = ApplyResultDetail::mass_cancel(3, 2, 1);
    const auto retaining = [](const ApplyResult& retained_result) {
        TradingState value = catalog_test::state();
        value.state_version = 5;
        value.caller_cursor_count = 1;
        value.caller_cursors[0] = {CallerId{1}, {}, {}};
        value.receipt_count = 1;
        value.receipts[0] = {{CallerId{1}, RequestSequence{77}, digest(0x32)}, retained_result};
        return value;
    };
    std::array<std::byte, kKernelResultBytes> bytes{};
    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    require(encode_kernel_result(result, bytes).error == KernelCodecError::none &&
                encode_state_snapshot(retaining(result), snapshot).error == StateSnapshotError::none,
            "a valid mass-cancel detail was refused");
    const auto refused = [&](auto change, const std::string& what) {
        auto invalid = result;
        change(invalid);
        require(encode_kernel_result(invalid, bytes).error == KernelCodecError::invalid_value,
                "result codec accepted " + what);
        require(encode_state_snapshot(retaining(invalid), snapshot).error ==
                    StateSnapshotError::invalid_state,
                "snapshot accepted " + what);
    };
    // The reducer emits this detail only on `applied`, and every in-scope order
    // is exactly one of cancelled or skipped.
    refused([](ApplyResult& r) { r.code = ApplyCode::outbox_capacity_exceeded; },
            "a mass-cancel detail on a refusal");
    refused([](ApplyResult& r) { r.detail.words[1] = 3; }, "cancelled + skipped above in-scope");
    refused([](ApplyResult& r) { r.detail.words[2] = 0; }, "cancelled + skipped below in-scope");
    refused([](ApplyResult& r) {
                r.detail = ApplyResultDetail::mass_cancel(kMaxClusterOrders + 1, kMaxClusterOrders + 1, 0);
            }, "more orders in scope than the order arena holds");
    refused([](ApplyResult& r) {
                r.detail = ApplyResultDetail::mass_cancel(1, std::numeric_limits<std::uint64_t>::max(), 2);
            }, "a wrapping cancelled + skipped");
}

void test_mass_cancel_skips_the_uncancellable_without_failing() {
    using namespace velocita::v2;
    // An already-filled order is not a FAILURE of a mass cancel, it is simply
    // outside the cancellable set - and a strict reading would make the command
    // useless in the one situation it exists for, since there is nearly always a
    // filled or conflicted order in the book when things go wrong.
    //
    // What "atomic" therefore means: the cancellable set is decided up front,
    // and either all of THAT set is cancelled or none of it is.
    auto state = ready_state();
    ApplyScratch scratch;
    append_submit_decision(state, DecisionId{21}, 11, 1, LogicalOrderSlot{1});
    const auto first = catalog_test::apply(state, authorize_decision(41, 0x91, DecisionId{21}), scratch);
    require(first.code == ApplyCode::applied, "fixture order one failed");
    append_submit_decision(state, DecisionId{22}, 11, 1, LogicalOrderSlot{2});
    const auto second = catalog_test::apply(state, authorize_decision(42, 0x92, DecisionId{22}), scratch);
    require(second.code == ApplyCode::applied, "fixture order two failed");

    // Put the first beyond cancelling, by hand: the point under test is the
    // reducer's selection rule, not the route that produced the state.
    for (std::size_t i = 0; i < state.order_count; ++i) {
        if (state.orders[i].cluster_order_id == first.cluster_order_id()) {
            state.orders[i].state = ClusterOrderState::filled;
        }
    }

    const auto outcome = catalog_test::apply(state, mass_cancel(state, 90, 0xA2, MassCancelScope::all),
                               scratch);
    require(outcome.code == ApplyCode::applied,
            "a mass cancel refused because one order was already filled");
    require(outcome.mass_cancelled() == 1,
            "the mass cancel did not cancel exactly the cancellable order");
    require(outcome.mass_skipped() == 1,
            "the result did not report the order it could not cancel");

    for (std::size_t i = 0; i < state.order_count; ++i) {
        if (state.orders[i].cluster_order_id == first.cluster_order_id()) {
            require(state.orders[i].state == ClusterOrderState::filled,
                    "the mass cancel disturbed an order it could not cancel");
        }
        if (state.orders[i].cluster_order_id == second.cluster_order_id()) {
            require(state.orders[i].state == ClusterOrderState::cancel_pending,
                    "the cancellable order was not cancelled");
        }
    }
}

void test_mass_cancel_refuses_rather_than_half_finishing() {
    using namespace velocita::v2;
    // THE PROPERTY THE SINGLE COMMAND EXISTS FOR. If the outbox cannot hold a
    // cancel for every order in the cancellable set, the whole command is
    // refused and NOTHING moves. A partial mass cancel is the worst outcome
    // available: some orders live, no record of the intent to cancel them, and
    // an operator who has been told the book is flat.
    auto state = ready_state();
    ApplyScratch scratch;
    for (std::uint64_t slot = 1; slot <= 3; ++slot) {
        const DecisionId decision{20 + slot};
        append_submit_decision(state, decision, 11, 1, LogicalOrderSlot{static_cast<std::uint16_t>(slot)});
        require(catalog_test::apply(state, authorize_decision(40 + slot,
                                                static_cast<std::uint8_t>(0x90 + slot),
                                                decision),
                      scratch)
                    .code == ApplyCode::applied,
                "fixture order failed");
    }

    // Fill the outbox so that fewer free slots remain than orders to cancel.
    state.outbox_effect_count = state.outbox_effects.size() - 1;

    const auto snapshot = state;
    const auto outcome = catalog_test::apply(state, mass_cancel(state, 90, 0xA3, MassCancelScope::all),
                               scratch);
    require(outcome.code == ApplyCode::outbox_capacity_exceeded,
            "a mass cancel that could not fit was not refused");
    // Economics, not bytes. A refusal is REQUIRED to move state - it retains a
    // request receipt, which is what makes the refusal itself replayable and
    // attributable. What must not move is anything economic.
    require_economics_unchanged(snapshot, state,
                                "a refused mass cancel still changed economics, "
                                "so it was not atomic");
    for (std::size_t i = 0; i < state.order_count; ++i) {
        require(state.orders[i].state == snapshot.orders[i].state,
                "a refused mass cancel still moved an order's state");
    }
}

void test_mass_cancel_is_fenced_like_every_other_control_action() {
    using namespace velocita::v2;
    // The expected kill epoch and executor generation are carried for the same
    // reason SetStrategyMode carries them: an operator acting on a stale view of
    // authority must not have that action silently applied to a newer one.
    auto state = ready_state();
    ApplyScratch scratch;
    append_submit_decision(state, DecisionId{21}, 11, 1, LogicalOrderSlot{1});
    require(catalog_test::apply(state, authorize_decision(41, 0x91, DecisionId{21}), scratch).code ==
                ApplyCode::applied,
            "fixture order failed");

    auto stale = mass_cancel(state, 90, 0xA4, MassCancelScope::all);
    std::get<RequestMassCancel>(stale.body).expected_kill_epoch =
        KillEpoch{state.kill_epoch.value() + 1};
    const auto snapshot = state;
    require(catalog_test::apply(state, stale, scratch).code == ApplyCode::stale_control_generation,
            "a mass cancel against a stale kill epoch was applied");
    require_economics_unchanged(snapshot, state,
                                "a stale-fenced mass cancel still changed economics");
    for (std::size_t i = 0; i < state.order_count; ++i) {
        require(state.orders[i].state == snapshot.orders[i].state,
                "a stale-fenced mass cancel still moved an order's state");
    }
}

void test_authorization_requires_the_installed_catalog_context() {
    using namespace velocita::v2;
    const auto attempt = [](const VerifiedExecutionCatalog* catalog) {
        auto state = ready_state();
        const auto before_version = state.state_version;
        ApplyScratch scratch;
        bool unavailable = false;
        try {
            (void)velocita::v2::apply(state, authorize(2, 0xf8), scratch, catalog);
        } catch (const CatalogDependencyUnavailable&) {
            unavailable = true;
        }
        require(unavailable && state.state_version == before_version &&
                    state.reservation_count == 0 && state.order_count == 0 &&
                    state.outbox_effect_count == 0,
                "missing or wrong catalog context reached authorization mutation");
    };

    attempt(nullptr);
    const auto fixture = ready_state();
    const std::array other_rows{catalog_test::row(
        AccountId{6}, fixture.active_executable_policy.instrument,
        fixture.active_executable_policy.venue)};
    const auto other = catalog_test::catalog(other_rows);
    attempt(&other);
}

void test_bound_effect_keeps_historical_catalog_versions() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto authorization = catalog_test::apply(state, authorize(2, 0xf9), scratch);
    require(authorization.code == ApplyCode::applied,
            "historical binding fixture did not authorize");
    require(execution_catalog_bindings_valid(state, catalog_test::standard_catalog()),
            "reachable authorized state failed catalog recovery validation");
    for (unsigned corruption = 0; corruption < 6; ++corruption) {
        auto changed = state;
        auto& proposal = changed.decision_cold[0].proposal;
        switch (corruption) {
            case 0: changed.active_executable_policy.instrument.maximum_price =
                        PriceTicks::from_atoms(101); break;
            case 1: changed.active_executable_policy.venue.supports_post_only = 0; break;
            case 2: proposal.instrument_spec_version = InstrumentSpecVersion{99}; break;
            case 3: proposal.notional = MoneyMicros::from_atoms(1); break;
            case 4: proposal.time_in_force = TimeInForce::fill_or_kill; break;
            case 5: proposal.quantity = QuantityLots::from_atoms(11);
                    proposal.notional = MoneyMicros::from_atoms(proposal.price.atoms() * 1100);
                    break;
        }
        require(!execution_catalog_bindings_valid(changed, catalog_test::standard_catalog()),
                "catalog recovery validation accepted contradictory retained metadata");
    }

    const auto bound = materialize_bound_order_effect(
        state, authorization.effect_id(), catalog_test::standard_catalog());
    require(bound.has_value() && bound->catalog_root == state.execution_catalog_root &&
                bound->instrument_spec_version == InstrumentSpecVersion{7} &&
                bound->venue_capabilities_version == VenueCapabilitiesVersion{8},
            "bound effect omitted its retained catalog identity");

    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(state, bytes);
    TradingState restored;
    StateSnapshotScratch snapshot_scratch;
    require(encoded.error == StateSnapshotError::none &&
                decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                                      snapshot_scratch).error ==
                    StateSnapshotError::none,
            "historical binding fixture did not survive snapshot restore");
    const auto restored_bound = materialize_bound_order_effect(
        restored, authorization.effect_id(), catalog_test::standard_catalog());
    require(restored_bound.has_value() &&
                restored_bound->catalog_root == bound->catalog_root &&
                restored_bound->instrument_spec_version ==
                    bound->instrument_spec_version &&
                restored_bound->venue_capabilities_version ==
                    bound->venue_capabilities_version,
            "snapshot restore changed historical catalog binding");

    // Historical binding comes from the retained cold proposal, never from a
    // subsequently changed active-policy projection.
    state.active_executable_policy.instrument.instrument_spec_version =
        InstrumentSpecVersion{99};
    state.active_executable_policy.venue.venue_capabilities_version =
        VenueCapabilitiesVersion{99};
    const auto after_policy_change = materialize_bound_order_effect(
        state, authorization.effect_id(), catalog_test::standard_catalog());
    require(after_policy_change.has_value() &&
                after_policy_change->instrument_spec_version ==
                    InstrumentSpecVersion{7} &&
                after_policy_change->venue_capabilities_version ==
                    VenueCapabilitiesVersion{8},
            "materialization borrowed versions from changed active policy");

    const std::array missing_rows{catalog_test::row(
        AccountId{6}, restored.active_executable_policy.instrument,
        restored.active_executable_policy.venue)};
    const auto missing = catalog_test::catalog(missing_rows);
    require(!materialize_bound_order_effect(
                 restored, authorization.effect_id(), missing).has_value(),
            "bound effect accepted a catalog with the wrong installed root");
    restored.execution_catalog_root = missing.root();
    require(!materialize_bound_order_effect(
                 restored, authorization.effect_id(), missing).has_value(),
            "bound effect accepted a matching root with no historical row");
}

// ---------------------------------------------------------------------------
// Position limits. The oracle below is written from the contract, in 128-bit
// arithmetic, and never consults the reducer: P is the signed position, R the
// open quantity on BOTH sides, q the proposal quantity.
enum class PositionExpectation { allowed, refused, overflow };

PositionExpectation expected_position_outcome(const velocita::v2::Side side,
                                              const std::int64_t position,
                                              const std::int64_t reserved,
                                              const std::int64_t quantity,
                                              const std::int64_t maximum,
                                              const bool allow_short) {
    const __int128 p = position, r = reserved, q = quantity, m = maximum;
    if (side == velocita::v2::Side::buy) {
        const __int128 after = p + r + q;
        if (after > std::numeric_limits<std::int64_t>::max()) return PositionExpectation::overflow;
        return after <= m ? PositionExpectation::allowed : PositionExpectation::refused;
    }
    const __int128 after = p - r - q;
    if (after < std::numeric_limits<std::int64_t>::min()) return PositionExpectation::overflow;
    const __int128 floor = allow_short ? -m : 0;
    return after >= floor ? PositionExpectation::allowed : PositionExpectation::refused;
}

void test_position_limit_matrix() {
    using namespace velocita::v2;
    struct Case {
        const char* name;
        Side side;
        std::int64_t position;
        std::int64_t reserved;
        std::int64_t maximum;
        bool allow_short;
    };
    constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
    constexpr auto kMin = std::numeric_limits<std::int64_t>::min();
    const std::array<Case, 22> cases{{
        {"flat buy inside the cap", Side::buy, 0, 0, 10, false},
        {"buy lands exactly on the cap", Side::buy, 6, 2, 10, false},
        {"buy one lot over the cap", Side::buy, 7, 2, 10, false},
        {"open orders alone push a buy over", Side::buy, 0, 3, 4, false},
        {"zero cap refuses a flat buy", Side::buy, 0, 0, 0, true},
        {"zero cap still lets a short cover", Side::buy, -4, 0, 0, true},
        {"long above a lowered cap cannot add", Side::buy, 6, 0, 3, false},
        {"short buy inside the cap", Side::buy, -4, 0, 10, true},
        {"long-only sell reduces a long", Side::sell, 5, 0, 10, false},
        {"long-only sell lands exactly flat", Side::sell, 2, 0, 10, false},
        {"long-only sell would go short", Side::sell, 1, 0, 10, false},
        {"long-only flat sell", Side::sell, 0, 0, 10, false},
        {"open buys count against a sell too", Side::sell, 4, 3, 10, false},
        {"long above a lowered cap may reduce", Side::sell, 6, 0, 3, false},
        {"shorting allowed inside the cap", Side::sell, 0, 0, 10, true},
        {"short lands exactly on the cap", Side::sell, -8, 0, 10, true},
        {"short one lot past the cap", Side::sell, -9, 0, 10, true},
        {"zero cap refuses shorting even when allowed", Side::sell, 0, 0, 0, true},
        {"zero cap long-only reducing sell", Side::sell, 3, 0, 0, false},
        {"buy overflow", Side::buy, kMax - 1, 0, 10, false},
        {"sell overflow", Side::sell, kMin + 1, 0, 10, true},
        {"open orders plus quantity overflow", Side::buy, kMax - 3, 2, kMax, false},
    }};
    for (const auto& c : cases) {
        auto state = ready_state();
        auto& risk = state.risk_accounts[0];
        risk.position_quantity = QuantityLots::from_atoms(c.position);
        risk.reserved_quantity = QuantityLots::from_atoms(c.reserved);
        risk.max_position_quantity = QuantityLots::from_atoms(c.maximum);
        risk.allow_short = c.allow_short;
        state.decision_cold[0].proposal.side = c.side;
        const auto quantity = state.decision_cold[0].proposal.quantity.atoms();
        const auto expected = expected_position_outcome(c.side, c.position, c.reserved,
                                                        quantity, c.maximum, c.allow_short);
        const auto before = state;
        ApplyScratch scratch;
        const auto outcome = catalog_test::apply(state, authorize(2, 0x93), scratch);
        const std::string context = std::string("position case '") + c.name + "'";
        if (expected == PositionExpectation::allowed) {
            require(outcome.code == ApplyCode::applied, context + " was refused");
            require(state.risk_accounts[0].reserved_quantity.atoms() == c.reserved + quantity &&
                        state.order_count == before.order_count + 1,
                    context + " did not reserve exactly the proposal quantity");
            continue;
        }
        require(outcome.code == (expected == PositionExpectation::overflow
                                     ? ApplyCode::risk_arithmetic_overflow
                                     : ApplyCode::position_limit_exceeded),
                context + " returned code " + std::to_string(static_cast<int>(outcome.code)));
        require_economics_unchanged(before, state, context + " mutated economics");
        require(state.risk_accounts == before.risk_accounts &&
                    state.reservations == before.reservations && state.orders == before.orders &&
                    state.outbox_effects == before.outbox_effects,
                context + " left a partial reservation behind");
    }
}

// The conservative rule is only conservative if the OTHER side's open quantity
// really is counted. Shown with two live orders from the real path: a resting
// buy must shrink what a long-only account may sell.
// Unreachable through admitted commands or snapshots; guards the negation of a
// corrupt in-memory cap, which would be undefined for INT64_MIN.
void test_negative_position_cap_fails_closed() {
    using namespace velocita::v2;
    auto state = ready_state();
    state.risk_accounts[0].max_position_quantity =
        QuantityLots::from_atoms(std::numeric_limits<std::int64_t>::min());
    state.risk_accounts[0].allow_short = true;
    state.decision_cold[0].proposal.side = Side::sell;
    const auto before = state;
    ApplyScratch scratch;
    require(catalog_test::apply(state, authorize(2, 0x99), scratch).code == ApplyCode::invalid_command,
            "a negative position cap did not fail closed");
    require_economics_unchanged(before, state, "negative-cap refusal mutated economics");
}

void test_position_limit_counts_open_orders_on_both_sides() {
    using namespace velocita::v2;
    auto state = two_sided_state();
    state.risk_accounts[0].position_quantity = QuantityLots::from_atoms(3);
    state.risk_accounts[0].allow_short = false;
    state.risk_accounts[0].max_position_quantity = QuantityLots::from_atoms(10);
    ApplyScratch scratch;
    require(evaluate_at_feature(state, scratch, DecisionId{31}, 16, 2, 2).code == ApplyCode::applied &&
                state.decision_cold[state.decision_count - 1].proposal.side == Side::buy,
            "fixture drifted: the rich feature no longer selects the buy slot");
    require(catalog_test::apply(state, authorize_decision(3, 0x94, DecisionId{31}), scratch).code ==
                ApplyCode::applied, "the resting buy was not authorized");
    require(evaluate_at_feature(state, scratch, DecisionId{32}, 11, 3, 4).code == ApplyCode::applied &&
                state.decision_cold[state.decision_count - 1].proposal.side == Side::sell,
            "fixture drifted: the floor feature no longer selects the sell slot");
    const auto& risk = state.risk_accounts[0];
    const auto sell_quantity = state.decision_cold[state.decision_count - 1].proposal.quantity.atoms();
    const auto expected = expected_position_outcome(
        Side::sell, risk.position_quantity.atoms(), risk.reserved_quantity.atoms(), sell_quantity,
        risk.max_position_quantity.atoms(), risk.allow_short);
    const auto ignoring_buys = expected_position_outcome(
        Side::sell, risk.position_quantity.atoms(), 0, sell_quantity,
        risk.max_position_quantity.atoms(), risk.allow_short);
    require(expected == PositionExpectation::refused && ignoring_buys == PositionExpectation::allowed,
            "fixture no longer separates both-sides counting from same-side counting");
    const auto before = state;
    require(catalog_test::apply(state, authorize_decision(5, 0x95, DecisionId{32}), scratch).code ==
                ApplyCode::position_limit_exceeded,
            "a sell ignored the open buy when checking the long-only floor");
    require_economics_unchanged(before, state, "position refusal mutated economics");
}

// A frame bound to explicit versions, as a strategy would build it from the
// refusal it just received rather than from a peek at replicated state.
velocita::v2::ApplyResult evaluate_bound_decision(
    velocita::v2::TradingState& state, velocita::v2::ApplyScratch& scratch,
    const velocita::v2::DecisionId decision_id, const std::uint64_t source_sequence,
    const std::uint64_t request, const velocita::v2::RiskVersion risk_version,
    const velocita::v2::PositionVersion position_version) {
    using namespace velocita::v2;
    const auto evidence = digest(static_cast<std::uint8_t>(0x40 + (source_sequence % 0x30)));
    require(catalog_test::apply(state, {{CallerId{7}, RequestSequence{request}, digest(0x21)},
                RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                  SourceSequence{source_sequence}, evidence}}, scratch).code ==
                ApplyCode::applied, "source event admission failed");
    auto value = frame();
    value.venue_sequence = VenueSequence{source_sequence};
    value.source_sequence = SourceSequence{source_sequence};
    value.raw_evidence_digest = evidence;
    value.risk_version = risk_version;
    value.position_version = position_version;
    return catalog_test::apply(state, {{CallerId{8}, RequestSequence{request},
                digest(static_cast<std::uint8_t>(1 + decision_id.value() % 255))},
            EvaluateDecisionFrame{decision_id, EvaluationMode::authoritative, value}}, scratch);
}

void test_lowered_cap_is_reduce_only_and_stale_refusal_resyncs() {
    using namespace velocita::v2;
    auto state = ready_state();
    state.risk_accounts[0].position_quantity = QuantityLots::from_atoms(6);
    ApplyScratch scratch;
    auto lower = configure_risk(state, 1);
    std::get<ConfigureRiskLimits>(lower.body).max_position_quantity = QuantityLots::from_atoms(3);
    std::get<ConfigureRiskLimits>(lower.body).allow_short = false;
    const auto lowered = catalog_test::apply(state, lower, scratch);
    require(lowered.code == ApplyCode::applied &&
                state.risk_accounts[0].max_position_quantity == QuantityLots::from_atoms(3) &&
                !state.risk_accounts[0].allow_short &&
                state.risk_accounts[0].position_quantity == QuantityLots::from_atoms(6),
            "a cap below the current position was refused or rewrote the position");

    // The fixture decision was bound to risk version 1; the account is now at 2.
    const auto current = state.risk_accounts[0];
    require(current.risk_version != state.decision_cold[0].frame.risk_version,
            "fixture drifted: the old decision is not stale");
    const auto before = state;
    const auto stale_command = authorize(2, 0x96);
    const auto stale = catalog_test::apply(state, stale_command, scratch);
    require(stale.code == ApplyCode::stale_risk_state, "stale decision was not refused as stale");
    require_economics_unchanged(before, state, "stale refusal mutated economics");
    require(stale.detail_kind == ApplyResultDetailKind::risk_state &&
                stale.detail.words == std::array<std::uint64_t, 3>{
                    current.risk_version.value(), current.position_version.value(), 6},
            "stale refusal did not carry the account's current versions and position");
    require(stale.decision_id == DecisionId{20}, "stale refusal lost its decision identity");

    std::array<std::byte, kKernelResultBytes> encoded_result{};
    const auto encoded = encode_kernel_result(stale, encoded_result);
    ApplyResult decoded;
    require(encoded.error == KernelCodecError::none &&
                decode_kernel_result({encoded_result.data(), encoded.bytes_written}, decoded).error ==
                    KernelCodecError::none && decoded == stale,
            "the re-sync detail did not survive the canonical result codec");

    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    const auto saved = encode_state_snapshot(state, snapshot);
    TradingState restored = catalog_test::state();
    StateSnapshotScratch snapshot_scratch;
    require(saved.error == StateSnapshotError::none &&
                decode_state_snapshot({snapshot.data(), saved.bytes_written}, restored,
                                      snapshot_scratch).error == StateSnapshotError::none,
            "state retaining a re-sync detail did not round-trip");
    require(catalog_test::apply(restored, stale_command, scratch) == stale,
            "a restored replica answered the stale retry differently");

    // Bind exactly what the refusal said, then trade against the lowered cap.
    require(evaluate_bound_decision(state, scratch, DecisionId{21}, 2, 3, stale.current_risk_version(),
                                    stale.current_position_version()).code == ApplyCode::applied,
            "re-synced decision was not evaluated");
    const auto buy_before = state;
    const auto limited = catalog_test::apply(state, authorize_decision(4, 0x97, DecisionId{21}), scratch);
    require(limited.code == ApplyCode::position_limit_exceeded,
            "a long above its lowered cap was allowed to add");
    require_economics_unchanged(buy_before, state, "reduce-only buy refusal mutated economics");
    ApplyResult limited_decoded;
    const auto limited_encoded = encode_kernel_result(limited, encoded_result);
    require(limited_encoded.error == KernelCodecError::none &&
                decode_kernel_result({encoded_result.data(), limited_encoded.bytes_written},
                                     limited_decoded).error == KernelCodecError::none &&
                limited_decoded == limited,
            "a position-limit refusal did not survive the canonical result codec");
    require(encode_state_snapshot(state, snapshot).error == StateSnapshotError::none,
            "state retaining a position-limit refusal did not encode");

    state.decision_cold[state.decision_count - 1].proposal.side = Side::sell;
    const auto reserved_before = state.risk_accounts[0].reserved_quantity;
    const auto quantity = state.decision_cold[state.decision_count - 1].proposal.quantity;
    require(catalog_test::apply(state, authorize_decision(5, 0x98, DecisionId{21}), scratch).code ==
                    ApplyCode::applied &&
                state.risk_accounts[0].reserved_quantity.atoms() ==
                    reserved_before.atoms() + quantity.atoms(),
            "a reducing sell was refused under a lowered cap");
}

// Only the position moved (as a fill would move it): the detail must report
// the account's position version, not the one the decision was bound to.
void test_stale_position_resync_reports_current_position() {
    using namespace velocita::v2;
    auto state = ready_state();
    state.risk_accounts[0].position_version = PositionVersion{5};
    state.risk_accounts[0].position_quantity = QuantityLots::from_atoms(-2);
    const auto current = state.risk_accounts[0];
    require(state.decision_cold[0].frame.risk_version == current.risk_version &&
                state.decision_cold[0].frame.position_version != current.position_version,
            "fixture drifted: only the position version should be stale");
    ApplyScratch scratch;
    const auto stale = catalog_test::apply(state, authorize(2, 0x9a), scratch);
    require(stale.code == ApplyCode::stale_risk_state &&
                stale.detail_kind == ApplyResultDetailKind::risk_state &&
                stale.detail.words == std::array<std::uint64_t, 3>{
                    current.risk_version.value(), 5,
                    static_cast<std::uint64_t>(std::int64_t{-2})},
            "a stale position refusal did not report the current position version and quantity");
}

void test_risk_state_detail_is_validated_by_both_codecs() {
    using namespace velocita::v2;
    ApplyResult result;
    result.code = ApplyCode::stale_risk_state;
    result.decision_id = DecisionId{20};
    result.detail_kind = ApplyResultDetailKind::risk_state;
    result.detail = ApplyResultDetail::risk_state(RiskVersion{3}, PositionVersion{4},
                                                  QuantityLots::from_atoms(-5));
    std::array<std::byte, kKernelResultBytes> bytes{};
    const auto encoded = encode_kernel_result(result, bytes);
    ApplyResult decoded;
    require(encoded.error == KernelCodecError::none &&
                decode_kernel_result({bytes.data(), encoded.bytes_written}, decoded).error ==
                    KernelCodecError::none &&
                decoded.current_position_quantity() == QuantityLots::from_atoms(-5),
            "a short position did not survive the re-sync detail");
    // Minimal canonical state: one registered caller and one retained receipt.
    const auto retaining = [](const ApplyResult& retained_result) {
        TradingState value = catalog_test::state();
        value.state_version = 5;
        value.caller_cursor_count = 1;
        value.caller_cursors[0] = {CallerId{1}, {}, {}};
        value.receipt_count = 1;
        value.receipts[0] = {{CallerId{1}, RequestSequence{77}, digest(0x31)}, retained_result};
        return value;
    };
    auto state = retaining(result);
    std::array<std::byte, kMaxStateSnapshotBytes> snapshot{};
    const auto retained = encode_state_snapshot(state, snapshot);
    require(retained.error == StateSnapshotError::none,
            "snapshot refused a valid retained re-sync detail, error " +
                std::to_string(static_cast<int>(retained.error)));
    const auto refused = [&](auto change, const std::string& what) {
        auto invalid = result;
        change(invalid);
        require(encode_kernel_result(invalid, bytes).error == KernelCodecError::invalid_value,
                "result codec accepted " + what);
        const auto invalid_state = retaining(invalid);
        require(encode_state_snapshot(invalid_state, snapshot).error == StateSnapshotError::invalid_state,
                "snapshot accepted " + what);
    };
    refused([](ApplyResult& r) { r.code = ApplyCode::applied; }, "a re-sync detail on a non-stale code");
    refused([](ApplyResult& r) { r.detail.words[0] = 0; }, "a zero risk version");
    refused([](ApplyResult& r) { r.detail.words[1] = 0; }, "a zero position version");
    refused([](ApplyResult& r) { r.decision_id = DecisionId{}; }, "a re-sync detail with no decision");
}

int main() {
    try {
        {
            using namespace velocita::v2;
            TradingState state = catalog_test::state();
            ApplyScratch scratch;
            const Command configure{{CallerId{1}, RequestSequence{1}, digest(0xe1)},
                ConfigureRiskLimits{AccountId{5}, InstrumentId{4}, RiskPolicyVersion{6},
                    RiskVersion{}, KillEpoch{}, ExecutorGeneration{},
                    QuantityLots::from_atoms(4), QuantityLots::from_atoms(8),
                    MoneyMicros::from_atoms(4'000), MoneyMicros::from_atoms(8'000),
                    1'000'000, 2'500, QuantityLots::from_atoms(1'000), true, 1}};
            const auto configured = catalog_test::apply(state, configure, scratch);
            require(configured.code == ApplyCode::applied && state.risk_account_count == 1 &&
                        state.risk_accounts[0].risk_version == RiskVersion{1} &&
                        state.risk_accounts[0].position_version == PositionVersion{1} &&
                        state.risk_accounts[0].reserved_quantity == QuantityLots{} &&
                        state.receipt_count == 1 && state.order_count == 0 &&
                        state.reservation_count == 0 && state.outbox_effect_count == 0,
                    "replicated risk configuration did not atomically initialize an empty ledger");
            require(state.risk_accounts[0].max_position_quantity == QuantityLots::from_atoms(1'000) &&
                        state.risk_accounts[0].allow_short,
                    "a new risk account did not store its position cap and short permission");
        }
        test_risk_configuration_updates_preserve_economics();
        test_risk_configuration_codec_and_restore();
        test_position_limit_matrix();
        test_position_limit_counts_open_orders_on_both_sides();
        test_negative_position_cap_fails_closed();
        test_lowered_cap_is_reduce_only_and_stale_refusal_resyncs();
        test_stale_position_resync_reports_current_position();
        test_risk_state_detail_is_validated_by_both_codecs();
        test_risk_configuration_admission_and_capacity();
        test_serial_replacement_across_a_risk_limit_update();
        test_reclamation_during_cancellation_preserves_the_recovery_path();
        test_risk_configuration_respects_every_unresolved_order_state();
        test_two_sided_slots_requote_independently();
        test_unauthorizable_decisions_do_not_fill_the_arena();
        test_superseded_slot_decisions_are_reclaimed();
        test_request_receipts_do_not_bound_the_kernel_lifetime();
        test_an_expired_request_is_refused_never_re_executed();
        test_resolved_orders_release_venue_identity_capacity();
        test_gateway_acknowledgement_can_drain_a_full_receipt_arena();
        test_acknowledged_duplicate_fills_do_not_exhaust_receipts();
        test_reclaimed_decision_retains_identity_and_authorization();
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
        test_authorization_requires_current_latest_intent();
        test_cancel_effect_refuses_submit_ack_and_repeated_unknown();
        test_fill_proves_only_the_submit_effect_it_names();
        test_authorization_reserves_completion_and_cancel_capacity();
        test_caller_prefix_registration_is_atomic_and_bounded();
        test_invalid_observation_refusal_is_canonical_state();
        test_decision_retry_retains_original_authorization_after_fill();
        test_submit_authorization_is_atomic_and_idempotent();
        test_authorization_requires_initialized_execution_authority();
        test_risk_and_capacity_fail_before_economic_mutation();
        test_non_authoritative_decisions_cannot_create_economics();
        test_partial_fills_conserve_risk_position_and_identity();
        test_fill_replay_and_snapshot_are_byte_identical();
        test_fill_failures_are_atomic_and_bounded();
        test_cancel_and_unknown_remain_risk_bearing_until_evidence();
        test_fill_cancel_race_consumes_one_reservation();
        test_a_resting_order_the_venue_does_not_hold_is_resolved();
        test_an_unrequested_venue_cancellation_is_evidence_not_noise();
        test_mass_cancel_pulls_every_cancellable_order_in_scope();
        test_mass_cancel_result_survives_codec_snapshot_and_retry();
        test_mass_cancel_detail_is_validated_by_both_codecs();
        test_mass_cancel_skips_the_uncancellable_without_failing();
        test_mass_cancel_refuses_rather_than_half_finishing();
        test_mass_cancel_is_fenced_like_every_other_control_action();
        test_authorization_requires_the_installed_catalog_context();
        test_bound_effect_keeps_historical_catalog_versions();
        std::cout << "all V2 risk/order/outbox tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 risk/order/outbox test failed: " << error.what() << '\n';
        return 1;
    }
}
