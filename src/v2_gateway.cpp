#include "velocita/v2/gateway.hpp"

namespace velocita::v2 {
namespace {

// Unknown cancels may retry without increasing risk. Unknown submits need
// reconciliation; reconciling and gateway-local states remain undeliverable.
[[nodiscard]] bool deliverable_from(const OutboxState state,
                                    const EffectOperation operation) noexcept {
    if (state == OutboxState::pending) return true;
    return state == OutboxState::unknown && operation == EffectOperation::cancel;
}

// Cancels remain eligible in halted/cancel-only mode; generation and epoch still fence them.
[[nodiscard]] bool control_permits(const OrderEffect& effect,
                                   const StrategyMode mode) noexcept {
    if (effect.operation == EffectOperation::cancel) return true;
    return mode == StrategyMode::normal;
}

// Reject unrepresentable intent; never alter order economics to fit the venue.
[[nodiscard]] bool venue_can_represent(const OrderEffect& effect,
                                       const VenueCapabilityView& venue) noexcept {
    if (venue.venue_id != effect.venue_id) return false;
    if (effect.post_only == PostOnlyMode::required && !venue.supports_post_only) {
        return false;
    }
    if (effect.time_in_force == TimeInForce::good_til_cancel &&
        !venue.supports_good_til_cancel) {
        return false;
    }
    return true;
}

[[nodiscard]] PreSendDecision refuse(const PreSendRefusal reason) noexcept {
    return {reason, std::nullopt};
}

}  // namespace

bool renew_authority(GatewayControlView& control, const AuthorityReading& reading,
                     const Tick now) noexcept {
    // A non-authoritative read is not a weaker renewal, it is none: the cached
    // confirmation time must stay exactly where it was, or a partitioned
    // gateway polling a follower would hold its lease open forever.
    if (!reading.authoritative) return false;
    // Time running backwards is not evidence of freshness. Refusing rather than
    // clamping, because a tick that moved backwards means the caller's clock is
    // wrong and extending authority on it is the one thing not to do.
    if (now < control.confirmed_at) return false;

    control.executor_generation = reading.executor_generation;
    control.kill_epoch = reading.kill_epoch;
    control.strategy_mode = reading.strategy_mode;
    control.confirmed_at = now;
    control.now = now;
    return true;
}

PreSendDecision evaluate_pre_send(const TradingState& state,
                                  const EffectId effect_id,
                                  const GatewayControlView& control,
                                  const VenueCapabilityView& venue,
                                  const SendQuotaView& quota) noexcept {
    const OutboxEffectRecord* record = nullptr;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].effect_id == effect_id) {
            record = &state.outbox_effects[index];
            break;
        }
    }
    if (record == nullptr) return refuse(PreSendRefusal::effect_not_found);
    if (record->operation != EffectOperation::submit &&
        record->operation != EffectOperation::cancel) {
        return refuse(PreSendRefusal::unsupported_operation);
    }
    if (!deliverable_from(record->state, record->operation)) {
        return refuse(PreSendRefusal::effect_not_deliverable);
    }

    // Canonical payload carries the authority needed by the remaining checks.
    auto effect = materialize_order_effect(state, effect_id);
    if (!effect.has_value()) {
        return refuse(PreSendRefusal::payload_unreconstructable);
    }

    // Matching stale cache/effect values are insufficient: verify current replicated
    // authority first. Generation precedes epoch for deterministic refusal priority.
    if (control.executor_generation != state.executor_generation) {
        return refuse(PreSendRefusal::stale_executor_generation);
    }
    if (control.kill_epoch != state.kill_epoch) {
        return refuse(PreSendRefusal::stale_kill_epoch);
    }
    if (control.strategy_mode != state.strategy_mode) {
        return refuse(PreSendRefusal::control_forbids_new_risk);
    }

    if (effect->executor_generation != control.executor_generation) {
        return refuse(PreSendRefusal::stale_executor_generation);
    }
    if (effect->kill_epoch != control.kill_epoch) {
        return refuse(PreSendRefusal::stale_kill_epoch);
    }
    if (!control_permits(*effect, control.strategy_mode)) {
        return refuse(PreSendRefusal::control_forbids_new_risk);
    }

    // THE LEASE, and only for effects that increase risk.
    //
    // Placed after the fences it complements rather than before them: when the
    // gateway CAN see the cluster, a stale generation is the more specific
    // answer and the more useful one to report. The lease is what remains true
    // when it cannot see the cluster at all.
    //
    // A cancel is deliberately exempt. A lapsed lease means this gateway may no
    // longer be the authority, and the safe response to that is to stop opening
    // exposure - not to stop closing it. Refusing cancels here would trap risk
    // at precisely the moment the operator most needs it gone, which inverts
    // what the lease is for.
    const bool reducing = reduces_risk(effect->operation);
    if (!reducing && !control.lease_is_live()) {
        return refuse(PreSendRefusal::authority_lease_expired);
    }

    if (!venue_can_represent(*effect, venue)) {
        return refuse(PreSendRefusal::venue_cannot_represent_intent);
    }

    // Risk-reducing effects may fall back to the reserved lane; nothing else
    // may touch it. That asymmetry IS the reservation - a lane both lanes can
    // spend is one lane with two names.
    if (quota.normal_lane_remaining == 0 &&
        !(reducing && quota.emergency_lane_remaining > 0)) {
        return refuse(PreSendRefusal::quota_exhausted);
    }

    return {PreSendRefusal::none, std::move(effect)};
}

}  // namespace velocita::v2

namespace velocita::v2 {

const DeliveryAttempt* find_attempt(const GatewayDeliveryState& delivery,
                                    const EffectId effect_id) noexcept {
    for (std::size_t index = 0; index < delivery.attempt_count; ++index) {
        if (delivery.attempts[index].effect_id == effect_id) {
            return &delivery.attempts[index];
        }
    }
    return nullptr;
}

bool record_send(GatewayDeliveryState& delivery, const EffectId effect_id,
                 const Tick now) noexcept {
    for (std::size_t index = 0; index < delivery.attempt_count; ++index) {
        auto& attempt = delivery.attempts[index];
        if (attempt.effect_id != effect_id) continue;
        // Retry the stable EffectId so the venue can deduplicate.
        attempt.sent_at = now;
        attempt.awaiting_response = true;
        // An in-flight send invalidates any prior proof that it was unsent.
        attempt.resend_permitted = false;
        ++attempt.attempts;
        return true;
    }
    if (delivery.attempt_count == delivery.attempts.size()) return false;
    delivery.attempts[delivery.attempt_count++] = {effect_id, now, 1, true, false};
    return true;
}

bool record_response(GatewayDeliveryState& delivery,
                     const EffectId effect_id) noexcept {
    for (std::size_t index = 0; index < delivery.attempt_count; ++index) {
        if (delivery.attempts[index].effect_id != effect_id) continue;
        delivery.attempts[index].awaiting_response = false;

        return true;
    }
    return false;
}

std::size_t reclaim_delivery_attempts(GatewayDeliveryState& delivery,
                                      const TradingState& state) noexcept {
    const auto held = [&state](const EffectId effect_id) noexcept {
        for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
            if (state.outbox_effects[index].effect_id == effect_id) return true;
        }
        return false;
    };
    // Preserve authorization order while compacting; borrowed pointers are invalidated.
    std::size_t kept = 0;
    for (std::size_t index = 0; index < delivery.attempt_count; ++index) {
        if (!held(delivery.attempts[index].effect_id)) continue;
        delivery.attempts[kept++] = delivery.attempts[index];
    }
    const auto dropped = delivery.attempt_count - kept;
    for (std::size_t index = kept; index < delivery.attempt_count; ++index) {
        delivery.attempts[index] = {};
    }
    delivery.attempt_count = kept;
    return dropped;
}

std::size_t reclaim_reconciliation_attempts(
    GatewayReconciliationState& reconciliation,
    const TradingState& state) noexcept {
    const auto held = [&state](const ClusterOrderId order) noexcept {
        for (std::size_t index = 0; index < state.order_count; ++index) {
            if (state.orders[index].cluster_order_id == order) return true;
        }
        return false;
    };
    std::size_t kept = 0;
    for (std::size_t index = 0; index < reconciliation.attempt_count; ++index) {
        if (!held(reconciliation.attempts[index].cluster_order_id)) continue;
        reconciliation.attempts[kept++] = reconciliation.attempts[index];
    }
    const auto dropped = reconciliation.attempt_count - kept;
    for (std::size_t index = kept; index < reconciliation.attempt_count; ++index) {
        reconciliation.attempts[index] = {};
    }
    reconciliation.attempt_count = kept;
    return dropped;
}

bool record_unsent(GatewayDeliveryState& delivery,
                   const EffectId effect_id) noexcept {
    for (std::size_t index = 0; index < delivery.attempt_count; ++index) {
        auto& attempt = delivery.attempts[index];
        if (attempt.effect_id != effect_id) continue;
        attempt.awaiting_response = false;
        attempt.resend_permitted = true;
        return true;
    }
    return false;
}

namespace {

// Incomplete effects are non-deliverable and reclaimable; a synthetic in-flight
// attempt would create a recovery barrier no legitimate response can clear.
[[nodiscard]] bool effect_requires_startup_recovery(
    const OutboxState state) noexcept {
    return state != OutboxState::acknowledged &&
           state != OutboxState::rejected &&
           state != OutboxState::incomplete;
}

}  // namespace

std::size_t begin_gateway_recovery(GatewayDeliveryState& delivery,
                                   const TradingState& state,
                                   const Tick now) noexcept {
    delivery.started = true;
    std::size_t suspect = 0;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        const auto& effect = state.outbox_effects[index];
        if (!effect_requires_startup_recovery(effect.state)) continue;
        if (find_attempt(delivery, effect.effect_id) != nullptr) continue;
        if (delivery.attempt_count == delivery.attempts.size()) break;

        delivery.attempts[delivery.attempt_count++] = {effect.effect_id, now, 1,
                                                       true, false, true};
        ++suspect;
    }
    delivery.recovering = suspect != 0;
    return suspect;
}

bool recovery_complete(const GatewayDeliveryState& delivery,
                       const TradingState& state) noexcept {
    for (std::size_t index = 0; index < delivery.attempt_count; ++index) {
        const auto& attempt = delivery.attempts[index];
        if (!attempt.from_recovery) continue;
        const OutboxEffectRecord* effect = nullptr;
        for (std::size_t slot = 0; slot < state.outbox_effect_count; ++slot) {
            if (state.outbox_effects[slot].effect_id == attempt.effect_id) {
                effect = &state.outbox_effects[slot];
                break;
            }
        }
        // Absence means the fully resolved group was swept.
        if (effect == nullptr) continue;
        switch (effect->state) {
            case OutboxState::acknowledged:
            case OutboxState::rejected:
            case OutboxState::unknown:
            case OutboxState::reconciling:
            case OutboxState::conflict:
            case OutboxState::incomplete:
                break;
            default:
                return false;
        }
    }
    return true;
}

bool record_evidence_obligation(GatewayEvidenceState& evidence,
                                const CommandIdentity& identity,
                                const VenueObservation& observation,
                                const EffectId effect_id, const Tick now) noexcept {
    if (find_obligation(evidence, identity) != nullptr) return true;
    if (evidence.obligation_count == evidence.obligations.size()) return false;
    evidence.obligations[evidence.obligation_count++] = {
        identity, observation, effect_id, now, 0, 0, false};
    return true;
}

EvidencePlan plan_evidence(const GatewayEvidenceState& evidence,
                           const GatewayTimeouts& timeouts,
                           const Tick now) noexcept {
    EvidencePlan plan;
    for (std::size_t index = 0; index < evidence.obligation_count; ++index) {
        const auto& obligation = evidence.obligations[index];

        if (obligation.attempts != 0) {
            if (!obligation.awaiting_commit) {
                // A result was lost rather than merely delayed; retry at once.
            } else if (now < obligation.last_proposed_at + timeouts.commit_deadline) {
                continue;
            }
        }
        if (plan.proposal_count == plan.to_propose.size()) break;
        plan.to_propose[plan.proposal_count++] = obligation.identity;
    }
    return plan;
}

bool record_proposal(GatewayEvidenceState& evidence,
                     const CommandIdentity& identity, const Tick now) noexcept {
    for (std::size_t index = 0; index < evidence.obligation_count; ++index) {
        auto& obligation = evidence.obligations[index];
        if (obligation.identity != identity) continue;
        obligation.last_proposed_at = now;
        obligation.awaiting_commit = true;
        ++obligation.attempts;
        return true;
    }
    return false;
}

bool discharge_evidence(GatewayEvidenceState& evidence,
                        const CommandIdentity& identity) noexcept {
    for (std::size_t index = 0; index < evidence.obligation_count; ++index) {
        if (evidence.obligations[index].identity != identity) continue;

        for (std::size_t slot = index + 1; slot < evidence.obligation_count; ++slot) {
            evidence.obligations[slot - 1] = evidence.obligations[slot];
        }
        evidence.obligations[--evidence.obligation_count] = {};
        return true;
    }
    return false;
}

bool evidence_queue_saturated(const GatewayEvidenceState& evidence) noexcept {
    return evidence.obligation_count == evidence.obligations.size();
}

const EvidenceObligation* find_obligation(const GatewayEvidenceState& evidence,
                                          const CommandIdentity& identity) noexcept {
    for (std::size_t index = 0; index < evidence.obligation_count; ++index) {
        if (evidence.obligations[index].identity == identity) {
            return &evidence.obligations[index];
        }
    }
    return nullptr;
}

DeliveryPlan plan_delivery(const TradingState& state,
                           const GatewayDeliveryState& delivery,
                           const GatewayEvidenceState& evidence,
                           const GatewayControlView& control,
                           const VenueCapabilityView& venue,
                           const SendQuotaView& quota,
                           const GatewayTimeouts& timeouts,
                           const Tick now) noexcept {
    DeliveryPlan plan;

    // Sends and ambiguity declarations both generate evidence that must be retained.
    if (evidence_queue_saturated(evidence)) return plan;

    // Without the startup scan, pending effects may have been sent by a prior process.
    if (!delivery.started) return plan;

    // Classify timeouts before sends so this tick cannot also retry an ambiguous effect.
    for (std::size_t index = 0; index < delivery.attempt_count; ++index) {
        const auto& attempt = delivery.attempts[index];
        if (!attempt.awaiting_response) continue;
        if (now < attempt.sent_at + timeouts.response_deadline) continue;
        if (plan.ambiguous_count == plan.now_ambiguous.size()) break;
        plan.now_ambiguous[plan.ambiguous_count++] = attempt.effect_id;
    }

    // Arena order is authorization order.
    std::uint32_t budget = quota.normal_lane_remaining;
    for (std::size_t index = 0; index < state.outbox_effect_count && budget != 0;
         ++index) {
        const auto effect_id = state.outbox_effects[index].effect_id;

        const auto* attempt = find_attempt(delivery, effect_id);
        if (attempt != nullptr && attempt->awaiting_response) continue;
        bool just_ambiguous = false;
        for (std::size_t pending = 0; pending < plan.ambiguous_count; ++pending) {
            if (plan.now_ambiguous[pending] == effect_id) just_ambiguous = true;
        }
        if (just_ambiguous) continue;

        // Submit retries need proof the prior send was unsent. Cancels may retry under
        // the same EffectId because they cannot add exposure.
        const bool cancel = state.outbox_effects[index].operation ==
                            EffectOperation::cancel;

        if (delivery.recovering && !cancel) continue;
        if (attempt != nullptr && !attempt->resend_permitted && !cancel) continue;

        if (attempt != nullptr && attempt->attempts >= timeouts.max_send_attempts) {
            continue;
        }

        const auto decision = evaluate_pre_send(state, effect_id, control, venue,
                                                SendQuotaView{budget});
        if (!decision.permitted()) continue;

        plan.to_send[plan.send_count++] = effect_id;
        --budget;
    }
    return plan;
}

}  // namespace velocita::v2

namespace velocita::v2 {
namespace {

// submission_unknown can describe either operation; inspect the current effect
// to distinguish an ambiguous submit from an ambiguous cancel.
[[nodiscard]] EffectOperation current_operation(const TradingState& state,
                                                const ClusterOrder& order) noexcept {
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].effect_id == order.effect_id) {
            return state.outbox_effects[index].operation;
        }
    }
    return EffectOperation::submit;
}

[[nodiscard]] bool delivery_exhausted(const ClusterOrder& order,
                                      const GatewayDeliveryState& delivery,
                                      const GatewayTimeouts& timeouts) noexcept {
    const auto* attempt = find_attempt(delivery, order.effect_id);
    return attempt != nullptr && attempt->attempts >= timeouts.max_send_attempts;
}

[[nodiscard]] ReconciliationNeed need_of(const TradingState& state,
                                        const ClusterOrder& order,
                                        const GatewayDeliveryState& delivery,
                                        const GatewayTimeouts& timeouts) noexcept {
    switch (order.state) {
        case ClusterOrderState::submission_unknown:
            if (current_operation(state, order) != EffectOperation::cancel) {
                return ReconciliationNeed::submission_unknown;
            }
            // Try the idempotent cancel retry first; reconcile after its send budget is spent.
            return delivery_exhausted(order, delivery, timeouts)
                       ? ReconciliationNeed::cancel_unconfirmed
                       : ReconciliationNeed::none;
        case ClusterOrderState::reconciling:
            return ReconciliationNeed::cancel_unconfirmed;
        case ClusterOrderState::dispatch_authorized:
            // Wait for send exhaustion so an absence query cannot race a scheduled send.
            return delivery_exhausted(order, delivery, timeouts)
                       ? ReconciliationNeed::delivery_exhausted
                       : ReconciliationNeed::none;
        case ClusterOrderState::acknowledged:
        case ClusterOrderState::live:
        case ClusterOrderState::partially_filled:
            // ADR 0008. Confirmed, not assumed. Nothing here is known to be
            // wrong, and that is exactly the point: an order whose cancellation
            // event was lost looks identical to one that is fine.
            return ReconciliationNeed::unconfirmed_resting;
        default:
            return ReconciliationNeed::none;
    }
}

}  // namespace

const ReconciliationAttempt* find_reconciliation(
    const GatewayReconciliationState& reconciliation,
    const ClusterOrderId cluster_order_id) noexcept {
    for (std::size_t index = 0; index < reconciliation.attempt_count; ++index) {
        if (reconciliation.attempts[index].cluster_order_id == cluster_order_id) {
            return &reconciliation.attempts[index];
        }
    }
    return nullptr;
}

std::uint64_t accepted_evidence_watermark(
    const TradingState& state, const ClusterOrderId cluster_order_id) noexcept {
    for (std::size_t index = 0; index < state.order_count; ++index) {
        if (state.orders[index].cluster_order_id == cluster_order_id) {
            return state.orders[index].last_evidence_state_version;
        }
    }
    return 0;
}

bool record_query(GatewayReconciliationState& reconciliation,
                  const ClusterOrderId cluster_order_id, const Tick now,
                  const std::uint64_t evidence_at_query) noexcept {
    for (std::size_t index = 0; index < reconciliation.attempt_count; ++index) {
        auto& attempt = reconciliation.attempts[index];
        if (attempt.cluster_order_id != cluster_order_id) continue;
        attempt.last_query_at = now;
        attempt.awaiting_answer = true;
        attempt.evidence_at_query = evidence_at_query;
        ++attempt.attempts;
        return true;
    }
    if (reconciliation.attempt_count == reconciliation.attempts.size()) return false;
    reconciliation.attempts[reconciliation.attempt_count++] = {
        cluster_order_id, now, 1, true, evidence_at_query};
    return true;
}

bool record_answer(GatewayReconciliationState& reconciliation,
                   const ClusterOrderId cluster_order_id) noexcept {
    for (std::size_t index = 0; index < reconciliation.attempt_count; ++index) {
        auto& attempt = reconciliation.attempts[index];
        if (attempt.cluster_order_id != cluster_order_id) continue;
        attempt.awaiting_answer = false;
        return true;
    }
    return false;
}

QueryDisposition classify_query(const VenueQueryView& answer,
                                const ReconciliationAttempt* const attempt,
                                const std::uint64_t evidence_now,
                                const GatewayTimeouts& timeouts) noexcept {
    const bool exhausted =
        attempt != nullptr && attempt->attempts >= timeouts.max_query_attempts;

    switch (answer.status) {
        case VenueQueryStatus::unavailable:

            return exhausted ? QueryDisposition::exhausted
                             : QueryDisposition::unavailable;
        case VenueQueryStatus::not_found:
        case VenueQueryStatus::known:

            if (!answer.window_complete ||
                !answer.ordered_after_relevant_sends) {
                return exhausted ? QueryDisposition::exhausted
                                 : QueryDisposition::incomplete;
            }
            // Concurrent accepted evidence invalidates the snapshot comparison even when
            // the venue reports complete window coverage.
            if (attempt != nullptr && evidence_now != attempt->evidence_at_query) {
                return exhausted ? QueryDisposition::exhausted
                                 : QueryDisposition::incomplete;
            }
            return QueryDisposition::submit;
    }
    return QueryDisposition::unavailable;
}

ReconciliationPlan plan_reconciliation(const TradingState& state,
                                       const GatewayReconciliationState& reconciliation,
                                       const GatewayDeliveryState& delivery,
                                       const GatewayControlView& control,
                                       const VenueCapabilityView& venue,
                                       const GatewayTimeouts& timeouts,
                                       const Tick now) noexcept {
    ReconciliationPlan plan;

    // Generation fences the writer. Kill epoch/mode permit reconciliation of existing risk.
    if (control.executor_generation != state.executor_generation) return plan;
    // Unavailable query support is a transport outcome, handled by classify_query.
    (void)venue;

    for (std::size_t index = 0; index < state.order_count; ++index) {
        const auto& order = state.orders[index];
        const auto need = need_of(state, order, delivery, timeouts);
        if (need == ReconciliationNeed::none) continue;

        const auto* attempt = find_reconciliation(reconciliation,
                                                  order.cluster_order_id);
        const bool heartbeat = need == ReconciliationNeed::unconfirmed_resting;
        if (attempt != nullptr) {
            // One outstanding query per order prevents answers from arriving out of order.
            if (attempt->awaiting_answer) continue;
            // THE ATTEMPT CAP DOES NOT APPLY TO THE HEARTBEAT. Giving up is right
            // for a need that names something already wrong: stop hammering a
            // stuck venue, leave the slot blocked, let an operator see it. Giving
            // up on confirming a resting order instead returns the cluster to
            // ASSUMING, which is the silent state ADR 0008 exists to end. The
            // bound on this need is its RATE, not its count.
            if (!heartbeat && attempt->attempts >= timeouts.max_query_attempts) {
                continue;
            }
            const auto interval = heartbeat ? timeouts.resting_confirm_interval
                                            : timeouts.query_interval;
            if (now < attempt->last_query_at + interval) continue;
        }
        if (plan.query_count == plan.to_query.size()) break;
        plan.to_query[plan.query_count++] = {order.cluster_order_id, order.effect_id,
                                             need};
    }
    return plan;
}

}  // namespace velocita::v2
