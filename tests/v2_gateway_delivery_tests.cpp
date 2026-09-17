
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
#include <utility>
#include <vector>

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

const GatewayEvidenceState no_evidence;


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

// A live authority lease, held constant, because these suites are about
// delivery and reconciliation rather than about authority. The lease's own
// behaviour is covered in `v2-gateway-tests`; here it must simply never be the
// reason something did or did not send.
//
// The lease is fail-closed, so leaving these fields default would have silenced
// every send in this file - which is how the enforcement was confirmed to reach
// every path rather than only the new tests.
constexpr Tick kLiveLeaseBound = 1'000'000;

GatewayControlView control_of(const TradingState& state) {
    return {state.executor_generation, state.kill_epoch, state.strategy_mode,
            0, kLiveLeaseBound, 0};
}

GatewayDeliveryState running_gateway() {
    GatewayDeliveryState delivery;
    const TradingState before_anything;
    require(begin_gateway_recovery(delivery, before_anything, 0) == 0,
            "an empty cluster produced suspect effects");
    require(delivery.started && !delivery.recovering,
            "a gateway that scanned an empty cluster is still recovering");
    return delivery;
}

void test_a_fresh_pending_effect_is_scheduled_once() {
    const auto fixture = one_authorized_submit();
    const auto delivery = running_gateway();
    const auto plan = plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4},
                                    GatewayTimeouts{10}, 0);
    require(plan.send_count == 1 && plan.to_send[0] == fixture.effect_id,
            "the planner did not schedule a fresh pending effect");
    require(plan.ambiguous_count == 0,
            "a freshly authorized effect was reported ambiguous");
}

void test_an_outstanding_effect_is_not_resent() {
    const auto fixture = one_authorized_submit();
    auto delivery = running_gateway();
    require(record_send(delivery, fixture.effect_id, 0), "record_send failed");
    const auto plan = plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4},
                                    GatewayTimeouts{10}, 1);
    require(plan.send_count == 0,
            "the planner rescheduled an effect that was still outstanding");
    const auto* attempt = find_attempt(delivery, fixture.effect_id);
    require(attempt != nullptr && attempt->awaiting_response && attempt->attempts == 1,
            "the attempt record does not reflect the outstanding send");
}

void test_a_passed_deadline_is_ambiguous_and_not_also_a_retry() {
    const auto fixture = one_authorized_submit();
    auto delivery = running_gateway();
    require(record_send(delivery, fixture.effect_id, 0), "record_send failed");
    const auto plan = plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4},
                                    GatewayTimeouts{8}, 20);
    require(plan.ambiguous_count == 1 && plan.now_ambiguous[0] == fixture.effect_id,
            "a passed deadline did not become ambiguous");
    require(plan.send_count == 0,
            "a timed-out effect was blindly retried in the same tick");
}

void test_an_answered_effect_is_never_blindly_resent() {
    const auto fixture = one_authorized_submit();
    auto delivery = running_gateway();
    require(record_send(delivery, fixture.effect_id, 0), "record_send failed");
    require(record_response(delivery, fixture.effect_id),
            "record_response did not find the outstanding attempt");
    const auto* attempt = find_attempt(delivery, fixture.effect_id);
    require(attempt != nullptr && !attempt->awaiting_response &&
                !attempt->resend_permitted,
            "a venue response left the effect marked resendable");
    const auto plan = plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4},
                                    GatewayTimeouts{10}, 5);
    require(plan.send_count == 0,
            "an effect the venue had already answered was blindly resent");
}

void test_a_provably_unsent_effect_retries_under_its_own_identity() {
    const auto fixture = one_authorized_submit();
    auto delivery = running_gateway();
    require(record_send(delivery, fixture.effect_id, 0), "first record_send failed");
    require(record_unsent(delivery, fixture.effect_id),
            "record_unsent did not find the attempt");
    const auto plan = plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
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
    const auto fixture = one_authorized_submit();
    auto delivery = running_gateway();
    const GatewayTimeouts timeouts{10, 3};
    for (std::uint32_t round = 0; round < timeouts.max_send_attempts; ++round) {
        const auto plan = plan_delivery(fixture.state, delivery, no_evidence,
                                        control_of(fixture.state), capable_venue(),
                                        SendQuotaView{4}, timeouts, round);
        require(plan.send_count == 1,
                "a retry below the attempt bound was not scheduled");
        require(record_send(delivery, fixture.effect_id, round), "record_send failed");
        require(record_unsent(delivery, fixture.effect_id), "record_unsent failed");
    }
    const auto plan = plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4}, timeouts, 9);
    require(plan.send_count == 0, "the retry bound did not stop scheduling");
    require(plan.ambiguous_count == 0,
            "an effect that never left the process was reported ambiguous");
}

void test_quota_bounds_the_plan() {
    const auto fixture = one_authorized_submit();
    const auto delivery = running_gateway();
    require(plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
                          capable_venue(), SendQuotaView{0}, GatewayTimeouts{10}, 0)
                    .send_count == 0,
            "a zero quota still produced sends");
}

void test_a_halted_cluster_plans_no_sends() {
    auto fixture = one_authorized_submit();
    fixture.state.strategy_mode = StrategyMode::halted;
    const auto delivery = running_gateway();
    const auto plan = plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4},
                                    GatewayTimeouts{10}, 0);
    require(plan.send_count == 0, "a halted cluster still planned new risk");
}

void test_the_planner_only_schedules_what_the_guard_permits() {
    const auto fixture = one_authorized_submit();
    const auto delivery = running_gateway();
    const auto plan = plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
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

const GatewayDeliveryState no_attempts;

constexpr VenueQueryView complete_answer(const VenueQueryStatus status) {
    VenueQueryView answer;
    answer.status = status;
    answer.venue_session_id = VenueSessionId{1};
    answer.window_complete = true;
    answer.ordered_after_relevant_sends = true;
    return answer;
}

void test_reconciliation_targets_only_unresolvable_exposure() {
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

void test_a_resting_order_is_confirmed_rather_than_assumed() {
    // ADR 0008. Reconciliation only ever asked about orders already known to be
    // wrong, so a resting order was never questioned - and a cancellation whose
    // event never arrived (a dropped socket, a reconnect gap) left the cluster
    // holding a reservation against an order that no longer existed, with
    // nothing to raise it.
    auto fixture = one_authorized_submit();
    for (std::size_t i = 0; i < fixture.state.order_count; ++i) {
        fixture.state.orders[i].state = ClusterOrderState::live;
    }
    const GatewayTimeouts timeouts;
    const GatewayReconciliationState fresh;

    const auto first = plan_reconciliation(fixture.state, fresh, no_attempts,
                                           control_of(fixture.state),
                                           capable_venue(), timeouts, 0);
    require(first.query_count == 1,
            "a resting order was never asked about, so a cancellation whose "
            "event was lost would stay invisible");
    require(first.to_query[0].need == ReconciliationNeed::unconfirmed_resting,
            "the resting order was queried under the wrong need");

    // THE CADENCE IS SLOWER THAN EVERY OTHER NEED, because confirming a healthy
    // order is not urgent and the traffic is proportional to the whole book.
    GatewayReconciliationState asked;
    require(record_query(asked, only_order(fixture.state), 0, 0),
            "the fixture could not record a query");
    require(record_answer(asked, only_order(fixture.state)),
            "the fixture could not record its answer");
    require(plan_reconciliation(fixture.state, asked, no_attempts,
                                control_of(fixture.state), capable_venue(),
                                timeouts, timeouts.query_interval)
                    .query_count == 0,
            "a resting order was re-queried at the urgent interval");
    require(plan_reconciliation(fixture.state, asked, no_attempts,
                                control_of(fixture.state), capable_venue(),
                                timeouts, timeouts.resting_confirm_interval)
                    .query_count == 1,
            "a resting order was never re-confirmed");

    // AND IT IS UNCAPPED. The other needs give up after `max_query_attempts`,
    // which is right for something already known to be wrong: stop hammering a
    // stuck venue. Confirming a resting order is a HEARTBEAT, and giving up
    // returns the system to exactly the silent state this exists to end. The
    // bound is the rate, not the count.
    GatewayReconciliationState exhausted;
    for (std::uint32_t attempt = 0; attempt <= timeouts.max_query_attempts + 2;
         ++attempt) {
        require(record_query(exhausted, only_order(fixture.state),
                             attempt * timeouts.resting_confirm_interval, 0),
                "the fixture could not record a repeated query");
        require(record_answer(exhausted, only_order(fixture.state)),
                "the fixture could not record a repeated answer");
    }
    const auto after_many = plan_reconciliation(
        fixture.state, exhausted, no_attempts, control_of(fixture.state),
        capable_venue(), timeouts,
        (timeouts.max_query_attempts + 8) * timeouts.resting_confirm_interval);
    require(after_many.query_count == 1,
            "resting confirmation gave up after the attempt cap, which returns "
            "the cluster to assuming rather than confirming");
}

void test_one_outstanding_query_at_a_time() {
    const auto stranded = stranded_submit();
    GatewayReconciliationState reconciliation;
    require(record_query(reconciliation, only_order(stranded.state), 0, 0),
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
    require(record_query(reconciliation, only_order(stranded.state), 0, 0),
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
        require(record_query(reconciliation, order, now, 0), "record_query failed");
        require(record_answer(reconciliation, order), "record_answer failed");
        now += timeouts.query_interval;
    }
    require(plan_reconciliation(stranded.state, reconciliation, no_attempts,
                                control_of(stranded.state), capable_venue(), timeouts,
                                now)
                    .query_count == 0,
            "the query bound did not stop reconciliation");
    require(collect_metrics(stranded.state).unresolved_orders == 1,
            "an exhausted reconciliation stopped reporting its unresolved order");
}

void test_reconciliation_is_fenced_by_generation_but_not_by_halt() {
    const auto stranded = stranded_submit();
    const GatewayReconciliationState fresh;

    GatewayControlView stale = control_of(stranded.state);
    stale.executor_generation =
        ExecutorGeneration{stranded.state.executor_generation.value() - 1};
    require(plan_reconciliation(stranded.state, fresh, no_attempts, stale,
                                capable_venue(), GatewayTimeouts{}, 0)
                    .query_count == 0,
            "a superseded gateway planned to write venue evidence");

    auto halted = stranded.state;
    halted.strategy_mode = StrategyMode::halted;
    require(plan_reconciliation(halted, fresh, no_attempts, control_of(halted),
                                capable_venue(), GatewayTimeouts{}, 0)
                    .query_count == 1,
            "a halted cluster refused to resolve exposure it already holds");
}

void test_only_a_trustworthy_answer_may_be_submitted() {
    const GatewayTimeouts timeouts;

    require(classify_query(complete_answer(VenueQueryStatus::unavailable),
                           nullptr, 0, timeouts) == QueryDisposition::unavailable,
            "an unavailable query was treated as an answer");

    auto partial = complete_answer(VenueQueryStatus::known);
    partial.window_complete = false;
    require(classify_query(partial, nullptr, 0, timeouts) == QueryDisposition::incomplete,
            "an answer that did not cover its window was treated as complete");

    auto stale_to_send = complete_answer(VenueQueryStatus::known);
    stale_to_send.ordered_after_relevant_sends = false;
    require(classify_query(stale_to_send, nullptr, 0, timeouts) ==
                QueryDisposition::incomplete,
            "a query not ordered after the relevant sends was submitted");

    require(classify_query(complete_answer(VenueQueryStatus::known), nullptr, 0, timeouts) == QueryDisposition::submit,
            "a complete answer was not submitted");

    require(classify_query(complete_answer(VenueQueryStatus::not_found), nullptr, 0, timeouts) == QueryDisposition::submit,
            "a definitive not-found was not submitted");

    ReconciliationAttempt spent;
    spent.attempts = timeouts.max_query_attempts;
    require(classify_query(complete_answer(VenueQueryStatus::unavailable), &spent, 0, timeouts) == QueryDisposition::exhausted,
            "a spent reconciliation kept reporting as merely unavailable");
}

void test_an_answer_that_predates_new_evidence_is_not_comparable() {
    const GatewayTimeouts timeouts;

    ReconciliationAttempt issued;
    issued.attempts = 1;
    issued.evidence_at_query = 3;

    for (const auto status :
         {VenueQueryStatus::known, VenueQueryStatus::not_found}) {
        require(classify_query(complete_answer(status), &issued, 4, timeouts) ==
                    QueryDisposition::incomplete,
                "an answer that predates committed evidence was submitted anyway");
    }

    require(classify_query(complete_answer(VenueQueryStatus::known), &issued, 3,
                           timeouts) == QueryDisposition::submit,
            "an answer whose barrier never moved was refused");

    auto spent = issued;
    spent.attempts = timeouts.max_query_attempts;
    require(classify_query(complete_answer(VenueQueryStatus::known), &spent, 4,
                           timeouts) == QueryDisposition::exhausted,
            "a moved barrier kept asking past the attempt budget");

    require(classify_query(complete_answer(VenueQueryStatus::known), nullptr, 9,
                           timeouts) == QueryDisposition::submit,
            "an answer with no outstanding attempt was barred anyway");
}

void test_a_delivery_exhausted_order_is_reconciled_not_abandoned() {
    const auto fixture = one_authorized_submit();
    const GatewayTimeouts timeouts;
    const GatewayReconciliationState fresh;

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

void test_the_evidence_watermark_is_monotonic_per_order() {
    auto fixture = one_authorized_submit();
    const auto order = sole_order(fixture.state).cluster_order_id;

    const auto before = accepted_evidence_watermark(fixture.state, order);

    VenueObservation ack;
    ack.kind = VenueObservationKind::order_acknowledged;
    ack.venue_id = VenueId{9};
    ack.venue_session_id = VenueSessionId{1};
    ack.venue_sequence = VenueSequence{4'100};
    ack.cluster_order_id = order;
    ack.effect_id = fixture.effect_id;
    ack.venue_order_id = VenueOrderId{88};
    ack.evidence_digest = digest(0x71);
    require(submit_answer(fixture.state, ack, 4'101).code == ApplyCode::applied,
            "the watermark fixture could not acknowledge its order");
    const auto after_ack = accepted_evidence_watermark(fixture.state, order);
    require(after_ack > before, "accepted evidence did not move the watermark");

    require(submit_answer(fixture.state, ack, 4'102).code != ApplyCode::applied,
            "a replayed acknowledgement was applied twice");
    require(accepted_evidence_watermark(fixture.state, order) == after_ack,
            "a refused observation moved the watermark");

    book_partial_fill(fixture.state, fixture.effect_id, 1, 4'103);
    require(accepted_evidence_watermark(fixture.state, order) > after_ack,
            "a booked fill did not move the watermark");

    require(accepted_evidence_watermark(fixture.state, ClusterOrderId{9'999}) == 0,
            "an unknown order reported a non-zero evidence watermark");
}


enum class CommitOutcome { committed, result_lost, leadership_lost };

struct CommitBoundary {
    TradingState* state;
    ApplyScratch scratch;
    std::optional<ApplyResult> truth;

    std::optional<ApplyResult> propose(const CommandIdentity& identity,
                                       const VenueObservation& observation,
                                       const CommitOutcome outcome) {
        const bool is_fill = observation.kind == VenueObservationKind::fill;
        const Command command{identity,
                              is_fill ? CommandBody{ObserveFill{observation}}
                                      : CommandBody{ObserveOrderEvent{observation}}};
        if (outcome == CommitOutcome::leadership_lost) {
            return std::nullopt;
        }
        truth = apply(*state, command, scratch);
        if (outcome == CommitOutcome::result_lost) return std::nullopt;
        return truth;
    }
};

VenueObservation seam_fill(const TradingState& state, const EffectId effect_id,
                           const std::int64_t quantity) {
    VenueObservation fill;
    fill.kind = VenueObservationKind::fill;
    fill.venue_id = VenueId{9};
    fill.venue_session_id = VenueSessionId{1};
    fill.venue_sequence = VenueSequence{8'400};
    fill.cluster_order_id = sole_order(state).cluster_order_id;
    fill.effect_id = effect_id;
    fill.venue_order_id = VenueOrderId{88};
    fill.execution_id = ExecutionId{8'400};
    fill.side = Side::buy;
    fill.price = PriceTicks::from_atoms(11);
    fill.quantity = QuantityLots::from_atoms(quantity);
    fill.evidence_digest = digest(0x78);
    return fill;
}

Fixture acknowledged_order() {
    auto fixture = one_authorized_submit();
    VenueObservation ack;
    ack.kind = VenueObservationKind::order_acknowledged;
    ack.venue_id = VenueId{9};
    ack.venue_session_id = VenueSessionId{1};
    ack.venue_sequence = VenueSequence{8'300};
    ack.cluster_order_id = sole_order(fixture.state).cluster_order_id;
    ack.effect_id = fixture.effect_id;
    ack.venue_order_id = VenueOrderId{88};
    ack.evidence_digest = digest(0x71);
    require(submit_answer(fixture.state, ack, 8'301).code == ApplyCode::applied,
            "the seam fixture could not acknowledge its order");
    return fixture;
}

void test_a_venue_answer_does_not_discharge_the_commit_obligation() {
    const GatewayTimeouts timeouts;
    auto fixture = acknowledged_order();
    auto delivery = running_gateway();
    require(record_send(delivery, fixture.effect_id, 0), "record_send failed");

    const auto fill = seam_fill(fixture.state, fixture.effect_id, 1);
    const CommandIdentity identity{CallerId{1}, RequestSequence{8'401},
                                   observation_fingerprint(fill)};

    require(record_response(delivery, fixture.effect_id), "record_response failed");
    GatewayEvidenceState evidence;
    require(record_evidence_obligation(evidence, identity, fill, fixture.effect_id, 0),
            "the evidence queue refused its first obligation");
    require(find_obligation(evidence, identity) != nullptr,
            "the obligation was not recorded");

    const auto first = plan_evidence(evidence, timeouts, 0);
    require(first.proposal_count == 1 && first.to_propose[0] == identity,
            "a fresh obligation was not proposed immediately");

    require(record_proposal(evidence, identity, 0), "record_proposal failed");
    require(find_obligation(evidence, identity) != nullptr,
            "a proposal discharged the obligation");
    require(plan_evidence(evidence, timeouts, 0).proposal_count == 0,
            "an outstanding proposal was immediately re-proposed");

    const auto again = plan_evidence(evidence, timeouts, timeouts.commit_deadline);
    require(again.proposal_count == 1 && again.to_propose[0] == identity,
            "a proposal whose result never arrived was never retried");

    require(discharge_evidence(evidence, identity),
            "a committed result did not discharge the obligation");
    require(find_obligation(evidence, identity) == nullptr,
            "the obligation survived its commit");
    require(plan_evidence(evidence, timeouts, 1'000).proposal_count == 0,
            "a discharged obligation was proposed again");
}

void test_a_lost_commit_result_moves_the_position_exactly_once() {
    const GatewayTimeouts timeouts;
    auto fixture = acknowledged_order();
    CommitBoundary cluster{&fixture.state, {}, {}};
    const auto before = fixture.state.risk_accounts[0].position_quantity;

    const auto fill = seam_fill(fixture.state, fixture.effect_id, 1);
    const CommandIdentity identity{CallerId{1}, RequestSequence{8'402},
                                   observation_fingerprint(fill)};
    GatewayEvidenceState evidence;
    require(record_evidence_obligation(evidence, identity, fill, fixture.effect_id, 0),
            "the evidence queue refused an obligation");

    require(record_proposal(evidence, identity, 0), "record_proposal failed");
    require(!cluster.propose(identity, fill, CommitOutcome::result_lost).has_value(),
            "a lost result was still observed");
    require(find_obligation(evidence, identity) != nullptr,
            "a lost result silently discharged the obligation");
    const auto after_first = fixture.state.risk_accounts[0].position_quantity;
    require(after_first.atoms() == before.atoms() + 1,
            "the first proposal did not move the position");

    const auto original = *cluster.truth;
    const auto version_before_retry = fixture.state.state_version;
    const auto retry = plan_evidence(evidence, timeouts, timeouts.commit_deadline);
    require(retry.proposal_count == 1 && retry.to_propose[0] == identity,
            "the obligation was not retried");
    require(record_proposal(evidence, identity, timeouts.commit_deadline),
            "record_proposal failed");
    const auto result = cluster.propose(identity, fill, CommitOutcome::committed);
    require(result.has_value(), "the retry produced no result");

    require(result->code == original.code &&
                result->state_version == original.state_version,
            "an identical retry did not replay the original result");
    require(fixture.state.state_version == version_before_retry,
            "the retry advanced the state machine, so it re-executed");
    require(fixture.state.risk_accounts[0].position_quantity.atoms() ==
                after_first.atoms(),
            "the retry moved the position a second time");

    require(discharge_evidence(evidence, identity),
            "a replayed result did not discharge the obligation");
}

void test_simulator_evidence_survives_every_pre_ack_crash_cut() {
    auto fixture = one_authorized_submit();
    const auto effect = materialize_order_effect(fixture.state, fixture.effect_id);
    require(effect.has_value(), "the crash fixture could not materialize its effect");
    sim::SimulatedVenue venue(1, sim::FaultPolicy::clean(), VenueId{9});
    require(venue.send(*effect, 0) == sim::SendOutcome::accepted,
            "the crash fixture could not send its effect");

    const auto first_poll = venue.poll(100);
    const auto fill = std::find_if(
        first_poll.begin(), first_poll.end(), [](const VenueObservation& value) {
            return value.kind == VenueObservationKind::fill;
        });
    require(fill != first_poll.end(), "the venue produced no fill evidence");
    const auto canonical = encode_payload(*fill).bytes;

    venue.restart_delivery();
    const auto after_poll_crash = venue.poll(100);
    require(std::any_of(after_poll_crash.begin(), after_poll_crash.end(),
                        [&](const VenueObservation& value) {
                            return encode_payload(value).bytes == canonical;
                        }),
            "poll-before-obligation lost the venue evidence");

    GatewayEvidenceState abandoned;
    const CommandIdentity abandoned_identity{
        CallerId{1}, RequestSequence{50'000}, observation_fingerprint(*fill)};
    require(record_evidence_obligation(abandoned, abandoned_identity, *fill,
                                       fill->effect_id, 0),
            "the crash fixture could not record its obligation");
    venue.restart_delivery();
    const auto after_obligation_crash = venue.poll(100);
    require(std::any_of(after_obligation_crash.begin(),
                        after_obligation_crash.end(),
                        [&](const VenueObservation& value) {
                            return encode_payload(value).bytes == canonical;
                        }),
            "obligation-before-proposal lost the venue evidence");

    ApplyScratch scratch;
    const Command first_command{abandoned_identity, ObserveFill{*fill}};
    const auto first_result = apply(fixture.state, first_command, scratch);
    require(first_result.code == ApplyCode::applied,
            "the crash fixture could not commit its fill");
    const auto position_after_commit =
        fixture.state.risk_accounts[0].position_quantity;

    venue.restart_delivery();
    const auto after_result_loss = venue.poll(100);
    require(std::any_of(after_result_loss.begin(), after_result_loss.end(),
                        [&](const VenueObservation& value) {
                            return encode_payload(value).bytes == canonical;
                        }),
            "apply-before-result lost the unacknowledged venue evidence");

    const Command retry{{CallerId{1}, RequestSequence{50'001},
                         observation_fingerprint(*fill)},
                        ObserveFill{*fill}};
    const auto replayed = apply(fixture.state, retry, scratch);
    require(replayed.code == ApplyCode::already_applied &&
                fixture.state.risk_accounts[0].position_quantity ==
                    position_after_commit,
            "replayed venue evidence changed economics twice");

    for (const auto& observation : first_poll) {
        venue.acknowledge_observation(observation);
    }
    venue.restart_delivery();
    require(venue.poll(100).empty(),
            "acknowledged venue evidence survived its ownership transfer");
}

void test_leadership_loss_before_commit_leaves_the_evidence_owned() {
    const GatewayTimeouts timeouts;
    auto fixture = acknowledged_order();
    CommitBoundary cluster{&fixture.state, {}, {}};
    const auto before = fixture.state.risk_accounts[0].position_quantity;

    const auto fill = seam_fill(fixture.state, fixture.effect_id, 1);
    const CommandIdentity identity{CallerId{1}, RequestSequence{8'403},
                                   observation_fingerprint(fill)};
    GatewayEvidenceState evidence;
    require(record_evidence_obligation(evidence, identity, fill, fixture.effect_id, 0),
            "the evidence queue refused an obligation");
    require(record_proposal(evidence, identity, 0), "record_proposal failed");
    require(!cluster.propose(identity, fill, CommitOutcome::leadership_lost)
                 .has_value(),
            "a lost leadership produced a result");
    require(fixture.state.risk_accounts[0].position_quantity.atoms() ==
                before.atoms(),
            "a command that never committed still moved the position");
    require(find_obligation(evidence, identity) != nullptr,
            "a leadership change discharged the obligation");

    const auto retry = plan_evidence(evidence, timeouts, timeouts.commit_deadline);
    require(retry.proposal_count == 1 && retry.to_propose[0] == identity,
            "evidence stranded by a leadership change was never retried");
    require(record_proposal(evidence, identity, timeouts.commit_deadline),
            "record_proposal failed");
    const auto result = cluster.propose(identity, fill, CommitOutcome::committed);
    require(result.has_value() && result->code == ApplyCode::applied,
            "the retry after a leadership change was not applied");
    require(discharge_evidence(evidence, identity), "discharge failed");
    require(fixture.state.risk_accounts[0].position_quantity.atoms() ==
                before.atoms() + 1,
            "the retry did not deliver the evidence exactly once");
}

void test_a_saturated_evidence_queue_stops_producing_more_evidence() {
    const GatewayTimeouts timeouts;
    const auto fixture = one_authorized_submit();
    auto delivery = running_gateway();
    GatewayEvidenceState evidence;

    const auto fill = seam_fill(fixture.state, fixture.effect_id, 1);
    std::vector<CommandIdentity> queued;
    for (std::uint64_t index = 0; index < kMaxEvidenceObligations; ++index) {
        const CommandIdentity identity{CallerId{1}, RequestSequence{8'500 + index},
                                       digest(static_cast<std::uint8_t>(
                                           1 + index % 200))};
        require(record_evidence_obligation(evidence, identity, fill,
                                           fixture.effect_id, 0),
                "the evidence queue refused an obligation before it was full");
        queued.push_back(identity);
    }
    require(evidence_queue_saturated(evidence), "the queue never saturated");
    const CommandIdentity overflow{CallerId{1}, RequestSequence{9'999}, digest(0x5F)};
    require(!record_evidence_obligation(evidence, overflow, fill, fixture.effect_id, 0),
            "a full evidence queue accepted one more obligation");

    require(plan_delivery(fixture.state, delivery, evidence, control_of(fixture.state),
                          capable_venue(), SendQuotaView{4}, timeouts, 0)
                    .send_count == 0,
            "a saturated evidence queue still scheduled a send");

    require(record_send(delivery, fixture.effect_id, 0), "record_send failed");
    require(plan_delivery(fixture.state, delivery, evidence, control_of(fixture.state),
                          capable_venue(), SendQuotaView{4}, timeouts,
                          timeouts.response_deadline)
                    .ambiguous_count == 0,
            "a saturated evidence queue still produced an ambiguity report");

    require(discharge_evidence(evidence, queued.front()), "discharge failed");
    require(!evidence_queue_saturated(evidence), "the queue stayed saturated");
    require(plan_delivery(fixture.state, delivery, evidence, control_of(fixture.state),
                          capable_venue(), SendQuotaView{4}, timeouts,
                          timeouts.response_deadline)
                    .ambiguous_count == 1,
            "the planner stayed stopped after the queue drained");
}


void test_a_gateway_crash_cannot_produce_a_second_order() {
    for (int cut = 0; cut <= 2; ++cut) {
        const std::string where = " (cut " + std::to_string(cut) + ")";
        auto fixture = one_authorized_submit();
        auto& state = fixture.state;
        SimulatedVenue venue(1, FaultPolicy::clean(), VenueId{9});
        const GatewayTimeouts timeouts;
        sim::Tick now = 0;
        std::size_t venue_orders = 0;

        {
            auto before_crash = running_gateway();
            if (cut >= 1) {
                const auto decision =
                    evaluate_pre_send(state, fixture.effect_id, control_of(state),
                                      capable_venue(), SendQuotaView{4});
                require(decision.permitted(), "the guard refused the fixture" + where);
                require(venue.send(*decision.effect, now) == SendOutcome::accepted,
                        "the venue refused the submit" + where);
                ++venue_orders;
            }
            if (cut >= 2) {
                require(record_send(before_crash, fixture.effect_id, now),
                        "record_send failed" + where);
            }
        }

        now += 1;
        GatewayDeliveryState delivery;
        require(begin_gateway_recovery(delivery, state, now) == 1,
                "the startup scan did not find the pending submit" + where);
        require(delivery.started && delivery.recovering,
                "the startup scan did not raise the barrier" + where);
        require(!recovery_complete(delivery, state),
                "recovery completed before anything had been established" + where);

        require(plan_delivery(state, delivery, no_evidence, control_of(state), capable_venue(),
                              SendQuotaView{4}, timeouts, now)
                        .send_count == 0,
                "a restarted gateway scheduled a possibly-sent submit" + where);

        now += timeouts.response_deadline;
        const auto plan = plan_delivery(state, delivery, no_evidence, control_of(state),
                                        capable_venue(), SendQuotaView{4}, timeouts,
                                        now);
        require(plan.send_count == 0 && plan.ambiguous_count == 1 &&
                    plan.now_ambiguous[0] == fixture.effect_id,
                "the suspect effect was not declared ambiguous" + where);

        VenueObservation unknown;
        unknown.kind = VenueObservationKind::effect_unknown;
        unknown.venue_id = VenueId{9};
        unknown.venue_session_id = venue.session();
        unknown.venue_sequence = VenueSequence{910'000 + static_cast<std::uint64_t>(cut)};
        unknown.cluster_order_id = sole_order(state).cluster_order_id;
        unknown.effect_id = fixture.effect_id;
        unknown.evidence_digest = digest(0x7E);
        require(submit_answer(state, unknown, 9'100).code == ApplyCode::applied,
                "the cluster refused the recovery's unknown report" + where);
        require(record_response(delivery, fixture.effect_id),
                "record_response did not find the suspect attempt" + where);

        require(recovery_complete(delivery, state),
                "the barrier never lifted" + where);
        delivery.recovering = false;

        require(plan_delivery(state, delivery, no_evidence, control_of(state), capable_venue(),
                              SendQuotaView{4}, timeouts, now)
                        .send_count == 0,
                "the suspect submit became resendable once recovery finished" + where);

        GatewayReconciliationState reconciliation;
        const auto recon = plan_reconciliation(state, reconciliation, delivery,
                                               control_of(state), capable_venue(),
                                               timeouts, now);
        require(recon.query_count == 1 &&
                    recon.to_query[0].cluster_order_id ==
                        sole_order(state).cluster_order_id,
                "the restart left an order nobody would query" + where);

        const auto raw = venue.query(recon.to_query[0].cluster_order_id, now);
        require(record_query(reconciliation, recon.to_query[0].cluster_order_id, now,
                             accepted_evidence_watermark(
                                 state, recon.to_query[0].cluster_order_id)),
                "record_query failed" + where);
        require(record_answer(reconciliation, recon.to_query[0].cluster_order_id),
                "record_answer failed" + where);

        require((cut == 0) == (raw.outcome == QueryOutcome::not_found),
                "the venue's answer did not match what actually happened" + where);

        const auto answer = reconciled(
            state, fixture.effect_id,
            raw.outcome == QueryOutcome::known ? raw.venue_order_id.value() : 0,
            raw.filled_quantity.atoms(), 9'200);
        const auto adjudicated = submit_answer(state, answer, 9'201);
        if (cut == 0) {
            require(adjudicated.code == ApplyCode::applied,
                    "a definitive not-found was refused" + where);
            require(sole_order(state).state == ClusterOrderState::rejected,
                    "a denied order did not resolve after recovery" + where);
            require(risk_released(state),
                    "a denied order kept its reservation after recovery" + where);
        } else {
            require(adjudicated.code == ApplyCode::venue_observation_not_applicable,
                    "an answer ahead of the cluster was not refused" + where);
            require(sole_order(state).state == ClusterOrderState::submission_unknown,
                    "a refused answer moved the order" + where);

            std::uint64_t sequence = 9'300;
            for (const auto& observation : venue.poll(now + 8)) {
                const bool is_fill = observation.kind == VenueObservationKind::fill;
                const Command command{
                    {CallerId{1}, RequestSequence{++sequence},
                     observation_fingerprint(observation)},
                    is_fill ? CommandBody{ObserveFill{observation}}
                            : CommandBody{ObserveOrderEvent{observation}}};
                ApplyScratch scratch;
                (void)apply(state, command, scratch);
            }
            require(sole_order(state).state == ClusterOrderState::filled,
                    "the in-flight evidence did not resolve the restart" + where);
        }

        require(venue_orders == static_cast<std::size_t>(cut == 0 ? 0 : 1),
                "the restart changed how many orders exist at the venue" + where);
    }
}

void test_an_incomplete_effect_does_not_deadlock_restart_recovery() {
    auto fixture = one_authorized_submit();
    fixture.state.outbox_effects[0].state = OutboxState::incomplete;

    GatewayDeliveryState delivery;
    require(begin_gateway_recovery(delivery, fixture.state, 17) == 0,
            "restart recovery treated a settled incomplete effect as in flight");
    require(delivery.started && !delivery.recovering,
            "a settled incomplete effect raised the startup recovery barrier");
    require(recovery_complete(delivery, fixture.state),
            "a settled incomplete effect made recovery impossible to complete");
    require(plan_delivery(fixture.state, delivery, no_evidence,
                          control_of(fixture.state), capable_venue(),
                          SendQuotaView{4}, GatewayTimeouts{}, 17)
                    .send_count == 0,
            "restart attempted to deliver a settled incomplete effect");
}

void test_an_effect_becoming_incomplete_during_recovery_clears_the_barrier() {
    auto fixture = one_authorized_submit();
    const auto order = sole_order(fixture.state).cluster_order_id;

    ApplyScratch scratch;
    const Command cancel{{CallerId{1}, RequestSequence{3}, digest(0x61)},
                         RequestCancel{order}};
    const auto requested = apply(fixture.state, cancel, scratch);
    require(requested.code == ApplyCode::applied,
            "the recovery fixture could not request its cancel");

    GatewayDeliveryState delivery;
    require(begin_gateway_recovery(delivery, fixture.state, 17) == 2,
            "the recovery scan did not retain both pending effects");

    VenueObservation unknown;
    unknown.kind = VenueObservationKind::effect_unknown;
    unknown.venue_id = VenueId{9};
    unknown.venue_session_id = VenueSessionId{1};
    unknown.venue_sequence = VenueSequence{8'100};
    unknown.cluster_order_id = order;
    unknown.effect_id = requested.effect_id();
    unknown.evidence_digest = digest(0x62);
    require(submit_answer(fixture.state, unknown, 4).code == ApplyCode::applied,
            "the recovery fixture could not mark its cancel unknown");

    const auto final = cancel_final(fixture.state, requested.effect_id(), 0,
                                    8'101);
    require(submit_answer(fixture.state, final, 5).code == ApplyCode::applied,
            "the recovery fixture could not reconcile its cancel");
    require(fixture.state.outbox_effects[0].state == OutboxState::incomplete,
            "definitive cancellation did not settle the orphaned submit");
    require(recovery_complete(delivery, fixture.state),
            "a replicated incomplete transition left a stale recovery barrier");
    require(record_response(delivery, fixture.effect_id),
            "the local response flag could not be cleared");
    require(recovery_complete(delivery, fixture.state),
            "an incomplete effect blocked recovery after its local response cleared");
}

void test_recovery_blocks_new_risk_without_blocking_a_cancel() {
    const GatewayTimeouts timeouts;
    auto fixture = pending_cancel();
    GatewayDeliveryState delivery;
    require(begin_gateway_recovery(delivery, fixture.state, 0) == 1,
            "the startup scan did not find the pending cancel");
    require(delivery.recovering, "the startup scan did not raise the barrier");

    require(plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
                          capable_venue(), SendQuotaView{4}, timeouts, 0)
                    .send_count == 0,
            "a suspect cancel was redelivered before its outcome was declared");
    const auto matured =
        plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
                      capable_venue(), SendQuotaView{4}, timeouts,
                      timeouts.response_deadline);
    require(matured.ambiguous_count == 1 &&
                matured.now_ambiguous[0] == fixture.effect_id,
            "the suspect cancel was never declared ambiguous");
    require(record_response(delivery, fixture.effect_id), "record_response failed");

    require(delivery.recovering, "the barrier lifted on its own");
    require(plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
                          capable_venue(), SendQuotaView{4}, timeouts,
                          timeouts.response_deadline)
                    .send_count == 1,
            "the recovery barrier refused to redeliver a cancel");

    const auto submit_fixture = one_authorized_submit();
    auto barred = running_gateway();
    barred.recovering = true;
    require(plan_delivery(submit_fixture.state, barred, no_evidence,
                          control_of(submit_fixture.state), capable_venue(),
                          SendQuotaView{4}, timeouts, 0)
                    .send_count == 0,
            "the recovery barrier let new risk through");
    barred.recovering = false;
    require(plan_delivery(submit_fixture.state, barred, no_evidence,
                          control_of(submit_fixture.state), capable_venue(),
                          SendQuotaView{4}, timeouts, 0)
                    .send_count == 1,
            "lifting the barrier did not restore ordinary delivery, so the "
            "refusal above proved nothing about the barrier");
}

void test_a_gateway_that_never_scanned_sends_nothing() {
    const auto fixture = one_authorized_submit();
    const GatewayDeliveryState never_started;
    require(!never_started.started, "a fresh gateway claimed to have started");
    require(plan_delivery(fixture.state, never_started, no_evidence, control_of(fixture.state),
                          capable_venue(), SendQuotaView{4}, GatewayTimeouts{10}, 0)
                    .send_count == 0,
            "a gateway that never scanned replicated state sent anyway");
}

// --- native versus synthetic observation identity (architecture 10.15) ------

VenueObservation at_sequence(const TradingState& state, const EffectId effect_id,
                             const VenueObservationKind kind,
                             const std::uint64_t sequence) {
    VenueObservation value;
    value.kind = kind;
    value.venue_id = VenueId{9};
    value.venue_session_id = VenueSessionId{1};
    value.venue_sequence = VenueSequence{sequence};
    value.cluster_order_id = sole_order(state).cluster_order_id;
    value.effect_id = effect_id;
    value.venue_order_id = VenueOrderId{88};
    value.evidence_digest = digest(0x6D);
    return value;
}

void test_a_gateway_authored_event_cannot_squat_on_a_venue_sequence() {
    // The gateway picks the sequence on an `effect_unknown`; the venue picks its
    // own. Nothing coordinates the two, so they can collide - and the loser must
    // never be real venue evidence.
    auto fixture = pending_cancel();
    const auto cancel_effect = fixture.effect_id;
    constexpr std::uint64_t contested = 900'041;

    const auto unknown = at_sequence(fixture.state, cancel_effect,
                                     VenueObservationKind::effect_unknown,
                                     contested);
    require(submit_answer(fixture.state, unknown, 4'400).code == ApplyCode::applied,
            "the gateway could not report its cancel unknown");

    const auto cancelled = at_sequence(fixture.state, cancel_effect,
                                       VenueObservationKind::cancelled, contested);
    const auto applied = submit_answer(fixture.state, cancelled, 4'401);
    require(applied.code == ApplyCode::applied,
            "a native cancellation was refused because a gateway-authored record "
            "had taken its venue sequence, code " +
                std::to_string(static_cast<int>(applied.code)));
    require(sole_order(fixture.state).state != ClusterOrderState::submission_unknown,
            "the cancellation was accepted but did not resolve the cancel");
}

void test_a_synthetic_answer_is_not_blocked_by_a_native_sequence() {
    // The same collision the other way. A reconciliation answer is the only
    // thing that can resolve an unknown effect, so refusing it on a number the
    // venue happened to use once strands the order with no way out.
    auto fixture = silent_cancel();
    constexpr std::uint64_t contested = 7'100;  // the acknowledgement's sequence

    auto answer = at_sequence(fixture.state, fixture.effect_id,
                              VenueObservationKind::cancel_reconciled, contested);
    answer.quantity = QuantityLots{};
    const auto applied = submit_answer(fixture.state, answer, 4'410);
    require(applied.code == ApplyCode::applied,
            "a reconciliation answer was refused because the venue had once used "
            "that sequence number, code " +
                std::to_string(static_cast<int>(applied.code)));
}

void test_a_replayed_synthetic_answer_is_caught_by_command_identity() {
    // Synthetic observations leave the venue's deduplication domain, so command
    // identity is what stops a replay. That makes the evidence seam's rule -
    // retry under the identity you first used - load-bearing for reconciliation
    // and not only for delivery. This is the test that fails if it stops being
    // true.
    auto fixture = silent_cancel();
    auto answer = at_sequence(fixture.state, fixture.effect_id,
                              VenueObservationKind::cancel_reconciled, 8'800);
    answer.quantity = QuantityLots{};

    const CommandIdentity identity{CallerId{1}, RequestSequence{4'420},
                                   observation_fingerprint(answer)};
    ApplyScratch scratch;
    const Command command{identity, ObserveOrderEvent{answer}};
    const auto first = apply(fixture.state, command, scratch);
    require(first.code == ApplyCode::applied, "the reconciliation answer failed");
    const auto version_after_first = fixture.state.state_version;
    const auto settled = sole_order(fixture.state).state;

    const auto replay = apply(fixture.state, command, scratch);
    require(replay.code == first.code && replay.state_version == first.state_version,
            "a replayed synthetic answer did not replay its original result");
    require(fixture.state.state_version == version_after_first,
            "a replayed synthetic answer advanced the state machine");
    require(sole_order(fixture.state).state == settled,
            "a replayed synthetic answer moved the order");

    // A fresh identity carrying the same verdict is harmless by VALUE: a final
    // cumulative total re-decided against what is already booked is a delta of
    // zero.
    const auto position = fixture.state.risk_accounts[0].position_quantity;
    const CommandIdentity fresh{CallerId{1}, RequestSequence{4'421},
                                observation_fingerprint(answer)};
    (void)apply(fixture.state, Command{fresh, ObserveOrderEvent{answer}}, scratch);
    require(fixture.state.risk_accounts[0].position_quantity == position,
            "re-adjudicating the same verdict moved the position");
}

void test_native_kinds_keep_strict_sequence_deduplication() {
    // The native domain is unchanged, and the point of separating the two was
    // to keep it that way rather than to weaken it.
    auto fixture = one_authorized_submit();
    const auto submit_effect = fixture.effect_id;

    const auto ack = at_sequence(fixture.state, submit_effect,
                                 VenueObservationKind::order_acknowledged, 3'300);
    require(submit_answer(fixture.state, ack, 4'430).code == ApplyCode::applied,
            "the acknowledgement was refused");

    auto contradiction = at_sequence(fixture.state, submit_effect,
                                     VenueObservationKind::order_rejected, 3'300);
    contradiction.evidence_digest = digest(0x6E);
    require(submit_answer(fixture.state, contradiction, 4'431).code ==
                ApplyCode::venue_event_identity_conflict,
            "a venue that reused a sequence for different content was not "
            "refused");

    require(submit_answer(fixture.state, ack, 4'432).code ==
                ApplyCode::already_applied,
            "a byte-identical native repeat was not recognised as a duplicate");
}

void test_a_denied_order_releases_its_reservation_and_frees_the_slot() {
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
    const GatewayReconciliationState fresh;
    const GatewayDeliveryState idle;
    require(plan_reconciliation(fixture.state, fresh, idle,
                                control_of(fixture.state), capable_venue(),
                                GatewayTimeouts{}, 0)
                    .query_count == 1,
            "a refused answer left the order unqueryable");
}

void test_a_venue_that_lost_a_fill_is_a_conflict() {
    auto fixture = silent_cancel_with_booked_fill(1);
    const auto applied = submit_answer(
        fixture.state, reconciled(fixture.state, fixture.effect_id, 88, 0), 20);
    require(applied.code == ApplyCode::applied,
            "a regressing answer was not recorded");
    require(sole_order(fixture.state).state == ClusterOrderState::conflict,
            "a venue losing a booked fill was not a conflict");
}

void test_a_snapshot_taken_before_a_fill_cannot_strand_the_order() {
    const GatewayTimeouts timeouts;
    auto fixture = silent_cancel();
    const auto submit_id = submit_effect_of(fixture.state);
    const auto order = sole_order(fixture.state).cluster_order_id;

    GatewayReconciliationState reconciliation;
    const auto at_query = accepted_evidence_watermark(fixture.state, order);
    require(record_query(reconciliation, order, 10, at_query),
            "record_query failed");

    auto snapshot = complete_answer(VenueQueryStatus::known);
    snapshot.venue_order_id = VenueOrderId{88};
    snapshot.filled_quantity = QuantityLots{};

    book_partial_fill(fixture.state, submit_id, 1, 5'400);
    require(sole_order(fixture.state).state == ClusterOrderState::submission_unknown,
            "the fill resolved the order, so the window this test needs is gone");
    const auto at_answer = accepted_evidence_watermark(fixture.state, order);
    require(at_answer != at_query, "the fill did not move the evidence watermark");

    {
        auto counterfactual = fixture.state;
        require(submit_answer(counterfactual,
                              reconciled(counterfactual, fixture.effect_id, 88, 0),
                              5'401)
                        .code == ApplyCode::applied,
                "the stale answer was not even accepted");
        require(sole_order(counterfactual).state == ClusterOrderState::conflict,
                "the stale answer did not strand the order, so the rest of this "
                "test proves nothing");
    }

    require(classify_query(snapshot, find_reconciliation(reconciliation, order),
                           at_answer, timeouts) == QueryDisposition::incomplete,
            "a snapshot taken before a committed fill was acted on");
    require(sole_order(fixture.state).state == ClusterOrderState::submission_unknown,
            "refusing to act moved the order anyway");

    require(record_answer(reconciliation, order), "record_answer failed");
    require(record_query(reconciliation, order, 20, at_answer),
            "record_query failed");
    auto fresh_snapshot = snapshot;
    fresh_snapshot.filled_quantity = QuantityLots::from_atoms(1);
    require(classify_query(fresh_snapshot, find_reconciliation(reconciliation, order),
                           accepted_evidence_watermark(fixture.state, order),
                           timeouts) == QueryDisposition::submit,
            "a comparable answer was refused");
    require(submit_answer(fixture.state,
                          reconciled(fixture.state, fixture.effect_id, 88, 1),
                          5'402)
                    .code == ApplyCode::applied,
            "the agreeing answer was refused");
    require(sole_order(fixture.state).state == ClusterOrderState::partially_filled,
            "an agreeing answer did not resolve the order");
}

void test_replaying_the_same_answer_changes_nothing() {
    auto fixture = stranded_submit();
    const auto answer = reconciled(fixture.state, fixture.effect_id, 77, 0);
    require(submit_answer(fixture.state, answer, 20).code == ApplyCode::applied,
            "the first answer was refused");
    const auto settled = fixture.state;

    require(submit_answer(fixture.state, answer, 20).code == ApplyCode::applied,
            "an exact command replay did not return its retained result");

    // A repeat under a FRESH request identity is not refused by the venue's
    // sequence domain, because a synthetic answer never belonged to that domain -
    // its sequence was chosen by the gateway.
    //
    // CHANGED BY ADR 0008, DELIBERATELY. This previously required a refusal, on
    // the ground that the effect was already resolved and the answer had nothing
    // left to say. That ground no longer holds: the first answer left the order
    // RESTING, and a resting order is now confirmed rather than assumed, on an
    // uncapped heartbeat. Refusing repeat confirmations is precisely the
    // "assume" behaviour ADR 0008 exists to end - a cancellation whose event
    // never arrived would be undiscoverable.
    //
    // Admitting it was always safe, and the comment this replaces said so:
    // adjudicating a FINAL CUMULATIVE total against what the cluster already
    // holds computes a delta of zero. Harmless by value is the property that was
    // doing the real work; the refusal was the tidier statement, not the safer
    // one.
    const auto before_repeat = fixture.state;
    require(submit_answer(fixture.state, answer, 21).code == ApplyCode::applied,
            "a repeat confirmation of a resting order was refused, which returns "
            "the cluster to assuming rather than confirming");
    require(fixture.state.orders[0].state == before_repeat.orders[0].state &&
                fixture.state.reservations[0] == before_repeat.reservations[0] &&
                fixture.state.risk_accounts[0] == before_repeat.risk_accounts[0],
            "a repeat confirmation moved something, so it was not a no-op by value");
    require(sole_order(fixture.state).state == settled.orders[0].state,
            "a re-adjudicated answer moved the order");

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
    // CHANGED BY ADR 0008. This encoded the assumption the ADR overturns - that a
    // live order "needs none" - and it is the most direct statement of it in the
    // suite. A resting order is now confirmed rather than assumed, because a
    // cancellation whose event never arrived leaves an order looking exactly
    // like this one and nothing else would ever discover it.
    //
    // The answer here says the venue still holds the order with zero filled,
    // which agrees with what the cluster holds, so it is admitted and moves
    // nothing. That is what a confirmation is supposed to look like.
    const auto before_confirm = live.state;
    require(submit_answer(live.state, reconciled(live.state, live.effect_id, 77, 0),
                          20)
                    .code == ApplyCode::applied,
            "a confirmation of a resting order was refused, so a lost "
            "cancellation would stay undiscoverable");
    require(sole_order(live.state).state == ClusterOrderState::live,
            "confirming a resting order moved it");
    require(live.state.reservations[0] == before_confirm.reservations[0] &&
                live.state.risk_accounts[0] == before_confirm.risk_accounts[0],
            "confirming a resting order changed economics");

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


void test_a_cancel_is_deliverable_in_every_strategy_mode() {
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
    const auto fixture = silent_cancel();
    const GatewayTimeouts timeouts;
    const GatewayReconciliationState fresh;
    auto delivery = running_gateway();

    require(record_send(delivery, fixture.effect_id, 0), "record_send failed");
    require(record_response(delivery, fixture.effect_id), "record_response failed");
    const auto plan = plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
                                    capable_venue(), SendQuotaView{4}, timeouts, 5);
    require(plan.send_count == 1 && plan.to_send[0] == fixture.effect_id,
            "a silent cancel was not redelivered under its own identity");
    require(plan_reconciliation(fixture.state, fresh, delivery,
                                control_of(fixture.state), capable_venue(), timeouts, 5)
                    .query_count == 0,
            "a cancel was queried while redelivery was still available");

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
    require(plan_delivery(fixture.state, delivery, no_evidence, control_of(fixture.state),
                          capable_venue(), SendQuotaView{4}, timeouts, 9)
                    .send_count == 0,
            "cancel redelivery was unbounded");
}

void test_a_rejected_cancel_does_not_prove_the_order_is_live() {
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

    {
        auto fixture = stranded_submit();
        require(submit_answer(fixture.state,
                              reconciled(fixture.state, fixture.effect_id, 0, 0), 20)
                        .code == ApplyCode::applied,
                "denied-submit fixture failed");
        round_trips(fixture.state, "submission denied by the venue");
    }
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
    {
        auto fixture = silent_cancel();
        require(submit_answer(fixture.state,
                              cancel_final(fixture.state, fixture.effect_id, 0), 20)
                        .code == ApplyCode::applied,
                "silent-cancel fixture failed");
        round_trips(fixture.state, "silent cancel finally reconciled");
    }
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

    require(after.state == ClusterOrderState::conflict,
            "booking an execution declared the recorded disagreement solved");

    const auto metrics = collect_metrics(fixture.state);
    require(metrics.orders_by_state[static_cast<std::size_t>(
                ClusterOrderState::conflict)] > 0,
            "the conflicted order stopped being reported");
    require(fixture.state.order_count == 1,
            "the conflicted group was reclaimed despite an unsettled dispute");

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

ApplyResult acknowledge_gateway_results(TradingState& state,
                                        std::uint64_t& request_sequence,
                                        const std::uint64_t through) {
    ApplyScratch scratch;
    const auto command_sequence = ++request_sequence;
    const Command command{{CallerId{1}, RequestSequence{command_sequence},
                           request_digest(command_sequence)},
                          AcknowledgeGatewayResults{RequestSequence{through}}};
    return apply(state, command, scratch);
}

bool has_receipt(const TradingState& state, const CallerId caller_id,
                 const RequestSequence request_sequence) {
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        if (state.receipts[index].identity.caller_id == caller_id &&
            state.receipts[index].identity.request_sequence == request_sequence) {
            return true;
        }
    }
    return false;
}

void test_gateway_acknowledgement_gates_venue_receipt_reclamation() {
    auto fixture = pending_cancel();
    ApplyScratch scratch;
    const auto order_id = sole_order(fixture.state).cluster_order_id;
    std::uint64_t sequence = 9'000;
    Command first_event;
    VenueObservation blocked_event;
    bool capacity_reached = false;
    std::uint64_t first_evidence_revision = 0;

    for (std::size_t cycle = 0; cycle < 40; ++cycle) {
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
        Command reject_command{{CallerId{1}, RequestSequence{reject_request},
                                observation_fingerprint(rejected)},
                               ObserveOrderEvent{rejected}};
        if (cycle == 0) first_event = reject_command;
        const auto rejected_result = apply(fixture.state, reject_command, scratch);
        if (rejected_result.code == ApplyCode::venue_event_capacity_exceeded) {
            blocked_event = rejected;
            capacity_reached = true;
            break;
        }
        require(rejected_result.code == ApplyCode::applied,
                "the unacknowledged fixture failed before its capacity bound");
        if (cycle == 0) {
            first_evidence_revision =
                accepted_evidence_watermark(fixture.state, order_id);
        }
        const auto venue_sequence = ++sequence;
        const auto request_sequence = ++sequence;
        auto answer =
            reconciled(fixture.state, current, 88, 0, venue_sequence);
        const auto reconciled_result =
            submit_answer(fixture.state, answer, request_sequence);
        if (reconciled_result.code == ApplyCode::venue_event_capacity_exceeded) {
            blocked_event = answer;
            capacity_reached = true;
            break;
        }
        require(reconciled_result.code == ApplyCode::applied,
                "the unacknowledged fixture failed before its capacity bound");
        const auto cancel_sequence = ++sequence;
        const Command again{{CallerId{1}, RequestSequence{cancel_sequence},
                             request_digest(cancel_sequence)},
                            RequestCancel{order_id}};
        if (apply(fixture.state, again, scratch).code != ApplyCode::applied) break;
    }

    require(capacity_reached,
            "unacknowledged committed results did not remain pinned");
    require(has_receipt(fixture.state, first_event.identity.caller_id,
                        first_event.identity.request_sequence),
            "an unacknowledged result was reclaimed");

    require(acknowledge_gateway_results(fixture.state, sequence, sequence - 1).code ==
                ApplyCode::applied,
            "the gateway result prefix could not advance");
    const auto* caller = std::find_if(
        fixture.state.caller_cursors.begin(),
        fixture.state.caller_cursors.begin() +
            static_cast<std::ptrdiff_t>(fixture.state.caller_cursor_count),
        [](const CallerCursor& value) { return value.caller_id == CallerId{1}; });
    require(caller != fixture.state.caller_cursors.begin() +
                          static_cast<std::ptrdiff_t>(
                              fixture.state.caller_cursor_count),
            "the gateway caller cursor was not retained");
    const auto acknowledged = caller->gateway_results_acknowledged_through;
    require(acknowledge_gateway_results(fixture.state, sequence, 1).code ==
                ApplyCode::already_applied,
            "a lower gateway acknowledgement was not idempotent");
    require(caller->gateway_results_acknowledged_through == acknowledged,
            "a lower gateway acknowledgement regressed the prefix");
    require(submit_answer(fixture.state, blocked_event, ++sequence).code ==
                ApplyCode::applied,
            "acknowledged settled evidence did not release completion capacity");
    require(!has_receipt(fixture.state, first_event.identity.caller_id,
                         first_event.identity.request_sequence),
            "acknowledged superseded evidence was not reclaimed");
    require(accepted_evidence_watermark(fixture.state, order_id) >
                first_evidence_revision,
            "receipt reclamation regressed the order evidence coordinate");

    const auto before = fixture.state;
    const auto replay = apply(fixture.state, first_event, scratch);
    require(replay.code == ApplyCode::request_expired,
            "a reclaimed command identity was re-executed");
    require(std::memcmp(&fixture.state.risk_accounts, &before.risk_accounts,
                        sizeof(before.risk_accounts)) == 0 &&
                std::memcmp(&fixture.state.reservations, &before.reservations,
                            sizeof(before.reservations)) == 0,
            "a reclaimed command identity moved economics");
}

void test_conflicting_fill_keeps_exact_evidence_after_acknowledged_sweep() {
    auto fixture = one_authorized_submit();
    ApplyScratch scratch;
    const auto& order = sole_order(fixture.state);

    VenueObservation fill;
    fill.kind = VenueObservationKind::fill;
    fill.venue_id = VenueId{9};
    fill.venue_session_id = VenueSessionId{1};
    fill.venue_sequence = VenueSequence{701};
    fill.cluster_order_id = order.cluster_order_id;
    fill.effect_id = order.effect_id;
    fill.venue_order_id = VenueOrderId{88};
    fill.execution_id = ExecutionId{701};
    fill.side = Side::buy;
    fill.price = PriceTicks::from_atoms(11);
    fill.quantity = QuantityLots::from_atoms(3);
    fill.evidence_digest = digest(0xA7);
    const Command command{{CallerId{1}, RequestSequence{3},
                           observation_fingerprint(fill)},
                          ObserveFill{fill}};

    const auto conflict = apply(fixture.state, command, scratch);
    require(conflict.code == ApplyCode::fill_quantity_conflict &&
                sole_order(fixture.state).state == ClusterOrderState::conflict,
            "the overfill fixture did not retain a conflict");
    std::uint64_t sequence = 3;
    require(acknowledge_gateway_results(fixture.state, sequence, 3).code ==
                ApplyCode::applied,
            "the conflicting fill result could not be acknowledged");

    for (std::uint64_t index = 1; index <= kMaxRequestReceipts + 8; ++index) {
        const Command pressure{
            {CallerId{3}, RequestSequence{index}, request_digest(index)},
            SetStrategyMode{fixture.state.strategy_mode, fixture.state.kill_epoch,
                            fixture.state.executor_generation}};
        require(apply(fixture.state, pressure, scratch).code ==
                    ApplyCode::already_applied,
                "receipt-pressure command was not admitted");
    }

    require(has_receipt(fixture.state, CallerId{1}, RequestSequence{3}),
            "an acknowledged conflicting fill lost its exact execution receipt");
    require(apply(fixture.state, command, scratch) == conflict,
            "a conflicting fill could not replay its exact retained result");
}

void test_unacknowledged_full_fill_result_survives_group_and_receipt_sweeps() {
    auto fixture = one_authorized_submit();
    ApplyScratch scratch;
    const auto order = sole_order(fixture.state);

    VenueObservation fill;
    fill.kind = VenueObservationKind::fill;
    fill.venue_id = VenueId{9};
    fill.venue_session_id = VenueSessionId{1};
    fill.venue_sequence = VenueSequence{702};
    fill.cluster_order_id = order.cluster_order_id;
    fill.effect_id = order.effect_id;
    fill.venue_order_id = VenueOrderId{88};
    fill.execution_id = ExecutionId{702};
    fill.side = Side::buy;
    fill.price = PriceTicks::from_atoms(11);
    fill.quantity = QuantityLots::from_atoms(2);
    fill.evidence_digest = digest(0xA8);
    const Command command{{CallerId{1}, RequestSequence{3},
                           observation_fingerprint(fill)},
                          ObserveFill{fill}};
    const auto result = apply(fixture.state, command, scratch);
    require(result.code == ApplyCode::applied,
            "the full-fill fixture did not apply");

    for (std::uint64_t index = 1; index <= kMaxDecisionRecords + 1; ++index) {
        const auto evidence = digest(0x44);
        const Command evaluate{
            {CallerId{3}, RequestSequence{index}, request_digest(index)},
            EvaluateDecisionFrame{DecisionId{20'000 + index},
                                  EvaluationMode::authoritative,
                                  frame(1, evidence, fixture.state)}};
        (void)apply(fixture.state, evaluate, scratch);
    }
    require(fixture.state.order_count == 0,
            "receipt test did not reclaim the resolved order group");

    for (std::uint64_t index = 1; index <= kMaxRequestReceipts + 8; ++index) {
        const Command pressure{
            {CallerId{4}, RequestSequence{index}, request_digest(index)},
            SetStrategyMode{fixture.state.strategy_mode, fixture.state.kill_epoch,
                            fixture.state.executor_generation}};
        (void)apply(fixture.state, pressure, scratch);
    }

    require(has_receipt(fixture.state, CallerId{1}, RequestSequence{3}),
            "an unacknowledged applied fill result was reclaimed");
    const auto position_before =
        fixture.state.risk_accounts[0].position_quantity;
    require(apply(fixture.state, command, scratch) == result &&
                fixture.state.risk_accounts[0].position_quantity ==
                    position_before,
            "a lost full-fill result could not replay without moving position");
}

void test_gateway_ack_receipt_cannot_expire_an_uncommitted_sequence_gap() {
    auto fixture = pending_cancel();
    ApplyScratch scratch;
    const auto order = sole_order(fixture.state);

    VenueObservation rejected;
    rejected.kind = VenueObservationKind::order_rejected;
    rejected.venue_id = VenueId{9};
    rejected.venue_session_id = VenueSessionId{1};
    rejected.venue_sequence = VenueSequence{703};
    rejected.cluster_order_id = order.cluster_order_id;
    rejected.effect_id = order.effect_id;
    rejected.evidence_digest = digest(0xA9);
    const Command first{{CallerId{2}, RequestSequence{1},
                         observation_fingerprint(rejected)},
                        ObserveOrderEvent{rejected}};
    require(apply(fixture.state, first, scratch).code == ApplyCode::applied,
            "the first evidence result did not commit");

    auto answer = reconciled(fixture.state, order.effect_id, 88, 0, 704);
    const Command gap{{CallerId{2}, RequestSequence{2},
                       observation_fingerprint(answer)},
                      ObserveOrderEvent{answer}};
    const Command ack{{CallerId{2}, RequestSequence{3}, request_digest(3)},
                      AcknowledgeGatewayResults{RequestSequence{1}}};
    require(apply(fixture.state, ack, scratch).code == ApplyCode::applied,
            "the acknowledgement above the sequence gap did not commit");

    for (std::uint64_t index = 1; index <= kMaxRequestReceipts + 8; ++index) {
        const Command pressure{
            {CallerId{4}, RequestSequence{index}, request_digest(index)},
            SetStrategyMode{fixture.state.strategy_mode, fixture.state.kill_epoch,
                            fixture.state.executor_generation}};
        (void)apply(fixture.state, pressure, scratch);
    }

    require(apply(fixture.state, gap, scratch).code == ApplyCode::applied,
            "an acknowledgement command expired uncommitted evidence below it");
}

Fixture mixed_identity_domain_watermark_fixture(
    std::uint64_t& request_sequence) {
    auto fixture = pending_cancel();
    const auto order_id = sole_order(fixture.state).cluster_order_id;
    ApplyScratch scratch;
    std::uint64_t synthetic_sequence = 900'000;

    for (std::size_t cycle = 0; cycle < 40; ++cycle) {
        const auto cancel_effect = sole_order(fixture.state).effect_id;

        VenueObservation rejected;
        rejected.kind = VenueObservationKind::order_rejected;
        rejected.venue_id = VenueId{9};
        rejected.venue_session_id = VenueSessionId{1};
        rejected.venue_sequence = VenueSequence{++synthetic_sequence};
        rejected.cluster_order_id = order_id;
        rejected.effect_id = cancel_effect;
        rejected.evidence_digest = digest(0x95);
        const auto reject_request = ++request_sequence;
        require(submit_answer(fixture.state, rejected, reject_request).code ==
                    ApplyCode::applied,
                "the mixed-domain fixture could not reject its cancel");
        require(acknowledge_gateway_results(fixture.state, request_sequence,
                                            reject_request)
                    .code == ApplyCode::applied,
                "the gateway could not acknowledge the committed rejection");

        auto answer = reconciled(fixture.state, cancel_effect, 88, 0,
                                 ++synthetic_sequence);
        const auto answer_request = ++request_sequence;
        require(submit_answer(fixture.state, answer, answer_request).code ==
                    ApplyCode::applied,
                "the mixed-domain fixture could not reconcile its cancel");
        require(acknowledge_gateway_results(fixture.state, request_sequence,
                                            answer_request)
                    .code == ApplyCode::applied,
                "the gateway could not acknowledge committed reconciliation");

        const Command cancel{{CallerId{1}, RequestSequence{++request_sequence},
                              request_digest(request_sequence)},
                             RequestCancel{order_id}};
        require(apply(fixture.state, cancel, scratch).code == ApplyCode::applied,
                "the mixed-domain fixture could not request its next cancel");
    }

    return fixture;
}

void test_a_fresh_fill_cannot_be_expired_by_gateway_authored_control_sequences() {
    std::uint64_t request_sequence = 20'000;
    auto fixture = mixed_identity_domain_watermark_fixture(request_sequence);
    const auto order_id = sole_order(fixture.state).cluster_order_id;
    const auto submit_effect = submit_effect_of(fixture.state);
    ApplyScratch scratch;

    VenueObservation fill;
    fill.kind = VenueObservationKind::fill;
    fill.venue_id = VenueId{9};
    fill.venue_session_id = VenueSessionId{1};
    fill.venue_sequence = VenueSequence{8'000};
    fill.cluster_order_id = order_id;
    fill.effect_id = submit_effect;
    fill.venue_order_id = VenueOrderId{88};
    fill.execution_id = ExecutionId{80'000};
    fill.side = Side::buy;
    fill.price = PriceTicks::from_atoms(11);
    fill.quantity = QuantityLots::from_atoms(1);
    fill.evidence_digest = digest(0x96);
    const Command command{{CallerId{1}, RequestSequence{++request_sequence},
                           observation_fingerprint(fill)},
                          ObserveFill{fill}};

    const auto position_before =
        fixture.state.risk_accounts[0].position_quantity;
    const auto applied = apply(fixture.state, command, scratch);
    require(applied.code == ApplyCode::applied,
            "a fresh execution was discarded by a watermark from another "
            "identity domain, code " +
                std::to_string(static_cast<int>(applied.code)));
    require(fixture.state.risk_accounts[0].position_quantity ==
                QuantityLots::from_atoms(position_before.atoms() + 1),
            "the fresh execution did not move the authoritative position");
}

void test_a_fresh_cancel_response_cannot_be_expired_by_gateway_authored_sequences() {
    std::uint64_t request_sequence = 30'000;
    auto fixture = mixed_identity_domain_watermark_fixture(request_sequence);
    const auto& order = sole_order(fixture.state);

    VenueObservation cancelled;
    cancelled.kind = VenueObservationKind::cancelled;
    cancelled.venue_id = VenueId{9};
    cancelled.venue_session_id = VenueSessionId{1};
    cancelled.venue_sequence = VenueSequence{8'000};
    cancelled.cluster_order_id = order.cluster_order_id;
    cancelled.effect_id = order.effect_id;
    cancelled.venue_order_id = VenueOrderId{88};
    cancelled.evidence_digest = digest(0x97);

    const auto applied =
        submit_answer(fixture.state, cancelled, ++request_sequence);
    require(applied.code == ApplyCode::applied,
            "a fresh cancel response was discarded by a watermark from another "
            "identity domain, code " +
                std::to_string(static_cast<int>(applied.code)));
    require(sole_order(fixture.state).state == ClusterOrderState::reconciling,
            "the fresh cancel response did not advance the order lifecycle");
}

void test_the_guard_refuses_an_incoherent_control_snapshot() {
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
        const auto reject_request = ++sequence;
        if (submit_answer(fixture.state, rejected, reject_request).code !=
            ApplyCode::applied) {
            break;
        }
        if (acknowledge_gateway_results(fixture.state, sequence,
                                        reject_request)
                .code != ApplyCode::applied) {
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
        if (acknowledge_gateway_results(fixture.state, sequence,
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

    VenueObservation fill;
    fill.kind = VenueObservationKind::fill;
    fill.venue_id = VenueId{9};
    fill.venue_session_id = VenueSessionId{1};
    fill.venue_sequence = VenueSequence{++sequence};
    fill.cluster_order_id = order_id;
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
    if (applied.code == ApplyCode::fill_capacity_exceeded ||
        applied.code == ApplyCode::venue_event_capacity_exceeded) {
        require(sole_order(fixture.state).state == ClusterOrderState::conflict,
                "a fill refused for capacity left the order looking healthy, so "
                "a real execution vanished without a trace");
    }

    require(applied.code != ApplyCode::venue_event_capacity_exceeded &&
                applied.code != ApplyCode::fill_capacity_exceeded,
            "repeated legal cancel cycles consumed the venue-event capacity "
            "reserved for this order's own fills, so a real execution can no "
            "longer be accounted");
}

void test_a_rejected_cancel_can_still_be_finally_reconciled() {
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

    const auto applied = submit_answer(
        fixture.state, cancel_final(fixture.state, fixture.effect_id, 0, 7'601), 31);
    require(applied.code == ApplyCode::applied,
            "final cancel evidence was refused after a cancel rejection, so the "
            "reservation is stranded with no path out");
    require(sole_order(fixture.state).state == ClusterOrderState::cancelled,
            "final cancel evidence did not resolve the order");
    require(risk_released(fixture.state),
            "a cancelled order kept its reservation");

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


struct ArenaPeak {
    const char* name;
    std::size_t capacity;
    std::size_t peak;

    [[nodiscard]] std::size_t margin() const noexcept {
        return capacity > peak ? capacity - peak : 0;
    }
};

struct Horizons {
    std::size_t peak_decisions{};
    std::size_t peak_orders{};
    std::size_t peak_reservations{};
    std::size_t peak_effects{};
    std::size_t peak_receipts{};
    std::size_t peak_venue_events{};
    std::size_t peak_caller_cursors{};
    std::size_t peak_source_events{};

    std::uint64_t max_order_age{};        // authorization to last observation
    std::uint64_t max_unresolved_age{};   // the retention horizon that matters
    std::uint64_t max_unsettled_age{};
    std::uint32_t max_send_attempts_used{};
    std::uint32_t max_query_attempts_used{};
    std::uint64_t final_state_version{};
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

struct CommitFaults {
    std::uint16_t result_loss_permille{};
    std::uint16_t leadership_loss_permille{};

    [[nodiscard]] static constexpr CommitFaults clean() noexcept { return {}; }
};

struct RunOutcome {
    std::size_t venue_orders_created{};
    std::set<std::uint64_t> accepted_order_ids;
    std::map<std::uint64_t, std::uint64_t> identity_owner;  // order -> effect

    std::map<std::uint64_t, std::int64_t> venue_reported;
    std::map<std::uint64_t, std::int64_t> cluster_booked;
    std::set<std::uint64_t> seen_executions;
    std::map<ApplyCode, std::size_t> fill_refusals;
    std::map<ApplyCode, std::size_t> event_refusals;

    struct ExecutionTruth {
        std::uint64_t order{};
        Side side{};
        std::int64_t quantity{};
    };
    std::map<std::pair<std::uint64_t, std::uint64_t>, ExecutionTruth>
        actual_executions;

    struct ExecutionRecord : ExecutionTruth {
        std::size_t reported{};
        std::size_t booked{};
        std::int64_t booked_quantity{};
        std::map<ApplyCode, std::size_t> refusals;
    };
    std::map<std::pair<std::uint64_t, std::uint64_t>, ExecutionRecord> executions;
    std::set<std::pair<std::uint64_t, std::uint64_t>> retained_execution_evidence;
    std::int64_t authoritative_position_delta{};

    TradingMetrics metrics{};
    std::size_t observed_unresolved{};

    std::size_t sends{};
    std::size_t ambiguous_declared{};
    std::size_t authorizations{};
    std::size_t cancels{};

    std::map<ClusterOrderState, std::size_t> unresolved_states;
    std::map<std::uint64_t, ClusterOrderState> unresolved_orders_by_id;
    std::map<std::uint64_t, ClusterOrderState> final_order_states;
    std::size_t reconciliation_targets{};
    std::size_t queries{};
    std::size_t answers{};
    std::size_t stale_answers{};
    std::size_t reconciled{};

    std::size_t evidence_owed{};
    std::size_t evidence_proposed{};
    std::size_t evidence_retried{};
    std::size_t evidence_dropped{};
    std::size_t evidence_outstanding{};
    std::map<QueryDisposition, std::size_t> dispositions;
    std::set<std::uint64_t> answered_ahead;   // venue reported fills we never got
    std::set<std::uint64_t> never_answered;   // no trustworthy answer was obtained
    std::size_t reconciliation_refused_inapplicable{};
    std::size_t adjudications_checked{};

    Horizons horizons;
};

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
                    const std::uint64_t cycles,
                    const CommitFaults& commit_faults = CommitFaults::clean()) {
    auto state = ready_state();
    const auto initial_position = state.risk_accounts[0].position_quantity.atoms();
    ApplyScratch scratch;
    SimulatedVenue venue(seed, faults, VenueId{9});
    GatewayDeliveryState delivery;
    (void)begin_gateway_recovery(delivery, state, 0);
    GatewayReconciliationState reconciliation;
    GatewayEvidenceState evidence;
    const GatewayTimeouts timeouts{8};

    std::uint64_t commit_random = seed ^ 0xC0FFEE5EA11ULL;
    const auto commit_roll = [&](const std::uint16_t permille) {
        commit_random += 0x9e3779b97f4a7c15ULL;
        auto value = commit_random;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        value ^= value >> 31;
        return permille != 0 && (value % 1000) < permille;
    };

    RunOutcome outcome;
    const auto submit = [&](const Command& command) {
        const auto result = apply(state, command, scratch);
        if (is_capacity_refusal(result.code)) {
            ++outcome.horizons.capacity_refusals[result.code];
        }
        return result;
    };

    const auto booked_quantity_of =
        [&](const ClusterOrderId id) -> std::optional<std::int64_t> {
        for (std::size_t index = 0; index < state.order_count; ++index) {
            const auto& order = state.orders[index];
            if (order.cluster_order_id != id) continue;
            if (order.reservation_index >= state.reservation_count) {
                return std::nullopt;
            }
            return state.reservations[order.reservation_index]
                .filled_quantity.atoms();
        }
        return std::nullopt;
    };
    const auto order_state_of =
        [&](const ClusterOrderId id) -> std::optional<ClusterOrderState> {
        for (std::size_t index = 0; index < state.order_count; ++index) {
            if (state.orders[index].cluster_order_id == id) {
                return state.orders[index].state;
            }
        }
        return std::nullopt;
    };

    std::uint64_t counter = 0;
    std::uint64_t evidence_sequence = 0;
    std::uint64_t acknowledged_evidence_sequence = 0;
    std::uint64_t published_evidence_ack = 0;
    std::set<std::uint64_t> observed_evidence_results;
    std::uint64_t sequence = 0;
    std::uint64_t decision_id = 100;
    sim::Tick now = 0;

    const auto next_identity = [&]() {
        ++counter;
        return CommandIdentity{CallerId{1}, RequestSequence{counter},
                               request_digest(counter)};
    };

    struct InFlightQuery {
        ReconciliationTarget target{};
        sim::Tick due{};
        VenueQueryView view{};
    };
    std::vector<InFlightQuery> in_flight;

    const auto owe = [&](const VenueObservation& observation) {
        ++evidence_sequence;
        const CommandIdentity identity{CallerId{2},
                                       RequestSequence{evidence_sequence},
                                       observation_fingerprint(observation)};
        if (!record_evidence_obligation(evidence, identity, observation,
                                        observation.effect_id, now)) {
            ++outcome.evidence_dropped;
            return;
        }
        ++outcome.evidence_owed;
    };

    const auto pump = [&]() {
        const auto plan = plan_evidence(evidence, timeouts, now);
        for (std::size_t index = 0; index < plan.proposal_count; ++index) {
            const auto identity = plan.to_propose[index];
            const auto* obligation = find_obligation(evidence, identity);
            if (obligation == nullptr) continue;
            const auto observation = obligation->observation;
            const bool retry = obligation->attempts != 0;
            require(record_proposal(evidence, identity, now),
                    "record_proposal did not find its own obligation");
            ++outcome.evidence_proposed;
            if (retry) ++outcome.evidence_retried;

            if (commit_roll(commit_faults.leadership_loss_permille)) continue;

            const bool is_fill = observation.kind == VenueObservationKind::fill;
            const Command command{identity,
                                  is_fill ? CommandBody{ObserveFill{observation}}
                                          : CommandBody{ObserveOrderEvent{observation}}};
            const bool adjudication =
                observation.kind == VenueObservationKind::order_reconciled ||
                observation.kind == VenueObservationKind::cancel_reconciled;
            const auto booked_before =
                adjudication ? booked_quantity_of(observation.cluster_order_id)
                             : std::nullopt;
            const auto state_before =
                adjudication ? order_state_of(observation.cluster_order_id)
                             : std::nullopt;
            const auto execution_key =
                std::pair<std::uint64_t, std::uint64_t>{
                    observation.venue_id.value(), observation.execution_id.value()};

            const auto position_before =
                state.risk_accounts[0].position_quantity.atoms();
            const auto applied = submit(command);
            const auto position_after =
                state.risk_accounts[0].position_quantity.atoms();
            if (is_fill && position_after != position_before) {
                const auto economic_quantity =
                    observation.side == Side::buy
                        ? position_after - position_before
                        : position_before - position_after;
                auto& record = outcome.executions[execution_key];
                ++record.booked;
                record.booked_quantity += economic_quantity;
                outcome.cluster_booked[observation.cluster_order_id.value()] +=
                    economic_quantity;
            }

            if (commit_roll(commit_faults.result_loss_permille)) continue;

            venue.acknowledge_observation(observation);
            require(discharge_evidence(evidence, identity),
                    "a committed result did not discharge its obligation");
            observed_evidence_results.insert(identity.request_sequence.value());
            while (observed_evidence_results.contains(
                acknowledged_evidence_sequence + 1)) {
                ++acknowledged_evidence_sequence;
            }
            if (acknowledged_evidence_sequence > published_evidence_ack) {
                const auto ack_sequence = ++evidence_sequence;
                const Command ack{
                    {CallerId{2}, RequestSequence{ack_sequence},
                     request_digest(ack_sequence)},
                    AcknowledgeGatewayResults{
                        RequestSequence{acknowledged_evidence_sequence}}};
                require(submit(ack).code == ApplyCode::applied,
                        "the gateway could not publish its committed-result prefix");
                published_evidence_ack = acknowledged_evidence_sequence;
                observed_evidence_results.insert(ack_sequence);
                while (observed_evidence_results.contains(
                    acknowledged_evidence_sequence + 1)) {
                    ++acknowledged_evidence_sequence;
                }
            }

            const auto order = observation.cluster_order_id.value();
            const bool reconciliation_answer =
                observation.kind == VenueObservationKind::order_reconciled ||
                observation.kind == VenueObservationKind::cancel_reconciled;
            if (applied.code == ApplyCode::applied) {
                if (reconciliation_answer) ++outcome.reconciled;
            } else if (is_fill) {
                ++outcome.fill_refusals[applied.code];
                ++outcome.executions[execution_key].refusals[applied.code];
            } else if (reconciliation_answer) {
            } else {
                ++outcome.event_refusals[applied.code];
            }

            if (!adjudication || !booked_before.has_value()) continue;
            ++outcome.adjudications_checked;
            const auto reported = observation.quantity.atoms();
            const auto after = order_state_of(observation.cluster_order_id);
            const bool conflicted_now =
                after.has_value() && *after == ClusterOrderState::conflict;
            const bool conflicted_before =
                state_before.has_value() &&
                *state_before == ClusterOrderState::conflict;

            if (applied.code == ApplyCode::applied) {
                if (reported < *booked_before) {
                    require(conflicted_now,
                            "an answer below what the cluster had booked did not "
                            "conflict the order");
                } else if (reported == *booked_before) {
                    require(conflicted_now == conflicted_before,
                            "an answer that agreed with the cluster changed "
                            "whether the order was conflicted");
                }
            } else if (applied.code ==
                       ApplyCode::venue_observation_not_applicable) {
                if (reported > *booked_before) {
                    outcome.answered_ahead.insert(order);
                } else {
                    ++outcome.reconciliation_refused_inapplicable;
                }
            }
        }
    };

    std::uint64_t cycle = 0;
    std::map<std::uint64_t, bool> last_seen;
    const auto tick = [&](const bool propose, const bool issue_queries) {
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

        const auto control = control_of(state);
        const auto plan = plan_delivery(state, delivery, evidence, control, capable_venue(),
                                        SendQuotaView{4}, timeouts, now);

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
            unknown.venue_sequence = VenueSequence{900'000 + now * 64 + index};
            unknown.cluster_order_id = order;
            unknown.effect_id = effect_id;
            unknown.evidence_digest = digest(0x7E);
            (void)record_response(delivery, effect_id);
            owe(unknown);
            ++outcome.ambiguous_declared;
        }
        pump();

        for (std::size_t index = 0; index < plan.send_count; ++index) {
            const auto effect_id = plan.to_send[index];
            const auto decision = evaluate_pre_send(state, effect_id, control,
                                                    capable_venue(), SendQuotaView{4});
            require(decision.permitted(),
                    "the planner scheduled an effect the guard refuses");
            const auto order = decision.effect->cluster_order_id.value();
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
                (void)record_unsent(delivery, effect_id);
            }
        }

        now += 3;
        for (const auto& observation : venue.poll(now)) {
            const bool is_fill = observation.kind == VenueObservationKind::fill;
            const auto order = observation.cluster_order_id.value();
            if (is_fill) {
                auto& record = outcome.executions[{observation.venue_id.value(),
                                                   observation.execution_id.value()}];
                if (record.reported == 0) {
                    record.order = order;
                    record.side = observation.side;
                    record.quantity = observation.quantity.atoms();
                    outcome.seen_executions.insert(observation.execution_id.value());
                    outcome.venue_reported[order] += observation.quantity.atoms();
                } else {
                    require(record.order == order &&
                                record.side == observation.side &&
                                record.quantity == observation.quantity.atoms(),
                            "the venue reported one execution two different ways");
                }
                ++record.reported;
            }
            (void)record_response(delivery, observation.effect_id);
            (void)order;
            owe(observation);
        }
        pump();
        std::vector<InFlightQuery> still_flying;
        still_flying.reserve(in_flight.size());
        for (std::size_t index = 0; index < in_flight.size(); ++index) {
            const auto& pending = in_flight[index];
            if (pending.due > now) {
                still_flying.push_back(pending);
                continue;
            }
            const auto& target = pending.target;
            const auto& view = pending.view;
            require(record_answer(reconciliation, target.cluster_order_id),
                    "record_answer did not find the outstanding query");
            ++outcome.answers;

            const auto* attempt =
                find_reconciliation(reconciliation, target.cluster_order_id);
            const auto evidence_now =
                accepted_evidence_watermark(state, target.cluster_order_id);
            if (attempt != nullptr && evidence_now != attempt->evidence_at_query) {
                ++outcome.stale_answers;
            }
            const auto disposition =
                classify_query(view, attempt, evidence_now, timeouts);
            ++outcome.dispositions[disposition];
            if (disposition != QueryDisposition::submit) {
                outcome.never_answered.insert(target.cluster_order_id.value());
                continue;
            }
            outcome.never_answered.erase(target.cluster_order_id.value());

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
            if (cancel_took_effect && answer.venue_order_id.value() == 0) continue;
            owe(answer);
        }
        in_flight = std::move(still_flying);
        pump();

        if (issue_queries) {
            const auto recon = plan_reconciliation(state, reconciliation, delivery,
                                                   control_of(state), capable_venue(),
                                                   timeouts, now);
            for (std::size_t index = 0; index < recon.query_count; ++index) {
                const auto& target = recon.to_query[index];
                require(record_query(
                            reconciliation, target.cluster_order_id, now,
                            accepted_evidence_watermark(state,
                                                        target.cluster_order_id)),
                        "the reconciliation arena overflowed");
                const auto raw = venue.query(target.cluster_order_id, now);
                ++outcome.queries;

                VenueQueryView view;
                view.venue_session_id = venue.session();
                switch (raw.outcome) {
                    case QueryOutcome::known:
                        view.status = VenueQueryStatus::known;
                        view.venue_order_id = raw.venue_order_id;
                        view.filled_quantity = raw.filled_quantity;
                        view.remaining_quantity = raw.remaining_quantity;
                        view.cancelled = raw.cancelled;
                        view.window_complete = raw.window_complete;
                        view.ordered_after_relevant_sends =
                            raw.ordered_after_relevant_sends;
                        break;
                    case QueryOutcome::not_found:
                        view.status = VenueQueryStatus::not_found;
                        view.window_complete = true;
                        view.ordered_after_relevant_sends =
                            raw.ordered_after_relevant_sends;
                        break;
                    case QueryOutcome::unavailable:
                        view.status = VenueQueryStatus::unavailable;
                        break;
                }
                in_flight.push_back({target, now + 1, view});
            }
        }

        (void)reclaim_delivery_attempts(delivery, state);
        (void)reclaim_reconciliation_attempts(reconciliation, state);

        outcome.horizons.observe(collect_metrics(state));

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
            if (was_unresolved) {
                ++outcome.horizons.vanished_while_unresolved;
            }
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

    for (cycle = 0; cycle < cycles; ++cycle) tick(true, true);
    constexpr std::uint64_t kDrainPasses = 13;
    for (std::uint64_t pass = 0; pass < kDrainPasses; ++pass) {
        tick(false, pass + 1 < kDrainPasses);
    }

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
    for (std::size_t index = 0; index < state.order_count; ++index) {
        outcome.final_order_states[state.orders[index].cluster_order_id.value()] =
            state.orders[index].state;
    }
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        const auto& result = state.receipts[index].result;
        if (result.detail_kind == ApplyResultDetailKind::fill) {
            outcome.retained_execution_evidence.insert(
                {result.fill_venue_id().value(), result.execution_id().value()});
        }
    }
    for (const auto& execution : venue.executions()) {
        const auto key = std::pair<std::uint64_t, std::uint64_t>{
            execution.venue_id.value(), execution.execution_id.value()};
        const RunOutcome::ExecutionTruth truth{
            execution.cluster_order_id.value(), execution.side,
            execution.quantity.atoms()};
        const auto [found, inserted] = outcome.actual_executions.emplace(key, truth);
        require(inserted ||
                    (found->second.order == truth.order &&
                     found->second.side == truth.side &&
                     found->second.quantity == truth.quantity),
                "the venue reused one execution identity for different economics");
    }
    outcome.evidence_outstanding = evidence.obligation_count;
    outcome.authoritative_position_delta =
        state.risk_accounts[0].position_quantity.atoms() - initial_position;
    outcome.metrics = collect_metrics(state);
    const GatewayReconciliationState fresh;
    outcome.reconciliation_targets =
        plan_reconciliation(state, fresh, delivery, control_of(state),
                            capable_venue(), timeouts, now)
            .query_count;
    return outcome;
}

void check_gates(const RunOutcome& outcome, const std::string& where) {
    require(outcome.evidence_dropped == 0,
            "the gateway dropped evidence it owed the cluster" + where);

    require(outcome.accepted_order_ids.size() == outcome.venue_orders_created,
            "the venue created more orders than there were distinct intents" + where);

    for (const auto& [key, truth] : outcome.actual_executions) {
        const auto delivered = outcome.executions.find(key);
        require(delivered != outcome.executions.end(),
                "a venue execution never entered the replayable delivery ledger" +
                    where);
        require(delivered->second.order == truth.order &&
                    delivered->second.side == truth.side &&
                    delivered->second.quantity == truth.quantity,
                "delivered execution bytes disagree with venue economic truth" +
                    where);
    }
    for (const auto& [key, delivered] : outcome.executions) {
        const auto truth = outcome.actual_executions.find(key);
        require(truth != outcome.actual_executions.end(),
                "the delivery ledger invented an execution absent at the venue" +
                    where);
        (void)delivered;
    }

    for (const auto& [key, record] : outcome.executions) {
        const std::string which = " (execution " + std::to_string(key.second) +
                                  " on order " + std::to_string(record.order) +
                                  ")" + where;

        require(record.booked <= 1,
                "one venue execution was booked more than once" + which);

        std::size_t accounted = record.booked;
        for (const auto& [code, count] : record.refusals) {
            const bool proves_repeat =
                code == ApplyCode::already_applied ||
                code == ApplyCode::fill_identity_conflict ||
                code == ApplyCode::venue_event_identity_conflict;
            if (record.booked == 1 && proves_repeat) {
                accounted += count;
                continue;
            }
            accounted += count;
        }
        require(accounted == record.reported,
                "a venue execution was neither booked nor refused - it was "
                "simply never accounted for" + which);

        if (record.booked == 1) {
            require(record.booked_quantity == record.quantity,
                    "an execution was booked for a quantity the venue never "
                    "reported" + which);
            for (const auto& [code, count] : record.refusals) {
                (void)count;
                require(code == ApplyCode::already_applied ||
                            code == ApplyCode::fill_identity_conflict ||
                            code == ApplyCode::venue_event_identity_conflict,
                        "a repeat of a booked execution was refused for a reason "
                        "that does not prove it was that same execution: code " +
                            std::to_string(static_cast<int>(code)) + which);
            }
            continue;
        }

        const auto final_state = outcome.final_order_states.find(record.order);
        const bool conflicted =
            final_state != outcome.final_order_states.end() &&
            final_state->second == ClusterOrderState::conflict;
        const bool evidence_retained =
            outcome.retained_execution_evidence.count(key) != 0;
        require(conflicted && evidence_retained,
                "a venue execution was never booked and the order that owns it "
                "does not remain conflicted with its exact execution evidence "
                "retained" + which);
    }

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
    {
        std::map<std::uint64_t, std::int64_t> ledger_reported;
        std::map<std::uint64_t, std::int64_t> ledger_booked;
        std::int64_t signed_booked = 0;
        for (const auto& [key, record] : outcome.executions) {
            (void)key;
            ledger_reported[record.order] += record.quantity;
            ledger_booked[record.order] += record.booked_quantity;
            signed_booked += record.side == Side::buy ? record.booked_quantity
                                                       : -record.booked_quantity;
        }
        require(ledger_reported == outcome.venue_reported,
                "the execution ledger and the per-order totals disagree about "
                "what the venue reported" + where);
        std::map<std::uint64_t, std::int64_t> booked_nonzero;
        for (const auto& [order, quantity] : ledger_booked) {
            if (quantity != 0) booked_nonzero[order] = quantity;
        }
        require(booked_nonzero == outcome.cluster_booked,
                "the execution ledger and the per-order totals disagree about "
                "what the cluster booked" + where);
        require(signed_booked == outcome.authoritative_position_delta,
                "fill receipts and authoritative position movement disagree" +
                    where);
    }
    if (outcome.fill_refusals.count(ApplyCode::fill_quantity_conflict) != 0) {
        require(outcome.metrics
                        .orders_by_state[static_cast<std::size_t>(
                            ClusterOrderState::conflict)] > 0,
                "a fill was refused as unbookable without leaving a conflicted "
                "order behind, so it vanished silently" + where);
    }
    for (const auto& [code, count] : outcome.event_refusals) {
        require(refusal_is_lossless(code),
                "venue evidence was refused for a reason that loses it: code " +
                    std::to_string(static_cast<int>(code)) + " x" +
                    std::to_string(count) + where);
    }

    require(outcome.metrics.unresolved_orders == outcome.observed_unresolved,
            "the projection disagreed with state about unresolved orders" + where);
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
    for (const auto& [order_state, count] : outcome.unresolved_states) {
        (void)count;
        const bool queryable = order_state == ClusterOrderState::submission_unknown ||
                               order_state == ClusterOrderState::reconciling ||
                               order_state == ClusterOrderState::dispatch_authorized;
        const bool adjudicated = order_state == ClusterOrderState::conflict ||
                                 order_state == ClusterOrderState::live ||
                                 order_state == ClusterOrderState::partially_filled ||
                                 order_state == ClusterOrderState::cancel_pending;
        require(queryable || adjudicated,
                "an order holds exposure in a state nothing can act on: " +
                    std::to_string(static_cast<int>(order_state)) + where);
    }
    for (const auto& order : outcome.unresolved_orders_by_id) {
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

void test_a_resolved_sweep_cannot_excuse_an_unbooked_execution() {
    RunOutcome outcome;
    RunOutcome::ExecutionRecord record;
    record.order = 42;
    record.side = Side::buy;
    record.quantity = 3;
    record.reported = 1;
    record.refusals[ApplyCode::order_not_found] = 1;
    outcome.executions[{9, 7001}] = record;
    outcome.actual_executions[{9, 7001}] = {42, Side::buy, 3};
    outcome.venue_reported[42] = 3;

    bool rejected = false;
    try {
        check_gates(outcome, " (oracle counterexample)");
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected,
            "a resolved-and-swept order excused an execution that was never "
            "booked or durably retained");

    outcome.final_order_states[42] = ClusterOrderState::conflict;
    rejected = false;
    try {
        check_gates(outcome, " (missing exact evidence counterexample)");
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected,
            "a generic order conflict excused an execution whose exact evidence "
            "was not retained");

    outcome.retained_execution_evidence.insert({9, 7001});
    check_gates(outcome, " (durably retained conflict control)");
}

void test_execution_oracle_rejects_a_receipt_without_an_economic_mutation() {
    RunOutcome outcome;
    RunOutcome::ExecutionRecord record;
    record.order = 42;
    record.side = Side::buy;
    record.quantity = 3;
    record.reported = 1;
    record.booked = 1;
    record.booked_quantity = 3;
    outcome.executions[{9, 7002}] = record;
    outcome.actual_executions[{9, 7002}] = {42, Side::buy, 3};
    outcome.venue_reported[42] = 3;
    outcome.cluster_booked[42] = 3;

    bool rejected = false;
    try {
        check_gates(outcome, " (receipt-only booking counterexample)");
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected,
            "the oracle trusted a successful fill receipt without observing the "
            "corresponding position mutation");
}

void test_execution_oracle_rejects_a_venue_execution_never_delivered() {
    RunOutcome outcome;
    outcome.actual_executions[{9, 7003}] = {42, Side::buy, 3};

    bool rejected = false;
    try {
        check_gates(outcome, " (undelivered venue execution counterexample)");
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected,
            "the oracle ignored an execution present at the venue but absent "
            "from its delivery ledger");
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
    faults.partial_window_permille = 200;

    std::size_t total_sends = 0;
    std::size_t total_ambiguous = 0;
    std::size_t total_orders = 0;
    std::size_t total_duplicate_fills = 0;
    std::size_t total_unresolved = 0;
    std::size_t total_queries = 0;
    std::size_t total_reconciled = 0;
    std::size_t total_unavailable = 0;
    std::size_t total_incomplete = 0;
    std::size_t total_stale = 0;
    std::size_t total_cancels = 0;
    std::size_t total_adjudications = 0;
    std::size_t total_conflicts = 0;
    std::size_t total_repeated_executions = 0;
    std::size_t total_inapplicable = 0;
    for (std::uint64_t seed = 1; seed <= 32; ++seed) {
        const auto outcome = run_seed(seed, faults, 12);
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
            std::size_t unbooked = 0;
            std::size_t repeated = 0;
            for (const auto& [key, record] : outcome.executions) {
                (void)key;
                if (record.booked == 0) ++unbooked;
                if (record.reported > 1) ++repeated;
            }
            std::cout << "adjudications " << outcome.adjudications_checked
                      << " inapplicable "
                      << outcome.reconciliation_refused_inapplicable << " ";
            std::cout << "executions " << outcome.executions.size() << " unbooked "
                      << unbooked << " repeated " << repeated << " ";
            std::cout << "queries " << outcome.queries << " answers "
                      << outcome.answers << " stale " << outcome.stale_answers
                      << " reconciled " << outcome.reconciled << " recon-targets "
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
        if (outcome.dispositions.count(QueryDisposition::incomplete) != 0) {
            total_incomplete += outcome.dispositions.at(QueryDisposition::incomplete);
        }
        total_stale += outcome.stale_answers;
        total_adjudications += outcome.adjudications_checked;
        total_conflicts += outcome.metrics.orders_by_state[static_cast<std::size_t>(
            ClusterOrderState::conflict)];
        total_inapplicable += outcome.reconciliation_refused_inapplicable;
        for (const auto& [key, record] : outcome.executions) {
            (void)key;
            if (record.reported > 1) ++total_repeated_executions;
        }
        total_sends += outcome.sends;
        total_ambiguous += outcome.ambiguous_declared;
        total_orders += outcome.venue_orders_created;
    }
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
    require(total_cancels > 0,
            "the matrix never cancelled anything, so the cancel lifecycle was "
            "never exercised end to end");
    require(total_unavailable > 0,
            "the matrix never lost a query, so the classifier's silence branch "
            "was never exercised");
    require(total_incomplete > 0,
            "the matrix never refused an answer as incomplete, so neither the "
            "partial-window branch nor the ordering barrier was ever exercised");
    require(total_stale > 0,
            "no answer ever arrived after the cluster's evidence had moved, so "
            "the ordering barrier was never actually crossed");
    require(total_adjudications > 0,
            "the independent adjudication oracle never had a reconciliation "
            "answer to check, so it proves nothing");
    require(total_repeated_executions > 0,
            "the venue never delivered one execution twice, so the per-execution "
            "ledger's duplicate branch was never exercised");
    (void)total_inapplicable;
    require(total_conflicts > 0,
            "the matrix never produced a reconciliation conflict, so the branch "
            "that retains unresolvable disagreement was never exercised");
}

void test_the_evidence_seam_holds_under_commit_faults() {
    FaultPolicy faults;
    faults.delay_permille = 120;
    faults.loss_permille = 80;
    faults.duplication_permille = 120;
    faults.reorder_permille = 120;
    faults.partial_fill_permille = 180;
    faults.disconnect_permille = 60;
    faults.rate_limit_permille = 90;
    faults.inconsistent_query_permille = 220;
    faults.partial_window_permille = 200;

    CommitFaults commit;
    commit.result_loss_permille = 200;
    commit.leadership_loss_permille = 150;

    std::size_t total_owed = 0;
    std::size_t total_proposed = 0;
    std::size_t total_retried = 0;
    std::size_t total_outstanding = 0;
    for (std::uint64_t seed = 1; seed <= 24; ++seed) {
        const auto outcome = run_seed(seed, faults, 12, commit);
        check_gates(outcome, " (commit-fault seed " + std::to_string(seed) + ")");
        total_owed += outcome.evidence_owed;
        total_proposed += outcome.evidence_proposed;
        total_retried += outcome.evidence_retried;
        total_outstanding += outcome.evidence_outstanding;
    }

    require(total_owed > 0, "the matrix never owed the cluster any evidence");
    require(total_retried > 0,
            "no evidence was ever re-proposed, so neither a lost result nor a "
            "leadership change was ever survived");
    require(total_proposed > total_owed,
            "every obligation committed first time, so the retry path is untested");
    require(total_outstanding == 0,
            "the run ended still owing the cluster evidence");
}

void test_the_matrix_is_reproducible() {
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
    Horizons worst;
    for (std::uint64_t seed = 1; seed <= 48; ++seed) {
        const auto outcome = run_seed(seed, nominal_faults(), 40);
        check_gates(outcome, " (horizon seed " + std::to_string(seed) + ")");
        worst.merge(outcome.horizons);
    }
    report("nominal faults, 48 seeds x 40 cycles", worst);

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


constexpr std::size_t kSlots = kMaxExecutableActionTemplates;

PolicyArtifact multi_slot_policy(const std::uint64_t version = 1) {
    auto value = policy();
    value.policy_version = PolicyVersion{version};
    value.artifact_digest =
        digest(static_cast<std::uint8_t>(0x80 + version));
    value.rule_count = static_cast<std::uint16_t>(kSlots);
    for (std::size_t index = 0; index < kSlots; ++index) {
        value.rules[index] = {
            0, PredicateOperator::greater_than,
            FeatureMicros::from_atoms(static_cast<std::int64_t>(kSlots - index)),
            Directive::place_limit};
    }
    return value;
}

ExecutablePolicyArtifact multi_slot_executable_policy(
    const FeatureSchemaArtifact& schema, const std::uint64_t version = 1,
    const std::uint16_t first_logical_slot = 1) {
    auto value = executable_policy(schema);
    value.policy_version = PolicyVersion{version};
    value.policy_artifact_digest = multi_slot_policy(version).artifact_digest;
    value.template_count = static_cast<std::uint16_t>(kSlots);
    for (std::size_t index = 0; index < kSlots; ++index) {
        value.templates[index] = action_template();
        value.templates[index].order_slot =
            LogicalOrderSlot{static_cast<std::uint16_t>(first_logical_slot + index)};
        value.rule_template_slots[index] =
            ActionTemplateSlot{static_cast<std::uint8_t>(index + 1)};
    }
    value.rule_binding_count = static_cast<std::uint16_t>(kSlots);
    value.artifact_digest = compute_artifact_digest(value).digest;
    return value;
}

TradingState multi_slot_state() {
    auto state = ready_state();
    state.policies[0] = {multi_slot_policy(), PolicyLifecycle::active};
    const auto schema = feature_schema();
    state.active_executable_policy.executable_policy =
        multi_slot_executable_policy(schema);
    state.risk_accounts[0].max_reserved_quantity = QuantityLots::from_atoms(256);
    state.risk_accounts[0].max_reserved_exposure =
        MoneyMicros::from_atoms(100'000'000);
    return state;
}

struct SlotAuthorization {
    EffectId effect_id{};
    ApplyCode last_code{ApplyCode::applied};
};

SlotAuthorization authorize_on_slot(TradingState& state, std::uint64_t& sequence,
                                    std::uint64_t& request, std::uint64_t& decision,
                                    const std::int64_t feature,
                                    const std::uint64_t policy_version = 1) {
    ApplyScratch scratch;
    ++sequence;
    const auto evidence = digest(static_cast<std::uint8_t>(0x40 + sequence % 100));
    const Command record{{CallerId{1}, RequestSequence{++request},
                          request_digest(request)},
                         RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                           SourceSequence{sequence}, evidence}};
    auto code = apply(state, record, scratch).code;
    if (code != ApplyCode::applied) return {EffectId{}, code};

    auto next = frame(sequence, evidence, state);
    next.policy_version = PolicyVersion{policy_version};
    next.features[0] = FeatureMicros::from_atoms(feature);
    const DecisionId id{decision++};
    const Command evaluate{{CallerId{1}, RequestSequence{++request},
                            request_digest(request)},
                           EvaluateDecisionFrame{id, EvaluationMode::authoritative,
                                                 next}};
    code = apply(state, evaluate, scratch).code;
    if (code != ApplyCode::applied) return {EffectId{}, code};

    const Command authorize{{CallerId{1}, RequestSequence{++request},
                             request_digest(request)},
                            AuthorizeDecision{id}};
    const auto authorized = apply(state, authorize, scratch);
    if (authorized.code != ApplyCode::applied) {
        return {EffectId{}, authorized.code};
    }
    return {authorized.effect_id(), ApplyCode::applied};
}

void rotate_multi_slot_policy(TradingState& state, std::uint64_t& request,
                              const std::uint64_t version,
                              const std::uint16_t first_logical_slot) {
    ApplyScratch scratch;
    const auto submit = [&](CommandBody body) {
        const auto identity = CommandIdentity{
            CallerId{1}, RequestSequence{++request}, request_digest(request)};
        return apply(state, Command{identity, std::move(body)}, scratch).code;
    };

    const auto next_policy = multi_slot_policy(version);
    require(submit(PublishPolicy{next_policy}) == ApplyCode::applied,
            "the rotated policy could not be published");
    require(submit(ValidatePolicy{next_policy.policy_id,
                                  next_policy.policy_version}) ==
                ApplyCode::applied,
            "the rotated policy could not be validated");

    const auto schema = feature_schema();
    const auto executable = multi_slot_executable_policy(
        schema, version, first_logical_slot);
    const auto instrument = state.active_executable_policy.instrument;
    const auto venue = state.active_executable_policy.venue;
    require(submit(StageExecutablePolicy{schema, executable, instrument, venue}) ==
                ApplyCode::applied,
            "the rotated executable policy could not be staged");
    require(submit(ActivateExecutablePolicy{
                       next_policy.policy_id, next_policy.policy_version,
                       executable.artifact_digest}) == ApplyCode::applied,
            "the rotated executable policy could not be activated");
}

void conflict_the_order(TradingState& state, const ClusterOrderId order,
                        const EffectId submit_effect, std::uint64_t& request,
                        std::uint64_t& venue_sequence) {
    const auto observe = [&](const VenueObservation& observation) {
        ApplyScratch scratch;
        const bool is_fill = observation.kind == VenueObservationKind::fill;
        const Command command{{CallerId{1}, RequestSequence{++request},
                               observation_fingerprint(observation)},
                              is_fill ? CommandBody{ObserveFill{observation}}
                                      : CommandBody{ObserveOrderEvent{observation}}};
        return apply(state, command, scratch).code;
    };
    const auto base = [&](const VenueObservationKind kind) {
        VenueObservation value;
        value.kind = kind;
        value.venue_id = VenueId{9};
        value.venue_session_id = VenueSessionId{1};
        value.venue_sequence = VenueSequence{++venue_sequence};
        value.cluster_order_id = order;
        value.effect_id = submit_effect;
        value.venue_order_id = VenueOrderId{5'000 + order.value()};
        value.evidence_digest = digest(0x71);
        return value;
    };

    require(observe(base(VenueObservationKind::order_acknowledged)) ==
                ApplyCode::applied,
            "the slot fixture could not acknowledge its order");

    auto fill = base(VenueObservationKind::fill);
    fill.execution_id = ExecutionId{venue_sequence};
    fill.side = Side::buy;
    fill.price = PriceTicks::from_atoms(11);
    fill.quantity = QuantityLots::from_atoms(1);
    fill.evidence_digest = digest(0x78);
    require(observe(fill) == ApplyCode::applied,
            "the slot fixture could not book its partial fill");

    ApplyScratch scratch;
    const Command cancel{{CallerId{1}, RequestSequence{++request}, digest(0x7B)},
                         RequestCancel{order}};
    const auto requested = apply(state, cancel, scratch);
    require(requested.code == ApplyCode::applied,
            "the slot fixture could not authorize its cancel");

    auto unknown = base(VenueObservationKind::effect_unknown);
    unknown.effect_id = requested.effect_id();
    unknown.evidence_digest = digest(0x7C);
    require(observe(unknown) == ApplyCode::applied,
            "the slot fixture could not lose its cancel");

    auto answer = base(VenueObservationKind::order_reconciled);
    answer.effect_id = requested.effect_id();
    answer.quantity = QuantityLots{};  // below the one lot the cluster booked
    answer.evidence_digest = digest(0x6C);
    require(observe(answer) == ApplyCode::applied,
            "the slot fixture could not conflict its order");
}

void test_policy_rotation_reaches_the_reserved_outbox_conflict_bound() {
    auto state = multi_slot_state();
    std::uint64_t sequence = 0;
    std::uint64_t request = 0;
    std::uint64_t decision = 500;
    std::uint64_t venue_sequence = 6'000;

    std::size_t conflicted = 0;
    for (std::size_t index = 0; index < kSlots; ++index) {
        const auto feature = static_cast<std::int64_t>(kSlots - index + 1);
        const auto authorized =
            authorize_on_slot(state, sequence, request, decision, feature);
        require(authorized.effect_id.value() != 0,
                "slot " + std::to_string(index + 1) +
                    " could not be authorized, code " +
                    std::to_string(static_cast<int>(authorized.last_code)));
        ClusterOrderId order{};
        for (std::size_t slot = 0; slot < state.outbox_effect_count; ++slot) {
            if (state.outbox_effects[slot].effect_id == authorized.effect_id) {
                order = state.outbox_effects[slot].cluster_order_id;
            }
        }
        require(order.value() != 0, "an authorized effect named no order");
        conflict_the_order(state, order, authorized.effect_id, request,
                           venue_sequence);
        ++conflicted;
    }

    const auto metrics = collect_metrics(state);
    require(metrics.orders_by_state[static_cast<std::size_t>(
                ClusterOrderState::conflict)] == kSlots,
            "not every slot reached a concurrent conflict");
    require(conflicted == kSlots, "the loop did not conflict every slot");

    rotate_multi_slot_policy(state, request, 2,
                             static_cast<std::uint16_t>(kSlots + 1));

    const auto blocked = authorize_on_slot(state, sequence, request, decision,
                                           static_cast<std::int64_t>(kSlots + 1),
                                           2);
    require(blocked.effect_id.value() == 0,
            "a ninth unresolved order exceeded the reserved outbox bound");
    require(blocked.last_code == ApplyCode::outbox_capacity_exceeded,
            "the first post-rotation refusal was not outbox capacity, it was code " +
                std::to_string(static_cast<int>(blocked.last_code)));

    require(state.order_count == kSlots &&
                state.outbox_effect_count == kMaxOutboxEffects,
            "the fixture did not consume two retained effects per conflict");
    require(state.decision_count == kSlots + 1 &&
                state.decision_cold[state.decision_count - 1]
                        .proposal.order_slot == LogicalOrderSlot{kSlots + 1},
            "the rejected authorization did not name a distinct rotated slot");
    require(kSlots * 2 == kMaxOutboxEffects,
            "the measured eight-conflict bound no longer matches the reviewed "
            "submit-plus-cancel outbox budget");
}

void test_conflicts_saturate_the_cluster_and_it_fails_closed() {
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

    require(worst.vanished_while_unresolved == 0,
            "an order holding unresolved exposure was reclaimed under saturation");
    for (const auto& arena : worst.arenas()) {
        require(arena.peak <= arena.capacity,
                std::string("an arena exceeded its capacity, which means a bound "
                            "was not enforced: ") + arena.name);
    }

    require(worst.peak_conflicts > 0,
            "the saturation run produced no conflicts, so it measured nothing");
}

void test_the_bounded_retry_and_query_budgets_are_actually_reached() {
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
        test_a_resting_order_is_confirmed_rather_than_assumed();
    test_one_outstanding_query_at_a_time();
        test_the_query_interval_is_respected();
        test_reconciliation_queries_are_bounded();
        test_reconciliation_is_fenced_by_generation_but_not_by_halt();
        test_only_a_trustworthy_answer_may_be_submitted();
        test_an_answer_that_predates_new_evidence_is_not_comparable();
        test_the_evidence_watermark_is_monotonic_per_order();
        test_a_delivery_exhausted_order_is_reconciled_not_abandoned();
        test_the_reconciliation_planner_reads_no_clock_and_mutates_nothing();
        test_a_gateway_crash_cannot_produce_a_second_order();
        test_an_incomplete_effect_does_not_deadlock_restart_recovery();
        test_an_effect_becoming_incomplete_during_recovery_clears_the_barrier();
        test_recovery_blocks_new_risk_without_blocking_a_cancel();
        test_a_gateway_that_never_scanned_sends_nothing();
        test_a_venue_answer_does_not_discharge_the_commit_obligation();
        test_a_lost_commit_result_moves_the_position_exactly_once();
        test_simulator_evidence_survives_every_pre_ack_crash_cut();
        test_leadership_loss_before_commit_leaves_the_evidence_owned();
        test_a_saturated_evidence_queue_stops_producing_more_evidence();
        test_a_cancel_is_deliverable_in_every_strategy_mode();
        test_a_conflicted_order_keeps_accounting_valid_executions();
        test_gateway_acknowledgement_gates_venue_receipt_reclamation();
        test_conflicting_fill_keeps_exact_evidence_after_acknowledged_sweep();
        test_unacknowledged_full_fill_result_survives_group_and_receipt_sweeps();
        test_gateway_ack_receipt_cannot_expire_an_uncommitted_sequence_gap();
        test_a_fresh_fill_cannot_be_expired_by_gateway_authored_control_sequences();
        test_a_fresh_cancel_response_cannot_be_expired_by_gateway_authored_sequences();
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
        test_a_gateway_authored_event_cannot_squat_on_a_venue_sequence();
        test_a_synthetic_answer_is_not_blocked_by_a_native_sequence();
        test_a_replayed_synthetic_answer_is_caught_by_command_identity();
        test_native_kinds_keep_strict_sequence_deduplication();
        test_a_denied_order_releases_its_reservation_and_frees_the_slot();
        test_a_filled_submit_can_never_become_unknown();
        test_a_denied_order_that_already_filled_is_a_conflict();
        test_agreement_resolves_the_effect_without_moving_position();
        test_a_fill_the_cluster_never_saw_is_missing_evidence_not_a_conflict();
        test_a_venue_that_lost_a_fill_is_a_conflict();
        test_a_snapshot_taken_before_a_fill_cannot_strand_the_order();
        test_replaying_the_same_answer_changes_nothing();
        test_the_verdict_is_a_function_of_the_total_not_of_history();
        test_a_reconciliation_answer_is_refused_where_it_cannot_apply();
        test_a_resolved_sweep_cannot_excuse_an_unbooked_execution();
        test_execution_oracle_rejects_a_receipt_without_an_economic_mutation();
        test_execution_oracle_rejects_a_venue_execution_never_delivered();
        test_a_clean_venue_delivers_and_resolves();
        test_the_fault_matrix_holds_the_gate();
        test_the_evidence_seam_holds_under_commit_faults();
        test_the_matrix_is_reproducible();
        test_retention_capacity_exceeds_the_horizon_under_nominal_faults();
        test_policy_rotation_reaches_the_reserved_outbox_conflict_bound();
        test_conflicts_saturate_the_cluster_and_it_fails_closed();
        test_the_bounded_retry_and_query_budgets_are_actually_reached();
        std::cout << "all V2 gateway delivery tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 gateway delivery test failed: " << error.what() << '\n';
        return 1;
    }
}
