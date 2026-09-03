#include "velocita/v2/state_snapshot.hpp"

#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <type_traits>

namespace velocita::v2 {
namespace {

inline constexpr std::array kSnapshotMagic{
    std::byte{'V'}, std::byte{'2'}, std::byte{'S'}, std::byte{'T'}};

class Writer final {
public:
    explicit Writer(const std::span<std::byte> output) noexcept : output_(output) {}

    template <typename Value>
    void integral(const Value value) noexcept {
        static_assert(std::is_integral_v<Value>);
        using Unsigned = std::make_unsigned_t<Value>;
        const auto bits = static_cast<Unsigned>(value);
        for (std::size_t index = 0; index < sizeof(Value); ++index) {
            output_[offset_++] =
                static_cast<std::byte>((bits >> (index * 8U)) & 0xffU);
        }
    }

    void digest(const Digest256& value) noexcept {
        std::copy(value.bytes.begin(), value.bytes.end(),
                  output_.begin() + static_cast<std::ptrdiff_t>(offset_));
        offset_ += value.bytes.size();
    }

private:
    std::span<std::byte> output_;
    std::size_t offset_{};
};

class Reader final {
public:
    explicit Reader(const std::span<const std::byte> input) noexcept : input_(input) {}

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
                        std::to_integer<unsigned char>(input_[offset_ + index]))
                    << (index * 8U);
        }
        offset_ += sizeof(Value);
        if constexpr (std::is_signed_v<Value>) {
            return std::bit_cast<Value>(bits);
        }
        return bits;
    }

    [[nodiscard]] Digest256 digest() noexcept {
        Digest256 value;
        if (remaining() < value.bytes.size()) {
            valid_ = false;
            return value;
        }
        std::copy_n(input_.begin() + static_cast<std::ptrdiff_t>(offset_),
                    value.bytes.size(), value.bytes.begin());
        offset_ += value.bytes.size();
        return value;
    }

    [[nodiscard]] bool finished() const noexcept {
        return valid_ && offset_ == input_.size();
    }

private:
    [[nodiscard]] std::size_t remaining() const noexcept {
        return offset_ <= input_.size() ? input_.size() - offset_ : 0;
    }

    std::span<const std::byte> input_;
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

[[nodiscard]] constexpr bool known(const PolicyLifecycle value,
                                   const bool allow_none) noexcept {
    switch (value) {
        case PolicyLifecycle::none:
            return allow_none;
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
        case ApplyCode::policy_capacity_exceeded:
        case ApplyCode::receipt_capacity_exceeded:
        case ApplyCode::state_version_exhausted:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool known(const ContractError value) noexcept {
    switch (value) {
        case ContractError::none:
        case ContractError::invalid_identity:
        case ContractError::unsupported_schema:
        case ContractError::missing_digest:
        case ContractError::invalid_time_range:
        case ContractError::capacity_exceeded:
        case ContractError::empty_rules:
        case ContractError::empty_features:
        case ContractError::invalid_feature_index:
        case ContractError::invalid_age:
        case ContractError::invalid_enum:
        case ContractError::non_positive_price:
        case ContractError::non_positive_quantity:
            return true;
    }
    return false;
}

[[nodiscard]] bool valid_state(const TradingState& state) noexcept {
    if (!known(state.strategy_mode) || state.policy_count > state.policies.size() ||
        state.source_count > state.sources.size() ||
        state.source_event_count > state.source_events.size() ||
        state.receipt_count > state.receipts.size()) {
        return false;
    }

    for (std::size_t index = 0; index < state.policy_count; ++index) {
        const auto& record = state.policies[index];
        if (validate(record.artifact) != ContractError::none ||
            !known(record.lifecycle, false)) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            const auto& previous = state.policies[prior];
            if (previous.artifact.policy_id == record.artifact.policy_id &&
                previous.artifact.policy_version == record.artifact.policy_version) {
                return false;
            }
            if (previous.lifecycle == PolicyLifecycle::active &&
                record.lifecycle == PolicyLifecycle::active &&
                previous.artifact.account_id == record.artifact.account_id &&
                previous.artifact.instrument_id == record.artifact.instrument_id) {
                return false;
            }
        }
    }

    for (std::size_t index = 0; index < state.source_count; ++index) {
        const auto& source = state.sources[index];
        if (source.source_id.value() == 0 || source.generation.value() == 0 ||
            source.sequence.value() == 0) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (state.sources[prior].source_id == source.source_id) return false;
        }
    }

    for (std::size_t index = 0; index < state.source_event_count; ++index) {
        const auto& event = state.source_events[index];
        if (event.source_id.value() == 0 || event.generation.value() == 0 ||
            event.sequence.value() == 0 || event.event_digest.is_zero()) {
            return false;
        }
        const auto* cursor = find_source_cursor(state, event.source_id);
        if (cursor == nullptr || event.generation.value() > cursor->generation.value() ||
            (event.generation == cursor->generation &&
             event.sequence.value() > cursor->sequence.value())) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            const auto& previous = state.source_events[prior];
            if (previous.source_id == event.source_id &&
                previous.generation == event.generation &&
                previous.sequence == event.sequence) {
                return false;
            }
        }
    }

    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        const auto& receipt = state.receipts[index];
        if (receipt.identity.caller_id.value() == 0 ||
            receipt.identity.request_sequence.value() == 0 ||
            receipt.identity.payload_digest.is_zero() ||
            !known(receipt.result.code) || !known(receipt.result.contract_error) ||
            !known(receipt.result.policy_lifecycle, true) ||
            !known(receipt.result.strategy_mode) ||
            receipt.result.state_version > state.state_version) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (state.receipts[prior].identity.caller_id == receipt.identity.caller_id &&
                state.receipts[prior].identity.request_sequence ==
                    receipt.identity.request_sequence) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] std::size_t payload_size(const TradingState& state) noexcept {
    auto size = kStateSnapshotEmptyPayloadBytes;
    for (std::size_t index = 0; index < state.policy_count; ++index) {
        size += kStateSnapshotPolicyFixedBytes +
                state.policies[index].artifact.rule_count *
                    kStateSnapshotPolicyRuleBytes;
    }
    size += state.source_count * kStateSnapshotSourceBytes;
    size += state.source_event_count * kStateSnapshotSourceEventBytes;
    size += state.receipt_count * kStateSnapshotReceiptBytes;
    return size;
}

void write_policy(Writer& writer, const PolicyRecord& record) noexcept {
    const auto& value = record.artifact;
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
    writer.integral(static_cast<std::uint8_t>(record.lifecycle));
}

void write_result(Writer& writer, const ApplyResult& value) noexcept {
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
}

void write_payload(Writer& writer, const TradingState& state) noexcept {
    writer.integral(state.state_version);
    writer.integral(static_cast<std::uint8_t>(state.strategy_mode));
    writer.integral(state.kill_epoch.value());
    writer.integral(state.executor_generation.value());

    writer.integral(static_cast<std::uint16_t>(state.policy_count));
    for (std::size_t index = 0; index < state.policy_count; ++index) {
        write_policy(writer, state.policies[index]);
    }

    writer.integral(static_cast<std::uint16_t>(state.source_count));
    for (std::size_t index = 0; index < state.source_count; ++index) {
        const auto& source = state.sources[index];
        writer.integral(source.source_id.value());
        writer.integral(source.generation.value());
        writer.integral(source.sequence.value());
    }

    writer.integral(static_cast<std::uint16_t>(state.source_event_count));
    for (std::size_t index = 0; index < state.source_event_count; ++index) {
        const auto& event = state.source_events[index];
        writer.integral(event.source_id.value());
        writer.integral(event.generation.value());
        writer.integral(event.sequence.value());
        writer.digest(event.event_digest);
    }

    writer.integral(static_cast<std::uint16_t>(state.receipt_count));
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        const auto& receipt = state.receipts[index];
        writer.integral(receipt.identity.caller_id.value());
        writer.integral(receipt.identity.request_sequence.value());
        writer.digest(receipt.identity.payload_digest);
        write_result(writer, receipt.result);
    }
}

[[nodiscard]] PolicyRecord read_policy(Reader& reader) noexcept {
    PolicyRecord record;
    auto& value = record.artifact;
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
    if (value.rule_count > kMaxPolicyRules) return record;
    for (std::size_t index = 0; index < value.rule_count; ++index) {
        auto& rule = value.rules[index];
        rule.feature_index = reader.integral<std::uint16_t>();
        rule.predicate =
            static_cast<PredicateOperator>(reader.integral<std::uint8_t>());
        rule.directive = static_cast<Directive>(reader.integral<std::uint8_t>());
        rule.threshold = FeatureMicros::from_atoms(reader.integral<std::int64_t>());
    }
    record.lifecycle =
        static_cast<PolicyLifecycle>(reader.integral<std::uint8_t>());
    return record;
}

[[nodiscard]] ApplyResult read_result(Reader& reader) noexcept {
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
    return value;
}

[[nodiscard]] bool read_payload(const std::span<const std::byte> payload,
                                TradingState& state) noexcept {
    Reader reader(payload);
    state = {};
    state.state_version = reader.integral<std::uint64_t>();
    state.strategy_mode =
        static_cast<StrategyMode>(reader.integral<std::uint8_t>());
    state.kill_epoch = KillEpoch{reader.integral<std::uint64_t>()};
    state.executor_generation =
        ExecutorGeneration{reader.integral<std::uint64_t>()};

    state.policy_count = reader.integral<std::uint16_t>();
    if (state.policy_count > state.policies.size()) return false;
    for (std::size_t index = 0; index < state.policy_count; ++index) {
        state.policies[index] = read_policy(reader);
        if (state.policies[index].artifact.rule_count > kMaxPolicyRules) return false;
    }

    state.source_count = reader.integral<std::uint16_t>();
    if (state.source_count > state.sources.size()) return false;
    for (std::size_t index = 0; index < state.source_count; ++index) {
        state.sources[index] =
            SourceCursor{SourceId{reader.integral<std::uint64_t>()},
                         SourceGeneration{reader.integral<std::uint64_t>()},
                         SourceSequence{reader.integral<std::uint64_t>()}};
    }

    state.source_event_count = reader.integral<std::uint16_t>();
    if (state.source_event_count > state.source_events.size()) return false;
    for (std::size_t index = 0; index < state.source_event_count; ++index) {
        state.source_events[index] =
            SourceEventReceipt{SourceId{reader.integral<std::uint64_t>()},
                               SourceGeneration{reader.integral<std::uint64_t>()},
                               SourceSequence{reader.integral<std::uint64_t>()},
                               reader.digest()};
    }

    state.receipt_count = reader.integral<std::uint16_t>();
    if (state.receipt_count > state.receipts.size()) return false;
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        state.receipts[index] =
            RequestReceipt{CommandIdentity{CallerId{reader.integral<std::uint64_t>()},
                                           RequestSequence{
                                               reader.integral<std::uint64_t>()},
                                           reader.digest()},
                           read_result(reader)};
    }
    return reader.finished() && valid_state(state);
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

}  // namespace

EncodeStateSnapshotResult encode_state_snapshot(
    const TradingState& state, const std::span<std::byte> output) noexcept {
    if (!valid_state(state)) return {StateSnapshotError::invalid_state, 0, {}};

    const auto payload_bytes = payload_size(state);
    const auto frame_bytes = kStateSnapshotPayloadOffset + payload_bytes + kDigestBytes;
    if (output.size() < frame_bytes) {
        return {StateSnapshotError::output_too_small, 0, {}};
    }

    std::copy(kSnapshotMagic.begin(), kSnapshotMagic.end(), output.begin());
    Writer writer(output.subspan(kStateSnapshotMagicBytes));
    writer.integral(static_cast<std::uint32_t>(payload_bytes));
    writer.integral(kStateSnapshotSchemaVersion);
    write_payload(writer, state);

    const auto digest_end = kStateSnapshotPayloadOffset + payload_bytes;
    const auto state_digest = sha256_digest(output.subspan(
        kStateSnapshotDigestInputOffset,
        digest_end - kStateSnapshotDigestInputOffset));
    std::copy(state_digest.bytes.begin(), state_digest.bytes.end(),
              output.begin() + static_cast<std::ptrdiff_t>(digest_end));
    return {StateSnapshotError::none, frame_bytes, state_digest};
}

DecodeStateSnapshotResult decode_state_snapshot(
    const std::span<const std::byte> snapshot,
    TradingState& destination,
    StateSnapshotScratch& scratch) noexcept {
    constexpr auto minimum_frame = kStateSnapshotPayloadOffset + kDigestBytes;
    if (snapshot.size() < minimum_frame) {
        return {StateSnapshotError::truncated, {}};
    }
    if (!std::equal(kSnapshotMagic.begin(), kSnapshotMagic.end(), snapshot.begin())) {
        return {StateSnapshotError::invalid_magic, {}};
    }

    const auto payload_bytes = read_u32(snapshot, kStateSnapshotMagicBytes);
    if (payload_bytes > kMaxStateSnapshotPayloadBytes) {
        return {StateSnapshotError::length_mismatch, {}};
    }
    const auto expected_size =
        kStateSnapshotPayloadOffset + static_cast<std::size_t>(payload_bytes) +
        kDigestBytes;
    if (snapshot.size() < expected_size) {
        return {StateSnapshotError::truncated, {}};
    }
    if (snapshot.size() != expected_size) {
        return {StateSnapshotError::length_mismatch, {}};
    }

    if (read_u16(snapshot, kStateSnapshotSchemaOffset) !=
        kStateSnapshotSchemaVersion) {
        return {StateSnapshotError::unsupported_schema, {}};
    }

    const auto digest_offset = expected_size - kDigestBytes;
    const auto calculated = sha256_digest(snapshot.subspan(
        kStateSnapshotDigestInputOffset,
        digest_offset - kStateSnapshotDigestInputOffset));
    Digest256 retained;
    std::copy_n(snapshot.begin() + static_cast<std::ptrdiff_t>(digest_offset),
                retained.bytes.size(), retained.bytes.begin());
    if (calculated != retained) {
        return {StateSnapshotError::digest_mismatch, {}};
    }

    const auto payload = snapshot.subspan(kStateSnapshotPayloadOffset, payload_bytes);
    if (!read_payload(payload, scratch.candidate)) {
        return {StateSnapshotError::invalid_state, {}};
    }

    destination = scratch.candidate;
    return {StateSnapshotError::none, calculated};
}

}  // namespace velocita::v2
