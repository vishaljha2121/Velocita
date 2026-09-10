#pragma once

#include "velocita/v2/kernel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace velocita::v2 {

// The execution gateway's final pre-send guard (G-029), as a PURE FUNCTION.
//
// Phase 5 is the first phase to leave the deterministic reducer: it owns a
// clock, a transport, and an event loop. The strategy that keeps the project's
// discipline is to push all three out of the decidable core, so what remains
// here reads no clock, performs no I/O, allocates nothing, and mutates nothing.
// The shell around it does the untestable work and is kept trivial.
//
// This follows `OwnerLoopScheduler` from V1.1, which already takes `now` as a
// parameter rather than reading a clock. That precedent is why its scheduling
// decisions were testable at all, and the same reason applies here.

// Why an effect was refused before send. Every value is a deliberate,
// reportable outcome: a refusal is a successful evaluation, never an error.
enum class PreSendRefusal : std::uint8_t {
    none = 0,
    effect_not_found,
    // The effect is settled, or is in a state the gateway may not deliver from.
    effect_not_deliverable,
    // Architecture 9.1.3: expiry IS control-generation staleness. There is no
    // wall-clock deadline on an effect and none will be added.
    stale_executor_generation,
    stale_kill_epoch,
    // Replicated control state forbids new external risk right now.
    control_forbids_new_risk,
    // Native replacement is refused at authorization (G-018), so a replace
    // effect cannot exist. Refusing here too keeps the guard total rather than
    // relying on an upstream invariant it cannot see.
    unsupported_operation,
    // The venue cannot represent the exact economic intent. Architecture 9.1.4:
    // lowering is representation only, so the gateway refuses rather than
    // sending an approximation.
    venue_cannot_represent_intent,
    quota_exhausted,
    // The effect's canonical bytes could not be reconstructed from retained
    // provenance, so there is nothing safe to send.
    payload_unreconstructable,
};

// What a venue can faithfully represent. Anything the gateway cannot express
// EXACTLY is refused, never approximated.
struct VenueCapabilityView {
    VenueId venue_id{};
    VenueCapabilitiesVersion capabilities_version{};
    bool supports_post_only{};
    bool supports_good_til_cancel{};

    constexpr auto operator<=>(const VenueCapabilityView&) const noexcept = default;
};

// The gateway's cached view of replicated control authority. It is a disposable
// copy by the section 2 authority table; the cluster remains authoritative, and
// the guard compares this against the effect rather than trusting either alone.
struct GatewayControlView {
    ExecutorGeneration executor_generation{};
    KillEpoch kill_epoch{};
    StrategyMode strategy_mode{StrategyMode::halted};

    constexpr auto operator<=>(const GatewayControlView&) const noexcept = default;
};

// Remaining send allowance. Bounded by G-034; the normal lane is what this
// guard consumes, and emergency capacity is deliberately not modelled here
// because Phase 5 builds the lane without claiming its Phase 8 semantics.
struct SendQuotaView {
    std::uint32_t normal_lane_remaining{};

    constexpr auto operator<=>(const SendQuotaView&) const noexcept = default;
};

// A permitted decision carries the exact bytes to send. A refused one carries
// nothing, so a caller cannot accidentally send a refused effect: the payload is
// simply absent.
struct PreSendDecision {
    PreSendRefusal refusal{PreSendRefusal::none};
    std::optional<OrderEffect> effect{};

    [[nodiscard]] constexpr bool permitted() const noexcept {
        return refusal == PreSendRefusal::none && effect.has_value();
    }
};

// Pure, bounded, allocation-free apart from the returned value. Reads no clock.
//
// Ordering is fixed so the reported reason is stable on every host: identity and
// deliverability, then control authority, then venue capability, then quota.
// Control authority precedes capability because a stale generation must be
// reported as stale even on a venue that could not have taken the order anyway.
[[nodiscard]] PreSendDecision evaluate_pre_send(
    const TradingState& state,
    EffectId effect_id,
    const GatewayControlView& control,
    const VenueCapabilityView& venue,
    const SendQuotaView& quota) noexcept;

static_assert(std::is_trivially_copyable_v<VenueCapabilityView>);
static_assert(std::is_trivially_copyable_v<GatewayControlView>);
static_assert(std::is_trivially_copyable_v<SendQuotaView>);

}  // namespace velocita::v2
