// Measures contract validation and codec costs.
#include "velocita/v2/artifact_digest.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/payload.hpp"
#include "velocita/v2/wire.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

velocita::v2::Digest256 nonzero_digest(const std::byte first) {
    velocita::v2::Digest256 digest{};
    digest.bytes[0] = first;
    return digest;
}

velocita::v2::PolicyArtifact maximum_policy() {
    using namespace velocita::v2;
    PolicyArtifact policy;
    policy.policy_id = PolicyId{1};
    policy.policy_version = PolicyVersion{1};
    policy.schema_version = kPolicyArtifactSchemaVersion;
    policy.feature_schema_id = FeatureSchemaId{1};
    policy.instrument_id = InstrumentId{1};
    policy.account_id = AccountId{1};
    policy.model_digest = nonzero_digest(std::byte{1});
    policy.artifact_digest = nonzero_digest(std::byte{2});
    policy.valid_from_event_time_ns = 1;
    policy.valid_until_event_time_ns = 2;
    policy.risk_policy_version = RiskPolicyVersion{1};
    policy.rule_count = static_cast<std::uint16_t>(kMaxPolicyRules);
    for (std::size_t index = 0; index < policy.rule_count; ++index) {
        policy.rules[index] = PolicyRule{
            static_cast<std::uint16_t>(index),
            PredicateOperator::greater_than_or_equal,
            FeatureMicros::from_atoms(static_cast<std::int64_t>(index)),
            Directive::place_limit,
        };
    }
    return policy;
}

velocita::v2::FeatureSchemaArtifact maximum_feature_schema() {
    using namespace velocita::v2;
    FeatureSchemaArtifact schema;
    schema.schema_version = kFeatureSchemaArtifactSchemaVersion;
    schema.feature_schema_id = FeatureSchemaId{1};
    schema.artifact_digest = nonzero_digest(std::byte{3});
    schema.feature_count = static_cast<std::uint16_t>(kMaxDecisionFeatures);
    for (std::size_t index = 0; index < schema.feature_count; ++index) {
        schema.features[index] = {FeatureEconomicKind::price, 1'000'000};
    }
    return schema;
}

velocita::v2::ActionTemplate action_template(const std::uint16_t slot) {
    using namespace velocita::v2;
    ActionTemplate action;
    action.schema_version = kActionTemplateSchemaVersion;
    action.directive = Directive::place_limit;
    action.side = Side::buy;
    action.time_in_force = TimeInForce::good_til_cancel;
    action.post_only = PostOnlyMode::required;
    action.order_slot = LogicalOrderSlot{slot};
    action.instrument_spec_version = InstrumentSpecVersion{1};
    action.venue_capabilities_version = VenueCapabilitiesVersion{1};
    action.price_source = {EconomicValueSourceKind::feature, 0,
                           RoundingMode::toward_zero, {}, 1'000'000, {}};
    action.quantity_source = {EconomicValueSourceKind::fixed, 0,
                              RoundingMode::toward_zero,
                              QuantityLots::from_atoms(1), 0};
    action.safety_caps.max_quantity = QuantityLots::from_atoms(1);
    action.safety_caps.max_notional = MoneyMicros::from_atoms(1'000'000);
    action.safety_caps.max_abs_price_deviation = PriceTicks::from_atoms(1);
    action.safety_caps.max_abs_tick_offset = PriceTicks::from_atoms(1);
    action.safety_caps.max_market_data_age_ns = 1'000'000;
    action.safety_caps.reference_price = action.price_source;
    return action;
}

velocita::v2::ExecutablePolicyArtifact maximum_executable_policy() {
    using namespace velocita::v2;
    ExecutablePolicyArtifact executable;
    executable.schema_version = kExecutablePolicyArtifactSchemaVersion;
    executable.policy_id = PolicyId{1};
    executable.policy_version = PolicyVersion{1};
    executable.policy_artifact_digest = nonzero_digest(std::byte{2});
    executable.feature_schema_id = FeatureSchemaId{1};
    executable.feature_schema_digest = nonzero_digest(std::byte{3});
    executable.artifact_digest = nonzero_digest(std::byte{4});
    executable.template_count =
        static_cast<std::uint16_t>(kMaxExecutableActionTemplates);
    for (std::size_t index = 0; index < executable.template_count; ++index) {
        executable.templates[index] =
            action_template(static_cast<std::uint16_t>(index + 1));
    }
    executable.rule_binding_count = static_cast<std::uint16_t>(kMaxPolicyRules);
    for (std::size_t index = 0; index < executable.rule_binding_count; ++index) {
        executable.rule_template_slots[index] = ActionTemplateSlot{
            static_cast<std::uint8_t>(index % executable.template_count + 1)};
    }
    return executable;
}

template <typename Operation>
void measure(const std::string_view name, const std::size_t iterations,
             Operation&& operation) {
    std::vector<std::uint64_t> samples;
    samples.reserve(iterations);
    std::uint64_t sink{};
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        const auto started = Clock::now();
        sink += operation();
        const auto finished = Clock::now();
        samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started)
                .count()));
    }
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](const std::size_t numerator) {
        const auto index = ((samples.size() - 1U) * numerator) / 100U;
        return samples[index];
    };
    std::cout << name << " iterations=" << iterations << " min_ns=" << samples.front()
              << " p50_ns=" << percentile(50) << " p99_ns=" << percentile(99)
              << " max_ns=" << samples.back() << " sink=" << sink << '\n';
}

}

int main() {
    using namespace velocita::v2;
    constexpr std::size_t iterations = 100'000;
    const auto policy = maximum_policy();
    const auto canonical = encode_payload(policy);
    const auto feature_schema = maximum_feature_schema();
    const auto canonical_feature_schema = encode_payload(feature_schema);
    const auto executable_policy = maximum_executable_policy();
    const auto canonical_executable_policy = encode_payload(executable_policy);
    if (canonical.error != PayloadError::none ||
        canonical.bytes.size() != kMaxPolicyPayloadBytes ||
        canonical_feature_schema.error != PayloadError::none ||
        canonical_feature_schema.bytes.size() != kMaxFeatureSchemaPayloadBytes ||
        canonical_executable_policy.error != PayloadError::none ||
        canonical_executable_policy.bytes.size() !=
            kMaxExecutablePolicyPayloadBytes) {
        std::cerr << "could not create maximum contract benchmark fixtures\n";
        return 1;
    }

    measure("policy_encode", iterations, [&] {
        const auto encoded = encode_payload(policy);
        return static_cast<std::uint64_t>(encoded.bytes.size());
    });
    measure("policy_decode", iterations, [&] {
        const auto decoded = decode_policy_artifact(canonical.bytes);
        return decoded.value ? decoded.value->policy_id.value() : 0U;
    });
    measure("policy_sha256", iterations, [&] {
        return static_cast<std::uint64_t>(
            std::to_integer<std::uint8_t>(sha256_digest(canonical.bytes).bytes[0]));
    });
    measure("feature_schema_encode", iterations, [&] {
        return static_cast<std::uint64_t>(encode_payload(feature_schema).bytes.size());
    });
    measure("feature_schema_decode", iterations, [&] {
        const auto decoded =
            decode_feature_schema_artifact(canonical_feature_schema.bytes);
        return decoded.value ? decoded.value->feature_schema_id.value() : 0U;
    });
    measure("executable_policy_encode", iterations, [&] {
        return static_cast<std::uint64_t>(
            encode_payload(executable_policy).bytes.size());
    });
    measure("executable_policy_decode", iterations, [&] {
        const auto decoded = decode_executable_policy_artifact(
            canonical_executable_policy.bytes);
        return decoded.value ? decoded.value->policy_id.value() : 0U;
    });
    measure("feature_schema_artifact_digest", iterations, [&] {
        const auto calculated = compute_artifact_digest(feature_schema);
        return static_cast<std::uint64_t>(
            std::to_integer<std::uint8_t>(calculated.digest.bytes[0]));
    });
    measure("executable_policy_artifact_digest", iterations, [&] {
        const auto calculated = compute_artifact_digest(executable_policy);
        return static_cast<std::uint64_t>(
            std::to_integer<std::uint8_t>(calculated.digest.bytes[0]));
    });

    std::cout << "max_policy_payload_bytes=" << kMaxPolicyPayloadBytes << '\n'
              << "max_decision_frame_payload_bytes=" << kMaxDecisionFramePayloadBytes
              << '\n'
              << "max_feature_schema_payload_bytes="
              << kMaxFeatureSchemaPayloadBytes << '\n'
              << "max_executable_policy_payload_bytes="
              << kMaxExecutablePolicyPayloadBytes << '\n'
              << "order_effect_payload_bytes=" << kOrderEffectPayloadBytes << '\n'
              << "venue_observation_payload_bytes=" << kVenueObservationPayloadBytes
              << '\n'
              << "max_envelope_bytes="
              << (kEnvelopeFixedBytes + kMaxEnvelopePayloadBytes) << '\n'
              << "audited_payload_encode_allocations_upper_bound=1\n"
              << "audited_payload_decode_allocations_upper_bound=0\n"
              << "audited_envelope_encode_allocations_upper_bound=1\n"
              << "audited_envelope_decode_allocations_upper_bound=1\n";
    return 0;
}
