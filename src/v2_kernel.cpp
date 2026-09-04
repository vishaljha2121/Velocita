#include "velocita/v2/kernel.hpp"
#include "velocita/v2/rule_evaluator.hpp"

#include <limits>

namespace velocita::v2 {
namespace {

[[nodiscard]] bool present(const CommandIdentity& identity) noexcept {
    return identity.caller_id.value() != 0 && identity.request_sequence.value() != 0 &&
           !identity.payload_digest.is_zero();
}

[[nodiscard]] bool present(const PolicyId policy_id,
                           const PolicyVersion policy_version) noexcept {
    return policy_id.value() != 0 && policy_version.value() != 0;
}

[[nodiscard]] bool known(const StrategyMode mode) noexcept {
    switch (mode) {
        case StrategyMode::normal:
        case StrategyMode::no_new_risk:
        case StrategyMode::cancel_only:
        case StrategyMode::reduce_only:
        case StrategyMode::halted:
            return true;
    }
    return false;
}

[[nodiscard]] bool known(const EvaluationMode mode) noexcept {
    return mode == EvaluationMode::authoritative || mode == EvaluationMode::shadow;
}

[[nodiscard]] PolicyRecord* find_policy_mutable(TradingState& state,
                                                const PolicyId policy_id,
                                                const PolicyVersion policy_version) noexcept {
    for (std::size_t index = 0; index < state.policy_count; ++index) {
        auto& record = state.policies[index];
        if (record.artifact.policy_id == policy_id &&
            record.artifact.policy_version == policy_version) {
            return &record;
        }
    }
    return nullptr;
}

[[nodiscard]] SourceCursor* find_source_cursor_mutable(
    TradingState& state, const SourceId source_id) noexcept {
    for (std::size_t index = 0; index < state.source_count; ++index) {
        if (state.sources[index].source_id == source_id) {
            return &state.sources[index];
        }
    }
    return nullptr;
}

[[nodiscard]] const SourceEventReceipt* find_source_event(
    const TradingState& state,
    const SourceId source_id,
    const SourceGeneration generation,
    const SourceSequence sequence) noexcept {
    for (std::size_t index = 0; index < state.source_event_count; ++index) {
        const auto& event = state.source_events[index];
        if (event.source_id == source_id && event.generation == generation &&
            event.sequence == sequence) {
            return &event;
        }
    }
    return nullptr;
}

[[nodiscard]] const RequestReceipt*
find_receipt(const TradingState& state, const CommandIdentity& identity) noexcept {
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        const auto& receipt = state.receipts[index];
        if (receipt.identity.caller_id == identity.caller_id &&
            receipt.identity.request_sequence == identity.request_sequence) {
            return &receipt;
        }
    }
    return nullptr;
}

[[nodiscard]] std::size_t find_decision_index(const TradingState& state,
                                              const DecisionId decision_id) noexcept {
    for (std::size_t index = 0; index < state.decision_count; ++index) {
        if (state.decision_hot[index].decision_id == decision_id) return index;
    }
    return state.decision_count;
}

[[nodiscard]] ApplyResult result(const TradingState& state,
                                 const ApplyCode code,
                                 const std::uint64_t state_version,
                                 const PolicyId policy_id = {},
                                 const PolicyVersion policy_version = {},
                                 const PolicyLifecycle lifecycle = PolicyLifecycle::none,
                                 const ContractError contract_error = ContractError::none,
                                 const SourceId source_id = {},
                                 const SourceGeneration source_generation = {},
                                 const SourceSequence source_sequence = {}) noexcept {
    return {code,
            contract_error,
            policy_id,
            policy_version,
            lifecycle,
            state_version,
            state.strategy_mode,
            state.kill_epoch,
            state.executor_generation,
            source_id,
            source_generation,
            source_sequence};
}

[[nodiscard]] ApplyResult apply_publish(TradingState& state,
                                        const PublishPolicy& command,
                                        const std::uint64_t next_version) noexcept {
    const auto contract_error = validate(command.artifact);
    if (contract_error != ContractError::none) {
        return result(state, ApplyCode::invalid_policy_contract,
                      next_version,
                      command.artifact.policy_id,
                      command.artifact.policy_version,
                      PolicyLifecycle::none,
                      contract_error);
    }

    if (const auto* existing = find_policy_mutable(
            state, command.artifact.policy_id, command.artifact.policy_version)) {
        const auto code = existing->artifact.artifact_digest ==
                                  command.artifact.artifact_digest
                              ? ApplyCode::already_applied
                              : ApplyCode::immutable_policy_conflict;
        return result(state, code,
                      next_version,
                      command.artifact.policy_id,
                      command.artifact.policy_version,
                      existing->lifecycle);
    }

    for (std::size_t index = 0; index < state.policy_count; ++index) {
        const auto& existing = state.policies[index].artifact;
        if (existing.policy_id == command.artifact.policy_id &&
            existing.policy_version.value() >= command.artifact.policy_version.value()) {
            return result(state, ApplyCode::non_monotonic_policy_version,
                          next_version,
                          command.artifact.policy_id,
                          command.artifact.policy_version);
        }
    }

    if (state.policy_count == state.policies.size()) {
        return result(state, ApplyCode::policy_capacity_exceeded,
                      next_version,
                      command.artifact.policy_id,
                      command.artifact.policy_version);
    }

    state.policies[state.policy_count] =
        PolicyRecord{command.artifact, PolicyLifecycle::proposed};
    ++state.policy_count;
    return result(state, ApplyCode::applied,
                  next_version,
                  command.artifact.policy_id,
                  command.artifact.policy_version,
                  PolicyLifecycle::proposed);
}

[[nodiscard]] ApplyResult apply_validate(TradingState& state,
                                         const ValidatePolicy& command,
                                         const std::uint64_t next_version) noexcept {
    if (!present(command.policy_id, command.policy_version)) {
        return result(state, ApplyCode::invalid_command,
                      next_version,
                      command.policy_id,
                      command.policy_version,
                      PolicyLifecycle::none,
                      ContractError::invalid_identity);
    }
    auto* record = find_policy_mutable(state, command.policy_id, command.policy_version);
    if (record == nullptr) {
        return result(state, ApplyCode::policy_not_found,
                      next_version,
                      command.policy_id,
                      command.policy_version);
    }
    if (record->lifecycle == PolicyLifecycle::validated) {
        return result(state, ApplyCode::already_applied,
                      next_version,
                      command.policy_id,
                      command.policy_version,
                      record->lifecycle);
    }
    if (record->lifecycle != PolicyLifecycle::proposed) {
        return result(state, ApplyCode::invalid_policy_transition,
                      next_version,
                      command.policy_id,
                      command.policy_version,
                      record->lifecycle);
    }
    record->lifecycle = PolicyLifecycle::validated;
    return result(state, ApplyCode::applied,
                  next_version,
                  command.policy_id,
                  command.policy_version,
                  record->lifecycle);
}

[[nodiscard]] ApplyResult apply_activate(TradingState& state,
                                         const ActivatePolicy& command,
                                         const std::uint64_t next_version) noexcept {
    if (!present(command.policy_id, command.policy_version)) {
        return result(state, ApplyCode::invalid_command,
                      next_version,
                      command.policy_id,
                      command.policy_version,
                      PolicyLifecycle::none,
                      ContractError::invalid_identity);
    }
    auto* record = find_policy_mutable(state, command.policy_id, command.policy_version);
    if (record == nullptr) {
        return result(state, ApplyCode::policy_not_found,
                      next_version,
                      command.policy_id,
                      command.policy_version);
    }
    if (record->lifecycle == PolicyLifecycle::active) {
        return result(state, ApplyCode::already_applied,
                      next_version,
                      command.policy_id,
                      command.policy_version,
                      record->lifecycle);
    }
    if (record->lifecycle != PolicyLifecycle::validated) {
        return result(state, ApplyCode::invalid_policy_transition,
                      next_version,
                      command.policy_id,
                      command.policy_version,
                      record->lifecycle);
    }
    for (std::size_t index = 0; index < state.policy_count; ++index) {
        const auto& candidate = state.policies[index];
        if (candidate.lifecycle == PolicyLifecycle::active &&
            candidate.artifact.account_id == record->artifact.account_id &&
            candidate.artifact.instrument_id == record->artifact.instrument_id) {
            return result(state, ApplyCode::active_policy_scope_conflict,
                          next_version,
                          command.policy_id,
                          command.policy_version,
                          record->lifecycle);
        }
    }
    record->lifecycle = PolicyLifecycle::active;
    return result(state, ApplyCode::applied,
                  next_version,
                  command.policy_id,
                  command.policy_version,
                  record->lifecycle);
}

[[nodiscard]] ApplyResult apply_supersede(TradingState& state,
                                          const SupersedePolicy& command,
                                          const std::uint64_t next_version) noexcept {
    if (!present(command.policy_id, command.policy_version)) {
        return result(state, ApplyCode::invalid_command,
                      next_version,
                      command.policy_id,
                      command.policy_version,
                      PolicyLifecycle::none,
                      ContractError::invalid_identity);
    }
    auto* record = find_policy_mutable(state, command.policy_id, command.policy_version);
    if (record == nullptr) {
        return result(state, ApplyCode::policy_not_found,
                      next_version,
                      command.policy_id,
                      command.policy_version);
    }
    if (record->lifecycle == PolicyLifecycle::superseded) {
        return result(state, ApplyCode::already_applied,
                      next_version,
                      command.policy_id,
                      command.policy_version,
                      record->lifecycle);
    }
    if (record->lifecycle != PolicyLifecycle::active) {
        return result(state, ApplyCode::invalid_policy_transition,
                      next_version,
                      command.policy_id,
                      command.policy_version,
                      record->lifecycle);
    }
    record->lifecycle = PolicyLifecycle::superseded;
    return result(state, ApplyCode::applied,
                  next_version,
                  command.policy_id,
                  command.policy_version,
                  record->lifecycle);
}

[[nodiscard]] ApplyResult apply_set_strategy_mode(
    TradingState& state,
    const SetStrategyMode& command,
    const std::uint64_t next_version) noexcept {
    if (!known(command.mode)) {
        return result(state,
                      ApplyCode::invalid_command,
                      next_version,
                      {},
                      {},
                      PolicyLifecycle::none,
                      ContractError::invalid_enum);
    }
    if (command.expected_kill_epoch != state.kill_epoch ||
        command.expected_executor_generation != state.executor_generation) {
        return result(state, ApplyCode::stale_control_generation, next_version);
    }
    if (command.mode == state.strategy_mode) {
        return result(state, ApplyCode::already_applied, next_version);
    }

    state.strategy_mode = command.mode;
    return result(state, ApplyCode::applied, next_version);
}

[[nodiscard]] ApplyResult apply_advance_kill_epoch(
    TradingState& state,
    const AdvanceKillEpoch& command,
    const std::uint64_t next_version) noexcept {
    if (command.new_kill_epoch.value() <= state.kill_epoch.value()) {
        return result(state, ApplyCode::non_monotonic_kill_epoch, next_version);
    }

    state.kill_epoch = command.new_kill_epoch;
    state.strategy_mode = StrategyMode::halted;
    return result(state, ApplyCode::applied, next_version);
}

[[nodiscard]] ApplyResult apply_set_executor_generation(
    TradingState& state,
    const SetExecutorGeneration& command,
    const std::uint64_t next_version) noexcept {
    if (command.new_generation.value() <= state.executor_generation.value()) {
        return result(state,
                      ApplyCode::non_monotonic_executor_generation,
                      next_version);
    }

    state.executor_generation = command.new_generation;
    if (state.strategy_mode != StrategyMode::halted &&
        state.strategy_mode != StrategyMode::cancel_only) {
        state.strategy_mode = StrategyMode::no_new_risk;
    }
    return result(state, ApplyCode::applied, next_version);
}

[[nodiscard]] ApplyResult source_result(
    const TradingState& state,
    const ApplyCode code,
    const std::uint64_t next_version,
    const RecordSourceEvent& command,
    const ContractError contract_error = ContractError::none) noexcept {
    return result(state,
                  code,
                  next_version,
                  {},
                  {},
                  PolicyLifecycle::none,
                  contract_error,
                  command.source_id,
                  command.source_generation,
                  command.source_sequence);
}

[[nodiscard]] ApplyResult apply_record_source_event(
    TradingState& state,
    const RecordSourceEvent& command,
    const std::uint64_t next_version) noexcept {
    if (command.source_id.value() == 0 || command.source_generation.value() == 0 ||
        command.source_sequence.value() == 0) {
        return source_result(state,
                             ApplyCode::invalid_command,
                             next_version,
                             command,
                             ContractError::invalid_identity);
    }
    if (command.event_digest.is_zero()) {
        return source_result(state,
                             ApplyCode::invalid_command,
                             next_version,
                             command,
                             ContractError::missing_digest);
    }

    if (const auto* existing = find_source_event(state,
                                                 command.source_id,
                                                 command.source_generation,
                                                 command.source_sequence)) {
        return source_result(state,
                             existing->event_digest == command.event_digest
                                 ? ApplyCode::already_applied
                                 : ApplyCode::source_event_conflict,
                             next_version,
                             command);
    }

    auto* cursor = find_source_cursor_mutable(state, command.source_id);
    if (cursor == nullptr) {
        if (command.source_generation.value() != 1) {
            return source_result(
                state, ApplyCode::source_generation_gap, next_version, command);
        }
        if (command.source_sequence.value() != 1) {
            return source_result(
                state, ApplyCode::source_sequence_gap, next_version, command);
        }
    } else if (command.source_generation.value() < cursor->generation.value()) {
        return source_result(
            state, ApplyCode::source_generation_regression, next_version, command);
    } else if (command.source_generation == cursor->generation) {
        if (command.source_sequence.value() <= cursor->sequence.value()) {
            return source_result(
                state, ApplyCode::source_sequence_regression, next_version, command);
        }
        if (command.source_sequence.value() != cursor->sequence.value() + 1) {
            return source_result(
                state, ApplyCode::source_sequence_gap, next_version, command);
        }
    } else {
        if (command.source_generation.value() != cursor->generation.value() + 1) {
            return source_result(
                state, ApplyCode::source_generation_gap, next_version, command);
        }
        if (command.source_sequence.value() != 1) {
            return source_result(
                state, ApplyCode::source_sequence_gap, next_version, command);
        }
    }

    if (state.source_event_count == state.source_events.size()) {
        return source_result(
            state, ApplyCode::source_event_capacity_exceeded, next_version, command);
    }
    if (cursor == nullptr && state.source_count == state.sources.size()) {
        return source_result(
            state, ApplyCode::source_capacity_exceeded, next_version, command);
    }

    if (cursor == nullptr) {
        state.sources[state.source_count] = SourceCursor{command.source_id,
                                                        command.source_generation,
                                                        command.source_sequence};
        cursor = &state.sources[state.source_count];
        ++state.source_count;
    } else {
        cursor->generation = command.source_generation;
        cursor->sequence = command.source_sequence;
    }
    state.source_events[state.source_event_count] =
        SourceEventReceipt{command.source_id,
                           command.source_generation,
                           command.source_sequence,
                           command.event_digest};
    ++state.source_event_count;
    return source_result(state, ApplyCode::applied, next_version, command);
}

[[nodiscard]] ApplyResult decision_result(
    const TradingState& state,
    const ApplyCode apply_code,
    const std::uint64_t next_version,
    const EvaluateDecisionFrame& command,
    const DecisionCode decision_code,
    const ContractError contract_error,
    const Directive proposed,
    const Directive effective,
    const std::uint16_t matched_rule_index,
    const std::uint16_t rules_visited) noexcept {
    auto value = result(state, apply_code, next_version,
                        command.frame.policy_id, command.frame.policy_version,
                        PolicyLifecycle::none, contract_error,
                        command.frame.source_id, command.frame.source_generation,
                        command.frame.source_sequence);
    value.decision_id = command.decision_id;
    value.decision_code = decision_code;
    value.proposed_directive = proposed;
    value.effective_directive = effective;
    value.evaluation_mode = command.evaluation_mode;
    value.matched_rule_index = matched_rule_index;
    value.rules_visited = rules_visited;
    return value;
}

[[nodiscard]] ApplyResult apply_evaluate_decision(
    TradingState& state,
    const CommandIdentity& identity,
    const EvaluateDecisionFrame& command,
    const std::uint64_t next_version) noexcept {
    constexpr auto no_rule = std::numeric_limits<std::uint16_t>::max();
    if (command.decision_id.value() == 0 || !known(command.evaluation_mode)) {
        return decision_result(state, ApplyCode::invalid_command, next_version,
                               command, DecisionCode::none,
                               command.decision_id.value() == 0
                                   ? ContractError::invalid_identity
                                   : ContractError::invalid_enum,
                               Directive::no_action, Directive::no_action,
                               no_rule, 0);
    }

    const auto existing_index = find_decision_index(state, command.decision_id);
    if (existing_index != state.decision_count) {
        const auto& hot = state.decision_hot[existing_index];
        if (state.decision_cold[existing_index].command_payload_digest !=
            identity.payload_digest) {
            return decision_result(state, ApplyCode::decision_id_conflict,
                                   next_version, command, DecisionCode::none,
                                   ContractError::none, Directive::no_action,
                                   Directive::no_action, no_rule, 0);
        }
        return decision_result(state, ApplyCode::already_applied, next_version,
                               command, hot.code, hot.contract_error,
                               hot.proposed_directive, hot.effective_directive,
                               hot.matched_rule_index, hot.rules_visited);
    }
    if (state.decision_count == state.decision_hot.size()) {
        return decision_result(state, ApplyCode::decision_capacity_exceeded,
                               next_version, command, DecisionCode::none,
                               ContractError::none, Directive::no_action,
                               Directive::no_action, no_rule, 0);
    }

    DecisionCode code = DecisionCode::none;
    ContractError contract_error = validate(command.frame);
    Directive proposed = Directive::no_action;
    Directive effective = Directive::no_action;
    std::uint16_t matched = no_rule;
    std::uint16_t visited = 0;
    const PolicyRecord* policy_record = nullptr;

    if (contract_error != ContractError::none) {
        code = DecisionCode::invalid_frame;
    } else if ((policy_record = find_policy(state, command.frame.policy_id,
                                             command.frame.policy_version)) == nullptr) {
        code = DecisionCode::policy_not_found;
    } else if (policy_record->lifecycle != PolicyLifecycle::active) {
        code = DecisionCode::policy_not_active;
    } else if (policy_record->artifact.account_id != command.frame.account_id) {
        code = DecisionCode::account_scope_mismatch;
    } else if (policy_record->artifact.instrument_id != command.frame.instrument_id) {
        code = DecisionCode::instrument_scope_mismatch;
    } else if (policy_record->artifact.feature_schema_id !=
               command.frame.feature_schema_id) {
        code = DecisionCode::feature_schema_mismatch;
    } else if (state.strategy_mode == StrategyMode::halted) {
        code = DecisionCode::control_halted;
    } else {
        const auto* source = find_source_event(state, command.frame.source_id,
                                                command.frame.source_generation,
                                                command.frame.source_sequence);
        if (source == nullptr) {
            code = DecisionCode::source_not_admitted;
        } else if (source->event_digest != command.frame.raw_evidence_digest) {
            code = DecisionCode::source_evidence_mismatch;
        } else if (command.frame.event_time_ns <
                   policy_record->artifact.valid_from_event_time_ns) {
            code = DecisionCode::policy_not_yet_valid;
        } else if (command.frame.event_time_ns >
                   policy_record->artifact.valid_until_event_time_ns) {
            code = DecisionCode::policy_expired;
        } else if (command.frame.market_data_status != MarketDataStatus::ready) {
            code = DecisionCode::market_data_not_ready;
        } else {
            const auto evaluation = evaluate_rules(policy_record->artifact, command.frame);
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
            } else if (command.evaluation_mode == EvaluationMode::shadow) {
                code = DecisionCode::shadow_recorded;
            } else if ((state.strategy_mode == StrategyMode::no_new_risk ||
                        state.strategy_mode == StrategyMode::cancel_only ||
                        state.strategy_mode == StrategyMode::reduce_only) &&
                       proposed != Directive::cancel &&
                       proposed != Directive::no_action) {
                code = DecisionCode::control_suppressed;
            } else {
                code = DecisionCode::action;
                effective = proposed;
            }
        }
    }

    const auto outcome = decision_result(state, ApplyCode::applied, next_version,
                                         command, code, contract_error, proposed,
                                         effective, matched, visited);
    const auto index = state.decision_count;
    state.decision_hot[index] = DecisionRecordHot{
        command.decision_id, command.frame.policy_id, command.frame.policy_version,
        next_version, state.kill_epoch, state.executor_generation, matched, visited,
        code, contract_error, proposed, effective, command.evaluation_mode,
        state.strategy_mode};
    state.decision_cold[index] = DecisionRecordCold{
        command.decision_id, identity.payload_digest, command.frame};
    ++state.decision_count;
    return outcome;
}

[[nodiscard]] ApplyResult apply_new_command(TradingState& state,
                                            const Command& command,
                                            const std::uint64_t next_version) noexcept {
    if (const auto* body = std::get_if<PublishPolicy>(&command.body)) {
        return apply_publish(state, *body, next_version);
    }
    if (const auto* body = std::get_if<ValidatePolicy>(&command.body)) {
        return apply_validate(state, *body, next_version);
    }
    if (const auto* body = std::get_if<ActivatePolicy>(&command.body)) {
        return apply_activate(state, *body, next_version);
    }
    if (const auto* body = std::get_if<SupersedePolicy>(&command.body)) {
        return apply_supersede(state, *body, next_version);
    }
    if (const auto* body = std::get_if<SetStrategyMode>(&command.body)) {
        return apply_set_strategy_mode(state, *body, next_version);
    }
    if (const auto* body = std::get_if<AdvanceKillEpoch>(&command.body)) {
        return apply_advance_kill_epoch(state, *body, next_version);
    }
    if (const auto* body = std::get_if<SetExecutorGeneration>(&command.body)) {
        return apply_set_executor_generation(state, *body, next_version);
    }
    if (const auto* body = std::get_if<RecordSourceEvent>(&command.body)) {
        return apply_record_source_event(state, *body, next_version);
    }
    if (const auto* body = std::get_if<EvaluateDecisionFrame>(&command.body)) {
        return apply_evaluate_decision(state, command.identity, *body, next_version);
    }
    return result(state, ApplyCode::invalid_command, next_version);
}

}  // namespace

const PolicyRecord* find_policy(const TradingState& state,
                                const PolicyId policy_id,
                                const PolicyVersion policy_version) noexcept {
    for (std::size_t index = 0; index < state.policy_count; ++index) {
        const auto& record = state.policies[index];
        if (record.artifact.policy_id == policy_id &&
            record.artifact.policy_version == policy_version) {
            return &record;
        }
    }
    return nullptr;
}

const SourceCursor* find_source_cursor(const TradingState& state,
                                       const SourceId source_id) noexcept {
    for (std::size_t index = 0; index < state.source_count; ++index) {
        if (state.sources[index].source_id == source_id) {
            return &state.sources[index];
        }
    }
    return nullptr;
}

ApplyResult apply(TradingState& state,
                  const Command& command,
                  ApplyScratch&) noexcept {
    if (!present(command.identity)) {
        return result(state, ApplyCode::invalid_command,
                      state.state_version,
                      {},
                      {},
                      PolicyLifecycle::none,
                      command.identity.caller_id.value() == 0 ||
                              command.identity.request_sequence.value() == 0
                          ? ContractError::invalid_identity
                          : ContractError::missing_digest);
    }

    if (const auto* receipt = find_receipt(state, command.identity)) {
        if (receipt->identity.payload_digest == command.identity.payload_digest) {
            return receipt->result;
        }
        return result(state, ApplyCode::duplicate_conflict, state.state_version);
    }

    if (state.receipt_count == state.receipts.size()) {
        return result(state, ApplyCode::receipt_capacity_exceeded, state.state_version);
    }
    if (state.state_version == std::numeric_limits<std::uint64_t>::max()) {
        return result(state, ApplyCode::state_version_exhausted, state.state_version);
    }

    const auto next_version = state.state_version + 1;
    const auto outcome = apply_new_command(state, command, next_version);

    state.state_version = next_version;
    state.receipts[state.receipt_count] = RequestReceipt{command.identity, outcome};
    ++state.receipt_count;
    return outcome;
}

}  // namespace velocita::v2
