// Phase 5: the delivery loop, and the gate it has to hold under faults.
//
// The gate is stated as three absolutes: no duplicate venue order, no lost
// accepted fill, no unresolved state presented as healthy. This file drives the
// REAL kernel through the REAL pre-send guard and planner into the adversarial
// simulator, then checks all three after every seed.
//
// The shell here is deliberately tiny - read the tick, call the transport, drain
// responses - because every decision it would otherwise make lives in
// `plan_delivery` and `evaluate_pre_send`, which are pure functions of state.
// That is exactly what lets a nine-fault matrix be reproducible rather than
// anecdotal: a failing seed is a bug report, not a story about a flaky machine.

#include "velocita/v2/digest.hpp"
#include "velocita/v2/gateway.hpp"
#include "velocita/v2/metrics.hpp"
#include "velocita/v2/payload.hpp"
#include "velocita/v2/state_snapshot.hpp"
#include "velocita/v2/venue_simulator.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace {

using namespace velocita::v2;
using velocita::v2::sim::FaultPolicy;
using velocita::v2::sim::SendOutcome;
using velocita::v2::sim::QueryOutcome;
using velocita::v2::sim::SimulatedVenue;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Digest256 digest(const std::uint8_t marker) {
    Digest256 value;
    value.bytes[0] = static_cast<std::byte>(marker);
    return value;
}

// A request identity's payload digest must be nonzero, and distinct commands
// must not collide on (caller, sequence). One monotonic counter feeds both.
const char* name_of_state(const ClusterOrderState state) {
    switch (state) {
        case ClusterOrderState::live:               return "live";
        case ClusterOrderState::cancel_pending:     return "cancel_pending";
        case ClusterOrderState::reconciling:        return "reconciling";
        case ClusterOrderState::conflict:           return "conflict";
        case ClusterOrderState::partially_filled:   return "partially_filled";
        default:                                    return "other";
    }
}

Digest256 request_digest(const std::uint64_t counter) {
    return digest(static_cast<std::uint8_t>(1 + counter % 255));
}

// --- fixture ---------------------------------------------------------------
//
// Identical in economics to the pre-send guard's fixture, so a failure here is a
// delivery failure rather than a fixture divergence.

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

TradingState ready_state() {
    TradingState state;
    state.state_version = 2;
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
         kVenueDirectivePlaceLimit, kVenueTifGtc, 1},
        1, 2};
    state.risk_account_count = 1;
    state.risk_accounts[0] = {
        AccountId{5}, InstrumentId{4}, RiskPolicyVersion{6}, RiskVersion{1},
        PositionVersion{1}, QuantityLots::from_atoms(4),
        QuantityLots::from_atoms(8), {}, MoneyMicros::from_atoms(4'000),
        MoneyMicros::from_atoms(8'000), {}, {}, 1'000'000, 2'500};
    return state;
}

DecisionFrame frame(const std::uint64_t sequence, const Digest256& evidence,
                    const TradingState& state) {
    DecisionFrame value;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.feature_schema_id = FeatureSchemaId{3};
    value.venue_id = VenueId{9};
    value.venue_session_id = VenueSessionId{1};
    value.venue_sequence = VenueSequence{sequence};
    value.source_id = SourceId{12};
    value.source_generation = SourceGeneration{1};
    value.source_sequence = SourceSequence{sequence};
    value.event_time_ns = 150;
    value.receive_time_ns = 151;
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.feature_count = 1;
    value.features[0] = FeatureMicros::from_atoms(11);
    value.market_data_status = MarketDataStatus::ready;
    value.market_data_age_ns = 1;
    value.position_version = state.risk_accounts[0].position_version;
    value.risk_version = state.risk_accounts[0].risk_version;
    value.raw_evidence_digest = evidence;
    return value;
}

constexpr VenueCapabilityView capable_venue() {
    return {VenueId{9}, VenueCapabilitiesVersion{8}, true, true};
}

// --- planner unit tests ----------------------------------------------------

struct Fixture {
    TradingState state;
    EffectId effect_id;
};

Fixture one_authorized_submit() {
    auto state = ready_state();
    state.source_count = 1;
    state.sources[0] = {SourceId{12}, SourceGeneration{1}, SourceSequence{1}};
    state.source_event_count = 1;
    state.source_events[0] = {SourceId{12}, SourceGeneration{1},
                              SourceSequence{1}, digest(0x44)};
    ApplyScratch scratch;
    const Command evaluate{{CallerId{1}, RequestSequence{1}, digest(0x51)},
                           EvaluateDecisionFrame{DecisionId{20},
                                                 EvaluationMode::authoritative,
                                                 frame(1, digest(0x44), state)}};
    require(apply(state, evaluate, scratch).code == ApplyCode::applied,
            "planner fixture could not retain its decision");
    const Command authorize{{CallerId{1}, RequestSequence{2}, digest(0x52)},
                            AuthorizeDecision{DecisionId{20}}};
    const auto submit = apply(state, authorize, scratch);
    require(submit.code == ApplyCode::applied,
            "planner fixture could not authorize its submit");
    return {state, submit.effect_id()};
}

GatewayControlView control_of(const TradingState& state) {
    return {state.executor_generation, state.kill_epoch, state.strategy_mode};
}

void test_a_fresh_pending_effect_is_scheduled_once() {
    const auto fixture = one_authorized_submit();
    const GatewayDeliveryState delivery;
    const auto plan = plan_delivery(fixture.state, delivery, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4},
                                    GatewayTimeouts{10}, 0);
    require(plan.send_count == 1 && plan.to_send[0] == fixture.effect_id,
            "the planner did not schedule a fresh pending effect");
    require(plan.ambiguous_count == 0,
            "a freshly authorized effect was reported ambiguous");
}

void test_an_outstanding_effect_is_not_resent() {
    // At-least-once delivery is safe only because a retry keeps its identity and
    // reconciliation precedes it. Resending while the first attempt is still
    // outstanding is neither of those things.
    const auto fixture = one_authorized_submit();
    GatewayDeliveryState delivery;
    require(record_send(delivery, fixture.effect_id, 0), "record_send failed");
    const auto plan = plan_delivery(fixture.state, delivery, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4},
                                    GatewayTimeouts{10}, 1);
    require(plan.send_count == 0,
            "the planner rescheduled an effect that was still outstanding");
    const auto* attempt = find_attempt(delivery, fixture.effect_id);
    require(attempt != nullptr && attempt->awaiting_response && attempt->attempts == 1,
            "the attempt record does not reflect the outstanding send");
}

void test_a_passed_deadline_is_ambiguous_and_not_also_a_retry() {
    // The single most important ordering property in the planner. A tick that
    // both declares an outcome unknown and blindly resends it is precisely what
    // G-026 forbids, and it is an easy mistake to make by computing sends first.
    const auto fixture = one_authorized_submit();
    GatewayDeliveryState delivery;
    require(record_send(delivery, fixture.effect_id, 0), "record_send failed");
    const auto plan = plan_delivery(fixture.state, delivery, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4},
                                    GatewayTimeouts{8}, 20);
    require(plan.ambiguous_count == 1 && plan.now_ambiguous[0] == fixture.effect_id,
            "a passed deadline did not become ambiguous");
    require(plan.send_count == 0,
            "a timed-out effect was blindly retried in the same tick");
}

void test_an_answered_effect_is_never_blindly_resent() {
    // The venue answered, so an order may be resting there. Whether to send
    // again is reconciliation's judgement, not the planner's - and until
    // reconciliation exists, the honest answer is "no".
    const auto fixture = one_authorized_submit();
    GatewayDeliveryState delivery;
    require(record_send(delivery, fixture.effect_id, 0), "record_send failed");
    require(record_response(delivery, fixture.effect_id),
            "record_response did not find the outstanding attempt");
    const auto* attempt = find_attempt(delivery, fixture.effect_id);
    require(attempt != nullptr && !attempt->awaiting_response &&
                !attempt->resend_permitted,
            "a venue response left the effect marked resendable");
    const auto plan = plan_delivery(fixture.state, delivery, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4},
                                    GatewayTimeouts{10}, 5);
    require(plan.send_count == 0,
            "an effect the venue had already answered was blindly resent");
}

void test_a_provably_unsent_effect_retries_under_its_own_identity() {
    // The effect identity is the venue's deduplication key. A retry that changed
    // it would turn one intent into two orders, which is the failure the whole
    // gate exists to exclude. A retry that never happened would wedge the slot
    // on a transient rate limit, which is the opposite failure.
    const auto fixture = one_authorized_submit();
    GatewayDeliveryState delivery;
    require(record_send(delivery, fixture.effect_id, 0), "first record_send failed");
    require(record_unsent(delivery, fixture.effect_id),
            "record_unsent did not find the attempt");
    const auto plan = plan_delivery(fixture.state, delivery, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4},
                                    GatewayTimeouts{10}, 5);
    require(plan.send_count == 1 && plan.to_send[0] == fixture.effect_id,
            "an effect that never reached the venue was not retried");
    require(record_send(delivery, fixture.effect_id, 5), "second record_send failed");
    const auto* attempt = find_attempt(delivery, fixture.effect_id);
    require(attempt != nullptr && attempt->effect_id == fixture.effect_id &&
                attempt->attempts == 2 && attempt->sent_at == 5,
            "a retry did not preserve its effect identity across attempts");
    require(!attempt->resend_permitted,
            "a fresh send left the provably-unsent permission standing");
}

void test_unsent_retries_are_bounded() {
    // A venue that refuses for pacing forever must not become an unbounded retry
    // loop. Exhaustion leaves the effect pending and unsettled rather than sent.
    const auto fixture = one_authorized_submit();
    GatewayDeliveryState delivery;
    const GatewayTimeouts timeouts{10, 3};
    for (std::uint32_t round = 0; round < timeouts.max_send_attempts; ++round) {
        const auto plan = plan_delivery(fixture.state, delivery,
                                        control_of(fixture.state), capable_venue(),
                                        SendQuotaView{4}, timeouts, round);
        require(plan.send_count == 1,
                "a retry below the attempt bound was not scheduled");
        require(record_send(delivery, fixture.effect_id, round), "record_send failed");
        require(record_unsent(delivery, fixture.effect_id), "record_unsent failed");
    }
    const auto plan = plan_delivery(fixture.state, delivery, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4}, timeouts, 9);
    require(plan.send_count == 0, "the retry bound did not stop scheduling");
    require(plan.ambiguous_count == 0,
            "an effect that never left the process was reported ambiguous");
}

void test_quota_bounds_the_plan() {
    const auto fixture = one_authorized_submit();
    const GatewayDeliveryState delivery;
    require(plan_delivery(fixture.state, delivery, control_of(fixture.state),
                          capable_venue(), SendQuotaView{0}, GatewayTimeouts{10}, 0)
                    .send_count == 0,
            "a zero quota still produced sends");
}

void test_a_halted_cluster_plans_no_sends() {
    auto fixture = one_authorized_submit();
    fixture.state.strategy_mode = StrategyMode::halted;
    const GatewayDeliveryState delivery;
    const auto plan = plan_delivery(fixture.state, delivery, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4},
                                    GatewayTimeouts{10}, 0);
    require(plan.send_count == 0, "a halted cluster still planned new risk");
}

void test_the_planner_only_schedules_what_the_guard_permits() {
    // The planner and the guard are separate functions, so they can disagree.
    // If they ever do, the shell would either send something refused or stall on
    // something permitted. Both are silent failures, so assert the agreement.
    const auto fixture = one_authorized_submit();
    const GatewayDeliveryState delivery;
    const auto plan = plan_delivery(fixture.state, delivery, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4},
                                    GatewayTimeouts{10}, 0);
    for (std::size_t index = 0; index < plan.send_count; ++index) {
        require(evaluate_pre_send(fixture.state, plan.to_send[index],
                                  control_of(fixture.state), capable_venue(),
                                  SendQuotaView{4})
                    .permitted(),
                "the planner scheduled an effect the guard refuses");
    }
}

// --- reconciliation planner tests ------------------------------------------

// Drive the fixture's one order into submission_unknown the way the loop does:
// a send whose deadline passed, reported to the cluster as unknown.
Fixture stranded_submit() {
    auto fixture = one_authorized_submit();
    ApplyScratch scratch;
    ClusterOrderId order{};
    for (std::size_t index = 0; index < fixture.state.outbox_effect_count; ++index) {
        if (fixture.state.outbox_effects[index].effect_id == fixture.effect_id) {
            order = fixture.state.outbox_effects[index].cluster_order_id;
        }
    }
    require(order.value() != 0, "the stranded fixture has no cluster order");
    VenueObservation unknown;
    unknown.kind = VenueObservationKind::effect_unknown;
    unknown.venue_id = VenueId{9};
    unknown.venue_session_id = VenueSessionId{1};
    unknown.venue_sequence = VenueSequence{5'000};
    unknown.cluster_order_id = order;
    unknown.effect_id = fixture.effect_id;
    unknown.evidence_digest = digest(0x7E);
    const Command report{{CallerId{1}, RequestSequence{3},
                          observation_fingerprint(unknown)},
                         ObserveOrderEvent{unknown}};
    require(apply(fixture.state, report, scratch).code == ApplyCode::applied,
            "the stranded fixture could not report an unknown outcome");
    return fixture;
}

ClusterOrderId only_order(const TradingState& state) {
    require(state.order_count == 1, "fixture does not hold exactly one order");
    return state.orders[0].cluster_order_id;
}

// No send has been attempted in the reconciliation unit tests, so the delivery
// arena is empty. `delivery_exhausted` gets its own test below.
const GatewayDeliveryState no_attempts;

constexpr VenueQueryView authoritative_answer(const VenueQueryStatus status) {
    VenueQueryView answer;
    answer.status = status;
    answer.venue_session_id = VenueSessionId{1};
    answer.authoritative = true;
    return answer;
}

void test_reconciliation_targets_only_unresolvable_exposure() {
    // A live, unfilled order is waiting for the market, not for reconciliation.
    // Querying it would be noise that hides the cases that matter.
    const auto live = one_authorized_submit();
    const GatewayReconciliationState fresh;
    require(plan_reconciliation(live.state, fresh, no_attempts,
                                control_of(live.state),
                                capable_venue(), GatewayTimeouts{}, 0)
                    .query_count == 0,
            "reconciliation was planned for an order that needs none");

    const auto stranded = stranded_submit();
    const auto plan = plan_reconciliation(stranded.state, fresh, no_attempts,
                                          control_of(stranded.state), capable_venue(),
                                          GatewayTimeouts{}, 0);
    require(plan.query_count == 1, "a stranded order was not scheduled for query");
    require(plan.to_query[0].cluster_order_id == only_order(stranded.state) &&
                plan.to_query[0].effect_id == stranded.effect_id &&
                plan.to_query[0].need == ReconciliationNeed::submission_unknown,
            "the reconciliation target does not name the stranded order");
}

void test_one_outstanding_query_at_a_time() {
    // Two queries in flight can answer out of order, letting a stale view
    // overwrite a fresh one - a conflict the cluster would adjudicate for no
    // reason at all.
    const auto stranded = stranded_submit();
    GatewayReconciliationState reconciliation;
    require(record_query(reconciliation, only_order(stranded.state), 0),
            "record_query failed");
    require(plan_reconciliation(stranded.state, reconciliation, no_attempts,
                                control_of(stranded.state), capable_venue(),
                                GatewayTimeouts{}, 50)
                    .query_count == 0,
            "a second query was planned while the first was outstanding");
}

void test_the_query_interval_is_respected() {
    const auto stranded = stranded_submit();
    const GatewayTimeouts timeouts;
    GatewayReconciliationState reconciliation;
    require(record_query(reconciliation, only_order(stranded.state), 0),
            "record_query failed");
    require(record_answer(reconciliation, only_order(stranded.state)),
            "record_answer failed");
    require(plan_reconciliation(stranded.state, reconciliation, no_attempts,
                                control_of(stranded.state), capable_venue(), timeouts,
                                timeouts.query_interval - 1)
                    .query_count == 0,
            "a query was planned before its interval elapsed");
    require(plan_reconciliation(stranded.state, reconciliation, no_attempts,
                                control_of(stranded.state), capable_venue(), timeouts,
                                timeouts.query_interval)
                    .query_count == 1,
            "a query was not planned once its interval elapsed");
}

void test_reconciliation_queries_are_bounded() {
    const auto stranded = stranded_submit();
    const GatewayTimeouts timeouts;
    GatewayReconciliationState reconciliation;
    const auto order = only_order(stranded.state);
    Tick now = 0;
    for (std::uint32_t round = 0; round < timeouts.max_query_attempts; ++round) {
        require(plan_reconciliation(stranded.state, reconciliation, no_attempts,
                                    control_of(stranded.state), capable_venue(),
                                    timeouts, now)
                        .query_count == 1,
                "a query below the attempt bound was not planned");
        require(record_query(reconciliation, order, now), "record_query failed");
        require(record_answer(reconciliation, order), "record_answer failed");
        now += timeouts.query_interval;
    }
    require(plan_reconciliation(stranded.state, reconciliation, no_attempts,
                                control_of(stranded.state), capable_venue(), timeouts,
                                now)
                    .query_count == 0,
            "the query bound did not stop reconciliation");
    // Exhaustion means the gateway stops asking, NOT that anything was concluded.
    // The order stays unresolved and visible, which is the G-074a situation.
    require(collect_metrics(stranded.state).unresolved_orders == 1,
            "an exhausted reconciliation stopped reporting its unresolved order");
}

void test_reconciliation_is_fenced_by_generation_but_not_by_halt() {
    const auto stranded = stranded_submit();
    const GatewayReconciliationState fresh;

    // A superseded gateway must not write venue evidence into consensus at all.
    GatewayControlView stale = control_of(stranded.state);
    stale.executor_generation =
        ExecutorGeneration{stranded.state.executor_generation.value() - 1};
    require(plan_reconciliation(stranded.state, fresh, no_attempts, stale,
                                capable_venue(), GatewayTimeouts{}, 0)
                    .query_count == 0,
            "a superseded gateway planned to write venue evidence");

    // A halt stops NEW risk. Reconciliation originates none - it only resolves
    // exposure that already exists - and a halted cluster holding a blocked slot
    // is exactly when resolving it matters most.
    auto halted = stranded.state;
    halted.strategy_mode = StrategyMode::halted;
    require(plan_reconciliation(halted, fresh, no_attempts, control_of(halted),
                                capable_venue(), GatewayTimeouts{}, 0)
                    .query_count == 1,
            "a halted cluster refused to resolve exposure it already holds");
}

void test_only_a_trustworthy_answer_may_be_submitted() {
    const GatewayTimeouts timeouts;

    // Silence proves nothing. In particular it does NOT prove the order is
    // absent, which is the inference that turns a network problem into a
    // duplicate order.
    require(classify_query(authoritative_answer(VenueQueryStatus::unavailable),
                           nullptr, timeouts) == QueryDisposition::unavailable,
            "an unavailable query was treated as an answer");

    // A partial view of a fill history is indistinguishable from a short one.
    auto partial = authoritative_answer(VenueQueryStatus::known);
    partial.authoritative = false;
    require(classify_query(partial, nullptr, timeouts) == QueryDisposition::incomplete,
            "a non-authoritative answer was treated as complete");

    require(classify_query(authoritative_answer(VenueQueryStatus::known), nullptr,
                           timeouts) == QueryDisposition::submit,
            "a complete answer was not submitted");

    // The most valuable answer reconciliation can get: nothing was ever placed,
    // so the reservation can be released rather than held forever.
    require(classify_query(authoritative_answer(VenueQueryStatus::not_found), nullptr,
                           timeouts) == QueryDisposition::submit,
            "a definitive not-found was not submitted");

    ReconciliationAttempt spent;
    spent.attempts = timeouts.max_query_attempts;
    require(classify_query(authoritative_answer(VenueQueryStatus::unavailable), &spent,
                           timeouts) == QueryDisposition::exhausted,
            "a spent reconciliation kept reporting as merely unavailable");
}

void test_a_delivery_exhausted_order_is_reconciled_not_abandoned() {
    // The retry bound of 9.2.2 stops an unbounded loop, but on its own it would
    // trade that for a permanently wedged slot. A query is what breaks the tie:
    // the venue can say it never had the order, and the reservation is released.
    const auto fixture = one_authorized_submit();
    const GatewayTimeouts timeouts;
    const GatewayReconciliationState fresh;

    // Not yet spent: an order still waiting its turn must not be queried, or the
    // query races the send.
    GatewayDeliveryState delivery;
    require(record_send(delivery, fixture.effect_id, 0), "record_send failed");
    require(record_unsent(delivery, fixture.effect_id), "record_unsent failed");
    require(plan_reconciliation(fixture.state, fresh, delivery,
                                control_of(fixture.state), capable_venue(), timeouts,
                                100)
                    .query_count == 0,
            "an order still eligible to be sent was queried");

    while (find_attempt(delivery, fixture.effect_id)->attempts <
           timeouts.max_send_attempts) {
        require(record_send(delivery, fixture.effect_id, 1), "record_send failed");
        require(record_unsent(delivery, fixture.effect_id), "record_unsent failed");
    }
    const auto plan = plan_reconciliation(fixture.state, fresh, delivery,
                                          control_of(fixture.state), capable_venue(),
                                          timeouts, 100);
    require(plan.query_count == 1 &&
                plan.to_query[0].need == ReconciliationNeed::delivery_exhausted,
            "a delivery-exhausted order was abandoned rather than reconciled");
}

void test_the_reconciliation_planner_reads_no_clock_and_mutates_nothing() {
    const auto stranded = stranded_submit();
    auto probe = stranded.state;
    const GatewayReconciliationState fresh;
    const auto first = plan_reconciliation(probe, fresh, no_attempts,
                                           control_of(probe), capable_venue(),
                                           GatewayTimeouts{}, 3);
    require(std::memcmp(&probe, &stranded.state, sizeof(TradingState)) == 0,
            "the reconciliation planner mutated authoritative state");
    const auto second = plan_reconciliation(probe, fresh, no_attempts,
                                            control_of(probe), capable_venue(),
                                            GatewayTimeouts{}, 3);
    require(first == second, "the reconciliation planner is not a function");
}

// --- adjudication of a reconciliation answer -------------------------------

// Build the observation a gateway submits after a trustworthy query answer.
// `venue_order` zero means the venue denies holding the order at all.
VenueObservation reconciled(const TradingState& state, EffectId effect_id,
                            const std::uint64_t venue_order,
                            const std::int64_t cumulative_filled,
                            const std::uint64_t sequence = 6'000) {
    VenueObservation answer;
    answer.kind = VenueObservationKind::order_reconciled;
    answer.venue_id = VenueId{9};
    answer.venue_session_id = VenueSessionId{1};
    answer.venue_sequence = VenueSequence{sequence};
    answer.effect_id = effect_id;
    answer.venue_order_id = VenueOrderId{venue_order};
    answer.quantity = QuantityLots::from_atoms(cumulative_filled);
    answer.evidence_digest = digest(0x6C);
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].effect_id == effect_id) {
            answer.cluster_order_id = state.outbox_effects[index].cluster_order_id;
        }
    }
    return answer;
}

ApplyResult submit_answer(TradingState& state, const VenueObservation& answer,
                          const std::uint64_t sequence) {
    ApplyScratch scratch;
    const Command command{{CallerId{1}, RequestSequence{sequence},
                           observation_fingerprint(answer)},
                          ObserveOrderEvent{answer}};
    return apply(state, command, scratch);
}

const ClusterOrder& sole_order(const TradingState& state) {
    require(state.order_count == 1, "fixture does not hold exactly one order");
    return state.orders[0];
}

bool risk_released(const TradingState& state) {
    const auto& order = sole_order(state);
    return order.reservation_index < state.reservation_count &&
           state.reservations[order.reservation_index].state ==
               ReservationState::released;
}

// Book a real partial fill, so a "the cluster has already booked N" fixture is a
// state the kernel can actually produce.
//
// Hand-setting `reservation.filled_quantity` looks equivalent and is not: it
// leaves filled + remaining greater than the original, which is an invariant the
// snapshot encoder checks and refuses. A test that asserts behaviour on a state
// the reducer cannot reach proves nothing about the reducer.
EffectId submit_effect_of(const TradingState& state) {
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].operation == EffectOperation::submit) {
            return state.outbox_effects[index].effect_id;
        }
    }
    return EffectId{};
}

void book_partial_fill(TradingState& state, const EffectId effect_id,
                       const std::int64_t quantity, const std::uint64_t sequence) {
    VenueObservation fill;
    fill.kind = VenueObservationKind::fill;
    fill.venue_id = VenueId{9};
    fill.venue_session_id = VenueSessionId{1};
    fill.venue_sequence = VenueSequence{sequence};
    fill.cluster_order_id = sole_order(state).cluster_order_id;
    fill.effect_id = effect_id;
    fill.venue_order_id = VenueOrderId{88};
    fill.execution_id = ExecutionId{sequence};
    fill.side = Side::buy;
    fill.price = PriceTicks::from_atoms(11);
    fill.quantity = QuantityLots::from_atoms(quantity);
    fill.evidence_digest = digest(0x78);
    ApplyScratch scratch;
    const Command command{{CallerId{1}, RequestSequence{sequence},
                           observation_fingerprint(fill)},
                          ObserveFill{fill}};
    const auto applied = apply(state, command, scratch);
    require(applied.code == ApplyCode::applied,
            "the partial-fill fixture was refused, code " +
                std::to_string(static_cast<int>(applied.code)));
    require(state.reservations[sole_order(state).reservation_index].filled_quantity ==
                QuantityLots::from_atoms(quantity),
            "the partial-fill fixture did not book its quantity");
}

// An acknowledged order with a cancel authorized and pending delivery. Built
// through real commands, because the cancel lifecycle is where hand-built state
// is most likely to be quietly impossible.
Fixture pending_cancel() {
    auto fixture = one_authorized_submit();
    ClusterOrderId order{};
    for (std::size_t index = 0; index < fixture.state.outbox_effect_count; ++index) {
        if (fixture.state.outbox_effects[index].effect_id == fixture.effect_id) {
            order = fixture.state.outbox_effects[index].cluster_order_id;
        }
    }
    VenueObservation ack;
    ack.kind = VenueObservationKind::order_acknowledged;
    ack.venue_id = VenueId{9};
    ack.venue_session_id = VenueSessionId{1};
    ack.venue_sequence = VenueSequence{7'100};
    ack.cluster_order_id = order;
    ack.effect_id = fixture.effect_id;
    ack.venue_order_id = VenueOrderId{88};
    ack.evidence_digest = digest(0x71);
    require(submit_answer(fixture.state, ack, 10).code == ApplyCode::applied,
            "the cancel fixture could not acknowledge its order");

    ApplyScratch scratch;
    const Command cancel{{CallerId{1}, RequestSequence{11}, digest(0x72)},
                         RequestCancel{order}};
    const auto requested = apply(fixture.state, cancel, scratch);
    require(requested.code == ApplyCode::applied,
            "the cancel fixture could not authorize its cancel");
    require(sole_order(fixture.state).state == ClusterOrderState::cancel_pending,
            "the cancel fixture is not cancel-pending");
    return {fixture.state, requested.effect_id()};
}

// A cancel whose response never arrived: the effect timed out and the gateway
// reported it unknown. Note the order state is `submission_unknown` even though
// the SUBMIT is not in doubt - the reducer overloads that state to mean "the
// current effect's outcome is unknown", and the current effect here is a cancel.
Fixture silent_cancel() {
    auto fixture = pending_cancel();
    VenueObservation unknown;
    unknown.kind = VenueObservationKind::effect_unknown;
    unknown.venue_id = VenueId{9};
    unknown.venue_session_id = VenueSessionId{1};
    unknown.venue_sequence = VenueSequence{7'200};
    unknown.cluster_order_id = sole_order(fixture.state).cluster_order_id;
    unknown.effect_id = fixture.effect_id;
    unknown.evidence_digest = digest(0x73);
    require(submit_answer(fixture.state, unknown, 12).code == ApplyCode::applied,
            "the silent-cancel fixture could not go unknown");
    require(sole_order(fixture.state).state ==
                ClusterOrderState::submission_unknown,
            "the silent-cancel fixture is not unknown");
    return fixture;
}

// A cancel that went silent on an order which had genuinely booked part of its
// quantity. Built in the order these things actually happen: the order fills
// partially, the remainder is cancelled, and the cancel goes silent.
//
// The fill has to be booked BEFORE the cancel exists. Fill evidence is scoped to
// the order's submit effect, and requesting a cancel makes the cancel effect
// current - so a fill offered afterwards names the wrong effect and is correctly
// refused. That ordering constraint is easy to miss and is exactly why this is a
// helper rather than four lines repeated in each test.
Fixture silent_cancel_with_booked_fill(const std::int64_t booked) {
    auto fixture = one_authorized_submit();
    const auto submit_effect = fixture.effect_id;
    VenueObservation ack;
    ack.kind = VenueObservationKind::order_acknowledged;
    ack.venue_id = VenueId{9};
    ack.venue_session_id = VenueSessionId{1};
    ack.venue_sequence = VenueSequence{5'200};
    ack.cluster_order_id = sole_order(fixture.state).cluster_order_id;
    ack.effect_id = submit_effect;
    ack.venue_order_id = VenueOrderId{88};
    ack.evidence_digest = digest(0x7A);
    require(submit_answer(fixture.state, ack, 5'201).code == ApplyCode::applied,
            "the booked silent-cancel fixture could not acknowledge");
    book_partial_fill(fixture.state, submit_effect, booked, 5'202);

    ApplyScratch scratch;
    const Command cancel{{CallerId{1}, RequestSequence{5'203}, digest(0x7B)},
                         RequestCancel{sole_order(fixture.state).cluster_order_id}};
    const auto requested = apply(fixture.state, cancel, scratch);
    require(requested.code == ApplyCode::applied,
            "the booked silent-cancel fixture could not authorize its cancel");

    VenueObservation unknown;
    unknown.kind = VenueObservationKind::effect_unknown;
    unknown.venue_id = VenueId{9};
    unknown.venue_session_id = VenueSessionId{1};
    unknown.venue_sequence = VenueSequence{5'204};
    unknown.cluster_order_id = sole_order(fixture.state).cluster_order_id;
    unknown.effect_id = requested.effect_id();
    unknown.evidence_digest = digest(0x7C);
    require(submit_answer(fixture.state, unknown, 5'205).code == ApplyCode::applied,
            "the booked silent-cancel fixture could not go unknown");
    require(fixture.state.reservations[sole_order(fixture.state).reservation_index]
                    .filled_quantity == QuantityLots::from_atoms(booked),
            "the booked silent-cancel fixture lost its booked quantity");
    return {fixture.state, requested.effect_id()};
}

VenueObservation cancel_final(const TradingState& state, EffectId effect_id,
                              const std::int64_t cumulative_filled,
                              const std::uint64_t sequence = 7'300) {
    VenueObservation final;
    final.kind = VenueObservationKind::cancel_reconciled;
    final.venue_id = VenueId{9};
    final.venue_session_id = VenueSessionId{1};
    final.venue_sequence = VenueSequence{sequence};
    final.cluster_order_id = sole_order(state).cluster_order_id;
    final.effect_id = effect_id;
    final.venue_order_id = VenueOrderId{88};
    final.quantity = QuantityLots::from_atoms(cumulative_filled);
    final.evidence_digest = digest(0x74);
    return final;
}

void test_a_denied_order_releases_its_reservation_and_frees_the_slot() {
    // The single most valuable answer reconciliation can obtain: the venue never
    // had the order, so the held risk was never real. Economically this is a
    // submit the venue refused, which is why it needs no new lifecycle state.
    auto fixture = stranded_submit();
    require(!risk_released(fixture.state), "the stranded fixture holds no risk");
    const auto before = collect_metrics(fixture.state).unresolved_orders;
    require(before == 1, "the stranded fixture is not stranded");

    const auto applied = submit_answer(
        fixture.state, reconciled(fixture.state, fixture.effect_id, 0, 0), 20);
    require(applied.code == ApplyCode::applied,
            "a definitive not-found answer was refused");
    require(sole_order(fixture.state).state == ClusterOrderState::rejected,
            "a denied order did not resolve");
    require(risk_released(fixture.state), "a denied order kept its reservation");
    require(collect_metrics(fixture.state).unresolved_orders == 0,
            "a resolved order still counted as unresolved");

    // The payoff. A blocked slot that reconciliation resolves must actually
    // accept new risk again - otherwise the whole exercise recovers a metric
    // rather than the ability to trade.
    ApplyScratch scratch;
    auto& state = fixture.state;
    state.source_events[state.source_event_count++] = {
        SourceId{12}, SourceGeneration{1}, SourceSequence{2}, digest(0x45)};
    state.sources[0].sequence = SourceSequence{2};
    auto next = frame(2, digest(0x45), state);
    const Command evaluate{{CallerId{1}, RequestSequence{21}, digest(0x53)},
                           EvaluateDecisionFrame{DecisionId{21},
                                                 EvaluationMode::authoritative, next}};
    require(apply(state, evaluate, scratch).code == ApplyCode::applied,
            "a fresh decision was refused after reconciliation");
    const Command authorize{{CallerId{1}, RequestSequence{22}, digest(0x54)},
                            AuthorizeDecision{DecisionId{21}}};
    require(apply(state, authorize, scratch).code == ApplyCode::applied,
            "the logical slot stayed blocked after reconciliation resolved it");
}

void test_a_filled_submit_can_never_become_unknown() {
    // The invariant that decides which conflict branches are even reachable, and
    // it is worth pinning because two tests here were originally written against
    // a state it forbids.
    //
    // A fill proves the submit reached the venue, so `apply_observe_fill` marks
    // the submit effect acknowledged. A resolved effect cannot be declared
    // unknown again. Therefore a SUBMIT effect in `submission_unknown` always has
    // zero booked quantity - which means `order_reconciled` on a submit can only
    // agree or be ahead, never behind.
    //
    // The same is NOT true of a cancel. A partially filled order can have its
    // cancel go silent, and then the order is in `submission_unknown` with fills
    // already booked. That is why the conflict branches exist and why they are
    // reached through the cancel path.
    auto fixture = one_authorized_submit();
    book_partial_fill(fixture.state, fixture.effect_id, 1, 5'100);
    require(sole_order(fixture.state).state == ClusterOrderState::partially_filled,
            "the partial fill did not move the order");
    VenueObservation unknown;
    unknown.kind = VenueObservationKind::effect_unknown;
    unknown.venue_id = VenueId{9};
    unknown.venue_session_id = VenueSessionId{1};
    unknown.venue_sequence = VenueSequence{5'101};
    unknown.cluster_order_id = sole_order(fixture.state).cluster_order_id;
    unknown.effect_id = fixture.effect_id;
    unknown.evidence_digest = digest(0x79);
    require(submit_answer(fixture.state, unknown, 5'102).code ==
                ApplyCode::venue_observation_not_applicable,
            "a submit the venue had demonstrably filled was allowed to become "
            "unknown");
}

void test_a_denied_order_that_already_filled_is_a_conflict() {
    // The venue denies an order the cluster has booked fills against. One of the
    // two is wrong and the cluster cannot tell which, so it refuses to choose.
    // Reached through the CANCEL path, which is the only way an order can hold
    // booked fills while its current effect's outcome is unknown.
    auto fixture = silent_cancel_with_booked_fill(1);

    const auto applied = submit_answer(
        fixture.state, reconciled(fixture.state, fixture.effect_id, 0, 0), 20);
    require(applied.code == ApplyCode::applied,
            "a contradicting answer was not recorded");
    require(sole_order(fixture.state).state == ClusterOrderState::conflict,
            "the venue denying a filled order was not a conflict");
    require(!risk_released(fixture.state),
            "a conflicted order released its reservation");
    require(collect_metrics(fixture.state).unresolved_orders == 1,
            "a conflicted order stopped being reported as unresolved");
}

void test_agreement_resolves_the_effect_without_moving_position() {
    // The venue holds the order and agrees about what it has filled. The submit
    // definitely arrived, so the effect resolves - but nothing about the position
    // changes, because agreement is not a fill.
    auto fixture = stranded_submit();
    const auto before = fixture.state.risk_accounts[0];
    const auto applied = submit_answer(
        fixture.state, reconciled(fixture.state, fixture.effect_id, 77, 0), 20);
    require(applied.code == ApplyCode::applied, "an agreeing answer was refused");
    require(sole_order(fixture.state).state == ClusterOrderState::live,
            "an order the venue holds did not become live");
    require(!risk_released(fixture.state),
            "a live order at the venue released its reservation");
    require(fixture.state.risk_accounts[0].position_quantity ==
                before.position_quantity,
            "agreement moved the position");
}

void test_a_fill_the_cluster_never_saw_is_missing_evidence_not_a_conflict() {
    // The two directions of disagreement are NOT symmetric, and an earlier
    // version of this treated them as if they were.
    //
    // The venue reporting MORE than the cluster booked is missing evidence:
    // fills that close the gap may still be in flight, and under a reordering
    // transport they routinely are. Freezing the order terminal here would burn
    // a recoverable state on evidence that might be one tick away - and Phase 4
    // already depends on the opposite, since its cancel/fill/final permutation
    // test requires all six orderings to converge.
    auto fixture = stranded_submit();
    const auto before = fixture.state;
    const auto applied = submit_answer(
        fixture.state, reconciled(fixture.state, fixture.effect_id, 77, 2), 20);
    require(applied.code == ApplyCode::venue_observation_not_applicable,
            "an answer ahead of the cluster's evidence was not refused");
    require(sole_order(fixture.state).state == ClusterOrderState::submission_unknown,
            "a refused answer still moved the order");
    require(std::memcmp(&fixture.state.reservations, &before.reservations,
                        sizeof(before.reservations)) == 0,
            "a refused answer moved economics");
    // Still queryable, so reconciliation keeps asking rather than giving up.
    const GatewayReconciliationState fresh;
    const GatewayDeliveryState idle;
    require(plan_reconciliation(fixture.state, fresh, idle,
                                control_of(fixture.state), capable_venue(),
                                GatewayTimeouts{}, 0)
                    .query_count == 1,
            "a refused answer left the order unqueryable");
}

void test_a_venue_that_lost_a_fill_is_a_conflict() {
    // Below what is booked. A fill is never un-booked, so this can only be a
    // conflict - the alternative is letting a venue talk the cluster out of a
    // position it already recorded.
    auto fixture = silent_cancel_with_booked_fill(1);
    const auto applied = submit_answer(
        fixture.state, reconciled(fixture.state, fixture.effect_id, 88, 0), 20);
    require(applied.code == ApplyCode::applied,
            "a regressing answer was not recorded");
    require(sole_order(fixture.state).state == ClusterOrderState::conflict,
            "a venue losing a booked fill was not a conflict");
}

void test_replaying_the_same_answer_changes_nothing() {
    // Three independent layers must each hold, and the test asserts all three
    // rather than letting one cover for the others.
    auto fixture = stranded_submit();
    const auto answer = reconciled(fixture.state, fixture.effect_id, 77, 0);
    require(submit_answer(fixture.state, answer, 20).code == ApplyCode::applied,
            "the first answer was refused");
    const auto settled = fixture.state;

    // 1. Command identity. The same request returns its retained result.
    require(submit_answer(fixture.state, answer, 20).code == ApplyCode::applied,
            "an exact command replay did not return its retained result");

    // 2. Venue event identity. A fresh request carrying the same venue identity
    //    and the same evidence is recognised as already applied.
    require(submit_answer(fixture.state, answer, 21).code ==
                ApplyCode::already_applied,
            "a repeated venue event identity was not recognised");

    // 3. The case neither identity catches: the same answer under a FRESH venue
    //    sequence, which is exactly what a re-queried snapshot looks like after a
    //    reconnect mints a new session.
    const auto fresh = reconciled(fixture.state, fixture.effect_id, 77, 0, 6'001);
    (void)submit_answer(fixture.state, fresh, 22);
    require(std::memcmp(&fixture.state.risk_accounts, &settled.risk_accounts,
                        sizeof(settled.risk_accounts)) == 0 &&
                std::memcmp(&fixture.state.reservations, &settled.reservations,
                            sizeof(settled.reservations)) == 0 &&
                fixture.state.orders[0].state == settled.orders[0].state,
            "replaying a cumulative answer under a fresh identity moved economics");
}

void test_the_verdict_is_a_function_of_the_total_not_of_history() {
    // This is what cumulative evidence actually buys, and it is stronger than
    // "a replay is harmless". Because quantity is a TOTAL compared for equality
    // against what the cluster has booked, the verdict depends only on the answer
    // and the current booked quantity - never on how many answers preceded it, or
    // in what order they arrived. The adjudication needs no history, which is
    // precisely why it needs no reliable identity either.
    //
    // Same answer, same booked quantity, two states reached by different routes:
    // the verdict must match.
    const auto verdict_for = [](const std::int64_t booked,
                                const std::int64_t reported) {
        auto fixture = booked > 0 ? silent_cancel_with_booked_fill(booked)
                                  : stranded_submit();
        (void)submit_answer(fixture.state,
                            reconciled(fixture.state, fixture.effect_id, 88, reported),
                            20);
        return sole_order(fixture.state).state;
    };
    require(verdict_for(0, 0) == ClusterOrderState::live,
            "agreement at zero was not agreement");
    require(verdict_for(1, 1) == ClusterOrderState::partially_filled,
            "agreement at one was not agreement");
    require(verdict_for(0, 1) == ClusterOrderState::submission_unknown,
            "an answer ahead of the cluster's evidence was not left recoverable");
    require(verdict_for(1, 0) == ClusterOrderState::conflict,
            "a lost fill was not a conflict");
}

void test_a_reconciliation_answer_is_refused_where_it_cannot_apply() {
    // A live order is not reconciling anything. Accepting an answer here would
    // let a snapshot overwrite lifecycle the cluster already knows. Note that a
    // merely AUTHORIZED order is different and legitimately reconcilable - that
    // is the delivery-exhausted case - so the fixture has to be acknowledged
    // first to reach a state where an answer really has nothing to say.
    auto live = one_authorized_submit();
    VenueObservation ack;
    ack.kind = VenueObservationKind::order_acknowledged;
    ack.venue_id = VenueId{9};
    ack.venue_session_id = VenueSessionId{1};
    ack.venue_sequence = VenueSequence{7'000};
    ack.effect_id = live.effect_id;
    ack.venue_order_id = VenueOrderId{77};
    ack.evidence_digest = digest(0x6D);
    for (std::size_t index = 0; index < live.state.outbox_effect_count; ++index) {
        if (live.state.outbox_effects[index].effect_id == live.effect_id) {
            ack.cluster_order_id = live.state.outbox_effects[index].cluster_order_id;
        }
    }
    require(submit_answer(live.state, ack, 19).code == ApplyCode::applied,
            "the acknowledgement fixture was refused");
    require(sole_order(live.state).state == ClusterOrderState::live,
            "the acknowledgement fixture is not live");
    require(submit_answer(live.state, reconciled(live.state, live.effect_id, 77, 0),
                          20)
                    .code == ApplyCode::venue_observation_not_applicable,
            "a reconciliation answer applied to an order that needs none");

    // And once resolved, a second answer has nothing left to say.
    auto fixture = stranded_submit();
    require(submit_answer(fixture.state,
                          reconciled(fixture.state, fixture.effect_id, 0, 0), 20)
                    .code == ApplyCode::applied,
            "the first answer was refused");
    require(submit_answer(fixture.state,
                          reconciled(fixture.state, fixture.effect_id, 77, 0, 6'002),
                          21)
                    .code == ApplyCode::venue_observation_not_applicable,
            "a resolved order accepted a second, contradicting answer");
}

// --- the cancel path -------------------------------------------------------

void test_a_cancel_is_deliverable_in_every_strategy_mode() {
    // The mode gate asks "does control permit NEW RISK", and then applies the
    // answer to every effect. A cancel creates no risk - it can only reduce or
    // not change exposure - so gating it on that question is a category error,
    // and `cancel_only` is a mode whose own guard refuses the only thing it
    // exists to allow. Halted is the same case with higher stakes: it means
    // originate nothing new, not abandon the orders already working.
    const auto fixture = pending_cancel();
    for (const auto mode : {StrategyMode::normal, StrategyMode::no_new_risk,
                            StrategyMode::cancel_only, StrategyMode::reduce_only,
                            StrategyMode::halted}) {
        auto state = fixture.state;
        state.strategy_mode = mode;
        const auto decision = evaluate_pre_send(state, fixture.effect_id,
                                                control_of(state), capable_venue(),
                                                SendQuotaView{4});
        require(decision.permitted(),
                "a cancel was refused in strategy mode " +
                    std::to_string(static_cast<int>(mode)));
    }
}

void test_a_silent_cancel_redelivers_before_it_reconciles() {
    // A cancel may be redelivered under its own identity without proof, because
    // a duplicate cancel cannot create risk. So the cheap idempotent action is
    // tried first and the query round trip - which would most likely just tell
    // us to try it - is the fallback once redelivery has stopped working.
    const auto fixture = silent_cancel();
    const GatewayTimeouts timeouts;
    const GatewayReconciliationState fresh;
    GatewayDeliveryState delivery;

    // Redelivery, not reconciliation, while attempts remain.
    require(record_send(delivery, fixture.effect_id, 0), "record_send failed");
    require(record_response(delivery, fixture.effect_id), "record_response failed");
    const auto plan = plan_delivery(fixture.state, delivery, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4}, timeouts, 5);
    require(plan.send_count == 1 && plan.to_send[0] == fixture.effect_id,
            "a silent cancel was not redelivered under its own identity");
    require(plan_reconciliation(fixture.state, fresh, delivery,
                                control_of(fixture.state), capable_venue(), timeouts, 5)
                    .query_count == 0,
            "a cancel was queried while redelivery was still available");

    // Once attempts are spent, reconciliation takes over - and recognises it as a
    // stuck CANCEL, not a stuck submission. `submission_unknown` names the order
    // but describes its current effect; reading the order state alone would send
    // the gateway off to reconcile a submission that was never in doubt.
    while (find_attempt(delivery, fixture.effect_id)->attempts <
           timeouts.max_send_attempts) {
        require(record_send(delivery, fixture.effect_id, 5), "record_send failed");
        require(record_response(delivery, fixture.effect_id), "record_response failed");
    }
    const auto recon = plan_reconciliation(fixture.state, fresh, delivery,
                                           control_of(fixture.state), capable_venue(),
                                           timeouts, 9);
    require(recon.query_count == 1,
            "an exhausted silent cancel was not scheduled for query");
    require(recon.to_query[0].need == ReconciliationNeed::cancel_unconfirmed,
            "a silent cancel was mistaken for a silent submission");
    require(plan_delivery(fixture.state, delivery, control_of(fixture.state),
                          capable_venue(), SendQuotaView{4}, timeouts, 9)
                    .send_count == 0,
            "cancel redelivery was unbounded");
}

void test_a_rejected_cancel_does_not_prove_the_order_is_live() {
    // The hazard that makes cancel redelivery non-free. A venue refuses a cancel
    // for "already cancelled", "already filled", and "no such order" as readily
    // as for "still working", and a rejection of the DUPLICATE is now the common
    // case. Returning the order to `live` on that evidence would let a reordered
    // rejection flip a genuinely cancelled order back to working - after which
    // its real cancellation is refused because the effect is already resolved.
    auto fixture = pending_cancel();
    VenueObservation rejected;
    rejected.kind = VenueObservationKind::order_rejected;
    rejected.venue_id = VenueId{9};
    rejected.venue_session_id = VenueSessionId{1};
    rejected.venue_sequence = VenueSequence{7'400};
    rejected.cluster_order_id = sole_order(fixture.state).cluster_order_id;
    rejected.effect_id = fixture.effect_id;
    rejected.evidence_digest = digest(0x75);
    require(submit_answer(fixture.state, rejected, 20).code == ApplyCode::applied,
            "a cancel rejection was refused");
    require(sole_order(fixture.state).state == ClusterOrderState::reconciling,
            "a rejected cancel was read as proof the order is still working");

    // And `reconciling` must not be a dead end. `cancel_reconciled` can only say
    // the cancel DID take effect, so without `order_reconciled` speaking about a
    // cancel there would be no way back to a state a fresh cancel could be
    // authorized from.
    const auto applied = submit_answer(
        fixture.state, reconciled(fixture.state, fixture.effect_id, 88, 0, 7'401), 21);
    require(applied.code == ApplyCode::applied,
            "reconciliation could not report the order still working");
    require(sole_order(fixture.state).state == ClusterOrderState::live,
            "reconciliation did not restore a working order");

    ApplyScratch scratch;
    const Command fresh_cancel{{CallerId{1}, RequestSequence{22}, digest(0x76)},
                               RequestCancel{sole_order(fixture.state).cluster_order_id}};
    require(apply(fixture.state, fresh_cancel, scratch).code == ApplyCode::applied,
            "a fresh cancel could not be authorized after reconciliation");
}

void test_a_silent_cancel_can_be_finally_reconciled() {
    // The gate used to require `reconciling`, which is only reachable after a
    // `cancelled` observation. A cancel that simply went silent never gets one,
    // so the only way out of the state was closed - the order held its risk and
    // its slot with nothing able to resolve it.
    auto fixture = silent_cancel();
    const auto applied = submit_answer(
        fixture.state, cancel_final(fixture.state, fixture.effect_id, 0), 20);
    require(applied.code == ApplyCode::applied,
            "a silent cancel could not be finally reconciled");
    require(sole_order(fixture.state).state == ClusterOrderState::cancelled,
            "final cancel evidence did not resolve the order");
    require(risk_released(fixture.state), "a cancelled order kept its reservation");
}

void test_a_cancel_final_below_booked_is_a_conflict() {
    // The venue's final word reports less filled than the cluster booked. No
    // future evidence reduces a booked fill, so this gap can never close.
    auto fixture = silent_cancel_with_booked_fill(1);
    const auto applied = submit_answer(
        fixture.state, cancel_final(fixture.state, fixture.effect_id, 0), 20);
    require(applied.code == ApplyCode::applied,
            "a contradicting cancel final was not recorded");
    require(sole_order(fixture.state).state == ClusterOrderState::conflict,
            "a venue denying a booked fill in its final word was not a conflict");
    require(!risk_released(fixture.state),
            "a conflicted cancel released its reservation");
}

void test_a_cancel_final_above_booked_waits_rather_than_conflicting() {
    // The other direction, and the one Phase 4 already depends on. The fill that
    // closes this gap may still be in flight - the cancel/fill/final permutation
    // test applies these three in every order and requires all six to converge -
    // so this must refuse and let the evidence settle, not freeze terminal.
    auto fixture = silent_cancel();
    const auto before = fixture.state;
    const auto applied = submit_answer(
        fixture.state, cancel_final(fixture.state, fixture.effect_id, 1), 20);
    require(applied.code == ApplyCode::venue_observation_not_applicable,
            "final evidence ahead of the cluster was not refused");
    require(sole_order(fixture.state).state ==
                ClusterOrderState::submission_unknown,
            "a refused cancel final still moved the order");
    require(std::memcmp(&fixture.state.reservations, &before.reservations,
                        sizeof(before.reservations)) == 0,
            "a refused cancel final moved economics");
}

void test_states_reached_only_through_phase_five_survive_a_snapshot() {
    // Every Phase 5 path ends somewhere the reducer can be asked to persist, and
    // the snapshot codec was written and accepted before any of these paths
    // existed. A state that cannot round trip is a state the cluster cannot
    // restart from, which would make every guarantee above it conditional on
    // never restarting.
    const auto round_trips = [](const TradingState& state, const char* what) {
        std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
        const auto encoded = encode_state_snapshot(state, bytes);
        require(encoded.error == StateSnapshotError::none,
                std::string("state did not encode: ") + what);
        TradingState restored;
        StateSnapshotScratch scratch;
        const auto decoded = decode_state_snapshot(
            {bytes.data(), encoded.bytes_written}, restored, scratch);
        require(decoded.error == StateSnapshotError::none,
                std::string("state did not decode: ") + what);
        require(decoded.state_digest == encoded.state_digest,
                std::string("state digest changed across a round trip: ") + what);
        // Canonical equality, not `memcmp`. The struct has padding whose contents
        // depend on how the value was built, and the snapshot is the canonical
        // form by definition - so re-encoding the restored state and comparing
        // BYTES is the equality that actually means "the cluster restarts here".
        std::array<std::byte, kMaxStateSnapshotBytes> again{};
        const auto reencoded = encode_state_snapshot(restored, again);
        require(reencoded.error == StateSnapshotError::none &&
                    reencoded.bytes_written == encoded.bytes_written &&
                    std::equal(bytes.begin(),
                               bytes.begin() + static_cast<std::ptrdiff_t>(
                                                   encoded.bytes_written),
                               again.begin()),
                std::string("restored state did not re-encode identically: ") + what);
    };

    // A submission the venue denied: reservation released, order rejected.
    {
        auto fixture = stranded_submit();
        require(submit_answer(fixture.state,
                              reconciled(fixture.state, fixture.effect_id, 0, 0), 20)
                        .code == ApplyCode::applied,
                "denied-submit fixture failed");
        round_trips(fixture.state, "submission denied by the venue");
    }
    // A conflict reached through reconciliation, which is terminal but retains
    // its reservation and is deliberately unreclaimable.
    {
        auto fixture = silent_cancel_with_booked_fill(1);
        require(submit_answer(fixture.state,
                              reconciled(fixture.state, fixture.effect_id, 0, 0), 20)
                        .code == ApplyCode::applied,
                "reconciliation-conflict fixture failed");
        require(sole_order(fixture.state).state == ClusterOrderState::conflict,
                "reconciliation-conflict fixture is not conflicted");
        round_trips(fixture.state, "conflict reached through reconciliation");
    }
    // A cancel that went silent and was finally reconciled.
    {
        auto fixture = silent_cancel();
        require(submit_answer(fixture.state,
                              cancel_final(fixture.state, fixture.effect_id, 0), 20)
                        .code == ApplyCode::applied,
                "silent-cancel fixture failed");
        round_trips(fixture.state, "silent cancel finally reconciled");
    }
    // A rejected cancel restored to working by reconciliation - the state that
    // did not exist at all before this phase.
    {
        auto fixture = pending_cancel();
        VenueObservation rejected;
        rejected.kind = VenueObservationKind::order_rejected;
        rejected.venue_id = VenueId{9};
        rejected.venue_session_id = VenueSessionId{1};
        rejected.venue_sequence = VenueSequence{7'500};
        rejected.cluster_order_id = sole_order(fixture.state).cluster_order_id;
        rejected.effect_id = fixture.effect_id;
        rejected.evidence_digest = digest(0x77);
        require(submit_answer(fixture.state, rejected, 20).code == ApplyCode::applied,
                "rejected-cancel fixture failed");
        round_trips(fixture.state, "cancel rejected, lifecycle unknown");
        require(submit_answer(fixture.state,
                              reconciled(fixture.state, fixture.effect_id, 88, 0,
                                         7'501),
                              21)
                        .code == ApplyCode::applied,
                "cancel-restored fixture failed");
        round_trips(fixture.state, "cancel rejected then restored to working");
    }
}

void test_a_conflicted_order_keeps_accounting_valid_executions() {
    // MATERIAL DECISION A. A unique execution carrying venue identity, execution
    // identity, side, price, quantity and fee is STRONGER evidence than the
    // order-level cumulative snapshot that created the conflict. Refusing it left
    // the known position and remaining hold wrong while the kernel held every
    // field needed to book it safely.
    //
    // The decision separates two properties the code had conflated: the dispute
    // stays recorded, and valid accounting continues anyway.
    auto fixture = silent_cancel_with_booked_fill(1);
    require(submit_answer(fixture.state,
                          cancel_final(fixture.state, fixture.effect_id, 0, 9'500),
                          9'501)
                    .code == ApplyCode::applied,
            "the conflict fixture was refused");
    require(sole_order(fixture.state).state == ClusterOrderState::conflict,
            "the conflict fixture is not conflicted");

    const auto& order = sole_order(fixture.state);
    const auto& reservation = fixture.state.reservations[order.reservation_index];
    const auto position_before =
        fixture.state.risk_accounts[0].position_quantity.atoms();
    const auto reserved_before =
        fixture.state.risk_accounts[0].reserved_quantity.atoms();
    const auto remaining_before = reservation.remaining_quantity.atoms();
    require(remaining_before > 0, "the conflict fixture has nothing left to fill");

    // A genuine execution that FITS the still-retained reservation.
    VenueObservation execution;
    execution.kind = VenueObservationKind::fill;
    execution.venue_id = VenueId{9};
    execution.venue_session_id = VenueSessionId{1};
    execution.venue_sequence = VenueSequence{9'502};
    execution.cluster_order_id = order.cluster_order_id;
    execution.effect_id = submit_effect_of(fixture.state);
    execution.venue_order_id = VenueOrderId{88};
    execution.execution_id = ExecutionId{9'502};
    execution.side = Side::buy;
    execution.price = PriceTicks::from_atoms(11);
    execution.quantity = QuantityLots::from_atoms(remaining_before);
    execution.evidence_digest = digest(0x94);
    {
        ApplyScratch fill_scratch;
        const Command command{{CallerId{1}, RequestSequence{9'503},
                               observation_fingerprint(execution)},
                              ObserveFill{execution}};
        const auto booked = apply(fixture.state, command, fill_scratch);
        require(booked.code == ApplyCode::applied,
                "a valid execution that fits the retained reservation was "
                "refused against a conflicted order, code " +
                    std::to_string(static_cast<int>(booked.code)));
    }

    const auto& after = sole_order(fixture.state);
    const auto& reservation_after = fixture.state.reservations[after.reservation_index];

    // POSITION and HOLDS moved, atomically and by exactly the executed quantity.
    require(fixture.state.risk_accounts[0].position_quantity.atoms() ==
                position_before + remaining_before,
            "a valid execution against a conflicted order did not move the "
            "position");
    require(fixture.state.risk_accounts[0].reserved_quantity.atoms() <
                reserved_before,
            "the aggregate hold did not shrink as the reservation was consumed");
    require(reservation_after.remaining_quantity.atoms() == 0 &&
                reservation_after.state == ReservationState::released,
            "the reservation was not consumed by the execution it authorized");

    // STICKY. The lifecycle does not move, even though the reservation released.
    require(after.state == ClusterOrderState::conflict,
            "booking an execution declared the recorded disagreement solved");

    // NOT RECLAIMABLE. order_is_risk_resolved keys off the terminal states, and
    // conflict is deliberately not one, so the group survives for the operator.
    const auto metrics = collect_metrics(fixture.state);
    require(metrics.orders_by_state[static_cast<std::size_t>(
                ClusterOrderState::conflict)] > 0,
            "the conflicted order stopped being reported");
    require(fixture.state.order_count == 1,
            "the conflicted group was reclaimed despite an unsettled dispute");

    // DEDUPLICATION still holds: the same execution cannot be booked twice.
    const auto position_now =
        fixture.state.risk_accounts[0].position_quantity.atoms();
    VenueObservation duplicate;
    duplicate.kind = VenueObservationKind::fill;
    duplicate.venue_id = VenueId{9};
    duplicate.venue_session_id = VenueSessionId{1};
    duplicate.venue_sequence = VenueSequence{9'502};
    duplicate.cluster_order_id = after.cluster_order_id;
    duplicate.effect_id = submit_effect_of(fixture.state);
    duplicate.venue_order_id = VenueOrderId{88};
    duplicate.execution_id = ExecutionId{9'502};
    duplicate.side = Side::buy;
    duplicate.price = PriceTicks::from_atoms(11);
    duplicate.quantity = QuantityLots::from_atoms(remaining_before);
    duplicate.evidence_digest = digest(0x94);
    ApplyScratch scratch;
    const Command repeat{{CallerId{1}, RequestSequence{9'510},
                          observation_fingerprint(duplicate)},
                         ObserveFill{duplicate}};
    (void)apply(fixture.state, repeat, scratch);
    require(fixture.state.risk_accounts[0].position_quantity.atoms() ==
                position_now,
            "a duplicate execution was booked twice against a conflicted order");

    // And the whole thing survives a restart.
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(fixture.state, bytes);
    require(encoded.error == StateSnapshotError::none,
            "the conflicted-but-accounted state did not encode");
    TradingState restored;
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                                  snapshot_scratch)
                    .error == StateSnapshotError::none,
            "the conflicted-but-accounted state did not decode");
    require(restored.orders[0].state == ClusterOrderState::conflict,
            "the restored order lost its recorded disagreement");
}

void test_an_expired_venue_event_is_refused_not_replayed() {
    // The watermark makes receipts reclaimable. It is only SOUND because what it
    // forgets it also refuses: re-presenting an observation below the watermark
    // must never re-apply, because re-applying is a second economic mutation and
    // that is the one outcome expiry may not produce. Losing the retained answer
    // is the accepted cost - the same trade architecture 8.6 made for request
    // receipts.
    auto fixture = pending_cancel();
    ApplyScratch scratch;
    const auto order_id = sole_order(fixture.state).cluster_order_id;
    std::uint64_t sequence = 9'000;

    // Drive cycles until the sweep has advanced a watermark.
    for (std::size_t cycle = 0; cycle < 40; ++cycle) {
        if (fixture.state.venue_cursor_count != 0 &&
            fixture.state.venue_cursors[0].expired_below.value() != 0) {
            break;
        }
        const auto current = sole_order(fixture.state).effect_id;
        VenueObservation rejected;
        rejected.kind = VenueObservationKind::order_rejected;
        rejected.venue_id = VenueId{9};
        rejected.venue_session_id = VenueSessionId{1};
        rejected.venue_sequence = VenueSequence{++sequence};
        rejected.cluster_order_id = order_id;
        rejected.effect_id = current;
        rejected.evidence_digest = digest(0x91);
        const auto reject_request = ++sequence;
        if (submit_answer(fixture.state, rejected, reject_request).code !=
            ApplyCode::applied) {
            break;
        }
        const auto venue_sequence = ++sequence;
        const auto request_sequence = ++sequence;
        if (submit_answer(fixture.state,
                          reconciled(fixture.state, current, 88, 0, venue_sequence),
                          request_sequence)
                .code != ApplyCode::applied) {
            break;
        }
        const auto cancel_sequence = ++sequence;
        const Command again{{CallerId{1}, RequestSequence{cancel_sequence},
                             request_digest(cancel_sequence)},
                            RequestCancel{order_id}};
        if (apply(fixture.state, again, scratch).code != ApplyCode::applied) break;
    }

    require(fixture.state.venue_cursor_count == 1,
            "no venue cursor was established under capacity pressure");
    const auto watermark = fixture.state.venue_cursors[0].expired_below;
    require(watermark.value() != 0,
            "the sweep never advanced a watermark, so expiry is untested");

    // Re-present an observation from below the watermark. It was applied once,
    // long ago, and its receipt has since been reclaimed.
    const auto before = fixture.state;
    VenueObservation replayed;
    replayed.kind = VenueObservationKind::order_rejected;
    replayed.venue_id = VenueId{9};
    replayed.venue_session_id = VenueSessionId{1};
    replayed.venue_sequence = VenueSequence{watermark.value() - 1};
    replayed.cluster_order_id = order_id;
    replayed.effect_id = sole_order(fixture.state).effect_id;
    replayed.evidence_digest = digest(0x92);
    const auto applied = submit_answer(fixture.state, replayed, ++sequence);
    require(applied.code == ApplyCode::venue_event_expired,
            "an observation below the watermark was not refused as expired");
    require(std::memcmp(&fixture.state.risk_accounts, &before.risk_accounts,
                        sizeof(before.risk_accounts)) == 0 &&
                std::memcmp(&fixture.state.reservations, &before.reservations,
                            sizeof(before.reservations)) == 0 &&
                sole_order(fixture.state).state == sole_order(before).state,
            "an expired observation moved economics");

    // Anything at or above the watermark still behaves exactly as before.
    VenueObservation current_event = replayed;
    current_event.venue_sequence = VenueSequence{sequence + 1'000};
    current_event.evidence_digest = digest(0x93);
    require(submit_answer(fixture.state, current_event, ++sequence).code !=
                ApplyCode::venue_event_expired,
            "the watermark expired an observation at or above itself");
}

void test_the_guard_refuses_an_incoherent_control_snapshot() {
    // REVIEW FINDING P1-1. `evaluate_pre_send` compares the effect only against
    // the supplied GatewayControlView, never against the TradingState it was
    // handed. Two matching STALE values agree with each other and prove nothing
    // about either being current, so the last callable decision before transport
    // can authorize a send while holding authoritative state that already
    // contains the revocation.
    //
    // `plan_reconciliation` already performs this coherency check, so the
    // delivery and reconciliation halves apply different authority rules today.
    const auto fixture = one_authorized_submit();
    const auto stale = control_of(fixture.state);
    ApplyScratch scratch;

    {   // Kill epoch advanced in state; the cached view still matches the effect.
        auto state = fixture.state;
        const Command advance{{CallerId{1}, RequestSequence{40}, digest(0x61)},
                              AdvanceKillEpoch{KillEpoch{
                                  state.kill_epoch.value() + 1}}};
        require(apply(state, advance, scratch).code == ApplyCode::applied,
                "the kill-epoch fixture could not advance");
        require(!evaluate_pre_send(state, fixture.effect_id, stale, capable_venue(),
                                   SendQuotaView{4})
                     .permitted(),
                "a send was authorized against a kill epoch the supplied state "
                "had already advanced past");
    }
    {   // Executor generation advanced in state.
        auto state = fixture.state;
        const Command advance{{CallerId{1}, RequestSequence{41}, digest(0x62)},
                              SetExecutorGeneration{ExecutorGeneration{
                                  state.executor_generation.value() + 1}}};
        require(apply(state, advance, scratch).code == ApplyCode::applied,
                "the generation fixture could not advance");
        require(!evaluate_pre_send(state, fixture.effect_id, stale, capable_venue(),
                                   SendQuotaView{4})
                     .permitted(),
                "a send was authorized against an executor generation the "
                "supplied state had already superseded");
    }
    {   // Strategy mode revoked in state while the cached view still says normal.
        auto state = fixture.state;
        const Command halt{{CallerId{1}, RequestSequence{42}, digest(0x63)},
                           SetStrategyMode{StrategyMode::halted, state.kill_epoch,
                                           state.executor_generation}};
        require(apply(state, halt, scratch).code == ApplyCode::applied,
                "the halt fixture could not apply");
        require(!evaluate_pre_send(state, fixture.effect_id, stale, capable_venue(),
                                   SendQuotaView{4})
                     .permitted(),
                "new risk was authorized against a strategy mode the supplied "
                "state had already revoked");
    }
}

void test_repeated_cancel_cycles_cannot_starve_fill_capacity() {
    // REVIEW FINDING P1-5. Authorization reserves worst-case future venue-event
    // capacity for every live order, so an admitted order is guaranteed enough
    // receipts to account all its remaining fills. `apply_request_cancel` does
    // not re-check that reserve, and the accepted serial-cancel lifecycle can be
    // repeated: reject the cancel, reconcile "still working", cancel again.
    //
    // Each cycle pins two more applied venue-event receipts to an order that is
    // never resolved and therefore never reclaimed. If that can reach the 64
    // ceiling, a later genuine fill is refused at the capacity check - the venue
    // has executed and the cluster cannot account for it, while the order still
    // reads as live rather than conflicted. That is the exact outcome the reserve
    // exists to make impossible.
    auto fixture = pending_cancel();
    ApplyScratch scratch;
    const auto order_id = sole_order(fixture.state).cluster_order_id;
    std::uint64_t sequence = 8'000;
    std::size_t cycles = 0;

    for (; cycles < 40; ++cycles) {
        auto current = sole_order(fixture.state).effect_id;
        VenueObservation rejected;
        rejected.kind = VenueObservationKind::order_rejected;
        rejected.venue_id = VenueId{9};
        rejected.venue_session_id = VenueSessionId{1};
        rejected.venue_sequence = VenueSequence{++sequence};
        rejected.cluster_order_id = order_id;
        rejected.effect_id = current;
        rejected.evidence_digest = digest(0x81);
        if (submit_answer(fixture.state, rejected, ++sequence).code !=
            ApplyCode::applied) {
            break;
        }
        const auto venue_sequence = ++sequence;
        const auto request_sequence = ++sequence;
        if (submit_answer(fixture.state,
                          reconciled(fixture.state, current, 88, 0, venue_sequence),
                          request_sequence)
                .code != ApplyCode::applied) {
            break;
        }
        const auto cancel_sequence = ++sequence;
        const Command again{{CallerId{1}, RequestSequence{cancel_sequence},
                             request_digest(cancel_sequence)},
                            RequestCancel{order_id}};
        if (apply(fixture.state, again, scratch).code != ApplyCode::applied) break;
    }

    const auto events =
        kMaxVenueEventReceipts -
        collect_metrics(fixture.state).venue_event_headroom;
    std::printf("  P1-5 probe: %zu cycles, %zu venue events consumed, order %s\n",
                cycles, events,
                name_of_state(sole_order(fixture.state).state));

    // The order was authorized for two lots and has filled none, so the reserve
    // must still admit both remaining one-atom fills plus its control tail.
    VenueObservation fill;
    fill.kind = VenueObservationKind::fill;
    fill.venue_id = VenueId{9};
    fill.venue_session_id = VenueSessionId{1};
    fill.venue_sequence = VenueSequence{++sequence};
    fill.cluster_order_id = order_id;
    // The SUBMIT effect, located from current state. `fixture.effect_id` is the
    // original cancel, and reuse_rejected_cancel has overwritten that record many
    // generations ago - naming it would be refused as not-applicable and the test
    // would pass while proving nothing.
    EffectId submit_effect{};
    for (std::size_t index = 0; index < fixture.state.outbox_effect_count; ++index) {
        const auto& effect = fixture.state.outbox_effects[index];
        if (effect.cluster_order_id == order_id &&
            effect.operation == EffectOperation::submit) {
            submit_effect = effect.effect_id;
        }
    }
    require(submit_effect.value() != 0,
            "the cycle fixture lost its submit effect");
    fill.effect_id = submit_effect;
    fill.venue_order_id = VenueOrderId{88};
    fill.execution_id = ExecutionId{9'999};
    fill.side = Side::buy;
    fill.price = PriceTicks::from_atoms(11);
    fill.quantity = QuantityLots::from_atoms(1);
    fill.evidence_digest = digest(0x82);
    ApplyScratch fill_scratch;
    const Command command{{CallerId{1}, RequestSequence{++sequence},
                           observation_fingerprint(fill)},
                          ObserveFill{fill}};
    const auto applied = apply(fixture.state, command, fill_scratch);
    // PART ONE, fixed: whatever happens to the quantity, the divergence must
    // never be silent. A venue execution the cluster cannot book must leave the
    // order conflicted and its group unreclaimable, not reading as live.
    if (applied.code == ApplyCode::fill_capacity_exceeded ||
        applied.code == ApplyCode::venue_event_capacity_exceeded) {
        require(sole_order(fixture.state).state == ClusterOrderState::conflict,
                "a fill refused for capacity left the order looking healthy, so "
                "a real execution vanished without a trace");
    }

    // PART TWO, still open pending the retention contract: the reserve that
    // authorization established for this order's own fills must survive any legal
    // repetition of the serial cancel lifecycle.
    require(applied.code != ApplyCode::venue_event_capacity_exceeded &&
                applied.code != ApplyCode::fill_capacity_exceeded,
            "repeated legal cancel cycles consumed the venue-event capacity "
            "reserved for this order's own fills, so a real execution can no "
            "longer be accounted");
}

void test_a_rejected_cancel_can_still_be_finally_reconciled() {
    // REVIEW FINDING P1-4. Architecture 10.3.6 motivates the rejected-cancel
    // state with a duplicate rejection overtaking the original SUCCESSFUL
    // cancellation - and then the implementation refuses the final evidence for
    // exactly that case.
    //
    // After `order_rejected` on a cancel the pair is (order=reconciling,
    // effect=rejected). `cancel_reconcilable` accepts only
    // (reconciling,reconciling) and (submission_unknown,unknown), so the "the
    // venue did cancel it" truth has no path out and the reservation is stranded
    // permanently. Only the opposite answer - "still working", via
    // order_reconciled - was ever tested.
    auto fixture = pending_cancel();
    VenueObservation rejected;
    rejected.kind = VenueObservationKind::order_rejected;
    rejected.venue_id = VenueId{9};
    rejected.venue_session_id = VenueSessionId{1};
    rejected.venue_sequence = VenueSequence{7'600};
    rejected.cluster_order_id = sole_order(fixture.state).cluster_order_id;
    rejected.effect_id = fixture.effect_id;
    rejected.evidence_digest = digest(0x7D);
    require(submit_answer(fixture.state, rejected, 30).code == ApplyCode::applied,
            "the cancel rejection was refused");
    require(sole_order(fixture.state).state == ClusterOrderState::reconciling,
            "a rejected cancel did not leave the lifecycle unknown");

    // The venue is then queried and answers authoritatively that the order IS
    // cancelled. That is final evidence and must resolve the slot.
    const auto applied = submit_answer(
        fixture.state, cancel_final(fixture.state, fixture.effect_id, 0, 7'601), 31);
    require(applied.code == ApplyCode::applied,
            "final cancel evidence was refused after a cancel rejection, so the "
            "reservation is stranded with no path out");
    require(sole_order(fixture.state).state == ClusterOrderState::cancelled,
            "final cancel evidence did not resolve the order");
    require(risk_released(fixture.state),
            "a cancelled order kept its reservation");

    // And it must survive a restart, since a state the cluster cannot restore
    // from is a state it cannot really be in.
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(fixture.state, bytes);
    require(encoded.error == StateSnapshotError::none,
            "the resolved rejected-cancel state did not encode");
    TradingState restored;
    StateSnapshotScratch scratch;
    require(decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                                  scratch)
                    .error == StateSnapshotError::none,
            "the resolved rejected-cancel state did not decode");
}

// The other two quantity relations from the same (reconciling, rejected) pair.
// The review asked for all three because the adjudication must not depend on
// which state the cancel happened to be rejected from.
void test_rejected_cancel_final_evidence_covers_every_quantity_relation() {
    const auto reject_the_cancel = [](Fixture& fixture, std::uint64_t sequence) {
        VenueObservation rejected;
        rejected.kind = VenueObservationKind::order_rejected;
        rejected.venue_id = VenueId{9};
        rejected.venue_session_id = VenueSessionId{1};
        rejected.venue_sequence = VenueSequence{sequence};
        rejected.cluster_order_id = sole_order(fixture.state).cluster_order_id;
        rejected.effect_id = fixture.effect_id;
        rejected.evidence_digest = digest(0x7D);
        require(submit_answer(fixture.state, rejected, sequence + 1).code ==
                    ApplyCode::applied,
                "the cancel rejection was refused");
    };

    {   // BELOW what the cluster booked: unclosable, so a conflict.
        auto fixture = silent_cancel_with_booked_fill(1);
        // Drive it to (reconciling, rejected) rather than (submission_unknown,
        // unknown), so the pair under test is the newly admitted one.
        reject_the_cancel(fixture, 8'500);
        require(sole_order(fixture.state).state == ClusterOrderState::reconciling,
                "the below-booked fixture is not reconciling");
        require(submit_answer(fixture.state,
                              cancel_final(fixture.state, fixture.effect_id, 0,
                                           8'502),
                              8'503)
                        .code == ApplyCode::applied,
                "contradicting final evidence was not recorded");
        require(sole_order(fixture.state).state == ClusterOrderState::conflict,
                "a venue denying a booked fill in its final word was not a "
                "conflict");
        require(!risk_released(fixture.state),
                "a conflicted order released its reservation");
    }
    {   // ABOVE what the cluster booked: missing evidence, so refuse and wait.
        auto fixture = pending_cancel();
        reject_the_cancel(fixture, 8'600);
        const auto before = fixture.state;
        require(submit_answer(fixture.state,
                              cancel_final(fixture.state, fixture.effect_id, 1,
                                           8'602),
                              8'603)
                        .code == ApplyCode::venue_observation_not_applicable,
                "final evidence ahead of the cluster was not refused");
        require(std::memcmp(&fixture.state.reservations, &before.reservations,
                            sizeof(before.reservations)) == 0,
                "a refused final answer moved economics");
    }
}

void test_new_risk_is_still_refused_outside_normal() {
    // The other half of the same rule, which must not be lost while fixing it.
    const auto fixture = one_authorized_submit();
    for (const auto mode : {StrategyMode::no_new_risk, StrategyMode::cancel_only,
                            StrategyMode::reduce_only, StrategyMode::halted}) {
        auto state = fixture.state;
        state.strategy_mode = mode;
        const auto decision = evaluate_pre_send(state, fixture.effect_id,
                                                control_of(state), capable_venue(),
                                                SendQuotaView{4});
        require(decision.refusal == PreSendRefusal::control_forbids_new_risk,
                "a submit was permitted in strategy mode " +
                    std::to_string(static_cast<int>(mode)));
    }
}

// --- the fault-matrix harness ----------------------------------------------

// A bounded arena is a HORIZON BUDGET. It holds N records; if records are created
// at rate r per state version and each lives h state versions, the arena needs
// N >= r * h. Phase 4 chose every N by argument because r and h were unmeasurable
// without a gateway. These are those numbers, taken under adversarial load.
struct ArenaPeak {
    const char* name;
    std::size_t capacity;
    std::size_t peak;

    [[nodiscard]] std::size_t margin() const noexcept {
        return capacity > peak ? capacity - peak : 0;
    }
};

struct Horizons {
    // Peak simultaneous occupancy, which is r * h realised.
    std::size_t peak_decisions{};
    std::size_t peak_orders{};
    std::size_t peak_reservations{};
    std::size_t peak_effects{};
    std::size_t peak_receipts{};
    std::size_t peak_venue_events{};
    std::size_t peak_caller_cursors{};
    std::size_t peak_source_events{};

    // Lifetimes and horizons, in STATE VERSIONS. Wall-clock has no meaning to a
    // clockless reducer, and a state version is exact on every replica.
    std::uint64_t max_order_age{};        // authorization to last observation
    std::uint64_t max_unresolved_age{};   // the retention horizon that matters
    std::uint64_t max_unsettled_age{};
    std::uint32_t max_send_attempts_used{};
    std::uint32_t max_query_attempts_used{};
    std::uint64_t final_state_version{};
    // The safety property, which must hold at ANY capacity: a record holding
    // unresolved exposure is never reclaimed. Saturation is a liveness bound and
    // is acceptable; premature reclamation is a correctness failure and is not.
    std::size_t vanished_while_unresolved{};
    std::size_t peak_conflicts{};
    std::uint64_t reclaimed_groups{};
    std::map<ApplyCode, std::size_t> capacity_refusals;

    void observe(const TradingMetrics& metrics) noexcept {
        peak_decisions = std::max<std::size_t>(peak_decisions, metrics.decision_count);
        peak_orders = std::max<std::size_t>(peak_orders, metrics.order_count);
        peak_reservations =
            std::max<std::size_t>(peak_reservations, metrics.reservation_count);
        peak_effects =
            std::max<std::size_t>(peak_effects, metrics.outbox_effect_count);
        peak_receipts = std::max<std::size_t>(peak_receipts, metrics.receipt_count);
        peak_venue_events = std::max<std::size_t>(
            peak_venue_events, kMaxVenueEventReceipts - metrics.venue_event_headroom);
        peak_caller_cursors =
            std::max<std::size_t>(peak_caller_cursors, metrics.caller_cursor_count);
        peak_source_events =
            std::max<std::size_t>(peak_source_events, metrics.source_event_count);
        max_unresolved_age =
            std::max(max_unresolved_age, metrics.oldest_unresolved_order_age);
        max_unsettled_age =
            std::max(max_unsettled_age, metrics.oldest_unsettled_effect_age);
        final_state_version = metrics.state_version;
        peak_conflicts = std::max<std::size_t>(
            peak_conflicts,
            metrics.orders_by_state[static_cast<std::size_t>(
                ClusterOrderState::conflict)]);
        reclaimed_groups = std::max(reclaimed_groups, metrics.reclaimed_group_count);
    }

    void merge(const Horizons& other) noexcept {
        peak_decisions = std::max(peak_decisions, other.peak_decisions);
        peak_orders = std::max(peak_orders, other.peak_orders);
        peak_reservations = std::max(peak_reservations, other.peak_reservations);
        peak_effects = std::max(peak_effects, other.peak_effects);
        peak_receipts = std::max(peak_receipts, other.peak_receipts);
        peak_venue_events = std::max(peak_venue_events, other.peak_venue_events);
        peak_caller_cursors =
            std::max(peak_caller_cursors, other.peak_caller_cursors);
        peak_source_events = std::max(peak_source_events, other.peak_source_events);
        max_order_age = std::max(max_order_age, other.max_order_age);
        max_unresolved_age = std::max(max_unresolved_age, other.max_unresolved_age);
        max_unsettled_age = std::max(max_unsettled_age, other.max_unsettled_age);
        max_send_attempts_used =
            std::max(max_send_attempts_used, other.max_send_attempts_used);
        max_query_attempts_used =
            std::max(max_query_attempts_used, other.max_query_attempts_used);
        final_state_version = std::max(final_state_version, other.final_state_version);
        vanished_while_unresolved += other.vanished_while_unresolved;
        peak_conflicts = std::max(peak_conflicts, other.peak_conflicts);
        reclaimed_groups = std::max(reclaimed_groups, other.reclaimed_groups);
        for (const auto& [code, count] : other.capacity_refusals) {
            capacity_refusals[code] += count;
        }
    }

    [[nodiscard]] std::array<ArenaPeak, 8> arenas() const noexcept {
        return {{{"decisions", kMaxDecisionRecords, peak_decisions},
                 {"orders", kMaxClusterOrders, peak_orders},
                 {"reservations", kMaxRiskReservations, peak_reservations},
                 {"outbox effects", kMaxOutboxEffects, peak_effects},
                 {"request receipts", kMaxRequestReceipts, peak_receipts},
                 {"venue events", kMaxVenueEventReceipts, peak_venue_events},
                 {"caller cursors", kMaxCallerCursors, peak_caller_cursors},
                 {"source events", kMaxSourceEventReceipts, peak_source_events}}};
    }
};

struct RunOutcome {
    // Gate 1.
    std::size_t venue_orders_created{};
    std::set<std::uint64_t> accepted_order_ids;
    std::map<std::uint64_t, std::uint64_t> identity_owner;  // order -> effect

    // Gate 2, per cluster order.
    std::map<std::uint64_t, std::int64_t> venue_reported;
    std::map<std::uint64_t, std::int64_t> cluster_booked;
    std::set<std::uint64_t> seen_executions;
    std::map<ApplyCode, std::size_t> fill_refusals;
    std::map<ApplyCode, std::size_t> event_refusals;

    // Gate 3.
    TradingMetrics metrics{};
    std::size_t observed_unresolved{};

    std::size_t sends{};
    std::size_t ambiguous_declared{};
    std::size_t authorizations{};
    std::size_t cancels{};

    // What the loop leaves behind, and what reconciliation did about it.
    std::map<ClusterOrderState, std::size_t> unresolved_states;
    std::map<std::uint64_t, ClusterOrderState> unresolved_orders_by_id;
    std::size_t reconciliation_targets{};
    std::size_t queries{};
    std::size_t reconciled{};
    std::map<QueryDisposition, std::size_t> dispositions;
    // Why an order could not be resolved, recorded as it happens. An order left
    // unresolved must have one of these against its name - "stuck for no reason"
    // is the outcome the gate exists to exclude.
    std::set<std::uint64_t> answered_ahead;   // venue reported fills we never got
    std::set<std::uint64_t> never_answered;   // no trustworthy answer was obtained

    // Architecture 9.1.7. Section 3 requires identity retention to exceed the
    // retry, recovery and reconciliation horizons. Phase 4 could not show that,
    // because the horizons did not exist until a gateway produced them. These are
    // the measurements that discharge it.
    Horizons horizons;
};

// Did an admission get refused because a bounded arena had no room? This is the
// real liveness signal, and peak occupancy is not.
//
// Several arenas - the decision arena above all - sweep ON ADMISSION WHEN FULL,
// so reaching the ceiling is the designed trigger for reclamation rather than a
// symptom of anything. What matters is whether the sweep frees enough: if it
// does, no admission is refused and the capacity genuinely exceeds the horizon.
// Measuring occupancy instead would have condemned a sweep-on-full arena for
// working exactly as designed.
[[nodiscard]] bool is_capacity_refusal(const ApplyCode code) noexcept {
    switch (code) {
        case ApplyCode::source_capacity_exceeded:
        case ApplyCode::source_event_capacity_exceeded:
        case ApplyCode::decision_capacity_exceeded:
        case ApplyCode::policy_capacity_exceeded:
        case ApplyCode::receipt_capacity_exceeded:
        case ApplyCode::reservation_capacity_exceeded:
        case ApplyCode::order_capacity_exceeded:
        case ApplyCode::outbox_capacity_exceeded:
        case ApplyCode::fill_capacity_exceeded:
        case ApplyCode::venue_event_capacity_exceeded:
        case ApplyCode::risk_account_capacity_exceeded:
            return true;
        default:
            return false;
    }
}

// Refusals that lose no economic information: the cluster already holds the
// fact, or it deliberately swept the resolved group that owned it.
bool refusal_is_lossless(const ApplyCode code) noexcept {
    switch (code) {
        case ApplyCode::already_applied:
        case ApplyCode::venue_event_identity_conflict:
        case ApplyCode::fill_identity_conflict:
        case ApplyCode::venue_observation_not_applicable:
        case ApplyCode::order_not_found:
            return true;
        default:
            return false;
    }
}

RunOutcome run_seed(const std::uint64_t seed, const FaultPolicy& faults,
                    const std::uint64_t cycles) {
    auto state = ready_state();
    ApplyScratch scratch;
    SimulatedVenue venue(seed, faults, VenueId{9});
    GatewayDeliveryState delivery;
    GatewayReconciliationState reconciliation;
    const GatewayTimeouts timeouts{8};

    RunOutcome outcome;
    const auto submit = [&](const Command& command) {
        const auto result = apply(state, command, scratch);
        if (is_capacity_refusal(result.code)) {
            ++outcome.horizons.capacity_refusals[result.code];
        }
        return result;
    };
    std::uint64_t counter = 0;
    std::uint64_t sequence = 0;
    std::uint64_t decision_id = 100;
    sim::Tick now = 0;

    const auto next_identity = [&]() {
        ++counter;
        return CommandIdentity{CallerId{1}, RequestSequence{counter},
                               request_digest(counter)};
    };

    // One tick of the loop. `propose` is false during the drain, so the run can
    // finish delivering what it already authorized without starting new risk -
    // which is what makes "did anything stay unresolved" a real question rather
    // than an artefact of stopping mid-flight.
    std::uint64_t cycle = 0;
    std::map<std::uint64_t, bool> last_seen;
    const auto tick = [&](const bool propose) {
        // 1. The edge proposes; the cluster authorizes. Refusals are expected and
        //    correct: slot exclusivity and arena headroom both fail closed, and
        //    the harness must tolerate that rather than force throughput.
        if (propose) {
            ++sequence;
            const auto evidence =
                digest(static_cast<std::uint8_t>(1 + sequence % 200));
            const Command record{
                next_identity(),
                RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                  SourceSequence{sequence}, evidence}};
            if (submit(record).code == ApplyCode::applied) {
                const DecisionId id{decision_id++};
                const Command evaluate{next_identity(),
                                       EvaluateDecisionFrame{
                                           id, EvaluationMode::authoritative,
                                           frame(sequence, evidence, state)}};
                if (submit(evaluate).code == ApplyCode::applied) {
                    const Command authorize{next_identity(), AuthorizeDecision{id}};
                    if (submit(authorize).code == ApplyCode::applied) {
                        ++outcome.authorizations;
                    }
                }
            }
        }

        // 1b. Periodically cancel whatever is working. Without this the cancel
        //     lifecycle - redelivery, rejection, final reconciliation - is only
        //     ever unit-tested, and the interactions between a cancel in flight
        //     and a fill in flight are exactly what a fault matrix is for.
        if (propose && cycle % 3 == 2) {
            for (std::size_t index = 0; index < state.order_count; ++index) {
                const auto& order = state.orders[index];
                if (order.state != ClusterOrderState::live &&
                    order.state != ClusterOrderState::partially_filled) {
                    continue;
                }
                const Command cancel{next_identity(),
                                     RequestCancel{order.cluster_order_id}};
                if (submit(cancel).code == ApplyCode::applied) {
                    ++outcome.cancels;
                }
                break;
            }
        }

        // 2. Plan. Every judgement in the loop happens here, in a pure function.
        const auto control = control_of(state);
        const auto plan = plan_delivery(state, delivery, control, capable_venue(),
                                        SendQuotaView{4}, timeouts, now);

        // 3. Ambiguity is reported before anything is sent. A deadline passing
        //    means UNKNOWN - never rejected, and never a licence to resend.
        for (std::size_t index = 0; index < plan.ambiguous_count; ++index) {
            const auto effect_id = plan.now_ambiguous[index];
            ClusterOrderId order{};
            for (std::size_t slot = 0; slot < state.outbox_effect_count; ++slot) {
                if (state.outbox_effects[slot].effect_id == effect_id) {
                    order = state.outbox_effects[slot].cluster_order_id;
                }
            }
            if (order.value() == 0) continue;
            VenueObservation unknown;
            unknown.kind = VenueObservationKind::effect_unknown;
            unknown.venue_id = VenueId{9};
            unknown.venue_session_id = venue.session();
            // A private sequence range, so a harness-authored unknown can never
            // collide with a simulator-authored observation identity.
            unknown.venue_sequence = VenueSequence{900'000 + now * 64 + index};
            unknown.cluster_order_id = order;
            unknown.effect_id = effect_id;
            unknown.evidence_digest = digest(0x7E);
            ++counter;
            const Command report{{CallerId{1}, RequestSequence{counter},
                                  observation_fingerprint(unknown)},
                                 ObserveOrderEvent{unknown}};
            (void)submit(report);
            (void)record_response(delivery, effect_id);
            ++outcome.ambiguous_declared;
        }

        // 4. Send. The shell executes the plan and records what the transport
        //    saw; it decides nothing.
        for (std::size_t index = 0; index < plan.send_count; ++index) {
            const auto effect_id = plan.to_send[index];
            const auto decision = evaluate_pre_send(state, effect_id, control,
                                                    capable_venue(), SendQuotaView{4});
            require(decision.permitted(),
                    "the planner scheduled an effect the guard refuses");
            const auto order = decision.effect->cluster_order_id.value();
            // Scoped to SUBMITS. A cancel carries the cluster order identity of
            // the order it cancels, so sharing it is the point rather than a
            // violation - what must never happen is one identity being used to
            // CREATE a second venue order.
            if (decision.effect->operation == EffectOperation::submit) {
                const auto owner = outcome.identity_owner.find(order);
                require(owner == outcome.identity_owner.end() ||
                            owner->second == effect_id.value(),
                        "one cluster order identity was submitted under two "
                        "effects");
                outcome.identity_owner[order] = effect_id.value();
            }

            ++outcome.sends;
            const auto sent = venue.send(*decision.effect, now);
            require(record_send(delivery, effect_id, now),
                    "the delivery attempt arena overflowed");
            if (sent == SendOutcome::accepted &&
                decision.effect->operation == EffectOperation::submit) {
                ++outcome.venue_orders_created;
                outcome.accepted_order_ids.insert(order);
            }
            if (sent == SendOutcome::rate_limited ||
                sent == SendOutcome::pre_send_failure) {
                // Nothing reached the venue, so the effect is cleanly retryable
                // without reconciliation. This is the ONLY case where that is
                // true, which is why the outcomes are kept distinct - and why
                // this is the only place the shell may call `record_unsent`.
                (void)record_unsent(delivery, effect_id);
            }
        }

        // 5. Drain. Time advances before and after so a same-tick response is
        //    impossible: a venue answers later, always.
        now += 3;
        for (const auto& observation : venue.poll(now)) {
            const bool is_fill = observation.kind == VenueObservationKind::fill;
            const auto order = observation.cluster_order_id.value();
            if (is_fill &&
                outcome.seen_executions.insert(observation.execution_id.value())
                    .second) {
                outcome.venue_reported[order] += observation.quantity.atoms();
            }
            ++counter;
            const Command command{{CallerId{1}, RequestSequence{counter},
                                   observation_fingerprint(observation)},
                                  is_fill ? CommandBody{ObserveFill{observation}}
                                          : CommandBody{ObserveOrderEvent{observation}}};
            // The venue ANSWERED. Whether the cluster accepts the answer is a
            // separate question with a separate outcome, and conflating the two
            // would leave the attempt outstanding until it timed out - so the
            // gateway would declare an outcome unknown that the venue had
            // already told it, and retry something already answered.
            (void)record_response(delivery, observation.effect_id);
            const auto applied = submit(command);
            if (applied.code == ApplyCode::applied) {
                if (is_fill) {
                    outcome.cluster_booked[order] +=
                        applied.filled_quantity().atoms();
                }
            } else if (is_fill) {
                ++outcome.fill_refusals[applied.code];
            } else {
                ++outcome.event_refusals[applied.code];
            }
        }
        // 6. Reconcile. This runs after the drain so it sees the freshest state,
        //    and it is the only step allowed to resolve exposure the loop itself
        //    could not.
        const auto recon = plan_reconciliation(state, reconciliation, delivery,
                                               control_of(state), capable_venue(),
                                               timeouts, now);
        for (std::size_t index = 0; index < recon.query_count; ++index) {
            const auto& target = recon.to_query[index];
            require(record_query(reconciliation, target.cluster_order_id, now),
                    "the reconciliation arena overflowed");
            const auto raw = venue.query(target.cluster_order_id, now);
            require(record_answer(reconciliation, target.cluster_order_id),
                    "record_answer did not find the outstanding query");
            ++outcome.queries;

            // Map the transport's answer into what the gateway may act on. The
            // simulator's own result type deliberately never crosses this line.
            VenueQueryView view;
            view.venue_session_id = venue.session();
            switch (raw.outcome) {
                case QueryOutcome::known:
                    view.status = VenueQueryStatus::known;
                    view.venue_order_id = raw.venue_order_id;
                    view.filled_quantity = raw.filled_quantity;
                    view.remaining_quantity = raw.remaining_quantity;
                    view.cancelled = raw.cancelled;
                    // Authoritative means the VENUE says this answer covers the
                    // window it was asked about. It deliberately does NOT consult
                    // `raw.inconsistent`: that flag is the simulator telling the
                    // test what it did, and wiring it in here would hand the
                    // gateway ground truth no real gateway can have. A corrupted
                    // answer must reach the reducer and be caught by the
                    // reducer's own refusal to guess - which is the entire reason
                    // that logic exists.
                    view.authoritative = true;
                    break;
                case QueryOutcome::not_found:
                    view.status = VenueQueryStatus::not_found;
                    view.authoritative = true;
                    break;
                case QueryOutcome::unavailable:
                    view.status = VenueQueryStatus::unavailable;
                    break;
            }

            const auto* attempt =
                find_reconciliation(reconciliation, target.cluster_order_id);
            const auto disposition = classify_query(view, attempt, timeouts);
            ++outcome.dispositions[disposition];
            if (disposition != QueryDisposition::submit) {
                outcome.never_answered.insert(target.cluster_order_id.value());
                continue;
            }
            outcome.never_answered.erase(target.cluster_order_id.value());

            // Which kind the answer becomes is the gateway's only remaining
            // choice, and it is a reporting choice rather than an economic one.
            // `cancel_reconciled` asserts the cancel TOOK EFFECT and is final.
            // `order_reconciled` asserts the venue still holds the order, which
            // for a stuck cancel means the cancel did not take. Anything else -
            // including an order the venue denies - is the latter with a zero
            // identity, saying nothing is resting there.
            const bool cancel_took_effect =
                target.need == ReconciliationNeed::cancel_unconfirmed &&
                view.status == VenueQueryStatus::known && view.cancelled;
            VenueObservation answer;
            answer.kind = cancel_took_effect
                              ? VenueObservationKind::cancel_reconciled
                              : VenueObservationKind::order_reconciled;
            answer.venue_id = VenueId{9};
            answer.venue_session_id = view.venue_session_id;
            answer.venue_sequence = VenueSequence{800'000 + now * 64 + index};
            answer.cluster_order_id = target.cluster_order_id;
            answer.effect_id = target.effect_id;
            answer.venue_order_id = view.venue_order_id;
            answer.quantity = view.filled_quantity;
            answer.evidence_digest = digest(0x6C);
            // `cancel_reconciled` requires a venue order identity; it is only
            // ever built from a `known` answer, which always carries one.
            if (cancel_took_effect && answer.venue_order_id.value() == 0) continue;
            ++counter;
            const Command command{{CallerId{1}, RequestSequence{counter},
                                   observation_fingerprint(answer)},
                                  ObserveOrderEvent{answer}};
            const auto adjudicated = submit(command);
            if (adjudicated.code == ApplyCode::applied) {
                ++outcome.reconciled;
            } else if (adjudicated.code ==
                       ApplyCode::venue_observation_not_applicable) {
                // The only refusal a trustworthy answer can draw here is the
                // venue reporting fills the cluster never received: real evidence
                // the cluster is missing, not a conflict, because a fill still in
                // flight would close the gap.
                outcome.answered_ahead.insert(target.cluster_order_id.value());
            }
        }

        // The gateway reclaims its own bookkeeping on the same cadence the
        // reducer sweeps, or its arenas outlive the records they describe.
        (void)reclaim_delivery_attempts(delivery, state);
        (void)reclaim_reconciliation_attempts(reconciliation, state);

        // Sample every tick. A peak that occurs between ticks is invisible to a
        // single final reading, and the peak is the whole measurement.
        outcome.horizons.observe(collect_metrics(state));

        // Nothing unresolved may vanish. An order that leaves the arena must have
        // been resolved when last seen - reclamation frees capacity, and freeing
        // it against exposure that may still exist is the one failure no capacity
        // choice can excuse.
        std::map<std::uint64_t, bool> present;
        for (std::size_t index = 0; index < state.order_count; ++index) {
            const auto& order = state.orders[index];
            const bool unresolved =
                order.reservation_index >= state.reservation_count ||
                state.reservations[order.reservation_index].state !=
                    ReservationState::released;
            present[order.cluster_order_id.value()] = unresolved;
        }
        for (const auto& [id, was_unresolved] : last_seen) {
            if (present.count(id) != 0) continue;
            if (was_unresolved) ++outcome.horizons.vanished_while_unresolved;
        }
        last_seen = present;
        for (std::size_t index = 0; index < state.order_count; ++index) {
            const auto& order = state.orders[index];
            if (state.state_version >= order.authorization_state_version) {
                outcome.horizons.max_order_age =
                    std::max(outcome.horizons.max_order_age,
                             state.state_version - order.authorization_state_version);
            }
        }
        for (std::size_t index = 0; index < delivery.attempt_count; ++index) {
            outcome.horizons.max_send_attempts_used =
                std::max(outcome.horizons.max_send_attempts_used,
                         delivery.attempts[index].attempts);
        }
        for (std::size_t index = 0; index < reconciliation.attempt_count; ++index) {
            outcome.horizons.max_query_attempts_used =
                std::max(outcome.horizons.max_query_attempts_used,
                         reconciliation.attempts[index].attempts);
        }

        now += 2;
    };

    for (cycle = 0; cycle < cycles; ++cycle) tick(true);
    // Drain. Bounded by construction: each pass advances virtual time by five
    // ticks, so every deadline and every scheduled response comes due. What is
    // still unresolved after this is genuinely unresolved.
    for (std::uint64_t pass = 0; pass < 12; ++pass) tick(false);

    for (std::size_t index = 0; index < state.order_count; ++index) {
        const auto& order = state.orders[index];
        const bool unresolved =
            order.reservation_index >= state.reservation_count ||
            state.reservations[order.reservation_index].state !=
                ReservationState::released;
        if (unresolved) {
            ++outcome.observed_unresolved;
            ++outcome.unresolved_states[order.state];
            outcome.unresolved_orders_by_id[order.cluster_order_id.value()] =
                order.state;
        }
    }
    outcome.metrics = collect_metrics(state);
    const GatewayReconciliationState fresh;
    outcome.reconciliation_targets =
        plan_reconciliation(state, fresh, delivery, control_of(state),
                            capable_venue(), timeouts, now)
            .query_count;
    return outcome;
}

void check_gates(const RunOutcome& outcome, const std::string& where) {
    // GATE 1: no duplicate venue order. The simulator creates a resting order
    // only for a cluster order identity it has never seen, and answers a repeat
    // with `venue_duplicate` instead. So the count of orders it created must
    // equal the number of DISTINCT identities we had accepted - if a retry had
    // ever mutated its identity, this would be strictly larger.
    require(outcome.accepted_order_ids.size() == outcome.venue_orders_created,
            "the venue created more orders than there were distinct intents" + where);

    // GATE 2: no lost accepted fill. Every distinct execution the venue reported
    // must be booked exactly once, or refused for a reason that loses nothing.
    for (const auto& [order, reported] : outcome.venue_reported) {
        const auto booked = outcome.cluster_booked.count(order) != 0
                                ? outcome.cluster_booked.at(order)
                                : 0;
        require(booked <= reported,
                "the cluster booked more than the venue ever reported" + where);
    }
    for (const auto& [order, booked] : outcome.cluster_booked) {
        require(outcome.venue_reported.count(order) != 0,
                "the cluster booked a fill for an order the venue never filled" +
                    where);
        (void)booked;
    }
    for (const auto& [code, count] : outcome.fill_refusals) {
        // `fill_quantity_conflict` is the one refusal that genuinely does not
        // book a fill the venue reported: the hold is released, or the quantity
        // exceeds what was ever reserved, and booking it would exceed authorized
        // risk. That is allowed - but only because it is RECORDED. The gate is
        // not "every fill is booked", which no risk-bounded system can promise;
        // it is "no fill disappears without a trace", and the trace is a
        // conflicted order that cannot be reclaimed.
        if (code == ApplyCode::fill_quantity_conflict) continue;
        require(refusal_is_lossless(code),
                "a fill was refused for a reason that loses it: code " +
                    std::to_string(static_cast<int>(code)) + " x" +
                    std::to_string(count) + where);
    }
    if (outcome.fill_refusals.count(ApplyCode::fill_quantity_conflict) != 0) {
        require(outcome.metrics
                        .orders_by_state[static_cast<std::size_t>(
                            ClusterOrderState::conflict)] > 0,
                "a fill was refused as unbookable without leaving a conflicted "
                "order behind, so it vanished silently" + where);
    }
    // Non-fill venue evidence is what resolves a slot, so a refusal there that is
    // not idempotent suppression means evidence was dropped and an order can
    // never resolve. A capacity refusal would land here too, which is the point:
    // it would be fail-closed but still a sizing problem worth failing on.
    for (const auto& [code, count] : outcome.event_refusals) {
        require(refusal_is_lossless(code),
                "venue evidence was refused for a reason that loses it: code " +
                    std::to_string(static_cast<int>(code)) + " x" +
                    std::to_string(count) + where);
    }

    // GATE 3: no unresolved state presented as healthy. Unresolved orders are
    // permitted - loss and disconnect create them by design - but the projection
    // must SHOW them. A metric that quietly agreed everything was fine while a
    // slot held exposure is the failure this gate names.
    require(outcome.metrics.unresolved_orders == outcome.observed_unresolved,
            "the projection disagreed with state about unresolved orders" + where);
    // The fixture trades exactly one (account, instrument, venue, slot), and slot
    // exclusivity says at most one order there may hold unresolved risk. This is
    // what stops a flaky venue from accumulating stuck exposure: the second
    // authorization is refused rather than queued behind the first.
    require(outcome.metrics.unresolved_orders <= 1,
            "slot exclusivity let a second order hold unresolved risk" + where);
    if (outcome.metrics.unresolved_orders > 0) {
        require(outcome.metrics.oldest_unresolved_order_age > 0,
                "an unresolved order reported zero age, so it looks fresh forever" +
                    where);
    }
    if (outcome.metrics.unsettled_effects > 0) {
        require(outcome.metrics.oldest_unsettled_effect_age > 0,
                "an unsettled effect reported zero age" + where);
    }
    // Visibility is necessary but not sufficient: something has to be able to ACT
    // on what is visible. Every order still holding exposure must be in a state
    // that is either QUERYABLE by reconciliation, or one reconciliation has
    // already ruled on. Anything else is exposure with no path forward at all,
    // which is the discovery worth failing a build over.
    for (const auto& [order_state, count] : outcome.unresolved_states) {
        (void)count;
        const bool queryable = order_state == ClusterOrderState::submission_unknown ||
                               order_state == ClusterOrderState::reconciling ||
                               order_state == ClusterOrderState::dispatch_authorized;
        // Adjudicated: reconciliation has spoken and re-asking would add nothing.
        // `conflict` is the G-074a operator path; `live` and `partially_filled`
        // mean the venue confirmed it holds a working order, so the exposure is
        // real and correct rather than stranded.
        const bool adjudicated = order_state == ClusterOrderState::conflict ||
                                 order_state == ClusterOrderState::live ||
                                 order_state == ClusterOrderState::partially_filled ||
                                 order_state == ClusterOrderState::cancel_pending;
        require(queryable || adjudicated,
                "an order holds exposure in a state nothing can act on: " +
                    std::to_string(static_cast<int>(order_state)) + where);
    }
    // The claim, in its honest form. After reconciliation quiesces, an order may
    // still be in submission_unknown - but only for a NAMED reason: either no
    // trustworthy answer was ever obtained, or the answer reported fills the
    // cluster has never received, which is missing evidence rather than a
    // conflict and stays recoverable. What is excluded is an order stuck for no
    // reason at all, which is what the gate is really for.
    for (const auto& order : outcome.unresolved_orders_by_id) {
        // Both stuck shapes, symmetrically: a submission whose outcome is unknown
        // and a cancel awaiting its final word. Each must have a cause on record.
        if (order.second != ClusterOrderState::submission_unknown &&
            order.second != ClusterOrderState::reconciling) {
            continue;
        }
        require(outcome.answered_ahead.count(order.first) != 0 ||
                    outcome.never_answered.count(order.first) != 0,
                "an order was stuck awaiting reconciliation with no cause on "
                "record" + where);
    }
}

void test_a_clean_venue_delivers_and_resolves() {
    const auto outcome = run_seed(1, FaultPolicy::clean(), 10);
    if (std::getenv("VELOCITA_MATRIX_TRACE") != nullptr) {
        std::cout << "clean: auth " << outcome.authorizations << " cancels "
                  << outcome.cancels << " sends " << outcome.sends << " ambiguous "
                  << outcome.ambiguous_declared << " unresolved "
                  << outcome.observed_unresolved << " states ";
        for (const auto& [s, c] : outcome.unresolved_states) {
            std::cout << static_cast<int>(s) << "x" << c << " ";
        }
        std::cout << '\n';
    }
    check_gates(outcome, " (clean)");
    require(outcome.venue_orders_created > 0, "the clean run never placed an order");
    require(outcome.ambiguous_declared == 0,
            "a clean venue produced an ambiguous outcome");
    require(!outcome.venue_reported.empty(), "the clean run never filled");
    for (const auto& [order, reported] : outcome.venue_reported) {
        require(outcome.cluster_booked.count(order) != 0 &&
                    outcome.cluster_booked.at(order) == reported,
                "a clean run lost a fill");
    }
    require(outcome.metrics.unresolved_orders == 0,
            "a clean run left an order unresolved");
}

void test_the_fault_matrix_holds_the_gate() {
    FaultPolicy faults;
    faults.delay_permille = 120;
    faults.loss_permille = 80;
    faults.duplication_permille = 120;
    faults.reorder_permille = 120;
    faults.partial_fill_permille = 180;
    faults.disconnect_permille = 60;
    faults.rate_limit_permille = 90;
    faults.inconsistent_query_permille = 220;

    std::size_t total_sends = 0;
    std::size_t total_ambiguous = 0;
    std::size_t total_orders = 0;
    std::size_t total_duplicate_fills = 0;
    std::size_t total_unresolved = 0;
    std::size_t total_queries = 0;
    std::size_t total_reconciled = 0;
    std::size_t total_unavailable = 0;
    std::size_t total_cancels = 0;
    for (std::uint64_t seed = 1; seed <= 32; ++seed) {
        const auto outcome = run_seed(seed, faults, 12);
        // Printed BEFORE the gates so a failing seed's numbers are visible, and
        // only behind an environment switch so a passing run stays quiet. It
        // reports values already computed; it never re-runs anything under test.
        if (std::getenv("VELOCITA_MATRIX_TRACE") != nullptr) {
            std::cout << "seed " << seed << " auth " << outcome.authorizations
                      << " sends " << outcome.sends << " orders "
                      << outcome.venue_orders_created << " ambiguous "
                      << outcome.ambiguous_declared << " unresolved "
                      << outcome.metrics.unresolved_orders << " unsettled "
                      << outcome.metrics.unsettled_effects << " reported "
                      << outcome.venue_reported.size() << " fill-refusals "
                      << outcome.fill_refusals.size() << " event-refusals ";
            for (const auto& [code, count] : outcome.event_refusals) {
                std::cout << static_cast<int>(code) << "x" << count << " ";
            }
            std::cout << "queries " << outcome.queries << " reconciled "
                      << outcome.reconciled << " recon-targets "
                      << outcome.reconciliation_targets << " states ";
            for (const auto& [order_state, count] : outcome.unresolved_states) {
                std::cout << static_cast<int>(order_state) << "x" << count << " ";
            }
            std::cout << '\n';
        }
        check_gates(outcome, " (seed " + std::to_string(seed) + ")");
        for (const auto& [code, count] : outcome.fill_refusals) {
            (void)code;
            total_duplicate_fills += count;
        }
        total_unresolved += outcome.metrics.unresolved_orders;
        total_queries += outcome.queries;
        total_cancels += outcome.cancels;
        total_reconciled += outcome.reconciled;
        if (outcome.dispositions.count(QueryDisposition::unavailable) != 0) {
            total_unavailable += outcome.dispositions.at(QueryDisposition::unavailable);
        }
        total_sends += outcome.sends;
        total_ambiguous += outcome.ambiguous_declared;
        total_orders += outcome.venue_orders_created;
    }
    // A matrix that never exercised its hard paths would pass vacuously.
    require(total_sends > 0, "the matrix never sent anything");
    require(total_orders > 0, "the matrix never placed an order");
    require(total_ambiguous > 0,
            "the matrix never produced an ambiguous outcome, so timeout handling "
            "was never exercised");
    require(total_duplicate_fills > 0,
            "the matrix never delivered a duplicate fill, so the single most "
            "dangerous duplicate was never suppressed under test");
    require(total_unresolved > 0,
            "the matrix never stranded an order, so the unresolved-visibility "
            "gate was never exercised");
    require(total_queries > 0, "the matrix never reconciled anything");
    require(total_reconciled > 0, "no reconciliation answer was ever accepted");
    // Silence is the branch that matters most - it is the one where reading an
    // absent answer as an absent order would place a second real order - so the
    // matrix must actually reach it rather than only ever getting answers.
    require(total_cancels > 0,
            "the matrix never cancelled anything, so the cancel lifecycle was "
            "never exercised end to end");
    require(total_unavailable > 0,
            "the matrix never lost a query, so the classifier's silence branch "
            "was never exercised");
    // Deliberately NOT asserted here: that the matrix produced a reconciliation
    // conflict. A conflict needs an order that went unknown while already holding
    // booked fills, and this fault model cannot produce that - one fault applies
    // per operation, so a partial fill and a lost response never combine, and the
    // loop's drain granularity means fills either all beat the deadline or all
    // miss it. Requiring it here would be a false requirement. The branch is
    // covered directly and precisely by test_a_venue_that_lost_a_fill_is_a_conflict
    // and test_a_denied_order_that_already_filled_is_a_conflict. Closing the gap
    // properly means letting faults compose, which is a simulator change worth
    // making on its own rather than smuggling in here.
}

void test_the_matrix_is_reproducible() {
    // The seed is required evidence in the Phase 5 gate, which is worth nothing
    // unless replaying it reproduces the run exactly.
    FaultPolicy faults;
    faults.loss_permille = 150;
    faults.duplication_permille = 150;
    faults.partial_fill_permille = 250;
    faults.disconnect_permille = 80;
    const auto first = run_seed(7, faults, 12);
    const auto second = run_seed(7, faults, 12);
    require(first.metrics == second.metrics,
            "the same seed produced a different final projection");
    require(first.sends == second.sends &&
                first.venue_orders_created == second.venue_orders_created &&
                first.ambiguous_declared == second.ambiguous_declared &&
                first.venue_reported == second.venue_reported &&
                first.cluster_booked == second.cluster_booked,
            "the same seed produced a different run");
    const auto other = run_seed(8, faults, 12);
    require(!(other.metrics == first.metrics) || other.sends != first.sends ||
                other.venue_reported != first.venue_reported,
            "two different seeds produced identical runs, so the seed is inert");
}

// --- architecture 9.1.7: the retention horizon measurement -----------------

void report(const char* label, const Horizons& worst) {
    if (std::getenv("VELOCITA_HORIZON_REPORT") == nullptr) return;
    std::printf("\n  %s\n  arena              cap   peak  margin\n", label);
    for (const auto& arena : worst.arenas()) {
        std::printf("  %-16s %5zu  %5zu  %6zu\n", arena.name, arena.capacity,
                    arena.peak, arena.margin());
    }
    std::printf("  max order age / unresolved / unsettled (versions): %llu / %llu / %llu\n",
                static_cast<unsigned long long>(worst.max_order_age),
                static_cast<unsigned long long>(worst.max_unresolved_age),
                static_cast<unsigned long long>(worst.max_unsettled_age));
    std::printf("  send attempts %u/%u   query attempts %u/%u   peak conflicts %zu\n",
                worst.max_send_attempts_used, GatewayTimeouts{}.max_send_attempts,
                worst.max_query_attempts_used, GatewayTimeouts{}.max_query_attempts,
                worst.peak_conflicts);
    for (const auto& [code, count] : worst.capacity_refusals) {
        std::printf("  CAPACITY REFUSAL: code %d x%zu\n", static_cast<int>(code),
                    count);
    }
    std::printf("  orders that vanished while unresolved: %zu\n",
                worst.vanished_while_unresolved);
    std::printf("  order groups reclaimed: %llu\n",
                static_cast<unsigned long long>(worst.reclaimed_groups));
    std::printf("  final state version: %llu\n\n",
                static_cast<unsigned long long>(worst.final_state_version));
}

FaultPolicy nominal_faults() {
    // Everything a healthy venue does badly: latency, loss, duplication,
    // reordering, partial fills, disconnects, pacing. No self-contradiction.
    FaultPolicy faults;
    faults.delay_permille = 120;
    faults.loss_permille = 80;
    faults.duplication_permille = 120;
    faults.reorder_permille = 120;
    faults.partial_fill_permille = 180;
    faults.disconnect_permille = 60;
    faults.rate_limit_permille = 90;
    return faults;
}

void test_retention_capacity_exceeds_the_horizon_under_nominal_faults() {
    // Discharges the obligation Phase 4 was accepted with. Section 3 requires
    // identity retention to exceed the maximum retry, recovery and reconciliation
    // horizon. Phase 4 could not show that: 8.6 and 8.7 made retention finite and
    // capacity-driven, and 8.4 recorded that the horizons were unknown until a
    // gateway produced them. They exist now.
    //
    // The check is a MARGIN, not a bound. Every arena already fails closed at its
    // ceiling - a refusal, never a silent drop - so "capacity is never exceeded"
    // is true by construction and proves nothing. What must be shown is that the
    // ceiling is never REACHED, because reaching it means the cluster is refusing
    // work it was sized to accept.
    Horizons worst;
    for (std::uint64_t seed = 1; seed <= 48; ++seed) {
        const auto outcome = run_seed(seed, nominal_faults(), 40);
        check_gates(outcome, " (horizon seed " + std::to_string(seed) + ")");
        worst.merge(outcome.horizons);
    }
    report("nominal faults, 48 seeds x 40 cycles", worst);

    // The liveness property, stated where it actually lives. Not "no arena is
    // ever full" - several sweep on admission when full, so fullness is their
    // reclamation trigger - but "no admission was ever refused for want of room".
    for (const auto& [code, count] : worst.capacity_refusals) {
        require(false, "an admission was refused for capacity under nominal "
                       "faults, so a bounded arena no longer exceeds its horizon: "
                       "code " + std::to_string(static_cast<int>(code)) + " x" +
                    std::to_string(count));
    }
    require(worst.vanished_while_unresolved == 0,
            "an order holding unresolved exposure was reclaimed");
    for (const auto& arena : worst.arenas()) {
        require(arena.peak <= arena.capacity,
                std::string("an arena exceeded its capacity, so a bound was not "
                            "enforced: ") + arena.name);
    }

    // The run has to be long enough for the numbers to mean anything, and the
    // honest evidence of that is RECLAMATION rather than an arbitrary version
    // count: records must have been created, resolved, and swept, which is the
    // full cycle the capacity question is about. A run that only ever filled
    // arenas would satisfy a "no capacity refusal" check while measuring nothing.
    require(worst.reclaimed_groups > 0,
            "no order group was ever reclaimed, so the run never completed a "
            "create-resolve-sweep cycle and measured nothing");
    require(worst.peak_orders > 1 && worst.peak_effects > 1,
            "the horizon run never held more than one order at a time");
    require(worst.max_order_age > 0, "no order lived long enough to be measured");
    require(worst.max_send_attempts_used > 1 && worst.max_query_attempts_used > 1,
            "the run never retried or re-queried, so the horizons it measured "
            "were not the ones the bounds describe");
}

void test_conflicts_saturate_the_cluster_and_it_fails_closed() {
    // The other half of the measurement, and the more operationally important
    // one. A conflict is terminal-but-unresolved by design, so it is never
    // reclaimed - which means EVERY unresolved conflict permanently consumes one
    // slot in the decision, order, reservation and effect arenas at once.
    //
    // With sixteen slots, enough simultaneous conflicts saturate the cluster and
    // it stops accepting new decisions. That is correct - refusing is the only
    // alternative to freeing capacity against exposure that may still exist - but
    // it is a hard operational bound, and it makes the G-074a operator escalation
    // path load-bearing rather than a convenience.
    //
    // The capacity cannot simply be raised: `TradingState` is 81,168 bytes against
    // an accepted 81,920 ceiling, and one more decision record costs 704 of the
    // 752 bytes left. This is a bound to characterise, not a number to move.
    auto faults = nominal_faults();
    faults.fill_after_cancel_permille = 90;
    faults.inconsistent_query_permille = 220;

    Horizons worst;
    for (std::uint64_t seed = 1; seed <= 48; ++seed) {
        const auto outcome = run_seed(seed, faults, 40);
        check_gates(outcome, " (saturation seed " + std::to_string(seed) + ")");
        worst.merge(outcome.horizons);
    }
    report("conflict-inducing faults, 48 seeds x 40 cycles", worst);

    // SAFETY, which must hold at any capacity and under any load.
    require(worst.vanished_while_unresolved == 0,
            "an order holding unresolved exposure was reclaimed under saturation");
    for (const auto& arena : worst.arenas()) {
        require(arena.peak <= arena.capacity,
                std::string("an arena exceeded its capacity, which means a bound "
                            "was not enforced: ") + arena.name);
    }

    // LIVENESS, recorded rather than required. Saturation is the expected outcome
    // here; asserting no capacity refusal would be asserting that conflicts are
    // rare, which is not something the system controls.
    require(worst.peak_conflicts > 0,
            "the saturation run produced no conflicts, so it measured nothing");
}

void test_the_bounded_retry_and_query_budgets_are_actually_reached() {
    // A bound that is never hit is untested, and the two attempt bounds are the
    // only places where the gateway gives up on its own. If neither is reached
    // under the full fault matrix, the numbers in `GatewayTimeouts` are guesses
    // no evidence has touched.
    FaultPolicy faults;
    faults.loss_permille = 200;
    faults.disconnect_permille = 150;
    faults.rate_limit_permille = 200;
    faults.inconsistent_query_permille = 200;

    Horizons worst;
    for (std::uint64_t seed = 1; seed <= 32; ++seed) {
        worst.merge(run_seed(seed, faults, 30).horizons);
    }
    require(worst.max_send_attempts_used > 1,
            "no effect was ever redelivered, so the retry budget is unexercised");
    require(worst.max_query_attempts_used > 1,
            "no order was ever re-queried, so the query budget is unexercised");
}

}  // namespace

int main() {
    try {
        test_a_fresh_pending_effect_is_scheduled_once();
        test_an_outstanding_effect_is_not_resent();
        test_a_passed_deadline_is_ambiguous_and_not_also_a_retry();
        test_an_answered_effect_is_never_blindly_resent();
        test_a_provably_unsent_effect_retries_under_its_own_identity();
        test_unsent_retries_are_bounded();
        test_quota_bounds_the_plan();
        test_a_halted_cluster_plans_no_sends();
        test_the_planner_only_schedules_what_the_guard_permits();
        test_reconciliation_targets_only_unresolvable_exposure();
        test_one_outstanding_query_at_a_time();
        test_the_query_interval_is_respected();
        test_reconciliation_queries_are_bounded();
        test_reconciliation_is_fenced_by_generation_but_not_by_halt();
        test_only_a_trustworthy_answer_may_be_submitted();
        test_a_delivery_exhausted_order_is_reconciled_not_abandoned();
        test_the_reconciliation_planner_reads_no_clock_and_mutates_nothing();
        test_a_cancel_is_deliverable_in_every_strategy_mode();
        test_a_conflicted_order_keeps_accounting_valid_executions();
        test_an_expired_venue_event_is_refused_not_replayed();
        test_the_guard_refuses_an_incoherent_control_snapshot();
        test_repeated_cancel_cycles_cannot_starve_fill_capacity();
        test_a_rejected_cancel_can_still_be_finally_reconciled();
        test_rejected_cancel_final_evidence_covers_every_quantity_relation();
        test_new_risk_is_still_refused_outside_normal();
        test_states_reached_only_through_phase_five_survive_a_snapshot();
        test_a_silent_cancel_redelivers_before_it_reconciles();
        test_a_rejected_cancel_does_not_prove_the_order_is_live();
        test_a_silent_cancel_can_be_finally_reconciled();
        test_a_cancel_final_below_booked_is_a_conflict();
        test_a_cancel_final_above_booked_waits_rather_than_conflicting();
        test_a_denied_order_releases_its_reservation_and_frees_the_slot();
        test_a_filled_submit_can_never_become_unknown();
        test_a_denied_order_that_already_filled_is_a_conflict();
        test_agreement_resolves_the_effect_without_moving_position();
        test_a_fill_the_cluster_never_saw_is_missing_evidence_not_a_conflict();
        test_a_venue_that_lost_a_fill_is_a_conflict();
        test_replaying_the_same_answer_changes_nothing();
        test_the_verdict_is_a_function_of_the_total_not_of_history();
        test_a_reconciliation_answer_is_refused_where_it_cannot_apply();
        test_a_clean_venue_delivers_and_resolves();
        test_the_fault_matrix_holds_the_gate();
        test_the_matrix_is_reproducible();
        test_retention_capacity_exceeds_the_horizon_under_nominal_faults();
        test_conflicts_saturate_the_cluster_and_it_fails_closed();
        test_the_bounded_retry_and_query_budgets_are_actually_reached();
        std::cout << "all V2 gateway delivery tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 gateway delivery test failed: " << error.what() << '\n';
        return 1;
    }
}
