#pragma once

#include "velocita/v2/gateway_authority.hpp"

namespace velocita::v2 {

// Called synchronously by the transport owner, AFTER any preparation/queue wait.
// handoff takes an owned value. Once entered, exceptions/timeouts are ambiguous,
// not evidence of an unsent request. A real adapter must not enqueue unchecked work.
// Single-owner, non-reentrant call: clock may sample time but must not mutate any
// admission inputs (authority, state, controls, delivery, quota, evidence/catalog).
// These trusted callbacks are not a sandbox or an authenticated IPC boundary.
struct GatewayTransportBoundary {
    Tick (*clock)(void*) noexcept{};
    void (*handoff)(BoundOrderEffect, void*){};
    void* context{};
};

// Rechecks current eligibility, consumes proof and records ambiguity BEFORE handoff.
// False means the callback was not entered. This is not a reusable permission.
[[nodiscard]] bool dispatch_gateway_send(
    GatewayAuthority& authority, const TradingState& state,
    const BoundOrderEffect& prepared, const GatewayControlView& known_control,
    std::int64_t known_term,
    const VenueCapabilityView& venue, SendQuotaView& quota,
    GatewayDeliveryState& delivery, const GatewayEvidenceState& evidence,
    const GatewayTimeouts& timeouts, const VerifiedExecutionCatalog& catalog,
    const GatewayTransportBoundary& transport);

}  // namespace velocita::v2
