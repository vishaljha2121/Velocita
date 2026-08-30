#pragma once

#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace velocita::v2 {

template <typename Tag, std::integral Rep = std::uint64_t>
class StrongId {
public:
    using rep_type = Rep;

    constexpr StrongId() noexcept = default;
    explicit constexpr StrongId(const Rep value) noexcept : value_(value) {}

    [[nodiscard]] constexpr Rep value() const noexcept { return value_; }

    constexpr auto operator<=>(const StrongId&) const noexcept = default;

private:
    Rep value_{};
};

struct CallerIdTag;
struct RequestSequenceTag;
struct CorrelationIdTag;
struct SourceIdTag;
struct SourceGenerationTag;
struct SourceSequenceTag;
struct PolicyIdTag;
struct PolicyVersionTag;
struct FeatureSchemaIdTag;
struct DecisionIdTag;
struct ClusterOrderIdTag;
struct EffectIdTag;
struct AccountIdTag;
struct InstrumentIdTag;
struct RiskPolicyVersionTag;
struct RiskVersionTag;
struct PositionVersionTag;
struct VenueIdTag;
struct VenueSessionIdTag;
struct VenueSequenceTag;
struct VenueOrderIdTag;
struct ExecutionIdTag;
struct ExecutorGenerationTag;
struct KillEpochTag;

using CallerId = StrongId<CallerIdTag>;
using RequestSequence = StrongId<RequestSequenceTag>;
using CorrelationId = StrongId<CorrelationIdTag>;
using SourceId = StrongId<SourceIdTag>;
using SourceGeneration = StrongId<SourceGenerationTag>;
using SourceSequence = StrongId<SourceSequenceTag>;
using PolicyId = StrongId<PolicyIdTag>;
using PolicyVersion = StrongId<PolicyVersionTag>;
using FeatureSchemaId = StrongId<FeatureSchemaIdTag>;
using DecisionId = StrongId<DecisionIdTag>;
using ClusterOrderId = StrongId<ClusterOrderIdTag>;
using EffectId = StrongId<EffectIdTag>;
using AccountId = StrongId<AccountIdTag>;
using InstrumentId = StrongId<InstrumentIdTag>;
using RiskPolicyVersion = StrongId<RiskPolicyVersionTag>;
using RiskVersion = StrongId<RiskVersionTag>;
using PositionVersion = StrongId<PositionVersionTag>;
using VenueId = StrongId<VenueIdTag>;
using VenueSessionId = StrongId<VenueSessionIdTag>;
using VenueSequence = StrongId<VenueSequenceTag>;
using VenueOrderId = StrongId<VenueOrderIdTag>;
using ExecutionId = StrongId<ExecutionIdTag>;
using ExecutorGeneration = StrongId<ExecutorGenerationTag>;
using KillEpoch = StrongId<KillEpochTag>;

enum class RoundingMode : std::uint8_t {
    toward_zero = 0,
    away_from_zero = 1,
    nearest_ties_away_from_zero = 2,
};

template <typename Tag, std::int64_t Scale, typename Rep = std::int64_t>
requires std::is_same_v<Rep, std::int64_t>
class FixedPoint {
    static_assert(Scale > 0, "fixed-point scale must be positive");

public:
    using rep_type = Rep;
    using tag_type = Tag;
    static constexpr std::int64_t scale = Scale;

    constexpr FixedPoint() noexcept = default;

    [[nodiscard]] static constexpr FixedPoint from_atoms(const Rep atoms) noexcept {
        return FixedPoint(atoms);
    }

    [[nodiscard]] constexpr Rep atoms() const noexcept { return atoms_; }

    [[nodiscard]] constexpr std::optional<FixedPoint>
    checked_add(const FixedPoint other) const noexcept {
        constexpr auto minimum = std::numeric_limits<Rep>::min();
        constexpr auto maximum = std::numeric_limits<Rep>::max();
        if ((other.atoms_ > 0 && atoms_ > maximum - other.atoms_) ||
            (other.atoms_ < 0 && atoms_ < minimum - other.atoms_)) {
            return std::nullopt;
        }
        return FixedPoint(atoms_ + other.atoms_);
    }

    [[nodiscard]] constexpr std::optional<FixedPoint>
    checked_sub(const FixedPoint other) const noexcept {
        constexpr auto minimum = std::numeric_limits<Rep>::min();
        constexpr auto maximum = std::numeric_limits<Rep>::max();
        if ((other.atoms_ > 0 && atoms_ < minimum + other.atoms_) ||
            (other.atoms_ < 0 && atoms_ > maximum + other.atoms_)) {
            return std::nullopt;
        }
        return FixedPoint(atoms_ - other.atoms_);
    }

    [[nodiscard]] constexpr std::optional<FixedPoint>
    checked_multiply(const Rep multiplier) const noexcept {
        constexpr auto minimum = std::numeric_limits<Rep>::min();
        constexpr auto maximum = std::numeric_limits<Rep>::max();
        if (atoms_ == 0 || multiplier == 0) return FixedPoint{};

        if (atoms_ > 0) {
            if ((multiplier > 0 && atoms_ > maximum / multiplier) ||
                (multiplier < 0 && multiplier < minimum / atoms_)) {
                return std::nullopt;
            }
        } else {
            if ((multiplier > 0 && atoms_ < minimum / multiplier) ||
                (multiplier < 0 && atoms_ < maximum / multiplier)) {
                return std::nullopt;
            }
        }
        return FixedPoint(atoms_ * multiplier);
    }

    template <std::int64_t TargetScale>
    [[nodiscard]] constexpr std::optional<FixedPoint<Tag, TargetScale, Rep>>
    rescale(const RoundingMode mode) const noexcept {
        static_assert(TargetScale > 0, "target fixed-point scale must be positive");
        static_assert((Scale % TargetScale) == 0 || (TargetScale % Scale) == 0,
                      "fixed-point scales must divide exactly");
        using Target = FixedPoint<Tag, TargetScale, Rep>;

        if constexpr (TargetScale == Scale) {
            return Target::from_atoms(atoms_);
        } else if constexpr (TargetScale > Scale) {
            constexpr Rep multiplier = TargetScale / Scale;
            const auto expanded = checked_multiply(multiplier);
            if (!expanded) return std::nullopt;
            return Target::from_atoms(expanded->atoms());
        } else {
            constexpr Rep divisor = Scale / TargetScale;
            Rep quotient = atoms_ / divisor;
            const Rep remainder = atoms_ % divisor;
            if (remainder == 0 || mode == RoundingMode::toward_zero) {
                return Target::from_atoms(quotient);
            }

            bool increment = mode == RoundingMode::away_from_zero;
            if (mode == RoundingMode::nearest_ties_away_from_zero) {
                const auto magnitude = remainder < 0 ? -remainder : remainder;
                constexpr Rep threshold = divisor / 2 + divisor % 2;
                increment = magnitude >= threshold;
            }
            if (increment) quotient += atoms_ < 0 ? -1 : 1;
            return Target::from_atoms(quotient);
        }
    }

    constexpr auto operator<=>(const FixedPoint&) const noexcept = default;

private:
    explicit constexpr FixedPoint(const Rep atoms) noexcept : atoms_(atoms) {}

    Rep atoms_{};
};

struct PriceTicksTag;
struct QuantityLotsTag;
struct MoneyMicrosTag;
struct FeeMicrosTag;
struct FeatureMicrosTag;

using PriceTicks = FixedPoint<PriceTicksTag, 1>;
using QuantityLots = FixedPoint<QuantityLotsTag, 1>;
using MoneyMicros = FixedPoint<MoneyMicrosTag, 1'000'000>;
using FeeMicros = FixedPoint<FeeMicrosTag, 1'000'000>;
using FeatureMicros = FixedPoint<FeatureMicrosTag, 1'000'000>;

}  // namespace velocita::v2
