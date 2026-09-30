#include "velocita/v2/shadow_program.hpp"
#include "v2_catalog_test_support.hpp"
#include "velocita/v2/state_snapshot.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

// The shadow command builder, applied through the REAL reducer from a fresh
// state with the fixture catalog. Expected rule matches are worked out by hand
// from the feature values in each test, never read back from the builder.

namespace {

using namespace velocita::v2;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::int64_t kMicros = 1'000'000;
constexpr std::uint16_t kTrend = 0, kPrice = 4;

// The fixture catalog's only row: account 5, venue 9, instrument 4, prices 1..100 ticks.
ExecutionCatalogRow fixture_row() {
    return catalog_test::row(AccountId{5},
        {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4}, InstrumentSpecVersion{7},
         MoneyMicros::from_atoms(100), PriceTicks::from_atoms(1), PriceTicks::from_atoms(100),
         QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)},
        {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
         (kVenueDirectivePlaceLimit | kVenueDirectiveCancel), kVenueTifGtc, 1});
}

// BTC trend as rules: trend > 0 -> enter long; trend <= 0 -> exit.
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

// The operator ceremony a fresh cluster needs before any policy command: a
// halted cluster with a nonzero kill epoch and executor generation installs its
// execution catalog through consensus. (catalog_test::state() skips this by
// pre-setting the root, which hid the requirement once already.)
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
    // One lot per order and position, long-only; holds at 100 % margin + 1 % fee.
    // Reserve for two orders, so a second order is stopped by the slot alone.
    ApplyResult configure_risk(RiskVersion expected) {
        ConfigureRiskLimits limits;
        limits.account_id = AccountId{5};
        limits.instrument_id = InstrumentId{4};
        limits.risk_policy_version = program.risk_policy_version;
        limits.expected_risk_version = expected;
        limits.expected_kill_epoch = state.kill_epoch;
        limits.expected_executor_generation = state.executor_generation;
        limits.max_order_quantity = QuantityLots::from_atoms(1);
        limits.max_reserved_quantity = QuantityLots::from_atoms(2);
        limits.max_order_gross_notional = MoneyMicros::from_atoms(10'000);
        limits.max_reserved_exposure = MoneyMicros::from_atoms(10'000);
        limits.initial_margin_ppm = 1'000'000;
        limits.maximum_fee_ppm = 10'000;
        limits.max_position_quantity = QuantityLots::from_atoms(1);
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
    // trend +5 % -> rule 0 (enter long, a buy template): proposed place_limit, effective no_action.
    auto [source, up] = h.tick(50'000, 11);
    require(source.code == ApplyCode::applied, "source event refused");
    require(up.code == ApplyCode::applied && up.decision_code == DecisionCode::shadow_recorded,
            "an uptrend tick was not recorded as a shadow decision: " +
                std::to_string(static_cast<int>(up.decision_code)));
    require(up.matched_rule_index == 0 && up.proposed_directive == Directive::place_limit &&
                up.effective_directive == Directive::no_action,
            "uptrend did not propose the enter rule, or shadow acquired authority");
    // The retained proposal is a BUY of the program's clip at the price feature (11 ticks).
    const auto proposal = [&](DecisionId id) {
        for (std::size_t i = 0; i < h.state.decision_count; ++i)
            if (h.state.decision_hot[i].decision_id == id) return h.state.decision_cold[i].proposal;
        throw std::runtime_error("decision not retained");
    };
    const auto buy = proposal(DecisionId{h.decisions});
    require(buy.side == Side::buy && buy.price == PriceTicks::from_atoms(11) &&
                buy.quantity == QuantityLots::from_atoms(1),
            "the enter rule did not propose a buy of one clip at the price feature");
    // trend -5 % -> rule 1 (exit).
    const auto down = h.tick(-50'000, 11).second;
    require(proposal(DecisionId{h.decisions}).side == Side::sell, "the exit rule did not propose a sell");
    require(down.decision_code == DecisionCode::shadow_recorded && down.matched_rule_index == 1 &&
                down.effective_directive == Directive::no_action,
            "a downtrend tick did not match the exit rule");
    // trend exactly 0 -> "> 0" fails, "<= 0" matches: rule 1.
    require(h.tick(0, 11).second.matched_rule_index == 1, "the boundary went to the wrong rule");
}

void a_hold_rule_proposes_no_action() {
    Harness h;
    h.program.rules = {{kTrend, PredicateOperator::greater_than, 100 * kMicros, ShadowAction::enter_long},
                       {kTrend, PredicateOperator::greater_than_or_equal, 0, ShadowAction::hold},
                       {kTrend, PredicateOperator::less_than, 0, ShadowAction::exit}};
    (void)h.setup();
    const auto hold = h.tick(10'000, 11).second;  // +1 %: not > 100 x, but >= 0 -> rule 1, HOLD
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
    h.source_sequence = 0;  // a restarted edge starts its sequence again, on the next generation
    const auto [source, decision] = h.tick(1, 11, 2);
    require(source.code == ApplyCode::applied && decision.decision_code == DecisionCode::shadow_recorded,
            "generation 2 did not continue the source");
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

// The reducer's receipt contract: an exact retry (same caller, sequence and
// payload digest) returns the ORIGINAL retained result and changes nothing.
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

// Payload digests must bind the command's content: a DIFFERENT command reusing a
// request sequence is a conflict, never mistaken for an exact retry.
void a_reused_sequence_with_different_content_is_a_conflict() {
    Harness h;
    (void)h.setup();
    const auto reused = h.sequence;
    (void)h.tick(10'000, 11);
    h.sequence = reused;               // same request sequences again...
    h.source_sequence -= 1;            // ...for the same source slot, with different features
    const auto [source, decision] = h.tick(-10'000, 12);
    // The source event is byte-identical (same slot, same evidence): an exact retry, retained result.
    require(source.code == ApplyCode::applied, "an exact retry of the source event was not idempotent");
    // The decision differs (features): a conflict, not the earlier decision's result.
    require(decision.code == ApplyCode::duplicate_conflict,
            "a different command reusing a request sequence was not refused as a conflict: code " +
                std::to_string(static_cast<int>(decision.code)));
}

// Changing a rule while keeping the policy id and version must be refused: the
// published policy is immutable, and a silent "already applied" would leave
// the old rules running while the operator believes the new ones are live.
void a_changed_rule_under_the_same_version_is_refused() {
    Harness h;
    (void)h.setup();
    h.program.rules[0].threshold = 10'000;  // trend > +1 % instead of > 0
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

// The binding is carried into the frame exactly; the default is today's shadow frame.
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

// Paper through the real reducer, as a strategy caller (21, outside the operator
// band). A fresh account starts at risk version 1, position version 1; the
// fixture clip is 1 lot at 11 ticks x 100 micros per tick-lot = 1,100 micros.
void a_paper_buy_is_authorized_and_a_second_order_on_the_slot_is_not() {
    Harness h;
    (void)h.setup();
    h.caller = CallerId{21};
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
    // The buy is still open (no gateway here), so the slot is taken: the exit's
    // sell must not become a second order.
    const auto down = h.paper_tick(-50'000, {});
    require(authorizable(down), "the exit decision was not offered for authorization");
    const auto second = h.authorize();
    require(second.code != ApplyCode::applied && h.state.order_count == 1,
            "a second order was authorized while the slot's first order is open");
}

// A frame bound to an old risk version is never authorized; bound to the
// account's current one it is. Two configurations -> risk version 2.
void only_a_frame_bound_to_the_current_risk_version_is_authorized() {
    Harness h;
    (void)h.setup();
    h.caller = CallerId{21};
    require(h.configure_risk(RiskVersion{0}).code == ApplyCode::applied, "first configuration refused");
    require(h.configure_risk(RiskVersion{1}).code == ApplyCode::applied, "second configuration refused");
    (void)h.paper_tick(50'000, {});
    const auto stale = h.authorize();
    require(stale.code != ApplyCode::applied && h.state.order_count == 0,
            "a frame bound to risk version 1 was authorized after the account moved to 2");
    // The kernel reports the account's versions on the stale refusal (placeholder
    // detail kind until the kernel appends it); built here from the real state.
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

// Each condition of authorizable() on its own, from a hand-built actionable result.
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
    r.effective_directive = Directive::no_action;  // a hold rule: action, but nothing to place
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
}  // namespace

int main() {
    try {
        setup_requires_an_installed_catalog();
        setup_applies_on_a_fresh_cluster_state();
        a_tick_records_a_shadow_decision_on_the_rule_that_matches();
        a_hold_rule_proposes_no_action();
        a_long_stream_never_wedges_the_bounded_state();
        a_restarted_edge_continues_on_the_next_generation();
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
        std::cout << "shadow program tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "shadow program tests failed: " << error.what() << '\n';
        return 1;
    }
}
