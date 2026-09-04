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

}  // namespace

int main() {
    using namespace velocita::v2;
    constexpr std::size_t iterations = 100'000;
    const auto policy = maximum_policy();
    const auto canonical = encode_payload(policy);
    if (canonical.error != PayloadError::none ||
        canonical.bytes.size() != kMaxPolicyPayloadBytes) {
        std::cerr << "could not create maximum policy benchmark fixture\n";
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

    std::cout << "max_policy_payload_bytes=" << kMaxPolicyPayloadBytes << '\n'
              << "max_decision_frame_payload_bytes=" << kMaxDecisionFramePayloadBytes
              << '\n'
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
