#include "v2_catalog_test_support.hpp"
#include "v2_truemarkets_test_support.hpp"

#include "velocita/v2/digest.hpp"
#include "velocita/v2/gateway.hpp"
#include "velocita/v2/truemarkets_venue.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace velocita::v2;
using namespace velocita::v2::truemarkets;
using tm_test::FakeClock;
using tm_test::FakeTrueMarkets;
using tm_test::Script;
using tm_test::Settlement;
using tm_test::TestSigner;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Digest256 digest(const std::uint8_t marker) {
    Digest256 value;
    value.bytes[0] = static_cast<std::byte>(marker);
    return value;
}

constexpr std::int64_t kBuyLimit = 12'000;
constexpr std::int64_t kSellLimit = 11'900;
constexpr std::int64_t kLots = 20;
constexpr std::int64_t kBudget = 25'000'000;
constexpr std::int64_t kBuyNotional = 24'000'000;
constexpr std::int64_t kFeeHold = 288'000;
constexpr std::int64_t kFillTicks = 11'950;
constexpr std::int64_t kFee = 239'000;
constexpr std::int64_t kBuyDebit = 24'139'000;
constexpr std::int64_t kSellCredit = 23'661'000;
constexpr std::int64_t kBuyBound = 24'288'000;
constexpr std::int64_t kSellBound = 23'514'400;
constexpr Tick kLeaseBound = 1'000'000;

InstrumentExecutionProfile profile() {
    return {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4},
            InstrumentSpecVersion{7}, MoneyMicros::from_atoms(100),
            PriceTicks::from_atoms(1), PriceTicks::from_atoms(1'000'000),
            QuantityLots::from_atoms(1), QuantityLots::from_atoms(100'000)};
}

VenueCapabilities capabilities() {
    return {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
            kVenueDirectivePlaceLimit | kVenueDirectiveCancel, kVenueTifFok, 0};
}

VenueInstrumentSpec spec() { return {VenueId{9}, InstrumentId{4}, 10'000, 10'000'000, 9}; }

const VerifiedExecutionCatalog& tm_catalog() {
    static const auto value = [] {
        auto row = catalog_test::row(AccountId{5}, profile(), capabilities());
        row.tick_quote_micros = 10'000;
        row.lot_base_atoms = 10'000'000;
        row.base_decimals = 9;
        const std::array rows{row};
        return catalog_test::catalog(rows);
    }();
    return value;
}

TmText text(const std::string& value) {
    const auto made = tm_text(value);
    require(made.has_value(), "fixture text refused");
    return *made;
}

TrueMarketsVenueConfig config(const ExecutionFlow flow) {
    TrueMarketsVenueConfig value;
    value.spec = spec();
    value.profile = profile();
    value.flow = flow;
    value.chain = Chain::solana;
    value.base_asset = text("So11111111111111111111111111111111111111112");
    value.quote_asset = text("EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v");
    value.quote_decimals = 6;
    value.max_fee_bps = 120;
    value.quote_ttl = 2'000;
    value.status_interval = 100;
    value.order_rate = {10, 100};
    value.query_rate = {10, 100};
    value.caller = CallerId{22};
    value.session = VenueSessionId{1};
    return value;
}

struct Rig {
    FakeClock clock;
    FakeTrueMarkets venue;
    TestSigner signer;
    std::unique_ptr<TrueMarketsVenue> adapter;

    explicit Rig(const ExecutionFlow flow, void (*tune)(TrueMarketsVenueConfig&) = nullptr) {
        venue.clock = &clock;
        signer.clock = &clock;
        auto value = config(flow);
        if (tune) tune(value);
        venue.status_accepts_quote_id = value.status_accepts_quote_id;
        adapter = std::make_unique<TrueMarketsVenue>(value, venue.handle(), signer.handle(), clock.handle());
        require(adapter->configured(), "the adapter refused its configuration");
    }
    [[nodiscard]] std::uint64_t calls(const ApiCall call) const {
        return adapter->counters().api_calls[static_cast<std::size_t>(call)];
    }
};

PolicyArtifact tm_policy() {
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
    value.rules[0] = {0, PredicateOperator::greater_than_or_equal, FeatureMicros::from_atoms(kBuyLimit),
                      Directive::place_limit};
    value.rules[1] = {0, PredicateOperator::greater_than, FeatureMicros::from_atoms(0), Directive::place_limit};
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
    value.price_source = {EconomicValueSourceKind::feature, 0, RoundingMode::toward_zero, {}, 1, {}};
    value.quantity_source = {EconomicValueSourceKind::fixed, 0, RoundingMode::toward_zero,
                             QuantityLots::from_atoms(kLots), 0};
    value.safety_caps = {QuantityLots::from_atoms(kLots), MoneyMicros::from_atoms(kBudget),
                         PriceTicks::from_atoms(0), PriceTicks::from_atoms(0), 100, value.price_source};
    return value;
}

ExecutablePolicyArtifact executable_policy(const FeatureSchemaArtifact& schema) {
    ExecutablePolicyArtifact value;
    value.schema_version = kExecutablePolicyArtifactSchemaVersion;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.policy_artifact_digest = tm_policy().artifact_digest;
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
    return apply(state, command, scratch, &tm_catalog());
}

TradingState tm_state() {
    TradingState state = catalog_test::state(tm_catalog());
    state.strategy_mode = StrategyMode::normal;
    state.kill_epoch = KillEpoch{2};
    state.executor_generation = ExecutorGeneration{3};
    state.policy_count = 1;
    state.policies[0] = {tm_policy(), PolicyLifecycle::active};
    const auto schema = feature_schema();
    state.active_executable_policy = {true, schema, executable_policy(schema), profile(), capabilities(), 1, 2};
    state.source_count = 1;
    state.sources[0] = {SourceId{12}, SourceGeneration{1}, SourceSequence{1}};
    state.source_event_count = 1;
    state.source_events[0] = {SourceId{12}, SourceGeneration{1}, SourceSequence{1}, digest(0x44)};
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
    TradingState state = tm_state();
    std::uint64_t request{1'000};
    std::uint64_t source_sequence{1};
    std::uint64_t synthetic_sequence{900'000};
};

OrderEffect authorize_at(Harness& harness, const std::uint64_t decision, const std::int64_t price) {
    const auto sequence = ++harness.source_sequence;
    const auto evidence = digest(static_cast<std::uint8_t>(0x40 + sequence));
    require(run(harness.state, {{CallerId{7}, RequestSequence{++harness.request}, digest(0x21)},
                                RecordSourceEvent{SourceId{12}, SourceGeneration{1}, SourceSequence{sequence},
                                                  evidence}})
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
    require(run(harness.state, {{CallerId{8}, RequestSequence{++harness.request},
                                 digest(static_cast<std::uint8_t>(decision))},
                                EvaluateDecisionFrame{DecisionId{decision}, EvaluationMode::authoritative, frame}})
                    .code == ApplyCode::applied,
            "decision evaluation failed");
    const auto authorized = run(harness.state, {{CallerId{1}, RequestSequence{++harness.request},
                                                 digest(static_cast<std::uint8_t>(0x80 + decision))},
                                                AuthorizeDecision{DecisionId{decision}}});
    require(authorized.code == ApplyCode::applied, "authorization failed");
    const GatewayControlView control{harness.state.executor_generation, harness.state.kill_epoch,
                                     harness.state.strategy_mode, 0, kLeaseBound, 0};
    const auto candidate = evaluate_delivery_candidate(harness.state, authorized.effect_id(), control,
                                                       {VenueId{9}, VenueCapabilitiesVersion{8}, false, false},
                                                       SendQuotaView{4, 1}, &tm_catalog());
    require(candidate.permitted(), "the gateway refused to materialize the effect");
    return *candidate.effect;
}

ApplyResult commit(Harness& harness, const VenueObservation& observation) {
    const CommandIdentity identity{CallerId{2}, RequestSequence{++harness.request},
                                   observation_fingerprint(observation)};
    if (observation.kind == VenueObservationKind::fill) return run(harness.state, {identity, ObserveFill{observation}});
    return run(harness.state, {identity, ObserveOrderEvent{observation}});
}

struct Delivered {
    VenueObservation observation{};
    ApplyResult result{};
};

std::vector<Delivered> drain(TrueMarketsVenue& venue, Harness& harness, const Tick now) {
    std::array<VenueObservation, sim::kMaxVenueObservationsPerPoll> out{};
    const auto count = venue.poll(out, now);
    std::vector<Delivered> delivered;
    for (std::size_t i = 0; i < count; ++i) {
        delivered.push_back({out[i], commit(harness, out[i])});
        venue.acknowledge(out[i]);
    }
    return delivered;
}

VenueObservation synthetic(Harness& harness, const VenueObservationKind kind, const OrderEffect& effect) {
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

std::optional<ApplyResult> reconcile_answer(Harness& harness, const OrderEffect& effect, const QueryResult& answer) {
    if (answer.outcome == QueryOutcome::unavailable || answer.inconsistent) return std::nullopt;
    auto observation = synthetic(harness, VenueObservationKind::order_reconciled, effect);
    const bool known = answer.outcome == QueryOutcome::known;
    observation.venue_order_id = known ? answer.venue_order_id : VenueOrderId{};
    observation.quantity = known ? answer.filled_quantity : QuantityLots{};
    return commit(harness, observation);
}

const ClusterOrder& order_of(const TradingState& state, const ClusterOrderId id) {
    for (std::size_t i = 0; i < state.order_count; ++i)
        if (state.orders[i].cluster_order_id == id) return state.orders[i];
    throw std::runtime_error("order not found");
}

const RiskReservation& reservation_of(const TradingState& state, const ClusterOrderId id) {
    return state.reservations[order_of(state, id).reservation_index];
}

void require_released(const TradingState& state, const ClusterOrderId id, const std::string& where) {
    const auto& reservation = reservation_of(state, id);
    const auto& risk = state.risk_accounts[0];
    require(reservation.state == ReservationState::released && reservation.remaining_quantity.atoms() == 0 &&
                reservation.margin_hold.atoms() == 0 && reservation.fee_hold.atoms() == 0,
            where + ": the order's reservation was not released");
    require(risk.reserved_quantity.atoms() == 0 && risk.reserved_margin.atoms() == 0 &&
                risk.reserved_fee.atoms() == 0,
            where + ": the account still holds reserved risk");
}

void require_held_untouched(const TradingState& state, const ClusterOrderId id, const std::string& where) {
    const auto& reservation = reservation_of(state, id);
    require(reservation.state == ReservationState::held && reservation.remaining_quantity.atoms() == kLots &&
                reservation.margin_hold.atoms() == kBuyNotional && reservation.fee_hold.atoms() == kFeeHold,
            where + ": the reservation moved without evidence");
    require(state.risk_accounts[0].position_quantity.atoms() == 0, where + ": the position moved without evidence");
}

void require_single_fill(const std::vector<Delivered>& delivered, const std::string& where) {
    std::size_t fills = 0;
    for (const auto& each : delivered) {
        require(each.result.code == ApplyCode::applied, where + ": the kernel refused adapter evidence");
        if (each.observation.kind != VenueObservationKind::fill) continue;
        ++fills;
        require(each.observation.quantity.atoms() == kLots && each.observation.price.atoms() == kFillTicks &&
                    each.observation.fee.atoms() == kFee && each.observation.execution_id.value() != 0,
                where + ": the fill's quantity, price or fee differs from the venue's execution");
    }
    require(fills == 1, where + ": expected exactly one fill");
}

void settle_buy(Rig& rig, Harness& harness, const OrderEffect& effect, const std::string& where,
                const Tick now = 1'000) {
    const auto delivered = drain(*rig.adapter, harness, now);
    require_single_fill(delivered, where);
    require(order_of(harness.state, effect.cluster_order_id).state == ClusterOrderState::filled,
            where + ": the order did not reach filled");
    require(harness.state.risk_accounts[0].position_quantity.atoms() == kLots,
            where + ": the fill did not book exactly once");
    require_released(harness.state, effect.cluster_order_id, where);
    require(rig.venue.base_balance == 200'000'000 && rig.venue.quote_balance == kBudget - kBuyDebit,
            where + ": the venue's own ledger disagrees with one 0.2 SOL buy");
}

void test_quote_flow_buy_signs_the_quoted_payload_and_books_once() {
    Rig rig(ExecutionFlow::quote_then_execute);
    Harness harness;
    const auto effect = authorize_at(harness, 20, kBuyLimit);
    require(rig.adapter->send(effect, 10) == SendOutcome::accepted, "a clean buy was not accepted");
    require(rig.venue.calls == std::vector<ApiCall>{ApiCall::get_quote, ApiCall::trade_execute},
            "the send did not go quote -> execute");
    const std::string expected_quote =
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_quote","arguments":)"
        R"({"chain":"solana","side":"buy","base_asset":"So11111111111111111111111111111111111111112",)"
        R"("qty":"0.2","qty_unit":"base"}}})";
    require(rig.venue.requests[0] == expected_quote, "quote request differs: " + rig.venue.requests[0]);
    require(rig.signer.calls == 1, "the signer was not asked exactly once");
    const auto& intent = rig.signer.intents[0];
    require(intent.effect_id == effect.effect_id && intent.side == Side::buy && intent.base_atoms == 200'000'000 &&
                intent.quote_bound_atoms == kBuyBound && intent.caller == CallerId{22} &&
                intent.policy_version == PolicyVersion{1} && intent.effect_payload_digest == effect.payload_digest,
            "the signing intent is not the committed effect");
    require(rig.signer.audit.size() == 1 && rig.signer.audit[0].verdict == SignerVerdict::signed_ok,
            "the signer did not audit its signature");
    require(rig.venue.received_signatures.size() == 1 &&
                rig.venue.received_signatures[0].find(tm_test::sha_hex(
                    "velocita-test-signer/v1|test-key-1|" + rig.venue.payload_for(rig.venue.swaps.begin()->second))) !=
                    std::string::npos,
            "the venue did not receive the signature of the quoted payload");
    require(rig.adapter->find_order(effect.cluster_order_id)->signature_id == rig.signer.audit[0].signature_id,
            "the adapter did not keep the signature id for the audit join");
    settle_buy(rig, harness, effect, "quote flow");
}

void test_prepare_flow_round_trip_sell_uses_the_min_credit_bound() {
    Rig rig(ExecutionFlow::prepare_then_execute);
    Harness harness;
    const auto buy = authorize_at(harness, 20, kBuyLimit);
    require(rig.adapter->send(buy, 10) == SendOutcome::accepted, "the buy was not accepted");
    require(rig.venue.calls == std::vector<ApiCall>{ApiCall::trade_prepare, ApiCall::trade_execute},
            "the send did not go prepare -> execute");
    settle_buy(rig, harness, buy, "prepare buy");

    rig.clock.now = 2'000;
    const auto sell = authorize_at(harness, 21, kSellLimit);
    require(sell.side == Side::sell, "the fixture did not authorize a sell");
    require(rig.adapter->send(sell, 2'000) == SendOutcome::accepted, "the sell was not accepted");
    require(rig.signer.intents.back().quote_bound_atoms == kSellBound,
            "the sell's signer bound is not the minimum net credit");
    const auto delivered = drain(*rig.adapter, harness, 3'000);
    std::size_t fills = 0;
    for (const auto& each : delivered) {
        require(each.result.code == ApplyCode::applied, "the kernel refused sell evidence");
        if (each.observation.kind == VenueObservationKind::fill) {
            ++fills;
            require(each.observation.price.atoms() == kFillTicks && each.observation.fee.atoms() == kFee,
                    "the sell fill's price or fee differs");
        }
    }
    require(fills == 1 && harness.state.risk_accounts[0].position_quantity.atoms() == 0,
            "the round trip did not return flat");
    require(rig.venue.base_balance == 0 && rig.venue.quote_balance == kBudget - kBuyDebit + kSellCredit,
            "the venue ledger disagrees with the round trip");
}

void test_quote_expired_before_submit_sends_nothing_and_permits_a_fresh_quote() {
    Rig rig(ExecutionFlow::quote_then_execute);
    Harness harness;
    const auto effect = authorize_at(harness, 20, kBuyLimit);
    rig.signer.latency = 2'001;
    require(rig.adapter->send(effect, 10) == SendOutcome::pre_send_failure,
            "an expired quote was not a pre-send failure");
    require(rig.calls(ApiCall::trade_execute) == 0, "an expired quote was submitted");
    require(rig.adapter->query(effect.cluster_order_id, rig.clock.now).outcome == QueryOutcome::not_found,
            "an order that never reached a committal call was not definitively absent");

    rig.signer.latency = 0;
    require(rig.adapter->send(effect, rig.clock.now) == SendOutcome::accepted,
            "the redelivered effect was not re-quoted after a proven non-submission");
    require(rig.calls(ApiCall::get_quote) == 2 && rig.calls(ApiCall::trade_execute) == 1,
            "the retry did not take a fresh quote and submit once");
    settle_buy(rig, harness, effect, "after expiry", 5'000);
}

void test_submit_timeout_is_ambiguous_and_never_resent() {
    Rig rig(ExecutionFlow::prepare_then_execute);
    Harness harness;
    const auto effect = authorize_at(harness, 20, kBuyLimit);
    rig.venue.scripts[ApiCall::trade_execute] = {Script::lost_reply};
    require(rig.adapter->send(effect, 10) == SendOutcome::ambiguous, "a lost submit reply was not ambiguous");
    require(rig.adapter->send(effect, 20) == SendOutcome::venue_duplicate,
            "a second send of a possibly-submitted effect was not refused");
    require(rig.calls(ApiCall::trade_execute) == 1 && rig.calls(ApiCall::trade_prepare) == 1 &&
                rig.adapter->counters().duplicate_sends_refused == 1,
            "the adapter called the venue again for a possibly-submitted effect");

    require(commit(harness, synthetic(harness, VenueObservationKind::effect_unknown, effect)).code ==
                ApplyCode::applied,
            "the kernel refused the unknown-submission report");
    require_held_untouched(harness.state, effect.cluster_order_id, "unknown");

    const auto answer = rig.adapter->query(effect.cluster_order_id, 500);
    require(answer.outcome == QueryOutcome::known && answer.filled_quantity.atoms() == kLots &&
                answer.remaining_quantity.atoms() == 0 && !answer.inconsistent,
            "the query did not report the settled swap");
    const auto delivered = drain(*rig.adapter, harness, 600);
    require_single_fill(delivered, "adopt");
    require(order_of(harness.state, effect.cluster_order_id).state == ClusterOrderState::filled &&
                harness.state.risk_accounts[0].position_quantity.atoms() == kLots,
            "the adopted swap did not book exactly once");
    require_released(harness.state, effect.cluster_order_id, "adopt");
    require(rig.calls(ApiCall::trade_execute) == 1, "reconciliation resubmitted");
}

void test_submit_timeout_not_executed_is_released_and_retried_as_a_new_order() {
    Rig rig(ExecutionFlow::prepare_then_execute);
    Harness harness;
    const auto effect = authorize_at(harness, 20, kBuyLimit);
    rig.venue.scripts[ApiCall::trade_execute] = {Script::lost_request};
    require(rig.adapter->send(effect, 10) == SendOutcome::ambiguous, "a lost submit request was not ambiguous");
    require(commit(harness, synthetic(harness, VenueObservationKind::effect_unknown, effect)).code ==
                ApplyCode::applied,
            "unknown not applied");
    const auto answer = rig.adapter->query(effect.cluster_order_id, 500);
    require(answer.outcome == QueryOutcome::not_found, "a venue-reported non-execution was not an absence");
    const auto reconciled = reconcile_answer(harness, effect, answer);
    require(reconciled && reconciled->code == ApplyCode::applied &&
                order_of(harness.state, effect.cluster_order_id).state == ClusterOrderState::rejected,
            "the absent order did not resolve");
    require_released(harness.state, effect.cluster_order_id, "absent");
    require(rig.venue.base_balance == 0 && rig.venue.quote_balance == kBudget, "the venue moved money");

    const auto retry = authorize_at(harness, 21, kBuyLimit);
    require(retry.cluster_order_id != effect.cluster_order_id && retry.effect_id != effect.effect_id,
            "the retry reused the unknown order's identity");
    require(rig.adapter->send(retry, 1'000) == SendOutcome::accepted, "the fresh order was not accepted");
    require(rig.calls(ApiCall::trade_execute) == 2, "expected one submission per order");
    const auto delivered = drain(*rig.adapter, harness, 2'000);
    require_single_fill(delivered, "retry");
}

void test_quote_flow_lost_submit_without_queryable_id_stays_unknown() {
    Rig rig(ExecutionFlow::quote_then_execute);
    Harness harness;
    const auto effect = authorize_at(harness, 20, kBuyLimit);
    rig.venue.scripts[ApiCall::trade_execute] = {Script::lost_reply};
    require(rig.adapter->send(effect, 10) == SendOutcome::ambiguous, "a lost submit reply was not ambiguous");
    require(commit(harness, synthetic(harness, VenueObservationKind::effect_unknown, effect)).code ==
                ApplyCode::applied,
            "unknown not applied");
    for (Tick now = 500; now < 5'000; now += 500) {
        require(rig.adapter->query(effect.cluster_order_id, now).outcome == QueryOutcome::unavailable,
                "an unqueryable submission was reported as a fact");
        require(drain(*rig.adapter, harness, now).empty(), "evidence appeared from nowhere");
    }
    require(rig.calls(ApiCall::order_status) == 0, "a status call was made with an id it may not accept");
    require_held_untouched(harness.state, effect.cluster_order_id, "unqueryable");
    require(rig.adapter->send(effect, 6'000) == SendOutcome::venue_duplicate &&
                rig.calls(ApiCall::trade_execute) == 1,
            "an unknown submission licensed a fresh send");
}

void test_quote_flow_lost_submit_adopts_when_status_accepts_the_quote_id() {
    Rig rig(ExecutionFlow::quote_then_execute, [](TrueMarketsVenueConfig& value) { value.status_accepts_quote_id = true; });
    Harness harness;
    const auto effect = authorize_at(harness, 20, kBuyLimit);
    rig.venue.scripts[ApiCall::trade_execute] = {Script::lost_reply};
    require(rig.adapter->send(effect, 10) == SendOutcome::ambiguous, "a lost submit reply was not ambiguous");
    require(commit(harness, synthetic(harness, VenueObservationKind::effect_unknown, effect)).code ==
                ApplyCode::applied,
            "unknown not applied");
    const auto answer = rig.adapter->query(effect.cluster_order_id, 500);
    require(answer.outcome == QueryOutcome::known && answer.filled_quantity.atoms() == kLots,
            "the quote-id status did not report the settled swap");
    require_single_fill(drain(*rig.adapter, harness, 600), "quote-id adopt");
    require_released(harness.state, effect.cluster_order_id, "quote-id adopt");
}

void test_failed_settlement_after_acceptance_releases_through_reconciliation() {
    Rig rig(ExecutionFlow::prepare_then_execute);
    Harness harness;
    rig.venue.next_settlement = Settlement::failed;
    const auto effect = authorize_at(harness, 20, kBuyLimit);
    require(rig.adapter->send(effect, 10) == SendOutcome::accepted, "the swap was not accepted");
    const auto delivered = drain(*rig.adapter, harness, 1'000);
    require(delivered.size() == 1 && delivered[0].observation.kind == VenueObservationKind::order_acknowledged &&
                delivered[0].result.code == ApplyCode::applied,
            "a failed settlement produced more than the acknowledgement");
    require(order_of(harness.state, effect.cluster_order_id).state == ClusterOrderState::live,
            "the accepted swap is not live");
    require(reservation_of(harness.state, effect.cluster_order_id).state == ReservationState::held,
            "the hold was released before settlement was known");
    const auto answer = rig.adapter->query(effect.cluster_order_id, 1'100);
    require(answer.outcome == QueryOutcome::not_found, "a failed on-chain settlement was not an absence");
    const auto reconciled = reconcile_answer(harness, effect, answer);
    require(reconciled && reconciled->code == ApplyCode::applied &&
                order_of(harness.state, effect.cluster_order_id).state == ClusterOrderState::cancelled,
            "the failed swap did not resolve");
    require_released(harness.state, effect.cluster_order_id, "failed settlement");
    require(harness.state.risk_accounts[0].position_quantity.atoms() == 0, "a failed swap moved the position");
}

void test_partial_settlement_books_the_evidence_and_is_a_conflict() {
    Rig rig(ExecutionFlow::prepare_then_execute);
    Harness harness;
    rig.venue.next_settlement = Settlement::partial;
    const auto effect = authorize_at(harness, 20, kBuyLimit);
    require(rig.adapter->send(effect, 10) == SendOutcome::accepted, "the swap was not accepted");
    const auto delivered = drain(*rig.adapter, harness, 1'000);
    std::int64_t booked = 0;
    for (const auto& each : delivered) {
        require(each.result.code == ApplyCode::applied, "the kernel refused partial evidence");
        if (each.observation.kind == VenueObservationKind::fill) booked += each.observation.quantity.atoms();
    }
    require(booked == 10 && harness.state.risk_accounts[0].position_quantity.atoms() == 10,
            "the partial settlement was not booked as reported");
    const auto answer = rig.adapter->query(effect.cluster_order_id, 1'100);
    require(answer.inconsistent, "a partial settlement of a fill-or-kill swap was not a conflict");
    require(rig.adapter->counters().breaches == 1, "the breach was not counted");
    require(reservation_of(harness.state, effect.cluster_order_id).remaining_quantity.atoms() == 10 &&
                reservation_of(harness.state, effect.cluster_order_id).state != ReservationState::released,
            "the unexplained remainder was released");
}

void test_rate_limits_local_and_venue() {
    {
        Rig rig(ExecutionFlow::quote_then_execute,
                [](TrueMarketsVenueConfig& value) { value.order_rate = {2, 1'000}; });
        Harness harness;
        const auto effect = authorize_at(harness, 20, kBuyLimit);
        rig.venue.scripts[ApiCall::get_quote] = {Script::tool_error};
        require(rig.adapter->send(effect, 10) == SendOutcome::pre_send_failure, "a quote error was not pre-send");
        const auto before = rig.venue.calls.size();
        require(rig.adapter->send(effect, 10) == SendOutcome::rate_limited, "an empty bucket was not rate-limited");
        require(rig.venue.calls.size() == before && rig.adapter->counters().local_rate_limited == 1,
                "a locally rate-limited send reached the venue");
        require(rig.adapter->send(effect, 10 + 2'000) == SendOutcome::accepted, "the bucket did not refill");
    }
    {
        Rig rig(ExecutionFlow::quote_then_execute);
        Harness harness;
        const auto effect = authorize_at(harness, 20, kBuyLimit);
        rig.venue.scripts[ApiCall::get_quote] = {Script::rate_limited};
        require(rig.adapter->send(effect, 10) == SendOutcome::rate_limited,
                "a paced non-committal quote was not rate-limited");
        require(rig.adapter->query(effect.cluster_order_id, 20).outcome == QueryOutcome::not_found,
                "a paced quote left doubt about execution");
    }
    {
        Rig rig(ExecutionFlow::quote_then_execute);
        Harness harness;
        const auto effect = authorize_at(harness, 20, kBuyLimit);
        rig.venue.scripts[ApiCall::trade_execute] = {Script::rate_limited};
        require(rig.adapter->send(effect, 10) == SendOutcome::ambiguous,
                "a rate-limit label on the committal call was trusted as proof of no submission");
    }
    {
        Rig rig(ExecutionFlow::prepare_then_execute);
        Harness harness;
        const auto effect = authorize_at(harness, 20, kBuyLimit);
        rig.venue.scripts[ApiCall::trade_prepare] = {Script::rate_limited};
        require(rig.adapter->send(effect, 10) == SendOutcome::ambiguous,
                "a rate-limit label on a possibly-submitting prepare was trusted");
        require(rig.adapter->query(effect.cluster_order_id, 20).outcome == QueryOutcome::unavailable,
                "an uncertain prepare without an order id was answered");
    }
}

void test_prepare_direct_submission_is_unknown_and_adopted_by_status() {
    Rig rig(ExecutionFlow::prepare_then_execute);
    Harness harness;
    const auto effect = authorize_at(harness, 20, kBuyLimit);
    rig.venue.scripts[ApiCall::trade_prepare] = {Script::direct_submission};
    require(rig.adapter->send(effect, 10) == SendOutcome::ambiguous,
            "a direct submission (no payloads, no bound) was not treated as unknown exposure");
    require(rig.signer.calls == 0 && rig.calls(ApiCall::trade_execute) == 0,
            "the adapter tried to sign or execute a directly submitted order");
    require(rig.adapter->send(effect, 20) == SendOutcome::venue_duplicate && rig.calls(ApiCall::trade_prepare) == 1,
            "a directly submitted order was prepared again");
    require_single_fill(drain(*rig.adapter, harness, 1'000), "direct");
    require(harness.state.risk_accounts[0].position_quantity.atoms() == kLots, "the direct fill was not booked once");
}

void test_prepare_lost_reply_is_unknown_not_pre_send() {
    Rig rig(ExecutionFlow::prepare_then_execute);
    Harness harness;
    const auto effect = authorize_at(harness, 20, kBuyLimit);
    rig.venue.scripts[ApiCall::trade_prepare] = {Script::lost_reply};
    require(rig.adapter->send(effect, 10) == SendOutcome::ambiguous,
            "a lost prepare reply was reported as a pre-send failure");
    require(rig.adapter->query(effect.cluster_order_id, 500).outcome == QueryOutcome::unavailable,
            "an order with no known id was answered");
    require(rig.adapter->send(effect, 600) == SendOutcome::venue_duplicate && rig.calls(ApiCall::trade_prepare) == 1,
            "an uncertain prepare licensed a second prepare");
}

void test_quote_beyond_limit_or_fee_cap_is_killed_before_signing() {
    for (const auto& [price, fee_bps, where] :
         {std::tuple{std::int64_t{121'000'000}, std::int64_t{100}, "price above limit"},
          std::tuple{std::int64_t{119'500'000}, std::int64_t{150}, "fee above cap"}}) {
        Rig rig(ExecutionFlow::quote_then_execute);
        Harness harness;
        rig.venue.price_micros = price;
        rig.venue.fee_bps = fee_bps;
        const auto effect = authorize_at(harness, 20, kBuyLimit);
        require(rig.adapter->send(effect, 10) == SendOutcome::business_rejected,
                std::string(where) + ": an unfillable fill-or-kill was not killed");
        require(rig.signer.calls == 0 && rig.calls(ApiCall::trade_execute) == 0,
                std::string(where) + ": the adapter signed or submitted it anyway");
        const auto delivered = drain(*rig.adapter, harness, 100);
        require(delivered.size() == 1 && delivered[0].observation.kind == VenueObservationKind::order_rejected &&
                    delivered[0].result.code == ApplyCode::applied,
                std::string(where) + ": the kill was not reported and applied");
        require_released(harness.state, effect.cluster_order_id, where);
    }
}

void test_venue_issue_is_killed_before_signing() {
    Rig rig(ExecutionFlow::quote_then_execute);
    Harness harness;
    rig.venue.reply_issue = "Insufficient balance";
    const auto effect = authorize_at(harness, 20, kBuyLimit);
    require(rig.adapter->send(effect, 10) == SendOutcome::business_rejected, "a quote with an issue was not killed");
    require(rig.signer.calls == 0, "a quote with an issue was signed");
}

void test_signer_refuses_a_payload_that_exceeds_the_authorization() {
    Rig rig(ExecutionFlow::quote_then_execute);
    Harness harness;
    rig.venue.payload_bound_delta = kBuyBound - kBuyDebit + 1;
    const auto effect = authorize_at(harness, 20, kBuyLimit);
    require(rig.adapter->send(effect, 10) == SendOutcome::pre_send_failure,
            "a signer refusal was not a pre-send failure");
    require(rig.signer.audit.size() == 1 && rig.signer.audit[0].verdict == SignerVerdict::refused_mismatch,
            "the signer did not refuse the over-wide payload");
    require(rig.calls(ApiCall::trade_execute) == 0, "an unsigned order was submitted");

    Rig exact(ExecutionFlow::quote_then_execute);
    Harness second;
    exact.venue.payload_bound_delta = kBuyBound - kBuyDebit;
    const auto at_bound = authorize_at(second, 20, kBuyLimit);
    require(exact.adapter->send(at_bound, 10) == SendOutcome::accepted, "a payload at the bound was refused");
}

void test_quote_identity_mismatch_is_never_signed() {
    const std::array<void (*)(FakeTrueMarkets&), 4> tamper{
        [](FakeTrueMarkets& venue) { venue.reply_side_override = "sell"; },
        [](FakeTrueMarkets& venue) { venue.reply_base_override = "OtherMint1111"; },
        [](FakeTrueMarkets& venue) { venue.reply_qty_override = "0.3"; },
        [](FakeTrueMarkets& venue) { venue.reply_without_payloads = true; },
    };
    for (std::size_t i = 0; i < tamper.size(); ++i) {
        Rig rig(ExecutionFlow::quote_then_execute);
        Harness harness;
        tamper[i](rig.venue);
        const auto effect = authorize_at(harness, 20, kBuyLimit);
        require(rig.adapter->send(effect, 10) == SendOutcome::pre_send_failure,
                "tamper " + std::to_string(i) + ": a quote for something else was not refused");
        require(rig.signer.calls == 0 && rig.calls(ApiCall::trade_execute) == 0,
                "tamper " + std::to_string(i) + ": a quote for something else reached the signer");
    }
}

void test_unknown_order_query_is_silence_not_absence() {
    Rig rig(ExecutionFlow::prepare_then_execute);
    const auto answer = rig.adapter->query(ClusterOrderId{4'242}, 10);
    require(answer.outcome == QueryOutcome::unavailable, "an order this process never saw was declared absent");
    require(rig.venue.calls.empty(), "a query for an unknown order reached the venue");
}

void test_cancel_is_refused_because_a_swap_cannot_be_cancelled() {
    Rig rig(ExecutionFlow::quote_then_execute);
    Harness harness;
    const auto buy = authorize_at(harness, 20, kBuyLimit);
    require(rig.adapter->send(buy, 10) == SendOutcome::accepted, "the buy was not accepted");
    const auto calls_before = rig.venue.calls.size();
    auto cancel = buy;
    cancel.effect_id = EffectId{9'999};
    cancel.operation = EffectOperation::cancel;
    cancel.price = {};
    cancel.quantity = {};
    require(rig.adapter->send(cancel, 11) == SendOutcome::business_rejected, "a cancel was not refused");
    require(rig.venue.calls.size() == calls_before, "a cancel reached the venue");
    std::array<VenueObservation, 4> out{};
    const auto count = rig.adapter->poll(out, 12);
    bool reported = false;
    for (std::size_t i = 0; i < count; ++i)
        reported |= out[i].kind == VenueObservationKind::order_rejected && out[i].effect_id == cancel.effect_id;
    require(reported, "the refused cancel was not reported against the cancel effect");
    require(rig.adapter->find_order(buy.cluster_order_id)->phase == TmPhase::accepted,
            "refusing the cancel disturbed the swap it named");
}

OrderEffect raw_buy(const std::uint64_t id) {
    OrderEffect effect;
    effect.effect_id = EffectId{10'000 + id};
    effect.cluster_order_id = ClusterOrderId{20'000 + id};
    effect.committed_directive_index = 1;
    effect.executor_generation = ExecutorGeneration{3};
    effect.kill_epoch = KillEpoch{2};
    effect.venue_id = VenueId{9};
    effect.account_id = AccountId{5};
    effect.instrument_id = InstrumentId{4};
    effect.operation = EffectOperation::submit;
    effect.side = Side::buy;
    effect.price = PriceTicks::from_atoms(kBuyLimit);
    effect.quantity = QuantityLots::from_atoms(kLots);
    effect.time_in_force = TimeInForce::fill_or_kill;
    effect.policy_id = PolicyId{10};
    effect.policy_version = PolicyVersion{1};
    effect.decision_id = DecisionId{id + 1};
    effect.risk_version = RiskVersion{1};
    effect.source_generation = SourceGeneration{1};
    effect.source_sequence = SourceSequence{1};
    effect.payload_digest = digest(0x77);
    return effect;
}

void test_bounded_order_table_never_forgets_a_possible_submission() {
    Rig rig(ExecutionFlow::prepare_then_execute,
            [](TrueMarketsVenueConfig& value) { value.order_rate = {1'000, 1}; });
    for (std::uint64_t i = 0; i < kMaxTmOrders; ++i) {
        rig.venue.scripts[ApiCall::trade_prepare] = {Script::lost_request};
        require(rig.adapter->send(raw_buy(i), 10 + i) == SendOutcome::ambiguous, "filling the table failed");
    }
    const auto before = rig.venue.calls.size();
    require(rig.adapter->send(raw_buy(999), 1'000) == SendOutcome::pre_send_failure,
            "a table full of possible submissions accepted another order");
    require(rig.venue.calls.size() == before, "a full table still called the venue");
    require(rig.adapter->send(raw_buy(0), 1'001) == SendOutcome::venue_duplicate,
            "a possible submission was forgotten");

    Rig reuse(ExecutionFlow::quote_then_execute,
              [](TrueMarketsVenueConfig& value) { value.order_rate = {1'000, 1}; });
    for (std::uint64_t i = 0; i < kMaxTmOrders; ++i) {
        reuse.venue.scripts[ApiCall::get_quote] = {Script::tool_error};
        require(reuse.adapter->send(raw_buy(i), 10 + i) == SendOutcome::pre_send_failure, "filling failed");
    }
    require(reuse.adapter->send(raw_buy(999), 1'000) == SendOutcome::accepted,
            "records proving non-submission were not reclaimed");
}

void test_live_arming_refuses_without_both_and_without_a_compiled_transport() {
    require(evaluate_live_arming({false, ""}) == LiveArmingRefusal::not_armed, "unarmed start not refused");
    require(evaluate_live_arming({false, "/run/signer.sock"}) == LiveArmingRefusal::not_armed,
            "a signer alone armed live trading");
    require(evaluate_live_arming({true, ""}) == LiveArmingRefusal::no_signer, "arming without a signer accepted");
    require(evaluate_live_arming({true, "/run/signer.sock"}) == LiveArmingRefusal::no_live_transport,
            "this build claims a live transport");
    require(!kLiveVenueTransportCompiled, "a live transport is compiled into this build");
    require(evaluate_live_arming({true, "/run/signer.sock"}, true) == LiveArmingRefusal::none,
            "arming with both was refused by a live-capable build");
    require(evaluate_live_arming({false, "/run/signer.sock"}, true) == LiveArmingRefusal::not_armed &&
                evaluate_live_arming({true, ""}, true) == LiveArmingRefusal::no_signer,
            "a live-capable build started without both");
}

void test_configuration_is_never_defaulted() {
    FakeClock clock;
    FakeTrueMarkets venue;
    TestSigner signer;
    const std::array<void (*)(TrueMarketsVenueConfig&), 7> breaks{
        [](TrueMarketsVenueConfig& value) { value.flow = ExecutionFlow::unset; },
        [](TrueMarketsVenueConfig& value) { value.chain = Chain::unset; },
        [](TrueMarketsVenueConfig& value) { value.quote_asset = {}; },
        [](TrueMarketsVenueConfig& value) { value.max_fee_bps = 0; },
        [](TrueMarketsVenueConfig& value) { value.quote_ttl = 0; },
        [](TrueMarketsVenueConfig& value) { value.order_rate = {1, 100}; },
        [](TrueMarketsVenueConfig& value) { value.spec.lot_base_atoms = 1; },
    };
    for (std::size_t i = 0; i < breaks.size(); ++i) {
        auto value = config(ExecutionFlow::quote_then_execute);
        breaks[i](value);
        const auto adapter = std::make_unique<TrueMarketsVenue>(value, venue.handle(), signer.handle(), clock.handle());
        require(!adapter->configured(), "configuration " + std::to_string(i) + " was accepted");
        Harness harness;
        const auto effect = authorize_at(harness, 20, kBuyLimit);
        require(adapter->send(effect, 10) == SendOutcome::pre_send_failure && venue.calls.empty(),
                "an unconfigured adapter reached the venue");
    }
}

void test_transport_seam_drives_the_same_adapter() {
    Rig rig(ExecutionFlow::quote_then_execute);
    Harness harness;
    const auto transport = truemarkets_venue_transport(*rig.adapter);
    const auto effect = authorize_at(harness, 20, kBuyLimit);
    require(transport.send(effect, 10) == SendOutcome::accepted, "the seam did not send");
    std::array<VenueObservation, 8> out{};
    const auto count = transport.poll(out, 1'000);
    require(count == 2 && out[0].kind == VenueObservationKind::order_acknowledged &&
                out[1].kind == VenueObservationKind::fill,
            "the seam did not deliver ack then fill");
    transport.acknowledge(out[0]);
    transport.acknowledge(out[1]);
    require(rig.adapter->pending_observation_count() == 0, "acknowledged observations were retained");
    require(transport.query(effect.cluster_order_id, 1'100).outcome == QueryOutcome::known,
            "the seam did not query");
    require(transport.session() == VenueSessionId{1}, "session identity differs");
}

}

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"quote_flow_buy_signs_the_quoted_payload_and_books_once",
         test_quote_flow_buy_signs_the_quoted_payload_and_books_once},
        {"prepare_flow_round_trip_sell_uses_the_min_credit_bound",
         test_prepare_flow_round_trip_sell_uses_the_min_credit_bound},
        {"quote_expired_before_submit_sends_nothing_and_permits_a_fresh_quote",
         test_quote_expired_before_submit_sends_nothing_and_permits_a_fresh_quote},
        {"submit_timeout_is_ambiguous_and_never_resent", test_submit_timeout_is_ambiguous_and_never_resent},
        {"submit_timeout_not_executed_is_released_and_retried_as_a_new_order",
         test_submit_timeout_not_executed_is_released_and_retried_as_a_new_order},
        {"quote_flow_lost_submit_without_queryable_id_stays_unknown",
         test_quote_flow_lost_submit_without_queryable_id_stays_unknown},
        {"quote_flow_lost_submit_adopts_when_status_accepts_the_quote_id",
         test_quote_flow_lost_submit_adopts_when_status_accepts_the_quote_id},
        {"failed_settlement_after_acceptance_releases_through_reconciliation",
         test_failed_settlement_after_acceptance_releases_through_reconciliation},
        {"partial_settlement_books_the_evidence_and_is_a_conflict",
         test_partial_settlement_books_the_evidence_and_is_a_conflict},
        {"rate_limits_local_and_venue", test_rate_limits_local_and_venue},
        {"prepare_direct_submission_is_unknown_and_adopted_by_status",
         test_prepare_direct_submission_is_unknown_and_adopted_by_status},
        {"prepare_lost_reply_is_unknown_not_pre_send", test_prepare_lost_reply_is_unknown_not_pre_send},
        {"quote_beyond_limit_or_fee_cap_is_killed_before_signing",
         test_quote_beyond_limit_or_fee_cap_is_killed_before_signing},
        {"venue_issue_is_killed_before_signing", test_venue_issue_is_killed_before_signing},
        {"signer_refuses_a_payload_that_exceeds_the_authorization",
         test_signer_refuses_a_payload_that_exceeds_the_authorization},
        {"quote_identity_mismatch_is_never_signed", test_quote_identity_mismatch_is_never_signed},
        {"unknown_order_query_is_silence_not_absence", test_unknown_order_query_is_silence_not_absence},
        {"cancel_is_refused_because_a_swap_cannot_be_cancelled",
         test_cancel_is_refused_because_a_swap_cannot_be_cancelled},
        {"bounded_order_table_never_forgets_a_possible_submission",
         test_bounded_order_table_never_forgets_a_possible_submission},
        {"live_arming_refuses_without_both_and_without_a_compiled_transport",
         test_live_arming_refuses_without_both_and_without_a_compiled_transport},
        {"configuration_is_never_defaulted", test_configuration_is_never_defaulted},
        {"transport_seam_drives_the_same_adapter", test_transport_seam_drives_the_same_adapter},
    };
    int failed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cout << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << (sizeof(tests) / sizeof(tests[0]) - failed) << '/' << sizeof(tests) / sizeof(tests[0])
              << " truemarkets venue tests passed\n";
    return failed == 0 ? 0 : 1;
}
