#include "velocita/v2/kernel.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/rule_evaluator.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

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

[[nodiscard]] bool same_executable_identity(
    const ExecutablePolicyBundle& bundle,
    const PolicyId policy_id,
    const PolicyVersion policy_version,
    const Digest256& digest) noexcept {
    return bundle.present && bundle.executable_policy.policy_id == policy_id &&
           bundle.executable_policy.policy_version == policy_version &&
           bundle.executable_policy.artifact_digest == digest;
}

[[nodiscard]] bool same_scope(const PolicyArtifact& left,
                              const PolicyArtifact& right) noexcept {
    return left.account_id == right.account_id &&
           left.instrument_id == right.instrument_id;
}

[[nodiscard]] bool same_executable_bundle_contents(
    const ExecutablePolicyBundle& bundle,
    const StageExecutablePolicy& command) noexcept {
    return bundle.present && bundle.feature_schema == command.feature_schema &&
           bundle.executable_policy == command.executable_policy &&
           bundle.instrument == command.instrument && bundle.venue == command.venue;
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

[[nodiscard]] RiskAccount* find_risk_account(
    TradingState& state,
    const AccountId account_id,
    const InstrumentId instrument_id) noexcept {
    for (std::size_t index = 0; index < state.risk_account_count; ++index) {
        auto& account = state.risk_accounts[index];
        if (account.account_id == account_id &&
            account.instrument_id == instrument_id) {
            return &account;
        }
    }
    return nullptr;
}

[[nodiscard]] std::size_t find_order_by_decision(
    const TradingState& state, const DecisionId decision_id) noexcept {
    for (std::size_t index = 0; index < state.order_count; ++index) {
        if (state.orders[index].decision_id == decision_id) return index;
    }
    return state.order_count;
}

[[nodiscard]] std::size_t find_order_by_effect(
    const TradingState& state,
    const ClusterOrderId cluster_order_id,
    const EffectId effect_id) noexcept {
    bool effect_found = false;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        effect_found = effect_found ||
                       (state.outbox_effects[index].cluster_order_id ==
                            cluster_order_id &&
                        state.outbox_effects[index].effect_id == effect_id);
    }
    if (!effect_found) return state.order_count;
    for (std::size_t index = 0; index < state.order_count; ++index) {
        if (state.orders[index].cluster_order_id == cluster_order_id) return index;
    }
    return state.order_count;
}

[[nodiscard]] const RequestReceipt* find_applied_native_event(
    const TradingState& state,
    const VenueId venue_id,
    const VenueSessionId session_id,
    const VenueSequence sequence) noexcept {
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        const auto& receipt = state.receipts[index];
        const auto& outcome = receipt.result;
        const bool event_matches =
            outcome.detail_kind == ApplyResultDetailKind::venue_event &&
            outcome.event_origin() == ObservationOrigin::native &&
            outcome.event_venue_id() == venue_id &&
            outcome.event_session_id() == session_id &&
            outcome.event_sequence() == sequence;
        const bool fill_matches = outcome.detail_kind == ApplyResultDetailKind::fill &&
            outcome.fill_venue_id() == venue_id &&
            outcome.fill_session_id() == session_id && outcome.fill_sequence() == sequence;
        if (outcome.code == ApplyCode::applied && (event_matches || fill_matches)) {
            return &receipt;
        }
    }
    return nullptr;
}

[[nodiscard]] const RequestReceipt* find_applied_fill(
    const TradingState& state,
    const VenueId venue_id,
    const ExecutionId execution_id) noexcept {
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        const auto& receipt = state.receipts[index];
        if (receipt.result.code == ApplyCode::applied &&
            receipt.result.detail_kind == ApplyResultDetailKind::fill &&
            receipt.result.fill_venue_id() == venue_id &&
            receipt.result.execution_id() == execution_id) {
            return &receipt;
        }
    }
    return nullptr;
}

[[nodiscard]] std::size_t sweep_admitted_source_events(TradingState& state) noexcept;
[[nodiscard]] CallerCursor* find_caller_cursor(TradingState& state,
                                               CallerId caller_id) noexcept;

[[nodiscard]] std::size_t accepted_venue_event_count(
    const TradingState& state) noexcept {
    std::size_t count = 0;
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        const auto& result = state.receipts[index].result;
        count += result.code == ApplyCode::applied &&
                 (result.detail_kind == ApplyResultDetailKind::fill ||
                  result.detail_kind == ApplyResultDetailKind::venue_event);
    }
    return count;
}

struct LedgerAfterFill {
    MoneyMicros cash{};
    MoneyMicros cost_basis{};
    MoneyMicros realized_pnl{};
    MoneyMicros fees_paid{};
    MoneyMicros window_realized_pnl{};
};

[[nodiscard]] std::optional<MoneyMicros> fill_notional(
    const EconomicActionProposal& proposal, const VenueObservation& observation) noexcept {
    constexpr __int128 limit = std::numeric_limits<std::int64_t>::max();
    const __int128 quoted = static_cast<__int128>(proposal.price.atoms()) * proposal.quantity.atoms();
    if (quoted <= 0 || proposal.notional.atoms() <= 0 || proposal.notional.atoms() % quoted != 0 ||
        observation.price.atoms() <= 0 || observation.quantity.atoms() <= 0) {
        return std::nullopt;
    }
    const __int128 per_tick_lot = proposal.notional.atoms() / quoted;
    const __int128 per_lot = per_tick_lot * observation.price.atoms();
    if (per_lot > limit) return std::nullopt;
    const __int128 value = per_lot * observation.quantity.atoms();
    if (value > limit) return std::nullopt;
    return MoneyMicros::from_atoms(static_cast<std::int64_t>(value));
}

[[nodiscard]] std::optional<LedgerAfterFill> ledger_after_fill(
    const RiskAccount& risk, const Side side, const QuantityLots quantity,
    const MoneyMicros notional, const FeeMicros fee) noexcept {
    constexpr __int128 limit = std::numeric_limits<std::int64_t>::max();
    const __int128 position = risk.position_quantity.atoms();
    const __int128 held = position < 0 ? -position : position;
    const __int128 filled = quantity.atoms();
    const __int128 gross = notional.atoms();
    const __int128 charged = fee.atoms();
    const bool buying = side == Side::buy;
    const bool closing = buying ? position < 0 : position > 0;
    const __int128 closed = closing ? std::min(filled, held) : 0;
    const __int128 closed_gross = gross * closed / filled;
    const __int128 basis = risk.cost_basis.atoms();
    const __int128 basis_out = closed == 0 ? 0 : basis * closed / held;
    const __int128 traded = closed == 0 ? 0 : position > 0 ? closed_gross - basis_out : basis_out - closed_gross;
    const __int128 pnl = traded - charged;
    const __int128 values[5] = {
        risk.cash.atoms() + (buying ? -(gross + charged) : gross - charged),
        basis - basis_out + (gross - closed_gross),
        risk.realized_pnl.atoms() + pnl,
        risk.fees_paid.atoms() + charged,
        risk.window_realized_pnl.atoms() + pnl,
    };
    for (const auto value : values) {
        if (value > limit || value < -limit) return std::nullopt;
    }
    const auto money = [](const __int128 value) { return MoneyMicros::from_atoms(static_cast<std::int64_t>(value)); };
    return LedgerAfterFill{money(values[0]), money(values[1]), money(values[2]), money(values[3]), money(values[4])};
}

[[nodiscard]] std::optional<MoneyMicros> ceil_rate_hold(
    const MoneyMicros notional, const std::uint32_t rate_ppm) noexcept {
    constexpr std::int64_t scale = 1'000'000;
    if (notional.atoms() <= 0) return std::nullopt;
    const auto whole = MoneyMicros::from_atoms(notional.atoms() / scale)
                           .checked_multiply(static_cast<std::int64_t>(rate_ppm));
    if (!whole) return std::nullopt;
    const auto remainder = notional.atoms() % scale;
    const auto remainder_product =
        MoneyMicros::from_atoms(remainder).checked_multiply(
            static_cast<std::int64_t>(rate_ppm));
    if (!remainder_product) return std::nullopt;
    const auto rounded_remainder = MoneyMicros::from_atoms(
        remainder_product->atoms() == 0
            ? 0
            : 1 + (remainder_product->atoms() - 1) / scale);
    return whole->checked_add(rounded_remainder);
}

[[nodiscard]] std::optional<MoneyMicros> proportional_remaining_hold(
    const MoneyMicros current,
    const QuantityLots next_remaining,
    const QuantityLots current_remaining) noexcept {
    if (current.atoms() < 0 || next_remaining.atoms() < 0 ||
        current_remaining.atoms() <= 0 ||
        next_remaining > current_remaining) {
        return std::nullopt;
    }
    if (next_remaining.atoms() == 0 || current.atoms() == 0) {
        return MoneyMicros{};
    }
    const auto denominator = current_remaining.atoms();
    const auto numerator = next_remaining.atoms();
    const auto whole = MoneyMicros::from_atoms(current.atoms() / denominator)
                           .checked_multiply(numerator);
    const auto remainder = current.atoms() % denominator;
    const auto remainder_product =
        MoneyMicros::from_atoms(remainder).checked_multiply(numerator);
    if (!whole || !remainder_product) return std::nullopt;
    const auto rounded_remainder = MoneyMicros::from_atoms(
        remainder_product->atoms() == 0
            ? 0
            : 1 + (remainder_product->atoms() - 1) / denominator);
    return whole->checked_add(rounded_remainder);
}

template <std::size_t WordCount>
[[nodiscard]] Digest256 domain_digest(
    const std::string_view domain,
    const std::array<std::uint64_t, WordCount>& words) noexcept {
    std::array<std::byte, 192> preimage{};
    if (domain.size() + WordCount * sizeof(std::uint64_t) > preimage.size()) {
        return {};
    }
    std::size_t offset = 0;
    for (const char value : domain) {
        preimage[offset++] = static_cast<std::byte>(value);
    }
    for (const auto word : words) {
        for (std::size_t byte = 0; byte < sizeof(word); ++byte) {
            preimage[offset++] = static_cast<std::byte>(
                (word >> (byte * 8U)) & std::uint64_t{0xff});
        }
    }
    return sha256_digest(std::span<const std::byte>{preimage}.first(offset));
}

[[nodiscard]] std::uint64_t low_u64(const Digest256& digest) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint64_t>(digest.bytes[index]) << (index * 8U);
    }
    return value;
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
    ApplyResult value;
    value.code = code;
    value.contract_error = contract_error;
    value.metadata_words = {policy_id.value(), policy_version.value()};
    value.policy_lifecycle = lifecycle;
    value.state_version = state_version;
    value.strategy_mode = state.strategy_mode;
    value.kill_epoch = state.kill_epoch;
    value.executor_generation = state.executor_generation;
    if (source_id.value() != 0 || source_generation.value() != 0 ||
        source_sequence.value() != 0) {
        value.detail = ApplyResultDetail::source(
            source_id, source_generation, source_sequence);
        value.detail_kind = ApplyResultDetailKind::source;
    }
    return value;
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

[[nodiscard]] ApplyResult apply_stage_executable_policy(
    TradingState& state,
    const StageExecutablePolicy& command,
    const std::uint64_t next_version,
    const VerifiedExecutionCatalog& catalog) noexcept {
    const auto policy_id = command.executable_policy.policy_id;
    const auto policy_version = command.executable_policy.policy_version;
    const auto schema_digest = verify_artifact_digest(command.feature_schema);
    if (schema_digest.error != ArtifactDigestError::none) {
        return result(state,
                      schema_digest.error == ArtifactDigestError::digest_mismatch
                          ? ApplyCode::artifact_digest_mismatch
                          : ApplyCode::invalid_feature_schema_contract,
                      next_version, policy_id, policy_version,
                      PolicyLifecycle::none, schema_digest.contract_error);
    }
    const auto executable_digest =
        verify_artifact_digest(command.executable_policy);
    if (executable_digest.error != ArtifactDigestError::none) {
        return result(state,
                      executable_digest.error == ArtifactDigestError::digest_mismatch
                          ? ApplyCode::artifact_digest_mismatch
                          : ApplyCode::invalid_executable_policy_contract,
                      next_version, policy_id, policy_version,
                      PolicyLifecycle::none, executable_digest.contract_error);
    }
    if (const auto error = validate(command.instrument);
        error != ContractError::none) {
        return result(state, ApplyCode::invalid_instrument_profile_contract,
                      next_version, policy_id, policy_version,
                      PolicyLifecycle::none, error);
    }
    if (const auto error = validate(command.venue);
        error != ContractError::none) {
        return result(state, ApplyCode::invalid_venue_capabilities_contract,
                      next_version, policy_id, policy_version,
                      PolicyLifecycle::none, error);
    }

    auto* policy = find_policy_mutable(state, policy_id, policy_version);
    if (policy == nullptr) {
        return result(state, ApplyCode::policy_not_found, next_version,
                      policy_id, policy_version);
    }
    if (policy->lifecycle != PolicyLifecycle::validated &&
        policy->lifecycle != PolicyLifecycle::active) {
        return result(state, ApplyCode::invalid_policy_transition, next_version,
                      policy_id, policy_version, policy->lifecycle);
    }
    if (bind_executable_policy(policy->artifact, command.feature_schema,
                               command.executable_policy) !=
        ExecutablePolicyBindingCode::bound) {
        return result(state, ApplyCode::executable_policy_binding_failed,
                      next_version, policy_id, policy_version, policy->lifecycle);
    }
    if (command.instrument.instrument_id != policy->artifact.instrument_id ||
        command.instrument.venue_id != command.venue.venue_id) {
        return result(state, ApplyCode::executable_policy_binding_failed,
                      next_version, policy_id, policy_version, policy->lifecycle);
    }
    for (std::size_t index = 0;
         index < command.executable_policy.template_count; ++index) {
        const auto& action = command.executable_policy.templates[index];
        if (action.instrument_spec_version !=
                command.instrument.instrument_spec_version ||
            action.venue_capabilities_version !=
                command.venue.venue_capabilities_version) {
            return result(state, ApplyCode::executable_policy_binding_failed,
                          next_version, policy_id, policy_version,
                          policy->lifecycle);
        }
    }

    const auto* row = catalog.lookup(policy->artifact.account_id,
        command.instrument.venue_id, command.instrument.instrument_id,
        command.instrument.instrument_spec_version,
        command.venue.venue_capabilities_version);
    if (row == nullptr) {
        return result(state, ApplyCode::catalog_row_not_found, next_version,
                      policy_id, policy_version, policy->lifecycle);
    }
    if (row->instrument != command.instrument || row->capabilities != command.venue) {
        return result(state, ApplyCode::catalog_binding_mismatch, next_version,
                      policy_id, policy_version, policy->lifecycle);
    }

    if (same_executable_bundle_contents(state.active_executable_policy, command) ||
        same_executable_bundle_contents(state.staged_executable_policy, command)) {
        return result(state, ApplyCode::already_applied, next_version,
                      policy_id, policy_version, policy->lifecycle);
    }
    if (same_executable_identity(state.active_executable_policy, policy_id,
                                 policy_version,
                                 command.executable_policy.artifact_digest) ||
        same_executable_identity(state.staged_executable_policy, policy_id,
                                 policy_version,
                                 command.executable_policy.artifact_digest) ||
        (state.staged_executable_policy.present &&
         state.staged_executable_policy.executable_policy.policy_id == policy_id &&
         state.staged_executable_policy.executable_policy.policy_version ==
             policy_version)) {
        return result(state, ApplyCode::executable_policy_identity_conflict,
                      next_version, policy_id, policy_version, policy->lifecycle);
    }
    if (state.active_executable_policy.present) {
        const auto* active_policy = find_policy(
            state, state.active_executable_policy.executable_policy.policy_id,
            state.active_executable_policy.executable_policy.policy_version);
        if (active_policy != nullptr && !same_scope(active_policy->artifact,
                                                    policy->artifact)) {
            return result(state, ApplyCode::active_policy_scope_conflict,
                          next_version, policy_id, policy_version,
                          policy->lifecycle);
        }
        if (active_policy != nullptr &&
            active_policy->artifact.policy_id == policy_id &&
            active_policy->artifact.policy_version.value() >=
                policy_version.value()) {
            return result(state, ApplyCode::non_monotonic_policy_version,
                          next_version, policy_id, policy_version,
                          policy->lifecycle);
        }
    }

    state.staged_executable_policy = ExecutablePolicyBundle{
        true, command.feature_schema, command.executable_policy,
        command.instrument, command.venue, next_version, 0};
    return result(state, ApplyCode::applied, next_version, policy_id,
                  policy_version, policy->lifecycle);
}

[[nodiscard]] ApplyResult apply_activate_executable_policy(
    TradingState& state,
    const ActivateExecutablePolicy& command,
    const std::uint64_t next_version) noexcept {
    if (!present(command.policy_id, command.policy_version) ||
        command.executable_policy_digest.is_zero()) {
        return result(state, ApplyCode::invalid_command, next_version,
                      command.policy_id, command.policy_version,
                      PolicyLifecycle::none,
                      command.executable_policy_digest.is_zero()
                          ? ContractError::missing_digest
                          : ContractError::invalid_identity);
    }
    if (same_executable_identity(state.active_executable_policy,
                                 command.policy_id, command.policy_version,
                                 command.executable_policy_digest)) {
        return result(state, ApplyCode::already_applied, next_version,
                      command.policy_id, command.policy_version,
                      PolicyLifecycle::active);
    }
    if (!same_executable_identity(state.staged_executable_policy,
                                  command.policy_id, command.policy_version,
                                  command.executable_policy_digest)) {
        return result(state, ApplyCode::executable_policy_not_staged,
                      next_version, command.policy_id, command.policy_version);
    }
    auto* policy = find_policy_mutable(state, command.policy_id,
                                       command.policy_version);
    if (policy == nullptr) {
        return result(state, ApplyCode::policy_not_found, next_version,
                      command.policy_id, command.policy_version);
    }
    if (policy->lifecycle != PolicyLifecycle::validated &&
        policy->lifecycle != PolicyLifecycle::active) {
        return result(state, ApplyCode::invalid_policy_transition, next_version,
                      command.policy_id, command.policy_version,
                      policy->lifecycle);
    }

    if (state.active_executable_policy.present) {
        auto* old = find_policy_mutable(
            state, state.active_executable_policy.executable_policy.policy_id,
            state.active_executable_policy.executable_policy.policy_version);
        if (old != nullptr && old != policy) old->lifecycle = PolicyLifecycle::superseded;
    }
    policy->lifecycle = PolicyLifecycle::active;
    state.active_executable_policy = state.staged_executable_policy;
    state.active_executable_policy.activated_state_version = next_version;
    state.staged_executable_policy = {};
    return result(state, ApplyCode::applied, next_version, command.policy_id,
                  command.policy_version, PolicyLifecycle::active);
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
    if (state.active_executable_policy.present &&
        state.active_executable_policy.executable_policy.policy_id ==
            command.policy_id &&
        state.active_executable_policy.executable_policy.policy_version ==
            command.policy_version) {
        state.active_executable_policy = {};
    }
    if (state.staged_executable_policy.present &&
        state.staged_executable_policy.executable_policy.policy_id ==
            command.policy_id &&
        state.staged_executable_policy.executable_policy.policy_version ==
            command.policy_version) {
        state.staged_executable_policy = {};
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

    if (command.mode == StrategyMode::normal) {
        for (std::size_t index = 0; index < state.order_count; ++index) {
            const auto order_state = state.orders[index].state;
            if (order_state == ClusterOrderState::submission_unknown ||
                order_state == ClusterOrderState::reconciling ||
                order_state == ClusterOrderState::conflict) {
                return result(state, ApplyCode::reconciliation_required, next_version);
            }
        }
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

[[nodiscard]] ApplyResult source_generation_gap(
    const TradingState& state,
    const std::uint64_t next_version,
    const RecordSourceEvent& command,
    const SourceCursor* cursor) noexcept {
    auto value = result(state, ApplyCode::source_generation_gap, next_version);
    value.detail = ApplyResultDetail::source_state(
        command.source_id,
        cursor != nullptr ? cursor->generation : SourceGeneration{},
        cursor != nullptr ? cursor->sequence : SourceSequence{});
    value.detail_kind = ApplyResultDetailKind::source_state;
    return value;
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
            return source_generation_gap(state, next_version, command, cursor);
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
            return source_generation_gap(state, next_version, command, cursor);
        }
        if (command.source_sequence.value() != 1) {
            return source_result(
                state, ApplyCode::source_sequence_gap, next_version, command);
        }
    }

    if (state.source_event_count == state.source_events.size()) {
        (void)sweep_admitted_source_events(state);
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
    const std::uint16_t rules_visited,
    const ActionResolutionCode resolution_code =
        ActionResolutionCode::none) noexcept {
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
    value.action_resolution_code = resolution_code;
    return value;
}

[[nodiscard]] constexpr bool effect_is_settled(const OutboxState state) noexcept {
    return state == OutboxState::acknowledged || state == OutboxState::rejected ||
           state == OutboxState::conflict || state == OutboxState::incomplete;
}

[[nodiscard]] bool order_is_risk_resolved(const TradingState& state,
                                          const ClusterOrder& order) noexcept {
    const bool terminal = order.state == ClusterOrderState::filled ||
                          order.state == ClusterOrderState::cancelled ||
                          order.state == ClusterOrderState::rejected;
    if (!terminal) return false;
    if (order.reservation_index >= state.reservation_count) return false;
    const auto& reservation = state.reservations[order.reservation_index];
    return reservation.state == ReservationState::released &&
           reservation.remaining_quantity.atoms() == 0 &&
           reservation.remaining_notional.atoms() == 0 &&
           reservation.margin_hold.atoms() == 0 && reservation.fee_hold.atoms() == 0;
}

[[nodiscard]] std::size_t future_venue_events_for_order(
    const TradingState& state, const ClusterOrder& order) noexcept {
    if (order.reservation_index >= state.reservation_count) {
        return kMaxVenueEventReceipts + 1;
    }
    const auto& reservation = state.reservations[order.reservation_index];
    if (reservation.state == ReservationState::released) return 0;
    if (reservation.remaining_quantity.atoms() < 0 ||
        static_cast<std::uint64_t>(reservation.remaining_quantity.atoms()) >
            kMaxVenueEventReceipts) {
        return kMaxVenueEventReceipts + 1;
    }

    const OutboxEffectRecord* current_effect = nullptr;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].effect_id == order.effect_id) {
            current_effect = &state.outbox_effects[index];
            break;
        }
    }

    std::size_t control_events = 0;
    switch (order.state) {
        case ClusterOrderState::dispatch_authorized:
            control_events = 5;
            break;
        case ClusterOrderState::acknowledged:
        case ClusterOrderState::live:
        case ClusterOrderState::partially_filled:
            control_events = 3;
            break;
        case ClusterOrderState::cancel_pending:
            control_events = 3;
            break;
        case ClusterOrderState::submission_unknown:
            control_events = current_effect != nullptr &&
                                     current_effect->operation ==
                                         EffectOperation::submit
                                 ? 4
                                 : 2;
            break;
        case ClusterOrderState::reconciling:
        case ClusterOrderState::conflict:
            control_events = 1;
            break;
        case ClusterOrderState::replace_pending:
            return kMaxVenueEventReceipts + 1;
        case ClusterOrderState::filled:
        case ClusterOrderState::cancelled:
        case ClusterOrderState::rejected:
            control_events = 0;
            break;
    }
    const auto fills = static_cast<std::size_t>(
        reservation.remaining_quantity.atoms());
    if (fills > kMaxVenueEventReceipts -
                    std::min(control_events, kMaxVenueEventReceipts)) {
        return kMaxVenueEventReceipts + 1;
    }
    return fills + control_events;
}

[[nodiscard]] bool venue_completion_capacity_available(
    const TradingState& state, const QuantityLots new_order_quantity) noexcept {
    if (new_order_quantity.atoms() <= 0 ||
        static_cast<std::uint64_t>(new_order_quantity.atoms()) >
            kMaxVenueEventReceipts) {
        return false;
    }
    std::size_t required = accepted_venue_event_count(state);
    const auto add = [&](const std::size_t amount) noexcept {
        if (required > kMaxVenueEventReceipts ||
            amount > kMaxVenueEventReceipts - required) {
            required = kMaxVenueEventReceipts + 1;
        } else {
            required += amount;
        }
    };
    for (std::size_t index = 0; index < state.order_count; ++index) {
        add(future_venue_events_for_order(state, state.orders[index]));
    }
    add(static_cast<std::size_t>(new_order_quantity.atoms()));
    add(5);
    return required <= kMaxVenueEventReceipts;
}

[[nodiscard]] bool outbox_cancel_capacity_available_for_new_order(
    const TradingState& state) noexcept {
    std::size_t orders_needing_cancel_slot = 0;
    for (std::size_t order_index = 0; order_index < state.order_count;
         ++order_index) {
        const auto& order = state.orders[order_index];
        if (order.reservation_index >= state.reservation_count ||
            state.reservations[order.reservation_index].state ==
                ReservationState::released) {
            continue;
        }
        bool has_cancel_slot = false;
        for (std::size_t effect_index = 0;
             effect_index < state.outbox_effect_count; ++effect_index) {
            const auto& effect = state.outbox_effects[effect_index];
            if (effect.cluster_order_id == order.cluster_order_id &&
                effect.operation == EffectOperation::cancel) {
                has_cancel_slot = true;
                break;
            }
        }
        orders_needing_cancel_slot += !has_cancel_slot;
    }

    return state.outbox_effect_count <= kMaxOutboxEffects &&
           orders_needing_cancel_slot <= kMaxOutboxEffects &&
           state.outbox_effect_count + orders_needing_cancel_slot + 2 <=
               kMaxOutboxEffects;
}

std::size_t settle_orphaned_effects(TradingState& state,
                                    const ClusterOrderId cluster_order_id) noexcept {
    std::size_t settled = 0;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        auto& effect = state.outbox_effects[index];
        if (effect.cluster_order_id != cluster_order_id) continue;
        if (effect_is_settled(effect.state)) continue;
        effect.state = OutboxState::incomplete;
        ++settled;
    }
    state.settled_incomplete_effect_count += settled;
    return settled;
}

[[nodiscard]] const CallerCursor* find_caller_cursor(
    const TradingState& state, const CallerId caller_id) noexcept {
    for (std::size_t index = 0; index < state.caller_cursor_count; ++index) {
        if (state.caller_cursors[index].caller_id == caller_id) {
            return &state.caller_cursors[index];
        }
    }
    return nullptr;
}

[[nodiscard]] bool receipt_is_pinned(const TradingState& state,
                                     const RequestReceipt& receipt,
                                     const CallerId acknowledging_caller = {},
                                     const RequestSequence acknowledged_through = {}) noexcept {
    const auto& result = receipt.result;
    const auto* producer =
        find_caller_cursor(state, receipt.identity.caller_id);
    auto effective_acknowledgement =
        producer == nullptr ? RequestSequence{}
                            : producer->gateway_results_acknowledged_through;
    if (receipt.identity.caller_id == acknowledging_caller &&
        effective_acknowledgement < acknowledged_through) {
        effective_acknowledgement = acknowledged_through;
    }
    if (producer != nullptr &&
        effective_acknowledgement.value() != 0 &&
        effective_acknowledgement < receipt.identity.request_sequence) {
        return true;
    }
    const bool gateway_evidence =
        result.detail_kind == ApplyResultDetailKind::fill ||
        result.detail_kind == ApplyResultDetailKind::venue_event;
    if (gateway_evidence) {
        if (producer == nullptr ||
            effective_acknowledgement < receipt.identity.request_sequence) {
            return true;
        }
    }
    if (result.code != ApplyCode::applied &&
        result.detail_kind != ApplyResultDetailKind::fill) {
        return false;
    }
    switch (result.detail_kind) {
        case ApplyResultDetailKind::authorization: {
            for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
                if (state.outbox_effects[index].cluster_order_id ==
                        result.cluster_order_id() &&
                    state.outbox_effects[index].effect_id == result.effect_id()) {
                    return true;
                }
            }
            return false;
        }
        case ApplyResultDetailKind::venue_event: {
            const auto effect_id = result.event_effect_id();
            if (effect_id.value() == 0) return true;
            for (std::size_t index = 0; index < state.outbox_effect_count;
                 ++index) {
                const auto& effect = state.outbox_effects[index];
                if (effect.effect_id != effect_id) continue;
                if (!effect_is_settled(effect.state)) return true;
                const auto order_index =
                    find_order_by_decision(state, result.decision_id);
                return order_index != state.order_count &&
                       state.orders[order_index].effect_id == effect_id &&
                       !order_is_risk_resolved(state,
                                               state.orders[order_index]);
            }
            return false;
        }
        case ApplyResultDetailKind::fill: {
            if (result.decision_id.value() == 0) return false;
            if (find_order_by_decision(state, result.decision_id) ==
                state.order_count) {
                return false;
            }
            if (result.code == ApplyCode::applied) return true;
            if (result.code != ApplyCode::fill_capacity_exceeded &&
                result.code != ApplyCode::fill_quantity_conflict &&
                result.code != ApplyCode::fill_price_conflict &&
                result.code != ApplyCode::fill_fee_conflict) {
                return false;
            }
            for (std::size_t index = 0; index < state.receipt_count; ++index) {
                const auto& earlier = state.receipts[index];
                if (earlier.identity == receipt.identity) break;
                if (earlier.result.detail_kind == ApplyResultDetailKind::fill &&
                    earlier.result.code == result.code &&
                    earlier.result.decision_id == result.decision_id &&
                    earlier.result.fill_venue_id() == result.fill_venue_id() &&
                    earlier.result.execution_id() == result.execution_id() &&
                    earlier.identity.payload_digest ==
                        receipt.identity.payload_digest) {
                    return false;
                }
            }
            return true;
        }
        default:
            return false;
    }
}

[[nodiscard]] std::size_t sweep_expired_receipts(
    TradingState& state, const CallerId acknowledging_caller = {},
    const RequestSequence acknowledged_through = {}) noexcept {
    std::size_t kept = 0;
    std::size_t dropped = 0;
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        if (!receipt_is_pinned(state, state.receipts[index],
                               acknowledging_caller, acknowledged_through)) {
            const auto& identity = state.receipts[index].identity;
            auto* cursor = find_caller_cursor(state, identity.caller_id);
            if (cursor == nullptr &&
                state.caller_cursor_count < state.caller_cursors.size()) {
                cursor = &state.caller_cursors[state.caller_cursor_count++];
                *cursor = CallerCursor{identity.caller_id, RequestSequence{},
                                       RequestSequence{}};
            }
            if (cursor != nullptr && cursor->expired_below < identity.request_sequence) {
                cursor->expired_below = identity.request_sequence;
            }
            ++dropped;
            continue;
        }
        state.receipts[kept++] = state.receipts[index];
    }
    for (std::size_t index = kept; index < state.receipt_count; ++index) {
        state.receipts[index] = {};
    }
    state.receipt_count = kept;
    return dropped;
}

struct ReceiptAdmissionAvailability {
    bool receipt_available{};
    bool caller_available{};
};

[[nodiscard]] ReceiptAdmissionAvailability receipt_admission_after_sweep(
    const TradingState& state, const CallerId command_caller) noexcept {
    std::array<CallerId, kMaxCallerCursors> callers{};
    auto caller_count = state.caller_cursor_count;
    for (std::size_t index = 0; index < caller_count; ++index) {
        callers[index] = state.caller_cursors[index].caller_id;
    }
    const auto has_caller = [&](const CallerId caller) noexcept {
        return std::find(callers.begin(), callers.begin() +
            static_cast<std::ptrdiff_t>(caller_count), caller) !=
            callers.begin() + static_cast<std::ptrdiff_t>(caller_count);
    };

    auto kept = state.receipt_count;
    if (state.receipt_count == state.receipts.size()) {
        kept = 0;
        for (std::size_t index = 0; index < state.receipt_count; ++index) {
            const auto& receipt = state.receipts[index];
            if (receipt_is_pinned(state, receipt)) {
                ++kept;
                continue;
            }
            if (!has_caller(receipt.identity.caller_id) &&
                caller_count < callers.size()) {
                callers[caller_count++] = receipt.identity.caller_id;
            }
        }
    }
    return {kept < state.receipts.size(),
            has_caller(command_caller) || caller_count < callers.size()};
}

[[nodiscard]] std::size_t sweep_admitted_source_events(TradingState& state) noexcept {
    std::size_t kept = 0;
    for (std::size_t index = 0; index < state.source_event_count; ++index) {
        const auto& event = state.source_events[index];
        bool covered = false;
        for (std::size_t cursor = 0; cursor < state.source_count; ++cursor) {
            const auto& source = state.sources[cursor];
            if (source.source_id != event.source_id) continue;
            covered = source.generation > event.generation ||
                      (source.generation == event.generation &&
                       source.sequence >= event.sequence);
        }
        if (covered) continue;
        state.source_events[kept++] = state.source_events[index];
    }
    const auto dropped = state.source_event_count - kept;
    for (std::size_t index = kept; index < state.source_event_count; ++index) {
        state.source_events[index] = {};
    }
    state.source_event_count = kept;
    return dropped;
}

[[nodiscard]] CallerCursor* find_caller_cursor(TradingState& state,
                                               const CallerId caller_id) noexcept {
    for (std::size_t index = 0; index < state.caller_cursor_count; ++index) {
        if (state.caller_cursors[index].caller_id == caller_id) {
            return &state.caller_cursors[index];
        }
    }
    return nullptr;
}

[[nodiscard]] bool decision_is_authorizable(
    const DecisionRecordHot& hot, const DecisionRecordCold& cold) noexcept {
    return hot.evaluation_mode == EvaluationMode::authoritative &&
           hot.code == DecisionCode::action &&
           hot.effective_directive == Directive::place_limit &&
           hot.action_resolution_code == ActionResolutionCode::proposed &&
           validate(cold.proposal) == ContractError::none &&
           cold.proposal.decision_id == hot.decision_id;
}

[[nodiscard]] bool decision_matches_current_execution_contract(
    const TradingState& state,
    const DecisionRecordHot& hot,
    const DecisionRecordCold& cold) noexcept {
    const auto* policy = find_policy(state, hot.policy_id, hot.policy_version);
    const auto& bundle = state.active_executable_policy;
    return policy != nullptr && policy->lifecycle == PolicyLifecycle::active &&
           bundle.present &&
           bundle.executable_policy.policy_id == hot.policy_id &&
           bundle.executable_policy.policy_version == hot.policy_version &&
           bundle.executable_policy.policy_artifact_digest ==
               policy->artifact.artifact_digest &&
           policy->artifact.account_id == cold.proposal.account_id &&
           policy->artifact.instrument_id == cold.proposal.instrument_id &&
           policy->artifact.feature_schema_id == cold.frame.feature_schema_id &&
           bundle.feature_schema.feature_schema_id == cold.frame.feature_schema_id &&
           bundle.instrument.venue_id == cold.proposal.venue_id &&
           bundle.instrument.instrument_id == cold.proposal.instrument_id &&
           bundle.instrument.instrument_spec_version ==
               cold.proposal.instrument_spec_version &&
           bundle.venue.venue_id == cold.proposal.venue_id &&
           bundle.venue.venue_capabilities_version ==
               cold.proposal.venue_capabilities_version;
}

[[nodiscard]] bool decision_is_superseded(const TradingState& state,
                                          const std::size_t index) noexcept {
    const auto& proposal = state.decision_cold[index].proposal;
    for (std::size_t later = index + 1; later < state.decision_count; ++later) {
        if (!decision_is_authorizable(state.decision_hot[later],
                                      state.decision_cold[later])) {
            continue;
        }
        const auto& candidate = state.decision_cold[later].proposal;
        if (candidate.account_id == proposal.account_id &&
            candidate.instrument_id == proposal.instrument_id &&
            candidate.venue_id == proposal.venue_id &&
            candidate.order_slot == proposal.order_slot) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::size_t sweep_resolved_order_groups(TradingState& state) noexcept {
    constexpr std::uint16_t kDroppedIndex = std::numeric_limits<std::uint16_t>::max();

    std::array<bool, kMaxClusterOrders> drop_order{};
    std::array<bool, kMaxRiskReservations> drop_reservation{};
    std::array<bool, kMaxOutboxEffects> drop_effect{};
    std::array<bool, kMaxDecisionRecords> drop_decision{};

    std::size_t reclaimed = 0;
    for (std::size_t index = 0; index < state.order_count; ++index) {
        const auto& order = state.orders[index];
        if (!order_is_risk_resolved(state, order)) continue;

        bool effects_settled = true;
        for (std::size_t effect = 0; effect < state.outbox_effect_count; ++effect) {
            const auto& record = state.outbox_effects[effect];
            if (record.cluster_order_id != order.cluster_order_id) continue;
            if (!effect_is_settled(record.state)) {
                effects_settled = false;
                break;
            }
        }
        if (!effects_settled) continue;

        drop_order[index] = true;
        drop_reservation[order.reservation_index] = true;
        ++reclaimed;
    }
    for (std::size_t effect = 0; effect < state.outbox_effect_count; ++effect) {
        const auto& record = state.outbox_effects[effect];
        for (std::size_t index = 0; index < state.order_count; ++index) {
            if (drop_order[index] &&
                state.orders[index].cluster_order_id == record.cluster_order_id) {
                drop_effect[effect] = true;
                break;
            }
        }
    }

    for (std::size_t index = 0; index < state.order_count; ++index) {
        if (!drop_order[index]) continue;
        for (std::size_t slot = 0; slot < state.decision_count; ++slot) {
            if (state.decision_hot[slot].decision_id == state.orders[index].decision_id) {
                drop_decision[slot] = true;
            }
        }
    }
    for (std::size_t index = 0; index < state.decision_count; ++index) {
        if (drop_decision[index]) continue;
        if (decision_is_authorizable(state.decision_hot[index],
                                     state.decision_cold[index]) &&
            decision_matches_current_execution_contract(
                state, state.decision_hot[index], state.decision_cold[index]) &&
            !decision_is_superseded(state, index)) {
            continue;
        }
        drop_decision[index] = true;
    }

    for (std::size_t slot = 0; slot < state.decision_count; ++slot) {
        if (!drop_decision[slot]) continue;
        const auto decision_id = state.decision_hot[slot].decision_id;
        for (std::size_t index = 0; index < state.order_count; ++index) {
            if (!drop_order[index] && state.orders[index].decision_id == decision_id) {
                drop_decision[slot] = false;
            }
        }
        for (std::size_t effect = 0; effect < state.outbox_effect_count; ++effect) {
            if (!drop_effect[effect] &&
                state.outbox_effects[effect].decision_id == decision_id) {
                drop_decision[slot] = false;
            }
        }
    }

    const bool any_drop =
        reclaimed > 0 ||
        std::any_of(drop_decision.begin(),
                    drop_decision.begin() +
                        static_cast<std::ptrdiff_t>(state.decision_count),
                    [](const bool value) { return value; });
    if (!any_drop) return 0;

    std::array<std::uint16_t, kMaxRiskReservations> reservation_remap{};
    std::size_t kept_reservations = 0;
    for (std::size_t index = 0; index < state.reservation_count; ++index) {
        if (drop_reservation[index]) {
            reservation_remap[index] = kDroppedIndex;
            continue;
        }
        reservation_remap[index] = static_cast<std::uint16_t>(kept_reservations);
        state.reservations[kept_reservations++] = state.reservations[index];
    }
    for (std::size_t index = kept_reservations; index < state.reservation_count; ++index) {
        state.reservations[index] = {};
    }
    state.reservation_count = kept_reservations;

    std::size_t kept_orders = 0;
    for (std::size_t index = 0; index < state.order_count; ++index) {
        if (drop_order[index]) continue;
        auto order = state.orders[index];
        order.reservation_index = reservation_remap[order.reservation_index];
        state.orders[kept_orders++] = order;
    }
    for (std::size_t index = kept_orders; index < state.order_count; ++index) {
        state.orders[index] = {};
    }
    state.order_count = kept_orders;

    std::size_t kept_effects = 0;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (drop_effect[index]) continue;
        state.outbox_effects[kept_effects++] = state.outbox_effects[index];
    }
    for (std::size_t index = kept_effects; index < state.outbox_effect_count; ++index) {
        state.outbox_effects[index] = {};
    }
    state.outbox_effect_count = kept_effects;

    std::size_t kept_decisions = 0;
    for (std::size_t index = 0; index < state.decision_count; ++index) {
        if (drop_decision[index]) continue;
        state.decision_hot[kept_decisions] = state.decision_hot[index];
        state.decision_cold[kept_decisions] = state.decision_cold[index];
        ++kept_decisions;
    }
    for (std::size_t index = kept_decisions; index < state.decision_count; ++index) {
        state.decision_hot[index] = {};
        state.decision_cold[index] = {};
    }
    state.decision_count = kept_decisions;

    state.reclaimed_group_count += reclaimed;
    return reclaimed;
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

    if (state.decision_count == state.decision_hot.size()) {
        (void)sweep_resolved_order_groups(state);
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
                               hot.matched_rule_index, hot.rules_visited,
                               hot.action_resolution_code);
    }
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        const auto& receipt = state.receipts[index];
        if (receipt.result.code != ApplyCode::applied ||
            receipt.result.decision_id != command.decision_id ||
            receipt.result.decision_code == DecisionCode::none) continue;
        if (receipt.identity.payload_digest != identity.payload_digest) {
            return decision_result(state, ApplyCode::decision_id_conflict,
                                   next_version, command, DecisionCode::none,
                                   ContractError::none, Directive::no_action,
                                   Directive::no_action, no_rule, 0);
        }
        auto replay = receipt.result;
        replay.code = ApplyCode::already_applied;
        replay.state_version = next_version;
        return replay;
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
    ActionResolutionCode resolution_code = ActionResolutionCode::none;
    EconomicActionProposal proposal{};
    const PolicyRecord* policy_record = nullptr;
    const ExecutablePolicyBundle* executable_bundle = nullptr;

    if (contract_error != ContractError::none) {
        code = DecisionCode::invalid_frame;
    } else if ((policy_record = find_policy(state, command.frame.policy_id,
                                             command.frame.policy_version)) == nullptr) {
        code = DecisionCode::policy_not_found;
    } else if (policy_record->lifecycle != PolicyLifecycle::active) {
        code = DecisionCode::policy_not_active;
    } else if (!state.active_executable_policy.present ||
               state.active_executable_policy.executable_policy.policy_id !=
                   command.frame.policy_id ||
               state.active_executable_policy.executable_policy.policy_version !=
                   command.frame.policy_version ||
               state.active_executable_policy.executable_policy
                       .policy_artifact_digest !=
                   policy_record->artifact.artifact_digest) {
        code = DecisionCode::executable_policy_not_active;
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
            executable_bundle = &state.active_executable_policy;
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
            } else {
                const auto slot = executable_bundle->executable_policy
                                      .rule_template_slots[matched]
                                      .value();
                if (slot != 0) {
                    const auto resolution = resolve_action(
                        command.decision_id, policy_record->artifact,
                        command.frame,
                        executable_bundle->executable_policy.templates[slot - 1],
                        executable_bundle->instrument,
                        executable_bundle->venue);
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
        }
    }

    const auto outcome = decision_result(state, ApplyCode::applied, next_version,
                                         command, code, contract_error, proposed,
                                         effective, matched, visited,
                                         resolution_code);
    const auto index = state.decision_count;
    state.decision_hot[index] = DecisionRecordHot{
        command.decision_id, command.frame.policy_id, command.frame.policy_version,
        next_version, state.kill_epoch, state.executor_generation, matched, visited,
        code, contract_error, proposed, effective, command.evaluation_mode,
        state.strategy_mode, resolution_code};
    state.decision_cold[index] = DecisionRecordCold{
        command.decision_id, identity.payload_digest, command.frame, proposal};
    ++state.decision_count;
    return outcome;
}

[[nodiscard]] ApplyResult authorization_result(
    const TradingState& state,
    const ApplyCode code,
    const std::uint64_t next_version,
    const DecisionId decision_id,
    const ClusterOrderId cluster_order_id = {},
    const EffectId effect_id = {},
    const MoneyMicros reserved_exposure = {}) noexcept {
    auto value = result(state, code, next_version);
    value.decision_id = decision_id;
    if (cluster_order_id.value() != 0 || effect_id.value() != 0 ||
        reserved_exposure.atoms() != 0) {
        value.detail = ApplyResultDetail::authorization(
            cluster_order_id, effect_id, reserved_exposure);
        value.detail_kind = ApplyResultDetailKind::authorization;
    }
    return value;
}

[[nodiscard]] ApplyResult fill_result(
    const TradingState& state,
    const ApplyCode code,
    const std::uint64_t next_version,
    const VenueObservation& observation,
    const QuantityLots filled_quantity = {},
    const ContractError contract_error = ContractError::none,
    const DecisionId decision_id = {}) noexcept {
    auto value = result(state, code, next_version, {}, {}, PolicyLifecycle::none,
                        contract_error);
    if (observation.venue_id.value() != 0 ||
        observation.execution_id.value() != 0 || filled_quantity.atoms() != 0) {
        value.detail = ApplyResultDetail::fill(
            observation.venue_id, observation.execution_id, filled_quantity);
        value.detail_kind = ApplyResultDetailKind::fill;
        value.metadata_words = {observation.venue_session_id.value(),
                                observation.venue_sequence.value()};
    }
    value.decision_id = decision_id;
    return value;
}

[[nodiscard]] ApplyResult apply_authorize_decision(
    TradingState& state,
    const AuthorizeDecision& command,
    const std::uint64_t next_version,
    const VerifiedExecutionCatalog& catalog) noexcept {
    if (command.decision_id.value() == 0) {
        return authorization_result(state, ApplyCode::invalid_command,
                                    next_version, command.decision_id);
    }

    std::int64_t largest_configured_order = 0;
    for (std::size_t index = 0; index < state.risk_account_count; ++index) {
        largest_configured_order = std::max(
            largest_configured_order,
            state.risk_accounts[index].max_order_quantity.atoms());
    }
    const bool venue_capacity_pressure =
        accepted_venue_event_count(state) != 0 &&
        largest_configured_order > 0 &&
        !venue_completion_capacity_available(
            state, QuantityLots::from_atoms(largest_configured_order));
    const bool outbox_capacity_pressure =
        !outbox_cancel_capacity_available_for_new_order(state);
    if (state.reservation_count == state.reservations.size() ||
        state.order_count == state.orders.size() ||
        state.outbox_effect_count == state.outbox_effects.size() ||
        venue_capacity_pressure || outbox_capacity_pressure) {
        (void)sweep_resolved_order_groups(state);
        if (venue_capacity_pressure) (void)sweep_expired_receipts(state);
    }

    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        const auto& retained = state.receipts[index].result;
        if (retained.code != ApplyCode::applied ||
            retained.detail_kind != ApplyResultDetailKind::authorization ||
            retained.decision_id != command.decision_id) continue;
        const EffectId submit_id{low_u64(domain_digest(
            "velocita/order-effect/v1",
            std::array<std::uint64_t, 3>{retained.cluster_order_id().value(), 1,
                static_cast<std::uint64_t>(EffectOperation::submit)}))};
        if (retained.effect_id() == submit_id) {
            auto replay = retained;
            replay.code = ApplyCode::already_applied;
            replay.state_version = next_version;
            return replay;
        }
    }

    const auto decision_index = find_decision_index(state, command.decision_id);
    if (decision_index == state.decision_count) {
        return authorization_result(state, ApplyCode::decision_not_authorizable,
                                    next_version, command.decision_id);
    }
    const auto& decision = state.decision_hot[decision_index];
    const auto& cold = state.decision_cold[decision_index];
    if (!decision_is_authorizable(decision, cold) ||
        !decision_matches_current_execution_contract(state, decision, cold) ||
        decision_is_superseded(state, decision_index)) {
        return authorization_result(state, ApplyCode::decision_not_authorizable,
                                    next_version, command.decision_id);
    }

    const auto* row = catalog.lookup(cold.proposal.account_id,
        cold.proposal.venue_id, cold.proposal.instrument_id,
        cold.proposal.instrument_spec_version, cold.proposal.venue_capabilities_version);
    if (row == nullptr) {
        return authorization_result(state, ApplyCode::catalog_row_not_found,
                                    next_version, command.decision_id);
    }
    if (row->instrument != state.active_executable_policy.instrument ||
        row->capabilities != state.active_executable_policy.venue) {
        return authorization_result(state, ApplyCode::catalog_binding_mismatch,
                                    next_version, command.decision_id);
    }

    if (state.kill_epoch.value() == 0 ||
        state.executor_generation.value() == 0) {
        return authorization_result(state, ApplyCode::stale_control_generation,
                                    next_version, command.decision_id);
    }

    const auto existing_order_index =
        find_order_by_decision(state, command.decision_id);
    if (existing_order_index != state.order_count) {
        const auto& order = state.orders[existing_order_index];
        const auto& reservation = state.reservations[order.reservation_index];
        const auto exposure = reservation.margin_hold.checked_add(
            reservation.fee_hold);
        if (!exposure) {
            return authorization_result(state,
                                        ApplyCode::risk_arithmetic_overflow,
                                        next_version, command.decision_id);
        }
        return authorization_result(state, ApplyCode::already_applied,
                                    next_version, command.decision_id,
                                    order.cluster_order_id, order.effect_id,
                                    *exposure);
    }

    if (state.strategy_mode != StrategyMode::normal ||
        decision.kill_epoch != state.kill_epoch ||
        decision.executor_generation != state.executor_generation) {
        return authorization_result(state, ApplyCode::stale_control_generation,
                                    next_version, command.decision_id);
    }

    auto* risk = find_risk_account(state, cold.proposal.account_id,
                                   cold.proposal.instrument_id);
    if (risk == nullptr) {
        return authorization_result(state, ApplyCode::risk_account_not_found,
                                    next_version, command.decision_id);
    }
    if (risk->risk_policy_version != cold.proposal.risk_policy_version ||
        risk->risk_version != cold.frame.risk_version ||
        risk->position_version != cold.frame.position_version) {
        auto stale = authorization_result(state, ApplyCode::stale_risk_state,
                                          next_version, command.decision_id);
        stale.detail = ApplyResultDetail::risk_state(
            risk->risk_version, risk->position_version, risk->position_quantity);
        stale.detail_kind = ApplyResultDetailKind::risk_state;
        return stale;
    }
    if (risk->initial_margin_ppm == 0 ||
        risk->max_order_quantity.atoms() <= 0 ||
        risk->max_reserved_quantity.atoms() <= 0 ||
        risk->max_order_gross_notional.atoms() <= 0 ||
        risk->max_reserved_exposure.atoms() <= 0 ||
        risk->reserved_quantity.atoms() < 0 ||
        risk->max_position_quantity.atoms() < 0 ||
        risk->reserved_margin.atoms() < 0 || risk->reserved_fee.atoms() < 0) {
        return authorization_result(state, ApplyCode::invalid_command,
                                    next_version, command.decision_id);
    }

    const auto margin = ceil_rate_hold(cold.proposal.notional,
                                       risk->initial_margin_ppm);
    const auto fee = ceil_rate_hold(cold.proposal.notional,
                                    risk->maximum_fee_ppm);
    if (!margin || !fee) {
        return authorization_result(state, ApplyCode::risk_arithmetic_overflow,
                                    next_version, command.decision_id);
    }
    const auto exposure = margin->checked_add(*fee);
    const auto next_quantity = risk->reserved_quantity.checked_add(
        cold.proposal.quantity);
    const auto next_margin = risk->reserved_margin.checked_add(*margin);
    const auto next_fee = risk->reserved_fee.checked_add(*fee);
    if (!exposure || !next_quantity || !next_margin || !next_fee) {
        return authorization_result(state, ApplyCode::risk_arithmetic_overflow,
                                    next_version, command.decision_id);
    }
    const auto next_total_exposure = next_margin->checked_add(*next_fee);
    if (!next_total_exposure) {
        return authorization_result(state, ApplyCode::risk_arithmetic_overflow,
                                    next_version, command.decision_id);
    }
    if (cold.proposal.quantity > risk->max_order_quantity ||
        *next_quantity > risk->max_reserved_quantity ||
        cold.proposal.notional > risk->max_order_gross_notional ||
        *next_total_exposure > risk->max_reserved_exposure) {
        return authorization_result(state, ApplyCode::insufficient_risk_capacity,
                                    next_version, command.decision_id);
    }

    const bool buying = cold.proposal.side == Side::buy;
    const auto extreme = buying
        ? risk->position_quantity.checked_add(*next_quantity)
        : risk->position_quantity.checked_sub(*next_quantity);
    if (!extreme) {
        return authorization_result(state, ApplyCode::risk_arithmetic_overflow,
                                    next_version, command.decision_id);
    }
    const auto floor = risk->allow_short
        ? QuantityLots::from_atoms(-risk->max_position_quantity.atoms())
        : QuantityLots{};
    if (buying ? *extreme > risk->max_position_quantity : *extreme < floor) {
        return authorization_result(state, ApplyCode::position_limit_exceeded,
                                    next_version, command.decision_id);
    }
    const bool reduces_position = buying
        ? risk->position_quantity.atoms() < 0 && extreme->atoms() <= 0
        : risk->position_quantity.atoms() > 0 && extreme->atoms() >= 0;
    if (!reduces_position && risk->max_window_loss.atoms() > 0 &&
        risk->window_realized_pnl.atoms() <= -risk->max_window_loss.atoms()) {
        return authorization_result(state, ApplyCode::loss_limit_reached,
                                    next_version, command.decision_id);
    }

    if (!venue_completion_capacity_available(state, cold.proposal.quantity)) {
        return authorization_result(state,
                                    ApplyCode::venue_event_capacity_exceeded,
                                    next_version, command.decision_id);
    }

    for (std::size_t index = 0; index < state.order_count; ++index) {
        const auto& existing = state.orders[index];
        if (existing.reservation_index >= state.reservation_count) continue;
        if (state.reservations[existing.reservation_index].state ==
            ReservationState::released) continue;
        const auto existing_decision = find_decision_index(state,
                                                           existing.decision_id);
        if (existing_decision == state.decision_count) continue;
        const auto& occupant = state.decision_cold[existing_decision].proposal;
        if (occupant.account_id == cold.proposal.account_id &&
            occupant.instrument_id == cold.proposal.instrument_id &&
            occupant.venue_id == cold.proposal.venue_id &&
            occupant.order_slot == cold.proposal.order_slot) {
            return authorization_result(state, ApplyCode::order_slot_occupied,
                                        next_version, command.decision_id);
        }
    }

    if (state.reservation_count == state.reservations.size()) {
        return authorization_result(state, ApplyCode::reservation_capacity_exceeded,
                                    next_version, command.decision_id);
    }
    if (state.order_count == state.orders.size()) {
        return authorization_result(state, ApplyCode::order_capacity_exceeded,
                                    next_version, command.decision_id);
    }
    if (!outbox_cancel_capacity_available_for_new_order(state)) {
        return authorization_result(state, ApplyCode::outbox_capacity_exceeded,
                                    next_version, command.decision_id);
    }

    const auto order_digest = domain_digest(
        "velocita/cluster-order/v1",
        std::array<std::uint64_t, 4>{
            command.decision_id.value(), cold.proposal.account_id.value(),
            cold.proposal.instrument_id.value(), cold.proposal.order_slot.value()});
    const ClusterOrderId cluster_order_id{low_u64(order_digest)};
    const auto effect_identity_digest = domain_digest(
        "velocita/order-effect/v1",
        std::array<std::uint64_t, 3>{
            cluster_order_id.value(), 1,
            static_cast<std::uint64_t>(EffectOperation::submit)});
    const EffectId effect_id{low_u64(effect_identity_digest)};
    if (cluster_order_id.value() == 0 || effect_id.value() == 0) {
        return authorization_result(state, ApplyCode::derived_identity_collision,
                                    next_version, command.decision_id);
    }
    for (std::size_t index = 0; index < state.order_count; ++index) {
        if (state.orders[index].cluster_order_id == cluster_order_id) {
            return authorization_result(state,
                                        ApplyCode::derived_identity_collision,
                                        next_version, command.decision_id);
        }
    }
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].effect_id == effect_id) {
            return authorization_result(state,
                                        ApplyCode::derived_identity_collision,
                                        next_version, command.decision_id);
        }
    }

    const auto payload_digest = domain_digest(
        "velocita/order-effect-payload/v1",
        std::array<std::uint64_t, 20>{
            effect_id.value(), cluster_order_id.value(), next_version,
            state.executor_generation.value(), state.kill_epoch.value(),
            cold.proposal.venue_id.value(), cold.proposal.account_id.value(),
            cold.proposal.instrument_id.value(),
            static_cast<std::uint64_t>(EffectOperation::submit),
            static_cast<std::uint64_t>(cold.proposal.side),
            static_cast<std::uint64_t>(cold.proposal.price.atoms()),
            static_cast<std::uint64_t>(cold.proposal.quantity.atoms()),
            static_cast<std::uint64_t>(cold.proposal.time_in_force),
            cold.proposal.policy_id.value(), cold.proposal.policy_version.value(),
            command.decision_id.value(), cold.frame.risk_version.value(),
            cold.frame.source_generation.value(), cold.frame.source_sequence.value(),
            static_cast<std::uint64_t>(cold.proposal.post_only)});

    const auto reservation_index = state.reservation_count;
    const RiskReservation reservation{
        cluster_order_id,
        cold.proposal.quantity,
        {},
        cold.proposal.quantity,
        cold.proposal.notional,
        *margin,
        *fee,
        ReservationState::held};
    const ClusterOrder order{cluster_order_id,
                             effect_id,
                             command.decision_id,
                             next_version,
                             static_cast<std::uint16_t>(reservation_index),
                             ClusterOrderState::dispatch_authorized};
    const OutboxEffectRecord effect{effect_id,
                                    cluster_order_id,
                                    command.decision_id,
                                    payload_digest,
                                    1,
                                    OutboxState::pending};

    risk->reserved_quantity = *next_quantity;
    risk->reserved_margin = *next_margin;
    risk->reserved_fee = *next_fee;
    state.reservations[state.reservation_count++] = reservation;
    state.orders[state.order_count++] = order;
    state.outbox_effects[state.outbox_effect_count++] = effect;

    return authorization_result(state, ApplyCode::applied, next_version,
                                command.decision_id, cluster_order_id, effect_id,
                                *exposure);
}

[[nodiscard]] ApplyResult apply_observe_fill(
    TradingState& state,
    const CommandIdentity& identity,
    const ObserveFill& command,
    const std::uint64_t next_version) noexcept {
    const auto& observation = command.observation;
    const auto contract_error = validate(observation);
    if (contract_error != ContractError::none) {
        return fill_result(state, ApplyCode::invalid_command, next_version,
                           observation, observation.quantity, contract_error);
    }
    if (observation.kind != VenueObservationKind::fill) {
        return fill_result(state, ApplyCode::venue_observation_not_applicable,
                           next_version, observation, observation.quantity);
    }
    if (const auto* existing = find_applied_native_event(state, observation.venue_id,
                                observation.venue_session_id, observation.venue_sequence)) {
        if (existing->result.detail_kind != ApplyResultDetailKind::fill ||
            existing->result.execution_id() != observation.execution_id) {
            return fill_result(state, ApplyCode::venue_event_identity_conflict,
                               next_version, observation, observation.quantity);
        }
    }
    if (const auto* existing = find_applied_fill(
            state, observation.venue_id, observation.execution_id)) {
        return fill_result(
            state,
            existing->identity.payload_digest == identity.payload_digest
                ? ApplyCode::already_applied
                : ApplyCode::fill_identity_conflict,
            next_version, observation, existing->result.filled_quantity(),
            ContractError::none, existing->result.decision_id);
    }
    if (accepted_venue_event_count(state) == kMaxVenueEventReceipts) {
        (void)sweep_resolved_order_groups(state);
        (void)sweep_expired_receipts(state);
    }
    const bool venue_evidence_exhausted =
        accepted_venue_event_count(state) == kMaxVenueEventReceipts;

    const auto order_index = find_order_by_effect(
        state, observation.cluster_order_id, observation.effect_id);
    if (order_index == state.order_count) {
        return fill_result(state, ApplyCode::venue_observation_not_applicable,
                           next_version, observation, observation.quantity);
    }
    auto& order = state.orders[order_index];
    if (venue_evidence_exhausted) {
        order.state = ClusterOrderState::conflict;
        order.last_evidence_state_version = next_version;
        return fill_result(state, ApplyCode::fill_capacity_exceeded,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }
    OutboxEffectRecord* outbox = nullptr;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].effect_id == observation.effect_id) {
            outbox = &state.outbox_effects[index];
            break;
        }
    }
    if (outbox == nullptr || outbox->operation != EffectOperation::submit) {
        return fill_result(state, ApplyCode::venue_observation_not_applicable,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }
    if (order.reservation_index >= state.reservation_count) {
        return fill_result(state, ApplyCode::venue_observation_not_applicable,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }
    auto& reservation = state.reservations[order.reservation_index];
    const auto decision_index = find_decision_index(state, order.decision_id);
    if (decision_index == state.decision_count) {
        return fill_result(state, ApplyCode::venue_observation_not_applicable,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }
    const auto& proposal = state.decision_cold[decision_index].proposal;
    if (proposal.venue_id != observation.venue_id ||
        proposal.side != observation.side) {
        return fill_result(state, ApplyCode::venue_observation_not_applicable,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        auto& current = state.outbox_effects[index];
        if (current.effect_id == order.effect_id &&
            current.operation == EffectOperation::replace &&
            (observation.effect_id != current.effect_id ||
             order.state == ClusterOrderState::replace_pending ||
             order.state == ClusterOrderState::submission_unknown ||
             order.state == ClusterOrderState::reconciling)) {
            current.state = OutboxState::reconciling;
            order.state = ClusterOrderState::reconciling;
            order.last_evidence_state_version = next_version;
            return fill_result(state, ApplyCode::fill_quantity_conflict,
                               next_version, observation, observation.quantity,
                               ContractError::none, order.decision_id);
        }
    }

    auto* risk = find_risk_account(state, proposal.account_id,
                                   proposal.instrument_id);
    if (risk == nullptr) {
        return fill_result(state, ApplyCode::risk_account_not_found,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }
    if (reservation.state == ReservationState::released ||
        observation.quantity > reservation.remaining_quantity) {
        order.state = ClusterOrderState::conflict;
        order.last_evidence_state_version = next_version;
        return fill_result(state, ApplyCode::fill_quantity_conflict,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }

    const auto next_remaining = reservation.remaining_quantity.checked_sub(
        observation.quantity);
    const auto next_filled = reservation.filled_quantity.checked_add(
        observation.quantity);
    if (!next_remaining || !next_filled || next_remaining->atoms() < 0 ||
        *next_filled > reservation.original_quantity) {
        order.state = ClusterOrderState::conflict;
        order.last_evidence_state_version = next_version;
        return fill_result(state, ApplyCode::fill_quantity_conflict,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }
    const auto next_notional = proportional_remaining_hold(
        reservation.remaining_notional, *next_remaining,
        reservation.remaining_quantity);
    const auto next_margin = proportional_remaining_hold(
        reservation.margin_hold, *next_remaining,
        reservation.remaining_quantity);
    const auto next_fee = proportional_remaining_hold(
        reservation.fee_hold, *next_remaining,
        reservation.remaining_quantity);
    if (!next_notional || !next_margin || !next_fee) {
        return fill_result(state, ApplyCode::risk_arithmetic_overflow,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }
    const auto released_margin = reservation.margin_hold.checked_sub(*next_margin);
    const auto released_fee = reservation.fee_hold.checked_sub(*next_fee);
    if (!released_margin || !released_fee) {
        return fill_result(state, ApplyCode::risk_arithmetic_overflow,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }
    const auto notional = fill_notional(proposal, observation);
    const auto fee_cap = notional ? ceil_rate_hold(*notional, risk->maximum_fee_ppm) : std::nullopt;
    if (!notional || !fee_cap) {
        return fill_result(state, ApplyCode::risk_arithmetic_overflow,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }
    const bool beyond_limit = proposal.side == Side::buy ? observation.price > proposal.price
                                                         : observation.price < proposal.price;
    const bool over_fee = observation.fee.atoms() < 0 || observation.fee.atoms() > fee_cap->atoms();
    if (beyond_limit || over_fee) {
        order.state = ClusterOrderState::conflict;
        order.last_evidence_state_version = next_version;
        return fill_result(state, beyond_limit ? ApplyCode::fill_price_conflict : ApplyCode::fill_fee_conflict,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }
    const auto ledger = ledger_after_fill(*risk, proposal.side, observation.quantity, *notional, observation.fee);
    if (!ledger) {
        return fill_result(state, ApplyCode::position_arithmetic_overflow,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }
    const auto next_reserved_quantity = risk->reserved_quantity.checked_sub(
        observation.quantity);
    const auto next_reserved_margin = risk->reserved_margin.checked_sub(
        *released_margin);
    const auto next_reserved_fee = risk->reserved_fee.checked_sub(*released_fee);
    const auto signed_fill = proposal.side == Side::buy
                                 ? observation.quantity
                                 : QuantityLots::from_atoms(
                                       -observation.quantity.atoms());
    const auto next_position = risk->position_quantity.checked_add(signed_fill);
    if (!next_reserved_quantity || !next_reserved_margin || !next_reserved_fee ||
        !next_position || next_reserved_quantity->atoms() < 0 ||
        next_reserved_margin->atoms() < 0 || next_reserved_fee->atoms() < 0) {
        return fill_result(state, ApplyCode::position_arithmetic_overflow,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }
    if (risk->position_version.value() ==
        std::numeric_limits<std::uint64_t>::max()) {
        return fill_result(state, ApplyCode::position_arithmetic_overflow,
                           next_version, observation, observation.quantity,
                           ContractError::none, order.decision_id);
    }

    risk->position_quantity = *next_position;
    risk->position_version = PositionVersion{risk->position_version.value() + 1};
    risk->cash = ledger->cash;
    risk->cost_basis = ledger->cost_basis;
    risk->realized_pnl = ledger->realized_pnl;
    risk->fees_paid = ledger->fees_paid;
    risk->window_realized_pnl = ledger->window_realized_pnl;
    risk->reserved_quantity = *next_reserved_quantity;
    risk->reserved_margin = *next_reserved_margin;
    risk->reserved_fee = *next_reserved_fee;
    reservation.filled_quantity = *next_filled;
    reservation.remaining_quantity = *next_remaining;
    reservation.remaining_notional = *next_notional;
    reservation.margin_hold = *next_margin;
    reservation.fee_hold = *next_fee;
    reservation.state = next_remaining->atoms() == 0
                            ? ReservationState::released
                            : ReservationState::partially_consumed;
    if (order.state == ClusterOrderState::conflict) {
    } else if (next_remaining->atoms() == 0) {
        order.state = ClusterOrderState::filled;
    } else if (order.state != ClusterOrderState::cancel_pending &&
               order.state != ClusterOrderState::reconciling &&
               (order.state != ClusterOrderState::submission_unknown ||
                order.effect_id == observation.effect_id)) {
        order.state = ClusterOrderState::partially_filled;
    }
    outbox->state = OutboxState::acknowledged;
    order.last_evidence_state_version = next_version;
    return fill_result(state, ApplyCode::applied, next_version, observation,
                       observation.quantity, ContractError::none,
                       order.decision_id);
}

[[nodiscard]] ApplyResult venue_event_result(
    const TradingState& state,
    const ApplyCode code,
    const std::uint64_t next_version,
    const VenueObservation& observation,
    const ContractError contract_error = ContractError::none,
    const DecisionId decision_id = {}) noexcept {
    auto value = result(state, code, next_version, {}, {}, PolicyLifecycle::none,
                        contract_error);
    if (observation.venue_id.value() != 0 ||
        observation.venue_session_id.value() != 0 ||
        observation.venue_sequence.value() != 0) {
        value.detail = ApplyResultDetail::venue_event(
            observation.venue_id, observation.venue_session_id,
            observation.venue_sequence);
        value.detail_kind = ApplyResultDetailKind::venue_event;
        value.metadata_words[0] = observation.effect_id.value();
        value.metadata_words[1] =
            static_cast<std::uint64_t>(observation_origin(observation.kind));
    }
    value.decision_id = decision_id;
    return value;
}

enum class CancelDisposition : std::uint8_t {
    needs_slot,
    reuses_slot,
    already_cancelling,
    not_cancellable,
};

[[nodiscard]] constexpr bool known(const MassCancelScope value) noexcept {
    switch (value) {
        case MassCancelScope::all:
        case MassCancelScope::account:
        case MassCancelScope::account_instrument:
            return true;
    }
    return false;
}

[[nodiscard]] CancelDisposition disposition_of_cancel(
    const TradingState& state, const ClusterOrder& order) noexcept {
    if (order.reservation_index >= state.reservation_count) {
        return CancelDisposition::not_cancellable;
    }
    const auto& reservation = state.reservations[order.reservation_index];

    const OutboxEffectRecord* current_effect = nullptr;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].effect_id == order.effect_id) {
            current_effect = &state.outbox_effects[index];
            break;
        }
    }

    if ((order.state == ClusterOrderState::cancel_pending ||
         order.state == ClusterOrderState::submission_unknown) &&
        current_effect != nullptr &&
        current_effect->operation == EffectOperation::cancel) {
        return CancelDisposition::already_cancelling;
    }

    const auto cancellable =
        order.state == ClusterOrderState::dispatch_authorized ||
        order.state == ClusterOrderState::acknowledged ||
        order.state == ClusterOrderState::live ||
        order.state == ClusterOrderState::partially_filled;
    if (!cancellable || reservation.state == ReservationState::released) {
        return CancelDisposition::not_cancellable;
    }

    const bool reuse_rejected_cancel =
        current_effect != nullptr &&
        current_effect->operation == EffectOperation::cancel &&
        current_effect->state == OutboxState::rejected;
    return reuse_rejected_cancel ? CancelDisposition::reuses_slot
                                 : CancelDisposition::needs_slot;
}

[[nodiscard]] bool order_matches_scope(const ClusterOrder&,
                                       const EconomicActionProposal& proposal,
                                       const RequestMassCancel& command) noexcept {
    switch (command.scope) {
        case MassCancelScope::all:
            return true;
        case MassCancelScope::account:
            return proposal.account_id == command.account_id;
        case MassCancelScope::account_instrument:
            return proposal.account_id == command.account_id &&
                   proposal.instrument_id == command.instrument_id;
    }
    return false;
}

[[nodiscard]] ApplyResult apply_request_cancel(
    TradingState& state,
    const RequestCancel& command,
    const std::uint64_t next_version) noexcept {
    std::size_t order_index = state.order_count;
    for (std::size_t index = 0; index < state.order_count; ++index) {
        if (state.orders[index].cluster_order_id == command.cluster_order_id) {
            order_index = index;
            break;
        }
    }
    if (command.cluster_order_id.value() == 0 || order_index == state.order_count) {
        return authorization_result(state, ApplyCode::order_not_found,
                                    next_version, {});
    }
    auto& order = state.orders[order_index];
    if (order.reservation_index >= state.reservation_count) {
        return authorization_result(state, ApplyCode::order_not_cancellable,
                                    next_version, order.decision_id);
    }
    auto& reservation = state.reservations[order.reservation_index];

    const OutboxEffectRecord* current_effect = nullptr;
    std::size_t current_effect_index = state.outbox_effect_count;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].effect_id == order.effect_id) {
            current_effect = &state.outbox_effects[index];
            current_effect_index = index;
            break;
        }
    }
    const auto exposure = reservation.margin_hold.checked_add(reservation.fee_hold);
    if (!exposure) {
        return authorization_result(state, ApplyCode::risk_arithmetic_overflow,
                                    next_version, order.decision_id);
    }
    if ((order.state == ClusterOrderState::cancel_pending ||
         order.state == ClusterOrderState::submission_unknown) &&
        current_effect != nullptr &&
        current_effect->operation == EffectOperation::cancel) {
        return authorization_result(state, ApplyCode::already_applied,
                                    next_version, order.decision_id,
                                    order.cluster_order_id, order.effect_id,
                                    *exposure);
    }
    const auto cancellable =
        order.state == ClusterOrderState::dispatch_authorized ||
        order.state == ClusterOrderState::acknowledged ||
        order.state == ClusterOrderState::live ||
        order.state == ClusterOrderState::partially_filled;
    if (!cancellable || reservation.state == ReservationState::released) {
        return authorization_result(state, ApplyCode::order_not_cancellable,
                                    next_version, order.decision_id);
    }
    const bool reuse_rejected_cancel =
        current_effect != nullptr &&
        current_effect->operation == EffectOperation::cancel &&
        current_effect->state == OutboxState::rejected &&
        current_effect_index < state.outbox_effect_count;
    if (state.outbox_effect_count == state.outbox_effects.size() &&
        !reuse_rejected_cancel) {
        return authorization_result(state, ApplyCode::outbox_capacity_exceeded,
                                    next_version, order.decision_id);
    }

    std::uint64_t generation = 1;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].cluster_order_id == order.cluster_order_id) {
            generation = std::max<std::uint64_t>(
                generation, state.outbox_effects[index].effect_generation);
        }
    }
    if (generation == std::numeric_limits<std::uint32_t>::max()) {
        return authorization_result(state, ApplyCode::derived_identity_collision,
                                    next_version, order.decision_id);
    }
    ++generation;
    const auto identity_digest = domain_digest(
        "velocita/order-effect/v1",
        std::array<std::uint64_t, 3>{
            order.cluster_order_id.value(), generation,
            static_cast<std::uint64_t>(EffectOperation::cancel)});
    const EffectId effect_id{low_u64(identity_digest)};
    if (effect_id.value() == 0) {
        return authorization_result(state, ApplyCode::derived_identity_collision,
                                    next_version, order.decision_id);
    }
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].effect_id == effect_id) {
            return authorization_result(state,
                                        ApplyCode::derived_identity_collision,
                                        next_version, order.decision_id);
        }
    }
    const auto decision_index = find_decision_index(state, order.decision_id);
    if (decision_index == state.decision_count ||
        validate(state.decision_cold[decision_index].proposal) != ContractError::none) {
        return authorization_result(state, ApplyCode::order_not_cancellable,
                                    next_version, order.decision_id);
    }
    const auto& cold = state.decision_cold[decision_index];
    const auto& proposal = cold.proposal;
    const auto payload_digest = domain_digest(
        "velocita/order-effect-payload/v1",
        std::array<std::uint64_t, 20>{
            effect_id.value(), order.cluster_order_id.value(), next_version,
            state.executor_generation.value(), state.kill_epoch.value(),
            proposal.venue_id.value(), proposal.account_id.value(),
            proposal.instrument_id.value(),
            static_cast<std::uint64_t>(EffectOperation::cancel),
            static_cast<std::uint64_t>(proposal.side), 0, 0,
            static_cast<std::uint64_t>(proposal.time_in_force),
            proposal.policy_id.value(), proposal.policy_version.value(),
            order.decision_id.value(), cold.frame.risk_version.value(),
            cold.frame.source_generation.value(), cold.frame.source_sequence.value(),
            static_cast<std::uint64_t>(proposal.post_only)});
    const OutboxEffectRecord effect{
        effect_id, order.cluster_order_id, order.decision_id, payload_digest,
        static_cast<std::uint32_t>(generation), OutboxState::pending,
        EffectOperation::cancel};

    order.effect_id = effect_id;
    order.state = ClusterOrderState::cancel_pending;
    if (reuse_rejected_cancel) {
        state.outbox_effects[current_effect_index] = effect;
    } else {
        state.outbox_effects[state.outbox_effect_count++] = effect;
    }
    return authorization_result(state, ApplyCode::applied, next_version,
                                order.decision_id, order.cluster_order_id,
                                effect_id, *exposure);
}

[[nodiscard]] ApplyResult apply_observe_order_event(
    TradingState& state,
    const CommandIdentity& identity,
    const ObserveOrderEvent& command,
    const std::uint64_t next_version) noexcept {
    const auto& observation = command.observation;
    const auto contract_error = validate(observation);
    if (contract_error != ContractError::none) {
        return venue_event_result(state, ApplyCode::invalid_command,
                                  next_version, observation, contract_error);
    }
    if (observation.kind == VenueObservationKind::fill) {
        return venue_event_result(state,
                                  ApplyCode::venue_observation_not_applicable,
                                  next_version, observation);
    }
    if (observation_origin(observation.kind) == ObservationOrigin::native) {
        if (const auto* existing = find_applied_native_event(
                state, observation.venue_id, observation.venue_session_id,
                observation.venue_sequence)) {
            const auto exact =
                existing->identity.payload_digest == identity.payload_digest;
            return venue_event_result(
                state,
                exact ? ApplyCode::already_applied
                      : ApplyCode::venue_event_identity_conflict,
                next_version, observation, ContractError::none,
                exact ? existing->result.decision_id : DecisionId{});
        }
    }
    if (accepted_venue_event_count(state) == kMaxVenueEventReceipts) {
        (void)sweep_resolved_order_groups(state);
        (void)sweep_expired_receipts(state);
    }
    if (accepted_venue_event_count(state) == kMaxVenueEventReceipts) {
        return venue_event_result(state, ApplyCode::venue_event_capacity_exceeded,
                                  next_version, observation);
    }

    OutboxEffectRecord* effect = nullptr;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        auto& candidate = state.outbox_effects[index];
        if (candidate.effect_id == observation.effect_id &&
            candidate.cluster_order_id == observation.cluster_order_id) {
            effect = &candidate;
            break;
        }
    }
    if (effect == nullptr) {
        return venue_event_result(state,
                                  ApplyCode::venue_observation_not_applicable,
                                  next_version, observation);
    }
    std::size_t order_index = state.order_count;
    for (std::size_t index = 0; index < state.order_count; ++index) {
        if (state.orders[index].cluster_order_id == observation.cluster_order_id) {
            order_index = index;
            break;
        }
    }
    if (order_index == state.order_count) {
        return venue_event_result(state,
                                  ApplyCode::venue_observation_not_applicable,
                                  next_version, observation);
    }
    auto& order = state.orders[order_index];
    if (order.reservation_index >= state.reservation_count) {
        return venue_event_result(state,
                                  ApplyCode::venue_observation_not_applicable,
                                  next_version, observation);
    }
    auto& reservation = state.reservations[order.reservation_index];
    const auto decision_index = find_decision_index(state, order.decision_id);
    if (decision_index == state.decision_count ||
        state.decision_cold[decision_index].proposal.venue_id !=
            observation.venue_id) {
        return venue_event_result(state,
                                  ApplyCode::venue_observation_not_applicable,
                                  next_version, observation);
    }

    if (observation.kind == VenueObservationKind::order_acknowledged &&
        effect->operation != EffectOperation::submit) {
        return venue_event_result(state,
                                  ApplyCode::venue_observation_not_applicable,
                                  next_version, observation);
    }

    const bool reconciled_submit =
        observation.kind == VenueObservationKind::order_reconciled;
    const bool reconciled_cancel =
        reconciled_submit && effect->operation == EffectOperation::cancel &&
        effect->effect_id == order.effect_id &&
        (order.state == ClusterOrderState::submission_unknown ||
         order.state == ClusterOrderState::reconciling ||
         order.state == ClusterOrderState::cancel_pending) &&
        (effect->state == OutboxState::unknown ||
         effect->state == OutboxState::rejected ||
         effect->state == OutboxState::reconciling);
    const bool reconciled_resting =
        reconciled_submit &&
        effect->effect_id == order.effect_id &&
        effect->operation == EffectOperation::submit &&
        (order.state == ClusterOrderState::acknowledged ||
         order.state == ClusterOrderState::live ||
         order.state == ClusterOrderState::partially_filled) &&
        effect->state == OutboxState::acknowledged;
    if (reconciled_submit && !reconciled_cancel && !reconciled_resting &&
        !(effect->effect_id == order.effect_id &&
          effect->operation == EffectOperation::submit &&
          (order.state == ClusterOrderState::submission_unknown ||
           order.state == ClusterOrderState::dispatch_authorized) &&
          (effect->state == OutboxState::pending ||
           effect->state == OutboxState::unknown))) {
        return venue_event_result(state,
                                  ApplyCode::venue_observation_not_applicable,
                                  next_version, observation);
    }
    const bool venue_denies_order =
        reconciled_submit && observation.venue_order_id.value() == 0;
    const bool reconciled_conflict =
        reconciled_submit &&
        (venue_denies_order ? reservation.filled_quantity.atoms() != 0
                            : observation.quantity < reservation.filled_quantity);
    if (reconciled_submit && !venue_denies_order &&
        observation.quantity > reservation.filled_quantity) {
        return venue_event_result(state,
                                  ApplyCode::venue_observation_not_applicable,
                                  next_version, observation);
    }

    if (observation.kind == VenueObservationKind::effect_unknown &&
        effect->effect_id == order.effect_id &&
        effect->state == OutboxState::unknown &&
        order.state == ClusterOrderState::submission_unknown) {
        return venue_event_result(state, ApplyCode::already_applied,
                                  next_version, observation,
                                  ContractError::none, order.decision_id);
    }

    const bool terminal = order.state == ClusterOrderState::filled ||
                          order.state == ClusterOrderState::cancelled ||
                          order.state == ClusterOrderState::rejected;
    const bool terminal_cancel_rejection =
        observation.kind == VenueObservationKind::order_rejected &&
        effect->operation == EffectOperation::cancel &&
        effect->effect_id == order.effect_id &&
        (order.state == ClusterOrderState::filled ||
         order.state == ClusterOrderState::cancelled ||
         order.state == ClusterOrderState::rejected);
    const bool late_cancel_completion =
        order.state == ClusterOrderState::filled &&
        (observation.kind == VenueObservationKind::cancelled ||
         (observation.kind == VenueObservationKind::cancel_reconciled &&
          observation.quantity == reservation.filled_quantity)) &&
        effect->operation == EffectOperation::cancel;
    const bool cancel_reconcilable =
        observation.kind == VenueObservationKind::cancel_reconciled &&
        effect->operation == EffectOperation::cancel &&
        effect->effect_id == order.effect_id &&
        ((effect->state == OutboxState::reconciling &&
          order.state == ClusterOrderState::reconciling) ||
         (effect->state == OutboxState::unknown &&
          order.state == ClusterOrderState::submission_unknown) ||
         (effect->state == OutboxState::rejected &&
          order.state == ClusterOrderState::reconciling));
    const bool cancel_agrees = observation.quantity == reservation.filled_quantity;
    const bool cancel_contradicts =
        observation.quantity < reservation.filled_quantity;
    const bool reconciliation = cancel_reconcilable && cancel_agrees;
    if (observation.kind == VenueObservationKind::cancel_reconciled &&
        !late_cancel_completion &&
        (!cancel_reconcilable || (!cancel_agrees && !cancel_contradicts))) {
        return venue_event_result(state, ApplyCode::venue_observation_not_applicable,
                                  next_version, observation);
    }
    const bool effect_resolved = effect->state == OutboxState::acknowledged ||
                                 effect->state == OutboxState::rejected;
    const bool unsolicited_cancellation =
        observation.kind == VenueObservationKind::cancelled &&
        effect->operation == EffectOperation::submit &&
        effect->effect_id == order.effect_id &&
        (order.state == ClusterOrderState::acknowledged ||
         order.state == ClusterOrderState::live ||
         order.state == ClusterOrderState::partially_filled);
    if ((terminal && !late_cancel_completion && !terminal_cancel_rejection) ||
        (effect_resolved && reconciled_cancel &&
         effect->state != OutboxState::rejected) ||
        order.state == ClusterOrderState::conflict ||
        (order.state == ClusterOrderState::reconciling && !reconciled_cancel &&
         !(cancel_reconcilable && (cancel_agrees || cancel_contradicts))) ||
        (effect_resolved && !late_cancel_completion && !reconciled_cancel &&
         !terminal_cancel_rejection && !cancel_reconcilable &&
         !unsolicited_cancellation && !reconciled_resting) ||
        (effect->effect_id != order.effect_id && !late_cancel_completion)) {
        return venue_event_result(state,
                                  ApplyCode::venue_observation_not_applicable,
                                  next_version, observation);
    }

    const bool releases =
        reconciliation ||
        (observation.kind == VenueObservationKind::order_rejected &&
         effect->operation == EffectOperation::submit) ||
        (venue_denies_order && !reconciled_conflict);
    RiskAccount* risk = nullptr;
    std::optional<QuantityLots> next_reserved_quantity;
    std::optional<MoneyMicros> next_reserved_margin;
    std::optional<MoneyMicros> next_reserved_fee;
    if (releases && reservation.state != ReservationState::released) {
        const auto& proposal = state.decision_cold[decision_index].proposal;
        risk = find_risk_account(state, proposal.account_id,
                                 proposal.instrument_id);
        if (risk == nullptr) {
            return venue_event_result(state, ApplyCode::risk_account_not_found,
                                      next_version, observation);
        }
        next_reserved_quantity = risk->reserved_quantity.checked_sub(
            reservation.remaining_quantity);
        next_reserved_margin = risk->reserved_margin.checked_sub(
            reservation.margin_hold);
        next_reserved_fee = risk->reserved_fee.checked_sub(reservation.fee_hold);
        if (!next_reserved_quantity || !next_reserved_margin || !next_reserved_fee ||
            next_reserved_quantity->atoms() < 0 ||
            next_reserved_margin->atoms() < 0 || next_reserved_fee->atoms() < 0) {
            return venue_event_result(state, ApplyCode::risk_arithmetic_overflow,
                                      next_version, observation);
        }
    }

    switch (observation.kind) {
        case VenueObservationKind::effect_unknown:
            effect->state = OutboxState::unknown;
            order.state = ClusterOrderState::submission_unknown;
            break;
        case VenueObservationKind::order_acknowledged:
            effect->state = OutboxState::acknowledged;
            if (effect->operation == EffectOperation::submit) {
                order.state = ClusterOrderState::live;
            }
            break;
        case VenueObservationKind::order_rejected:
            effect->state = OutboxState::rejected;
            if (effect->operation == EffectOperation::submit) {
                order.state = ClusterOrderState::rejected;
                break;
            }
            if (terminal_cancel_rejection) {
                break;
            }
            order.state = ClusterOrderState::reconciling;
            break;
        case VenueObservationKind::cancelled:
            if (effect->operation != EffectOperation::cancel) {
                const bool resting =
                    order.state == ClusterOrderState::acknowledged ||
                    order.state == ClusterOrderState::live ||
                    order.state == ClusterOrderState::partially_filled;
                if (!resting || effect->operation != EffectOperation::submit ||
                    effect->effect_id != order.effect_id) {
                    return venue_event_result(
                        state, ApplyCode::venue_observation_not_applicable,
                        next_version, observation);
                }
                order.state = ClusterOrderState::reconciling;
                break;
            }
            effect->state = order.state == ClusterOrderState::filled
                                ? OutboxState::acknowledged : OutboxState::reconciling;
            if (order.state != ClusterOrderState::filled) {
                order.state = ClusterOrderState::reconciling;
            }
            break;
        case VenueObservationKind::cancel_reconciled:
            if (cancel_contradicts) {
                effect->state = OutboxState::conflict;
                order.state = ClusterOrderState::conflict;
                break;
            }
            effect->state = OutboxState::acknowledged;
            if (order.state != ClusterOrderState::filled)
                order.state = ClusterOrderState::cancelled;
            break;
        case VenueObservationKind::order_reconciled:
            if (reconciled_conflict) {
                effect->state = OutboxState::conflict;
                order.state = ClusterOrderState::conflict;
            } else if (venue_denies_order) {
                effect->state = OutboxState::rejected;
                order.state = reconciled_resting ? ClusterOrderState::cancelled
                                                 : ClusterOrderState::rejected;
            } else {
                effect->state = reconciled_cancel ? OutboxState::rejected
                                                  : OutboxState::acknowledged;
                order.state = reservation.filled_quantity.atoms() > 0
                                  ? ClusterOrderState::partially_filled
                                  : ClusterOrderState::live;
            }
            break;
        case VenueObservationKind::fill:
            return venue_event_result(
                state, ApplyCode::venue_observation_not_applicable,
                next_version, observation);
    }
    if (releases && reservation.state != ReservationState::released) {
        risk->reserved_quantity = *next_reserved_quantity;
        risk->reserved_margin = *next_reserved_margin;
        risk->reserved_fee = *next_reserved_fee;
        reservation.remaining_quantity = {};
        reservation.remaining_notional = {};
        reservation.margin_hold = {};
        reservation.fee_hold = {};
        reservation.state = ReservationState::released;
    }
    if (releases && order_is_risk_resolved(state, order)) {
        (void)settle_orphaned_effects(state, order.cluster_order_id);
    }
    order.last_evidence_state_version = next_version;
    return venue_event_result(state, ApplyCode::applied, next_version,
                              observation, ContractError::none,
                              order.decision_id);
}

[[nodiscard]] ApplyResult apply_request_mass_cancel(
    TradingState& state,
    const RequestMassCancel& command,
    const std::uint64_t next_version) noexcept {
    if (!known(command.scope)) {
        return result(state, ApplyCode::invalid_command, next_version, {}, {},
                      PolicyLifecycle::none, ContractError::invalid_enum);
    }
    if (command.expected_kill_epoch != state.kill_epoch ||
        command.expected_executor_generation != state.executor_generation) {
        return result(state, ApplyCode::stale_control_generation, next_version);
    }

    std::array<std::uint16_t, kMaxClusterOrders> selected{};
    std::size_t selected_count = 0;
    std::size_t in_scope = 0;
    std::size_t skipped = 0;
    std::size_t new_slots_needed = 0;
    for (std::size_t index = 0; index < state.order_count; ++index) {
        const auto& order = state.orders[index];
        const auto decision_index = find_decision_index(state, order.decision_id);
        if (decision_index == state.decision_count) {
            if (command.scope != MassCancelScope::all) continue;
        } else if (!order_matches_scope(order,
                                        state.decision_cold[decision_index].proposal,
                                        command)) {
            continue;
        }
        ++in_scope;

        switch (disposition_of_cancel(state, order)) {
            case CancelDisposition::needs_slot:
                ++new_slots_needed;
                selected[selected_count++] = static_cast<std::uint16_t>(index);
                break;
            case CancelDisposition::reuses_slot:
                selected[selected_count++] = static_cast<std::uint16_t>(index);
                break;
            case CancelDisposition::already_cancelling:
            case CancelDisposition::not_cancellable:
                ++skipped;
                break;
        }
    }

    const auto free_slots = state.outbox_effects.size() - state.outbox_effect_count;
    if (new_slots_needed > free_slots) {
        return result(state, ApplyCode::outbox_capacity_exceeded, next_version);
    }

    std::size_t cancelled = 0;
    for (std::size_t i = 0; i < selected_count; ++i) {
        const auto order_id = state.orders[selected[i]].cluster_order_id;
        const auto outcome =
            apply_request_cancel(state, RequestCancel{order_id}, next_version);
        if (outcome.code == ApplyCode::applied) ++cancelled;
        else ++skipped;
    }

    auto value = result(state, ApplyCode::applied, next_version);
    value.detail = ApplyResultDetail::mass_cancel(in_scope, cancelled, skipped);
    value.detail_kind = ApplyResultDetailKind::mass_cancel;
    return value;
}

[[nodiscard]] ApplyResult apply_request_replace(
    TradingState& state,
    const RequestReplace& command,
    const std::uint64_t next_version) noexcept {
    return authorization_result(state, ApplyCode::replacement_not_supported,
                                next_version, command.replacement_decision_id);
}

[[nodiscard]] ApplyResult apply_configure_risk_limits(
    TradingState& state, const ConfigureRiskLimits& command,
    const std::uint64_t next_version) noexcept {
    if (!valid_risk_limits(command)) return result(state, ApplyCode::invalid_command, next_version);
    if (command.expected_kill_epoch != state.kill_epoch ||
        command.expected_executor_generation != state.executor_generation)
        return result(state, ApplyCode::stale_control_generation, next_version);
    auto* existing = find_risk_account(state, command.account_id, command.instrument_id);
    if ((existing == nullptr && command.expected_risk_version.value() != 0) ||
        (existing != nullptr && command.expected_risk_version != existing->risk_version))
        return result(state, ApplyCode::stale_risk_state, next_version);
    if (existing == nullptr && state.risk_account_count == state.risk_accounts.size())
        return result(state, ApplyCode::risk_account_capacity_exceeded, next_version);
    RiskAccount configured;
    if (existing != nullptr) {
        if (existing->risk_version.value() == std::numeric_limits<std::uint64_t>::max())
            return result(state, ApplyCode::risk_version_exhausted, next_version);
        const bool rates_changed = command.initial_margin_ppm != existing->initial_margin_ppm ||
                                   command.maximum_fee_ppm != existing->maximum_fee_ppm;
        if (command.risk_policy_version < existing->risk_policy_version ||
            (rates_changed && command.risk_policy_version == existing->risk_policy_version))
            return result(state, ApplyCode::invalid_command, next_version);
        if (rates_changed || command.risk_policy_version != existing->risk_policy_version) {
            for (std::size_t i = 0; i < state.order_count; ++i) {
                const auto& order = state.orders[i];
                if (order.reservation_index >= state.reservation_count)
                    return result(state, ApplyCode::risk_reconfiguration_blocked, next_version);
                if (state.reservations[order.reservation_index].state == ReservationState::released) continue;
                const auto decision = find_decision_index(state, order.decision_id);
                if (decision == state.decision_count)
                    return result(state, ApplyCode::risk_reconfiguration_blocked, next_version);
                const auto& proposal = state.decision_cold[decision].proposal;
                if (proposal.account_id == command.account_id && proposal.instrument_id == command.instrument_id)
                    return result(state, ApplyCode::risk_reconfiguration_blocked, next_version);
            }
        }
        const auto held = existing->reserved_margin.checked_add(existing->reserved_fee);
        if (!held) return result(state, ApplyCode::risk_arithmetic_overflow, next_version);
        if (command.max_reserved_quantity < existing->reserved_quantity || command.max_reserved_exposure < *held)
            return result(state, ApplyCode::insufficient_risk_capacity, next_version);
        configured = *existing;
        configured.risk_version = RiskVersion{existing->risk_version.value() + 1};
    } else {
        configured.account_id = command.account_id;
        configured.instrument_id = command.instrument_id;
        configured.risk_version = RiskVersion{1};
        configured.position_version = PositionVersion{1};
    }
    configured.risk_policy_version = command.risk_policy_version;
    configured.max_order_quantity = command.max_order_quantity;
    configured.max_reserved_quantity = command.max_reserved_quantity;
    configured.max_order_gross_notional = command.max_order_gross_notional;
    configured.max_reserved_exposure = command.max_reserved_exposure;
    configured.initial_margin_ppm = command.initial_margin_ppm;
    configured.maximum_fee_ppm = command.maximum_fee_ppm;
    configured.max_position_quantity = command.max_position_quantity;
    configured.allow_short = command.allow_short;
    configured.max_window_loss = command.max_window_loss;
    if (command.reset_loss_window) configured.window_realized_pnl = {};
    if (existing != nullptr) *existing = configured;
    else state.risk_accounts[state.risk_account_count++] = configured;
    auto outcome = result(state, ApplyCode::applied, next_version);
    outcome.detail_kind = ApplyResultDetailKind::risk_configuration;
    outcome.detail.words = {command.account_id.value(), command.instrument_id.value(), configured.risk_version.value()};
    return outcome;
}

[[nodiscard]] ApplyResult apply_acknowledge_gateway_results(
    TradingState& state, const CallerId caller_id,
    const RequestSequence command_sequence,
    const AcknowledgeGatewayResults& command,
    const std::uint64_t next_version) noexcept {
    if (command.acknowledged_through.value() == 0 ||
        command.acknowledged_through >= command_sequence) {
        return result(state, ApplyCode::invalid_command, next_version);
    }
    auto* cursor = find_caller_cursor(state, caller_id);
    if (cursor == nullptr) {
        return result(state, ApplyCode::invalid_command, next_version);
    }
    if (command.acknowledged_through <=
        cursor->gateway_results_acknowledged_through) {
        return result(state, ApplyCode::already_applied, next_version);
    }
    cursor->gateway_results_acknowledged_through =
        command.acknowledged_through;
    return result(state, ApplyCode::applied, next_version);
}

[[nodiscard]] ApplyResult apply_assign_caller_role(
    TradingState& state, const AssignCallerRole& command,
    const std::uint64_t next_version) noexcept {
    if (command.caller_id.value() == 0 || is_operator_caller(command.caller_id) ||
        (command.role != CallerRole::strategy && command.role != CallerRole::gateway)) {
        return result(state, ApplyCode::invalid_command, next_version);
    }
    auto* cursor = find_caller_cursor(state, command.caller_id);
    if (cursor != nullptr && cursor->role == command.role) {
        return result(state, ApplyCode::already_applied, next_version);
    }
    if (cursor != nullptr && cursor->role != CallerRole::none) {
        return result(state, ApplyCode::caller_role_conflict, next_version);
    }
    if (cursor == nullptr) {
        if (state.caller_cursor_count == state.caller_cursors.size()) {
            return result(state, ApplyCode::receipt_capacity_exceeded, next_version);
        }
        cursor = &state.caller_cursors[state.caller_cursor_count++];
        *cursor = CallerCursor{command.caller_id, RequestSequence{}, RequestSequence{}};
    }
    cursor->role = command.role;
    return result(state, ApplyCode::applied, next_version);
}

[[nodiscard]] ApplyResult apply_resolve_order_conflict(
    TradingState& state, const ResolveOrderConflict& command,
    const std::uint64_t next_version) noexcept {
    const bool booking = command.unbooked_quantity.atoms() > 0;
    if (command.cluster_order_id.value() == 0 || command.evidence_digest.is_zero() ||
        command.unbooked_quantity.atoms() < 0 || command.unbooked_fee.atoms() < 0 ||
        (booking ? command.unbooked_price.atoms() <= 0
                 : command.unbooked_price.atoms() != 0 || command.unbooked_fee.atoms() != 0)) {
        return result(state, ApplyCode::invalid_command, next_version);
    }
    if (command.expected_kill_epoch != state.kill_epoch ||
        command.expected_executor_generation != state.executor_generation) {
        return result(state, ApplyCode::stale_control_generation, next_version);
    }
    std::size_t order_index = 0;
    while (order_index < state.order_count &&
           state.orders[order_index].cluster_order_id != command.cluster_order_id) ++order_index;
    if (order_index == state.order_count) {
        return result(state, ApplyCode::order_not_found, next_version);
    }
    auto& order = state.orders[order_index];
    if (order.state != ClusterOrderState::conflict) {
        return result(state, ApplyCode::order_not_in_conflict, next_version);
    }
    const auto decision_index = find_decision_index(state, order.decision_id);
    if (order.reservation_index >= state.reservation_count || decision_index == state.decision_count) {
        return result(state, ApplyCode::order_not_found, next_version);
    }
    auto& reservation = state.reservations[order.reservation_index];
    const auto& proposal = state.decision_cold[decision_index].proposal;
    auto* risk = find_risk_account(state, proposal.account_id, proposal.instrument_id);
    if (risk == nullptr) {
        return result(state, ApplyCode::risk_account_not_found, next_version);
    }
    const auto next_reserved_quantity = risk->reserved_quantity.checked_sub(reservation.remaining_quantity);
    const auto next_reserved_margin = risk->reserved_margin.checked_sub(reservation.margin_hold);
    const auto next_reserved_fee = risk->reserved_fee.checked_sub(reservation.fee_hold);
    if (!next_reserved_quantity || !next_reserved_margin || !next_reserved_fee ||
        next_reserved_quantity->atoms() < 0 || next_reserved_margin->atoms() < 0 ||
        next_reserved_fee->atoms() < 0) {
        return result(state, ApplyCode::risk_arithmetic_overflow, next_version);
    }
    auto booked = reservation.filled_quantity;
    if (booking) {
        VenueObservation fill;
        fill.price = command.unbooked_price;
        fill.quantity = command.unbooked_quantity;
        const auto notional = fill_notional(proposal, fill);
        const auto ledger = notional
            ? ledger_after_fill(*risk, proposal.side, command.unbooked_quantity, *notional, command.unbooked_fee)
            : std::nullopt;
        const auto next_position = risk->position_quantity.checked_add(
            proposal.side == Side::buy ? command.unbooked_quantity
                                       : QuantityLots::from_atoms(-command.unbooked_quantity.atoms()));
        const auto total = reservation.filled_quantity.checked_add(command.unbooked_quantity);
        if (!ledger || !next_position || !total ||
            risk->position_version.value() == std::numeric_limits<std::uint64_t>::max()) {
            return result(state, ApplyCode::position_arithmetic_overflow, next_version);
        }
        booked = std::min(*total, reservation.original_quantity);
        risk->position_quantity = *next_position;
        risk->position_version = PositionVersion{risk->position_version.value() + 1};
        risk->cash = ledger->cash;
        risk->cost_basis = ledger->cost_basis;
        risk->realized_pnl = ledger->realized_pnl;
        risk->fees_paid = ledger->fees_paid;
        risk->window_realized_pnl = ledger->window_realized_pnl;
    }
    risk->reserved_quantity = *next_reserved_quantity;
    risk->reserved_margin = *next_reserved_margin;
    risk->reserved_fee = *next_reserved_fee;
    reservation.filled_quantity = booked;
    reservation.remaining_quantity = {};
    reservation.remaining_notional = {};
    reservation.margin_hold = {};
    reservation.fee_hold = {};
    reservation.state = ReservationState::released;
    order.state = booked.atoms() > 0 ? ClusterOrderState::filled : ClusterOrderState::cancelled;
    order.last_evidence_state_version = next_version;
    (void)settle_orphaned_effects(state, order.cluster_order_id);
    return result(state, ApplyCode::applied, next_version);
}

[[nodiscard]] ApplyCode catalog_installation_code(
    const TradingState& state, const InstallExecutionCatalog& command) noexcept {
    if (command.catalog_root.is_zero()) return ApplyCode::invalid_command;
    if (command.expected_kill_epoch.value() == 0 ||
        command.expected_executor_generation.value() == 0 ||
        command.expected_kill_epoch != state.kill_epoch ||
        command.expected_executor_generation != state.executor_generation)
        return ApplyCode::stale_control_generation;
    if (!state.execution_catalog_root.is_zero())
        return state.execution_catalog_root == command.catalog_root
            ? ApplyCode::already_applied : ApplyCode::immutable_catalog_conflict;
    if (state.strategy_mode != StrategyMode::halted || state.policy_count != 0 ||
        state.active_executable_policy.present || state.staged_executable_policy.present ||
        state.decision_count != 0 || state.risk_account_count != 0 ||
        state.reservation_count != 0 || state.order_count != 0 || state.outbox_effect_count != 0)
        return ApplyCode::catalog_installation_blocked;
    return ApplyCode::applied;
}

[[nodiscard]] ApplyResult apply_new_command(TradingState& state,
                                            const Command& command,
                                            const std::uint64_t next_version,
                                            const VerifiedExecutionCatalog* catalog) noexcept {
    if (const auto* body = std::get_if<InstallExecutionCatalog>(&command.body)) {
        const auto code = catalog_installation_code(state, *body);
        if (code == ApplyCode::applied) state.execution_catalog_root = body->catalog_root;
        return result(state, code, next_version);
    }
    if (const auto* body = std::get_if<AssignCallerRole>(&command.body)) {
        return apply_assign_caller_role(state, *body, next_version);
    }
    if (const auto* body = std::get_if<ResolveOrderConflict>(&command.body)) {
        return apply_resolve_order_conflict(state, *body, next_version);
    }
    if (state.execution_catalog_root.is_zero() &&
        (std::holds_alternative<PublishPolicy>(command.body) ||
         std::holds_alternative<StageExecutablePolicy>(command.body) ||
         std::holds_alternative<ConfigureRiskLimits>(command.body) ||
         std::holds_alternative<EvaluateDecisionFrame>(command.body) ||
         std::holds_alternative<AuthorizeDecision>(command.body))) {
        return result(state, ApplyCode::catalog_not_installed, next_version);
    }
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
    if (const auto* body = std::get_if<StageExecutablePolicy>(&command.body)) {
        return apply_stage_executable_policy(state, *body, next_version, *catalog);
    }
    if (const auto* body = std::get_if<ActivateExecutablePolicy>(&command.body)) {
        return apply_activate_executable_policy(state, *body, next_version);
    }
    if (const auto* body = std::get_if<AuthorizeDecision>(&command.body)) {
        return apply_authorize_decision(state, *body, next_version, *catalog);
    }
    if (const auto* body = std::get_if<ObserveFill>(&command.body)) {
        return apply_observe_fill(state, command.identity, *body, next_version);
    }
    if (const auto* body = std::get_if<RequestMassCancel>(&command.body)) {
        return apply_request_mass_cancel(state, *body, next_version);
    }
    if (const auto* body = std::get_if<RequestCancel>(&command.body)) {
        return apply_request_cancel(state, *body, next_version);
    }
    if (const auto* body = std::get_if<ObserveOrderEvent>(&command.body)) {
        return apply_observe_order_event(state, command.identity, *body,
                                         next_version);
    }
    if (const auto* body = std::get_if<RequestReplace>(&command.body)) {
        return apply_request_replace(state, *body, next_version);
    }
    if (const auto* body = std::get_if<ConfigureRiskLimits>(&command.body)) {
        return apply_configure_risk_limits(state, *body, next_version);
    }
    if (const auto* body =
            std::get_if<AcknowledgeGatewayResults>(&command.body)) {
        return apply_acknowledge_gateway_results(
            state, command.identity.caller_id,
            command.identity.request_sequence, *body, next_version);
    }
    return result(state, ApplyCode::invalid_command, next_version);
}

}

bool execution_catalog_ready(const Digest256& required_root,
                             const VerifiedExecutionCatalog* catalog) noexcept {
    return required_root.is_zero() ||
        (catalog != nullptr && catalog->row_count() != 0 && catalog->root() == required_root);
}

Digest256 required_execution_catalog(const TradingState& state,
                                     const Command& command) noexcept {
    if (!state.execution_catalog_root.is_zero()) return state.execution_catalog_root;
    const auto* install = std::get_if<InstallExecutionCatalog>(&command.body);
    if (install == nullptr || !present(command.identity) ||
        !is_operator_caller(command.identity.caller_id) ||
        find_receipt(state, command.identity) != nullptr) return {};
    const auto* cursor = find_caller_cursor(state, command.identity.caller_id);
    if (cursor != nullptr && command.identity.request_sequence <= cursor->expired_below) return {};
    if (state.state_version == std::numeric_limits<std::uint64_t>::max()) return {};
    const auto admission = receipt_admission_after_sweep(
        state, command.identity.caller_id);
    if (!admission.receipt_available || !admission.caller_available) return {};
    return catalog_installation_code(state, *install) == ApplyCode::applied
        ? install->catalog_root : Digest256{};
}

std::optional<OrderEffect> materialize_order_effect(
    const TradingState& state, const EffectId effect_id) noexcept {
    if (state.outbox_effect_count > state.outbox_effects.size() ||
        state.receipt_count > state.receipts.size() ||
        state.decision_count > state.decision_hot.size()) return std::nullopt;
    const OutboxEffectRecord* record = nullptr;
    for (std::size_t i = 0; i < state.outbox_effect_count; ++i) {
        if (state.outbox_effects[i].effect_id == effect_id) {
            if (record != nullptr) return std::nullopt;
            record = &state.outbox_effects[i];
        }
    }
    if (record == nullptr ||
        (record->operation != EffectOperation::submit &&
         record->operation != EffectOperation::replace &&
         record->operation != EffectOperation::cancel)) return std::nullopt;
    const ApplyResult* authorization = nullptr;
    for (std::size_t i = 0; i < state.receipt_count; ++i) {
        const auto& receipt = state.receipts[i].result;
        if (receipt.code == ApplyCode::applied &&
            receipt.detail_kind == ApplyResultDetailKind::authorization &&
            receipt.effect_id() == effect_id &&
            receipt.cluster_order_id() == record->cluster_order_id &&
            receipt.decision_id == record->decision_id) {
            if (authorization != nullptr) return std::nullopt;
            authorization = &receipt;
        }
    }
    const auto index = find_decision_index(state, record->decision_id);
    if (authorization == nullptr || index == state.decision_count) return std::nullopt;
    const auto& cold = state.decision_cold[index];
    const auto& proposal = cold.proposal;
    if (validate(proposal) != ContractError::none ||
        proposal.decision_id != record->decision_id ||
        (record->operation != EffectOperation::cancel &&
         proposal.directive != (record->operation == EffectOperation::submit
            ? Directive::place_limit : Directive::replace))) return std::nullopt;
    OrderEffect value;
    value.effect_id = effect_id;
    value.cluster_order_id = record->cluster_order_id;
    value.committed_directive_index = authorization->state_version;
    value.executor_generation = authorization->executor_generation;
    value.kill_epoch = authorization->kill_epoch;
    value.venue_id = proposal.venue_id;
    value.account_id = proposal.account_id;
    value.instrument_id = proposal.instrument_id;
    value.operation = record->operation;
    value.side = proposal.side;
    value.price = record->operation == EffectOperation::cancel ? PriceTicks{} : proposal.price;
    value.quantity = record->operation == EffectOperation::cancel ? QuantityLots{} : proposal.quantity;
    value.time_in_force = proposal.time_in_force;
    value.post_only = proposal.post_only;
    value.policy_id = proposal.policy_id;
    value.policy_version = proposal.policy_version;
    value.decision_id = record->decision_id;
    value.risk_version = cold.frame.risk_version;
    value.source_generation = cold.frame.source_generation;
    value.source_sequence = cold.frame.source_sequence;
    value.payload_digest = domain_digest("velocita/order-effect-payload/v1",
        std::array<std::uint64_t, 20>{
            value.effect_id.value(), value.cluster_order_id.value(),
            value.committed_directive_index, value.executor_generation.value(),
            value.kill_epoch.value(), value.venue_id.value(), value.account_id.value(),
            value.instrument_id.value(), static_cast<std::uint64_t>(value.operation),
            static_cast<std::uint64_t>(value.side),
            static_cast<std::uint64_t>(value.price.atoms()),
            static_cast<std::uint64_t>(value.quantity.atoms()),
            static_cast<std::uint64_t>(value.time_in_force), value.policy_id.value(),
            value.policy_version.value(), value.decision_id.value(),
            value.risk_version.value(), value.source_generation.value(),
            value.source_sequence.value(), static_cast<std::uint64_t>(value.post_only)});
    if (value.payload_digest != record->payload_digest ||
        validate(value) != ContractError::none) return std::nullopt;
    return value;
}

namespace {
bool proposal_matches_catalog(const EconomicActionProposal& proposal,
                              const VerifiedExecutionCatalog& catalog) noexcept {
    if (validate(proposal) != ContractError::none) return false;
    const auto* row = catalog.lookup(proposal.account_id, proposal.venue_id,
        proposal.instrument_id, proposal.instrument_spec_version,
        proposal.venue_capabilities_version);
    if (row == nullptr) return false;
    const auto operation = proposal.directive == Directive::place_limit ? kVenueDirectivePlaceLimit
        : proposal.directive == Directive::cancel ? kVenueDirectiveCancel
        : proposal.directive == Directive::replace ? kVenueDirectiveReplace : 0;
    if (operation == 0 || (row->capabilities.directive_mask & operation) == 0) return false;
    if (proposal.directive == Directive::cancel) return true;
    const auto tif = proposal.time_in_force == TimeInForce::good_til_cancel ? kVenueTifGtc
        : proposal.time_in_force == TimeInForce::immediate_or_cancel ? kVenueTifIoc
        : proposal.time_in_force == TimeInForce::fill_or_kill ? kVenueTifFok : 0;
    if (tif == 0 || (row->capabilities.time_in_force_mask & tif) == 0 ||
        (proposal.post_only == PostOnlyMode::required && !row->capabilities.supports_post_only) ||
        proposal.price < row->instrument.minimum_price ||
        proposal.price > row->instrument.maximum_price ||
        proposal.quantity < row->instrument.minimum_quantity ||
        proposal.quantity > row->instrument.maximum_quantity) return false;
    const auto one_lot = row->instrument.quote_micros_per_tick_lot.checked_multiply(proposal.price.atoms());
    const auto notional = one_lot ? one_lot->checked_multiply(proposal.quantity.atoms()) : std::nullopt;
    return notional && *notional == proposal.notional;
}
}

bool execution_catalog_bindings_valid(const TradingState& state,
                                      const VerifiedExecutionCatalog& catalog) noexcept {
    if (!execution_catalog_ready(state.execution_catalog_root, &catalog) ||
        state.policy_count > state.policies.size() ||
        state.decision_count > state.decision_hot.size() ||
        state.outbox_effect_count > state.outbox_effects.size()) return false;
    if (state.execution_catalog_root.is_zero()) {
        return state.policy_count == 0 && !state.active_executable_policy.present &&
            !state.staged_executable_policy.present && state.decision_count == 0 &&
            state.risk_account_count == 0 && state.reservation_count == 0 &&
            state.order_count == 0 && state.outbox_effect_count == 0;
    }
    for (const auto* bundle : {&state.active_executable_policy, &state.staged_executable_policy}) {
        if (!bundle->present) continue;
        const auto* policy = find_policy(state, bundle->executable_policy.policy_id,
                                         bundle->executable_policy.policy_version);
        if (policy == nullptr) return false;
        const auto* row = catalog.lookup(policy->artifact.account_id,
            bundle->instrument.venue_id, bundle->instrument.instrument_id,
            bundle->instrument.instrument_spec_version,
            bundle->venue.venue_capabilities_version);
        if (row == nullptr || row->instrument != bundle->instrument ||
            row->capabilities != bundle->venue) return false;
    }
    for (std::size_t i = 0; i < state.decision_count; ++i) {
        if (state.decision_hot[i].action_resolution_code == ActionResolutionCode::proposed &&
            !proposal_matches_catalog(state.decision_cold[i].proposal, catalog)) return false;
    }
    for (std::size_t i = 0; i < state.outbox_effect_count; ++i) {
        if (!materialize_bound_order_effect(state, state.outbox_effects[i].effect_id, catalog))
            return false;
    }
    return true;
}

std::optional<BoundOrderEffect> materialize_bound_order_effect(
    const TradingState& state, const EffectId effect_id,
    const VerifiedExecutionCatalog& catalog) noexcept {
    if (state.execution_catalog_root.is_zero() ||
        !execution_catalog_ready(state.execution_catalog_root, &catalog)) return std::nullopt;
    const auto effect = materialize_order_effect(state, effect_id);
    if (!effect) return std::nullopt;
    const auto index = find_decision_index(state, effect->decision_id);
    if (index >= state.decision_count) return std::nullopt;
    const auto& proposal = state.decision_cold[index].proposal;
    if (!proposal_matches_catalog(proposal, catalog)) return std::nullopt;
    return BoundOrderEffect{*effect, state.execution_catalog_root,
        proposal.instrument_spec_version, proposal.venue_capabilities_version};
}

std::size_t pinned_receipt_count(const TradingState& state) noexcept {
    std::size_t pinned = 0;
    for (std::size_t index = 0; index < state.receipt_count; ++index)
        pinned += receipt_is_pinned(state, state.receipts[index]) ? 1 : 0;
    return pinned;
}

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

namespace {

inline constexpr unsigned kOperatorRole = 1;
inline constexpr unsigned kStrategyRole = 2;
inline constexpr unsigned kGatewayRole = 4;

template <typename T, typename... Kinds>
inline constexpr bool is_one_of = (std::is_same_v<T, Kinds> || ...);

[[nodiscard]] unsigned roles_allowed(const CommandBody& body) noexcept {
    return std::visit([](const auto& value) noexcept -> unsigned {
        using T = std::decay_t<decltype(value)>;
        if constexpr (is_one_of<T, AdvanceKillEpoch, SetExecutorGeneration, SetStrategyMode,
                                InstallExecutionCatalog, ConfigureRiskLimits, AssignCallerRole,
                                ResolveOrderConflict,
                                PublishPolicy, ValidatePolicy, ActivatePolicy, SupersedePolicy,
                                StageExecutablePolicy, ActivateExecutablePolicy>) {
            return kOperatorRole;
        } else if constexpr (is_one_of<T, RecordSourceEvent, EvaluateDecisionFrame, AuthorizeDecision>) {
            return kOperatorRole | kStrategyRole;
        } else if constexpr (is_one_of<T, ObserveFill, ObserveOrderEvent, AcknowledgeGatewayResults>) {
            return kGatewayRole;
        } else {
            static_assert(is_one_of<T, RequestCancel, RequestReplace, RequestMassCancel>,
                          "decide which caller roles may send this command");
            return kOperatorRole | kGatewayRole;
        }
    }, body);
}

[[nodiscard]] unsigned role_of(const TradingState& state, const CallerId caller) noexcept {
    if (is_operator_caller(caller)) return kOperatorRole;
    const auto* cursor = find_caller_cursor(state, caller);
    if (cursor == nullptr) return 0;
    return cursor->role == CallerRole::strategy ? kStrategyRole
         : cursor->role == CallerRole::gateway ? kGatewayRole : 0;
}

}

ApplyResult apply(TradingState& state,
                  const Command& command,
                  ApplyScratch&,
                  const VerifiedExecutionCatalog* catalog) {
    if (!execution_catalog_ready(required_execution_catalog(state, command), catalog))
        throw CatalogDependencyUnavailable{};
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

    if (std::holds_alternative<AdvanceKillEpoch>(command.body) ||
        std::holds_alternative<SetExecutorGeneration>(command.body) ||
        std::holds_alternative<SetStrategyMode>(command.body) ||
        std::holds_alternative<InstallExecutionCatalog>(command.body) ||
        std::holds_alternative<ConfigureRiskLimits>(command.body) ||
        std::holds_alternative<AssignCallerRole>(command.body) ||
        std::holds_alternative<ResolveOrderConflict>(command.body)) {
        if (!is_operator_caller(command.identity.caller_id)) {
            return result(state, ApplyCode::unauthorized_control, state.state_version);
        }
    }
    if ((roles_allowed(command.body) & role_of(state, command.identity.caller_id)) == 0) {
        return result(state, ApplyCode::unauthorized_command, state.state_version);
    }

    const VenueObservation* observation = nullptr;
    if (const auto* fill = std::get_if<ObserveFill>(&command.body)) {
        if (fill->observation.kind != VenueObservationKind::fill)
            return result(state, ApplyCode::invalid_command, state.state_version);
        observation = &fill->observation;
    }
    if (const auto* event = std::get_if<ObserveOrderEvent>(&command.body)) {
        if (event->observation.kind == VenueObservationKind::fill)
            return result(state, ApplyCode::invalid_command, state.state_version);
        observation = &event->observation;
    }
    if (observation != nullptr &&
        observation_fingerprint(*observation) != command.identity.payload_digest) {
        return result(state, ApplyCode::invalid_command, state.state_version);
    }

    if (const auto* receipt = find_receipt(state, command.identity)) {
        if (receipt->identity.payload_digest == command.identity.payload_digest) {
            return receipt->result;
        }
        return result(state, ApplyCode::duplicate_conflict, state.state_version);
    }

    if (const auto* cursor = find_caller_cursor(state, command.identity.caller_id);
        cursor != nullptr &&
        command.identity.request_sequence <= cursor->expired_below) {
        return result(state, ApplyCode::request_expired, state.state_version);
    }

    if (state.receipt_count == state.receipts.size()) {
        (void)sweep_expired_receipts(state);
    }
    if (state.receipt_count == state.receipts.size()) {
        if (const auto* acknowledgement =
                std::get_if<AcknowledgeGatewayResults>(&command.body)) {
            const auto* cursor =
                find_caller_cursor(state, command.identity.caller_id);
            if (cursor != nullptr &&
                acknowledgement->acknowledged_through.value() != 0 &&
                acknowledgement->acknowledged_through <
                    command.identity.request_sequence &&
                cursor->gateway_results_acknowledged_through <
                    acknowledgement->acknowledged_through) {
                (void)sweep_expired_receipts(
                    state, command.identity.caller_id,
                    acknowledgement->acknowledged_through);
            }
        }
    }
    if (state.receipt_count == state.receipts.size()) {
        return result(state, ApplyCode::receipt_capacity_exceeded, state.state_version);
    }
    if (find_caller_cursor(state, command.identity.caller_id) == nullptr &&
        state.caller_cursor_count == state.caller_cursors.size()) {
        return result(state, ApplyCode::receipt_capacity_exceeded, state.state_version);
    }
    if (state.state_version == std::numeric_limits<std::uint64_t>::max()) {
        return result(state, ApplyCode::state_version_exhausted, state.state_version);
    }

    if (find_caller_cursor(state, command.identity.caller_id) == nullptr) {
        state.caller_cursors[state.caller_cursor_count++] =
            CallerCursor{command.identity.caller_id, RequestSequence{},
                         RequestSequence{}};
    }
    const auto next_version = state.state_version + 1;
    const auto outcome = apply_new_command(state, command, next_version, catalog);

    state.state_version = next_version;
    state.receipts[state.receipt_count] = RequestReceipt{command.identity, outcome};
    ++state.receipt_count;
    return outcome;
}

}
