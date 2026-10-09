#include "velocita/v2/shadow_program.hpp"
#include "v2_catalog_test_support.hpp"
#include "velocita/v2/state_snapshot.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using namespace velocita::v2;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::int64_t kMicros = 1'000'000;
constexpr std::uint16_t kTrend = 0, kPrice = 4;

ExecutionCatalogRow fixture_row() {
    return catalog_test::row(AccountId{5},
        {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4}, InstrumentSpecVersion{7},
         MoneyMicros::from_atoms(100), PriceTicks::from_atoms(1), PriceTicks::from_atoms(100),
         QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)},
        {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
         (kVenueDirectivePlaceLimit | kVenueDirectiveCancel), kVenueTifGtc, 1});
}

ShadowProgram trend_program() {
    ShadowProgram p;
    p.policy_id = PolicyId{21};
    p.policy_version = PolicyVersion{1};
    p.feature_schema_id = FeatureSchemaId{31};
    p.risk_policy_version = RiskPolicyVersion{1};
    p.model_digest.bytes[0] = std::byte{0x5a};
    p.valid_from_ns = 1'000;
    p.valid_until_ns = 1'000'000'000'000;
    p.generic_features = 4;
    p.rules = {{kTrend, PredicateOperator::greater_than, 0, ShadowAction::enter_long},
               {kTrend, PredicateOperator::less_than_or_equal, 0, ShadowAction::exit}};
    p.row = fixture_row();
    p.time_in_force = TimeInForce::good_til_cancel;
    p.post_only = PostOnlyMode::required;
    p.order_quantity = QuantityLots::from_atoms(1);
    p.max_notional = MoneyMicros::from_atoms(10'000);
    p.max_market_data_age_ns = 5'000'000'000;
    return p;
}

std::vector<ApplyResult> ceremony(TradingState& state, ApplyScratch& scratch, std::uint64_t& sequence) {
    const auto operator_command = [&](CommandBody body) {
        Digest256 digest;
        digest.bytes[0] = std::byte{0x0c};
        digest.bytes[1] = static_cast<std::byte>(sequence);
        return Command{{CallerId{7}, RequestSequence{sequence++}, digest}, std::move(body)};
    };
    return {catalog_test::apply(state, operator_command(AdvanceKillEpoch{KillEpoch{1}}), scratch),
            catalog_test::apply(state, operator_command(SetExecutorGeneration{ExecutorGeneration{1}}), scratch),
            catalog_test::apply(state, operator_command(InstallExecutionCatalog{
                catalog_test::standard_catalog().root(), KillEpoch{1}, ExecutorGeneration{1}}), scratch)};
}

struct Harness {
    TradingState state{};
    ApplyScratch scratch;
    std::uint64_t sequence = 1;
    std::uint64_t decisions = 0;
    std::uint64_t source_sequence = 0;
    ShadowProgram program = trend_program();

    Harness() {
        for (const auto& r : ceremony(state, scratch, sequence))
            require(r.code == ApplyCode::applied, "operator ceremony refused");
    }

    std::vector<ApplyResult> setup() {
        const auto commands = shadow_setup(program, CallerId{7}, sequence, state.kill_epoch, state.executor_generation);
        sequence += commands.commands.size();
        std::vector<ApplyResult> out;
        for (const auto& command : commands.commands) out.push_back(catalog_test::apply(state, command, scratch));
        return out;
    }
    std::pair<ApplyResult, ApplyResult> tick(std::int64_t trend_micros, std::int64_t price_ticks,
                                             std::uint64_t generation = 1, const FrameBinding& binding = {}) {
        ShadowTick t;
        t.source = SourceId{41};
        t.generation = SourceGeneration{generation};
        t.sequence = SourceSequence{++source_sequence};
        t.event_time_ns = 2'000 + static_cast<std::int64_t>(source_sequence);
        t.receive_time_ns = t.event_time_ns + 10;
        t.market_data_age_ns = 10;
        t.features = {trend_micros, 0, 0, 0, price_ticks};
        t.evidence_digest.bytes[0] = std::byte{0x77};
        t.evidence_digest.bytes[1] = static_cast<std::byte>(source_sequence & 0xff);
        t.evidence_digest.bytes[2] = static_cast<std::byte>((source_sequence >> 8) & 0xff);
        const auto commands = shadow_tick(program, t, caller, sequence, DecisionId{++decisions}, binding);
        sequence += 2;
        const auto recorded = catalog_test::apply(state, commands[0], scratch);
        return {recorded, catalog_test::apply(state, commands[1], scratch)};
    }
    ApplyResult paper_tick(std::int64_t trend_micros, const RiskView& view) {
        return tick(trend_micros, 11, 1, {EvaluationMode::authoritative, view.risk_version, view.position_version})
            .second;
    }
    ApplyResult authorize() { return catalog_test::apply(state, authorize_decision(caller, sequence++,
                                                                                   DecisionId{decisions}), scratch); }
    ApplyResult set_mode(const StrategyMode mode) {
        Digest256 digest;
        digest.bytes[0] = std::byte{0x0e};
        digest.bytes[1] = static_cast<std::byte>(sequence);
        return catalog_test::apply(state, Command{{CallerId{7}, RequestSequence{sequence++}, digest},
            SetStrategyMode{mode, state.kill_epoch, state.executor_generation}}, scratch);
    }
    ApplyResult fill(const ApplyResult& order, const Side side, const std::int64_t quantity) {
        VenueObservation observation;
        observation.kind = VenueObservationKind::fill;
        observation.venue_id = VenueId{9};
        observation.venue_session_id = VenueSessionId{1};
        observation.venue_sequence = VenueSequence{++executions};
        observation.cluster_order_id = order.cluster_order_id();
        observation.effect_id = order.effect_id();
        observation.execution_id = ExecutionId{executions};
        observation.venue_order_id = VenueOrderId{700 + executions};
        observation.side = side;
        observation.price = PriceTicks::from_atoms(11);
        observation.quantity = QuantityLots::from_atoms(quantity);
        observation.evidence_digest.bytes[0] = std::byte{0x66};
        return catalog_test::apply(state, Command{{CallerId{22}, RequestSequence{sequence++},
                                                   observation_fingerprint(observation)},
                                                  ObserveFill{observation}}, scratch);
    }
    RiskView view() const {
        return {state.risk_accounts[0].risk_version, state.risk_accounts[0].position_version, std::nullopt};
    }
    std::int64_t position() const { return state.risk_accounts[0].position_quantity.atoms(); }
    const EconomicActionProposal& last_proposal() const { return state.decision_cold[state.decision_count - 1].proposal; }
    void trade_as_strategy(const std::int64_t max_order, const std::int64_t max_position) {
        (void)setup();
        caller = CallerId{21};
        catalog_test::grant(state, 21, CallerRole::strategy);
        catalog_test::grant(state, 22, CallerRole::gateway);
        max_order_lots = max_order;
        max_position_lots = max_position;
        require(configure_risk(RiskVersion{0}).code == ApplyCode::applied, "risk limits refused");
    }
    void hold(const std::int64_t lots) {
        require(paper_tick(50'000, view()).decision_code == DecisionCode::action, "fixture: no entry decision");
        const auto entry = authorize();
        const auto bought = entry.code == ApplyCode::applied ? fill(entry, Side::buy, program.order_quantity.atoms()).code
                                                            : entry.code;
        require(bought == ApplyCode::applied && position() == lots,
                "fixture: could not hold the position: code " + std::to_string(static_cast<int>(bought)));
    }
    std::uint64_t executions = 0;
    std::int64_t max_order_lots = 1;
    std::int64_t max_position_lots = 1;
    ApplyResult configure_risk(RiskVersion expected) {
        ConfigureRiskLimits limits;
        limits.account_id = AccountId{5};
        limits.instrument_id = InstrumentId{4};
        limits.risk_policy_version = program.risk_policy_version;
        limits.expected_risk_version = expected;
        limits.expected_kill_epoch = state.kill_epoch;
        limits.expected_executor_generation = state.executor_generation;
        limits.max_order_quantity = QuantityLots::from_atoms(max_order_lots);
        limits.max_reserved_quantity = QuantityLots::from_atoms(2 * max_order_lots);
        limits.max_order_gross_notional = MoneyMicros::from_atoms(10'000);
        limits.max_reserved_exposure = MoneyMicros::from_atoms(10'000);
        limits.initial_margin_ppm = 1'000'000;
        limits.maximum_fee_ppm = 10'000;
        limits.max_position_quantity = QuantityLots::from_atoms(max_position_lots);
        limits.reason_code = 1;
        Digest256 digest;
        digest.bytes[0] = std::byte{0x0d};
        digest.bytes[1] = static_cast<std::byte>(sequence);
        return catalog_test::apply(state, Command{{CallerId{7}, RequestSequence{sequence++}, digest}, limits}, scratch);
    }
    CallerId caller{7};
};

void setup_applies_on_a_fresh_cluster_state() {
    Harness h;
    for (const auto& result : h.setup())
        require(result.code == ApplyCode::applied, "a setup command was refused: code " +
                                                       std::to_string(static_cast<int>(result.code)));
    require(h.state.strategy_mode == StrategyMode::normal, "strategy mode is not normal after setup");
    require(h.state.active_executable_policy.present, "no executable policy is active after setup");
}

void a_tick_records_a_shadow_decision_on_the_rule_that_matches() {
    Harness h;
    (void)h.setup();
    auto [source, up] = h.tick(50'000, 11);
    require(source.code == ApplyCode::applied, "source event refused");
    require(up.code == ApplyCode::applied && up.decision_code == DecisionCode::shadow_recorded,
            "an uptrend tick was not recorded as a shadow decision: " +
                std::to_string(static_cast<int>(up.decision_code)));
    require(up.matched_rule_index == 0 && up.proposed_directive == Directive::place_limit &&
                up.effective_directive == Directive::no_action,
            "uptrend did not propose the enter rule, or shadow acquired authority");
    const auto proposal = [&](DecisionId id) {
        for (std::size_t i = 0; i < h.state.decision_count; ++i)
            if (h.state.decision_hot[i].decision_id == id) return h.state.decision_cold[i].proposal;
        throw std::runtime_error("decision not retained");
    };
    const auto buy = proposal(DecisionId{h.decisions});
    require(buy.side == Side::buy && buy.price == PriceTicks::from_atoms(11) &&
                buy.quantity == QuantityLots::from_atoms(1),
            "the enter rule did not propose a buy of one clip at the price feature");
    const auto down = h.tick(-50'000, 11).second;
    require(proposal(DecisionId{h.decisions}).side == Side::sell, "the exit rule did not propose a sell");
    require(down.decision_code == DecisionCode::shadow_recorded && down.matched_rule_index == 1 &&
                down.effective_directive == Directive::no_action,
            "a downtrend tick did not match the exit rule");
    require(h.tick(0, 11).second.matched_rule_index == 1, "the boundary went to the wrong rule");
}

void a_hold_rule_proposes_no_action() {
    Harness h;
    h.program.rules = {{kTrend, PredicateOperator::greater_than, 100 * kMicros, ShadowAction::enter_long},
                       {kTrend, PredicateOperator::greater_than_or_equal, 0, ShadowAction::hold},
                       {kTrend, PredicateOperator::less_than, 0, ShadowAction::exit}};
    (void)h.setup();
    const auto hold = h.tick(10'000, 11).second;
    require(hold.decision_code == DecisionCode::shadow_recorded && hold.matched_rule_index == 1 &&
                hold.proposed_directive == Directive::no_action,
            "a hold rule proposed an action");
}

void a_long_stream_never_wedges_the_bounded_state() {
    Harness h;
    (void)h.setup();
    for (int i = 0; i < 5000; ++i) {
        const auto [source, decision] = h.tick(i % 2 == 0 ? 20'000 : -20'000, 11);
        require(source.code == ApplyCode::applied, "source event refused at tick " + std::to_string(i));
        require(decision.decision_code == DecisionCode::shadow_recorded,
                "tick " + std::to_string(i) + " was not recorded: code " +
                    std::to_string(static_cast<int>(decision.code)) + " decision " +
                    std::to_string(static_cast<int>(decision.decision_code)));
    }
    require(h.state.decision_count <= kMaxDecisionRecords && h.state.receipt_count <= kMaxRequestReceipts,
            "bounded arenas exceeded their capacity");
}

void a_restarted_edge_continues_on_the_next_generation() {
    Harness h;
    (void)h.setup();
    for (int i = 0; i < 3; ++i) require(h.tick(1, 11).second.decision_code == DecisionCode::shadow_recorded,
                                        "generation 1 tick refused");
    h.source_sequence = 0;
    const auto [source, decision] = h.tick(1, 11, 2);
    require(source.code == ApplyCode::applied && decision.decision_code == DecisionCode::shadow_recorded,
            "generation 2 did not continue the source");
}

void a_submitter_ahead_of_the_kernel_rebases_one_past_the_reported_cursor() {
    Harness h;
    (void)h.setup();
    for (int i = 0; i < 2; ++i) require(h.tick(1, 11, 1).first.code == ApplyCode::applied, "generation 1 refused");
    h.source_sequence = 0;
    for (int i = 0; i < 3; ++i) require(h.tick(1, 11, 2).first.code == ApplyCode::applied, "generation 2 refused");
    h.source_sequence = 0;
    const auto refused = h.tick(1, 11, 7).first;
    require(refused.code == ApplyCode::source_generation_gap, "a generation five ahead was not refused as a gap");
    const auto next = resync_source_generation(SourceId{41}, refused);
    require(next == SourceGeneration{3}, "the re-based generation is not one past the committed generation 2");
    h.source_sequence = 0;
    const auto [source, decision] = h.tick(1, 11, next->value());
    require(source.code == ApplyCode::applied && decision.decision_code == DecisionCode::shadow_recorded,
            "the re-based generation did not continue the source");
}

void only_a_reported_gap_for_this_source_moves_the_generation() {
    ApplyResult gap;
    gap.code = ApplyCode::source_generation_gap;
    gap.detail_kind = ApplyResultDetailKind::source_state;
    gap.detail = ApplyResultDetail::source_state(SourceId{41}, SourceGeneration{2}, SourceSequence{9});
    require(resync_source_generation(SourceId{41}, gap) == SourceGeneration{3}, "a reported gap was not adopted");
    auto r = gap;
    r.detail.words = {41, 0, 0};
    require(resync_source_generation(SourceId{41}, r) == SourceGeneration{1},
            "a source the kernel has never seen did not restart at generation 1");
    r = gap;
    r.detail.words[0] = 42;
    require(!resync_source_generation(SourceId{41}, r), "another source's cursor was adopted");
    r = gap;
    r.code = ApplyCode::source_generation_regression;
    require(!resync_source_generation(SourceId{41}, r), "a regression moved the generation");
    r = gap;
    r.detail_kind = ApplyResultDetailKind::source;
    require(!resync_source_generation(SourceId{41}, r), "a gap without the cursor report moved the generation");
    r = gap;
    r.detail.words[1] = std::numeric_limits<std::uint64_t>::max();
    require(!resync_source_generation(SourceId{41}, r), "a generation past the last one was produced");
}

void a_stale_control_view_does_not_enable_the_strategy() {
    Harness h;
    const auto commands = shadow_setup(h.program, CallerId{7}, h.sequence, KillEpoch{99}, h.state.executor_generation);
    ApplyResult last;
    for (const auto& command : commands.commands) last = catalog_test::apply(h.state, command, h.scratch);
    require(last.code == ApplyCode::stale_control_generation && h.state.strategy_mode != StrategyMode::normal,
            "setup built from a stale kill epoch enabled the strategy");
}

Digest256 state_digest(const TradingState& state) {
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(state, bytes);
    require(encoded.error == StateSnapshotError::none, "state did not encode");
    return sha256_digest(std::span(bytes).first(encoded.bytes_written));
}

void setup_is_idempotent_under_exact_retry() {
    Harness h;
    const auto commands = shadow_setup(h.program, CallerId{7}, h.sequence, h.state.kill_epoch, h.state.executor_generation);
    std::vector<ApplyResult> first;
    for (const auto& command : commands.commands) first.push_back(catalog_test::apply(h.state, command, h.scratch));
    const auto before = state_digest(h.state);
    for (std::size_t i = 0; i < commands.commands.size(); ++i)
        require(catalog_test::apply(h.state, commands.commands[i], h.scratch) == first[i],
                "an exact retry did not return the retained result");
    require(state_digest(h.state) == before, "an exact retry of setup changed replicated state");
}

void a_reused_sequence_with_different_content_is_a_conflict() {
    Harness h;
    (void)h.setup();
    const auto reused = h.sequence;
    (void)h.tick(10'000, 11);
    h.sequence = reused;
    h.source_sequence -= 1;
    const auto [source, decision] = h.tick(-10'000, 12);
    require(source.code == ApplyCode::applied, "an exact retry of the source event was not idempotent");
    require(decision.code == ApplyCode::duplicate_conflict,
            "a different command reusing a request sequence was not refused as a conflict: code " +
                std::to_string(static_cast<int>(decision.code)));
}

void a_changed_rule_under_the_same_version_is_refused() {
    Harness h;
    (void)h.setup();
    h.program.rules[0].threshold = 10'000;
    const auto commands = shadow_setup(h.program, CallerId{7}, h.sequence, h.state.kill_epoch,
                                       h.state.executor_generation);
    const auto publish = catalog_test::apply(h.state, commands.commands[0], h.scratch);
    require(publish.code == ApplyCode::immutable_policy_conflict,
            "a changed rule under the same policy version was not refused: code " +
                std::to_string(static_cast<int>(publish.code)));
}

void setup_requires_an_installed_catalog() {
    TradingState state{};
    ApplyScratch scratch;
    const auto commands = shadow_setup(trend_program(), CallerId{7}, 1, state.kill_epoch, state.executor_generation);
    require(catalog_test::apply(state, commands.commands[0], scratch).code == ApplyCode::catalog_not_installed,
            "a policy was published into a cluster with no execution catalog");
}

DecisionFrame frame_of(const Command& command) {
    return std::get<EvaluateDecisionFrame>(command.body).frame;
}

void frames_bind_the_given_mode_and_risk_versions() {
    ShadowTick t;
    t.source = SourceId{41};
    t.generation = SourceGeneration{1};
    t.sequence = SourceSequence{1};
    t.features = {1, 0, 0, 0, 11};
    t.evidence_digest.bytes[0] = std::byte{0x77};
    const auto program = trend_program();
    const auto shadow = shadow_tick(program, t, CallerId{21}, 1, DecisionId{1});
    const auto& shadow_eval = std::get<EvaluateDecisionFrame>(shadow[1].body);
    require(shadow_eval.evaluation_mode == EvaluationMode::shadow && frame_of(shadow[1]).risk_version == RiskVersion{1} &&
                frame_of(shadow[1]).position_version == PositionVersion{1},
            "the default frame is no longer a 1/1 shadow frame");
    const auto paper = shadow_tick(program, t, CallerId{21}, 1, DecisionId{1},
                                   {EvaluationMode::authoritative, RiskVersion{3}, PositionVersion{5}});
    require(std::get<EvaluateDecisionFrame>(paper[1].body).evaluation_mode == EvaluationMode::authoritative &&
                frame_of(paper[1]).risk_version == RiskVersion{3} &&
                frame_of(paper[1]).position_version == PositionVersion{5},
            "a paper frame did not bind the given mode and versions");
    require(paper[1].identity.payload_digest != shadow[1].identity.payload_digest,
            "the payload digest does not bind the evaluation mode and versions");
}

void a_paper_buy_is_authorized_and_a_second_order_on_the_slot_is_not() {
    Harness h;
    (void)h.setup();
    h.caller = CallerId{21};
    catalog_test::grant(h.state, 21, CallerRole::strategy);
    require(h.configure_risk(RiskVersion{0}).code == ApplyCode::applied, "risk limits refused");
    const auto up = h.paper_tick(50'000, {});
    require(up.decision_code == DecisionCode::action && up.effective_directive == Directive::place_limit,
            "an uptrend paper frame was not an actionable decision");
    require(authorizable(up), "an actionable paper decision was not offered for authorization");
    const auto authorized = h.authorize();
    require(authorized.code == ApplyCode::applied && h.state.order_count == 1 &&
                h.state.orders[0].decision_id == DecisionId{h.decisions} &&
                h.state.reservations[0].remaining_quantity == QuantityLots::from_atoms(1) &&
                h.state.reservations[0].remaining_notional == MoneyMicros::from_atoms(1'100),
            "the paper buy did not become one order of one lot for 1,100 micros");
    const auto down = h.paper_tick(-50'000, {});
    require(authorizable(down), "the exit decision was not offered for authorization");
    const auto second = h.authorize();
    require(second.code != ApplyCode::applied && h.state.order_count == 1,
            "a second order was authorized while the slot's first order is open");
}

void reduce_only_lets_the_exit_through_and_nothing_else() {
    Harness h;
    h.trade_as_strategy(1, 1);
    require(h.set_mode(StrategyMode::reduce_only).code == ApplyCode::applied, "reduce-only refused");
    require(h.paper_tick(-50'000, h.view()).decision_code == DecisionCode::control_suppressed,
            "an exit was proposed in reduce-only with nothing to reduce");
    require(h.set_mode(StrategyMode::normal).code == ApplyCode::applied, "fixture: normal refused");
    h.hold(1);

    const auto entry_decided_in_normal = h.paper_tick(50'000, h.view());
    require(entry_decided_in_normal.decision_code == DecisionCode::action, "fixture: no second entry decision");
    require(h.set_mode(StrategyMode::reduce_only).code == ApplyCode::applied, "reduce-only refused");
    require(h.authorize().code == ApplyCode::stale_control_generation && h.state.order_count == 1,
            "an entry decided in normal mode was authorized in reduce-only");

    const auto entry = h.paper_tick(50'000, h.view());
    require(entry.decision_code == DecisionCode::control_suppressed &&
                entry.effective_directive == Directive::no_action && !authorizable(entry),
            "an entry was not suppressed in reduce-only");
    const auto exit = h.paper_tick(-50'000, h.view());
    require(exit.decision_code == DecisionCode::action && exit.effective_directive == Directive::place_limit &&
                exit.matched_rule_index == 1 && authorizable(exit) &&
                h.last_proposal().side == Side::sell && h.last_proposal().quantity == QuantityLots::from_atoms(1),
            "the exit was suppressed in reduce-only");
    const auto sell = h.authorize();
    require(sell.code == ApplyCode::applied && submit_permitted(h.state, DecisionId{h.decisions}),
            "the exit was not authorized and sendable in reduce-only");
    require(h.paper_tick(-50'000, h.view()).decision_code == DecisionCode::control_suppressed,
            "a second exit was proposed while the first already covers the position");
    require(h.fill(sell, Side::sell, 1).code == ApplyCode::applied && h.position() == 0,
            "the exit fill was refused");
    require(h.paper_tick(-50'000, h.view()).decision_code == DecisionCode::control_suppressed,
            "an exit was proposed in reduce-only after the position closed");
}

void an_order_is_sendable_only_in_the_mode_that_decided_it() {
    Harness h;
    h.trade_as_strategy(1, 1);
    require(h.paper_tick(50'000, h.view()).decision_code == DecisionCode::action, "fixture: no entry decision");
    const auto buy = h.authorize();
    const DecisionId buy_decision{h.decisions};
    require(buy.code == ApplyCode::applied && submit_permitted(h.state, buy_decision),
            "a normal-mode order was not sendable in normal mode");
    for (const auto mode : {StrategyMode::reduce_only, StrategyMode::flatten, StrategyMode::no_new_risk,
                            StrategyMode::cancel_only, StrategyMode::halted}) {
        require(h.set_mode(mode).code == ApplyCode::applied && !submit_permitted(h.state, buy_decision),
                "an entry authorized in normal mode stayed sendable in mode " +
                    std::to_string(static_cast<int>(mode)));
    }
    require(h.set_mode(StrategyMode::normal).code == ApplyCode::applied && submit_permitted(h.state, buy_decision),
            "the entry was not sendable again in normal mode");
    require(h.fill(buy, Side::buy, 1).code == ApplyCode::applied, "fixture: entry fill refused");

    require(h.set_mode(StrategyMode::reduce_only).code == ApplyCode::applied, "reduce-only refused");
    require(h.paper_tick(-50'000, h.view()).decision_code == DecisionCode::action, "fixture: no exit decision");
    require(h.authorize().code == ApplyCode::applied, "fixture: exit not authorized");
    const DecisionId exit_decision{h.decisions};
    require(submit_permitted(h.state, exit_decision), "a reduce-only exit was not sendable in reduce-only");
    require(h.set_mode(StrategyMode::flatten).code == ApplyCode::applied &&
                !submit_permitted(h.state, exit_decision),
            "a reduce-only exit stayed sendable after the mode moved to flatten");
    require(h.set_mode(StrategyMode::halted).code == ApplyCode::applied &&
                !submit_permitted(h.state, exit_decision),
            "an exit stayed sendable after a halt");
    for (const auto mode : {StrategyMode::halted, StrategyMode::no_new_risk, StrategyMode::cancel_only}) {
        auto forged = h.state;
        forged.strategy_mode = mode;
        forged.decision_hot[forged.decision_count - 1].strategy_mode = mode;
        require(!submit_permitted(forged, exit_decision),
                "an order was sendable in a mode that allows none because its decision carries that mode");
    }
    require(!submit_permitted(h.state, DecisionId{9'999}) &&
                h.set_mode(StrategyMode::reduce_only).code == ApplyCode::applied &&
                !submit_permitted(h.state, DecisionId{9'999}),
            "an unknown decision was sendable");
}

void an_exit_that_no_longer_reduces_is_refused_at_authorization() {
    Harness h;
    h.trade_as_strategy(1, 1);
    h.hold(1);
    require(h.set_mode(StrategyMode::reduce_only).code == ApplyCode::applied, "reduce-only refused");
    require(h.paper_tick(-50'000, h.view()).decision_code == DecisionCode::action, "fixture: no exit decision");
    for (const auto mode : {StrategyMode::halted, StrategyMode::no_new_risk, StrategyMode::cancel_only}) {
        auto forged = h;
        forged.state.strategy_mode = mode;
        forged.state.decision_hot[forged.state.decision_count - 1].strategy_mode = mode;
        require(forged.authorize().code == ApplyCode::stale_control_generation,
                "a decision was authorized in a mode that allows no orders because it carries that mode");
    }
    h.state.risk_accounts[0].position_quantity = {};
    h.state.risk_accounts[0].cost_basis = {};
    h.state.risk_accounts[0].allow_short = true;
    require(h.authorize().code == ApplyCode::order_not_reducing && h.state.order_count == 1,
            "a sell that would open a short was authorized in reduce-only");
}

void flatten_sells_exactly_the_position_whatever_the_rules_say() {
    Harness h;
    h.program.order_quantity = QuantityLots::from_atoms(3);
    h.trade_as_strategy(3, 3);
    h.hold(3);
    h.max_order_lots = 2;
    require(h.configure_risk(h.state.risk_accounts[0].risk_version).code == ApplyCode::applied,
            "fixture: the order limit could not be lowered to two lots");
    require(h.set_mode(StrategyMode::flatten).code == ApplyCode::applied, "flatten refused");

    const auto shadow = h.tick(50'000, 11).second;
    require(shadow.decision_code == DecisionCode::shadow_recorded, "a shadow frame was acted on in flatten");
    const auto unpriced = h.tick(50'000, 101, 1, {EvaluationMode::authoritative, h.view().risk_version,
                                                  h.view().position_version});
    require(unpriced.second.decision_code == DecisionCode::action_resolution_rejected &&
                unpriced.second.action_resolution_code == ActionResolutionCode::price_out_of_bounds &&
                !authorizable(unpriced.second),
            "flatten proposed an order at a price outside the instrument's bounds");

    const auto first = h.paper_tick(50'000, h.view());
    require(first.decision_code == DecisionCode::action && first.proposed_directive == Directive::place_limit &&
                first.effective_directive == Directive::place_limit &&
                first.matched_rule_index == std::numeric_limits<std::uint16_t>::max() &&
                first.action_resolution_code == ActionResolutionCode::proposed,
            "flatten did not turn an entry frame into an order of its own");
    require(h.last_proposal().side == Side::sell && h.last_proposal().quantity == QuantityLots::from_atoms(2) &&
                h.last_proposal().price == PriceTicks::from_atoms(11) &&
                h.last_proposal().notional == MoneyMicros::from_atoms(2'200),
            "the first flatten order is not a sell of the two lots the order limit allows, at the frame price");
    require(paper_request(h.program, first, true) == PaperRequest::authorize,
            "an edge holding back entries would not authorize the flatten order");
    const auto sell = h.authorize();
    require(sell.code == ApplyCode::applied && submit_permitted(h.state, DecisionId{h.decisions}),
            "the flatten order was not authorized and sendable");
    require(h.paper_tick(-50'000, h.view()).decision_code == DecisionCode::control_suppressed,
            "flatten proposed a second order while the first is open");
    require(h.fill(sell, Side::sell, 2).code == ApplyCode::applied && h.position() == 1, "flatten fill refused");

    const auto second = h.paper_tick(50'000, h.view());
    require(second.decision_code == DecisionCode::action && h.last_proposal().side == Side::sell &&
                h.last_proposal().quantity == QuantityLots::from_atoms(1),
            "the second flatten order is not a sell of the one lot left");
    const auto last = h.authorize();
    require(last.code == ApplyCode::applied && h.fill(last, Side::sell, 1).code == ApplyCode::applied &&
                h.position() == 0,
            "the last lot was not sold");
    const auto flat = h.paper_tick(50'000, h.view());
    require(flat.decision_code == DecisionCode::control_suppressed && flat.effective_directive == Directive::no_action &&
                !authorizable(flat),
            "flatten proposed an order with no position");
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    require(encode_state_snapshot(h.state, bytes).error == StateSnapshotError::none,
            "the state after a flatten is not canonical");
}

void flatten_is_capped_by_the_exit_template_and_buys_back_a_short() {
    {
        Harness h;
        h.trade_as_strategy(3, 3);
        for (int lot = 1; lot <= 3; ++lot) h.hold(lot);
        require(h.set_mode(StrategyMode::flatten).code == ApplyCode::applied, "flatten refused");
        require(h.paper_tick(50'000, h.view()).decision_code == DecisionCode::action &&
                    h.last_proposal().side == Side::sell &&
                    h.last_proposal().quantity == QuantityLots::from_atoms(1),
                "flatten sold more than the exit template's one-lot cap in one order");
    }
    {
        Harness h;
        h.program.order_quantity = QuantityLots::from_atoms(3);
        h.trade_as_strategy(3, 3);
        h.state.risk_accounts[0].allow_short = true;
        h.state.risk_accounts[0].position_quantity = QuantityLots::from_atoms(-2);
        h.state.risk_accounts[0].cost_basis = MoneyMicros::from_atoms(2'200);
        require(h.set_mode(StrategyMode::flatten).code == ApplyCode::applied, "flatten refused");
        require(h.paper_tick(-50'000, h.view()).decision_code == DecisionCode::action &&
                    h.last_proposal().side == Side::buy &&
                    h.last_proposal().quantity == QuantityLots::from_atoms(2),
                "flatten did not buy back exactly a two-lot short");
        require(h.authorize().code == ApplyCode::applied, "the buy-back was not authorized in flatten");
    }
}

void only_a_frame_bound_to_the_current_risk_version_is_authorized() {
    Harness h;
    (void)h.setup();
    h.caller = CallerId{21};
    catalog_test::grant(h.state, 21, CallerRole::strategy);
    require(h.configure_risk(RiskVersion{0}).code == ApplyCode::applied, "first configuration refused");
    require(h.configure_risk(RiskVersion{1}).code == ApplyCode::applied, "second configuration refused");
    (void)h.paper_tick(50'000, {});
    const auto stale = h.authorize();
    require(stale.code != ApplyCode::applied && h.state.order_count == 0,
            "a frame bound to risk version 1 was authorized after the account moved to 2");
    ApplyResult report = stale;
    report.code = ApplyCode::stale_risk_state;
    report.detail_kind = kRiskStateDetailKind;
    report.detail.words = {h.state.risk_accounts[0].risk_version.value(),
                           h.state.risk_accounts[0].position_version.value(),
                           static_cast<std::uint64_t>(h.state.risk_accounts[0].position_quantity.atoms())};
    const auto view = resync_risk_view({}, report);
    require(view.risk_version == RiskVersion{2} && view.position_version == PositionVersion{1},
            "the reported versions were not adopted");
    (void)h.paper_tick(50'000, view);
    require(h.authorize().code == ApplyCode::applied && h.state.order_count == 1,
            "a frame bound to the reported current versions was not authorized");
}

void shadow_decisions_are_never_offered_for_authorization() {
    Harness h;
    (void)h.setup();
    const auto shadow = h.tick(50'000, 11).second;
    require(shadow.decision_code == DecisionCode::shadow_recorded && !authorizable(shadow),
            "a shadow decision was offered for authorization");
}

void authorizable_needs_every_condition() {
    ApplyResult good;
    good.code = ApplyCode::applied;
    good.evaluation_mode = EvaluationMode::authoritative;
    good.decision_code = DecisionCode::action;
    good.effective_directive = Directive::place_limit;
    good.action_resolution_code = ActionResolutionCode::proposed;
    require(authorizable(good), "the actionable result was refused");
    auto r = good;
    r.code = ApplyCode::already_applied;
    require(!authorizable(r), "a non-applied evaluation was offered");
    r = good;
    r.evaluation_mode = EvaluationMode::shadow;
    require(!authorizable(r), "a shadow evaluation was offered");
    r = good;
    r.decision_code = DecisionCode::control_halted;
    require(!authorizable(r), "a halted decision was offered");
    r = good;
    r.effective_directive = Directive::no_action;
    require(!authorizable(r), "a hold decision was offered");
    r = good;
    r.action_resolution_code = ActionResolutionCode::notional_cap_exceeded;
    require(!authorizable(r), "an unsized proposal was offered");
}

void the_risk_view_moves_only_on_a_reported_stale_refusal() {
    const RiskView current{RiskVersion{2}, PositionVersion{3}, std::nullopt};
    ApplyResult stale;
    stale.code = ApplyCode::stale_risk_state;
    stale.detail_kind = kRiskStateDetailKind;
    stale.detail.words = {4, 9, 25};
    require(resync_risk_view(current, stale) ==
                RiskView{RiskVersion{4}, PositionVersion{9}, QuantityLots::from_atoms(25)},
            "a reported stale refusal was not adopted");
    auto r = stale;
    r.code = ApplyCode::order_slot_occupied;
    require(resync_risk_view(current, r) == current, "a non-stale refusal moved the view");
    r = stale;
    r.detail_kind = ApplyResultDetailKind::authorization;
    require(resync_risk_view(current, r) == current, "a stale refusal without the risk-state report moved the view");
    r = stale;
    r.detail.words = {0, 9, 25};
    require(resync_risk_view(current, r) == current, "a zero risk version was adopted");
    r.detail.words = {4, 0, 25};
    require(resync_risk_view(current, r) == current, "a zero position version was adopted");
}

void exits_only_withholds_a_buy_and_still_asks_for_an_exit() {
    const auto program = trend_program();
    ApplyResult buy;
    buy.code = ApplyCode::applied;
    buy.evaluation_mode = EvaluationMode::authoritative;
    buy.decision_code = DecisionCode::action;
    buy.effective_directive = Directive::place_limit;
    buy.action_resolution_code = ActionResolutionCode::proposed;
    buy.matched_rule_index = 0;
    auto sell = buy;
    sell.matched_rule_index = 1;
    require(paper_request(program, buy, false) == PaperRequest::authorize, "a buy was not asked for without the stop");
    require(paper_request(program, sell, false) == PaperRequest::authorize, "an exit was not asked for");
    require(paper_request(program, buy, true) == PaperRequest::withheld, "the stop let a buy through");
    require(paper_request(program, sell, true) == PaperRequest::authorize, "the stop withheld an exit");
    auto unknown = buy;
    unknown.matched_rule_index = 2;
    require(paper_request(program, unknown, true) == PaperRequest::withheld, "an unknown rule passed the stop");
    auto hold = buy;
    hold.effective_directive = Directive::no_action;
    require(paper_request(program, hold, true) == PaperRequest::none &&
                paper_request(program, hold, false) == PaperRequest::none,
            "a decision with nothing to place was offered or withheld");
}

void a_withheld_buy_leaves_no_order() {
    Harness h;
    (void)h.setup();
    h.caller = CallerId{21};
    catalog_test::grant(h.state, 21, CallerRole::strategy);
    require(h.configure_risk(RiskVersion{0}).code == ApplyCode::applied, "risk limits refused");
    const auto up = h.paper_tick(50'000, {});
    require(paper_request(h.program, up, true) == PaperRequest::withheld, "the enter rule's buy was not withheld");
    require(h.state.order_count == 0, "an order exists before any authorization");
    const auto again = h.paper_tick(50'000, {});
    require(paper_request(h.program, again, false) == PaperRequest::authorize &&
                h.authorize().code == ApplyCode::applied && h.state.order_count == 1,
            "without the stop the same buy did not become an order");
}

void a_trend_churn_program_replaces_the_trend_program_and_follows_the_phase() {
    Harness h;
    (void)h.setup();
    ShadowProgram churn = trend_program();
    churn.policy_id = PolicyId{23};
    churn.feature_schema_id = FeatureSchemaId{33};
    churn.generic_features = 5;
    constexpr std::uint16_t kPhase = 4;
    churn.rules = {{kTrend, PredicateOperator::less_than_or_equal, 0, ShadowAction::exit},
                   {kPhase, PredicateOperator::greater_than, 0, ShadowAction::enter_long},
                   {kPhase, PredicateOperator::less_than_or_equal, 0, ShadowAction::exit}};
    h.program = churn;
    require(h.state.strategy_mode == StrategyMode::normal, "the trend setup did not leave the strategy enabled");
    const auto installed = h.setup();
    for (std::size_t step = 0; step < installed.size(); ++step)
        require(installed[step].code == (step < 4 ? ApplyCode::applied : ApplyCode::already_applied),
                "installing the churn program, step " + std::to_string(step) + ": code " +
                    std::to_string(static_cast<int>(installed[step].code)));
    require(h.state.active_executable_policy.executable_policy.policy_id == PolicyId{23},
            "the churn policy is not the active one");
    const auto frame = [&](std::int64_t trend, std::int64_t phase) {
        ShadowTick t;
        t.source = SourceId{41};
        t.generation = SourceGeneration{1};
        t.sequence = SourceSequence{++h.source_sequence};
        t.event_time_ns = 2'000 + static_cast<std::int64_t>(h.source_sequence);
        t.receive_time_ns = t.event_time_ns + 10;
        t.market_data_age_ns = 10;
        t.features = {trend, 0, 0, 0, phase, 11};
        t.evidence_digest.bytes[0] = std::byte{0x78};
        t.evidence_digest.bytes[1] = static_cast<std::byte>(h.source_sequence & 0xff);
        const auto commands = shadow_tick(h.program, t, h.caller, h.sequence, DecisionId{++h.decisions});
        h.sequence += 2;
        (void)catalog_test::apply(h.state, commands[0], h.scratch);
        return catalog_test::apply(h.state, commands[1], h.scratch);
    };
    const auto enter = frame(50'000, kMicros);
    require(enter.decision_code == DecisionCode::shadow_recorded && enter.matched_rule_index == 1 &&
                enter.proposed_directive == Directive::place_limit,
            "odd phase in an uptrend did not enter");
    require(frame(50'000, 0).matched_rule_index == 2, "even phase in an uptrend did not exit");
    require(frame(-1, kMicros).matched_rule_index == 0, "a downtrend did not exit first");
}

}

int main() {
    try {
        setup_requires_an_installed_catalog();
        setup_applies_on_a_fresh_cluster_state();
        a_tick_records_a_shadow_decision_on_the_rule_that_matches();
        reduce_only_lets_the_exit_through_and_nothing_else();
        an_order_is_sendable_only_in_the_mode_that_decided_it();
        an_exit_that_no_longer_reduces_is_refused_at_authorization();
        flatten_sells_exactly_the_position_whatever_the_rules_say();
        flatten_is_capped_by_the_exit_template_and_buys_back_a_short();
        a_hold_rule_proposes_no_action();
        a_long_stream_never_wedges_the_bounded_state();
        a_restarted_edge_continues_on_the_next_generation();
        a_submitter_ahead_of_the_kernel_rebases_one_past_the_reported_cursor();
        only_a_reported_gap_for_this_source_moves_the_generation();
        a_stale_control_view_does_not_enable_the_strategy();
        setup_is_idempotent_under_exact_retry();
        a_reused_sequence_with_different_content_is_a_conflict();
        a_changed_rule_under_the_same_version_is_refused();
        frames_bind_the_given_mode_and_risk_versions();
        a_paper_buy_is_authorized_and_a_second_order_on_the_slot_is_not();
        only_a_frame_bound_to_the_current_risk_version_is_authorized();
        shadow_decisions_are_never_offered_for_authorization();
        authorizable_needs_every_condition();
        the_risk_view_moves_only_on_a_reported_stale_refusal();
        exits_only_withholds_a_buy_and_still_asks_for_an_exit();
        a_withheld_buy_leaves_no_order();
        a_trend_churn_program_replaces_the_trend_program_and_follows_the_phase();
        std::cout << "shadow program tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "shadow program tests failed: " << error.what() << '\n';
        return 1;
    }
}
