// Turns a research rule program into the commands that install it on the cluster.
#pragma once

#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/kernel.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace velocita::v2 {

enum class ShadowAction : std::uint8_t { hold, enter_long, exit };

struct ShadowRule {
    std::uint16_t feature{};
    PredicateOperator op{};
    std::int64_t threshold{};
    ShadowAction action{};
};

struct ShadowProgram {
    PolicyId policy_id{};
    PolicyVersion policy_version{};
    FeatureSchemaId feature_schema_id{};
    RiskPolicyVersion risk_policy_version{};
    Digest256 model_digest{};
    std::int64_t valid_from_ns{};
    std::int64_t valid_until_ns{};
    std::uint16_t generic_features{};
    std::vector<ShadowRule> rules;
    ExecutionCatalogRow row;
    TimeInForce time_in_force{};
    PostOnlyMode post_only{};
    QuantityLots order_quantity{};
    MoneyMicros max_notional{};
    std::int64_t max_market_data_age_ns{};
};

struct ShadowSetup {
    std::vector<Command> commands;
    Digest256 executable_policy_digest{};
};

[[nodiscard]] ShadowSetup shadow_setup(const ShadowProgram& program, CallerId caller, std::uint64_t first_sequence,
                                       KillEpoch kill_epoch, ExecutorGeneration executor_generation);

struct ShadowTick {
    SourceId source{};
    SourceGeneration generation{};
    SourceSequence sequence{};
    std::int64_t event_time_ns{};
    std::int64_t receive_time_ns{};
    std::int64_t market_data_age_ns{};
    std::vector<std::int64_t> features;
    Digest256 evidence_digest{};
};

struct FrameBinding {
    EvaluationMode mode{EvaluationMode::shadow};
    RiskVersion risk_version{1};
    PositionVersion position_version{1};
};

[[nodiscard]] std::array<Command, 2> shadow_tick(const ShadowProgram& program, const ShadowTick& tick,
                                                 CallerId caller, std::uint64_t first_sequence, DecisionId decision,
                                                 const FrameBinding& binding = {});

[[nodiscard]] bool authorizable(const ApplyResult& evaluated) noexcept;

enum class PaperRequest : std::uint8_t { none, authorize, withheld };
[[nodiscard]] PaperRequest paper_request(const ShadowProgram& program, const ApplyResult& evaluated,
                                         bool exits_only) noexcept;

[[nodiscard]] Command authorize_decision(CallerId caller, std::uint64_t sequence, DecisionId decision);

inline constexpr ApplyResultDetailKind kRiskStateDetailKind = ApplyResultDetailKind::risk_state;

struct RiskView {
    RiskVersion risk_version{1};
    PositionVersion position_version{1};
    std::optional<QuantityLots> position;

    constexpr bool operator==(const RiskView&) const noexcept = default;
};

[[nodiscard]] RiskView resync_risk_view(const RiskView& current, const ApplyResult& authorization) noexcept;

[[nodiscard]] std::optional<SourceGeneration> resync_source_generation(SourceId source,
                                                                       const ApplyResult& recorded) noexcept;

}
