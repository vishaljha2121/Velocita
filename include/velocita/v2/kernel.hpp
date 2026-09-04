#pragma once

#include "velocita/v2/contracts.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace velocita::v2 {

inline constexpr std::size_t kMaxPolicies = 16;
inline constexpr std::size_t kMaxSources = 16;
inline constexpr std::size_t kMaxSourceEventReceipts = 128;
inline constexpr std::size_t kMaxRequestReceipts = 256;
static_assert(kMaxPolicies > 0);
static_assert(kMaxSources > 0);
static_assert(kMaxSourceEventReceipts > kMaxSources);
static_assert(kMaxRequestReceipts > kMaxSourceEventReceipts);
static_assert(kMaxRequestReceipts > kMaxPolicies);

enum class PolicyLifecycle : std::uint8_t {
    none = 0,
    proposed = 1,
    validated = 2,
    active = 3,
    superseded = 4,
};

enum class ApplyCode : std::uint8_t {
    applied = 0,
    already_applied,
    invalid_command,
    invalid_policy_contract,
    duplicate_conflict,
    policy_not_found,
    immutable_policy_conflict,
    non_monotonic_policy_version,
    invalid_policy_transition,
    active_policy_scope_conflict,
    stale_control_generation,
    non_monotonic_kill_epoch,
    non_monotonic_executor_generation,
    source_event_conflict,
    source_generation_gap,
    source_generation_regression,
    source_sequence_gap,
    source_sequence_regression,
    source_capacity_exceeded,
    source_event_capacity_exceeded,
    policy_capacity_exceeded,
    receipt_capacity_exceeded,
    state_version_exhausted,
};

struct CommandIdentity {
    CallerId caller_id{};
    RequestSequence request_sequence{};
    Digest256 payload_digest{};

    constexpr auto operator<=>(const CommandIdentity&) const noexcept = default;
};

struct PublishPolicy {
    PolicyArtifact artifact{};
};

struct ValidatePolicy {
    PolicyId policy_id{};
    PolicyVersion policy_version{};
};

struct ActivatePolicy {
    PolicyId policy_id{};
    PolicyVersion policy_version{};
};

struct SupersedePolicy {
    PolicyId policy_id{};
    PolicyVersion policy_version{};
};

struct SetStrategyMode {
    StrategyMode mode{StrategyMode::halted};
    KillEpoch expected_kill_epoch{};
    ExecutorGeneration expected_executor_generation{};
};

struct AdvanceKillEpoch {
    KillEpoch new_kill_epoch{};
};

struct SetExecutorGeneration {
    ExecutorGeneration new_generation{};
};

struct RecordSourceEvent {
    SourceId source_id{};
    SourceGeneration source_generation{};
    SourceSequence source_sequence{};
    Digest256 event_digest{};
};

using CommandBody = std::variant<PublishPolicy,
                                 ValidatePolicy,
                                 ActivatePolicy,
                                 SupersedePolicy,
                                 SetStrategyMode,
                                 AdvanceKillEpoch,
                                 SetExecutorGeneration,
                                 RecordSourceEvent>;

struct Command {
    CommandIdentity identity{};
    CommandBody body{};
};

struct ApplyResult {
    ApplyCode code{ApplyCode::invalid_command};
    ContractError contract_error{ContractError::none};
    PolicyId policy_id{};
    PolicyVersion policy_version{};
    PolicyLifecycle policy_lifecycle{PolicyLifecycle::none};
    std::uint64_t state_version{};
    StrategyMode strategy_mode{StrategyMode::halted};
    KillEpoch kill_epoch{};
    ExecutorGeneration executor_generation{};
    SourceId source_id{};
    SourceGeneration source_generation{};
    SourceSequence source_sequence{};

    constexpr auto operator<=>(const ApplyResult&) const noexcept = default;
};

struct PolicyRecord {
    PolicyArtifact artifact{};
    PolicyLifecycle lifecycle{PolicyLifecycle::none};
};

struct RequestReceipt {
    CommandIdentity identity{};
    ApplyResult result{};
};

struct SourceCursor {
    SourceId source_id{};
    SourceGeneration generation{};
    SourceSequence sequence{};
};

struct SourceEventReceipt {
    SourceId source_id{};
    SourceGeneration generation{};
    SourceSequence sequence{};
    Digest256 event_digest{};
};

// TradingState has one mutable owner. References into its arrays must not be
// retained across apply(): later phases may reuse slots during replicated
// expiry even though std::array itself never reallocates.
struct TradingState {
    std::uint64_t state_version{};
    StrategyMode strategy_mode{StrategyMode::halted};
    KillEpoch kill_epoch{};
    ExecutorGeneration executor_generation{};
    std::size_t policy_count{};
    std::array<PolicyRecord, kMaxPolicies> policies{};
    std::size_t source_count{};
    std::array<SourceCursor, kMaxSources> sources{};
    std::size_t source_event_count{};
    std::array<SourceEventReceipt, kMaxSourceEventReceipts> source_events{};
    std::size_t receipt_count{};
    std::array<RequestReceipt, kMaxRequestReceipts> receipts{};
};

// Reserved for bounded temporary indexes/encoding buffers in later reducer
// increments. It is caller-owned so apply never needs hidden thread-local or
// heap-backed scratch state.
struct ApplyScratch final {};

[[nodiscard]] ApplyResult apply(TradingState& state,
                                const Command& command,
                                ApplyScratch& scratch) noexcept;

[[nodiscard]] const PolicyRecord* find_policy(const TradingState& state,
                                              PolicyId policy_id,
                                              PolicyVersion policy_version) noexcept;

[[nodiscard]] const SourceCursor* find_source_cursor(const TradingState& state,
                                                     SourceId source_id) noexcept;

static_assert(std::is_nothrow_copy_assignable_v<PolicyArtifact>);
static_assert(std::is_nothrow_copy_assignable_v<CommandBody>);
static_assert(std::is_nothrow_copy_assignable_v<ApplyResult>);
static_assert(sizeof(TradingState) <= kTradingStateObjectBudgetBytes,
              "TradingState exceeded its reviewed in-memory object budget");

}  // namespace velocita::v2
