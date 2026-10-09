#include "v2_catalog_test_support.hpp"

#include "velocita/v2/digest.hpp"
#include "velocita/v2/gateway.hpp"
#include "velocita/v2/paper_venue.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace velocita::v2;
using paper::PaperFaultPolicy;
using paper::PaperVenue;
using paper::PaperVenueConfig;
using sim::QueryOutcome;
using sim::SendOutcome;
using sim::Tick;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Digest256 digest(const std::uint8_t marker) {
    Digest256 value;
    value.bytes[0] = static_cast<std::byte>(marker);
    return value;
}

constexpr std::int64_t kBuyPrice = 60'001;
constexpr std::int64_t kSellPrice = 59'001;
constexpr std::int64_t kLots = 33;
constexpr std::int64_t kBudget = 25'000'000;
constexpr std::int64_t kBuyNotional = 19'800'330;
constexpr std::int64_t kBuyFee = 227'704;
constexpr std::int64_t kSellFee = 233'644;
constexpr Tick kLeaseBound = 1'000'000;

InstrumentExecutionProfile profile() {
    return {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4},
            InstrumentSpecVersion{7}, MoneyMicros::from_atoms(10),
            PriceTicks::from_atoms(1), PriceTicks::from_atoms(1'000'000),
            QuantityLots::from_atoms(1), QuantityLots::from_atoms(100'000)};
}

VenueCapabilities capabilities() {
    return {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
            kVenueDirectivePlaceLimit | kVenueDirectiveCancel, kVenueTifFok, 0};
}

VenueInstrumentSpec spec() {
    return {VenueId{9}, InstrumentId{4}, 1'000'000, 1'000, 8};
}

const VerifiedExecutionCatalog& paper_catalog() {
    static const auto value = [] {
        auto row = catalog_test::row(AccountId{5}, profile(), capabilities());
        row.tick_quote_micros = 1'000'000;
        row.lot_base_atoms = 1'000;
        row.base_decimals = 8;
        const std::array rows{row};
        return catalog_test::catalog(rows);
    }();
    return value;
}

PaperVenueConfig config(const std::int64_t quote = kBudget,
                        const std::int64_t base = 0) {
    PaperVenueConfig value;
    value.spec = spec();
    value.profile = profile();
    value.buy_fee_bps = 115;
    value.sell_fee_bps = 120;
    value.quote_balance = MoneyMicros::from_atoms(quote);
    value.base_balance = QuantityLots::from_atoms(base);
    value.latency = 1;
    return value;
}

PolicyArtifact paper_policy() {
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
    value.rule_count = 2;
    value.rules[0] = {0, PredicateOperator::greater_than_or_equal,
                      FeatureMicros::from_atoms(60'000), Directive::place_limit};
    value.rules[1] = {0, PredicateOperator::greater_than,
                      FeatureMicros::from_atoms(0), Directive::place_limit};
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

ActionTemplate fok_template(const Side side, const std::uint16_t slot) {
    ActionTemplate value;
    value.schema_version = kActionTemplateSchemaVersion;
    value.directive = Directive::place_limit;
    value.side = side;
    value.time_in_force = TimeInForce::fill_or_kill;
    value.post_only = PostOnlyMode::prohibited;
    value.order_slot = LogicalOrderSlot{slot};
    value.instrument_spec_version = InstrumentSpecVersion{7};
    value.venue_capabilities_version = VenueCapabilitiesVersion{8};
    value.price_source = {EconomicValueSourceKind::feature, 0,
                          RoundingMode::toward_zero, {}, 1, {}};
    value.quantity_source = {EconomicValueSourceKind::fixed, 0,
                             RoundingMode::toward_zero,
                             QuantityLots::from_atoms(kLots), 0};
    value.safety_caps = {QuantityLots::from_atoms(kLots),
                         MoneyMicros::from_atoms(kBudget),
                         PriceTicks::from_atoms(0), PriceTicks::from_atoms(0),
                         100, value.price_source};
    return value;
}

ExecutablePolicyArtifact executable_policy(const FeatureSchemaArtifact& schema) {
    ExecutablePolicyArtifact value;
    value.schema_version = kExecutablePolicyArtifactSchemaVersion;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.policy_artifact_digest = paper_policy().artifact_digest;
    value.feature_schema_id = schema.feature_schema_id;
    value.feature_schema_digest = schema.artifact_digest;
    value.template_count = 2;
    value.templates[0] = fok_template(Side::buy, 1);
    value.templates[1] = fok_template(Side::sell, 2);
    value.rule_binding_count = 2;
    value.rule_template_slots[0] = ActionTemplateSlot{1};
    value.rule_template_slots[1] = ActionTemplateSlot{2};
    value.artifact_digest = compute_artifact_digest(value).digest;
    return value;
}

ApplyResult run(TradingState& state, const Command& command) {
    ApplyScratch scratch;
    return apply(state, command, scratch, &paper_catalog());
}

TradingState paper_state() {
    TradingState state = catalog_test::state(paper_catalog());
    state.strategy_mode = StrategyMode::normal;
    state.kill_epoch = KillEpoch{2};
    state.executor_generation = ExecutorGeneration{3};
    state.policy_count = 1;
    state.policies[0] = {paper_policy(), PolicyLifecycle::active};
    const auto schema = feature_schema();
    state.active_executable_policy = {true, schema, executable_policy(schema),
                                      profile(), capabilities(), 1, 2};
    state.source_count = 1;
    state.sources[0] = {SourceId{12}, SourceGeneration{1}, SourceSequence{1}};
    state.source_event_count = 1;
    state.source_events[0] = {SourceId{12}, SourceGeneration{1}, SourceSequence{1},
                              digest(0x44)};
    state.risk_account_count = 1;
    auto& risk = state.risk_accounts[0];
    risk.account_id = AccountId{5};
    risk.instrument_id = InstrumentId{4};
    risk.risk_policy_version = RiskPolicyVersion{6};
    risk.risk_version = RiskVersion{1};
    risk.position_version = PositionVersion{1};
    risk.max_order_quantity = QuantityLots::from_atoms(kLots);
    risk.max_reserved_quantity = QuantityLots::from_atoms(2 * kLots);
    risk.max_order_gross_notional = MoneyMicros::from_atoms(kBudget);
    risk.max_reserved_exposure = MoneyMicros::from_atoms(2 * kBudget);
    risk.initial_margin_ppm = 1'000'000;
    risk.maximum_fee_ppm = 12'000;
    risk.max_position_quantity = QuantityLots::from_atoms(10 * kLots);
    risk.allow_short = false;
    return state;
}

struct Harness {
    TradingState state = paper_state();
    std::uint64_t request{1'000};
    std::uint64_t source_sequence{1};
    std::uint64_t synthetic_sequence{900'000};
};

std::optional<OrderEffect> try_authorize_at(Harness& harness, const std::uint64_t decision,
                                            const std::int64_t price) {
    const auto sequence = ++harness.source_sequence;
    const auto evidence = digest(static_cast<std::uint8_t>(0x40 + sequence));
    require(run(harness.state,
                {{CallerId{7}, RequestSequence{++harness.request}, digest(0x21)},
                 RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                   SourceSequence{sequence}, evidence}})
                    .code == ApplyCode::applied,
            "source event admission failed");
    DecisionFrame frame;
    frame.policy_id = PolicyId{10};
    frame.policy_version = PolicyVersion{1};
    frame.feature_schema_id = FeatureSchemaId{3};
    frame.venue_id = VenueId{9};
    frame.venue_session_id = VenueSessionId{1};
    frame.venue_sequence = VenueSequence{sequence};
    frame.source_id = SourceId{12};
    frame.source_generation = SourceGeneration{1};
    frame.source_sequence = SourceSequence{sequence};
    frame.event_time_ns = 150;
    frame.receive_time_ns = 151;
    frame.instrument_id = InstrumentId{4};
    frame.account_id = AccountId{5};
    frame.feature_count = 1;
    frame.features[0] = FeatureMicros::from_atoms(price);
    frame.market_data_status = MarketDataStatus::ready;
    frame.market_data_age_ns = 1;
    frame.position_version = harness.state.risk_accounts[0].position_version;
    frame.risk_version = harness.state.risk_accounts[0].risk_version;
    frame.raw_evidence_digest = evidence;
    require(run(harness.state,
                {{CallerId{8}, RequestSequence{++harness.request},
                  digest(static_cast<std::uint8_t>(decision))},
                 EvaluateDecisionFrame{DecisionId{decision},
                                       EvaluationMode::authoritative, frame}})
                    .code == ApplyCode::applied,
            "decision evaluation failed");
    const auto authorized =
        run(harness.state, {{CallerId{1}, RequestSequence{++harness.request},
                             digest(static_cast<std::uint8_t>(0x80 + decision))},
                            AuthorizeDecision{DecisionId{decision}}});
    if (authorized.code != ApplyCode::applied) return std::nullopt;
    const GatewayControlView control{harness.state.executor_generation,
                                     harness.state.kill_epoch,
                                     harness.state.strategy_mode, 0, kLeaseBound, 0};
    const auto candidate = evaluate_delivery_candidate(
        harness.state, authorized.effect_id(), control,
        {VenueId{9}, VenueCapabilitiesVersion{8}, false, false}, SendQuotaView{4, 1},
        &paper_catalog());
    require(candidate.permitted(), "the gateway refused to materialize the effect");
    return *candidate.effect;
}

OrderEffect authorize_at(Harness& harness, const std::uint64_t decision,
                         const std::int64_t price) {
    const auto effect = try_authorize_at(harness, decision, price);
    require(effect.has_value(), "authorization failed");
    return *effect;
}

ApplyResult commit(Harness& harness, const VenueObservation& observation) {
    const CommandIdentity identity{CallerId{22}, RequestSequence{++harness.request},
                                   observation_fingerprint(observation)};
    if (observation.kind == VenueObservationKind::fill) {
        return run(harness.state, {identity, ObserveFill{observation}});
    }
    return run(harness.state, {identity, ObserveOrderEvent{observation}});
}

struct Delivered {
    VenueObservation observation{};
    ApplyResult result{};
};

std::vector<Delivered> drain(PaperVenue& venue, Harness& harness, const Tick now) {
    std::array<VenueObservation, sim::kMaxVenueObservationsPerPoll> out{};
    const auto count = venue.poll(out, now);
    std::vector<Delivered> delivered;
    for (std::size_t index = 0; index < count; ++index) {
        delivered.push_back({out[index], commit(harness, out[index])});
        venue.acknowledge(out[index]);
    }
    return delivered;
}

VenueObservation synthetic(Harness& harness, const VenueObservationKind kind,
                           const OrderEffect& effect) {
    VenueObservation value;
    value.kind = kind;
    value.venue_id = VenueId{9};
    value.venue_session_id = VenueSessionId{1};
    value.venue_sequence = VenueSequence{++harness.synthetic_sequence};
    value.cluster_order_id = effect.cluster_order_id;
    value.effect_id = effect.effect_id;
    value.evidence_digest = digest(0x6C);
    return value;
}

const ClusterOrder& order_of(const TradingState& state, const ClusterOrderId id) {
    for (std::size_t index = 0; index < state.order_count; ++index) {
        if (state.orders[index].cluster_order_id == id) return state.orders[index];
    }
    throw std::runtime_error("order not found");
}

const RiskReservation& reservation_of(const TradingState& state,
                                      const ClusterOrderId id) {
    return state.reservations[order_of(state, id).reservation_index];
}

const OutboxEffectRecord& outbox_of(const TradingState& state, const EffectId id) {
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].effect_id == id) return state.outbox_effects[index];
    }
    throw std::runtime_error("effect not found");
}

void require_released(const TradingState& state, const ClusterOrderId id,
                      const std::string& where) {
    const auto& reservation = reservation_of(state, id);
    const auto& risk = state.risk_accounts[0];
    require(reservation.state == ReservationState::released &&
                reservation.remaining_quantity.atoms() == 0 &&
                reservation.margin_hold.atoms() == 0 &&
                reservation.fee_hold.atoms() == 0,
            where + ": the order's reservation was not released");
    require(risk.reserved_quantity.atoms() == 0 && risk.reserved_margin.atoms() == 0 &&
                risk.reserved_fee.atoms() == 0,
            where + ": the account still holds reserved risk");
}

void require_held_untouched(const TradingState& state, const ClusterOrderId id,
                            const std::string& where) {
    const auto& reservation = reservation_of(state, id);
    require(reservation.state == ReservationState::held &&
                reservation.remaining_quantity.atoms() == kLots &&
                reservation.margin_hold.atoms() == kBuyNotional &&
                reservation.fee_hold.atoms() == 237'604,
            where + ": the reservation moved without evidence");
    require(state.risk_accounts[0].position_quantity.atoms() == 0,
            where + ": the position moved without evidence");
}

OrderEffect raw_effect(const std::uint64_t id, const Side side,
                       const std::int64_t price = kBuyPrice,
                       const std::int64_t lots = kLots) {
    OrderEffect effect;
    effect.effect_id = EffectId{1'000 + id};
    effect.cluster_order_id = ClusterOrderId{2'000 + id};
    effect.committed_directive_index = 1;
    effect.executor_generation = ExecutorGeneration{3};
    effect.kill_epoch = KillEpoch{2};
    effect.venue_id = VenueId{9};
    effect.account_id = AccountId{5};
    effect.instrument_id = InstrumentId{4};
    effect.operation = EffectOperation::submit;
    effect.side = side;
    effect.price = PriceTicks::from_atoms(price);
    effect.quantity = QuantityLots::from_atoms(lots);
    effect.time_in_force = TimeInForce::fill_or_kill;
    effect.policy_id = PolicyId{10};
    effect.policy_version = PolicyVersion{1};
    effect.decision_id = DecisionId{id};
    effect.risk_version = RiskVersion{1};
    effect.source_generation = SourceGeneration{1};
    effect.source_sequence = SourceSequence{1};
    effect.payload_digest = digest(0x77);
    return effect;
}

PaperFaultPolicy only(std::uint16_t PaperFaultPolicy::*fault) {
    auto policy = PaperFaultPolicy::clean();
    policy.*fault = 1'000;
    return policy;
}

void test_fok_buy_fills_whole_at_limit_with_rounded_up_fee() {
    Harness harness;
    PaperVenue venue(config(), PaperFaultPolicy::clean(), 7);
    require(venue.configured(), "the fixture configuration was refused");
    const auto effect = authorize_at(harness, 20, kBuyPrice);
    require(venue.send(effect, 10) == SendOutcome::accepted, "a clean FOK buy was not accepted");

    const auto delivered = drain(venue, harness, 100);
    require(delivered.size() == 2, "a whole fill should report exactly ack + one fill");
    require(delivered[0].observation.kind == VenueObservationKind::order_acknowledged,
            "the acknowledgement was not first");
    const auto& fill = delivered[1].observation;
    require(fill.kind == VenueObservationKind::fill && fill.execution_id.value() != 0,
            "the second report was not an identified fill");
    require(fill.price.atoms() == kBuyPrice, "FOK filled away from the limit price");
    require(fill.quantity.atoms() == kLots, "FOK did not fill the entire quantity");
    require(fill.side == Side::buy, "the fill carried the wrong side");
    require(fill.fee.atoms() == kBuyFee, "the buy fee is not ceil(notional * 115bp)");
    require(fill.venue_order_id == delivered[0].observation.venue_order_id,
            "the fill and the acknowledgement name different venue orders");
    for (const auto& each : delivered) {
        require(each.result.code == ApplyCode::applied, "the kernel refused a clean report");
    }

    require(harness.state.risk_accounts[0].position_quantity.atoms() == kLots,
            "the kernel position is not the filled quantity");
    require(order_of(harness.state, effect.cluster_order_id).state ==
                ClusterOrderState::filled,
            "the order did not reach filled");
    require(outbox_of(harness.state, effect.effect_id).state == OutboxState::acknowledged,
            "the submit effect was not settled");
    require_released(harness.state, effect.cluster_order_id, "clean buy");

    const auto balances = venue.balances();
    require(balances.quote.atoms() == 4'971'966, "the venue debited the wrong quote");
    require(balances.base.atoms() == kLots, "the venue credited the wrong base");
    require(balances.fees_paid.atoms() == kBuyFee, "the venue booked the wrong fee");
    require(venue.pending_observation_count() == 0,
            "acknowledged reports were still owned by the venue");
}

void test_round_trip_sell_uses_sell_side_cost_and_returns_flat() {
    Harness harness;
    PaperVenue venue(config(), PaperFaultPolicy::clean(), 7);
    const auto buy = authorize_at(harness, 20, kBuyPrice);
    require(venue.send(buy, 10) == SendOutcome::accepted, "the buy was not accepted");
    (void)drain(venue, harness, 100);

    const auto sell = authorize_at(harness, 21, kSellPrice);
    require(sell.side == Side::sell && sell.price.atoms() == kSellPrice,
            "the fixture did not produce the sell decision");
    require(venue.send(sell, 200) == SendOutcome::accepted, "the covered sell was not accepted");
    const auto delivered = drain(venue, harness, 300);
    require(delivered.size() == 2 && delivered[1].observation.kind == VenueObservationKind::fill,
            "the sell did not report ack + fill");
    require(delivered[1].observation.fee.atoms() == kSellFee,
            "the sell fee is not ceil(notional * 120bp)");
    require(delivered[1].result.code == ApplyCode::applied, "the kernel refused the sell fill");

    require(harness.state.risk_accounts[0].position_quantity.atoms() == 0,
            "a round trip did not return the kernel position to flat");
    require_released(harness.state, sell.cluster_order_id, "round trip");
    const auto balances = venue.balances();
    require(balances.quote.atoms() == 24'208'652, "the round trip left the wrong quote");
    require(balances.base.atoms() == 0, "the round trip left base inventory");
    require(balances.fees_paid.atoms() == 461'348, "the round trip booked the wrong fees");
}

void test_budget_boundary_includes_the_fee() {
    {
        Harness harness;
        PaperVenue venue(config(20'028'034), PaperFaultPolicy::clean(), 7);
        const auto effect = authorize_at(harness, 20, kBuyPrice);
        require(venue.send(effect, 10) == SendOutcome::accepted,
                "a buy costing exactly the balance was refused");
        require(venue.balances().quote.atoms() == 0, "an exact-balance buy left change");
    }
    Harness harness;
    PaperVenue venue(config(20'028'033), PaperFaultPolicy::clean(), 7);
    const auto effect = authorize_at(harness, 20, kBuyPrice);
    require(venue.send(effect, 10) == SendOutcome::business_rejected,
            "a buy one micro over budget (fee included) was not killed");
    require(venue.last_reject() == paper::PaperRejectReason::insufficient_quote,
            "the budget refusal gave the wrong reason");
    require(venue.balances().quote.atoms() == 20'028'033 && venue.order_count() == 0,
            "a killed buy moved venue balances or left an order");
    const auto delivered = drain(venue, harness, 100);
    require(delivered.size() == 1 &&
                delivered[0].observation.kind == VenueObservationKind::order_rejected &&
                delivered[0].result.code == ApplyCode::applied,
            "the kill was not reported to the kernel as a rejection");
    require(order_of(harness.state, effect.cluster_order_id).state ==
                ClusterOrderState::rejected,
            "the killed order is not rejected in the kernel");
    require(harness.state.risk_accounts[0].position_quantity.atoms() == 0,
            "a killed buy moved the position");
    require_released(harness.state, effect.cluster_order_id, "budget kill");
}

void test_long_only_sell_without_inventory_is_killed_whole() {
    PaperVenue naked(config(), PaperFaultPolicy::clean(), 7);
    require(naked.send(raw_effect(1, Side::sell, kSellPrice), 10) ==
                SendOutcome::business_rejected,
            "a sell with no inventory was not killed");
    require(naked.last_reject() == paper::PaperRejectReason::insufficient_base,
            "the long-only refusal gave the wrong reason");
    require(naked.balances() == paper::PaperBalances{MoneyMicros::from_atoms(kBudget), {}, {}},
            "a killed sell moved venue balances");

    Harness harness;
    PaperVenue venue(config(), PaperFaultPolicy::clean(), 7);
    if (const auto sell = try_authorize_at(harness, 20, kSellPrice)) {
        require(sell->side == Side::sell, "the fixture did not produce a sell");
        require(venue.send(*sell, 10) == SendOutcome::business_rejected,
                "a kernel-authorized naked sell was not killed");
        (void)drain(venue, harness, 100);
        require(harness.state.risk_accounts[0].position_quantity.atoms() == 0,
                "a killed naked sell moved the position short");
        require_released(harness.state, sell->cluster_order_id, "naked sell");
    } else {
        require(harness.state.order_count == 0 &&
                    harness.state.risk_accounts[0].reserved_quantity.atoms() == 0,
                "a refused naked sell left kernel state behind");
    }

    PaperVenue short_by_one(config(kBudget, kLots - 1), PaperFaultPolicy::clean(), 7);
    require(short_by_one.send(raw_effect(1, Side::sell, kSellPrice), 10) ==
                SendOutcome::business_rejected,
            "a sell one lot over inventory was not killed");
    PaperVenue exact(config(kBudget, kLots), PaperFaultPolicy::clean(), 7);
    require(exact.send(raw_effect(1, Side::sell, kSellPrice), 10) == SendOutcome::accepted,
            "a sell of exactly the inventory was refused");
}

void test_only_fill_or_kill_submits_execute() {
    PaperVenue venue(config(), PaperFaultPolicy::clean(), 7);
    auto gtc = raw_effect(1, Side::buy);
    gtc.time_in_force = TimeInForce::good_til_cancel;
    require(venue.send(gtc, 10) == SendOutcome::business_rejected,
            "a GTC submit was not refused by a FOK-only venue");
    require(venue.last_reject() == paper::PaperRejectReason::not_fill_or_kill,
            "the TIF refusal gave the wrong reason");
    require(venue.order_count() == 0 && venue.balances().quote.atoms() == kBudget,
            "a refused GTC submit rested or moved balances");
    std::array<VenueObservation, 4> out{};
    require(venue.poll(out, 100) == 1 && out[0].kind == VenueObservationKind::order_rejected &&
                out[0].effect_id == gtc.effect_id,
            "the TIF refusal was not reported");
}

void test_cancel_is_refused_because_nothing_rests() {
    PaperVenue venue(config(), PaperFaultPolicy::clean(), 7);
    const auto buy = raw_effect(1, Side::buy);
    require(venue.send(buy, 10) == SendOutcome::accepted, "the buy was not accepted");
    auto cancel = buy;
    cancel.effect_id = EffectId{9'999};
    cancel.operation = EffectOperation::cancel;
    cancel.price = {};
    cancel.quantity = {};
    const auto before = venue.balances();
    require(venue.send(cancel, 11) == SendOutcome::business_rejected,
            "a cancel of a FOK order was accepted");
    require(venue.last_reject() == paper::PaperRejectReason::nothing_resting,
            "the cancel refusal gave the wrong reason");
    require(venue.balances() == before, "a refused cancel moved balances");
    std::array<VenueObservation, 8> out{};
    const auto count = venue.poll(out, 100);
    bool reported = false;
    for (std::size_t index = 0; index < count; ++index) {
        reported |= out[index].kind == VenueObservationKind::order_rejected &&
                    out[index].effect_id == cancel.effect_id;
    }
    require(reported, "the refused cancel was not reported against the cancel effect");
}

void test_resubmitting_a_known_order_is_a_venue_duplicate() {
    PaperVenue venue(config(), PaperFaultPolicy::clean(), 7);
    const auto buy = raw_effect(1, Side::buy);
    require(venue.send(buy, 10) == SendOutcome::accepted, "the buy was not accepted");
    const auto after_first = venue.balances();
    require(venue.send(buy, 11) == SendOutcome::venue_duplicate,
            "a resubmitted client order was executed again");
    require(venue.balances() == after_first && venue.order_count() == 1,
            "a duplicate submit moved balances or created an order");
}

void test_unrenderable_or_misrouted_effects_never_reach_the_book() {
    PaperVenue venue(config(), PaperFaultPolicy::clean(), 7);
    const auto huge = raw_effect(1, Side::buy, 10'000'000'000'000);
    require(venue.send(huge, 10) == SendOutcome::pre_send_failure,
            "an unrenderable price was sent");
    auto misrouted = raw_effect(2, Side::buy);
    misrouted.venue_id = VenueId{10};
    require(venue.send(misrouted, 10) == SendOutcome::pre_send_failure,
            "an effect for another venue was accepted");
    auto other_instrument = raw_effect(3, Side::buy);
    other_instrument.instrument_id = InstrumentId{5};
    require(venue.send(other_instrument, 10) == SendOutcome::pre_send_failure,
            "an effect for another instrument was accepted");
    require(venue.order_count() == 0 && venue.pending_observation_count() == 0 &&
                venue.balances().quote.atoms() == kBudget,
            "a refused send left venue state behind");

    auto disagreeing = config();
    disagreeing.spec.lot_base_atoms = 200;
    PaperVenue misconfigured(disagreeing, PaperFaultPolicy::clean(), 7);
    require(!misconfigured.configured(), "a spec disagreeing with the profile was accepted");
    require(misconfigured.send(raw_effect(4, Side::buy), 10) == SendOutcome::pre_send_failure,
            "a misconfigured venue executed an order");
}

void test_fee_arithmetic_is_exact_and_rounds_against_us() {
    require(paper::paper_fee(MoneyMicros::from_atoms(10'000), 115)->atoms() == 115,
            "an exact fee was rounded");
    require(paper::paper_fee(MoneyMicros::from_atoms(1), 1)->atoms() == 1,
            "a fractional micro was forgiven");
    require(paper::paper_fee(MoneyMicros::from_atoms(0), 120)->atoms() == 0,
            "a zero notional was charged");
    require(!paper::paper_fee(MoneyMicros::from_atoms(1), 10'001).has_value(),
            "a fee above 100% was accepted");
    const auto max = std::numeric_limits<std::int64_t>::max();
    require(paper::paper_fee(MoneyMicros::from_atoms(max), 10'000)->atoms() == max,
            "a 100% fee on the largest notional was not exact");
    require(paper::paper_notional(PriceTicks::from_atoms(kBuyPrice),
                                  QuantityLots::from_atoms(kLots),
                                  MoneyMicros::from_atoms(10))
                    ->atoms() == kBuyNotional,
            "the notional is not price * lots * micros per tick-lot");
    require(!paper::paper_notional(PriceTicks::from_atoms(max / 2),
                                   QuantityLots::from_atoms(3), MoneyMicros::from_atoms(1))
                 .has_value(),
            "an overflowing notional produced a value");
}

void test_fault_delay_withholds_reports_until_due() {
    Harness harness;
    PaperVenue venue(config(), only(&PaperFaultPolicy::delay_permille), 7);
    const auto effect = authorize_at(harness, 20, kBuyPrice);
    require(venue.send(effect, 10) == SendOutcome::accepted, "the delayed buy was not accepted");
    require(venue.last_faults().delay, "the delay fault did not fire");
    require(drain(venue, harness, 35).empty(), "a delayed report arrived early");
    require_held_untouched(harness.state, effect.cluster_order_id, "delay");
    require(order_of(harness.state, effect.cluster_order_id).state ==
                ClusterOrderState::dispatch_authorized,
            "the order moved before any report arrived");
    const auto ack = drain(venue, harness, 36);
    require(ack.size() == 1 && ack[0].observation.kind == VenueObservationKind::order_acknowledged,
            "the delayed acknowledgement was not due at tick 36");
    const auto fill = drain(venue, harness, 37);
    require(fill.size() == 1 && fill[0].result.code == ApplyCode::applied,
            "the delayed fill was not due at tick 37");
    require(harness.state.risk_accounts[0].position_quantity.atoms() == kLots,
            "the delayed fill did not book");
    require_released(harness.state, effect.cluster_order_id, "delay");
}

void test_fault_duplicate_moves_the_position_once() {
    Harness harness;
    PaperVenue venue(config(), only(&PaperFaultPolicy::duplicate_permille), 7);
    const auto effect = authorize_at(harness, 20, kBuyPrice);
    require(venue.send(effect, 10) == SendOutcome::accepted, "the buy was not accepted");
    std::vector<Delivered> all;
    const std::array<std::size_t, 3> per_tick{1, 2, 1};
    for (Tick now = 11; now <= 13; ++now) {
        const auto batch = drain(venue, harness, now);
        require(batch.size() == per_tick[now - 11],
                "a duplicate did not trail its acknowledged original by one tick");
        for (const auto& each : batch) all.push_back(each);
    }
    require(all.size() == 4, "the duplicate fault did not deliver every report twice");
    std::size_t fills_applied = 0;
    std::size_t fills_deduplicated = 0;
    for (const auto& each : all) {
        if (each.observation.kind != VenueObservationKind::fill) continue;
        fills_applied += each.result.code == ApplyCode::applied;
        fills_deduplicated += each.result.code == ApplyCode::already_applied;
    }
    require(fills_applied == 1 && fills_deduplicated == 1,
            "the kernel did not book one fill and deduplicate its copy");
    require(harness.state.risk_accounts[0].position_quantity.atoms() == kLots,
            "a duplicated fill moved the position twice");
    require_released(harness.state, effect.cluster_order_id, "duplicate");
    require(venue.balances().base.atoms() == kLots &&
                venue.balances().fees_paid.atoms() == kBuyFee,
            "the venue executed a duplicated report twice");
    require(venue.pending_observation_count() == 0, "a duplicate outlived acknowledgement");
}

void test_fault_reorder_fill_before_ack_converges() {
    Harness harness;
    PaperVenue venue(config(), only(&PaperFaultPolicy::reorder_permille), 7);
    const auto effect = authorize_at(harness, 20, kBuyPrice);
    require(venue.send(effect, 10) == SendOutcome::accepted, "the buy was not accepted");
    const auto delivered = drain(venue, harness, 100);
    require(delivered.size() == 2 && delivered[0].observation.kind == VenueObservationKind::fill,
            "the reorder fault did not deliver the fill first");
    require(delivered[0].observation.venue_sequence > delivered[1].observation.venue_sequence,
            "reordering changed venue sequence numbers rather than delivery order");
    require(delivered[0].result.code == ApplyCode::applied,
            "a fill arriving before its acknowledgement was refused");
    require(delivered[1].result.code == ApplyCode::venue_observation_not_applicable,
            "a late acknowledgement was applied to a filled order");
    require(order_of(harness.state, effect.cluster_order_id).state == ClusterOrderState::filled,
            "the reordered order did not end filled");
    require(harness.state.risk_accounts[0].position_quantity.atoms() == kLots,
            "the reordered fill did not book exactly once");
    require_released(harness.state, effect.cluster_order_id, "reorder");
}

void test_a_late_reconciliation_that_agrees_with_a_finished_order_is_not_a_refusal() {
    {
        Harness harness;
        PaperVenue venue(config(), PaperFaultPolicy::clean(), 7);
        const auto effect = authorize_at(harness, 20, kBuyPrice);
        require(venue.send(effect, 10) == SendOutcome::accepted, "the buy was not accepted");
        const auto delivered = drain(venue, harness, 100);
        require(delivered.size() == 2 && delivered[1].result.code == ApplyCode::applied &&
                    order_of(harness.state, effect.cluster_order_id).state == ClusterOrderState::filled,
                "fixture: the order did not fill");
        const auto answer = venue.query(effect.cluster_order_id, 200);
        const auto before_account = harness.state.risk_accounts[0];
        const auto before_order = order_of(harness.state, effect.cluster_order_id);
        auto agreeing = synthetic(harness, VenueObservationKind::order_reconciled, effect);
        agreeing.venue_order_id = answer.venue_order_id;
        agreeing.quantity = answer.filled_quantity;
        require(commit(harness, agreeing).code == ApplyCode::already_applied,
                "a reconciliation that agrees with a filled order was refused");
        require(harness.state.risk_accounts[0] == before_account &&
                    order_of(harness.state, effect.cluster_order_id) == before_order,
                "an agreeing reconciliation changed the account or the order");
        for (const std::int64_t wrong : {kLots - 1, kLots + 1, std::int64_t{0}}) {
            auto disagreeing = synthetic(harness, VenueObservationKind::order_reconciled, effect);
            disagreeing.venue_order_id = answer.venue_order_id;
            disagreeing.quantity = QuantityLots::from_atoms(wrong);
            require(commit(harness, disagreeing).code == ApplyCode::venue_observation_not_applicable,
                    "a reconciliation reporting " + std::to_string(wrong) + " lots for a filled order was accepted");
        }
        auto unknown_to_venue = synthetic(harness, VenueObservationKind::order_reconciled, effect);
        unknown_to_venue.quantity = answer.filled_quantity;
        const auto denied_code = commit(harness, unknown_to_venue).code;
        require(denied_code != ApplyCode::applied && denied_code != ApplyCode::already_applied,
                "a venue that denies a filled order was taken as agreeing with it");
        auto denied_and_empty = synthetic(harness, VenueObservationKind::order_reconciled, effect);
        require(commit(harness, denied_and_empty).code == ApplyCode::venue_observation_not_applicable,
                "a venue with no record of a filled order was taken as agreeing with it");
        require(harness.state.risk_accounts[0] == before_account, "a refused reconciliation changed the account");
    }
    {
        Harness harness;
        PaperVenue venue(config(), only(&PaperFaultPolicy::reject_permille), 7);
        const auto effect = authorize_at(harness, 20, kBuyPrice);
        require(venue.send(effect, 10) == SendOutcome::business_rejected, "fixture: the order was not killed");
        (void)drain(venue, harness, 100);
        require(order_of(harness.state, effect.cluster_order_id).state == ClusterOrderState::rejected,
                "fixture: the order is not rejected");
        const auto denied = synthetic(harness, VenueObservationKind::order_reconciled, effect);
        require(commit(harness, denied).code == ApplyCode::already_applied,
                "a venue with no record of a rejected order was not taken as agreeing");
        auto filled = synthetic(harness, VenueObservationKind::order_reconciled, effect);
        filled.venue_order_id = VenueOrderId{9};
        filled.quantity = QuantityLots::from_atoms(1);
        require(commit(harness, filled).code == ApplyCode::venue_observation_not_applicable,
                "a fill reported for a rejected order was accepted");
        auto resting = synthetic(harness, VenueObservationKind::order_reconciled, effect);
        resting.venue_order_id = VenueOrderId{9};
        require(commit(harness, resting).code == ApplyCode::venue_observation_not_applicable,
                "a venue that still holds a rejected order was taken as agreeing it is over");
    }
}

void test_fault_drop_response_unknown_then_query_adopts_the_fill() {
    Harness harness;
    PaperVenue venue(config(), only(&PaperFaultPolicy::drop_response_permille), 7);
    const auto effect = authorize_at(harness, 20, kBuyPrice);
    GatewayDeliveryState delivery;
    (void)begin_gateway_recovery(delivery, paper_state(), 0);
    require(record_send(delivery, effect.effect_id, 10), "record_send failed");
    require(venue.send(effect, 10) == SendOutcome::ambiguous,
            "a lost response was not reported as ambiguous");
    require(venue.balances().base.atoms() == kLots, "the fixture venue did not execute");
    require(drain(venue, harness, 1'000).empty(), "lost reports were delivered anyway");

    const GatewayTimeouts timeouts;
    const Tick deadline = 10 + timeouts.response_deadline;
    const GatewayControlView control{harness.state.executor_generation, harness.state.kill_epoch,
                                     harness.state.strategy_mode, deadline, kLeaseBound,
                                     deadline};
    const VenueCapabilityView view{VenueId{9}, VenueCapabilitiesVersion{8}, false, false};
    const auto plan = plan_delivery(harness.state, delivery, GatewayEvidenceState{}, control,
                                    view, SendQuotaView{4, 1}, timeouts, deadline,
                                    &paper_catalog());
    require(plan.ambiguous_count == 1 && plan.now_ambiguous[0] == effect.effect_id &&
                plan.send_count == 0,
            "the gateway did not declare the silent submit ambiguous");
    require(commit(harness, synthetic(harness, VenueObservationKind::effect_unknown, effect))
                    .code == ApplyCode::applied,
            "the kernel refused the unknown-submission report");
    require(record_response(delivery, effect.effect_id), "record_response failed");
    require(order_of(harness.state, effect.cluster_order_id).state ==
                ClusterOrderState::submission_unknown,
            "the order is not UnknownSubmission");
    require_held_untouched(harness.state, effect.cluster_order_id, "unknown");

    require(plan_delivery(harness.state, delivery, GatewayEvidenceState{}, control, view,
                          SendQuotaView{4, 1}, timeouts, deadline + 100, &paper_catalog())
                    .send_count == 0,
            "an unknown submit became resendable");
    {
        auto probe = harness;
        const auto sequence = ++probe.source_sequence;
        const auto evidence = digest(static_cast<std::uint8_t>(0x40 + sequence));
        (void)run(probe.state, {{CallerId{7}, RequestSequence{++probe.request}, digest(0x21)},
                                RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                                  SourceSequence{sequence}, evidence}});
        auto frame = probe.state.decision_cold[0].frame;
        frame.venue_sequence = VenueSequence{sequence};
        frame.source_sequence = SourceSequence{sequence};
        frame.raw_evidence_digest = evidence;
        require(run(probe.state, {{CallerId{8}, RequestSequence{++probe.request}, digest(0x31)},
                                  EvaluateDecisionFrame{DecisionId{31},
                                                        EvaluationMode::authoritative, frame}})
                        .code == ApplyCode::applied,
                "the second decision was not evaluated");
        const auto second =
            run(probe.state, {{CallerId{1}, RequestSequence{++probe.request}, digest(0x32)},
                              AuthorizeDecision{DecisionId{31}}});
        require(second.code == ApplyCode::venue_event_capacity_exceeded ||
                    second.code == ApplyCode::order_slot_occupied,
                "a second buy was authorized while the first was unknown");
        require(probe.state.order_count == harness.state.order_count &&
                    probe.state.risk_accounts[0].reserved_quantity.atoms() == kLots,
                "a refused second buy created an order or reserved risk");
    }

    GatewayReconciliationState reconciliation;
    const auto targets = plan_reconciliation(harness.state, reconciliation, delivery, control,
                                             view, timeouts, deadline);
    require(targets.query_count == 1 &&
                targets.to_query[0].cluster_order_id == effect.cluster_order_id &&
                targets.to_query[0].need == ReconciliationNeed::submission_unknown,
            "the unknown submission was not scheduled for a query");
    require(record_query(reconciliation, effect.cluster_order_id, deadline,
                         accepted_evidence_watermark(harness.state, effect.cluster_order_id)),
            "record_query failed");
    const auto answer = venue.query(effect.cluster_order_id, deadline);
    require(answer.outcome == QueryOutcome::known && answer.filled_quantity.atoms() == kLots &&
                answer.remaining_quantity.atoms() == 0 && answer.window_complete &&
                answer.ordered_after_relevant_sends,
            "the venue did not answer that the order filled whole");
    const VenueQueryView query_view{VenueQueryStatus::known, VenueSessionId{1},
                                    answer.venue_order_id, answer.filled_quantity,
                                    answer.remaining_quantity, false, true, true};
    require(classify_query(query_view, find_reconciliation(reconciliation, effect.cluster_order_id),
                           accepted_evidence_watermark(harness.state, effect.cluster_order_id),
                           timeouts) == QueryDisposition::submit,
            "a complete known answer was not submittable");
    require(record_answer(reconciliation, effect.cluster_order_id), "record_answer failed");

    auto snapshot = synthetic(harness, VenueObservationKind::order_reconciled, effect);
    snapshot.venue_order_id = answer.venue_order_id;
    snapshot.quantity = answer.filled_quantity;
    require(commit(harness, snapshot).code == ApplyCode::venue_observation_not_applicable,
            "a totals-only snapshot was booked as a fill");
    require_held_untouched(harness.state, effect.cluster_order_id, "snapshot");

    const auto adopted = drain(venue, harness, deadline + 5);
    require(adopted.size() == 2 && adopted[1].observation.kind == VenueObservationKind::fill &&
                adopted[1].observation.fee.atoms() == kBuyFee,
            "the query did not release the identified fill");
    for (const auto& each : adopted) {
        require(each.result.code == ApplyCode::applied, "the kernel refused adopted evidence");
    }
    require(order_of(harness.state, effect.cluster_order_id).state == ClusterOrderState::filled,
            "the adopted order did not reach filled");
    require(harness.state.risk_accounts[0].position_quantity.atoms() == kLots,
            "the adopted fill did not book exactly once");
    require_released(harness.state, effect.cluster_order_id, "adopt");
}

void test_fault_drop_request_unknown_then_not_found_releases_and_permits_retry() {
    Harness harness;
    PaperVenue venue(config(), only(&PaperFaultPolicy::drop_request_permille), 7);
    const auto effect = authorize_at(harness, 20, kBuyPrice);
    require(venue.send(effect, 10) == SendOutcome::ambiguous,
            "a lost request was not reported as ambiguous");
    require(venue.order_count() == 0 && venue.balances().quote.atoms() == kBudget,
            "a request that never arrived executed");
    require(commit(harness, synthetic(harness, VenueObservationKind::effect_unknown, effect))
                    .code == ApplyCode::applied,
            "the kernel refused the unknown-submission report");
    require_held_untouched(harness.state, effect.cluster_order_id, "unknown");

    const auto answer = venue.query(effect.cluster_order_id, 30);
    require(answer.outcome == QueryOutcome::not_found,
            "the venue did not answer that it never received the order");
    auto absent = synthetic(harness, VenueObservationKind::order_reconciled, effect);
    require(commit(harness, absent).code == ApplyCode::applied,
            "the kernel refused a definitive absence answer");
    require(order_of(harness.state, effect.cluster_order_id).state ==
                ClusterOrderState::rejected,
            "an order the venue never had did not resolve");
    require(harness.state.risk_accounts[0].position_quantity.atoms() == 0,
            "an absent order moved the position");
    require_released(harness.state, effect.cluster_order_id, "absent");

    PaperVenue healthy(config(), PaperFaultPolicy::clean(), 7);
    const auto retry = authorize_at(harness, 21, kBuyPrice);
    require(retry.cluster_order_id != effect.cluster_order_id,
            "the retry reused the unknown order's identity");
    require(healthy.send(retry, 40) == SendOutcome::accepted, "the retry was not accepted");
    (void)drain(healthy, harness, 100);
    require(harness.state.risk_accounts[0].position_quantity.atoms() == kLots,
            "the retry did not book exactly once");
}

void test_fault_reject_releases_the_reservation() {
    Harness harness;
    PaperVenue venue(config(), only(&PaperFaultPolicy::reject_permille), 7);
    const auto effect = authorize_at(harness, 20, kBuyPrice);
    require(venue.send(effect, 10) == SendOutcome::business_rejected,
            "the reject fault did not kill the order");
    require(venue.last_reject() == paper::PaperRejectReason::fault,
            "the fault kill gave the wrong reason");
    require(venue.order_count() == 0 &&
                venue.balances() == paper::PaperBalances{MoneyMicros::from_atoms(kBudget), {}, {}},
            "a killed order moved venue state");
    const auto delivered = drain(venue, harness, 100);
    require(delivered.size() == 1 &&
                delivered[0].observation.kind == VenueObservationKind::order_rejected &&
                delivered[0].result.code == ApplyCode::applied,
            "the kill was not reported and applied");
    require(order_of(harness.state, effect.cluster_order_id).state ==
                ClusterOrderState::rejected,
            "the killed order is not rejected");
    require(outbox_of(harness.state, effect.effect_id).state == OutboxState::rejected,
            "the killed submit effect was not settled as rejected");
    require(harness.state.risk_accounts[0].position_quantity.atoms() == 0,
            "a killed order moved the position");
    require_released(harness.state, effect.cluster_order_id, "reject");
    require(venue.query(effect.cluster_order_id, 200).outcome == QueryOutcome::not_found,
            "a killed order was reported as held by the venue");
}

void test_fault_split_fills_in_pieces_and_the_kernel_books_each() {
    Harness harness;
    PaperVenue venue(config(), only(&PaperFaultPolicy::split_permille), 7);
    const auto effect = authorize_at(harness, 20, kBuyPrice);
    require(venue.send(effect, 10) == SendOutcome::accepted, "the split buy was not accepted");
    std::array<VenueObservation, 8> out{};
    const auto count = venue.poll(out, 100);
    require(count == 3, "a two-piece split did not report ack + two fills");
    const auto& first = out[1];
    const auto& second = out[2];
    require(first.kind == VenueObservationKind::fill && second.kind == VenueObservationKind::fill,
            "the split reports were not fills");
    require(first.quantity.atoms() == 16 && second.quantity.atoms() == 17,
            "the split pieces are not 16 + 17");
    require(first.price.atoms() == kBuyPrice && second.price.atoms() == kBuyPrice,
            "a split piece filled away from the limit");
    require(first.fee.atoms() == 110'402 && second.fee.atoms() == 117'302,
            "a split piece carried the wrong fee");
    require(first.execution_id != second.execution_id, "split pieces share an execution id");

    require(commit(harness, out[0]).code == ApplyCode::applied, "the split ack was refused");
    require(commit(harness, first).code == ApplyCode::applied, "the first piece was refused");
    const auto& order = order_of(harness.state, effect.cluster_order_id);
    const auto& reservation = reservation_of(harness.state, effect.cluster_order_id);
    require(order.state == ClusterOrderState::partially_filled,
            "the first piece did not leave the order partially filled");
    require(reservation.state == ReservationState::partially_consumed &&
                reservation.remaining_quantity.atoms() == 17 &&
                reservation.margin_hold.atoms() == 10'200'170 &&
                reservation.fee_hold.atoms() == 122'403,
            "the first piece released the wrong share of the hold");
    require(harness.state.risk_accounts[0].position_quantity.atoms() == 16 &&
                harness.state.risk_accounts[0].reserved_quantity.atoms() == 17,
            "the first piece booked the wrong position");

    require(commit(harness, second).code == ApplyCode::applied, "the second piece was refused");
    require(order_of(harness.state, effect.cluster_order_id).state == ClusterOrderState::filled,
            "the split order did not end filled");
    require(harness.state.risk_accounts[0].position_quantity.atoms() == kLots,
            "the split pieces did not sum to the order");
    require_released(harness.state, effect.cluster_order_id, "split");
    require(venue.balances().fees_paid.atoms() == 227'704 &&
                venue.balances().quote.atoms() == 4'971'966,
            "the venue charged the split pieces the wrong total");
}

void test_fault_query_unavailable_concludes_nothing() {
    auto policy = only(&PaperFaultPolicy::drop_response_permille);
    policy.query_unavailable_permille = 1'000;
    PaperVenue venue(config(), policy, 7);
    const auto effect = raw_effect(1, Side::buy);
    require(venue.send(effect, 10) == SendOutcome::ambiguous, "the fixture send was not ambiguous");
    const auto answer = venue.query(effect.cluster_order_id, 30);
    require(venue.last_faults().query_unavailable, "the unavailable fault did not fire");
    require(answer.outcome == QueryOutcome::unavailable && answer.venue_order_id.value() == 0 &&
                answer.filled_quantity.atoms() == 0,
            "an unavailable query claimed a fact");
    std::array<VenueObservation, 8> out{};
    require(venue.poll(out, 1'000) == 0, "an unanswered query released evidence");
    const VenueQueryView view{VenueQueryStatus::unavailable, {}, {}, {}, {}, false, false, false};
    require(classify_query(view, nullptr, 0, GatewayTimeouts{}) == QueryDisposition::unavailable,
            "the gateway would submit an unavailable answer");
}

std::vector<std::uint64_t> script(const std::uint64_t seed) {
    PaperFaultPolicy policy;
    policy.delay_permille = 300;
    policy.duplicate_permille = 300;
    policy.reorder_permille = 300;
    policy.drop_request_permille = 150;
    policy.drop_response_permille = 150;
    policy.reject_permille = 200;
    policy.split_permille = 300;
    policy.query_unavailable_permille = 300;
    PaperVenue venue(config(10 * kBudget), policy, seed);
    std::vector<std::uint64_t> trace;
    std::array<VenueObservation, 8> out{};
    for (std::uint64_t index = 1; index <= 12; ++index) {
        const auto effect = raw_effect(index, Side::buy, kBuyPrice, 10);
        trace.push_back(static_cast<std::uint64_t>(venue.send(effect, index * 10)));
        const auto answer = venue.query(effect.cluster_order_id, index * 10 + 1);
        trace.push_back(static_cast<std::uint64_t>(answer.outcome));
        for (Tick now = index * 10; now < index * 10 + 10; ++now) {
            const auto count = venue.poll(std::span{out}.first(2), now);
            for (std::size_t slot = 0; slot < count; ++slot) {
                const auto fingerprint = observation_fingerprint(out[slot]);
                std::uint64_t word = 0;
                for (std::size_t byte = 0; byte < 8; ++byte) {
                    word = (word << 8) | static_cast<std::uint64_t>(fingerprint.bytes[byte]);
                }
                trace.push_back(now);
                trace.push_back(word);
                venue.acknowledge(out[slot]);
            }
        }
    }
    trace.push_back(static_cast<std::uint64_t>(venue.balances().quote.atoms()));
    return trace;
}

void test_fault_rates_map_to_the_policy_and_are_bounded() {
    using namespace velocita::v2::paper;
    const auto policy = paper_fault_policy({11, 22, 33, 44});
    require(policy && policy->reject_permille == 11 && policy->split_permille == 22 &&
                policy->drop_request_permille == 33 && policy->drop_response_permille == 44 &&
                policy->delay_permille == 0 && policy->duplicate_permille == 0 &&
                policy->reorder_permille == 0 && policy->query_unavailable_permille == 0 &&
                policy->split_pieces == 2,
            "fault rates did not map one to one onto the policy");
    const auto none = paper_fault_policy({});
    require(none && none->reject_permille == 0 && none->split_permille == 0 &&
                none->drop_request_permille == 0 && none->drop_response_permille == 0,
            "no fault rates did not give the clean policy");
    require(paper_fault_policy({1'000, 1'000, 1'000, 1'000}).has_value(), "a rate of 1000 was refused");
    require(!paper_fault_policy({1'001, 0, 0, 0}) && !paper_fault_policy({0, 1'001, 0, 0}) &&
                !paper_fault_policy({0, 0, 1'001, 0}) && !paper_fault_policy({0, 0, 0, 1'001}) &&
                !paper_fault_policy({0, 0, 0, 65'536 + 5}),
            "a rate above 1000 was accepted");
}

void test_same_seed_same_outputs() {
    const auto first = script(41);
    require(first == script(41), "the same seed and inputs produced different outputs");
    require(first != script(42), "the seed does not influence fault selection");
}

void test_bounded_state_refuses_before_mutation_and_poll_respects_the_caller() {
    PaperVenue venue(config(1'000 * kBudget), PaperFaultPolicy::clean(), 7);
    for (std::uint64_t index = 1; index <= 64; ++index) {
        require(venue.send(raw_effect(index, Side::buy, kBuyPrice, 1), 10) ==
                    SendOutcome::accepted,
                "an order within report capacity was refused");
    }
    const auto balances = venue.balances();
    require(venue.send(raw_effect(65, Side::buy, kBuyPrice, 1), 10) ==
                SendOutcome::pre_send_failure,
            "an order beyond report capacity was not refused");
    require(venue.balances() == balances && venue.order_count() == 64 &&
                venue.pending_observation_count() == paper::kMaxPaperObservations,
            "a capacity refusal mutated the venue");

    std::array<VenueObservation, 1> one{};
    require(venue.poll(one, 100) == 1, "poll did not fill the caller's span");
    venue.acknowledge(one[0]);
    require(venue.pending_observation_count() == paper::kMaxPaperObservations - 1,
            "acknowledge released more or fewer reports than it was handed");
    require(venue.send(raw_effect(65, Side::buy, kBuyPrice, 1), 10) ==
                SendOutcome::pre_send_failure,
            "an order needing two report slots was accepted into one");
    require(venue.balances() == balances && venue.order_count() == 64,
            "a one-slot-short refusal mutated the venue");
    require(venue.poll(one, 100) == 1, "the remainder was not offered again");
    venue.acknowledge(one[0]);
    require(venue.send(raw_effect(65, Side::buy, kBuyPrice, 1), 10) == SendOutcome::accepted,
            "freed report capacity was not reusable");
}

void test_restart_redelivers_unacknowledged_reports() {
    PaperVenue venue(config(), PaperFaultPolicy::clean(), 7);
    require(venue.send(raw_effect(1, Side::buy), 10) == SendOutcome::accepted, "send failed");
    std::array<VenueObservation, 4> out{};
    require(venue.poll(out, 100) == 2, "the fixture did not report twice");
    require(venue.poll(out, 100) == 0, "delivered reports were offered twice without restart");
    venue.restart_delivery();
    require(venue.poll(out, 100) == 2, "unacknowledged reports were lost across restart");
    venue.acknowledge(out[0]);
    venue.acknowledge(out[1]);
    venue.restart_delivery();
    require(venue.poll(out, 100) == 0, "acknowledged reports survived restart");
}

void test_transport_seam_drives_the_same_venue() {
    PaperVenue direct(config(), only(&PaperFaultPolicy::split_permille), 7);
    PaperVenue seamed(config(), only(&PaperFaultPolicy::split_permille), 7);
    const auto transport = paper::paper_venue_transport(seamed);
    const auto effect = raw_effect(1, Side::buy);
    require(direct.send(effect, 10) == transport.send(effect, 10), "the seam changed send");
    require(transport.session() == direct.session(), "the seam changed the session");
    std::array<VenueObservation, 8> left{};
    std::array<VenueObservation, 8> right{};
    const auto count = direct.poll(left, 100);
    require(count == 3 && transport.poll(right, 100) == count, "the seam changed poll");
    for (std::size_t index = 0; index < count; ++index) {
        require(observation_fingerprint(left[index]) == observation_fingerprint(right[index]),
                "the seam changed a report");
        transport.acknowledge(right[index]);
    }
    require(seamed.pending_observation_count() == 0, "the seam did not acknowledge");
    require(transport.query(effect.cluster_order_id, 100).filled_quantity.atoms() == kLots,
            "the seam changed query");
}

void test_kernel_resequenced_execution_report_is_not_rebooked() {
    Harness harness;
    PaperVenue venue(config(), PaperFaultPolicy::clean(), 7);
    const auto effect = authorize_at(harness, 20, kBuyPrice);
    require(venue.send(effect, 10) == SendOutcome::accepted, "the buy was not accepted");
    const auto delivered = drain(venue, harness, 100);
    auto resent = delivered[1].observation;
    resent.venue_sequence = VenueSequence{resent.venue_sequence.value() + 1'000};
    resent.evidence_digest = digest(0x99);
    const auto result = commit(harness, resent);
    require(result.code == ApplyCode::fill_identity_conflict,
            "a re-sequenced report of a booked execution was not refused");
    require(harness.state.risk_accounts[0].position_quantity.atoms() == kLots,
            "a re-sequenced execution report moved the position again");
}

}

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"fok_buy_fills_whole_at_limit_with_rounded_up_fee",
         test_fok_buy_fills_whole_at_limit_with_rounded_up_fee},
        {"round_trip_sell_uses_sell_side_cost_and_returns_flat",
         test_round_trip_sell_uses_sell_side_cost_and_returns_flat},
        {"budget_boundary_includes_the_fee", test_budget_boundary_includes_the_fee},
        {"long_only_sell_without_inventory_is_killed_whole",
         test_long_only_sell_without_inventory_is_killed_whole},
        {"only_fill_or_kill_submits_execute", test_only_fill_or_kill_submits_execute},
        {"cancel_is_refused_because_nothing_rests",
         test_cancel_is_refused_because_nothing_rests},
        {"resubmitting_a_known_order_is_a_venue_duplicate",
         test_resubmitting_a_known_order_is_a_venue_duplicate},
        {"unrenderable_or_misrouted_effects_never_reach_the_book",
         test_unrenderable_or_misrouted_effects_never_reach_the_book},
        {"fee_arithmetic_is_exact_and_rounds_against_us",
         test_fee_arithmetic_is_exact_and_rounds_against_us},
        {"fault_delay_withholds_reports_until_due", test_fault_delay_withholds_reports_until_due},
        {"fault_duplicate_moves_the_position_once", test_fault_duplicate_moves_the_position_once},
        {"fault_reorder_fill_before_ack_converges", test_fault_reorder_fill_before_ack_converges},
        {"a_late_reconciliation_that_agrees_with_a_finished_order_is_not_a_refusal",
         test_a_late_reconciliation_that_agrees_with_a_finished_order_is_not_a_refusal},
        {"fault_drop_response_unknown_then_query_adopts_the_fill",
         test_fault_drop_response_unknown_then_query_adopts_the_fill},
        {"fault_drop_request_unknown_then_not_found_releases_and_permits_retry",
         test_fault_drop_request_unknown_then_not_found_releases_and_permits_retry},
        {"fault_reject_releases_the_reservation", test_fault_reject_releases_the_reservation},
        {"fault_split_fills_in_pieces_and_the_kernel_books_each",
         test_fault_split_fills_in_pieces_and_the_kernel_books_each},
        {"fault_query_unavailable_concludes_nothing",
         test_fault_query_unavailable_concludes_nothing},
        {"fault_rates_map_to_the_policy_and_are_bounded", test_fault_rates_map_to_the_policy_and_are_bounded},
        {"same_seed_same_outputs", test_same_seed_same_outputs},
        {"bounded_state_refuses_before_mutation_and_poll_respects_the_caller",
         test_bounded_state_refuses_before_mutation_and_poll_respects_the_caller},
        {"restart_redelivers_unacknowledged_reports",
         test_restart_redelivers_unacknowledged_reports},
        {"transport_seam_drives_the_same_venue", test_transport_seam_drives_the_same_venue},
        {"kernel_resequenced_execution_report_is_not_rebooked",
         test_kernel_resequenced_execution_report_is_not_rebooked},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << (failures == 0 ? "all paper venue tests passed" : "paper venue tests failed")
              << '\n';
    return failures == 0 ? 0 : 1;
}
