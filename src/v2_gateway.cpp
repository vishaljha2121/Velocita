#include "velocita/v2/gateway.hpp"

namespace velocita::v2 {
namespace {

// An effect is deliverable only from a state the gateway may act on.
//
// `pending` is the ordinary case. `unknown` and `reconciling` are deliberately
// NOT deliverable here: ADR 0001 requires reconciliation before resending an
// ambiguous effect, so re-sending one straight from this guard would be exactly
// the blind retry G-026 forbids. A future reconcile-then-resend path will carry
// its own evidence and is not this function's job.
//
// `claimed` and `attempted` are gateway-local by architecture 9.1.2 and the
// reducer never writes them, so meeting one in replicated state means the state
// is corrupt or forged. Refusing is the only honest response: interpreting it
// would mean trusting a value the authority cannot have produced.
[[nodiscard]] bool deliverable_from(const OutboxState state) noexcept {
    return state == OutboxState::pending;
}

// Replicated control state may forbid new external risk regardless of what the
// effect says. This mirrors the reducer's own control-authority dominance: the
// policy proposes, control disposes, and the last chance to honour that is here.
//
// Every non-normal mode refuses. Phase 5 delivers only `submit` effects, all of
// which are risk-increasing, so no mode short of `normal` permits one. When the
// emergency lane arrives in Phase 8 it will carry its own authority and will not
// route through this predicate.
[[nodiscard]] bool control_permits_new_risk(const StrategyMode mode) noexcept {
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
    if (!deliverable_from(record->state)) {
        return refuse(PreSendRefusal::effect_not_deliverable);
    }
    if (record->operation != EffectOperation::submit &&
        record->operation != EffectOperation::cancel) {
        return refuse(PreSendRefusal::unsupported_operation);
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
    if (effect->executor_generation != control.executor_generation) {
        return refuse(PreSendRefusal::stale_executor_generation);
    }
    if (effect->kill_epoch != control.kill_epoch) {
        return refuse(PreSendRefusal::stale_kill_epoch);
    }
    if (!control_permits_new_risk(control.strategy_mode)) {
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
