#pragma once

#include "velocita/v2/domain.hpp"
#include "velocita/v2/layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace velocita::v2 {

inline constexpr std::uint16_t kPolicyArtifactSchemaVersion = 1;
inline constexpr std::uint16_t kDecisionFrameSchemaVersion = 2;
inline constexpr std::uint16_t kOrderEffectSchemaVersion = 2;
inline constexpr std::uint16_t kVenueObservationSchemaVersion = 2;
inline constexpr std::size_t kDigestBytes = 32;
inline constexpr std::size_t kMaxPolicyRules = 32;
inline constexpr std::size_t kMaxDecisionFeatures = 32;

struct Digest256 {
    std::array<std::byte, kDigestBytes> bytes{};

    [[nodiscard]] constexpr bool is_zero() const noexcept {
        for (const auto byte : bytes) {
            if (byte != std::byte{0}) return false;
        }
        return true;
    }

    constexpr auto operator<=>(const Digest256&) const noexcept = default;
};

enum class ContractError : std::uint8_t {
    none = 0,
    invalid_identity,
    unsupported_schema,
    missing_digest,
    invalid_time_range,
    capacity_exceeded,
    empty_rules,
    empty_features,
    invalid_feature_index,
    invalid_age,
    invalid_enum,
    non_positive_price,
    non_positive_quantity,
    non_positive_notional,
    invalid_value_source,
    non_canonical_value,
    invalid_safety_cap,
    invalid_order_target,
    invalid_action_template,
    invalid_capability,
};

enum class PredicateOperator : std::uint8_t {
    less_than = 1,
    less_than_or_equal = 2,
    equal = 3,
    greater_than_or_equal = 4,
    greater_than = 5,
};

enum class Directive : std::uint8_t {
    no_action = 1,
    place_limit = 2,
    cancel = 3,
    replace = 4,
};

enum class MarketDataStatus : std::uint8_t {
    ready = 1,
    stale = 2,
    recovering = 3,
    crossed = 4,
    incomplete = 5,
};

enum class StrategyMode : std::uint8_t {
    normal = 1,
    no_new_risk = 2,
    cancel_only = 3,
    reduce_only = 4,
    halted = 5,
};

enum class Side : std::uint8_t {
    buy = 1,
    sell = 2,
};

enum class TimeInForce : std::uint8_t {
    good_til_cancel = 1,
    immediate_or_cancel = 2,
    fill_or_kill = 3,
    post_only = 4,
};

enum class EffectOperation : std::uint8_t {
    submit = 1,
    cancel = 2,
    replace = 3,
};

// Who authored an observation's identity (architecture 10.15).
//
// A NATIVE observation is the venue's own event and its sequence is the venue's,
// so `(venue, session, sequence)` is a real identity. A SYNTHETIC one is
// authored by the gateway - a declared timeout, or an answer to a query it asked
// - and its sequence was chosen by the shell. Deduplicating both in one domain
// lets a gateway-chosen number collide with a venue-chosen one, and the loser is
// real economic evidence in either direction.
//
// Derived from the kind rather than carried on the wire, so the two cannot
// disagree.
enum class ObservationOrigin : std::uint8_t {
    native = 1,
    synthetic = 2,
};

enum class VenueObservationKind : std::uint8_t {
    order_acknowledged = 1,
    order_rejected = 2,
    fill = 3,
    cancelled = 4,
    effect_unknown = 5,
    // Terminal venue evidence: quantity is final cumulative filled quantity,
    // not an incremental fill. Only accepted fills may change position.
    cancel_reconciled = 6,
    // A reconciliation query's answer about one order (architecture 10.1.6).
    //
    // Like `cancel_reconciled`, quantity is FINAL CUMULATIVE filled quantity
    // rather than an increment, and that is the whole safety argument. A query
    // answer is a snapshot, not an event, so it has no venue sequence of its own
    // and session-scoped identity is unsafe across exactly the reconnects
    // reconciliation runs after. Cumulative evidence makes a repeat harmless BY
    // VALUE - apply books the difference against what it already recorded, so a
    // second delivery computes a delta of zero - which demotes deduplication
    // from a correctness requirement to an optimisation.
    //
    // Uniquely, `venue_order_id` MAY be zero here, meaning the venue answered
    // that it holds no such order. That answer is the point of reconciliation:
    // it proves nothing was placed, so the reservation was never real risk and
    // can be released. `validate` enforces that absence implies zero fill.
    order_reconciled = 7,
};

// Total by construction: every kind is named, so adding one forces a decision
// here rather than defaulting into the venue's identity domain by omission.
[[nodiscard]] constexpr ObservationOrigin observation_origin(
    const VenueObservationKind kind) noexcept {
    switch (kind) {
        case VenueObservationKind::order_acknowledged:
        case VenueObservationKind::order_rejected:
        case VenueObservationKind::fill:
        case VenueObservationKind::cancelled:
            return ObservationOrigin::native;
        case VenueObservationKind::effect_unknown:
        case VenueObservationKind::cancel_reconciled:
        case VenueObservationKind::order_reconciled:
            return ObservationOrigin::synthetic;
    }
    // An unknown kind is refused upstream by `validate`; treating it as native
    // here keeps the strictest deduplication for anything unrecognised.
    return ObservationOrigin::native;
}

struct PolicyRule {
    std::uint16_t feature_index{};
    PredicateOperator predicate{};
    FeatureMicros threshold{};
    Directive directive{};
};

struct PolicyArtifact {
    PolicyId policy_id{};
    PolicyVersion policy_version{};
    std::uint16_t schema_version{};
    FeatureSchemaId feature_schema_id{};
    InstrumentId instrument_id{};
    AccountId account_id{};
    Digest256 model_digest{};
    Digest256 artifact_digest{};
    std::int64_t valid_from_event_time_ns{};
    std::int64_t valid_until_event_time_ns{};
    RiskPolicyVersion risk_policy_version{};
    std::uint16_t rule_count{};
    std::array<PolicyRule, kMaxPolicyRules> rules{};
};

struct DecisionFrame {
    PolicyId policy_id{};
    PolicyVersion policy_version{};
    FeatureSchemaId feature_schema_id{};
    VenueId venue_id{};
    VenueSessionId venue_session_id{};
    VenueSequence venue_sequence{};
    SourceId source_id{};
    SourceGeneration source_generation{};
    SourceSequence source_sequence{};
    std::int64_t event_time_ns{};
    std::int64_t receive_time_ns{};
    InstrumentId instrument_id{};
    AccountId account_id{};
    std::uint16_t feature_count{};
    std::array<FeatureMicros, kMaxDecisionFeatures> features{};
    MarketDataStatus market_data_status{};
    std::int64_t market_data_age_ns{};
    PositionVersion position_version{};
    RiskVersion risk_version{};
    Digest256 raw_evidence_digest{};
};

inline constexpr std::size_t kDecisionFrameArenaSlotStrideBytes =
    round_up_to_cache_line(sizeof(DecisionFrame));

static_assert(std::is_trivially_copyable_v<DecisionFrame>);
static_assert(sizeof(DecisionFrame) <= kDecisionFrameObjectBudgetBytes,
              "DecisionFrame exceeded its reviewed in-memory object budget");
static_assert(sizeof(PolicyArtifact) <= kPolicyArtifactObjectBudgetBytes,
              "PolicyArtifact exceeded its reviewed in-memory object budget");

enum class PostOnlyMode : std::uint8_t {
    prohibited = 1,
    required = 2,
};

struct OrderEffect {
    EffectId effect_id{};
    ClusterOrderId cluster_order_id{};
    std::uint64_t committed_directive_index{};
    ExecutorGeneration executor_generation{};
    KillEpoch kill_epoch{};
    VenueId venue_id{};
    AccountId account_id{};
    InstrumentId instrument_id{};
    EffectOperation operation{};
    Side side{};
    PriceTicks price{};
    QuantityLots quantity{};
    TimeInForce time_in_force{};
    PostOnlyMode post_only{PostOnlyMode::prohibited};
    PolicyId policy_id{};
    PolicyVersion policy_version{};
    DecisionId decision_id{};
    RiskVersion risk_version{};
    SourceGeneration source_generation{};
    SourceSequence source_sequence{};
    Digest256 payload_digest{};
};

struct VenueObservation {
    VenueObservationKind kind{};
    VenueId venue_id{};
    VenueSessionId venue_session_id{};
    VenueSequence venue_sequence{};
    ClusterOrderId cluster_order_id{};
    EffectId effect_id{};
    VenueOrderId venue_order_id{};
    ExecutionId execution_id{};
    Side side{};
    PriceTicks price{};
    QuantityLots quantity{};
    FeeMicros fee{};
    Digest256 evidence_digest{};
};

namespace detail {

template <typename Id>
[[nodiscard]] constexpr bool present(const Id id) noexcept {
    return id.value() != 0;
}

[[nodiscard]] constexpr bool known(const PredicateOperator value) noexcept {
    switch (value) {
        case PredicateOperator::less_than:
        case PredicateOperator::less_than_or_equal:
        case PredicateOperator::equal:
        case PredicateOperator::greater_than_or_equal:
        case PredicateOperator::greater_than:
            return true;
    }
    return false;
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

[[nodiscard]] constexpr bool known(const MarketDataStatus value) noexcept {
    switch (value) {
        case MarketDataStatus::ready:
        case MarketDataStatus::stale:
        case MarketDataStatus::recovering:
        case MarketDataStatus::crossed:
        case MarketDataStatus::incomplete:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool known(const Side value) noexcept {
    return value == Side::buy || value == Side::sell;
}

[[nodiscard]] constexpr bool known(const TimeInForce value) noexcept {
    switch (value) {
        case TimeInForce::good_til_cancel:
        case TimeInForce::immediate_or_cancel:
        case TimeInForce::fill_or_kill:
        case TimeInForce::post_only:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool known(const EffectOperation value) noexcept {
    return value == EffectOperation::submit || value == EffectOperation::cancel ||
           value == EffectOperation::replace;
}

[[nodiscard]] constexpr bool known(const VenueObservationKind value) noexcept {
    switch (value) {
        case VenueObservationKind::order_acknowledged:
        case VenueObservationKind::order_rejected:
        case VenueObservationKind::fill:
        case VenueObservationKind::cancelled:
        case VenueObservationKind::effect_unknown:
        case VenueObservationKind::cancel_reconciled:
        case VenueObservationKind::order_reconciled:
            return true;
    }
    return false;
}

}  // namespace detail

[[nodiscard]] constexpr ContractError validate(const PolicyArtifact& policy) noexcept {
    if (!detail::present(policy.policy_id) || !detail::present(policy.policy_version) ||
        !detail::present(policy.feature_schema_id) ||
        !detail::present(policy.instrument_id) || !detail::present(policy.account_id) ||
        !detail::present(policy.risk_policy_version)) {
        return ContractError::invalid_identity;
    }
    if (policy.schema_version != kPolicyArtifactSchemaVersion) {
        return ContractError::unsupported_schema;
    }
    if (policy.model_digest.is_zero() || policy.artifact_digest.is_zero()) {
        return ContractError::missing_digest;
    }
    if (policy.valid_until_event_time_ns <= policy.valid_from_event_time_ns) {
        return ContractError::invalid_time_range;
    }
    if (policy.rule_count > kMaxPolicyRules) return ContractError::capacity_exceeded;
    if (policy.rule_count == 0) return ContractError::empty_rules;
    for (std::size_t index = 0; index < policy.rule_count; ++index) {
        const auto& rule = policy.rules[index];
        if (rule.feature_index >= kMaxDecisionFeatures) {
            return ContractError::invalid_feature_index;
        }
        if (!detail::known(rule.predicate) || !detail::known(rule.directive)) {
            return ContractError::invalid_enum;
        }
    }
    return ContractError::none;
}

[[nodiscard]] constexpr ContractError validate(const DecisionFrame& frame) noexcept {
    if (!detail::present(frame.policy_id) || !detail::present(frame.policy_version) ||
        !detail::present(frame.feature_schema_id) || !detail::present(frame.venue_id) ||
        !detail::present(frame.venue_session_id) ||
        !detail::present(frame.venue_sequence) ||
        !detail::present(frame.source_id) ||
        !detail::present(frame.source_generation) ||
        !detail::present(frame.source_sequence) ||
        !detail::present(frame.instrument_id) || !detail::present(frame.account_id) ||
        !detail::present(frame.position_version) || !detail::present(frame.risk_version)) {
        return ContractError::invalid_identity;
    }
    if (frame.event_time_ns <= 0 || frame.receive_time_ns <= 0) {
        return ContractError::invalid_time_range;
    }
    if (frame.feature_count > kMaxDecisionFeatures) return ContractError::capacity_exceeded;
    if (frame.feature_count == 0) return ContractError::empty_features;
    if (!detail::known(frame.market_data_status)) return ContractError::invalid_enum;
    if (frame.market_data_age_ns < 0) return ContractError::invalid_age;
    if (frame.raw_evidence_digest.is_zero()) return ContractError::missing_digest;
    return ContractError::none;
}

[[nodiscard]] constexpr ContractError validate(const OrderEffect& effect) noexcept {
    if (effect.operation == EffectOperation::cancel &&
        (effect.price.atoms() != 0 || effect.quantity.atoms() != 0))
        return ContractError::invalid_action_template;
    if (effect.post_only != PostOnlyMode::prohibited &&
        effect.post_only != PostOnlyMode::required) return ContractError::invalid_enum;
    if (effect.post_only == PostOnlyMode::required &&
        effect.time_in_force != TimeInForce::good_til_cancel)
        return ContractError::invalid_action_template;
    if (!detail::present(effect.effect_id) ||
        !detail::present(effect.cluster_order_id) || effect.committed_directive_index == 0 ||
        !detail::present(effect.executor_generation) || !detail::present(effect.kill_epoch) ||
        !detail::present(effect.venue_id) || !detail::present(effect.account_id) ||
        !detail::present(effect.instrument_id) || !detail::present(effect.policy_id) ||
        !detail::present(effect.policy_version) || !detail::present(effect.decision_id) ||
        !detail::present(effect.risk_version) ||
        !detail::present(effect.source_generation) ||
        !detail::present(effect.source_sequence)) {
        return ContractError::invalid_identity;
    }
    if (!detail::known(effect.operation) || !detail::known(effect.side) ||
        !detail::known(effect.time_in_force)) {
        return ContractError::invalid_enum;
    }
    if (effect.operation != EffectOperation::cancel && effect.price.atoms() <= 0) {
        return ContractError::non_positive_price;
    }
    if (effect.operation != EffectOperation::cancel && effect.quantity.atoms() <= 0) {
        return ContractError::non_positive_quantity;
    }
    if (effect.payload_digest.is_zero()) return ContractError::missing_digest;
    return ContractError::none;
}

[[nodiscard]] constexpr ContractError validate(const VenueObservation& observation) noexcept {
    if (!detail::known(observation.kind)) return ContractError::invalid_enum;
    if (!detail::present(observation.venue_id) ||
        !detail::present(observation.venue_session_id) ||
        !detail::present(observation.venue_sequence) ||
        !detail::present(observation.cluster_order_id) ||
        !detail::present(observation.effect_id)) {
        return ContractError::invalid_identity;
    }
    if (observation.evidence_digest.is_zero()) return ContractError::missing_digest;

    switch (observation.kind) {
        case VenueObservationKind::cancel_reconciled:
            if (!detail::present(observation.venue_order_id))
                return ContractError::invalid_identity;
            if (observation.quantity.atoms() < 0 || observation.price.atoms() != 0 ||
                observation.fee.atoms() != 0 || observation.execution_id.value() != 0)
                return ContractError::non_canonical_value;
            return ContractError::none;
        case VenueObservationKind::order_acknowledged:
        case VenueObservationKind::cancelled:
            if (!detail::present(observation.venue_order_id)) {
                return ContractError::invalid_identity;
            }
            return ContractError::none;
        case VenueObservationKind::fill:
            if (!detail::present(observation.venue_order_id) ||
                !detail::present(observation.execution_id)) {
                return ContractError::invalid_identity;
            }
            if (!detail::known(observation.side)) return ContractError::invalid_enum;
            if (observation.price.atoms() <= 0) return ContractError::non_positive_price;
            if (observation.quantity.atoms() <= 0) {
                return ContractError::non_positive_quantity;
            }
            return ContractError::none;
        case VenueObservationKind::order_reconciled:
            // A snapshot, never an execution: no price, fee, or execution
            // identity may ride along and be mistaken for one.
            if (observation.price.atoms() != 0 || observation.fee.atoms() != 0 ||
                observation.execution_id.value() != 0) {
                return ContractError::non_canonical_value;
            }
            if (observation.quantity.atoms() < 0) {
                return ContractError::non_canonical_value;
            }
            // Absence implies nothing filled. "The venue denies this order but
            // reports it filled" is incoherent, so it is refused at the contract
            // boundary rather than adjudicated deeper in where the two halves
            // would have to be reconciled against each other.
            if (!detail::present(observation.venue_order_id) &&
                observation.quantity.atoms() != 0) {
                return ContractError::non_canonical_value;
            }
            return ContractError::none;
        case VenueObservationKind::order_rejected:
        case VenueObservationKind::effect_unknown:
            return ContractError::none;
    }
    return ContractError::invalid_enum;
}

}  // namespace velocita::v2
