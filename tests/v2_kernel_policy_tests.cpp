#include "v2_catalog_test_support.hpp"
#include "velocita/v2/kernel.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

velocita::v2::Digest256 digest(const std::byte first) {
    velocita::v2::Digest256 result;
    result.bytes[0] = first;
    return result;
}

velocita::v2::PolicyArtifact policy(const std::uint64_t id,
                                    const std::uint64_t version,
                                    const std::byte artifact_marker,
                                    const std::uint64_t account = 5,
                                    const std::uint64_t instrument = 4) {
    using namespace velocita::v2;
    PolicyArtifact value;
    value.policy_id = PolicyId{id};
    value.policy_version = PolicyVersion{version};
    value.schema_version = kPolicyArtifactSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{3};
    value.instrument_id = InstrumentId{instrument};
    value.account_id = AccountId{account};
    value.model_digest = digest(std::byte{0x11});
    value.artifact_digest = digest(artifact_marker);
    value.valid_from_event_time_ns = 100;
    value.valid_until_event_time_ns = 200;
    value.risk_policy_version = RiskPolicyVersion{6};
    value.rule_count = 1;
    value.rules[0] = PolicyRule{0,
                                PredicateOperator::greater_than_or_equal,
                                FeatureMicros::from_atoms(1'250'000),
                                Directive::place_limit};
    return value;
}

velocita::v2::Command command(const std::uint64_t request_sequence,
                              const std::byte payload_marker,
                              velocita::v2::CommandBody body) {
    using namespace velocita::v2;
    return Command{CommandIdentity{CallerId{7},
                                   RequestSequence{request_sequence},
                                   digest(payload_marker)},
                   std::move(body)};
}

void test_publish_is_bounded_and_idempotent() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;
    const auto publish = command(1, std::byte{0x31}, PublishPolicy{policy(10, 1, std::byte{0x21})});

    const auto first = catalog_test::apply(state, publish, scratch);
    require(first.code == ApplyCode::applied &&
                first.policy_lifecycle == PolicyLifecycle::proposed,
            "valid publication was not applied as proposed");
    require(first.state_version == 1 && state.state_version == 1,
            "first retained command did not advance state exactly once");
    require(state.policy_count == 1 && state.receipt_count == 1,
            "publication and receipt were not retained atomically");

    const auto duplicate = catalog_test::apply(state, publish, scratch);
    require(duplicate == first, "exact duplicate did not return the retained outcome");
    require(state.state_version == 1 && state.policy_count == 1 && state.receipt_count == 1,
            "exact duplicate mutated authoritative state");

    auto conflicting = publish;
    conflicting.identity.payload_digest = digest(std::byte{0x32});
    const auto conflict = catalog_test::apply(state, conflicting, scratch);
    require(conflict.code == ApplyCode::duplicate_conflict,
            "same request identity with different bytes was not rejected");
    require(state.state_version == 1 && state.policy_count == 1 && state.receipt_count == 1,
            "conflicting duplicate partially mutated state");
}

void test_policy_identity_is_immutable_and_versioned() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;

    require(catalog_test::apply(state,
                  command(1, std::byte{0x41},
                          PublishPolicy{policy(10, 2, std::byte{0x21})}),
                  scratch).code == ApplyCode::applied,
            "initial policy publication failed");

    const auto same_identity_new_bytes = catalog_test::apply(
        state,
        command(2, std::byte{0x42}, PublishPolicy{policy(10, 2, std::byte{0x22})}),
        scratch);
    require(same_identity_new_bytes.code == ApplyCode::immutable_policy_conflict,
            "immutable policy identity accepted a different artifact digest");

    const auto lower_version = catalog_test::apply(
        state,
        command(3, std::byte{0x43}, PublishPolicy{policy(10, 1, std::byte{0x23})}),
        scratch);
    require(lower_version.code == ApplyCode::non_monotonic_policy_version,
            "policy family accepted a regressing version");
    const auto retained_version = state.state_version;
    const auto lower_version_retry = catalog_test::apply(
        state,
        command(3, std::byte{0x43}, PublishPolicy{policy(10, 1, std::byte{0x23})}),
        scratch);
    require(lower_version_retry == lower_version && state.state_version == retained_version,
            "typed policy rejection was not retained byte-equivalently");

    require(catalog_test::apply(state,
                  command(4, std::byte{0x44},
                          PublishPolicy{policy(10, 3, std::byte{0x24})}),
                  scratch).code == ApplyCode::applied,
            "strictly newer policy version was rejected");
    require(state.policy_count == 2 && state.receipt_count == 4,
            "typed rejections were not retained for deterministic retry");
}

void test_validate_activate_supersede_lifecycle() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;

    require(catalog_test::apply(state,
                  command(1, std::byte{0x51},
                          PublishPolicy{policy(10, 1, std::byte{0x21})}),
                  scratch).code == ApplyCode::applied,
            "policy publication failed");

    const auto too_early = catalog_test::apply(
        state,
        command(2, std::byte{0x52}, ActivatePolicy{PolicyId{10}, PolicyVersion{1}}),
        scratch);
    require(too_early.code == ApplyCode::invalid_policy_transition,
            "proposed policy activated without validation");

    const auto validated = catalog_test::apply(
        state,
        command(3, std::byte{0x53}, ValidatePolicy{PolicyId{10}, PolicyVersion{1}}),
        scratch);
    require(validated.code == ApplyCode::applied &&
                validated.policy_lifecycle == PolicyLifecycle::validated,
            "policy validation transition failed");

    const auto activated = catalog_test::apply(
        state,
        command(4, std::byte{0x54}, ActivatePolicy{PolicyId{10}, PolicyVersion{1}}),
        scratch);
    require(activated.code == ApplyCode::applied &&
                activated.policy_lifecycle == PolicyLifecycle::active,
            "validated policy did not activate");

    require(catalog_test::apply(state,
                  command(5, std::byte{0x55},
                          PublishPolicy{policy(11, 1, std::byte{0x22})}),
                  scratch).code == ApplyCode::applied,
            "second policy publication failed");
    require(catalog_test::apply(state,
                  command(6, std::byte{0x56},
                          ValidatePolicy{PolicyId{11}, PolicyVersion{1}}),
                  scratch).code == ApplyCode::applied,
            "second policy validation failed");

    const auto scope_conflict = catalog_test::apply(
        state,
        command(7, std::byte{0x57}, ActivatePolicy{PolicyId{11}, PolicyVersion{1}}),
        scratch);
    require(scope_conflict.code == ApplyCode::active_policy_scope_conflict,
            "two policies became active for one account/instrument scope");

    const auto superseded = catalog_test::apply(
        state,
        command(8, std::byte{0x58}, SupersedePolicy{PolicyId{10}, PolicyVersion{1}}),
        scratch);
    require(superseded.code == ApplyCode::applied &&
                superseded.policy_lifecycle == PolicyLifecycle::superseded,
            "active policy did not become superseded");

    require(catalog_test::apply(state,
                  command(9, std::byte{0x59},
                          ActivatePolicy{PolicyId{11}, PolicyVersion{1}}),
                  scratch).code == ApplyCode::applied,
            "validated replacement did not activate after supersession");
}

void test_invalid_input_and_capacity_fail_closed() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;

    auto missing_identity = command(
        1, std::byte{0x61}, PublishPolicy{policy(10, 1, std::byte{0x21})});
    missing_identity.identity.caller_id = CallerId{};
    const auto invalid_identity = catalog_test::apply(state, missing_identity, scratch);
    require(invalid_identity.code == ApplyCode::invalid_command &&
                invalid_identity.contract_error == ContractError::invalid_identity,
            "missing command identity was not rejected with a typed reason");
    require(state.state_version == 0 && state.receipt_count == 0,
            "unidentifiable command was retained or mutated state");

    for (std::size_t index = 0; index < kMaxPolicies; ++index) {
        const auto result = catalog_test::apply(
            state,
            command(index + 1,
                    static_cast<std::byte>(0x70U + index),
                    PublishPolicy{policy(100 + index,
                                         1,
                                         static_cast<std::byte>(0x20U + index),
                                         100 + index,
                                         200 + index)}),
            scratch);
        require(result.code == ApplyCode::applied, "policy catalog filled too early");
    }

    const auto full = catalog_test::apply(
        state,
        command(kMaxPolicies + 1,
                std::byte{0x6F},
                PublishPolicy{policy(999, 1, std::byte{0x7F}, 999, 999)}),
        scratch);
    require(full.code == ApplyCode::policy_capacity_exceeded,
            "full policy catalog did not fail closed");
    require(state.policy_count == kMaxPolicies,
            "capacity rejection wrote beyond the policy catalog");
    const auto version_after_full = state.state_version;
    require(catalog_test::apply(state,
                  command(kMaxPolicies + 1,
                          std::byte{0x6F},
                          PublishPolicy{policy(999, 1, std::byte{0x7F}, 999, 999)}),
                  scratch) == full &&
                state.state_version == version_after_full,
            "capacity rejection was not retained for exact retry");
}

void test_receipt_and_version_exhaustion_precede_business_mutation() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;

    for (std::size_t index = 0; index < kMaxRequestReceipts; ++index) {
        const auto rejected = catalog_test::apply(
            state,
            command(index + 1,
                    static_cast<std::byte>((index % 250U) + 1U),
                    ValidatePolicy{PolicyId{10'000 + index}, PolicyVersion{1}}),
            scratch);
        require(rejected.code == ApplyCode::policy_not_found,
                "receipt table setup did not retain the expected rejection");
    }
    require(state.receipt_count == kMaxRequestReceipts && state.policy_count == 0,
            "receipt table setup mutated policy state");

    // Receipts are reclaimable now (architecture 8.6), and rejections are
    // unpinned, so the table above would simply be swept and capacity would
    // never be exceeded. Overwrite it with PINNED receipts - applied fill
    // results, which are the deduplication for a venue execution identity and
    // are never forgotten - so this stays a test of the capacity path rather
    // than of the sweep.
    for (std::size_t index = 0; index < kMaxRequestReceipts; ++index) {
        ApplyResult pinned;
        pinned.code = ApplyCode::applied;
        pinned.detail = ApplyResultDetail::fill(VenueId{9},
                                                ExecutionId{30'000 + index},
                                                QuantityLots::from_atoms(1));
        pinned.detail_kind = ApplyResultDetailKind::fill;
        pinned.metadata_words = {2, 30'000 + index};
        state.receipts[index] = {
            {CallerId{60}, RequestSequence{index + 1}, digest(std::byte{0x5A})},
            pinned};
    }

    const auto full = catalog_test::apply(
        state,
        command(kMaxRequestReceipts + 1,
                std::byte{0x7F},
                PublishPolicy{policy(1, 1, std::byte{0x21})}),
        scratch);
    require(full.code == ApplyCode::receipt_capacity_exceeded && state.policy_count == 0,
            "new policy mutated state without space for its idempotency receipt");

    TradingState version_exhausted = catalog_test::state();
    version_exhausted.state_version = std::numeric_limits<std::uint64_t>::max();
    const auto exhausted = catalog_test::apply(
        version_exhausted,
        command(1, std::byte{0x71}, PublishPolicy{policy(1, 1, std::byte{0x21})}),
        scratch);
    require(exhausted.code == ApplyCode::state_version_exhausted &&
                version_exhausted.policy_count == 0 &&
                version_exhausted.receipt_count == 0,
            "version overflow partially mutated authoritative state");
}

}  // namespace

int main() {
    try {
        test_publish_is_bounded_and_idempotent();
        test_policy_identity_is_immutable_and_versioned();
        test_validate_activate_supersede_lifecycle();
        test_invalid_input_and_capacity_fail_closed();
        test_receipt_and_version_exhaustion_precede_business_mutation();
        std::cout << "all V2 kernel policy tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 kernel policy test failed: " << error.what() << '\n';
        return 1;
    }
}
