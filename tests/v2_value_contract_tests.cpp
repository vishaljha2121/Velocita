#include "velocita/v2/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Left, typename Right>
concept CheckedAddable = requires(Left left, Right right) {
    left.checked_add(right);
};

velocita::v2::Digest256 digest_with_first_byte(const std::byte value) {
    velocita::v2::Digest256 digest{};
    digest.bytes[0] = value;
    return digest;
}

void test_dimension_safe_trading_numbers() {
    using namespace velocita::v2;

    static_assert(PriceTicks::scale == 1);
    static_assert(QuantityLots::scale == 1);
    static_assert(MoneyMicros::scale == 1'000'000);
    static_assert(FeeMicros::scale == 1'000'000);
    static_assert(FeatureMicros::scale == 1'000'000);
    static_assert(!std::is_same_v<PriceTicks, QuantityLots>);
    static_assert(!std::is_same_v<MoneyMicros, FeeMicros>);
    static_assert(!CheckedAddable<PriceTicks, QuantityLots>);
    static_assert(!CheckedAddable<MoneyMicros, FeeMicros>);

    constexpr auto price = PriceTicks::from_atoms(12'345);
    constexpr auto quantity = QuantityLots::from_atoms(7);
    static_assert(price.atoms() == 12'345);
    static_assert(quantity.atoms() == 7);
}

velocita::v2::PolicyArtifact valid_policy() {
    using namespace velocita::v2;
    PolicyArtifact policy;
    policy.policy_id = PolicyId{1};
    policy.policy_version = PolicyVersion{2};
    policy.schema_version = kPolicyArtifactSchemaVersion;
    policy.feature_schema_id = FeatureSchemaId{3};
    policy.instrument_id = InstrumentId{4};
    policy.account_id = AccountId{5};
    policy.model_digest = digest_with_first_byte(std::byte{0x11});
    policy.artifact_digest = digest_with_first_byte(std::byte{0x22});
    policy.valid_from_event_time_ns = 100;
    policy.valid_until_event_time_ns = 200;
    policy.risk_policy_version = RiskPolicyVersion{6};
    policy.rule_count = 1;
    policy.rules[0] = PolicyRule{
        0,
        PredicateOperator::greater_than_or_equal,
        FeatureMicros::from_atoms(1'250'000),
        Directive::place_limit,
    };
    return policy;
}

void test_policy_artifact_contract() {
    using namespace velocita::v2;

    auto policy = valid_policy();
    require(validate(policy) == ContractError::none, "valid policy was rejected");

    policy.policy_id = PolicyId{};
    require(validate(policy) == ContractError::invalid_identity,
            "zero policy identity was accepted");

    policy = valid_policy();
    policy.schema_version += 1;
    require(validate(policy) == ContractError::unsupported_schema,
            "unsupported policy schema was accepted");

    policy = valid_policy();
    policy.artifact_digest = {};
    require(validate(policy) == ContractError::missing_digest,
            "missing artifact digest was accepted");

    policy = valid_policy();
    policy.valid_until_event_time_ns = policy.valid_from_event_time_ns;
    require(validate(policy) == ContractError::invalid_time_range,
            "empty policy validity interval was accepted");

    policy = valid_policy();
    policy.rule_count = kMaxPolicyRules + 1;
    require(validate(policy) == ContractError::capacity_exceeded,
            "oversized policy rule set was accepted");

    policy = valid_policy();
    policy.rules[0].feature_index = kMaxDecisionFeatures;
    require(validate(policy) == ContractError::invalid_feature_index,
            "out-of-range policy feature index was accepted");
}

velocita::v2::DecisionFrame valid_frame() {
    using namespace velocita::v2;
    DecisionFrame frame;
    frame.policy_id = PolicyId{1};
    frame.policy_version = PolicyVersion{2};
    frame.feature_schema_id = FeatureSchemaId{3};
    frame.venue_id = VenueId{4};
    frame.venue_session_id = VenueSessionId{5};
    frame.venue_sequence = VenueSequence{6};
    frame.source_id = SourceId{11};
    frame.source_generation = SourceGeneration{12};
    frame.source_sequence = SourceSequence{13};
    frame.event_time_ns = 1'000'000;
    frame.receive_time_ns = 1'000'100;
    frame.instrument_id = InstrumentId{7};
    frame.account_id = AccountId{8};
    frame.feature_count = 2;
    frame.features[0] = FeatureMicros::from_atoms(-500'000);
    frame.features[1] = FeatureMicros::from_atoms(2'000'000);
    frame.market_data_status = MarketDataStatus::ready;
    frame.market_data_age_ns = 1'000;
    frame.position_version = PositionVersion{9};
    frame.risk_version = RiskVersion{10};
    frame.raw_evidence_digest = digest_with_first_byte(std::byte{0x33});
    return frame;
}

void test_decision_frame_contract() {
    using namespace velocita::v2;

    auto frame = valid_frame();
    require(validate(frame) == ContractError::none, "valid decision frame was rejected");

    frame.source_id = SourceId{};
    require(validate(frame) == ContractError::invalid_identity,
            "decision frame without source identity was accepted");

    frame = valid_frame();
    frame.source_generation = SourceGeneration{};
    require(validate(frame) == ContractError::invalid_identity,
            "decision frame without source generation was accepted");

    frame = valid_frame();
    frame.source_sequence = SourceSequence{};
    require(validate(frame) == ContractError::invalid_identity,
            "decision frame without source sequence was accepted");

    frame = valid_frame();
    frame.event_time_ns = 0;
    require(validate(frame) == ContractError::invalid_time_range,
            "decision frame without event time was accepted");

    frame = valid_frame();
    frame.receive_time_ns = 0;
    require(validate(frame) == ContractError::invalid_time_range,
            "decision frame without receive time was accepted");

    frame = valid_frame();
    frame.event_time_ns = frame.receive_time_ns + 1;
    require(validate(frame) == ContractError::none,
            "decision frame assumed event and receive timestamps share a clock domain");

    frame.feature_count = kMaxDecisionFeatures + 1;
    require(validate(frame) == ContractError::capacity_exceeded,
            "oversized decision frame was accepted");

    frame = valid_frame();
    frame.feature_count = 0;
    require(validate(frame) == ContractError::empty_features,
            "empty decision frame was accepted");

    frame = valid_frame();
    frame.market_data_age_ns = -1;
    require(validate(frame) == ContractError::invalid_age,
            "negative market-data age was accepted");

    frame = valid_frame();
    frame.raw_evidence_digest = {};
    require(validate(frame) == ContractError::missing_digest,
            "decision frame without evidence digest was accepted");
}

velocita::v2::OrderEffect valid_effect() {
    using namespace velocita::v2;
    OrderEffect effect;
    effect.effect_id = EffectId{1};
    effect.cluster_order_id = ClusterOrderId{2};
    effect.committed_directive_index = 3;
    effect.executor_generation = ExecutorGeneration{4};
    effect.kill_epoch = KillEpoch{5};
    effect.venue_id = VenueId{6};
    effect.account_id = AccountId{7};
    effect.instrument_id = InstrumentId{8};
    effect.operation = EffectOperation::submit;
    effect.side = Side::buy;
    effect.price = PriceTicks::from_atoms(10'000);
    effect.quantity = QuantityLots::from_atoms(9);
    effect.time_in_force = TimeInForce::good_til_cancel;
    effect.policy_id = PolicyId{10};
    effect.policy_version = PolicyVersion{11};
    effect.decision_id = DecisionId{12};
    effect.risk_version = RiskVersion{13};
    effect.source_generation = SourceGeneration{14};
    effect.source_sequence = SourceSequence{15};
    effect.payload_digest = digest_with_first_byte(std::byte{0x44});
    return effect;
}

void test_order_effect_contract() {
    using namespace velocita::v2;

    auto effect = valid_effect();
    require(validate(effect) == ContractError::none, "valid order effect was rejected");

    effect.quantity = QuantityLots::from_atoms(0);
    require(validate(effect) == ContractError::non_positive_quantity,
            "zero order quantity was accepted");

    effect = valid_effect();
    effect.price = PriceTicks::from_atoms(-1);
    require(validate(effect) == ContractError::non_positive_price,
            "negative limit price was accepted");

    effect = valid_effect();
    effect.payload_digest = {};
    require(validate(effect) == ContractError::missing_digest,
            "effect without payload digest was accepted");
}

velocita::v2::VenueObservation valid_fill() {
    using namespace velocita::v2;
    VenueObservation observation;
    observation.kind = VenueObservationKind::fill;
    observation.venue_id = VenueId{1};
    observation.venue_session_id = VenueSessionId{2};
    observation.venue_sequence = VenueSequence{3};
    observation.cluster_order_id = ClusterOrderId{4};
    observation.effect_id = EffectId{5};
    observation.venue_order_id = VenueOrderId{6};
    observation.execution_id = ExecutionId{7};
    observation.side = Side::sell;
    observation.price = PriceTicks::from_atoms(9'999);
    observation.quantity = QuantityLots::from_atoms(2);
    observation.fee = FeeMicros::from_atoms(25'000);
    observation.evidence_digest = digest_with_first_byte(std::byte{0x55});
    return observation;
}

void test_venue_observation_contract() {
    using namespace velocita::v2;

    auto fill = valid_fill();
    require(validate(fill) == ContractError::none, "valid fill observation was rejected");

    fill.execution_id = ExecutionId{};
    require(validate(fill) == ContractError::invalid_identity,
            "fill without execution identity was accepted");

    fill = valid_fill();
    fill.quantity = QuantityLots::from_atoms(0);
    require(validate(fill) == ContractError::non_positive_quantity,
            "fill without positive quantity was accepted");

    auto acknowledgement = valid_fill();
    acknowledgement.kind = VenueObservationKind::order_acknowledged;
    acknowledgement.execution_id = ExecutionId{};
    acknowledgement.price = PriceTicks{};
    acknowledgement.quantity = QuantityLots{};
    acknowledgement.fee = FeeMicros{};
    require(validate(acknowledgement) == ContractError::none,
            "valid acknowledgement observation was rejected");

    acknowledgement.venue_order_id = VenueOrderId{};
    require(validate(acknowledgement) == ContractError::invalid_identity,
            "acknowledgement without venue order identity was accepted");
}

}

int main() {
    try {
        test_dimension_safe_trading_numbers();
        test_policy_artifact_contract();
        test_decision_frame_contract();
        test_order_effect_contract();
        test_venue_observation_contract();
        std::cout << "all V2 value-contract tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 value-contract test failed: " << error.what() << '\n';
        return 1;
    }
}
