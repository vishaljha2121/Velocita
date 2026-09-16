#include "velocita/v2/gateway.hpp"
#include "velocita/v2/payload.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
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

Digest256 digest(const std::uint8_t marker) {
    Digest256 value;
    value.bytes[0] = static_cast<std::byte>(marker);
    return value;
}

// Assert the transport stayed empty: a refusal code alone cannot prove no send occurred.
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
    auto decision = evaluate_pre_send(state, effect_id, control, venue, quota);
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
    TradingState state;
    EffectId effect_id;
    ClusterOrderId cluster_order_id;
};

Fixture authorized_submit() {
    TradingState state;
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
        MoneyMicros::from_atoms(8'000), {}, {}, 1'000'000, 2'500};

    ApplyScratch scratch;
    const Command evaluate{{CallerId{1}, RequestSequence{1}, digest(0x51)},
                           EvaluateDecisionFrame{DecisionId{20},
                                                 EvaluationMode::authoritative,
                                                 frame()}};
    require(apply(state, evaluate, scratch).code == ApplyCode::applied,
            "gateway fixture could not retain its decision");
    const Command authorize{{CallerId{1}, RequestSequence{2}, digest(0x52)},
                            AuthorizeDecision{DecisionId{20}}};
    const auto submit = apply(state, authorize, scratch);
    require(submit.code == ApplyCode::applied,
            "gateway fixture could not authorize its submit");
    return {state, submit.effect_id(), submit.cluster_order_id()};
}

// The same fixture carried one step further: the submit is acknowledged so the
// order rests, then a cancel is authorized against it. The emergency-lane tests
// need a RISK-REDUCING effect, and a cancel is the only operation that is one by
// its identity alone.
Fixture authorized_cancel() {
    auto fixture = authorized_submit();
    ApplyScratch scratch;
    const Command cancel{{CallerId{1}, RequestSequence{3}, digest(0x53)},
                         RequestCancel{fixture.cluster_order_id}};
    const auto result = apply(fixture.state, cancel, scratch);
    require(result.code == ApplyCode::applied,
            "gateway fixture could not authorize its cancel");
    require(result.effect_id() != fixture.effect_id,
            "the cancel reused the submit's effect identity");
    return {fixture.state, result.effect_id(), fixture.cluster_order_id};
}

// A control view confirmed against the leader AT `now`, with a lease that has
// not lapsed. Every existing test wants this: it is the ordinary case.
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

void test_a_permitted_effect_is_sent_with_its_exact_canonical_bytes() {
    const auto fixture = authorized_submit();
    RecordingTransport transport;
    const auto decision = deliver(fixture.state, fixture.effect_id,
                                  matching_control(fixture.state), capable_venue(),
                                  open_quota(), transport);
    require(decision.permitted(), "a valid effect was refused before send");
    require(transport.send_count() == 1, "a permitted effect was not sent once");

    // Transport bytes must equal the cluster's canonical reconstruction.
    const auto authoritative = materialize_order_effect(fixture.state, fixture.effect_id);
    require(authoritative.has_value() &&
                encode_payload(*authoritative).bytes == transport.last(),
            "the sent bytes differ from the cluster's own reconstruction");
}

void test_the_guard_reads_no_clock_and_mutates_nothing() {
    const auto fixture = authorized_submit();
    auto probe = fixture.state;
    static_assert(std::is_trivially_copyable_v<TradingState>);
    const auto first = evaluate_pre_send(probe, fixture.effect_id,
                                         matching_control(probe), capable_venue(),
                                         open_quota());
    require(std::memcmp(&probe, &fixture.state, sizeof(TradingState)) == 0,
            "the pre-send guard mutated authoritative state");

    const auto second = evaluate_pre_send(probe, fixture.effect_id,
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

void test_an_expired_authority_lease_stops_new_risk() {
    // G-055. Generation and kill-epoch fencing compare the gateway's cache
    // against CLUSTER STATE, which only helps a gateway that can still read the
    // cluster. A partitioned one keeps believing its last good answer forever,
    // and nothing in Phase 5 bounded how long it may act on it.
    //
    // The lease bounds exactly that: how stale a confirmed control view may be
    // before this gateway must assume someone else holds authority.
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

    // Exactly at the bound is still inside it; one tick past is not.
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
    // FAIL CLOSED. A default-constructed control view carries no lease, and the
    // tempting reading of "no bound" is "no limit". For an authority check the
    // only safe reading is the opposite: a gateway that cannot say when it last
    // confirmed its authority does not have any.
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

    // And a view claiming confirmation in the future is nonsense, not credit.
    auto from_the_future = matching_control(fixture.state);
    from_the_future.confirmed_at = from_the_future.now + 1;
    RecordingTransport other;
    require(deliver(fixture.state, fixture.effect_id, from_the_future,
                    capable_venue(), open_quota(), other)
                    .refusal == PreSendRefusal::authority_lease_expired,
            "a control view confirmed in the future was accepted");
}

void test_a_cancel_survives_what_stops_new_risk() {
    // G-056 and G-058. The emergency lane exists because the moment you most
    // need to cancel is the moment the normal lane is exhausted or the lease has
    // lapsed - a partition is exactly when both happen at once. A cancel path
    // that shares the normal lane's budget, or that the lease stops, is not an
    // independent kill path.
    const auto fixture = authorized_cancel();

    // Normal lane empty, emergency lane has room.
    RecordingTransport starved;
    require(deliver(fixture.state, fixture.effect_id, matching_control(fixture.state),
                    capable_venue(), SendQuotaView{0, 1}, starved)
                    .permitted(),
            "a cancel was refused while emergency capacity remained");
    require(starved.send_count() == 1, "the cancel did not reach the transport");

    // Lease lapsed. New risk is stopped; reducing risk is not.
    auto lapsed = matching_control(fixture.state);
    lapsed.now = lapsed.confirmed_at + lapsed.lease_bound + 1;
    RecordingTransport after_lapse;
    require(deliver(fixture.state, fixture.effect_id, lapsed, capable_venue(),
                    SendQuotaView{0, 1}, after_lapse)
                    .permitted(),
            "a lapsed lease stopped a cancel, trapping risk it cannot reduce");

    // Both lanes empty is still a refusal - the lane is reserved, not infinite.
    RecordingTransport exhausted;
    require(deliver(fixture.state, fixture.effect_id, matching_control(fixture.state),
                    capable_venue(), SendQuotaView{0, 0}, exhausted)
                    .refusal == PreSendRefusal::quota_exhausted,
            "a cancel was sent with both lanes exhausted");
}

void test_the_emergency_lane_cannot_carry_new_risk() {
    // G-057. The lane may only reduce risk, so admission is decided by the
    // OPERATION'S IDENTITY and not by arithmetic on its fields. A submit drawing
    // on emergency capacity would be the whole guarantee inverted.
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
    // claimed and attempted are gateway-local; replicated occurrences must be refused.
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

}  // namespace

int main() {
    try {
        test_a_permitted_effect_is_sent_with_its_exact_canonical_bytes();
        test_the_guard_reads_no_clock_and_mutates_nothing();
        test_stale_control_authority_is_never_sent();
        test_an_expired_authority_lease_stops_new_risk();
    test_an_unconfigured_lease_is_expired_not_unlimited();
    test_a_cancel_survives_what_stops_new_risk();
    test_the_emergency_lane_cannot_carry_new_risk();
    test_a_venue_that_cannot_represent_the_intent_is_refused_not_approximated();
        test_exhausted_quota_defers_rather_than_sending();
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
