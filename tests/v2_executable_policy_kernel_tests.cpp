#include "velocita/v2/kernel.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

velocita::v2::Digest256 digest(const std::uint8_t marker) {
    velocita::v2::Digest256 value;
    value.bytes[0] = static_cast<std::byte>(marker);
    return value;
}

velocita::v2::PolicyArtifact policy(const std::uint64_t version) {
    using namespace velocita::v2;
    PolicyArtifact value;
    value.schema_version = kPolicyArtifactSchemaVersion;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{version};
    value.feature_schema_id = FeatureSchemaId{3};
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.model_digest = digest(0x11);
    value.artifact_digest = digest(static_cast<std::uint8_t>(0x20 + version));
    value.valid_from_event_time_ns = 100;
    value.valid_until_event_time_ns = 200;
    value.risk_policy_version = RiskPolicyVersion{6};
    value.rule_count = 1;
    value.rules[0] = {0, PredicateOperator::greater_than,
                      FeatureMicros::from_atoms(10), Directive::place_limit};
    return value;
}

velocita::v2::StageExecutablePolicy bundle(const std::uint64_t version) {
    using namespace velocita::v2;
    StageExecutablePolicy value;
    value.feature_schema.schema_version = kFeatureSchemaArtifactSchemaVersion;
    value.feature_schema.feature_schema_id = FeatureSchemaId{3};
    value.feature_schema.feature_count = 1;
    value.feature_schema.features[0] = {FeatureEconomicKind::price, 1};
    value.feature_schema.artifact_digest =
        compute_artifact_digest(value.feature_schema).digest;

    auto& executable = value.executable_policy;
    executable.schema_version = kExecutablePolicyArtifactSchemaVersion;
    executable.policy_id = PolicyId{10};
    executable.policy_version = PolicyVersion{version};
    executable.policy_artifact_digest = policy(version).artifact_digest;
    executable.feature_schema_id = FeatureSchemaId{3};
    executable.feature_schema_digest = value.feature_schema.artifact_digest;
    executable.template_count = 1;
    auto& action = executable.templates[0];
    action.schema_version = kActionTemplateSchemaVersion;
    action.directive = Directive::place_limit;
    action.side = Side::buy;
    action.time_in_force = TimeInForce::good_til_cancel;
    action.post_only = PostOnlyMode::required;
    action.order_slot = LogicalOrderSlot{1};
    action.instrument_spec_version = InstrumentSpecVersion{7};
    action.venue_capabilities_version = VenueCapabilitiesVersion{8};
    action.price_source = {EconomicValueSourceKind::feature, 0,
                           RoundingMode::toward_zero, {}, 1, {}};
    action.quantity_source = {EconomicValueSourceKind::fixed, 0,
                              RoundingMode::toward_zero,
                              QuantityLots::from_atoms(2), 0};
    action.safety_caps = {QuantityLots::from_atoms(2),
                          MoneyMicros::from_atoms(10'000),
                          PriceTicks::from_atoms(2), PriceTicks::from_atoms(1),
                          100, action.price_source};
    executable.rule_binding_count = 1;
    executable.rule_template_slots[0] = ActionTemplateSlot{1};
    executable.artifact_digest = compute_artifact_digest(executable).digest;

    value.instrument = {kInstrumentExecutionProfileSchemaVersion, VenueId{9},
                        InstrumentId{4}, InstrumentSpecVersion{7},
                        MoneyMicros::from_atoms(100), PriceTicks::from_atoms(1),
                        PriceTicks::from_atoms(100), QuantityLots::from_atoms(1),
                        QuantityLots::from_atoms(10)};
    value.venue = {kVenueCapabilitiesSchemaVersion, VenueId{9},
                   VenueCapabilitiesVersion{8}, kVenueDirectivePlaceLimit,
                   kVenueTifGtc, 1};
    return value;
}

velocita::v2::DecisionFrame frame(const std::uint64_t version) {
    using namespace velocita::v2;
    DecisionFrame value;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{version};
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

velocita::v2::Command command(const std::uint64_t sequence,
                              velocita::v2::CommandBody body) {
    using namespace velocita::v2;
    return {{CallerId{1}, RequestSequence{sequence},
             digest(static_cast<std::uint8_t>(0x50 + sequence))},
            std::move(body)};
}

void test_atomic_stage_activate_and_replace() {
    using namespace velocita::v2;
    TradingState state;
    state.strategy_mode = StrategyMode::normal;
    ApplyScratch scratch;

    require(apply(state, command(1, PublishPolicy{policy(1)}), scratch).code ==
                ApplyCode::applied,
            "base policy publish failed");
    require(apply(state, command(2, ValidatePolicy{PolicyId{10}, PolicyVersion{1}}),
                  scratch).code == ApplyCode::applied,
            "base policy validation failed");
    const auto first = bundle(1);
    require(apply(state, command(3, first), scratch).code == ApplyCode::applied &&
                state.staged_executable_policy.present &&
                !state.active_executable_policy.present,
            "complete executable bundle was not staged atomically");
    require(apply(state,
                  command(4, ActivateExecutablePolicy{
                                 PolicyId{10}, PolicyVersion{1},
                                 first.executable_policy.artifact_digest}),
                  scratch).code == ApplyCode::applied &&
                state.active_executable_policy.present &&
                !state.staged_executable_policy.present &&
                state.policies[0].lifecycle == PolicyLifecycle::active,
            "staged bundle did not acquire authority atomically");

    require(apply(state,
                  command(5, RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                               SourceSequence{1}, digest(0x44)}),
                  scratch).code == ApplyCode::applied,
            "source fixture was not admitted");
    const auto decision = apply(
        state,
        command(6, EvaluateDecisionFrame{DecisionId{20},
                                         EvaluationMode::authoritative,
                                         frame(1)}),
        scratch);
    require(decision.decision_code == DecisionCode::action &&
                decision.action_resolution_code == ActionResolutionCode::proposed &&
                state.decision_cold[0].proposal.price == PriceTicks::from_atoms(11) &&
                state.decision_cold[0].proposal.quantity ==
                    QuantityLots::from_atoms(2) &&
                state.decision_cold[0].proposal.notional ==
                    MoneyMicros::from_atoms(2'200),
            "active executable policy did not produce its exact bounded proposal");

    auto over_limit = frame(1);
    over_limit.features[0] = FeatureMicros::from_atoms(101);
    const auto rejected = apply(
        state,
        command(7, EvaluateDecisionFrame{DecisionId{21},
                                         EvaluationMode::authoritative,
                                         over_limit}),
        scratch);
    require(rejected.decision_code == DecisionCode::action_resolution_rejected &&
                rejected.action_resolution_code ==
                    ActionResolutionCode::price_out_of_bounds &&
                rejected.effective_directive == Directive::no_action &&
                state.decision_cold[1].proposal == EconomicActionProposal{},
            "resolver rejection created an effective or partial action");

    require(apply(state, command(8, PublishPolicy{policy(2)}), scratch).code ==
                ApplyCode::applied &&
                apply(state,
                      command(9, ValidatePolicy{PolicyId{10}, PolicyVersion{2}}),
                      scratch).code == ApplyCode::applied,
            "replacement base policy did not reach validated state");
    const auto replacement = bundle(2);
    require(apply(state, command(10, replacement), scratch).code ==
                ApplyCode::applied,
            "replacement bundle did not stage");
    require(apply(state,
                  command(11, ActivateExecutablePolicy{
                                  PolicyId{10}, PolicyVersion{2},
                                  replacement.executable_policy.artifact_digest}),
                  scratch).code == ApplyCode::applied &&
                state.policies[0].lifecycle == PolicyLifecycle::superseded &&
                state.policies[1].lifecycle == PolicyLifecycle::active &&
                state.active_executable_policy.executable_policy.policy_version ==
                    PolicyVersion{2},
            "replacement activation did not supersede old authority");

    require(apply(state,
                  command(12, SupersedePolicy{PolicyId{10}, PolicyVersion{2}}),
                  scratch).code == ApplyCode::applied &&
                !state.active_executable_policy.present &&
                state.policies[1].lifecycle == PolicyLifecycle::superseded,
            "base-policy supersede did not atomically revoke executable authority");
}

void test_digest_and_identity_fail_closed() {
    using namespace velocita::v2;
    TradingState state;
    ApplyScratch scratch;
    require(apply(state, command(1, PublishPolicy{policy(1)}), scratch).code ==
                ApplyCode::applied,
            "digest fixture publish failed");
    require(apply(state,
                  command(2, ValidatePolicy{PolicyId{10}, PolicyVersion{1}}),
                  scratch).code == ApplyCode::applied,
            "digest fixture validation failed");

    auto corrupted = bundle(1);
    corrupted.feature_schema.artifact_digest.bytes[0] ^= std::byte{1};
    require(apply(state, command(3, corrupted), scratch).code ==
                ApplyCode::artifact_digest_mismatch &&
                !state.staged_executable_policy.present,
            "digest mismatch partially staged authority");

    const auto valid = bundle(1);
    require(apply(state,
                  command(4, ActivateExecutablePolicy{
                                 PolicyId{10}, PolicyVersion{1},
                                 valid.executable_policy.artifact_digest}),
                  scratch).code == ApplyCode::executable_policy_not_staged &&
                !state.active_executable_policy.present,
            "unstaged identity acquired authority");

    require(apply(state, command(5, valid), scratch).code == ApplyCode::applied,
            "valid bundle did not stage");
    auto changed_economics = valid;
    changed_economics.instrument.quote_micros_per_tick_lot =
        MoneyMicros::from_atoms(101);
    require(apply(state, command(6, changed_economics), scratch).code ==
                ApplyCode::executable_policy_identity_conflict,
            "reused executable identity silently changed instrument economics");
}

void test_stage_rejection_reasons_and_fail_closed_decision() {
    using namespace velocita::v2;
    const auto expect_stage = [](StageExecutablePolicy candidate,
                                 const ApplyCode expected,
                                 const std::string& message) {
        TradingState state;
        ApplyScratch scratch;
        require(apply(state, command(1, PublishPolicy{policy(1)}), scratch).code ==
                    ApplyCode::applied &&
                    apply(state,
                          command(2, ValidatePolicy{PolicyId{10},
                                                    PolicyVersion{1}}),
                          scratch).code == ApplyCode::applied,
                "rejection fixture setup failed");
        require(apply(state, command(3, std::move(candidate)), scratch).code ==
                        expected &&
                    !state.staged_executable_policy.present,
                message);
    };

    auto invalid_schema = bundle(1);
    invalid_schema.feature_schema.schema_version = 0;
    expect_stage(invalid_schema, ApplyCode::invalid_feature_schema_contract,
                 "invalid feature schema was staged");

    auto invalid_executable = bundle(1);
    invalid_executable.executable_policy.schema_version = 0;
    expect_stage(invalid_executable,
                 ApplyCode::invalid_executable_policy_contract,
                 "invalid executable policy was staged");

    auto invalid_instrument = bundle(1);
    invalid_instrument.instrument.schema_version = 0;
    expect_stage(invalid_instrument,
                 ApplyCode::invalid_instrument_profile_contract,
                 "invalid instrument economics were staged");

    auto invalid_venue = bundle(1);
    invalid_venue.venue.schema_version = 0;
    expect_stage(invalid_venue,
                 ApplyCode::invalid_venue_capabilities_contract,
                 "invalid venue capabilities were staged");

    auto unbound = bundle(1);
    unbound.instrument.instrument_id = InstrumentId{99};
    expect_stage(unbound, ApplyCode::executable_policy_binding_failed,
                 "cross-scope executable bundle was staged");

    TradingState proposed;
    ApplyScratch proposed_scratch;
    require(apply(proposed, command(1, PublishPolicy{policy(1)}),
                  proposed_scratch).code == ApplyCode::applied &&
                apply(proposed, command(2, bundle(1)), proposed_scratch).code ==
                    ApplyCode::invalid_policy_transition,
            "proposed base policy accepted executable authority");

    TradingState no_executable;
    no_executable.strategy_mode = StrategyMode::normal;
    ApplyScratch no_executable_scratch;
    require(apply(no_executable, command(1, PublishPolicy{policy(1)}),
                  no_executable_scratch).code == ApplyCode::applied &&
                apply(no_executable,
                      command(2, ValidatePolicy{PolicyId{10}, PolicyVersion{1}}),
                      no_executable_scratch).code == ApplyCode::applied &&
                apply(no_executable,
                      command(3, ActivatePolicy{PolicyId{10}, PolicyVersion{1}}),
                      no_executable_scratch).code == ApplyCode::applied &&
                apply(no_executable,
                      command(4, RecordSourceEvent{SourceId{12},
                                                   SourceGeneration{1},
                                                   SourceSequence{1},
                                                   digest(0x44)}),
                      no_executable_scratch).code == ApplyCode::applied,
            "fail-closed decision fixture setup failed");
    const auto rejected = apply(
        no_executable,
        command(5, EvaluateDecisionFrame{DecisionId{30},
                                         EvaluationMode::authoritative,
                                         frame(1)}),
        no_executable_scratch);
    require(rejected.decision_code == DecisionCode::executable_policy_not_active &&
                rejected.effective_directive == Directive::no_action &&
                rejected.action_resolution_code == ActionResolutionCode::none,
            "base policy executed without matching executable authority");
}

}  // namespace

int main() {
    try {
        test_atomic_stage_activate_and_replace();
        test_digest_and_identity_fail_closed();
        test_stage_rejection_reasons_and_fail_closed_decision();
        std::cout << "all V2 executable-policy kernel tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 executable-policy kernel test failed: " << error.what()
                  << '\n';
        return 1;
    }
}
