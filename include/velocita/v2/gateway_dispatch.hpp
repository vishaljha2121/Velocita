// The final check at the moment an order is handed to the venue transport.
#pragma once

#include "velocita/v2/gateway_authority.hpp"

namespace velocita::v2 {

struct GatewayTransportBoundary {
    Tick (*clock)(void*) noexcept{};
    void (*handoff)(BoundOrderEffect, void*){};
    void* context{};
};

[[nodiscard]] bool dispatch_gateway_send(
    GatewayAuthority& authority, const TradingState& state,
    const BoundOrderEffect& prepared, const GatewayControlView& known_control,
    std::int64_t known_term,
    const VenueCapabilityView& venue, SendQuotaView& quota,
    GatewayDeliveryState& delivery, const GatewayEvidenceState& evidence,
    const GatewayTimeouts& timeouts, const VerifiedExecutionCatalog& catalog,
    const GatewayTransportBoundary& transport);

}
