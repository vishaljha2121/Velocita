#include "velocita/v2/action_resolver.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/replay.hpp"
#include "velocita/v2/rule_evaluator.hpp"
#include "velocita/v2/state_snapshot.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace velocita::v2;

constexpr std::size_t kHistoryLength = 96;
constexpr std::size_t kSeedCount = 32;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string location(const std::uint64_t seed,
                     const std::size_t step,
                     const std::string& message) {
    return "seed=" + std::to_string(seed) + " step=" + std::to_string(step) +
           ": " + message;
}

Digest256 digest(const std::uint64_t value) {
    Digest256 result;
    auto mixed = value + 0x9e3779b97f4a7c15ULL;
    for (std::size_t index = 0; index < result.bytes.size(); ++index) {
        mixed ^= mixed >> 30;
        mixed *= 0xbf58476d1ce4e5b9ULL;
        mixed ^= mixed >> 27;
        mixed *= 0x94d049bb133111ebULL;
        mixed ^= mixed >> 31;
        result.bytes[index] = static_cast<std::byte>(mixed & 0xffU);
        mixed += index + 1;
    }
    return result;
}

class Generator final {
public:
    explicit Generator(const std::uint64_t seed) noexcept : state_(seed) {}

    std::uint64_t next() noexcept {
        state_ += 0x9e3779b97f4a7c15ULL;
        auto value = state_;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    std::uint64_t bounded(const std::uint64_t upper_exclusive) noexcept {
        return next() % upper_exclusive;
    }

private:
    std::uint64_t state_;
};

PolicyArtifact policy(const std::uint64_t id,
                      const std::uint64_t version,
                      const std::uint64_t variant) {
    PolicyArtifact value;
    value.policy_id = PolicyId{id};
    value.policy_version = PolicyVersion{version};
    value.schema_version = kPolicyArtifactSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{1 + variant % 3};
    value.instrument_id = InstrumentId{1 + variant % 2};
    value.account_id = AccountId{1 + (variant / 2) % 2};
    value.model_digest = digest(0x1000 + variant);
    value.artifact_digest = digest(0x2000 + id * 31 + version * 7 + variant);
    value.valid_from_event_time_ns = 100;
    value.valid_until_event_time_ns = 200;
    value.risk_policy_version = RiskPolicyVersion{1};
    value.rule_count = 1;
    value.rules[0] = PolicyRule{0,
                                PredicateOperator::greater_than,
                                FeatureMicros::from_atoms(
                                    static_cast<std::int64_t>(variant + 1)),
                                Directive::place_limit};
    return value;
}

DecisionFrame decision_frame(const std::uint64_t variant,
                             const Digest256 evidence) {
    DecisionFrame value;
    value.policy_id = PolicyId{1 + variant % 5};
    value.policy_version = PolicyVersion{1 + (variant / 5) % 4};
    value.feature_schema_id = FeatureSchemaId{1 + variant % 3};
    value.venue_id = VenueId{1};
    value.venue_session_id = VenueSessionId{1};
    value.venue_sequence = VenueSequence{1 + variant};
    value.source_id = SourceId{1 + variant % 4};
    value.source_generation = SourceGeneration{1};
    value.source_sequence = SourceSequence{1};
    value.event_time_ns = 100 + static_cast<std::int64_t>(variant % 101);
    value.receive_time_ns = value.event_time_ns + 1;
    value.instrument_id = InstrumentId{1 + variant % 2};
    value.account_id = AccountId{1 + (variant / 2) % 2};
    value.feature_count = 1;
    value.features[0] =
        FeatureMicros::from_atoms(static_cast<std::int64_t>(2 + variant % 8));
    value.market_data_status = static_cast<MarketDataStatus>(1 + variant % 5);
    value.market_data_age_ns = 1;
    value.position_version = PositionVersion{1};
    value.risk_version = RiskVersion{1};
    value.raw_evidence_digest = evidence;
    return value;
}

StageExecutablePolicy executable_bundle_command(const PolicyArtifact& base) {
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
    action.safety_caps = {QuantityLots::from_atoms(2),
                          MoneyMicros::from_atoms(1'000),
                          PriceTicks::from_atoms(2), PriceTicks::from_atoms(1),
                          100, action.price_source};
    executable.rule_binding_count = base.rule_count;
    executable.rule_template_slots[0] = ActionTemplateSlot{1};
    executable.artifact_digest = compute_artifact_digest(executable).digest;
    value.instrument = {kInstrumentExecutionProfileSchemaVersion, VenueId{1},
                        base.instrument_id, InstrumentSpecVersion{1},
                        MoneyMicros::from_atoms(10), PriceTicks::from_atoms(1),
                        PriceTicks::from_atoms(100), QuantityLots::from_atoms(1),
                        QuantityLots::from_atoms(10)};
    value.venue = {kVenueCapabilitiesSchemaVersion, VenueId{1},
                   VenueCapabilitiesVersion{1}, kVenueDirectivePlaceLimit,
                   kVenueTifGtc, 1};
    return value;
}

// These fixtures issue control commands, so their caller has to be one the edge
// would have authenticated - see `kMaxOperatorCallerId`. The number is otherwise
// arbitrary.
constexpr CallerId kFixtureOperator{7};

Command new_command(const std::uint64_t sequence,
                    const std::uint64_t marker,
                    CommandBody body) {
    return Command{CommandIdentity{kFixtureOperator, RequestSequence{sequence},
                                   digest(marker)},
                   std::move(body)};
}

struct Record {
    Command command{};
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    std::size_t size{};
};

Record record(Command command) {
    Record result;
    result.command = std::move(command);
    const auto encoded = encode_kernel_command(result.command, result.bytes);
    require(encoded.error == KernelCodecError::none,
            "generated command violated the codec contract");
    result.size = encoded.bytes_written;
    return result;
}

CommandBody random_body(Generator& rng) {
    const auto kind = rng.bounded(9);
    const auto id = 1 + rng.bounded(5);
    const auto version = 1 + rng.bounded(4);
    switch (kind) {
        case 0:
            return PublishPolicy{policy(id, version, rng.bounded(8))};
        case 1:
            return ValidatePolicy{PolicyId{id}, PolicyVersion{version}};
        case 2:
            return ActivatePolicy{PolicyId{id}, PolicyVersion{version}};
        case 3:
            return SupersedePolicy{PolicyId{id}, PolicyVersion{version}};
        case 4:
            return SetStrategyMode{
                static_cast<StrategyMode>(1 + rng.bounded(5)),
                KillEpoch{rng.bounded(5)},
                ExecutorGeneration{rng.bounded(5)}};
        case 5:
            return AdvanceKillEpoch{KillEpoch{rng.bounded(7)}};
        case 6:
            return SetExecutorGeneration{ExecutorGeneration{rng.bounded(7)}};
        case 7:
            return RecordSourceEvent{SourceId{1 + rng.bounded(4)},
                                     SourceGeneration{1 + rng.bounded(3)},
                                     SourceSequence{1 + rng.bounded(8)},
                                     digest(0x3000 + rng.next())};
        default: {
            const auto variant = rng.bounded(40);
            return EvaluateDecisionFrame{
                DecisionId{1 + rng.next()},
                static_cast<EvaluationMode>(1 + rng.bounded(2)),
                decision_frame(variant, digest(0x4000 + rng.next()))};
        }
    }
}

std::vector<Record> history(const std::uint64_t seed) {
    Generator rng(seed);
    std::vector<Record> records;
    records.reserve(kHistoryLength);

    records.push_back(record(new_command(1, seed + 1,
                                         PublishPolicy{policy(1, 1, 0)})));
    records.push_back(record(new_command(
        2, seed + 2, ValidatePolicy{PolicyId{1}, PolicyVersion{1}})));
    const auto initial_bundle = executable_bundle_command(policy(1, 1, 0));
    records.push_back(record(new_command(3, seed + 3, initial_bundle)));
    records.push_back(record(new_command(
        4, seed + 4,
        ActivateExecutablePolicy{PolicyId{1}, PolicyVersion{1},
                                 initial_bundle.executable_policy.artifact_digest})));
    records.push_back(record(new_command(
        5, seed + 5, SetExecutorGeneration{ExecutorGeneration{1}})));
    records.push_back(record(
        new_command(6, seed + 6, AdvanceKillEpoch{KillEpoch{1}})));
    records.push_back(record(new_command(
        7, seed + 7,
        RecordSourceEvent{SourceId{1}, SourceGeneration{1}, SourceSequence{1},
                          digest(seed + 0x100)})));
    records.push_back(record(new_command(
        8, seed + 8,
        RecordSourceEvent{SourceId{1}, SourceGeneration{1}, SourceSequence{2},
                          digest(seed + 0x101)})));
    records.push_back(record(new_command(
        9, seed + 9,
        SetStrategyMode{StrategyMode::normal, KillEpoch{1},
                        ExecutorGeneration{1}})));

    const auto append = [&](CommandBody body) {
        const auto sequence = records.size() + 1;
        records.push_back(record(new_command(sequence, seed + sequence,
                                             std::move(body))));
    };
    auto admitted_frame = decision_frame(0, digest(seed + 0x100));
    append(EvaluateDecisionFrame{DecisionId{seed + 1},
                                 EvaluationMode::authoritative,
                                 admitted_frame});
    auto invalid_frame = admitted_frame;
    invalid_frame.account_id = AccountId{};
    append(EvaluateDecisionFrame{DecisionId{seed + 2},
                                 EvaluationMode::authoritative,
                                 invalid_frame});
    append(EvaluateDecisionFrame{DecisionId{seed + 1},
                                 EvaluationMode::authoritative,
                                 admitted_frame});
    append(RecordSourceEvent{SourceId{1}, SourceGeneration{1}, SourceSequence{1},
                             digest(seed + 0xdead)});
    append(RecordSourceEvent{SourceId{1}, SourceGeneration{1}, SourceSequence{4},
                             digest(seed + 0x104)});
    append(RecordSourceEvent{SourceId{1}, SourceGeneration{3}, SourceSequence{1},
                             digest(seed + 0x301)});
    append(RecordSourceEvent{SourceId{1}, SourceGeneration{2}, SourceSequence{1},
                             digest(seed + 0x201)});
    append(RecordSourceEvent{SourceId{1}, SourceGeneration{1}, SourceSequence{3},
                             digest(seed + 0x103)});
    append(PublishPolicy{policy(1, 1, 1)});
    append(PublishPolicy{policy(2, 2, 2)});
    append(PublishPolicy{policy(2, 1, 2)});
    append(PublishPolicy{policy(3, 1, 3)});
    append(ActivatePolicy{PolicyId{3}, PolicyVersion{1}});
    append(ValidatePolicy{PolicyId{3}, PolicyVersion{1}});
    append(PublishPolicy{policy(4, 1, 0)});
    append(ValidatePolicy{PolicyId{4}, PolicyVersion{1}});
    append(ActivatePolicy{PolicyId{4}, PolicyVersion{1}});
    append(ValidatePolicy{PolicyId{5}, PolicyVersion{1}});
    append(SetStrategyMode{StrategyMode::cancel_only, KillEpoch{0},
                           ExecutorGeneration{0}});
    append(AdvanceKillEpoch{KillEpoch{1}});
    append(SetExecutorGeneration{ExecutorGeneration{1}});
    records.push_back(records.front());
    auto identity_conflict = records.front().command;
    identity_conflict.identity.payload_digest = digest(seed + 0xbeef);
    records.push_back(record(std::move(identity_conflict)));

    for (std::size_t index = records.size(); index < kHistoryLength; ++index) {
        const auto choice = rng.bounded(10);
        if (choice == 0 && !records.empty()) {
            records.push_back(records[rng.bounded(records.size())]);
            continue;
        }
        if (choice == 1 && !records.empty()) {
            auto conflict = records[rng.bounded(records.size())].command;
            conflict.identity.payload_digest = digest(seed + 0x9000 + index);
            records.push_back(record(std::move(conflict)));
            continue;
        }
        records.push_back(record(new_command(index + 1,
                                             seed + 0x10000 + index,
                                             random_body(rng))));
    }
    return records;
}

struct ModelState {
    std::uint64_t state_version{};
    StrategyMode strategy_mode{StrategyMode::halted};
    KillEpoch kill_epoch{};
    ExecutorGeneration executor_generation{};
    std::vector<PolicyRecord> policies;
    ExecutablePolicyBundle active_executable_policy{};
    ExecutablePolicyBundle staged_executable_policy{};
    std::vector<SourceCursor> sources;
    std::vector<SourceEventReceipt> source_events;
    std::vector<CallerCursor> caller_cursors;
    std::vector<RequestReceipt> receipts;
    std::vector<DecisionRecordHot> decision_hot;
    std::vector<DecisionRecordCold> decision_cold;
};

ApplyResult model_result(const ModelState& state,
                         const ApplyCode code,
                         const std::uint64_t version,
                         const PolicyId policy_id = {},
                         const PolicyVersion policy_version = {},
                         const PolicyLifecycle lifecycle = PolicyLifecycle::none,
                         const ContractError error = ContractError::none,
                         const SourceId source_id = {},
                         const SourceGeneration generation = {},
                         const SourceSequence sequence = {}) {
    ApplyResult value;
    value.code = code;
    value.contract_error = error;
    value.metadata_words = {policy_id.value(), policy_version.value()};
    value.policy_lifecycle = lifecycle;
    value.state_version = version;
    value.strategy_mode = state.strategy_mode;
    value.kill_epoch = state.kill_epoch;
    value.executor_generation = state.executor_generation;
    if (source_id.value() != 0 || generation.value() != 0 ||
        sequence.value() != 0) {
        value.detail = ApplyResultDetail::source(source_id, generation, sequence);
        value.detail_kind = ApplyResultDetailKind::source;
    }
    return value;
}

PolicyRecord* model_policy(ModelState& state,
                          const PolicyId id,
                          const PolicyVersion version) {
    const auto found = std::find_if(
        state.policies.begin(), state.policies.end(), [&](const auto& item) {
            return item.artifact.policy_id == id &&
                   item.artifact.policy_version == version;
        });
    return found == state.policies.end() ? nullptr : &*found;
}

SourceCursor* model_source(ModelState& state, const SourceId id) {
    const auto found = std::find_if(
        state.sources.begin(), state.sources.end(),
        [&](const auto& item) { return item.source_id == id; });
    return found == state.sources.end() ? nullptr : &*found;
}

ApplyResult model_publish(ModelState& state,
                          const PublishPolicy& command,
                          const std::uint64_t version) {
    const auto error = validate(command.artifact);
    if (error != ContractError::none) {
        return model_result(state, ApplyCode::invalid_policy_contract, version,
                            command.artifact.policy_id,
                            command.artifact.policy_version,
                            PolicyLifecycle::none, error);
    }
    if (const auto* existing = model_policy(state, command.artifact.policy_id,
                                            command.artifact.policy_version)) {
        return model_result(
            state,
            existing->artifact.artifact_digest == command.artifact.artifact_digest
                ? ApplyCode::already_applied
                : ApplyCode::immutable_policy_conflict,
            version, command.artifact.policy_id, command.artifact.policy_version,
            existing->lifecycle);
    }
    const auto non_monotonic = std::any_of(
        state.policies.begin(), state.policies.end(), [&](const auto& item) {
            return item.artifact.policy_id == command.artifact.policy_id &&
                   item.artifact.policy_version.value() >=
                       command.artifact.policy_version.value();
        });
    if (non_monotonic) {
        return model_result(state, ApplyCode::non_monotonic_policy_version,
                            version, command.artifact.policy_id,
                            command.artifact.policy_version);
    }
    if (state.policies.size() == kMaxPolicies) {
        return model_result(state, ApplyCode::policy_capacity_exceeded, version,
                            command.artifact.policy_id,
                            command.artifact.policy_version);
    }
    state.policies.push_back({command.artifact, PolicyLifecycle::proposed});
    return model_result(state, ApplyCode::applied, version,
                        command.artifact.policy_id,
                        command.artifact.policy_version,
                        PolicyLifecycle::proposed);
}

template <typename Transition>
ApplyResult model_policy_transition(ModelState& state,
                                    const PolicyId id,
                                    const PolicyVersion policy_version,
                                    const std::uint64_t version,
                                    const PolicyLifecycle required,
                                    const PolicyLifecycle destination,
                                    Transition&& extra_valid) {
    if (id.value() == 0 || policy_version.value() == 0) {
        return model_result(state, ApplyCode::invalid_command, version, id,
                            policy_version, PolicyLifecycle::none,
                            ContractError::invalid_identity);
    }
    auto* item = model_policy(state, id, policy_version);
    if (item == nullptr) {
        return model_result(state, ApplyCode::policy_not_found, version, id,
                            policy_version);
    }
    if (item->lifecycle == destination) {
        return model_result(state, ApplyCode::already_applied, version, id,
                            policy_version, item->lifecycle);
    }
    if (item->lifecycle != required) {
        return model_result(state, ApplyCode::invalid_policy_transition, version,
                            id, policy_version, item->lifecycle);
    }
    if (!extra_valid(*item)) {
        return model_result(state, ApplyCode::active_policy_scope_conflict,
                            version, id, policy_version, item->lifecycle);
    }
    if (destination == PolicyLifecycle::superseded &&
        state.active_executable_policy.present &&
        state.active_executable_policy.executable_policy.policy_id == id &&
        state.active_executable_policy.executable_policy.policy_version ==
            policy_version) {
        state.active_executable_policy = {};
    }
    if (destination == PolicyLifecycle::superseded &&
        state.staged_executable_policy.present &&
        state.staged_executable_policy.executable_policy.policy_id == id &&
        state.staged_executable_policy.executable_policy.policy_version ==
            policy_version) {
        state.staged_executable_policy = {};
    }
    item->lifecycle = destination;
    return model_result(state, ApplyCode::applied, version, id, policy_version,
                        destination);
}

ApplyResult model_source_event(ModelState& state,
                               const RecordSourceEvent& command,
                               const std::uint64_t version) {
    const auto source_result = [&](const ApplyCode code) {
        return model_result(state, code, version, {}, {}, PolicyLifecycle::none,
                            ContractError::none, command.source_id,
                            command.source_generation, command.source_sequence);
    };
    const auto existing = std::find_if(
        state.source_events.begin(), state.source_events.end(),
        [&](const auto& item) {
            return item.source_id == command.source_id &&
                   item.generation == command.source_generation &&
                   item.sequence == command.source_sequence;
        });
    if (existing != state.source_events.end()) {
        return source_result(existing->event_digest == command.event_digest
                                 ? ApplyCode::already_applied
                                 : ApplyCode::source_event_conflict);
    }

    auto* cursor = model_source(state, command.source_id);
    if (cursor == nullptr) {
        if (command.source_generation != SourceGeneration{1}) {
            return source_result(ApplyCode::source_generation_gap);
        }
        if (command.source_sequence != SourceSequence{1}) {
            return source_result(ApplyCode::source_sequence_gap);
        }
    } else if (command.source_generation.value() < cursor->generation.value()) {
        return source_result(ApplyCode::source_generation_regression);
    } else if (command.source_generation == cursor->generation) {
        if (command.source_sequence.value() <= cursor->sequence.value()) {
            return source_result(ApplyCode::source_sequence_regression);
        }
        if (command.source_sequence.value() != cursor->sequence.value() + 1) {
            return source_result(ApplyCode::source_sequence_gap);
        }
    } else {
        if (command.source_generation.value() != cursor->generation.value() + 1) {
            return source_result(ApplyCode::source_generation_gap);
        }
        if (command.source_sequence != SourceSequence{1}) {
            return source_result(ApplyCode::source_sequence_gap);
        }
    }

    if (state.source_events.size() == kMaxSourceEventReceipts) {
        return source_result(ApplyCode::source_event_capacity_exceeded);
    }
    if (cursor == nullptr && state.sources.size() == kMaxSources) {
        return source_result(ApplyCode::source_capacity_exceeded);
    }
    if (cursor == nullptr) {
        state.sources.push_back(
            {command.source_id, command.source_generation, command.source_sequence});
    } else {
        cursor->generation = command.source_generation;
        cursor->sequence = command.source_sequence;
    }
    state.source_events.push_back({command.source_id, command.source_generation,
                                   command.source_sequence, command.event_digest});
    return source_result(ApplyCode::applied);
}

bool same_bundle_identity(const ExecutablePolicyBundle& bundle,
                          const PolicyId id,
                          const PolicyVersion version,
                          const Digest256& digest_value) {
    return bundle.present && bundle.executable_policy.policy_id == id &&
           bundle.executable_policy.policy_version == version &&
           bundle.executable_policy.artifact_digest == digest_value;
}

bool same_bundle_contents(const ExecutablePolicyBundle& bundle,
                          const StageExecutablePolicy& command) {
    return bundle.present && bundle.feature_schema == command.feature_schema &&
           bundle.executable_policy == command.executable_policy &&
           bundle.instrument == command.instrument && bundle.venue == command.venue;
}

ApplyResult model_stage_executable(ModelState& state,
                                   const StageExecutablePolicy& command,
                                   const std::uint64_t version) {
    const auto id = command.executable_policy.policy_id;
    const auto policy_version = command.executable_policy.policy_version;
    const auto schema_digest = verify_artifact_digest(command.feature_schema);
    if (schema_digest.error != ArtifactDigestError::none) {
        return model_result(
            state,
            schema_digest.error == ArtifactDigestError::digest_mismatch
                ? ApplyCode::artifact_digest_mismatch
                : ApplyCode::invalid_feature_schema_contract,
            version, id, policy_version, PolicyLifecycle::none,
            schema_digest.contract_error);
    }
    const auto executable_digest =
        verify_artifact_digest(command.executable_policy);
    if (executable_digest.error != ArtifactDigestError::none) {
        return model_result(
            state,
            executable_digest.error == ArtifactDigestError::digest_mismatch
                ? ApplyCode::artifact_digest_mismatch
                : ApplyCode::invalid_executable_policy_contract,
            version, id, policy_version, PolicyLifecycle::none,
            executable_digest.contract_error);
    }
    if (const auto error = validate(command.instrument);
        error != ContractError::none) {
        return model_result(state, ApplyCode::invalid_instrument_profile_contract,
                            version, id, policy_version, PolicyLifecycle::none,
                            error);
    }
    if (const auto error = validate(command.venue);
        error != ContractError::none) {
        return model_result(state, ApplyCode::invalid_venue_capabilities_contract,
                            version, id, policy_version, PolicyLifecycle::none,
                            error);
    }
    auto* selected = model_policy(state, id, policy_version);
    if (selected == nullptr) {
        return model_result(state, ApplyCode::policy_not_found, version, id,
                            policy_version);
    }
    if (selected->lifecycle != PolicyLifecycle::validated &&
        selected->lifecycle != PolicyLifecycle::active) {
        return model_result(state, ApplyCode::invalid_policy_transition, version,
                            id, policy_version, selected->lifecycle);
    }
    auto bound = bind_executable_policy(selected->artifact, command.feature_schema,
                                        command.executable_policy) ==
                 ExecutablePolicyBindingCode::bound;
    bound = bound && command.instrument.instrument_id ==
                         selected->artifact.instrument_id &&
            command.instrument.venue_id == command.venue.venue_id;
    for (std::size_t index = 0;
         bound && index < command.executable_policy.template_count; ++index) {
        const auto& action = command.executable_policy.templates[index];
        bound = action.instrument_spec_version ==
                    command.instrument.instrument_spec_version &&
                action.venue_capabilities_version ==
                    command.venue.venue_capabilities_version;
    }
    if (!bound) {
        return model_result(state, ApplyCode::executable_policy_binding_failed,
                            version, id, policy_version, selected->lifecycle);
    }
    if (same_bundle_contents(state.active_executable_policy, command) ||
        same_bundle_contents(state.staged_executable_policy, command)) {
        return model_result(state, ApplyCode::already_applied, version, id,
                            policy_version, selected->lifecycle);
    }
    if (same_bundle_identity(state.active_executable_policy, id, policy_version,
                             command.executable_policy.artifact_digest) ||
        same_bundle_identity(state.staged_executable_policy, id, policy_version,
                             command.executable_policy.artifact_digest) ||
        (state.staged_executable_policy.present &&
         state.staged_executable_policy.executable_policy.policy_id == id &&
         state.staged_executable_policy.executable_policy.policy_version ==
             policy_version)) {
        return model_result(state, ApplyCode::executable_policy_identity_conflict,
                            version, id, policy_version, selected->lifecycle);
    }
    if (state.active_executable_policy.present) {
        auto* active = model_policy(
            state, state.active_executable_policy.executable_policy.policy_id,
            state.active_executable_policy.executable_policy.policy_version);
        if (active != nullptr &&
            (active->artifact.account_id != selected->artifact.account_id ||
             active->artifact.instrument_id != selected->artifact.instrument_id)) {
            return model_result(state, ApplyCode::active_policy_scope_conflict,
                                version, id, policy_version, selected->lifecycle);
        }
        if (active != nullptr && active->artifact.policy_id == id &&
            active->artifact.policy_version.value() >= policy_version.value()) {
            return model_result(state, ApplyCode::non_monotonic_policy_version,
                                version, id, policy_version, selected->lifecycle);
        }
    }
    state.staged_executable_policy =
        {true, command.feature_schema, command.executable_policy,
         command.instrument, command.venue, version, 0};
    return model_result(state, ApplyCode::applied, version, id, policy_version,
                        selected->lifecycle);
}

ApplyResult model_activate_executable(ModelState& state,
                                      const ActivateExecutablePolicy& command,
                                      const std::uint64_t version) {
    if (command.policy_id.value() == 0 || command.policy_version.value() == 0 ||
        command.executable_policy_digest.is_zero()) {
        return model_result(
            state, ApplyCode::invalid_command, version, command.policy_id,
            command.policy_version, PolicyLifecycle::none,
            command.executable_policy_digest.is_zero()
                ? ContractError::missing_digest
                : ContractError::invalid_identity);
    }
    if (same_bundle_identity(state.active_executable_policy, command.policy_id,
                             command.policy_version,
                             command.executable_policy_digest)) {
        return model_result(state, ApplyCode::already_applied, version,
                            command.policy_id, command.policy_version,
                            PolicyLifecycle::active);
    }
    if (!same_bundle_identity(state.staged_executable_policy, command.policy_id,
                              command.policy_version,
                              command.executable_policy_digest)) {
        return model_result(state, ApplyCode::executable_policy_not_staged,
                            version, command.policy_id, command.policy_version);
    }
    auto* selected = model_policy(state, command.policy_id, command.policy_version);
    if (selected == nullptr) {
        return model_result(state, ApplyCode::policy_not_found, version,
                            command.policy_id, command.policy_version);
    }
    if (selected->lifecycle != PolicyLifecycle::validated &&
        selected->lifecycle != PolicyLifecycle::active) {
        return model_result(state, ApplyCode::invalid_policy_transition, version,
                            command.policy_id, command.policy_version,
                            selected->lifecycle);
    }
    if (state.active_executable_policy.present) {
        auto* old = model_policy(
            state, state.active_executable_policy.executable_policy.policy_id,
            state.active_executable_policy.executable_policy.policy_version);
        if (old != nullptr && old != selected) {
            old->lifecycle = PolicyLifecycle::superseded;
        }
    }
    selected->lifecycle = PolicyLifecycle::active;
    state.active_executable_policy = state.staged_executable_policy;
    state.active_executable_policy.activated_state_version = version;
    state.staged_executable_policy = {};
    return model_result(state, ApplyCode::applied, version, command.policy_id,
                        command.policy_version, PolicyLifecycle::active);
}

ApplyResult model_decision_result(
    const ModelState& state,
    const ApplyCode apply_code,
    const std::uint64_t version,
    const EvaluateDecisionFrame& command,
    const DecisionCode decision_code,
    const ContractError contract_error,
    const Directive proposed,
    const Directive effective,
    const std::uint16_t matched_rule_index,
    const std::uint16_t rules_visited,
    const ActionResolutionCode resolution_code =
        ActionResolutionCode::none) {
    auto value = model_result(state, apply_code, version, command.frame.policy_id,
                              command.frame.policy_version,
                              PolicyLifecycle::none, contract_error,
                              command.frame.source_id,
                              command.frame.source_generation,
                              command.frame.source_sequence);
    value.decision_id = command.decision_id;
    value.decision_code = decision_code;
    value.proposed_directive = proposed;
    value.effective_directive = effective;
    value.evaluation_mode = command.evaluation_mode;
    value.matched_rule_index = matched_rule_index;
    value.rules_visited = rules_visited;
    value.action_resolution_code = resolution_code;
    return value;
}

ApplyResult model_evaluate_decision(ModelState& state,
                                    const CommandIdentity& identity,
                                    const EvaluateDecisionFrame& command,
                                    const std::uint64_t version) {
    constexpr auto no_rule = std::numeric_limits<std::uint16_t>::max();
    const auto existing = std::find_if(
        state.decision_hot.begin(), state.decision_hot.end(),
        [&](const auto& item) { return item.decision_id == command.decision_id; });
    if (existing != state.decision_hot.end()) {
        const auto index = static_cast<std::size_t>(
            std::distance(state.decision_hot.begin(), existing));
        if (state.decision_cold[index].command_payload_digest !=
            identity.payload_digest) {
            return model_decision_result(
                state, ApplyCode::decision_id_conflict, version, command,
                DecisionCode::none, ContractError::none, Directive::no_action,
                Directive::no_action, no_rule, 0);
        }
        return model_decision_result(
            state, ApplyCode::already_applied, version, command, existing->code,
            existing->contract_error, existing->proposed_directive,
            existing->effective_directive, existing->matched_rule_index,
            existing->rules_visited, existing->action_resolution_code);
    }
    if (state.decision_hot.size() == kMaxDecisionRecords) {
        return model_decision_result(
            state, ApplyCode::decision_capacity_exceeded, version, command,
            DecisionCode::none, ContractError::none, Directive::no_action,
            Directive::no_action, no_rule, 0);
    }

    DecisionCode code = DecisionCode::none;
    auto contract_error = validate(command.frame);
    Directive proposed = Directive::no_action;
    Directive effective = Directive::no_action;
    std::uint16_t matched = no_rule;
    std::uint16_t visited = 0;
    ActionResolutionCode resolution_code = ActionResolutionCode::none;
    EconomicActionProposal proposal{};
    const PolicyRecord* selected = nullptr;
    const ExecutablePolicyBundle* executable = nullptr;

    const auto found_policy = std::find_if(
        state.policies.begin(), state.policies.end(), [&](const auto& item) {
            return item.artifact.policy_id == command.frame.policy_id &&
                   item.artifact.policy_version == command.frame.policy_version;
        });
    if (contract_error != ContractError::none) {
        code = DecisionCode::invalid_frame;
    } else if (found_policy == state.policies.end()) {
        code = DecisionCode::policy_not_found;
    } else if ((selected = &*found_policy)->lifecycle != PolicyLifecycle::active) {
        code = DecisionCode::policy_not_active;
    } else if (!state.active_executable_policy.present ||
               state.active_executable_policy.executable_policy.policy_id !=
                   command.frame.policy_id ||
               state.active_executable_policy.executable_policy.policy_version !=
                   command.frame.policy_version ||
               state.active_executable_policy.executable_policy
                       .policy_artifact_digest !=
                   selected->artifact.artifact_digest) {
        code = DecisionCode::executable_policy_not_active;
    } else if (selected->artifact.account_id != command.frame.account_id) {
        code = DecisionCode::account_scope_mismatch;
    } else if (selected->artifact.instrument_id != command.frame.instrument_id) {
        code = DecisionCode::instrument_scope_mismatch;
    } else if (selected->artifact.feature_schema_id !=
               command.frame.feature_schema_id) {
        code = DecisionCode::feature_schema_mismatch;
    } else if (state.strategy_mode == StrategyMode::halted) {
        code = DecisionCode::control_halted;
    } else {
        const auto source = std::find_if(
            state.source_events.begin(), state.source_events.end(),
            [&](const auto& item) {
                return item.source_id == command.frame.source_id &&
                       item.generation == command.frame.source_generation &&
                       item.sequence == command.frame.source_sequence;
            });
        if (source == state.source_events.end()) {
            code = DecisionCode::source_not_admitted;
        } else if (source->event_digest != command.frame.raw_evidence_digest) {
            code = DecisionCode::source_evidence_mismatch;
        } else if (command.frame.event_time_ns <
                   selected->artifact.valid_from_event_time_ns) {
            code = DecisionCode::policy_not_yet_valid;
        } else if (command.frame.event_time_ns >
                   selected->artifact.valid_until_event_time_ns) {
            code = DecisionCode::policy_expired;
        } else if (command.frame.market_data_status != MarketDataStatus::ready) {
            code = DecisionCode::market_data_not_ready;
        } else {
            executable = &state.active_executable_policy;
            const auto evaluation = evaluate_rules(selected->artifact, command.frame);
            contract_error = evaluation.contract_error;
            proposed = evaluation.directive;
            matched = evaluation.matched_rule_index;
            visited = evaluation.rules_visited;
            if (evaluation.code == RuleEvaluationCode::feature_unavailable) {
                code = DecisionCode::feature_unavailable;
            } else if (evaluation.code == RuleEvaluationCode::no_rule_matched) {
                code = DecisionCode::no_rule_matched;
            } else if (evaluation.code != RuleEvaluationCode::matched) {
                code = DecisionCode::invalid_frame;
            } else {
                const auto slot = executable->executable_policy
                                      .rule_template_slots[matched]
                                      .value();
                if (slot != 0) {
                    const auto resolution = resolve_action(
                        command.decision_id, selected->artifact, command.frame,
                        executable->executable_policy.templates[slot - 1],
                        executable->instrument, executable->venue);
                    resolution_code = resolution.code;
                    contract_error = resolution.contract_error;
                    proposal = resolution.proposal;
                    if (resolution.code != ActionResolutionCode::proposed) {
                        code = DecisionCode::action_resolution_rejected;
                    }
                }
                if (code == DecisionCode::none) {
                    if (command.evaluation_mode == EvaluationMode::shadow) {
                        code = DecisionCode::shadow_recorded;
                    } else if ((state.strategy_mode ==
                                    StrategyMode::no_new_risk ||
                                state.strategy_mode ==
                                    StrategyMode::cancel_only ||
                                state.strategy_mode ==
                                    StrategyMode::reduce_only) &&
                               proposed != Directive::cancel &&
                               proposed != Directive::no_action) {
                        code = DecisionCode::control_suppressed;
                    } else {
                        code = DecisionCode::action;
                        effective = proposed;
                    }
                }
            }
        }
    }

    const auto outcome = model_decision_result(
        state, ApplyCode::applied, version, command, code, contract_error,
        proposed, effective, matched, visited, resolution_code);
    state.decision_hot.push_back(DecisionRecordHot{
        command.decision_id, command.frame.policy_id, command.frame.policy_version,
        version, state.kill_epoch, state.executor_generation, matched, visited,
        code, contract_error, proposed, effective, command.evaluation_mode,
        state.strategy_mode, resolution_code});
    state.decision_cold.push_back(DecisionRecordCold{
        command.decision_id, identity.payload_digest, command.frame, proposal});
    return outcome;
}

ApplyResult model_new_command(ModelState& state,
                              const CommandIdentity& identity,
                              const CommandBody& body,
                              const std::uint64_t version) {
    return std::visit(
        [&](const auto& command) -> ApplyResult {
            using T = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<T, PublishPolicy>) {
                return model_publish(state, command, version);
            } else if constexpr (std::is_same_v<T, ValidatePolicy>) {
                return model_policy_transition(
                    state, command.policy_id, command.policy_version, version,
                    PolicyLifecycle::proposed, PolicyLifecycle::validated,
                    [](const auto&) { return true; });
            } else if constexpr (std::is_same_v<T, ActivatePolicy>) {
                return model_policy_transition(
                    state, command.policy_id, command.policy_version, version,
                    PolicyLifecycle::validated, PolicyLifecycle::active,
                    [&](const auto& selected) {
                        return std::none_of(
                            state.policies.begin(), state.policies.end(),
                            [&](const auto& candidate) {
                                return candidate.lifecycle == PolicyLifecycle::active &&
                                       candidate.artifact.account_id ==
                                           selected.artifact.account_id &&
                                       candidate.artifact.instrument_id ==
                                           selected.artifact.instrument_id;
                            });
                    });
            } else if constexpr (std::is_same_v<T, SupersedePolicy>) {
                return model_policy_transition(
                    state, command.policy_id, command.policy_version, version,
                    PolicyLifecycle::active, PolicyLifecycle::superseded,
                    [](const auto&) { return true; });
            } else if constexpr (std::is_same_v<T, SetStrategyMode>) {
                if (command.expected_kill_epoch != state.kill_epoch ||
                    command.expected_executor_generation !=
                        state.executor_generation) {
                    return model_result(state, ApplyCode::stale_control_generation,
                                        version);
                }
                if (command.mode == state.strategy_mode) {
                    return model_result(state, ApplyCode::already_applied, version);
                }
                state.strategy_mode = command.mode;
                return model_result(state, ApplyCode::applied, version);
            } else if constexpr (std::is_same_v<T, AdvanceKillEpoch>) {
                if (command.new_kill_epoch.value() <= state.kill_epoch.value()) {
                    return model_result(state, ApplyCode::non_monotonic_kill_epoch,
                                        version);
                }
                state.kill_epoch = command.new_kill_epoch;
                state.strategy_mode = StrategyMode::halted;
                return model_result(state, ApplyCode::applied, version);
            } else if constexpr (std::is_same_v<T, SetExecutorGeneration>) {
                if (command.new_generation.value() <=
                    state.executor_generation.value()) {
                    return model_result(
                        state, ApplyCode::non_monotonic_executor_generation, version);
                }
                state.executor_generation = command.new_generation;
                if (state.strategy_mode != StrategyMode::halted &&
                    state.strategy_mode != StrategyMode::cancel_only) {
                    state.strategy_mode = StrategyMode::no_new_risk;
                }
                return model_result(state, ApplyCode::applied, version);
            } else if constexpr (std::is_same_v<T, RecordSourceEvent>) {
                return model_source_event(state, command, version);
            } else if constexpr (std::is_same_v<T, EvaluateDecisionFrame>) {
                return model_evaluate_decision(state, identity, command, version);
            } else if constexpr (std::is_same_v<T, StageExecutablePolicy>) {
                return model_stage_executable(state, command, version);
            } else if constexpr (std::is_same_v<T, ActivateExecutablePolicy>) {
                return model_activate_executable(state, command, version);
            } else if constexpr (std::is_same_v<T, AuthorizeDecision> ||
                                 std::is_same_v<T, ObserveFill> ||
                                 std::is_same_v<T, RequestCancel> ||
                                 std::is_same_v<T, ObserveOrderEvent> ||
                                 std::is_same_v<T, RequestReplace> ||
                                 std::is_same_v<T, ConfigureRiskLimits> ||
                                 std::is_same_v<T, RequestMassCancel> ||
                                 std::is_same_v<T, AcknowledgeGatewayResults>) {
                // This generator covers Phase 1-3 only. A complete independent
                // Phase 4 model remains an acceptance requirement.
                return model_result(state, ApplyCode::invalid_command, version);
            }
        },
        body);
}

ApplyResult model_apply(ModelState& state, const Command& command) {
    const auto receipt = std::find_if(
        state.receipts.begin(), state.receipts.end(), [&](const auto& item) {
            return item.identity.caller_id == command.identity.caller_id &&
                   item.identity.request_sequence ==
                       command.identity.request_sequence;
        });
    if (receipt != state.receipts.end()) {
        if (receipt->identity.payload_digest == command.identity.payload_digest) {
            return receipt->result;
        }
        return model_result(state, ApplyCode::duplicate_conflict,
                            state.state_version);
    }
    require(state.receipts.size() < kMaxRequestReceipts,
            "generated history exceeded the model receipt bound");
    const auto caller = std::find_if(
        state.caller_cursors.begin(), state.caller_cursors.end(),
        [&](const auto& item) {
            return item.caller_id == command.identity.caller_id;
        });
    if (caller == state.caller_cursors.end()) {
        require(state.caller_cursors.size() < kMaxCallerCursors,
                "generated history exceeded the model caller bound");
        state.caller_cursors.push_back(
            CallerCursor{command.identity.caller_id, RequestSequence{},
                         RequestSequence{}});
    }
    const auto next_version = state.state_version + 1;
    const auto result =
        model_new_command(state, command.identity, command.body, next_version);
    state.state_version = next_version;
    state.receipts.push_back({command.identity, result});
    return result;
}

TradingState materialize(const ModelState& model) {
    TradingState result;
    result.state_version = model.state_version;
    result.strategy_mode = model.strategy_mode;
    result.kill_epoch = model.kill_epoch;
    result.executor_generation = model.executor_generation;
    result.policy_count = model.policies.size();
    std::copy(model.policies.begin(), model.policies.end(), result.policies.begin());
    result.active_executable_policy = model.active_executable_policy;
    result.staged_executable_policy = model.staged_executable_policy;
    result.source_count = model.sources.size();
    std::copy(model.sources.begin(), model.sources.end(), result.sources.begin());
    result.source_event_count = model.source_events.size();
    std::copy(model.source_events.begin(), model.source_events.end(),
              result.source_events.begin());
    result.caller_cursor_count = model.caller_cursors.size();
    std::copy(model.caller_cursors.begin(), model.caller_cursors.end(),
              result.caller_cursors.begin());
    result.receipt_count = model.receipts.size();
    std::copy(model.receipts.begin(), model.receipts.end(), result.receipts.begin());
    result.decision_count = model.decision_hot.size();
    std::copy(model.decision_hot.begin(), model.decision_hot.end(),
              result.decision_hot.begin());
    std::copy(model.decision_cold.begin(), model.decision_cold.end(),
              result.decision_cold.begin());
    return result;
}

struct ExpectedRun {
    std::array<std::array<std::byte, kKernelResultBytes>, kHistoryLength> results{};
    std::array<std::byte, kMaxStateSnapshotBytes> final_snapshot{};
    std::size_t final_snapshot_size{};
    Digest256 final_digest{};
};

struct Coverage {
    static constexpr std::size_t size =
        static_cast<std::size_t>(ApplyCode::executable_policy_identity_conflict) +
        1;
    std::array<std::size_t, size> outcomes{};
    static constexpr std::size_t decision_size =
        static_cast<std::size_t>(DecisionCode::action_resolution_rejected) + 1;
    std::array<std::size_t, decision_size> decisions{};

    void observe(const ApplyResult& result) noexcept {
        ++outcomes[static_cast<std::size_t>(result.code)];
        ++decisions[static_cast<std::size_t>(result.decision_code)];
    }

    [[nodiscard]] std::size_t count(const ApplyCode code) const noexcept {
        return outcomes[static_cast<std::size_t>(code)];
    }

    [[nodiscard]] std::size_t count(const DecisionCode code) const noexcept {
        return decisions[static_cast<std::size_t>(code)];
    }
};

ExpectedRun compare_production_to_model(const std::uint64_t seed,
                                        const std::vector<Record>& records,
                                        Coverage& coverage) {
    ExpectedRun expected;
    TradingState production;
    ModelState model;
    ApplyScratch apply_scratch;
    KernelCommandCodecScratch codec_scratch;

    for (std::size_t step = 0; step < records.size(); ++step) {
        const auto model_result = model_apply(model, records[step].command);
        coverage.observe(model_result);
        const auto replayed = apply_encoded_kernel_command(
            production, {records[step].bytes.data(), records[step].size},
            apply_scratch, codec_scratch, expected.results[step]);
        require(replayed.error == ReplayError::none,
                location(seed, step, "production replay rejected valid frame"));
        require(replayed.apply_result == model_result,
                location(seed, step, "production result diverged from model"));

        std::array<std::byte, kKernelResultBytes> model_bytes{};
        const auto encoded_model = encode_kernel_result(model_result, model_bytes);
        require(encoded_model.error == KernelCodecError::none &&
                    model_bytes == expected.results[step],
                location(seed, step, "canonical result bytes diverged"));

        const auto model_state = materialize(model);
        std::array<std::byte, kMaxStateSnapshotBytes> production_bytes{};
        std::array<std::byte, kMaxStateSnapshotBytes> model_state_bytes{};
        const auto production_snapshot =
            encode_state_snapshot(production, production_bytes);
        const auto model_snapshot =
            encode_state_snapshot(model_state, model_state_bytes);
        require(production_snapshot.error == StateSnapshotError::none &&
                    model_snapshot.error == StateSnapshotError::none &&
                    production_snapshot.bytes_written ==
                        model_snapshot.bytes_written &&
                    std::equal(production_bytes.begin(),
                               production_bytes.begin() +
                                   static_cast<std::ptrdiff_t>(
                                       production_snapshot.bytes_written),
                               model_state_bytes.begin()),
                location(seed, step, "canonical state diverged from model"));
    }

    const auto snapshot =
        encode_state_snapshot(production, expected.final_snapshot);
    require(snapshot.error == StateSnapshotError::none,
            "final production state did not encode");
    expected.final_snapshot_size = snapshot.bytes_written;
    expected.final_digest = snapshot.state_digest;
    return expected;
}

void compare_snapshot_suffixes(const std::uint64_t seed,
                               const std::vector<Record>& records,
                               const ExpectedRun& expected) {
    constexpr std::array<std::size_t, 6> cuts{0, 1, 8, 32, 64, 95};
    for (const auto cut : cuts) {
        TradingState prefix;
        ApplyScratch prefix_apply_scratch;
        KernelCommandCodecScratch prefix_codec_scratch;
        std::array<std::byte, kKernelResultBytes> ignored{};
        for (std::size_t step = 0; step < cut; ++step) {
            const auto applied = apply_encoded_kernel_command(
                prefix, {records[step].bytes.data(), records[step].size},
                prefix_apply_scratch, prefix_codec_scratch, ignored);
            require(applied.error == ReplayError::none,
                    location(seed, step, "prefix replay failed"));
        }

        std::array<std::byte, kMaxStateSnapshotBytes> snapshot_bytes{};
        const auto snapshot = encode_state_snapshot(prefix, snapshot_bytes);
        require(snapshot.error == StateSnapshotError::none,
                "generated prefix did not snapshot");
        TradingState restored;
        StateSnapshotScratch snapshot_scratch;
        const auto decoded = decode_state_snapshot(
            {snapshot_bytes.data(), snapshot.bytes_written}, restored,
            snapshot_scratch);
        require(decoded.error == StateSnapshotError::none,
                "generated prefix snapshot did not restore");

        ApplyScratch suffix_apply_scratch;
        KernelCommandCodecScratch suffix_codec_scratch;
        for (std::size_t step = cut; step < records.size(); ++step) {
            std::array<std::byte, kKernelResultBytes> actual{};
            const auto applied = apply_encoded_kernel_command(
                restored, {records[step].bytes.data(), records[step].size},
                suffix_apply_scratch, suffix_codec_scratch, actual);
            require(applied.error == ReplayError::none &&
                        actual == expected.results[step],
                    location(seed, step,
                             "snapshot suffix result diverged at cut=" +
                                 std::to_string(cut)));
        }

        std::array<std::byte, kMaxStateSnapshotBytes> final_bytes{};
        const auto final_snapshot = encode_state_snapshot(restored, final_bytes);
        require(final_snapshot.error == StateSnapshotError::none &&
                    final_snapshot.bytes_written == expected.final_snapshot_size &&
                    std::equal(final_bytes.begin(),
                               final_bytes.begin() + static_cast<std::ptrdiff_t>(
                                                         final_snapshot.bytes_written),
                               expected.final_snapshot.begin()),
                "seed=" + std::to_string(seed) +
                    ": snapshot suffix final state diverged at cut=" +
                    std::to_string(cut));
    }
}

void test_generated_model_and_snapshot_replay() {
    std::array<std::byte, kSeedCount * kDigestBytes> final_digests{};
    Coverage coverage;
    for (std::size_t seed_index = 0; seed_index < kSeedCount; ++seed_index) {
        const auto seed = 0x5eed0000ULL + seed_index;
        const auto records = history(seed);
        const auto expected = compare_production_to_model(seed, records, coverage);
        compare_snapshot_suffixes(seed, records, expected);
        std::copy(expected.final_digest.bytes.begin(),
                  expected.final_digest.bytes.end(),
                  final_digests.begin() +
                      static_cast<std::ptrdiff_t>(seed_index * kDigestBytes));
    }

    const auto corpus_digest = sha256_digest(final_digests);
    const auto actual = to_hex(corpus_digest);
    constexpr std::array required_outcomes{
        ApplyCode::applied,
        ApplyCode::already_applied,
        ApplyCode::duplicate_conflict,
        ApplyCode::policy_not_found,
        ApplyCode::immutable_policy_conflict,
        ApplyCode::non_monotonic_policy_version,
        ApplyCode::invalid_policy_transition,
        ApplyCode::active_policy_scope_conflict,
        ApplyCode::stale_control_generation,
        ApplyCode::non_monotonic_kill_epoch,
        ApplyCode::non_monotonic_executor_generation,
        ApplyCode::source_event_conflict,
        ApplyCode::source_generation_gap,
        ApplyCode::source_generation_regression,
        ApplyCode::source_sequence_gap,
        ApplyCode::decision_id_conflict,
    };
    for (const auto outcome : required_outcomes) {
        require(coverage.count(outcome) != 0,
                "generated corpus missed required ApplyCode=" +
                    std::to_string(static_cast<unsigned>(outcome)));
    }
    require(coverage.count(DecisionCode::action) != 0,
            "generated corpus never produced an authoritative action");
    require(coverage.count(DecisionCode::policy_not_found) != 0,
            "generated corpus never exercised a rejected decision");
    require(coverage.count(DecisionCode::invalid_frame) != 0,
            "generated corpus never exercised invalid-frame precedence");
    // RE-PINNED 2026-09-16, for a reviewed fixture change and nothing else.
    // Control actions are now accepted only from the reserved operator caller
    // band (`kMaxOperatorCallerId`), and this corpus issues them, so its caller
    // moved from 77 into the band. Every generated command's identity therefore
    // changed, which is the whole of the difference - the generator, the
    // distribution and the reducer are untouched. The coverage assertions above
    // still hold, which is what makes that claim checkable rather than asserted.
    require(actual ==
                "ed3732db95453b12dfdaf9d15a616a302be4c94bfc0a78d5fafe38b8b8aa5eca",
            "generated replay corpus changed without review: " + actual);

    std::cout << "generated histories=" << kSeedCount
              << " commands_per_history=" << kHistoryLength
              << " snapshot_cuts_per_history=6 corpus_digest=" << actual << '\n';
}

}  // namespace

int main() {
    try {
        test_generated_model_and_snapshot_replay();
        std::cout << "all V2 generated model/replay tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 generated model/replay test failed: " << error.what()
                  << '\n';
        return 1;
    }
}
