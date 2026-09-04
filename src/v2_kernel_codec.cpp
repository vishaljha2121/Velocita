#include "velocita/v2/kernel_codec.hpp"

#include "velocita/v2/digest.hpp"
#include "velocita/v2/payload.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <type_traits>

namespace velocita::v2 {
namespace {

inline constexpr std::array kCommandMagic{
    std::byte{'V'}, std::byte{'2'}, std::byte{'K'}, std::byte{'C'}};
inline constexpr std::array kResultMagic{
    std::byte{'V'}, std::byte{'2'}, std::byte{'K'}, std::byte{'R'}};

class Writer final {
public:
    explicit Writer(const std::span<std::byte> bytes) noexcept : bytes_(bytes) {}

    template <typename Value>
    void integral(const Value value) noexcept {
        static_assert(std::is_integral_v<Value>);
        using Unsigned = std::make_unsigned_t<Value>;
        const auto bits = static_cast<Unsigned>(value);
        for (std::size_t index = 0; index < sizeof(Value); ++index) {
            bytes_[offset_++] =
                static_cast<std::byte>((bits >> (index * 8U)) & 0xffU);
        }
    }

    void digest(const Digest256& value) noexcept {
        std::copy(value.bytes.begin(), value.bytes.end(),
                  bytes_.begin() + static_cast<std::ptrdiff_t>(offset_));
        offset_ += value.bytes.size();
    }

private:
    std::span<std::byte> bytes_;
    std::size_t offset_{};
};

class Reader final {
public:
    explicit Reader(const std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

    template <typename Value>
    [[nodiscard]] Value integral() noexcept {
        static_assert(std::is_integral_v<Value>);
        using Unsigned = std::make_unsigned_t<Value>;
        if (remaining() < sizeof(Value)) {
            valid_ = false;
            return {};
        }
        Unsigned bits{};
        for (std::size_t index = 0; index < sizeof(Value); ++index) {
            bits |= static_cast<Unsigned>(
                        std::to_integer<unsigned char>(bytes_[offset_ + index]))
                    << (index * 8U);
        }
        offset_ += sizeof(Value);
        if constexpr (std::is_signed_v<Value>) return std::bit_cast<Value>(bits);
        return bits;
    }

    [[nodiscard]] Digest256 digest() noexcept {
        Digest256 value;
        if (remaining() < value.bytes.size()) {
            valid_ = false;
            return value;
        }
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                    value.bytes.size(), value.bytes.begin());
        offset_ += value.bytes.size();
        return value;
    }

    [[nodiscard]] bool finished() const noexcept {
        return valid_ && offset_ == bytes_.size();
    }

private:
    [[nodiscard]] std::size_t remaining() const noexcept {
        return offset_ <= bytes_.size() ? bytes_.size() - offset_ : 0;
    }

    std::span<const std::byte> bytes_;
    std::size_t offset_{};
    bool valid_{true};
};

[[nodiscard]] constexpr bool known(const StrategyMode value) noexcept {
    switch (value) {
        case StrategyMode::normal:
        case StrategyMode::no_new_risk:
        case StrategyMode::cancel_only:
        case StrategyMode::reduce_only:
        case StrategyMode::halted:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool known(const PolicyLifecycle value) noexcept {
    switch (value) {
        case PolicyLifecycle::none:
        case PolicyLifecycle::proposed:
        case PolicyLifecycle::validated:
        case PolicyLifecycle::active:
        case PolicyLifecycle::superseded:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool known(const ApplyCode value) noexcept {
    switch (value) {
        case ApplyCode::applied:
        case ApplyCode::already_applied:
        case ApplyCode::invalid_command:
        case ApplyCode::invalid_policy_contract:
        case ApplyCode::duplicate_conflict:
        case ApplyCode::policy_not_found:
        case ApplyCode::immutable_policy_conflict:
        case ApplyCode::non_monotonic_policy_version:
        case ApplyCode::invalid_policy_transition:
        case ApplyCode::active_policy_scope_conflict:
        case ApplyCode::stale_control_generation:
        case ApplyCode::non_monotonic_kill_epoch:
        case ApplyCode::non_monotonic_executor_generation:
        case ApplyCode::source_event_conflict:
        case ApplyCode::source_generation_gap:
        case ApplyCode::source_generation_regression:
        case ApplyCode::source_sequence_gap:
        case ApplyCode::source_sequence_regression:
        case ApplyCode::source_capacity_exceeded:
        case ApplyCode::source_event_capacity_exceeded:
        case ApplyCode::decision_capacity_exceeded:
        case ApplyCode::policy_capacity_exceeded:
        case ApplyCode::receipt_capacity_exceeded:
        case ApplyCode::state_version_exhausted:
        case ApplyCode::decision_id_conflict:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool known(const ContractError value) noexcept {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(ContractError::non_positive_quantity);
}

[[nodiscard]] constexpr bool known(const EvaluationMode value) noexcept {
    return value == EvaluationMode::authoritative ||
           value == EvaluationMode::shadow;
}

[[nodiscard]] constexpr bool known(const DecisionCode value) noexcept {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(DecisionCode::shadow_recorded);
}

[[nodiscard]] constexpr bool known(const Directive value) noexcept {
    switch (value) {
        case Directive::no_action:
        case Directive::place_limit:
        case Directive::cancel:
        case Directive::replace:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool known(const KernelCommandKind value) noexcept {
    switch (value) {
        case KernelCommandKind::publish_policy:
        case KernelCommandKind::validate_policy:
        case KernelCommandKind::activate_policy:
        case KernelCommandKind::supersede_policy:
        case KernelCommandKind::set_strategy_mode:
        case KernelCommandKind::advance_kill_epoch:
        case KernelCommandKind::set_executor_generation:
        case KernelCommandKind::record_source_event:
        case KernelCommandKind::evaluate_decision_frame:
            return true;
    }
    return false;
}

[[nodiscard]] bool present(const CommandIdentity& identity) noexcept {
    return identity.caller_id.value() != 0 &&
           identity.request_sequence.value() != 0 &&
           !identity.payload_digest.is_zero();
}

[[nodiscard]] bool valid_command(const Command& command) noexcept {
    if (!present(command.identity)) return false;
    if (const auto* body = std::get_if<PublishPolicy>(&command.body)) {
        return validate(body->artifact) == ContractError::none;
    }
    if (const auto* body = std::get_if<ValidatePolicy>(&command.body)) {
        return body->policy_id.value() != 0 && body->policy_version.value() != 0;
    }
    if (const auto* body = std::get_if<ActivatePolicy>(&command.body)) {
        return body->policy_id.value() != 0 && body->policy_version.value() != 0;
    }
    if (const auto* body = std::get_if<SupersedePolicy>(&command.body)) {
        return body->policy_id.value() != 0 && body->policy_version.value() != 0;
    }
    if (const auto* body = std::get_if<SetStrategyMode>(&command.body)) {
        return known(body->mode);
    }
    if (std::holds_alternative<AdvanceKillEpoch>(command.body) ||
        std::holds_alternative<SetExecutorGeneration>(command.body)) {
        return true;
    }
    if (const auto* body = std::get_if<RecordSourceEvent>(&command.body)) {
        return body->source_id.value() != 0 &&
               body->source_generation.value() != 0 &&
               body->source_sequence.value() != 0 &&
               !body->event_digest.is_zero();
    }
    if (const auto* body = std::get_if<EvaluateDecisionFrame>(&command.body)) {
        return body->decision_id.value() != 0 && known(body->evaluation_mode) &&
               body->frame.feature_count <= kMaxDecisionFeatures;
    }
    return false;
}

[[nodiscard]] bool valid_result(const ApplyResult& result) noexcept {
    return known(result.code) && known(result.contract_error) &&
           known(result.policy_lifecycle) && known(result.strategy_mode) &&
           known(result.decision_code) && known(result.proposed_directive) &&
           known(result.effective_directive) && known(result.evaluation_mode) &&
           (result.evaluation_mode != EvaluationMode::shadow ||
            result.effective_directive == Directive::no_action) &&
           result.rules_visited <= kMaxPolicyRules &&
           (result.matched_rule_index == std::numeric_limits<std::uint16_t>::max() ||
            result.matched_rule_index < kMaxPolicyRules);
}

[[nodiscard]] KernelCommandKind kind_of(const CommandBody& body) noexcept {
    if (std::holds_alternative<PublishPolicy>(body)) {
        return KernelCommandKind::publish_policy;
    }
    if (std::holds_alternative<ValidatePolicy>(body)) {
        return KernelCommandKind::validate_policy;
    }
    if (std::holds_alternative<ActivatePolicy>(body)) {
        return KernelCommandKind::activate_policy;
    }
    if (std::holds_alternative<SupersedePolicy>(body)) {
        return KernelCommandKind::supersede_policy;
    }
    if (std::holds_alternative<SetStrategyMode>(body)) {
        return KernelCommandKind::set_strategy_mode;
    }
    if (std::holds_alternative<AdvanceKillEpoch>(body)) {
        return KernelCommandKind::advance_kill_epoch;
    }
    if (std::holds_alternative<SetExecutorGeneration>(body)) {
        return KernelCommandKind::set_executor_generation;
    }
    if (std::holds_alternative<RecordSourceEvent>(body)) {
        return KernelCommandKind::record_source_event;
    }
    return KernelCommandKind::evaluate_decision_frame;
}

[[nodiscard]] std::size_t body_size(const CommandBody& body) noexcept {
    if (const auto* value = std::get_if<PublishPolicy>(&body)) {
        return kPolicyPayloadPrefixBytes +
               value->artifact.rule_count * kPolicyRuleEncodedBytes;
    }
    if (std::holds_alternative<ValidatePolicy>(body) ||
        std::holds_alternative<ActivatePolicy>(body) ||
        std::holds_alternative<SupersedePolicy>(body)) {
        return 16;
    }
    if (std::holds_alternative<SetStrategyMode>(body)) return 17;
    if (std::holds_alternative<AdvanceKillEpoch>(body) ||
        std::holds_alternative<SetExecutorGeneration>(body)) {
        return 8;
    }
    if (std::holds_alternative<RecordSourceEvent>(body)) return 56;
    const auto& value = std::get<EvaluateDecisionFrame>(body);
    return sizeof(std::uint64_t) + sizeof(std::uint8_t) +
           kDecisionFramePayloadFixedBytes +
           value.frame.feature_count * sizeof(std::int64_t);
}

void write_decision_frame(Writer& writer, const DecisionFrame& value) noexcept {
    writer.integral(kDecisionFrameSchemaVersion);
    writer.integral(value.policy_id.value());
    writer.integral(value.policy_version.value());
    writer.integral(value.feature_schema_id.value());
    writer.integral(value.venue_id.value());
    writer.integral(value.venue_session_id.value());
    writer.integral(value.venue_sequence.value());
    writer.integral(value.source_id.value());
    writer.integral(value.source_generation.value());
    writer.integral(value.source_sequence.value());
    writer.integral(value.event_time_ns);
    writer.integral(value.receive_time_ns);
    writer.integral(value.instrument_id.value());
    writer.integral(value.account_id.value());
    writer.integral(value.feature_count);
    for (std::size_t index = 0; index < value.feature_count; ++index) {
        writer.integral(value.features[index].atoms());
    }
    writer.integral(static_cast<std::uint8_t>(value.market_data_status));
    writer.integral(value.market_data_age_ns);
    writer.integral(value.position_version.value());
    writer.integral(value.risk_version.value());
    writer.digest(value.raw_evidence_digest);
}

void write_policy(Writer& writer, const PolicyArtifact& value) noexcept {
    writer.integral(value.schema_version);
    writer.integral(value.policy_id.value());
    writer.integral(value.policy_version.value());
    writer.integral(value.feature_schema_id.value());
    writer.integral(value.instrument_id.value());
    writer.integral(value.account_id.value());
    writer.digest(value.model_digest);
    writer.digest(value.artifact_digest);
    writer.integral(value.valid_from_event_time_ns);
    writer.integral(value.valid_until_event_time_ns);
    writer.integral(value.risk_policy_version.value());
    writer.integral(value.rule_count);
    for (std::size_t index = 0; index < value.rule_count; ++index) {
        const auto& rule = value.rules[index];
        writer.integral(rule.feature_index);
        writer.integral(static_cast<std::uint8_t>(rule.predicate));
        writer.integral(static_cast<std::uint8_t>(rule.directive));
        writer.integral(rule.threshold.atoms());
    }
}

void write_body(Writer& writer, const CommandBody& body) noexcept {
    if (const auto* value = std::get_if<PublishPolicy>(&body)) {
        write_policy(writer, value->artifact);
    } else if (const auto* value = std::get_if<ValidatePolicy>(&body)) {
        writer.integral(value->policy_id.value());
        writer.integral(value->policy_version.value());
    } else if (const auto* value = std::get_if<ActivatePolicy>(&body)) {
        writer.integral(value->policy_id.value());
        writer.integral(value->policy_version.value());
    } else if (const auto* value = std::get_if<SupersedePolicy>(&body)) {
        writer.integral(value->policy_id.value());
        writer.integral(value->policy_version.value());
    } else if (const auto* value = std::get_if<SetStrategyMode>(&body)) {
        writer.integral(static_cast<std::uint8_t>(value->mode));
        writer.integral(value->expected_kill_epoch.value());
        writer.integral(value->expected_executor_generation.value());
    } else if (const auto* value = std::get_if<AdvanceKillEpoch>(&body)) {
        writer.integral(value->new_kill_epoch.value());
    } else if (const auto* value = std::get_if<SetExecutorGeneration>(&body)) {
        writer.integral(value->new_generation.value());
    } else if (const auto* value = std::get_if<RecordSourceEvent>(&body)) {
        writer.integral(value->source_id.value());
        writer.integral(value->source_generation.value());
        writer.integral(value->source_sequence.value());
        writer.digest(value->event_digest);
    } else if (const auto* value = std::get_if<EvaluateDecisionFrame>(&body)) {
        writer.integral(value->decision_id.value());
        writer.integral(static_cast<std::uint8_t>(value->evaluation_mode));
        write_decision_frame(writer, value->frame);
    }
}

[[nodiscard]] bool read_decision_frame(Reader& reader,
                                       DecisionFrame& value) noexcept {
    if (reader.integral<std::uint16_t>() != kDecisionFrameSchemaVersion) {
        return false;
    }
    value.policy_id = PolicyId{reader.integral<std::uint64_t>()};
    value.policy_version = PolicyVersion{reader.integral<std::uint64_t>()};
    value.feature_schema_id = FeatureSchemaId{reader.integral<std::uint64_t>()};
    value.venue_id = VenueId{reader.integral<std::uint64_t>()};
    value.venue_session_id = VenueSessionId{reader.integral<std::uint64_t>()};
    value.venue_sequence = VenueSequence{reader.integral<std::uint64_t>()};
    value.source_id = SourceId{reader.integral<std::uint64_t>()};
    value.source_generation = SourceGeneration{reader.integral<std::uint64_t>()};
    value.source_sequence = SourceSequence{reader.integral<std::uint64_t>()};
    value.event_time_ns = reader.integral<std::int64_t>();
    value.receive_time_ns = reader.integral<std::int64_t>();
    value.instrument_id = InstrumentId{reader.integral<std::uint64_t>()};
    value.account_id = AccountId{reader.integral<std::uint64_t>()};
    value.feature_count = reader.integral<std::uint16_t>();
    if (value.feature_count > kMaxDecisionFeatures) return false;
    for (std::size_t index = 0; index < value.feature_count; ++index) {
        value.features[index] =
            FeatureMicros::from_atoms(reader.integral<std::int64_t>());
    }
    value.market_data_status =
        static_cast<MarketDataStatus>(reader.integral<std::uint8_t>());
    value.market_data_age_ns = reader.integral<std::int64_t>();
    value.position_version = PositionVersion{reader.integral<std::uint64_t>()};
    value.risk_version = RiskVersion{reader.integral<std::uint64_t>()};
    value.raw_evidence_digest = reader.digest();
    return true;
}

[[nodiscard]] bool read_policy(Reader& reader, PolicyArtifact& value) noexcept {
    value.schema_version = reader.integral<std::uint16_t>();
    value.policy_id = PolicyId{reader.integral<std::uint64_t>()};
    value.policy_version = PolicyVersion{reader.integral<std::uint64_t>()};
    value.feature_schema_id = FeatureSchemaId{reader.integral<std::uint64_t>()};
    value.instrument_id = InstrumentId{reader.integral<std::uint64_t>()};
    value.account_id = AccountId{reader.integral<std::uint64_t>()};
    value.model_digest = reader.digest();
    value.artifact_digest = reader.digest();
    value.valid_from_event_time_ns = reader.integral<std::int64_t>();
    value.valid_until_event_time_ns = reader.integral<std::int64_t>();
    value.risk_policy_version = RiskPolicyVersion{reader.integral<std::uint64_t>()};
    value.rule_count = reader.integral<std::uint16_t>();
    if (value.rule_count > kMaxPolicyRules) return false;
    for (std::size_t index = 0; index < value.rule_count; ++index) {
        auto& rule = value.rules[index];
        rule.feature_index = reader.integral<std::uint16_t>();
        rule.predicate =
            static_cast<PredicateOperator>(reader.integral<std::uint8_t>());
        rule.directive = static_cast<Directive>(reader.integral<std::uint8_t>());
        rule.threshold = FeatureMicros::from_atoms(reader.integral<std::int64_t>());
    }
    return validate(value) == ContractError::none;
}

[[nodiscard]] bool read_body(Reader& reader,
                             const KernelCommandKind kind,
                             CommandBody& body) noexcept {
    switch (kind) {
        case KernelCommandKind::publish_policy: {
            PublishPolicy value;
            if (!read_policy(reader, value.artifact)) return false;
            body = value;
            return true;
        }
        case KernelCommandKind::validate_policy:
            body = ValidatePolicy{PolicyId{reader.integral<std::uint64_t>()},
                                  PolicyVersion{reader.integral<std::uint64_t>()}};
            return true;
        case KernelCommandKind::activate_policy:
            body = ActivatePolicy{PolicyId{reader.integral<std::uint64_t>()},
                                  PolicyVersion{reader.integral<std::uint64_t>()}};
            return true;
        case KernelCommandKind::supersede_policy:
            body = SupersedePolicy{PolicyId{reader.integral<std::uint64_t>()},
                                   PolicyVersion{reader.integral<std::uint64_t>()}};
            return true;
        case KernelCommandKind::set_strategy_mode:
            body = SetStrategyMode{
                static_cast<StrategyMode>(reader.integral<std::uint8_t>()),
                KillEpoch{reader.integral<std::uint64_t>()},
                ExecutorGeneration{reader.integral<std::uint64_t>()}};
            return true;
        case KernelCommandKind::advance_kill_epoch:
            body = AdvanceKillEpoch{KillEpoch{reader.integral<std::uint64_t>()}};
            return true;
        case KernelCommandKind::set_executor_generation:
            body = SetExecutorGeneration{
                ExecutorGeneration{reader.integral<std::uint64_t>()}};
            return true;
        case KernelCommandKind::record_source_event:
            body = RecordSourceEvent{
                SourceId{reader.integral<std::uint64_t>()},
                SourceGeneration{reader.integral<std::uint64_t>()},
                SourceSequence{reader.integral<std::uint64_t>()}, reader.digest()};
            return true;
        case KernelCommandKind::evaluate_decision_frame: {
            EvaluateDecisionFrame value;
            value.decision_id = DecisionId{reader.integral<std::uint64_t>()};
            value.evaluation_mode =
                static_cast<EvaluationMode>(reader.integral<std::uint8_t>());
            if (!read_decision_frame(reader, value.frame)) return false;
            body = value;
            return true;
        }
    }
    return false;
}

void write_result_payload(Writer& writer, const ApplyResult& value) noexcept {
    writer.integral(static_cast<std::uint8_t>(value.code));
    writer.integral(static_cast<std::uint8_t>(value.contract_error));
    writer.integral(value.policy_id.value());
    writer.integral(value.policy_version.value());
    writer.integral(static_cast<std::uint8_t>(value.policy_lifecycle));
    writer.integral(value.state_version);
    writer.integral(static_cast<std::uint8_t>(value.strategy_mode));
    writer.integral(value.kill_epoch.value());
    writer.integral(value.executor_generation.value());
    writer.integral(value.source_id.value());
    writer.integral(value.source_generation.value());
    writer.integral(value.source_sequence.value());
    writer.integral(value.decision_id.value());
    writer.integral(static_cast<std::uint8_t>(value.decision_code));
    writer.integral(static_cast<std::uint8_t>(value.proposed_directive));
    writer.integral(static_cast<std::uint8_t>(value.effective_directive));
    writer.integral(static_cast<std::uint8_t>(value.evaluation_mode));
    writer.integral(value.matched_rule_index);
    writer.integral(value.rules_visited);
}

[[nodiscard]] ApplyResult read_result_payload(Reader& reader) noexcept {
    ApplyResult value;
    value.code = static_cast<ApplyCode>(reader.integral<std::uint8_t>());
    value.contract_error =
        static_cast<ContractError>(reader.integral<std::uint8_t>());
    value.policy_id = PolicyId{reader.integral<std::uint64_t>()};
    value.policy_version = PolicyVersion{reader.integral<std::uint64_t>()};
    value.policy_lifecycle =
        static_cast<PolicyLifecycle>(reader.integral<std::uint8_t>());
    value.state_version = reader.integral<std::uint64_t>();
    value.strategy_mode =
        static_cast<StrategyMode>(reader.integral<std::uint8_t>());
    value.kill_epoch = KillEpoch{reader.integral<std::uint64_t>()};
    value.executor_generation =
        ExecutorGeneration{reader.integral<std::uint64_t>()};
    value.source_id = SourceId{reader.integral<std::uint64_t>()};
    value.source_generation =
        SourceGeneration{reader.integral<std::uint64_t>()};
    value.source_sequence = SourceSequence{reader.integral<std::uint64_t>()};
    value.decision_id = DecisionId{reader.integral<std::uint64_t>()};
    value.decision_code =
        static_cast<DecisionCode>(reader.integral<std::uint8_t>());
    value.proposed_directive =
        static_cast<Directive>(reader.integral<std::uint8_t>());
    value.effective_directive =
        static_cast<Directive>(reader.integral<std::uint8_t>());
    value.evaluation_mode =
        static_cast<EvaluationMode>(reader.integral<std::uint8_t>());
    value.matched_rule_index = reader.integral<std::uint16_t>();
    value.rules_visited = reader.integral<std::uint16_t>();
    return value;
}

[[nodiscard]] std::uint32_t read_u32(const std::span<const std::byte> bytes,
                                     const std::size_t offset) noexcept {
    std::uint32_t value{};
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<unsigned char>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint16_t read_u16(const std::span<const std::byte> bytes,
                                     const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        std::to_integer<unsigned char>(bytes[offset]) |
        (std::to_integer<unsigned char>(bytes[offset + 1]) << 8U));
}

[[nodiscard]] bool digest_matches(const std::span<const std::byte> bytes,
                                  const std::size_t digest_input_offset,
                                  Digest256& calculated) noexcept {
    const auto digest_offset = bytes.size() - kDigestBytes;
    calculated = sha256_digest(bytes.subspan(
        digest_input_offset, digest_offset - digest_input_offset));
    return std::equal(calculated.bytes.begin(), calculated.bytes.end(),
                      bytes.begin() + static_cast<std::ptrdiff_t>(digest_offset));
}

}  // namespace

EncodeKernelValueResult encode_kernel_command(
    const Command& command, const std::span<std::byte> output) noexcept {
    static_assert(kMaxKernelCommandBodyBytes == kMaxPolicyPayloadBytes);
    if (!valid_command(command)) return {KernelCodecError::invalid_value, 0, {}};

    const auto payload_bytes = kKernelCommandIdentityBytes + body_size(command.body);
    const auto frame_bytes = kKernelCommandPayloadOffset + payload_bytes + kDigestBytes;
    if (output.size() < frame_bytes) {
        return {KernelCodecError::output_too_small, 0, {}};
    }

    std::copy(kCommandMagic.begin(), kCommandMagic.end(), output.begin());
    Writer writer(output.subspan(kKernelCommandMagicBytes));
    writer.integral(static_cast<std::uint32_t>(payload_bytes));
    writer.integral(kKernelCodecSchemaVersion);
    writer.integral(static_cast<std::uint16_t>(kind_of(command.body)));
    writer.integral(command.identity.caller_id.value());
    writer.integral(command.identity.request_sequence.value());
    writer.digest(command.identity.payload_digest);
    write_body(writer, command.body);

    const auto digest_offset = kKernelCommandPayloadOffset + payload_bytes;
    const auto frame_digest = sha256_digest(output.subspan(
        kKernelCommandDigestInputOffset,
        digest_offset - kKernelCommandDigestInputOffset));
    std::copy(frame_digest.bytes.begin(), frame_digest.bytes.end(),
              output.begin() + static_cast<std::ptrdiff_t>(digest_offset));
    return {KernelCodecError::none, frame_bytes, frame_digest};
}

DecodeKernelValueResult decode_kernel_command(
    const std::span<const std::byte> bytes,
    Command& destination,
    KernelCommandCodecScratch& scratch) noexcept {
    constexpr auto minimum = kKernelCommandPayloadOffset +
                             kKernelCommandIdentityBytes + 8 + kDigestBytes;
    if (bytes.size() < minimum) return {KernelCodecError::truncated, {}};
    if (!std::equal(kCommandMagic.begin(), kCommandMagic.end(), bytes.begin())) {
        return {KernelCodecError::invalid_magic, {}};
    }
    const auto payload_bytes = read_u32(bytes, kKernelCommandMagicBytes);
    if (payload_bytes > kMaxKernelCommandPayloadBytes) {
        return {KernelCodecError::length_mismatch, {}};
    }
    const auto expected = kKernelCommandPayloadOffset +
                          static_cast<std::size_t>(payload_bytes) + kDigestBytes;
    if (bytes.size() < expected) return {KernelCodecError::truncated, {}};
    if (bytes.size() != expected) return {KernelCodecError::length_mismatch, {}};
    if (read_u16(bytes, kKernelCommandSchemaOffset) != kKernelCodecSchemaVersion) {
        return {KernelCodecError::unsupported_schema, {}};
    }
    const auto kind =
        static_cast<KernelCommandKind>(read_u16(bytes, kKernelCommandKindOffset));
    if (!known(kind)) return {KernelCodecError::unknown_kind, {}};

    Digest256 frame_digest;
    if (!digest_matches(bytes, kKernelCommandDigestInputOffset, frame_digest)) {
        return {KernelCodecError::digest_mismatch, {}};
    }

    const auto payload = bytes.subspan(kKernelCommandPayloadOffset, payload_bytes);
    Reader reader(payload);
    scratch.candidate = {};
    scratch.candidate.identity =
        CommandIdentity{CallerId{reader.integral<std::uint64_t>()},
                        RequestSequence{reader.integral<std::uint64_t>()},
                        reader.digest()};
    if (!read_body(reader, kind, scratch.candidate.body) || !reader.finished() ||
        !valid_command(scratch.candidate)) {
        return {KernelCodecError::invalid_value, {}};
    }
    destination = scratch.candidate;
    return {KernelCodecError::none, frame_digest};
}

EncodeKernelValueResult encode_kernel_result(
    const ApplyResult& result, const std::span<std::byte> output) noexcept {
    if (!valid_result(result)) return {KernelCodecError::invalid_value, 0, {}};
    if (output.size() < kKernelResultBytes) {
        return {KernelCodecError::output_too_small, 0, {}};
    }

    std::copy(kResultMagic.begin(), kResultMagic.end(), output.begin());
    Writer writer(output.subspan(kKernelCommandMagicBytes));
    writer.integral(static_cast<std::uint32_t>(kKernelResultPayloadBytes));
    writer.integral(kKernelCodecSchemaVersion);
    write_result_payload(writer, result);

    constexpr auto digest_offset =
        kKernelResultPayloadOffset + kKernelResultPayloadBytes;
    const auto frame_digest = sha256_digest(output.subspan(
        kKernelResultDigestInputOffset,
        digest_offset - kKernelResultDigestInputOffset));
    std::copy(frame_digest.bytes.begin(), frame_digest.bytes.end(),
              output.begin() + static_cast<std::ptrdiff_t>(digest_offset));
    return {KernelCodecError::none, kKernelResultBytes, frame_digest};
}

DecodeKernelValueResult decode_kernel_result(
    const std::span<const std::byte> bytes, ApplyResult& destination) noexcept {
    if (bytes.size() < kKernelResultBytes) return {KernelCodecError::truncated, {}};
    if (bytes.size() > kKernelResultBytes) {
        return {KernelCodecError::length_mismatch, {}};
    }
    if (!std::equal(kResultMagic.begin(), kResultMagic.end(), bytes.begin())) {
        return {KernelCodecError::invalid_magic, {}};
    }
    if (read_u32(bytes, kKernelCommandMagicBytes) != kKernelResultPayloadBytes) {
        return {KernelCodecError::length_mismatch, {}};
    }
    if (read_u16(bytes, kKernelResultSchemaOffset) != kKernelCodecSchemaVersion) {
        return {KernelCodecError::unsupported_schema, {}};
    }

    Digest256 frame_digest;
    if (!digest_matches(bytes, kKernelResultDigestInputOffset, frame_digest)) {
        return {KernelCodecError::digest_mismatch, {}};
    }
    Reader reader(bytes.subspan(kKernelResultPayloadOffset,
                                kKernelResultPayloadBytes));
    const auto candidate = read_result_payload(reader);
    if (!reader.finished() || !valid_result(candidate)) {
        return {KernelCodecError::invalid_value, {}};
    }
    destination = candidate;
    return {KernelCodecError::none, frame_digest};
}

}  // namespace velocita::v2
