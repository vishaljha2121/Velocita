#include "velocita/v2/kernel.hpp"
#include "velocita/v2/rule_evaluator.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

velocita::v2::Digest256 digest(const std::byte marker) {
    velocita::v2::Digest256 value;
    value.bytes[0] = marker;
    return value;
}

velocita::v2::PolicyArtifact policy() {
    using namespace velocita::v2;
    PolicyArtifact value;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{2};
    value.schema_version = kPolicyArtifactSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{3};
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.model_digest = digest(std::byte{0x11});
    value.artifact_digest = digest(std::byte{0x12});
    value.valid_from_event_time_ns = 100;
    value.valid_until_event_time_ns = 200;
    value.risk_policy_version = RiskPolicyVersion{6};
    value.rule_count = 1;
    value.rules[0] = PolicyRule{0, PredicateOperator::greater_than,
                                FeatureMicros::from_atoms(10),
                                Directive::place_limit};
    return value;
}

velocita::v2::DecisionFrame frame() {
    using namespace velocita::v2;
    DecisionFrame value;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{2};
    value.feature_schema_id = FeatureSchemaId{3};
    value.venue_id = VenueId{7};
    value.venue_session_id = VenueSessionId{8};
    value.venue_sequence = VenueSequence{9};
    value.source_id = SourceId{11};
    value.source_generation = SourceGeneration{1};
    value.source_sequence = SourceSequence{1};
    value.event_time_ns = 150;
    value.receive_time_ns = 160;
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.feature_count = 1;
    value.features[0] = FeatureMicros::from_atoms(11);
    value.market_data_status = MarketDataStatus::ready;
    value.market_data_age_ns = 10;
    value.position_version = PositionVersion{12};
    value.risk_version = RiskVersion{13};
    value.raw_evidence_digest = digest(std::byte{0x21});
    return value;
}

velocita::v2::TradingState ready_state() {
    using namespace velocita::v2;
    TradingState state;
    state.strategy_mode = StrategyMode::normal;
    state.policy_count = 1;
    state.policies[0] = PolicyRecord{policy(), PolicyLifecycle::active};
    state.source_count = 1;
    state.sources[0] = SourceCursor{SourceId{11}, SourceGeneration{1}, SourceSequence{1}};
    state.source_event_count = 1;
    state.source_events[0] = SourceEventReceipt{SourceId{11}, SourceGeneration{1},
                                                SourceSequence{1}, digest(std::byte{0x21})};
    return state;
}

velocita::v2::Command evaluate(const std::uint64_t request,
                               const std::uint64_t decision,
                               const std::byte marker,
                               const velocita::v2::DecisionFrame& value,
                               const velocita::v2::EvaluationMode mode =
                                   velocita::v2::EvaluationMode::authoritative) {
    using namespace velocita::v2;
    return Command{CommandIdentity{CallerId{1}, RequestSequence{request}, digest(marker)},
                   EvaluateDecisionFrame{DecisionId{decision}, mode, value}};
}

void test_success_is_retained_hot_and_cold() {
    using namespace velocita::v2;
    auto state = ready_state();
    ApplyScratch scratch;
    const auto command = evaluate(1, 100, std::byte{0x31}, frame());
    const auto outcome = apply(state, command, scratch);

    require(outcome.code == ApplyCode::applied &&
                outcome.decision_code == DecisionCode::action &&
                outcome.effective_directive == Directive::place_limit,
            "valid frame did not produce an authoritative action");
    require(state.decision_count == 1 &&
                state.decision_hot[0].decision_id == DecisionId{100} &&
                state.decision_cold[0].command_payload_digest == digest(std::byte{0x31}),
            "decision was not retained across hot and cold stores");
    require(apply(state, command, scratch) == outcome && state.decision_count == 1,
            "exact command retry did not return the retained decision");
}

void test_rejection_precedence_is_stable() {
    using namespace velocita::v2;
    ApplyScratch scratch;

    auto invalid = frame();
    invalid.account_id = AccountId{};
    TradingState empty;
    require(apply(empty, evaluate(1, 1, std::byte{0x41}, invalid), scratch)
                .decision_code == DecisionCode::invalid_frame,
            "frame validation did not precede policy resolution");

    auto missing_policy = frame();
    require(apply(empty, evaluate(2, 2, std::byte{0x42}, missing_policy), scratch)
                .decision_code == DecisionCode::policy_not_found,
            "missing policy did not precede later checks");

    auto scoped = ready_state();
    auto wrong_account = frame();
    wrong_account.account_id = AccountId{99};
    require(apply(scoped, evaluate(1, 3, std::byte{0x43}, wrong_account), scratch)
                .decision_code == DecisionCode::account_scope_mismatch,
            "account scope did not precede source evaluation");

    auto no_source = ready_state();
    no_source.source_count = 0;
    no_source.source_event_count = 0;
    auto too_early = frame();
    too_early.event_time_ns = 50;
    require(apply(no_source, evaluate(1, 4, std::byte{0x44}, too_early), scratch)
                .decision_code == DecisionCode::source_not_admitted,
            "source admission did not precede event-time validity");
}

void test_control_authority_suppresses_after_rule_evaluation() {
    using namespace velocita::v2;
    auto state = ready_state();
    state.strategy_mode = StrategyMode::no_new_risk;
    ApplyScratch scratch;
    const auto outcome = apply(state, evaluate(1, 5, std::byte{0x51}, frame()), scratch);
    require(outcome.decision_code == DecisionCode::control_suppressed &&
                outcome.proposed_directive == Directive::place_limit &&
                outcome.effective_directive == Directive::no_action,
            "control authority did not suppress the evaluated proposal");
    require(state.decision_hot[0].rules_visited == 1,
            "control authority ran before the approved rule-evaluation step");
}

void test_full_rejection_precedence_and_rule_outcomes() {
    using namespace velocita::v2;
    ApplyScratch scratch;

    auto inactive = ready_state();
    inactive.policies[0].lifecycle = PolicyLifecycle::validated;
    require(apply(inactive, evaluate(1, 10, std::byte{0x61}, frame()), scratch)
                .decision_code == DecisionCode::policy_not_active,
            "inactive policy was not rejected before scope checks");

    auto instrument = frame();
    instrument.instrument_id = InstrumentId{99};
    auto instrument_state = ready_state();
    require(apply(instrument_state,
                  evaluate(1, 11, std::byte{0x62}, instrument), scratch)
                .decision_code == DecisionCode::instrument_scope_mismatch,
            "instrument scope precedence changed");

    auto schema = frame();
    schema.feature_schema_id = FeatureSchemaId{99};
    auto schema_state = ready_state();
    require(apply(schema_state, evaluate(1, 12, std::byte{0x63}, schema), scratch)
                .decision_code == DecisionCode::feature_schema_mismatch,
            "feature-schema scope precedence changed");

    auto evidence = frame();
    evidence.raw_evidence_digest = digest(std::byte{0x7f});
    auto evidence_state = ready_state();
    require(apply(evidence_state,
                  evaluate(1, 13, std::byte{0x64}, evidence), scratch)
                .decision_code == DecisionCode::source_evidence_mismatch,
            "source-evidence precedence changed");

    auto early = frame();
    early.event_time_ns = 99;
    auto early_state = ready_state();
    require(apply(early_state, evaluate(1, 14, std::byte{0x65}, early), scratch)
                .decision_code == DecisionCode::policy_not_yet_valid,
            "policy lower validity bound changed");

    auto expired = frame();
    expired.event_time_ns = 201;
    auto expired_state = ready_state();
    require(apply(expired_state,
                  evaluate(1, 15, std::byte{0x66}, expired), scratch)
                .decision_code == DecisionCode::policy_expired,
            "policy upper validity bound changed");

    auto stale = frame();
    stale.market_data_status = MarketDataStatus::stale;
    auto stale_state = ready_state();
    require(apply(stale_state, evaluate(1, 16, std::byte{0x67}, stale), scratch)
                .decision_code == DecisionCode::market_data_not_ready,
            "market readiness precedence changed");

    auto unavailable_state = ready_state();
    unavailable_state.policies[0].artifact.rules[0].feature_index = 1;
    require(apply(unavailable_state,
                  evaluate(1, 17, std::byte{0x68}, frame()), scratch)
                .decision_code == DecisionCode::feature_unavailable,
            "missing evaluated feature did not fail closed");

    auto no_match = frame();
    no_match.features[0] = FeatureMicros::from_atoms(10);
    auto no_match_state = ready_state();
    const auto no_match_result = apply(
        no_match_state, evaluate(1, 18, std::byte{0x69}, no_match), scratch);
    require(no_match_result.decision_code == DecisionCode::no_rule_matched &&
                no_match_result.effective_directive == Directive::no_action,
            "no-match did not remain an explicit no-action outcome");

    auto halted = ready_state();
    halted.strategy_mode = StrategyMode::halted;
    halted.source_count = 0;
    halted.source_event_count = 0;
    const auto halted_result =
        apply(halted, evaluate(1, 19, std::byte{0x6a}, frame()), scratch);
    require(halted_result.decision_code == DecisionCode::control_halted &&
                halted_result.rules_visited == 0,
            "categorical halt did not precede source and rule work");
}

void test_shadow_identity_and_capacity_are_fail_closed() {
    using namespace velocita::v2;
    ApplyScratch scratch;

    auto shadow_state = ready_state();
    const auto shadow = apply(
        shadow_state,
        evaluate(1, 30, std::byte{0x71}, frame(), EvaluationMode::shadow),
        scratch);
    require(shadow.decision_code == DecisionCode::shadow_recorded &&
                shadow.proposed_directive == Directive::place_limit &&
                shadow.effective_directive == Directive::no_action,
            "shadow evaluation acquired execution authority");

    auto identity_state = ready_state();
    const auto original = evaluate(1, 31, std::byte{0x72}, frame());
    require(apply(identity_state, original, scratch).code == ApplyCode::applied,
            "decision identity fixture did not apply");
    const auto same_economic_decision =
        evaluate(2, 31, std::byte{0x72}, frame());
    require(apply(identity_state, same_economic_decision, scratch).code ==
                ApplyCode::already_applied,
            "same decision provenance was not idempotent across request IDs");
    const auto conflict = evaluate(3, 31, std::byte{0x73}, frame());
    require(apply(identity_state, conflict, scratch).code ==
                ApplyCode::decision_id_conflict &&
                identity_state.decision_count == 1,
            "conflicting DecisionId mutated retained decision state");

    auto full = ready_state();
    for (std::size_t index = 0; index < kMaxDecisionRecords; ++index) {
        const auto outcome = apply(
            full, evaluate(index + 1, index + 1,
                           static_cast<std::byte>(0x80 + index), frame()), scratch);
        require(outcome.code == ApplyCode::applied,
                "decision capacity fixture failed before reaching the bound");
    }
    const auto retained_last = full.decision_hot.back();
    const auto rejected = apply(
        full, evaluate(kMaxDecisionRecords + 1, kMaxDecisionRecords + 1,
                       std::byte{0xf0}, frame()), scratch);
    require(rejected.code == ApplyCode::decision_capacity_exceeded &&
                full.decision_count == kMaxDecisionRecords &&
                full.decision_hot.back() == retained_last,
            "decision capacity exhaustion partially mutated the bounded region");
}

}  // namespace

int main() {
    try {
        test_success_is_retained_hot_and_cold();
        test_rejection_precedence_is_stable();
        test_control_authority_suppresses_after_rule_evaluation();
        test_full_rejection_precedence_and_rule_outcomes();
        test_shadow_identity_and_capacity_are_fail_closed();
        std::cout << "all V2 decision kernel tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 decision kernel test failed: " << error.what() << '\n';
        return 1;
    }
}
