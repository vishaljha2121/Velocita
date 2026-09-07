#include "velocita/v2/artifact_digest.hpp"
#include "velocita/v2/kernel.hpp"
#include "velocita/v2/rule_evaluator.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using namespace velocita::v2;

Digest256 digest(const std::uint8_t marker) {
    Digest256 value;
    value.bytes[0] = static_cast<std::byte>(marker);
    return value;
}

PolicyArtifact worst_case_policy() {
    PolicyArtifact value;
    value.schema_version = kPolicyArtifactSchemaVersion;
    value.policy_id = PolicyId{1};
    value.policy_version = PolicyVersion{1};
    value.feature_schema_id = FeatureSchemaId{1};
    value.instrument_id = InstrumentId{1};
    value.account_id = AccountId{1};
    value.model_digest = digest(1);
    value.artifact_digest = digest(2);
    value.valid_from_event_time_ns = 1;
    value.valid_until_event_time_ns = 1'000;
    value.risk_policy_version = RiskPolicyVersion{1};
    value.rule_count = static_cast<std::uint16_t>(kMaxPolicyRules);
    for (std::size_t index = 0; index < value.rule_count; ++index) {
        value.rules[index] = {
            0, PredicateOperator::greater_than,
            FeatureMicros::from_atoms(index + 1 == value.rule_count ? 50 : 1'000),
            Directive::place_limit};
    }
    return value;
}

DecisionFrame frame() {
    DecisionFrame value;
    value.policy_id = PolicyId{1};
    value.policy_version = PolicyVersion{1};
    value.feature_schema_id = FeatureSchemaId{1};
    value.venue_id = VenueId{1};
    value.venue_session_id = VenueSessionId{1};
    value.venue_sequence = VenueSequence{1};
    value.source_id = SourceId{1};
    value.source_generation = SourceGeneration{1};
    value.source_sequence = SourceSequence{1};
    value.event_time_ns = 100;
    value.receive_time_ns = 101;
    value.instrument_id = InstrumentId{1};
    value.account_id = AccountId{1};
    value.feature_count = 1;
    value.features[0] = FeatureMicros::from_atoms(100);
    value.market_data_status = MarketDataStatus::ready;
    value.market_data_age_ns = 1;
    value.position_version = PositionVersion{1};
    value.risk_version = RiskVersion{1};
    value.raw_evidence_digest = digest(3);
    return value;
}

StageExecutablePolicy executable_bundle(const PolicyArtifact& base) {
    StageExecutablePolicy value;
    value.feature_schema.schema_version = kFeatureSchemaArtifactSchemaVersion;
    value.feature_schema.feature_schema_id = base.feature_schema_id;
    value.feature_schema.feature_count = 1;
    value.feature_schema.features[0] = {FeatureEconomicKind::price, 1};
    value.feature_schema.artifact_digest =
        compute_artifact_digest(value.feature_schema).digest;

    auto& executable = value.executable_policy;
    executable.schema_version = kExecutablePolicyArtifactSchemaVersion;
    executable.policy_id = base.policy_id;
    executable.policy_version = base.policy_version;
    executable.policy_artifact_digest = base.artifact_digest;
    executable.feature_schema_id = base.feature_schema_id;
    executable.feature_schema_digest = value.feature_schema.artifact_digest;
    executable.template_count = 1;
    auto& action = executable.templates[0];
    action.schema_version = kActionTemplateSchemaVersion;
    action.directive = Directive::place_limit;
    action.side = Side::buy;
    action.time_in_force = TimeInForce::good_til_cancel;
    action.post_only = PostOnlyMode::required;
    action.order_slot = LogicalOrderSlot{1};
    action.instrument_spec_version = InstrumentSpecVersion{1};
    action.venue_capabilities_version = VenueCapabilitiesVersion{1};
    action.price_source = {EconomicValueSourceKind::feature, 0,
                           RoundingMode::toward_zero, {}, 1, {}};
    action.quantity_source = {EconomicValueSourceKind::fixed, 0,
                              RoundingMode::toward_zero,
                              QuantityLots::from_atoms(1), 0};
    action.safety_caps = {QuantityLots::from_atoms(1),
                          MoneyMicros::from_atoms(1'000),
                          PriceTicks::from_atoms(1), PriceTicks::from_atoms(1),
                          1'000, action.price_source};
    executable.rule_binding_count = base.rule_count;
    for (std::size_t index = 0; index < executable.rule_binding_count; ++index) {
        executable.rule_template_slots[index] = ActionTemplateSlot{1};
    }
    executable.artifact_digest = compute_artifact_digest(executable).digest;

    value.instrument = {kInstrumentExecutionProfileSchemaVersion, VenueId{1},
                        InstrumentId{1}, InstrumentSpecVersion{1},
                        MoneyMicros::from_atoms(1), PriceTicks::from_atoms(1),
                        PriceTicks::from_atoms(1'000), QuantityLots::from_atoms(1),
                        QuantityLots::from_atoms(10)};
    value.venue = {kVenueCapabilitiesSchemaVersion, VenueId{1},
                   VenueCapabilitiesVersion{1}, kVenueDirectivePlaceLimit,
                   kVenueTifGtc, 1};
    return value;
}

template <typename Operation>
void measure(const std::string_view name,
             const std::size_t iterations,
             Operation&& operation) {
    std::vector<std::uint64_t> samples;
    samples.reserve(iterations);
    std::uint64_t sink{};
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        const auto started = Clock::now();
        sink += operation(iteration);
        const auto finished = Clock::now();
        samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started)
                .count()));
    }
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](const std::size_t numerator) {
        return samples[((samples.size() - 1U) * numerator) / 100U];
    };
    std::cout << name << " iterations=" << iterations
              << " min_ns=" << samples.front()
              << " p50_ns=" << percentile(50)
              << " p99_ns=" << percentile(99)
              << " max_ns=" << samples.back() << " sink=" << sink << '\n';
}

Command command(const std::uint64_t sequence, CommandBody body) {
    return {{CallerId{1}, RequestSequence{sequence}, digest(9)}, std::move(body)};
}

bool prepare(TradingState& state,
             ApplyScratch& scratch,
             const PolicyArtifact& policy,
             const StageExecutablePolicy& bundle) {
    state.strategy_mode = StrategyMode::normal;
    return apply(state, command(1, PublishPolicy{policy}), scratch).code ==
               ApplyCode::applied &&
           apply(state,
                 command(2, ValidatePolicy{policy.policy_id,
                                           policy.policy_version}),
                 scratch).code == ApplyCode::applied &&
           apply(state, command(3, bundle), scratch).code == ApplyCode::applied &&
           apply(state,
                 command(4, ActivateExecutablePolicy{
                                policy.policy_id, policy.policy_version,
                                bundle.executable_policy.artifact_digest}),
                 scratch).code == ApplyCode::applied &&
           apply(state,
                 command(5, RecordSourceEvent{SourceId{1}, SourceGeneration{1},
                                              SourceSequence{1}, digest(3)}),
                 scratch).code == ApplyCode::applied;
}

}  // namespace

int main() {
    constexpr std::size_t iterations = 100'000;
    const auto policy = worst_case_policy();
    const auto decision_frame = frame();
    const auto bundle = executable_bundle(policy);

    const auto probe = evaluate_rules(policy, decision_frame);
    if (probe.code != RuleEvaluationCode::matched ||
        probe.rules_visited != kMaxPolicyRules) {
        std::cerr << "could not create worst-case Phase 3 fixture\n";
        return 1;
    }

    measure("phase3_rule_evaluation_last_match", iterations,
            [&](const std::size_t) {
                const auto result = evaluate_rules(policy, decision_frame);
                return static_cast<std::uint64_t>(result.rules_visited);
            });
    measure("phase3_action_resolution", iterations, [&](const std::size_t index) {
        const auto result = resolve_action(
            DecisionId{index + 1}, policy, decision_frame,
            bundle.executable_policy.templates[0], bundle.instrument, bundle.venue);
        return static_cast<std::uint64_t>(result.code);
    });

    TradingState state;
    ApplyScratch scratch;
    if (!prepare(state, scratch, policy, bundle)) {
        std::cerr << "could not prepare authoritative reducer benchmark fixture\n";
        return 1;
    }
    // Setup receipts are outside the timed workload. The authoritative state
    // they produced remains, while the bounded receipt arena starts empty.
    state.receipt_count = 0;
    std::size_t decision_high_watermark{};
    std::size_t receipt_high_watermark{};
    measure("phase3_authoritative_reducer", iterations,
            [&](const std::size_t index) {
                if (state.decision_count == kMaxDecisionRecords) {
                    state.decision_count = 0;
                    state.receipt_count = 0;
                }
                auto current_frame = decision_frame;
                current_frame.venue_sequence = VenueSequence{index + 2};
                const auto result = apply(
                    state,
                    command(index + 6,
                            EvaluateDecisionFrame{DecisionId{index + 1},
                                                  EvaluationMode::authoritative,
                                                  current_frame}),
                    scratch);
                decision_high_watermark =
                    std::max(decision_high_watermark, state.decision_count);
                receipt_high_watermark =
                    std::max(receipt_high_watermark, state.receipt_count);
                return static_cast<std::uint64_t>(result.rules_visited) +
                       static_cast<std::uint64_t>(result.action_resolution_code);
            });
    std::cout << "phase3_rules_visited_worst_case=" << probe.rules_visited << '\n'
              << "phase3_decision_capacity_high_watermark="
              << decision_high_watermark << '\n'
              << "phase3_receipt_capacity_high_watermark="
              << receipt_high_watermark << '\n'
              << "phase3_decision_capacity_limit=" << kMaxDecisionRecords << '\n'
              << "phase3_receipt_capacity_limit=" << kMaxRequestReceipts << '\n'
              << "phase3_measurement_scope=in_memory_no_raft_no_wal_no_network\n";
    return decision_high_watermark == kMaxDecisionRecords ? 0 : 1;
}
