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
#include "velocita/v2/venue_simulator.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace {

using namespace velocita::v2;
using velocita::v2::sim::FaultPolicy;
using velocita::v2::sim::SendOutcome;
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

// --- the fault-matrix harness ----------------------------------------------

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

    // What the loop leaves behind, and whether reconciliation would engage on it.
    std::map<ClusterOrderState, std::size_t> unresolved_states;
    std::size_t reconciliation_targets{};
};

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
    const GatewayTimeouts timeouts{8};

    RunOutcome outcome;
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
            if (apply(state, record, scratch).code == ApplyCode::applied) {
                const DecisionId id{decision_id++};
                const Command evaluate{next_identity(),
                                       EvaluateDecisionFrame{
                                           id, EvaluationMode::authoritative,
                                           frame(sequence, evidence, state)}};
                if (apply(state, evaluate, scratch).code == ApplyCode::applied) {
                    const Command authorize{next_identity(), AuthorizeDecision{id}};
                    if (apply(state, authorize, scratch).code == ApplyCode::applied) {
                        ++outcome.authorizations;
                    }
                }
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
            (void)apply(state, report, scratch);
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
            const auto owner = outcome.identity_owner.find(order);
            require(owner == outcome.identity_owner.end() ||
                        owner->second == effect_id.value(),
                    "one cluster order identity was sent under two effects");
            outcome.identity_owner[order] = effect_id.value();

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
            const auto applied = apply(state, command, scratch);
            if (applied.code == ApplyCode::applied) {
                (void)record_response(delivery, observation.effect_id);
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
        now += 2;
    };

    for (std::uint64_t cycle = 0; cycle < cycles; ++cycle) tick(true);
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
        require(refusal_is_lossless(code),
                "a fill was refused for a reason that loses it: code " +
                    std::to_string(static_cast<int>(code)) + " x" +
                    std::to_string(count) + where);
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
    // on what is visible. Every stranded slot the loop produces must be one the
    // reconciliation planner would engage. A failure here means the matrix found
    // a way to strand an order in a shape `need_of` does not recognise, which is
    // exactly the discovery worth failing a build over.
    if (outcome.observed_unresolved > 0) {
        require(outcome.reconciliation_targets > 0,
                "an order was stranded in a state reconciliation would never "
                "query" + where);
    }
}

void test_a_clean_venue_delivers_and_resolves() {
    const auto outcome = run_seed(1, FaultPolicy::clean(), 10);
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

    std::size_t total_sends = 0;
    std::size_t total_ambiguous = 0;
    std::size_t total_orders = 0;
    std::size_t total_duplicate_fills = 0;
    std::size_t total_unresolved = 0;
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
            std::cout << "recon-targets " << outcome.reconciliation_targets
                      << " states ";
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
        test_a_clean_venue_delivers_and_resolves();
        test_the_fault_matrix_holds_the_gate();
        test_the_matrix_is_reproducible();
        std::cout << "all V2 gateway delivery tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 gateway delivery test failed: " << error.what() << '\n';
        return 1;
    }
}
