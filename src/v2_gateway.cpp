#include "velocita/v2/gateway.hpp"

namespace velocita::v2 {
namespace {

// An effect is deliverable only from a state the gateway may act on.
//
// `pending` is the ordinary case for both operations.
//
// `unknown` is deliverable for a CANCEL and not for a submit, and the asymmetry
// is the same one that governs redelivery. Reconciliation must precede resending
// an ambiguous SUBMIT because a duplicate submit creates a second order. A
// duplicate cancel creates nothing - it can only reduce exposure or leave it
// unchanged - so an unknown cancel is precisely the effect most worth sending
// again, and refusing it means the guard is strictest about the one action an
// operator needs when trying to get flat.
//
// `reconciling` stays undeliverable for both. It means final evidence is owed
// and the cluster is mid-adjudication; sending then would race the answer.
//
// `claimed` and `attempted` are gateway-local by architecture 9.1.2 and the
// reducer never writes them, so meeting one in replicated state means the state
// is corrupt or forged. Refusing is the only honest response: interpreting it
// would mean trusting a value the authority cannot have produced.
[[nodiscard]] bool deliverable_from(const OutboxState state,
                                    const EffectOperation operation) noexcept {
    if (state == OutboxState::pending) return true;
    return state == OutboxState::unknown && operation == EffectOperation::cancel;
}

// Replicated control state may forbid new external risk regardless of what the
// effect says. This mirrors the reducer's own control-authority dominance: the
// policy proposes, control disposes, and the last chance to honour that is here.
//
// The question control actually answers is about the EFFECT, not the mode alone.
//
// The earlier form asked only "does this mode permit new risk" and then applied
// that answer to every effect. That is a category error with teeth: a cancel
// creates no risk - it can only reduce exposure or leave it unchanged - so the
// predicate refused cancels in `cancel_only`, a mode whose entire purpose is
// cancelling, and in `halted`, which means originate nothing new rather than
// abandon the orders already working. The gateway refused to get out of a
// position in exactly the modes an operator reaches for to get out of one.
//
// `kill_epoch` and `executor_generation` remain the real fences and are checked
// before this. A cancel authorized under a superseded epoch is still refused;
// what changes here is only that strategy mode stops standing in for risk
// direction.
[[nodiscard]] bool control_permits(const OrderEffect& effect,
                                   const StrategyMode mode) noexcept {
    if (effect.operation == EffectOperation::cancel) return true;
    return mode == StrategyMode::normal;
}

// Representation only, never economics (architecture 9.1.4).
//
// The question this asks is deliberately narrow: can the venue express THIS
// order exactly? If not, the gateway refuses. It never rounds, substitutes a
// time-in-force, or drops post-only to make an order fit - that would be the
// executor economically modifying an order, which ADR 0001 invariant 3 forbids
// outright.
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

PreSendDecision evaluate_pre_send(const TradingState& state,
                                  const EffectId effect_id,
                                  const GatewayControlView& control,
                                  const VenueCapabilityView& venue,
                                  const SendQuotaView& quota) noexcept {
    // 1. Identity and deliverability. A record must exist and be in a state the
    //    gateway may act on before anything else is worth asking.
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

    // 2. Reconstruct the exact bytes. Doing this BEFORE the remaining checks is
    //    deliberate: the effect's own recorded authority lives in the payload,
    //    so there is nothing to compare control state against until it exists.
    //    It also means a decision that reaches step 5 already owns what it needs
    //    to send, and cannot be permitted without a payload.
    auto effect = materialize_order_effect(state, effect_id);
    if (!effect.has_value()) {
        return refuse(PreSendRefusal::payload_unreconstructable);
    }

    // 3. Control authority, which architecture 9.1.3 defines as the expiry
    //    check. An effect authorized under an older generation or kill epoch is
    //    expired by definition; no wall-clock deadline exists or is wanted.
    //    Generation is tested before kill epoch so a doubly-stale effect always
    //    reports the same reason on every host.
    // The supplied view must be CURRENT, not merely self-consistent with the
    // effect. Two matching stale values agree with each other and prove nothing
    // about either being authoritative - and this function is handed the state
    // that would have revealed the revocation, so accepting an incoherent pair
    // means the last decision before transport ignored evidence it already held.
    //
    // Verified here rather than left to callers. The defect was never that a
    // caller passed the wrong thing; it is that the signature permits an
    // incoherent pair at all, and a convention cannot exclude what a type admits.
    // `plan_reconciliation` has always performed this check, so until now the
    // delivery and reconciliation halves applied different authority rules.
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

    // 4. Venue capability. Checked after authority so a stale effect reports as
    //    stale even on a venue that could not have accepted it anyway - the more
    //    specific and more actionable reason wins.
    if (!venue_can_represent(*effect, venue)) {
        return refuse(PreSendRefusal::venue_cannot_represent_intent);
    }

    // 5. Quota last, because it is the only refusal that is purely a matter of
    //    pacing. Everything above says "this must not be sent"; this one says
    //    "not yet", and a caller may reasonably treat it differently.
    if (quota.normal_lane_remaining == 0) {
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
        // A resend keeps the same identity and increments the attempt count.
        // G-025: delivery is at least once by STABLE EffectId, so a retry must
        // never mint a new identity - that is what lets the venue deduplicate.
        attempt.sent_at = now;
        attempt.awaiting_response = true;
        // A send in flight is never provably unsent. Clearing this here means no
        // caller can leave a stale permission behind for the next tick to act on.
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
        // Deliberately leaves `resend_permitted` false. The venue answered, so
        // something may be resting there; a further send is reconciliation's
        // decision to authorise, not this planner's.
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
    // Dense compaction in place, preserving order so the planner keeps
    // considering effects in authorization order.
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

// An effect the reducer still owes an outcome on. `conflict` and `incomplete`
// are deliberately included: both hold unknown exposure, and a restart that
// treated them as finished would be deciding an outcome it cannot see.
[[nodiscard]] bool effect_is_unsettled(const OutboxState state) noexcept {
    return state != OutboxState::acknowledged && state != OutboxState::rejected;
}

}  // namespace

std::size_t begin_gateway_recovery(GatewayDeliveryState& delivery,
                                   const TradingState& state,
                                   const Tick now) noexcept {
    delivery.started = true;
    std::size_t suspect = 0;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        const auto& effect = state.outbox_effects[index];
        if (!effect_is_unsettled(effect.state)) continue;
        if (find_attempt(delivery, effect.effect_id) != nullptr) continue;
        if (delivery.attempt_count == delivery.attempts.size()) break;
        // `awaiting_response` true and `resend_permitted` false: the same pair a
        // send that left the process and has not been answered would carry. That
        // is the honest description of what the restart knows.
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
        if (attempt.awaiting_response) return false;
        const OutboxEffectRecord* effect = nullptr;
        for (std::size_t slot = 0; slot < state.outbox_effect_count; ++slot) {
            if (state.outbox_effects[slot].effect_id == attempt.effect_id) {
                effect = &state.outbox_effects[slot];
                break;
            }
        }
        // Swept, so the group it belonged to was fully resolved.
        if (effect == nullptr) continue;
        switch (effect->state) {
            case OutboxState::acknowledged:
            case OutboxState::rejected:
            case OutboxState::unknown:
            case OutboxState::reconciling:
            case OutboxState::conflict:
                // Settled, or owned by the reconciliation planner from here.
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
    // Idempotent on identity. The same observation offered twice is the same
    // obligation, not two - the shell can be handed a duplicate by a venue that
    // duplicates, and queueing it twice would propose identical bytes twice for
    // no gain.
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
        // Never proposed: go now. An obligation that waited for a deadline it had
        // never started would sit there for one commit_deadline doing nothing.
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
        // Compact rather than tombstone. The queue is a work list, and a
        // tombstoned slot would count against saturation while owing nothing.
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

    // 0a. Backpressure (P1-3). Both halves of this function produce EVIDENCE - a
    //     send produces a venue answer, an ambiguity declaration produces an
    //     `effect_unknown` command - and evidence that cannot be queued is
    //     evidence that gets dropped. Stopping is the only option that loses
    //     nothing, and it self-clears as commits are observed.
    if (evidence_queue_saturated(evidence)) return plan;

    // 0. The startup barrier (P1-2). A gateway that has not scanned replicated
    //    state cannot tell a fresh authorization from one a previous incarnation
    //    already sent, so it does nothing at all. Failing closed here is the
    //    point: the alternative failure is a second real order.
    if (!delivery.started) return plan;

    // 1. Ambiguity first. An effect whose deadline has passed must be reported
    //    unknown before anything else considers sending it, so a timed-out
    //    effect can never also appear in this tick's send list. Doing this
    //    second would let one tick both declare an outcome unknown and blindly
    //    retry it, which is exactly what G-026 forbids.
    for (std::size_t index = 0; index < delivery.attempt_count; ++index) {
        const auto& attempt = delivery.attempts[index];
        if (!attempt.awaiting_response) continue;
        if (now < attempt.sent_at + timeouts.response_deadline) continue;
        if (plan.ambiguous_count == plan.now_ambiguous.size()) break;
        plan.now_ambiguous[plan.ambiguous_count++] = attempt.effect_id;
    }

    // 2. Sends, limited by the remaining normal-lane quota. Effects are
    //    considered in arena order, which is authorization order, so delivery
    //    does not reorder economic intent.
    std::uint32_t budget = quota.normal_lane_remaining;
    for (std::size_t index = 0; index < state.outbox_effect_count && budget != 0;
         ++index) {
        const auto effect_id = state.outbox_effects[index].effect_id;

        // Never send while a previous attempt is still outstanding, and never
        // send one this tick just declared ambiguous. An ambiguous effect
        // requires reconciliation before any resend (ADR 0001), which is not
        // this planner's job to perform.
        const auto* attempt = find_attempt(delivery, effect_id);
        if (attempt != nullptr && attempt->awaiting_response) continue;
        bool just_ambiguous = false;
        for (std::size_t pending = 0; pending < plan.ambiguous_count; ++pending) {
            if (plan.now_ambiguous[pending] == effect_id) just_ambiguous = true;
        }
        if (just_ambiguous) continue;

        // A completed attempt is re-sent only when the last one provably never
        // reached the venue. Any other completed attempt may have left an order
        // resting there, so redelivering it is the gateway's own duplicate - the
        // exact failure G-025 and the Phase 5 gate exist to exclude.
        //
        // A CANCEL is the exception, and the exception is principled rather than
        // convenient. That rule exists because a duplicate SUBMIT creates a
        // second order. A duplicate cancel cannot create risk: it can only reduce
        // exposure or leave it unchanged, and the venue deduplicates it by the
        // same stable identity. So the rule was strictest exactly where its own
        // justification does not apply - and exactly when an operator is trying
        // to get flat. Redelivery here is the same EffectId, never a fresh one;
        // minting a new cancel is re-authorization and is the reducer's business.
        const bool cancel = state.outbox_effects[index].operation ==
                            EffectOperation::cancel;
        // New risk waits for the recovery barrier (G-032). A cancel does not:
        // it cannot create exposure, and a barrier that also stopped an operator
        // getting flat would be protecting the wrong thing.
        if (delivery.recovering && !cancel) continue;
        if (attempt != nullptr && !attempt->resend_permitted && !cancel) continue;
        // Retries are bounded. A venue that refuses for pacing forever would
        // otherwise be retried forever; stopping leaves the effect pending and
        // visibly unsettled, which is the fail-closed outcome.
        if (attempt != nullptr && attempt->attempts >= timeouts.max_send_attempts) {
            continue;
        }

        // The guard is the single authority on whether an effect may leave.
        // Consulting it here rather than duplicating its rules keeps one place
        // to change when a rule changes.
        const auto decision = evaluate_pre_send(state, effect_id, control, venue,
                                                SendQuotaView{budget});
        if (!decision.permitted()) continue;

        plan.to_send[plan.send_count++] = effect_id;
        --budget;
    }
    return plan;
}

}  // namespace velocita::v2

// --- reconciliation --------------------------------------------------------

namespace velocita::v2 {
namespace {

// Which orders hold exposure the cluster cannot resolve from evidence it already
// has. This is deliberately narrower than "not terminal": an order that is merely
// live and unfilled is waiting for the market, not for reconciliation, and
// querying it would be noise that hides the cases that matter.
// `submission_unknown` names the ORDER but describes its CURRENT EFFECT, and for
// a cancel-pending order that effect is the cancel. The reducer overloads the
// state deliberately - `apply_request_cancel` reads it that way too - so the only
// honest way to tell a stuck submit from a stuck cancel is to look at what the
// current effect actually is. Reading the order state alone sent the gateway off
// to reconcile a submission that was never in doubt.
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
            // Redelivery FIRST, reconciliation second. A cancel may be resent
            // under its own identity without proof, so the cheap idempotent
            // action is tried before the round trip that would most likely just
            // tell us to try it. Reconciliation takes over when redelivery has
            // stopped working, which is the only point at which the query
            // actually adds information.
            return delivery_exhausted(order, delivery, timeouts)
                       ? ReconciliationNeed::cancel_unconfirmed
                       : ReconciliationNeed::none;
        case ClusterOrderState::reconciling:
            return ReconciliationNeed::cancel_unconfirmed;
        case ClusterOrderState::dispatch_authorized:
            // Authorized but never delivered. This is a target only once the
            // send attempts are SPENT: an order still waiting its turn to be
            // sent must not be queried, or the query races the send and the
            // venue is asked about an order the gateway is about to place.
            return delivery_exhausted(order, delivery, timeouts)
                       ? ReconciliationNeed::delivery_exhausted
                       : ReconciliationNeed::none;
        default:
            // A `live` order with no fill is deliberately NOT a target. Without a
            // clock there is no way to tell "its fill was lost" from "it has not
            // traded yet", so querying every resting order would be continuous
            // noise. A resting order is reconciled when it is cancelled, or at
            // session recovery under G-032, which is a different trigger.
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
    // Counting accepted observations for the order is enough to detect movement,
    // which is all the barrier needs. It deliberately does not try to order the
    // evidence itself - that is the venue's coordinate to supply, not ours to
    // invent.
    std::uint64_t seen = 0;
    const ClusterOrder* owner = nullptr;
    for (std::size_t index = 0; index < state.order_count; ++index) {
        if (state.orders[index].cluster_order_id == cluster_order_id) {
            owner = &state.orders[index];
            break;
        }
    }
    if (owner == nullptr) return 0;
    for (std::size_t index = 0; index < state.receipt_count; ++index) {
        const auto& result = state.receipts[index].result;
        if (result.code != ApplyCode::applied) continue;
        if (result.detail_kind != ApplyResultDetailKind::fill &&
            result.detail_kind != ApplyResultDetailKind::venue_event) {
            continue;
        }
        if (result.decision_id != owner->decision_id) continue;
        ++seen;
    }
    return seen;
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
    // Exhaustion is checked first and applies even to an answer that arrived,
    // because the alternative is to keep querying forever whenever the venue
    // keeps giving partial answers.
    const bool exhausted =
        attempt != nullptr && attempt->attempts >= timeouts.max_query_attempts;

    switch (answer.status) {
        case VenueQueryStatus::unavailable:
            // Silence. Nothing may be concluded from it - in particular NOT that
            // the order does not exist, which is the inference that turns a
            // network problem into a duplicate order.
            return exhausted ? QueryDisposition::exhausted
                             : QueryDisposition::unavailable;
        case VenueQueryStatus::not_found:
        case VenueQueryStatus::known:
            // An answer that does not cover its window is a partial view, and a
            // partial view of a fill history is indistinguishable from a short
            // one. Submitting it would let the cluster book "the venue says two
            // filled" when the venue only meant "two so far, in this page".
            if (!answer.window_complete) {
                return exhausted ? QueryDisposition::exhausted
                                 : QueryDisposition::incomplete;
            }
            // THE ORDERING BARRIER. `window_complete` orders this answer against
            // other queries and says nothing about fills that arrived while it was
            // in flight. If the cluster accepted evidence for this order in the
            // meantime, the snapshot predates evidence the cluster already holds,
            // and comparing the two would be comparing two different moments -
            // which is how a stale read becomes a permanent conflict.
            //
            // Ask again rather than conclude. Releasing risk or declaring a
            // conflict both require an answer that is comparable, and this one
            // demonstrably is not.
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

    // A superseded gateway must not write venue evidence into consensus, so
    // generation staleness stops reconciliation outright. Kill epoch and strategy
    // mode deliberately do NOT: reconciliation originates no risk, and a halted
    // cluster holding a blocked slot is precisely when resolving it matters most.
    if (control.executor_generation != state.executor_generation) return plan;
    // No `supports_query` capability flag, deliberately. A venue that cannot be
    // queried answers `unavailable`, which `classify_query` already handles
    // correctly and which ends in `exhausted` rather than in a guess. A flag here
    // would duplicate a fact the transport reports anyway, and promoting it into
    // the canonical `VenueCapabilities` artifact would be a byte-contract change
    // bought for nothing.
    (void)venue;

    for (std::size_t index = 0; index < state.order_count; ++index) {
        const auto& order = state.orders[index];
        const auto need = need_of(state, order, delivery, timeouts);
        if (need == ReconciliationNeed::none) continue;

        const auto* attempt = find_reconciliation(reconciliation,
                                                  order.cluster_order_id);
        if (attempt != nullptr) {
            // One outstanding query at a time. A second query for the same order
            // could answer out of order and let a stale view overwrite a fresh
            // one, which is a conflict the cluster would have to adjudicate for
            // no reason.
            if (attempt->awaiting_answer) continue;
            if (attempt->attempts >= timeouts.max_query_attempts) continue;
            if (now < attempt->last_query_at + timeouts.query_interval) continue;
        }
        if (plan.query_count == plan.to_query.size()) break;
        plan.to_query[plan.query_count++] = {order.cluster_order_id, order.effect_id,
                                             need};
    }
    return plan;
}

}  // namespace velocita::v2
