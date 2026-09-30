#include "velocita/v2/gateway_dispatch.hpp"

namespace velocita::v2 {

bool dispatch_gateway_send(
    GatewayAuthority& authority, const TradingState& state,
    const BoundOrderEffect& prepared, const GatewayControlView& known_control,
    const std::int64_t known_term,
    const VenueCapabilityView& venue, SendQuotaView& quota,
    GatewayDeliveryState& delivery, const GatewayEvidenceState& evidence,
    const GatewayTimeouts& timeouts, const VerifiedExecutionCatalog& catalog,
    const GatewayTransportBoundary& transport) {
    const auto refuse = [&] { authority.abandon_request(); return false; };
    authority.observe_fence(known_control, known_term);
    if (!transport.clock || !transport.handoff || authority.all_transport_stopped())
        return refuse();

    const auto now = transport.clock(transport.context);
    auto candidate_control = known_control;
    // Candidate age is not authority. Freshness comes only from the private
    // request-start deadline checked by consume immediately before handoff.
    candidate_control.now = now;
    candidate_control.confirmed_at = now;
    candidate_control.lease_bound = 1;
    const auto plan = plan_delivery(state, delivery, evidence, candidate_control,
                                    venue, quota, timeouts, now, &catalog);
    bool eligible = false;
    for (std::size_t i = 0; i < plan.send_count; ++i)
        eligible |= plan.to_send[i] == prepared.effect.effect_id;
    if (!eligible) return refuse();
    const auto candidate = evaluate_delivery_candidate(state, prepared.effect.effect_id,
        candidate_control, venue, quota, &catalog);
    if (!candidate.permitted()) return refuse();
    const BoundOrderEffect actual{*candidate.effect, candidate.catalog_root,
        candidate.instrument_spec_version, candidate.venue_capabilities_version};
    if (actual != prepared || delivery.attempt_count > delivery.attempts.size()) return refuse();
    if (!find_attempt(delivery, actual.effect.effect_id) &&
        delivery.attempt_count == delivery.attempts.size()) return refuse();

    // Sample again after validation; queued/prepared work did not buy a new age.
    const auto at_handoff = transport.clock(transport.context);
    if (at_handoff < now) { authority.stop_new_risk(); return refuse(); }
    if (actual.effect.operation == EffectOperation::cancel) {
        if (!authority.permits_committed_cancel(actual, known_control)) return refuse();
    } else if (!authority.consume(actual, known_control, at_handoff)) {
        return refuse();
    }
    if (!record_send(delivery, actual.effect.effect_id, at_handoff)) return refuse();
    if (quota.normal_lane_remaining != 0) --quota.normal_lane_remaining;
    else --quota.emergency_lane_remaining;

    // No callback runs between recording uncertainty/consumption and this handoff.
    // A thrown transport error leaves awaiting_response set; it cannot restore proof.
    transport.handoff(actual, transport.context);
    return true;
}

}  // namespace velocita::v2
