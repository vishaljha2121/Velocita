#include "v2_catalog_test_support.hpp"
#include "velocita/v2/gateway.hpp"
#include "velocita/v2/payload.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace velocita::v2;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

PreSendDecision test_evaluate_pre_send(
    const TradingState& state, const EffectId effect_id,
    const GatewayControlView& control, const VenueCapabilityView& venue,
    const SendQuotaView& quota) {
    return evaluate_delivery_candidate(state, effect_id, control, venue, quota,
                             &catalog_test::standard_catalog());
}

DeliveryPlan test_plan_delivery(
    const TradingState& state, const GatewayDeliveryState& delivery,
    const GatewayEvidenceState& evidence, const GatewayControlView& control,
    const VenueCapabilityView& venue, const SendQuotaView& quota,
    const GatewayTimeouts& timeouts, const Tick now) {
    return plan_delivery(state, delivery, evidence, control, venue, quota,
                         timeouts, now, &catalog_test::standard_catalog());
}

Digest256 digest(const std::uint8_t marker) {
    Digest256 value;
    value.bytes[0] = static_cast<std::byte>(marker);
    return value;
}

class RecordingTransport final {
public:
    void send(const OrderEffect& effect) {
        sent_.push_back(encode_payload(effect).bytes);
    }

    [[nodiscard]] std::size_t send_count() const noexcept { return sent_.size(); }
    [[nodiscard]] const std::vector<std::byte>& last() const { return sent_.back(); }

private:
    std::vector<std::vector<std::byte>> sent_;
};

PreSendDecision deliver(const TradingState& state,
                        const EffectId effect_id,
                        const GatewayControlView& control,
                        const VenueCapabilityView& venue,
                        const SendQuotaView& quota,
                        RecordingTransport& transport) {
    auto decision = test_evaluate_pre_send(state, effect_id, control, venue, quota);
    if (decision.permitted()) transport.send(*decision.effect);
    return decision;
}

PolicyArtifact policy() {
    PolicyArtifact value;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.schema_version = kPolicyArtifactSchemaVersion;
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

FeatureSchemaArtifact feature_schema() {
    FeatureSchemaArtifact value;
    value.schema_version = kFeatureSchemaArtifactSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{3};
    value.feature_count = 1;
    value.features[0] = {FeatureEconomicKind::price, 1};
    value.artifact_digest = compute_artifact_digest(value).digest;
    return value;
}

ActionTemplate action_template() {
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

ExecutablePolicyArtifact executable_policy(const FeatureSchemaArtifact& schema) {
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

DecisionFrame frame() {
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

struct Fixture {
    TradingState state = catalog_test::state();
    EffectId effect_id;
    ClusterOrderId cluster_order_id;
};

Fixture authorized_submit() {
    TradingState state = catalog_test::state();
    state.strategy_mode = StrategyMode::normal;
    state.kill_epoch = KillEpoch{2};
    state.executor_generation = ExecutorGeneration{3};
    state.policy_count = 1;
    state.policies[0] = {policy(), PolicyLifecycle::active};
    const auto schema = feature_schema();
    state.active_executable_policy = {
        true, schema, executable_policy(schema),
        {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4},
         InstrumentSpecVersion{7}, MoneyMicros::from_atoms(100),
         PriceTicks::from_atoms(1), PriceTicks::from_atoms(100),
         QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)},
        {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
         kVenueDirectivePlaceLimit | kVenueDirectiveCancel, kVenueTifGtc, 1},
        1, 2};
    state.state_version = 2;
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

    ApplyScratch scratch;
    const Command evaluate{{CallerId{1}, RequestSequence{1}, digest(0x51)},
                           EvaluateDecisionFrame{DecisionId{20},
                                                 EvaluationMode::authoritative,
                                                 frame()}};
    require(catalog_test::apply(state, evaluate, scratch).code == ApplyCode::applied,
            "gateway fixture could not retain its decision");
    const Command authorize{{CallerId{1}, RequestSequence{2}, digest(0x52)},
                            AuthorizeDecision{DecisionId{20}}};
    const auto submit = catalog_test::apply(state, authorize, scratch);
    require(submit.code == ApplyCode::applied,
            "gateway fixture could not authorize its submit");
    return {state, submit.effect_id(), submit.cluster_order_id()};
}

Fixture authorized_cancel() {
    auto fixture = authorized_submit();
    ApplyScratch scratch;
    const Command cancel{{CallerId{1}, RequestSequence{3}, digest(0x53)},
                         RequestCancel{fixture.cluster_order_id}};
    const auto result = catalog_test::apply(fixture.state, cancel, scratch);
    require(result.code == ApplyCode::applied,
            "gateway fixture could not authorize its cancel");
    require(result.effect_id() != fixture.effect_id,
            "the cancel reused the submit's effect identity");
    return {fixture.state, result.effect_id(), fixture.cluster_order_id};
}

GatewayControlView matching_control(const TradingState& state);
constexpr VenueCapabilityView capable_venue();
constexpr SendQuotaView open_quota();

VerifiedExecutionCatalog catalog_with_directives(const std::uint8_t directives) {
    const std::array rows{catalog_test::row(
        AccountId{5},
        {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4},
         InstrumentSpecVersion{7}, MoneyMicros::from_atoms(100),
         PriceTicks::from_atoms(1), PriceTicks::from_atoms(100),
         QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)},
        {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
         directives, kVenueTifGtc, 1})};
    return catalog_test::catalog(rows);
}

void test_send_admission_requires_exact_catalog_context() {
    const auto fixture = authorized_submit();
    const auto control = matching_control(fixture.state);
    require(evaluate_delivery_candidate(fixture.state, fixture.effect_id, control,
                capable_venue(), open_quota()).refusal ==
                    PreSendRefusal::payload_unreconstructable,
            "missing catalog context authorized a send");
    GatewayDeliveryState delivery;
    delivery.started = true;
    require(plan_delivery(fixture.state, delivery, {}, control,
                capable_venue(), open_quota(), {}, Tick{1}).send_count == 0,
            "delivery planning bypassed missing catalog context");
    const auto wrong = catalog_with_directives(kVenueDirectivePlaceLimit);
    require(evaluate_delivery_candidate(fixture.state, fixture.effect_id, control,
                capable_venue(), open_quota(), &wrong).refusal ==
                    PreSendRefusal::payload_unreconstructable,
            "wrong catalog context authorized a send");
}

void test_send_admission_carries_historical_binding() {
    auto fixture = authorized_submit();
    fixture.state.active_executable_policy.instrument.instrument_spec_version =
        InstrumentSpecVersion{99};
    fixture.state.active_executable_policy.venue.venue_capabilities_version =
        VenueCapabilitiesVersion{99};
    const auto decision = test_evaluate_pre_send(
        fixture.state, fixture.effect_id, matching_control(fixture.state),
        capable_venue(), open_quota());
    require(decision.permitted() &&
                decision.catalog_root == fixture.state.execution_catalog_root &&
                decision.instrument_spec_version == InstrumentSpecVersion{7} &&
                decision.venue_capabilities_version == VenueCapabilitiesVersion{8},
            "send admission borrowed binding metadata from the active policy");
}

void test_cancel_requires_catalog_operation_support() {
    auto fixture = authorized_cancel();
    const auto place_only = catalog_with_directives(kVenueDirectivePlaceLimit);
    fixture.state.execution_catalog_root = place_only.root();
    fixture.state.active_executable_policy.venue.directive_mask = kVenueDirectivePlaceLimit;
    require(execution_catalog_bindings_valid(fixture.state, place_only),
            "place-only cancel fixture contradicted its catalog");
    const auto decision = evaluate_delivery_candidate(
        fixture.state, fixture.effect_id, matching_control(fixture.state),
        capable_venue(), open_quota(), &place_only);
    require(decision.refusal == PreSendRefusal::unsupported_operation &&
                !decision.effect.has_value(),
            "catalog without cancel capability authorized a cancel");
}

void test_public_gateway_paths_reject_oversized_counts() {
    const auto fixture = authorized_submit();
    const auto control = matching_control(fixture.state);
    GatewayDeliveryState attempts;
    attempts.attempts[0].effect_id = fixture.effect_id;
    for (const auto count : {attempts.attempts.size() + 1,
                             std::numeric_limits<std::size_t>::max()}) {
        attempts.attempt_count = count;
        require(find_attempt(attempts, fixture.effect_id) == nullptr,
                "find_attempt accepted an oversized attempt count");
    }

    for (const auto count : {fixture.state.outbox_effects.size() + 1,
                             std::numeric_limits<std::size_t>::max()}) {
        auto malformed = fixture.state;
        malformed.outbox_effect_count = count;
        require(test_evaluate_pre_send(malformed, fixture.effect_id, control,
                    capable_venue(), open_quota()).refusal ==
                        PreSendRefusal::payload_unreconstructable,
                "pre-send accepted an oversized outbox count");
        GatewayDeliveryState delivery;
        delivery.started = true;
        require(test_plan_delivery(malformed, delivery, {}, control,
                    capable_venue(), {1, 0}, {}, Tick{1}).send_count == 0,
                "delivery planning accepted an oversized outbox count");
    }

    GatewayDeliveryState started;
    started.started = true;
    GatewayEvidenceState evidence;
    for (const auto count : {evidence.obligations.size() + 1,
                             std::numeric_limits<std::size_t>::max()}) {
        evidence.obligation_count = count;
        require(test_plan_delivery(fixture.state, started, evidence, control,
                    capable_venue(), {1, 0}, {}, Tick{1}).send_count == 0,
                "delivery planning accepted an oversized evidence count");
    }
}

constexpr Tick kNow = 1000;
constexpr Tick kLeaseBound = 50;

GatewayControlView matching_control(const TradingState& state) {
    return {state.executor_generation, state.kill_epoch, state.strategy_mode,
            kNow, kLeaseBound, kNow};
}

constexpr VenueCapabilityView capable_venue() {
    return {VenueId{9}, VenueCapabilitiesVersion{8}, true, true};
}

constexpr SendQuotaView open_quota() { return {16, 4}; }

void test_planner_preserves_emergency_cancel_capacity() {
    const auto fixture = authorized_cancel();
    GatewayDeliveryState delivery;
    delivery.started = true;
    auto control = matching_control(fixture.state);
    control.now = control.confirmed_at + control.lease_bound + 1;
    const auto plan = test_plan_delivery(fixture.state, delivery, {}, control,
                                    capable_venue(), {0, 1}, {}, control.now);
    require(plan.send_count == 1 && plan.to_send[0] == fixture.effect_id,
            "planner lost emergency cancel capacity after the normal lane exhausted");
    RecordingTransport transport;
    require(deliver(fixture.state, plan.to_send[0], control, capable_venue(),
                    {0, 1}, transport).permitted(),
            "planned emergency cancel was not deliverable");
    const auto expected = materialize_order_effect(fixture.state, fixture.effect_id);
    require(expected.has_value() && transport.send_count() == 1 &&
                transport.last() == encode_payload(*expected).bytes,
            "planned emergency cancel did not deliver its canonical bytes");
    require(test_plan_delivery(fixture.state, delivery, {}, control, capable_venue(),
                          {0, 0}, {}, control.now).send_count == 0,
            "planner sent a cancel without either quota lane");
    const auto submit = authorized_submit();
    require(test_plan_delivery(submit.state, delivery, {}, matching_control(submit.state),
                          capable_venue(), {0, 8}, {}, kNow).send_count == 0,
            "planner spent emergency quota on new risk");
    const auto mixed = test_plan_delivery(fixture.state, delivery, {},
                                     matching_control(fixture.state), capable_venue(),
                                     {1, 1}, {}, kNow);
    require(mixed.send_count == 2 && mixed.to_send[0] == submit.effect_id &&
                mixed.to_send[1] == fixture.effect_id,
            "planner did not preserve submit-then-cancel order across quota lanes");
    const auto normal_only = test_plan_delivery(fixture.state, delivery, {},
                                           matching_control(fixture.state), capable_venue(),
                                           {1, 0}, {}, kNow);
    require(normal_only.send_count == 1 && normal_only.to_send[0] == submit.effect_id,
            "planner overspent its single normal quota unit");
}

void test_a_permitted_effect_is_sent_with_its_exact_canonical_bytes() {
    const auto fixture = authorized_submit();
    RecordingTransport transport;
    const auto decision = deliver(fixture.state, fixture.effect_id,
                                  matching_control(fixture.state), capable_venue(),
                                  open_quota(), transport);
    require(decision.permitted(), "a valid effect was refused before send");
    require(transport.send_count() == 1, "a permitted effect was not sent once");

    const auto authoritative = materialize_order_effect(fixture.state, fixture.effect_id);
    require(authoritative.has_value() &&
                encode_payload(*authoritative).bytes == transport.last(),
            "the sent bytes differ from the cluster's own reconstruction");
}

void test_the_guard_reads_no_clock_and_mutates_nothing() {
    const auto fixture = authorized_submit();
    auto probe = fixture.state;
    static_assert(std::is_trivially_copyable_v<TradingState>);
    const auto first = test_evaluate_pre_send(probe, fixture.effect_id,
                                         matching_control(probe), capable_venue(),
                                         open_quota());
    require(std::memcmp(&probe, &fixture.state, sizeof(TradingState)) == 0,
            "the pre-send guard mutated authoritative state");

    const auto second = test_evaluate_pre_send(probe, fixture.effect_id,
                                          matching_control(probe), capable_venue(),
                                          open_quota());
    require(first.refusal == second.refusal &&
                first.permitted() == second.permitted(),
            "the pre-send guard was not deterministic");
}

void test_stale_control_authority_is_never_sent() {
    const auto fixture = authorized_submit();

    const auto refused = [&](GatewayControlView control,
                             const PreSendRefusal expected,
                             const std::string& what) {
        RecordingTransport transport;
        const auto decision = deliver(fixture.state, fixture.effect_id, control,
                                      capable_venue(), open_quota(), transport);
        require(decision.refusal == expected,
                what + ": wrong refusal reason");
        require(!decision.effect.has_value(),
                what + ": a refused decision still carried a payload");
        require(transport.send_count() == 0,
                what + ": a refused effect reached the transport");
    };

    auto newer_generation = matching_control(fixture.state);
    newer_generation.executor_generation =
        ExecutorGeneration{fixture.state.executor_generation.value() + 1};
    refused(newer_generation, PreSendRefusal::stale_executor_generation,
            "stale executor generation");

    auto newer_kill = matching_control(fixture.state);
    newer_kill.kill_epoch = KillEpoch{fixture.state.kill_epoch.value() + 1};
    refused(newer_kill, PreSendRefusal::stale_kill_epoch, "stale kill epoch");

    for (const auto mode : {StrategyMode::halted, StrategyMode::no_new_risk,
                            StrategyMode::cancel_only, StrategyMode::reduce_only}) {
        auto suppressed = matching_control(fixture.state);
        suppressed.strategy_mode = mode;
        refused(suppressed, PreSendRefusal::control_forbids_new_risk,
                "control mode " + std::to_string(static_cast<int>(mode)));
    }
}

void test_a_reducing_order_is_sent_only_in_the_mode_that_decided_it() {
    const auto fixture = authorized_submit();
    const auto attempt = [&](const StrategyMode decided, const StrategyMode current) {
        auto state = fixture.state;
        for (std::size_t index = 0; index < state.decision_count; ++index)
            state.decision_hot[index].strategy_mode = decided;
        state.strategy_mode = current;
        RecordingTransport transport;
        const auto decision = deliver(state, fixture.effect_id, matching_control(state),
                                      capable_venue(), open_quota(), transport);
        return std::pair{decision.refusal, transport.send_count()};
    };
    for (const auto mode : {StrategyMode::reduce_only, StrategyMode::flatten}) {
        require(attempt(mode, mode) == std::pair{PreSendRefusal::none, std::size_t{1}},
                "an order decided in a reducing mode was not sent in that mode");
        require(attempt(StrategyMode::normal, mode) ==
                    std::pair{PreSendRefusal::control_forbids_new_risk, std::size_t{0}},
                "an order decided in normal mode was sent in a reducing mode");
        require(attempt(mode, StrategyMode::normal) == std::pair{PreSendRefusal::none, std::size_t{1}},
                "normal mode held back an order");
    }
    require(attempt(StrategyMode::reduce_only, StrategyMode::flatten) ==
                std::pair{PreSendRefusal::control_forbids_new_risk, std::size_t{0}} &&
                attempt(StrategyMode::flatten, StrategyMode::reduce_only) ==
                    std::pair{PreSendRefusal::control_forbids_new_risk, std::size_t{0}},
            "an order decided in one reducing mode was sent in the other");
    for (const auto mode : {StrategyMode::halted, StrategyMode::no_new_risk, StrategyMode::cancel_only}) {
        require(attempt(mode, mode) == std::pair{PreSendRefusal::control_forbids_new_risk, std::size_t{0}},
                "an order was sent in a mode that allows none");
    }
}

void test_an_expired_authority_lease_stops_new_risk() {
    const auto fixture = authorized_submit();

    const auto attempt = [&](const GatewayControlView& control) {
        RecordingTransport transport;
        const auto decision = deliver(fixture.state, fixture.effect_id, control,
                                      capable_venue(), open_quota(), transport);
        return std::pair{decision.refusal, transport.send_count()};
    };

    auto fresh = matching_control(fixture.state);
    require(attempt(fresh).first == PreSendRefusal::none,
            "a freshly confirmed control view was refused");

    auto at_bound = matching_control(fixture.state);
    at_bound.now = at_bound.confirmed_at + at_bound.lease_bound;
    require(attempt(at_bound).first == PreSendRefusal::none,
            "a control view exactly at the lease bound was refused");

    auto lapsed = matching_control(fixture.state);
    lapsed.now = lapsed.confirmed_at + lapsed.lease_bound + 1;
    const auto [refusal, sends] = attempt(lapsed);
    require(refusal == PreSendRefusal::authority_lease_expired,
            "a lapsed authority lease did not stop a new-risk send");
    require(sends == 0, "an effect reached the transport under a lapsed lease");
}

void test_an_unconfigured_lease_is_expired_not_unlimited() {
    const auto fixture = authorized_submit();
    GatewayControlView unconfigured{fixture.state.executor_generation,
                                    fixture.state.kill_epoch,
                                    fixture.state.strategy_mode};
    RecordingTransport transport;
    const auto decision = deliver(fixture.state, fixture.effect_id, unconfigured,
                                  capable_venue(), open_quota(), transport);
    require(decision.refusal == PreSendRefusal::authority_lease_expired,
            "an unconfigured lease was treated as unlimited authority");
    require(transport.send_count() == 0,
            "an effect was sent with no authority lease at all");

    auto from_the_future = matching_control(fixture.state);
    from_the_future.confirmed_at = from_the_future.now + 1;
    RecordingTransport other;
    require(deliver(fixture.state, fixture.effect_id, from_the_future,
                    capable_venue(), open_quota(), other)
                    .refusal == PreSendRefusal::authority_lease_expired,
            "a control view confirmed in the future was accepted");
}

void test_a_cancel_survives_what_stops_new_risk() {
    const auto fixture = authorized_cancel();

    RecordingTransport starved;
    require(deliver(fixture.state, fixture.effect_id, matching_control(fixture.state),
                    capable_venue(), SendQuotaView{0, 1}, starved)
                    .permitted(),
            "a cancel was refused while emergency capacity remained");
    require(starved.send_count() == 1, "the cancel did not reach the transport");

    auto lapsed = matching_control(fixture.state);
    lapsed.now = lapsed.confirmed_at + lapsed.lease_bound + 1;
    RecordingTransport after_lapse;
    require(deliver(fixture.state, fixture.effect_id, lapsed, capable_venue(),
                    SendQuotaView{0, 1}, after_lapse)
                    .permitted(),
            "a lapsed lease stopped a cancel, trapping risk it cannot reduce");

    RecordingTransport exhausted;
    require(deliver(fixture.state, fixture.effect_id, matching_control(fixture.state),
                    capable_venue(), SendQuotaView{0, 0}, exhausted)
                    .refusal == PreSendRefusal::quota_exhausted,
            "a cancel was sent with both lanes exhausted");
}

void test_the_emergency_lane_cannot_carry_new_risk() {
    const auto fixture = authorized_submit();
    RecordingTransport transport;
    const auto decision = deliver(fixture.state, fixture.effect_id,
                                  matching_control(fixture.state), capable_venue(),
                                  SendQuotaView{0, 8}, transport);
    require(decision.refusal == PreSendRefusal::quota_exhausted,
            "a submit drew on the reserved emergency lane");
    require(transport.send_count() == 0,
            "a risk-increasing effect was sent on the emergency lane");
}

void test_boolean_authority_cannot_renew() {
    const auto fixture = authorized_submit();
    auto control = matching_control(fixture.state);

    const AuthorityReading authoritative{true, fixture.state.executor_generation,
                                         fixture.state.kill_epoch,
                                         fixture.state.strategy_mode};
    AuthorityReading follower = authoritative;
    follower.authoritative = false;

    const auto original = control;
    require(!renew_authority(control, authoritative, kNow + 10),
            "local leader boolean still minted a fresh authority lease");
    require(control == original, "boolean reading mutated gateway authority");

    const auto before = control;
    require(!renew_authority(control, follower, kNow + 11),
            "a non-authoritative read reported a renewal");
    require(control.confirmed_at == before.confirmed_at,
            "a non-authoritative read extended the lease");

    require(!renew_authority(control, authoritative, kNow + 9),
            "a backwards tick renewed the lease");
    require(control.confirmed_at == before.confirmed_at,
            "a backwards tick moved the confirmation time");
}

void test_boolean_reading_cannot_promote_the_gateway() {
    const auto fixture = authorized_submit();
    auto control = matching_control(fixture.state);

    AuthorityReading promoted{true, fixture.state.executor_generation,
                              fixture.state.kill_epoch, fixture.state.strategy_mode};
    promoted.executor_generation =
        ExecutorGeneration{fixture.state.executor_generation.value() + 1};
    promoted.kill_epoch = KillEpoch{fixture.state.kill_epoch.value() + 1};

    const auto before = control;
    require(!renew_authority(control, promoted, kNow + 1) && control == before,
            "boolean reading promoted the gateway or refreshed its authority");
}

void test_candidate_cache_age_filter_is_not_send_authority() {
    const auto fixture = authorized_submit();
    auto control = matching_control(fixture.state);
    const AuthorityReading reading{true, fixture.state.executor_generation,
                                   fixture.state.kill_epoch,
                                   fixture.state.strategy_mode};

    const Tick last_contact = kNow + 100;
    require(!renew_authority(control, reading, last_contact), "boolean renewal was accepted");
    control.confirmed_at = last_contact;

    const auto sends_at = [&](const Tick when) {
        auto probe = control;
        probe.now = when;
        RecordingTransport transport;
        const auto decision = deliver(fixture.state, fixture.effect_id, probe,
                                      capable_venue(), open_quota(), transport);
        return decision.permitted() && transport.send_count() == 1;
    };

    require(sends_at(last_contact), "the gateway stopped sending while in contact");
    require(sends_at(last_contact + kLeaseBound),
            "the gateway stopped sending before its lease was spent");
    require(!sends_at(last_contact + kLeaseBound + 1),
            "the gateway kept sending past its lease with no authoritative read");

    require(!sends_at(last_contact + kLeaseBound * 100),
            "a lapsed gateway resumed sending without renewing");
}

void test_a_venue_that_cannot_represent_the_intent_is_refused_not_approximated() {
    const auto fixture = authorized_submit();
    RecordingTransport transport;

    auto venue = capable_venue();
    venue.supports_post_only = false;
    const auto decision = deliver(fixture.state, fixture.effect_id,
                                  matching_control(fixture.state), venue,
                                  open_quota(), transport);
    require(decision.refusal == PreSendRefusal::venue_cannot_represent_intent,
            "a venue lacking post-only did not refuse a post-only effect");
    require(transport.send_count() == 0,
            "an unrepresentable effect was approximated onto the venue");

    auto wrong_venue = capable_venue();
    wrong_venue.venue_id = VenueId{99};
    RecordingTransport other;
    require(deliver(fixture.state, fixture.effect_id, matching_control(fixture.state),
                    wrong_venue, open_quota(), other)
                    .refusal == PreSendRefusal::venue_cannot_represent_intent &&
                other.send_count() == 0,
            "an effect was sent to the wrong venue");
}

void test_exhausted_quota_defers_rather_than_sending() {
    const auto fixture = authorized_submit();
    RecordingTransport transport;
    const auto decision = deliver(fixture.state, fixture.effect_id,
                                  matching_control(fixture.state), capable_venue(),
                                  SendQuotaView{0, 0}, transport);
    require(decision.refusal == PreSendRefusal::quota_exhausted &&
                transport.send_count() == 0,
            "an effect was sent with no remaining quota");
}

void test_the_gateway_can_send_nothing_the_cluster_did_not_author() {
    const auto fixture = authorized_submit();

    const auto in_outbox = [](const TradingState& state, const EffectId id) {
        for (std::size_t i = 0; i < state.outbox_effect_count; ++i) {
            if (state.outbox_effects[i].effect_id == id) return true;
        }
        return false;
    };

    std::size_t permitted_count = 0;
    const auto probe = [&](const EffectId id) {
        const auto decision = test_evaluate_pre_send(fixture.state, id,
                                                matching_control(fixture.state),
                                                capable_venue(), open_quota());
        if (!decision.permitted()) return;
        ++permitted_count;
        require(in_outbox(fixture.state, id),
                "the pre-send guard permitted EffectId " +
                    std::to_string(id.value()) +
                    ", which the cluster never authored");
        const auto authoritative = materialize_order_effect(fixture.state, id);
        require(authoritative.has_value() &&
                    encode_payload(*decision.effect).bytes ==
                        encode_payload(*authoritative).bytes,
                "the permitted payload differs from the cluster's own");
    };

    for (std::uint64_t candidate = 0; candidate < 4'096; ++candidate) {
        probe(EffectId{candidate});
    }
    const auto real = fixture.effect_id.value();
    for (std::uint64_t delta = 1; delta <= 64; ++delta) {
        probe(EffectId{real + delta});
        if (real >= delta) probe(EffectId{real - delta});
    }
    probe(fixture.effect_id);

    for (std::size_t i = 0; i < fixture.state.outbox_effect_count; ++i) {
        const auto& effect = fixture.state.outbox_effects[i];
        probe(EffectId{effect.cluster_order_id.value()});
        probe(EffectId{effect.decision_id.value()});
        probe(EffectId{effect.effect_generation});
    }
    for (std::size_t i = 0; i < fixture.state.order_count; ++i) {
        probe(EffectId{fixture.state.orders[i].cluster_order_id.value()});
        probe(EffectId{fixture.state.orders[i].decision_id.value()});
    }
    for (std::size_t i = 0; i < fixture.state.decision_count; ++i) {
        probe(EffectId{fixture.state.decision_hot[i].decision_id.value()});
    }

    require(permitted_count == 1,
            "the sweep permitted " + std::to_string(permitted_count) +
                " effects rather than the single authored one, so it either "
                "proved nothing or found a second door");

    auto emptied = fixture.state;
    emptied.outbox_effect_count = 0;
    for (std::uint64_t candidate = 0; candidate < 1'024; ++candidate) {
        require(!test_evaluate_pre_send(emptied, EffectId{candidate},
                                   matching_control(emptied), capable_venue(),
                                   open_quota())
                     .permitted(),
                "a gateway with an empty committed outbox still had something "
                "to send");
    }
    require(!test_evaluate_pre_send(emptied, fixture.effect_id, matching_control(emptied),
                               capable_venue(), open_quota())
                 .permitted(),
            "the authored effect survived the outbox it came from");
}

void test_a_late_send_against_a_closed_order_is_fenced_not_retained() {
    auto fixture = authorized_submit();
    const auto effect_id = fixture.effect_id;

    fixture.state.outbox_effects[0].state = OutboxState::incomplete;
    RecordingTransport settled_transport;
    const auto settled = deliver(fixture.state, effect_id,
                                 matching_control(fixture.state), capable_venue(),
                                 open_quota(), settled_transport);
    require(settled.refusal == PreSendRefusal::effect_not_deliverable,
            "a settled-incomplete effect was still considered deliverable");
    require(settled_transport.send_count() == 0,
            "a settled-incomplete effect reached the transport");

    auto advanced = fixture.state;
    advanced.outbox_effects[0].state = OutboxState::pending;
    const auto stale_control = matching_control(advanced);
    advanced.executor_generation =
        ExecutorGeneration{advanced.executor_generation.value() + 1};
    RecordingTransport generation_transport;
    require(deliver(advanced, effect_id, stale_control, capable_venue(),
                    open_quota(), generation_transport)
                    .refusal == PreSendRefusal::stale_executor_generation,
            "a deposed gateway's late send was not fenced by generation");
    require(generation_transport.send_count() == 0,
            "a deposed gateway's late send reached the transport");

    auto killed = fixture.state;
    killed.outbox_effects[0].state = OutboxState::pending;
    const auto pre_kill_control = matching_control(killed);
    killed.kill_epoch = KillEpoch{killed.kill_epoch.value() + 1};
    RecordingTransport kill_transport;
    require(deliver(killed, effect_id, pre_kill_control, capable_venue(),
                    open_quota(), kill_transport)
                    .refusal == PreSendRefusal::stale_kill_epoch,
            "a late send from before the kill was not fenced by kill epoch");
    require(kill_transport.send_count() == 0,
            "a late send from before the kill reached the transport");

    const auto authorized = materialize_order_effect(fixture.state, effect_id);
    require(authorized.has_value(), "the fixture effect could not be rebuilt");
    require(authorized->executor_generation == fixture.state.executor_generation &&
                authorized->kill_epoch == fixture.state.kill_epoch,
            "the effect does not carry the authority it was created under, so a "
            "gateway holding it could not be fenced by its own bytes");

    const auto rebuilt = materialize_order_effect(fixture.state, effect_id);
    require(rebuilt.has_value() && rebuilt->effect_id == effect_id &&
                encode_payload(*rebuilt).bytes == encode_payload(*authorized).bytes,
            "the same effect did not rebuild to the same identity and bytes");
}

void test_an_unknown_or_unreconstructable_effect_is_refused() {
    const auto fixture = authorized_submit();
    RecordingTransport transport;
    require(deliver(fixture.state, EffectId{999'999}, matching_control(fixture.state),
                    capable_venue(), open_quota(), transport)
                    .refusal == PreSendRefusal::effect_not_found &&
                transport.send_count() == 0,
            "an unknown EffectId was not refused");

    auto corrupt = fixture.state;
    corrupt.outbox_effects[0].payload_digest = digest(0xEE);
    RecordingTransport second;
    require(deliver(corrupt, fixture.effect_id, matching_control(corrupt),
                    capable_venue(), open_quota(), second)
                    .refusal == PreSendRefusal::payload_unreconstructable &&
                second.send_count() == 0,
            "an unreconstructable effect reached the transport");
}

void test_a_settled_effect_is_not_deliverable() {
    const auto fixture = authorized_submit();
    for (const auto settled : {OutboxState::acknowledged, OutboxState::rejected,
                               OutboxState::incomplete, OutboxState::conflict}) {
        auto state = fixture.state;
        state.outbox_effects[0].state = settled;
        RecordingTransport transport;
        require(deliver(state, fixture.effect_id, matching_control(state),
                        capable_venue(), open_quota(), transport)
                        .refusal == PreSendRefusal::effect_not_deliverable &&
                    transport.send_count() == 0,
                "a settled effect was delivered again");
    }
}

void test_gateway_local_delivery_states_are_never_read_from_the_cluster() {
    const auto fixture = authorized_submit();
    for (const auto local : {OutboxState::claimed, OutboxState::attempted}) {
        auto state = fixture.state;
        state.outbox_effects[0].state = local;
        RecordingTransport transport;
        require(deliver(state, fixture.effect_id, matching_control(state),
                        capable_venue(), open_quota(), transport)
                        .refusal == PreSendRefusal::effect_not_deliverable &&
                    transport.send_count() == 0,
                "a gateway-local delivery state was honoured as cluster state");
    }
}

}

int main() {
    try {
        test_planner_preserves_emergency_cancel_capacity();
        test_send_admission_requires_exact_catalog_context();
        test_send_admission_carries_historical_binding();
        test_cancel_requires_catalog_operation_support();
        test_public_gateway_paths_reject_oversized_counts();
        test_a_permitted_effect_is_sent_with_its_exact_canonical_bytes();
        test_the_guard_reads_no_clock_and_mutates_nothing();
        test_stale_control_authority_is_never_sent();
        test_a_reducing_order_is_sent_only_in_the_mode_that_decided_it();
        test_an_expired_authority_lease_stops_new_risk();
    test_an_unconfigured_lease_is_expired_not_unlimited();
    test_a_cancel_survives_what_stops_new_risk();
    test_the_emergency_lane_cannot_carry_new_risk();
    test_boolean_authority_cannot_renew();
    test_boolean_reading_cannot_promote_the_gateway();
    test_candidate_cache_age_filter_is_not_send_authority();
    test_a_venue_that_cannot_represent_the_intent_is_refused_not_approximated();
        test_exhausted_quota_defers_rather_than_sending();
        test_the_gateway_can_send_nothing_the_cluster_did_not_author();
    test_a_late_send_against_a_closed_order_is_fenced_not_retained();
    test_an_unknown_or_unreconstructable_effect_is_refused();
        test_a_settled_effect_is_not_deliverable();
        test_gateway_local_delivery_states_are_never_read_from_the_cluster();
        std::cout << "all V2 gateway tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 gateway test failed: " << error.what() << '\n';
        return 1;
    }
}
