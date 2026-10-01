#include "velocita/v2/shadow_program.hpp"

#include "velocita/v2/artifact_digest.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/kernel_codec.hpp"

#include <limits>
#include <stdexcept>
#include <string_view>

namespace velocita::v2 {
namespace {

void append(std::vector<std::byte>& out, std::string_view text) {
    for (const char c : text) out.push_back(static_cast<std::byte>(c));
}
void append(std::vector<std::byte>& out, std::uint64_t value) {
    for (int b = 0; b < 8; ++b) out.push_back(static_cast<std::byte>((value >> (8 * b)) & 0xff));
}
void append(std::vector<std::byte>& out, const Digest256& digest) {
    out.insert(out.end(), digest.bytes.begin(), digest.bytes.end());
}

// The policy artifact's own digest: every field that defines the policy, in a
// fixed order, under a domain tag. The C++ side has no canonical policy digest
// of its own; this one is deterministic and binds the rules exactly.
Digest256 policy_digest(const PolicyArtifact& policy) {
    std::vector<std::byte> bytes;
    append(bytes, "velocita.v2.shadow-policy.1");
    append(bytes, policy.policy_id.value());
    append(bytes, policy.policy_version.value());
    append(bytes, policy.feature_schema_id.value());
    append(bytes, policy.instrument_id.value());
    append(bytes, policy.account_id.value());
    append(bytes, policy.model_digest);
    append(bytes, static_cast<std::uint64_t>(policy.valid_from_event_time_ns));
    append(bytes, static_cast<std::uint64_t>(policy.valid_until_event_time_ns));
    append(bytes, policy.risk_policy_version.value());
    append(bytes, policy.rule_count);
    for (std::size_t i = 0; i < policy.rule_count; ++i) {
        const auto& rule = policy.rules[i];
        append(bytes, rule.feature_index);
        append(bytes, static_cast<std::uint64_t>(rule.predicate));
        append(bytes, static_cast<std::uint64_t>(rule.threshold.atoms()));
        append(bytes, static_cast<std::uint64_t>(rule.directive));
    }
    return sha256_digest(bytes);
}

// A command's payload digest binds every byte of the command except the digest
// field itself: encode with a constant placeholder there, then hash.
Command identified(CallerId caller, std::uint64_t sequence, CommandBody body) {
    Command command{{caller, RequestSequence{sequence}, {}}, std::move(body)};
    command.identity.payload_digest.bytes.fill(std::byte{0x01});
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(command, bytes);
    if (encoded.error != KernelCodecError::none) throw std::invalid_argument("shadow command does not encode");
    std::vector<std::byte> tagged;
    append(tagged, "velocita.v2.shadow-command.1");
    tagged.insert(tagged.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written));
    command.identity.payload_digest = sha256_digest(tagged);
    return command;
}

PolicyArtifact policy_artifact(const ShadowProgram& p) {
    if (p.rules.empty() || p.rules.size() > kMaxPolicyRules)
        throw std::invalid_argument("a shadow program needs 1..kMaxPolicyRules rules");
    PolicyArtifact policy;
    policy.policy_id = p.policy_id;
    policy.policy_version = p.policy_version;
    policy.schema_version = kPolicyArtifactSchemaVersion;
    policy.feature_schema_id = p.feature_schema_id;
    policy.instrument_id = p.row.instrument.instrument_id;
    policy.account_id = p.row.account_id;
    policy.model_digest = p.model_digest;
    policy.valid_from_event_time_ns = p.valid_from_ns;
    policy.valid_until_event_time_ns = p.valid_until_ns;
    policy.risk_policy_version = p.risk_policy_version;
    policy.rule_count = static_cast<std::uint16_t>(p.rules.size());
    for (std::size_t i = 0; i < p.rules.size(); ++i) {
        const auto& rule = p.rules[i];
        policy.rules[i] = PolicyRule{rule.feature, rule.op, FeatureMicros::from_atoms(rule.threshold),
                                     rule.action == ShadowAction::hold ? Directive::no_action : Directive::place_limit};
    }
    policy.artifact_digest = policy_digest(policy);
    return policy;
}

FeatureSchemaArtifact feature_schema(const ShadowProgram& p) {
    if (p.generic_features + 1u > kMaxDecisionFeatures) throw std::invalid_argument("too many features");
    FeatureSchemaArtifact schema;
    schema.schema_version = kFeatureSchemaArtifactSchemaVersion;
    schema.feature_schema_id = p.feature_schema_id;
    schema.feature_count = static_cast<std::uint16_t>(p.generic_features + 1);
    for (std::uint16_t i = 0; i < p.generic_features; ++i) schema.features[i] = {FeatureEconomicKind::generic, 0};
    schema.features[p.generic_features] = {FeatureEconomicKind::price, 1};  // the price feature carries ticks
    schema.artifact_digest = compute_artifact_digest(schema).digest;
    return schema;
}

ActionTemplate order_template(const ShadowProgram& p, Side side) {
    ActionTemplate t;
    t.schema_version = kActionTemplateSchemaVersion;
    t.directive = Directive::place_limit;
    t.side = side;
    t.time_in_force = p.time_in_force;
    t.post_only = p.post_only;
    t.order_slot = LogicalOrderSlot{1};  // one position: the latest proposal supersedes older ones
    t.instrument_spec_version = p.row.instrument.instrument_spec_version;
    t.venue_capabilities_version = p.row.capabilities.venue_capabilities_version;
    t.price_source = {EconomicValueSourceKind::feature, p.generic_features, RoundingMode::toward_zero, {}, 1, {}};
    t.quantity_source = {EconomicValueSourceKind::fixed, 0, RoundingMode::toward_zero, p.order_quantity, 0};
    t.safety_caps.max_quantity = p.order_quantity;
    t.safety_caps.max_notional = p.max_notional;
    // Price and reference are the same feature, so the deviation is zero; one tick
    // is the tightest cap the contract accepts.
    t.safety_caps.max_abs_price_deviation = PriceTicks::from_atoms(1);
    t.safety_caps.max_abs_tick_offset = PriceTicks::from_atoms(1);
    t.safety_caps.max_market_data_age_ns = p.max_market_data_age_ns;
    t.safety_caps.reference_price = t.price_source;
    return t;
}

ExecutablePolicyArtifact executable_policy(const ShadowProgram& p, const PolicyArtifact& policy,
                                           const FeatureSchemaArtifact& schema) {
    ExecutablePolicyArtifact e;
    e.schema_version = kExecutablePolicyArtifactSchemaVersion;
    e.policy_id = p.policy_id;
    e.policy_version = p.policy_version;
    e.policy_artifact_digest = policy.artifact_digest;
    e.feature_schema_id = p.feature_schema_id;
    e.feature_schema_digest = schema.artifact_digest;
    // Only templates some rule uses: the contract refuses unreferenced templates.
    std::uint16_t buy = 0, sell = 0;
    for (const auto& rule : p.rules) {
        if (rule.action == ShadowAction::enter_long && buy == 0) {
            e.templates[e.template_count++] = order_template(p, Side::buy);
            buy = e.template_count;
        } else if (rule.action == ShadowAction::exit && sell == 0) {
            e.templates[e.template_count++] = order_template(p, Side::sell);
            sell = e.template_count;
        }
    }
    e.rule_binding_count = static_cast<std::uint16_t>(p.rules.size());
    for (std::size_t i = 0; i < p.rules.size(); ++i) {
        const auto action = p.rules[i].action;
        const std::uint16_t slot = action == ShadowAction::enter_long ? buy : action == ShadowAction::exit ? sell : 0;
        e.rule_template_slots[i] = ActionTemplateSlot{static_cast<std::uint8_t>(slot)};  // <= kMaxExecutableActionTemplates
    }
    e.artifact_digest = compute_artifact_digest(e).digest;
    return e;
}

}  // namespace

ShadowSetup shadow_setup(const ShadowProgram& program, CallerId caller, std::uint64_t first_sequence,
                         KillEpoch kill_epoch, ExecutorGeneration executor_generation) {
    const auto policy = policy_artifact(program);
    const auto schema = feature_schema(program);
    const auto executable = executable_policy(program, policy, schema);
    ShadowSetup setup;
    setup.executable_policy_digest = executable.artifact_digest;
    auto sequence = first_sequence;
    setup.commands.push_back(identified(caller, sequence++, PublishPolicy{policy}));
    setup.commands.push_back(identified(caller, sequence++, ValidatePolicy{program.policy_id, program.policy_version}));
    setup.commands.push_back(identified(caller, sequence++,
        StageExecutablePolicy{schema, executable, program.row.instrument, program.row.capabilities}));
    setup.commands.push_back(identified(caller, sequence++,
        ActivateExecutablePolicy{program.policy_id, program.policy_version, executable.artifact_digest}));
    setup.commands.push_back(identified(caller, sequence++,
        SetStrategyMode{StrategyMode::normal, kill_epoch, executor_generation}));
    return setup;
}

std::array<Command, 2> shadow_tick(const ShadowProgram& program, const ShadowTick& tick, CallerId caller,
                                   std::uint64_t first_sequence, DecisionId decision, const FrameBinding& binding) {
    if (tick.features.size() != static_cast<std::size_t>(program.generic_features) + 1)
        throw std::invalid_argument("tick features do not match the program's feature schema");
    DecisionFrame frame;
    frame.policy_id = program.policy_id;
    frame.policy_version = program.policy_version;
    frame.feature_schema_id = program.feature_schema_id;
    frame.venue_id = program.row.instrument.venue_id;
    frame.venue_session_id = VenueSessionId{tick.generation.value()};
    frame.venue_sequence = VenueSequence{tick.sequence.value()};
    frame.source_id = tick.source;
    frame.source_generation = tick.generation;
    frame.source_sequence = tick.sequence;
    frame.event_time_ns = tick.event_time_ns;
    frame.receive_time_ns = tick.receive_time_ns;
    frame.instrument_id = program.row.instrument.instrument_id;
    frame.account_id = program.row.account_id;
    frame.feature_count = static_cast<std::uint16_t>(tick.features.size());
    for (std::size_t i = 0; i < tick.features.size(); ++i) frame.features[i] = FeatureMicros::from_atoms(tick.features[i]);
    frame.market_data_status = MarketDataStatus::ready;
    frame.market_data_age_ns = tick.market_data_age_ns;
    frame.position_version = binding.position_version;
    frame.risk_version = binding.risk_version;
    frame.raw_evidence_digest = tick.evidence_digest;
    return {identified(caller, first_sequence, RecordSourceEvent{tick.source, tick.generation, tick.sequence,
                                                                 tick.evidence_digest}),
            identified(caller, first_sequence + 1, EvaluateDecisionFrame{decision, binding.mode, frame})};
}

bool authorizable(const ApplyResult& evaluated) noexcept {
    return evaluated.code == ApplyCode::applied && evaluated.evaluation_mode == EvaluationMode::authoritative &&
           evaluated.decision_code == DecisionCode::action && evaluated.effective_directive == Directive::place_limit &&
           evaluated.action_resolution_code == ActionResolutionCode::proposed;
}

PaperRequest paper_request(const ShadowProgram& program, const ApplyResult& evaluated, bool exits_only) noexcept {
    if (!authorizable(evaluated)) return PaperRequest::none;
    if (!exits_only) return PaperRequest::authorize;
    const auto index = evaluated.matched_rule_index;
    const bool exit = index < program.rules.size() && program.rules[index].action == ShadowAction::exit;
    return exit ? PaperRequest::authorize : PaperRequest::withheld;
}

Command authorize_decision(CallerId caller, std::uint64_t sequence, DecisionId decision) {
    return identified(caller, sequence, AuthorizeDecision{decision});
}

RiskView resync_risk_view(const RiskView& current, const ApplyResult& authorization) noexcept {
    if (authorization.code != ApplyCode::stale_risk_state || authorization.detail_kind != kRiskStateDetailKind)
        return current;
    const auto& words = authorization.detail.words;
    if (words[0] == 0 || words[1] == 0) return current;
    return {RiskVersion{words[0]}, PositionVersion{words[1]},
            QuantityLots::from_atoms(static_cast<std::int64_t>(words[2]))};
}

std::optional<SourceGeneration> resync_source_generation(SourceId source, const ApplyResult& recorded) noexcept {
    // source_cursor_id() is zero unless the result carries the source_state report.
    if (recorded.code != ApplyCode::source_generation_gap || recorded.source_cursor_id() != source)
        return std::nullopt;
    const auto cursor = recorded.source_cursor_generation().value();
    if (cursor == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    return SourceGeneration{cursor + 1};
}

}  // namespace velocita::v2
