#pragma once

#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/kernel.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace velocita::v2 {

// A research rule program (model/.../research/rules.py) turned into the exact
// kernel commands that install it and evaluate it in SHADOW mode, or in paper
// mode as authoritative frames the kernel may authorize. Pure: no I/O,
// no clock. Every economic unit comes from the execution-catalog row the
// caller passes in; nothing here invents a tick size, lot or fee.

enum class ShadowAction : std::uint8_t { hold, enter_long, exit };

struct ShadowRule {
    std::uint16_t feature{};
    PredicateOperator op{};
    std::int64_t threshold{};  // feature micros
    ShadowAction action{};
};

struct ShadowProgram {
    PolicyId policy_id{};
    PolicyVersion policy_version{};
    FeatureSchemaId feature_schema_id{};
    RiskPolicyVersion risk_policy_version{};
    Digest256 model_digest{};             // names the research program that produced the rules
    std::int64_t valid_from_ns{};
    std::int64_t valid_until_ns{};
    std::uint16_t generic_features{};    // unit-free features first; the price feature follows them
    std::vector<ShadowRule> rules;
    ExecutionCatalogRow row;              // account, instrument and venue the policy binds to
    TimeInForce time_in_force{};
    PostOnlyMode post_only{};
    QuantityLots order_quantity{};        // the fixed clip a proposal would carry
    MoneyMicros max_notional{};
    std::int64_t max_market_data_age_ns{};
};

struct ShadowSetup {
    std::vector<Command> commands;         // apply in order
    Digest256 executable_policy_digest{};
};

// Publish, validate and activate the policy, stage and activate its executable
// bundle, then set strategy mode normal. KILL_EPOCH and EXECUTOR_GENERATION
// must be the cluster's CURRENT values, or the mode change is refused.
// CALLER must be an operator caller. Request sequences start at FIRST_SEQUENCE.
[[nodiscard]] ShadowSetup shadow_setup(const ShadowProgram& program, CallerId caller, std::uint64_t first_sequence,
                                       KillEpoch kill_epoch, ExecutorGeneration executor_generation);

struct ShadowTick {
    SourceId source{};
    SourceGeneration generation{};         // +1 on every edge restart, sequences restart at 1
    SourceSequence sequence{};             // contiguous within a generation
    std::int64_t event_time_ns{};
    std::int64_t receive_time_ns{};
    std::int64_t market_data_age_ns{};
    std::vector<std::int64_t> features;    // generic features (micros), then the price in ticks
    Digest256 evidence_digest{};           // digest of the raw market data behind this tick
};

// How a frame is evaluated and which risk state it binds. Shadow frames bind
// 1/1 as they always have; paper frames bind the account's CURRENT versions,
// because AuthorizeDecision refuses a frame bound to any other.
struct FrameBinding {
    EvaluationMode mode{EvaluationMode::shadow};
    RiskVersion risk_version{1};
    PositionVersion position_version{1};
};

// RecordSourceEvent, then EvaluateDecisionFrame (sequences FIRST_SEQUENCE and
// FIRST_SEQUENCE + 1). Shadow decisions never authorize.
[[nodiscard]] std::array<Command, 2> shadow_tick(const ShadowProgram& program, const ShadowTick& tick,
                                                 CallerId caller, std::uint64_t first_sequence, DecisionId decision,
                                                 const FrameBinding& binding = {});

// Paper: a committed evaluation whose proposal the kernel could authorize.
[[nodiscard]] bool authorizable(const ApplyResult& evaluated) noexcept;

// Paper, per tick: ask the kernel, withhold, or nothing to ask. EXITS_ONLY is the
// edge's buy stop (weekly loss budget or paper balance spent): a proposal from
// an enter_long rule is withheld, exits still go. A matched index the program
// does not have is treated as a buy, so the stop fails closed.
enum class PaperRequest : std::uint8_t { none, authorize, withheld };
[[nodiscard]] PaperRequest paper_request(const ShadowProgram& program, const ApplyResult& evaluated,
                                         bool exits_only) noexcept;

[[nodiscard]] Command authorize_decision(CallerId caller, std::uint64_t sequence, DecisionId decision);

inline constexpr ApplyResultDetailKind kRiskStateDetailKind = ApplyResultDetailKind::risk_state;

struct RiskView {
    RiskVersion risk_version{1};
    PositionVersion position_version{1};
    std::optional<QuantityLots> position;  // known only once the kernel has reported it

    constexpr bool operator==(const RiskView&) const noexcept = default;
};

// The view the next paper frame binds after AUTHORIZATION: the kernel's own
// versions when it refused as stale and reported them; otherwise unchanged.
// A report carrying a zero version is ignored: zero is never a live version.
[[nodiscard]] RiskView resync_risk_view(const RiskView& current, const ApplyResult& authorization) noexcept;

// The generation SOURCE's next event must use after its RecordSourceEvent was
// refused as a generation gap: one past the cursor the kernel reported (1 when
// it has none). nullopt for any other result. Only a gap is re-based: a
// regression means the kernel holds events this submitter's state never
// reserved (a rolled-back state file or a second writer), so it fails closed.
[[nodiscard]] std::optional<SourceGeneration> resync_source_generation(SourceId source,
                                                                       const ApplyResult& recorded) noexcept;

}  // namespace velocita::v2
