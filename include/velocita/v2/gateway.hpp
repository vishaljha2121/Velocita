#pragma once

#include "velocita/v2/kernel.hpp"

#include <array>
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

// --- delivery planning -----------------------------------------------------
//
// The second half of the same discipline. `evaluate_pre_send` answers "may this
// one effect go?"; `plan_delivery` answers "what should the gateway do right
// now?" - and answers it as a pure function of replicated state, gateway-local
// bookkeeping, and an injected tick.
//
// The shell that surrounds it reads the clock, calls the transport, and drains
// responses. It contains no decisions, which is what keeps the fault matrix
// deterministic while the gateway itself owns time and I/O.

using Tick = std::uint64_t;

// Gateway-local delivery bookkeeping. Architecture 9.1.2: this is exactly the
// state that does NOT enter consensus, which is why `claimed` and `attempted`
// are gateway-local enum values the reducer never writes.
//
// It is deliberately reconstructible rather than durable. A gateway restart
// discards it and rebuilds by reconciliation (G-032), which is why the design
// can defer durability to Phase 7 without leaving a gap.
struct DeliveryAttempt {
    EffectId effect_id{};
    Tick sent_at{};
    std::uint32_t attempts{};
    // True once a send left the process and no terminal response has arrived.
    // While true the effect is AMBIGUOUS, never assumed delivered or rejected.
    bool awaiting_response{};
    // True ONLY when the last attempt provably never reached the venue - the
    // transport refused it for pacing, or it failed before leaving the process.
    // Nothing is resting at the venue, so the next send is a first send rather
    // than a duplicate, and no reconciliation has to precede it.
    //
    // The default is false and every transition except the two provably-unsent
    // ones leaves it false. That asymmetry is deliberate: a gateway that guesses
    // wrong in this direction places a second real order, so the strong claim
    // has to be asserted explicitly by a caller that saw the evidence.
    bool resend_permitted{};

    constexpr auto operator<=>(const DeliveryAttempt&) const noexcept = default;
};

inline constexpr std::size_t kMaxDeliveryAttempts = kMaxOutboxEffects;

struct GatewayDeliveryState {
    std::size_t attempt_count{};
    std::array<DeliveryAttempt, kMaxDeliveryAttempts> attempts{};

    constexpr auto operator<=>(const GatewayDeliveryState&) const noexcept = default;
};

struct GatewayTimeouts {
    // Ticks after a send beyond which the outcome is treated as ambiguous. Not a
    // rejection: G-026 forbids reading a timeout as proof an order does not
    // exist, so this only ever produces `effect_unknown`.
    Tick response_deadline{10};
    // Bound on provably-unsent retries for one effect. Without it a venue that
    // rate-limits indefinitely turns into an unbounded retry loop, which is a
    // livelock rather than a safety failure but is no more acceptable for it.
    // On exhaustion the effect simply stops being scheduled: it stays pending
    // and unsettled, so it is visible in metrics and blocks its slot, which is
    // the fail-closed outcome rather than a silent drop.
    std::uint32_t max_send_attempts{3};
};

// One tick's worth of work, bounded by the outbox arena so a plan can never
// outgrow the state that produced it.
struct DeliveryPlan {
    std::size_t send_count{};
    std::array<EffectId, kMaxDeliveryAttempts> to_send{};
    // Effects whose response deadline passed. The shell reports each to the
    // cluster as `effect_unknown` and must NOT resend without reconciling first.
    std::size_t ambiguous_count{};
    std::array<EffectId, kMaxDeliveryAttempts> now_ambiguous{};

    constexpr auto operator<=>(const DeliveryPlan&) const noexcept = default;
};

// Pure and bounded. Reads no clock: `now` is supplied.
//
// Ambiguity is decided BEFORE sending, so an effect that has just timed out is
// never also selected to send in the same tick. That ordering is what stops the
// planner producing the blind retry G-026 forbids.
[[nodiscard]] DeliveryPlan plan_delivery(const TradingState& state,
                                         const GatewayDeliveryState& delivery,
                                         const GatewayControlView& control,
                                         const VenueCapabilityView& venue,
                                         const SendQuotaView& quota,
                                         const GatewayTimeouts& timeouts,
                                         Tick now) noexcept;

// Bookkeeping transitions, kept beside the planner so the two cannot disagree
// about what an attempt means. Each returns false when the arena is full or the
// attempt is unknown, and fails closed rather than silently dropping work.
[[nodiscard]] bool record_send(GatewayDeliveryState& delivery, EffectId effect_id,
                               Tick now) noexcept;
// The venue answered. The attempt is complete but NOT retryable: something may
// be resting at the venue, so any further send has to be preceded by
// reconciliation rather than by this planner.
[[nodiscard]] bool record_response(GatewayDeliveryState& delivery,
                                   EffectId effect_id) noexcept;
// The send provably never reached the venue. This is the only transition that
// makes an effect retryable without reconciliation, and callers must reserve it
// for transport outcomes that carry that proof - `rate_limited` and
// `pre_send_failure`, never `ambiguous`, `accepted`, or a business rejection.
[[nodiscard]] bool record_unsent(GatewayDeliveryState& delivery,
                                 EffectId effect_id) noexcept;
[[nodiscard]] const DeliveryAttempt* find_attempt(
    const GatewayDeliveryState& delivery, EffectId effect_id) noexcept;

static_assert(std::is_trivially_copyable_v<GatewayDeliveryState>);
static_assert(std::is_trivially_copyable_v<DeliveryPlan>);
static_assert(std::is_trivially_copyable_v<VenueCapabilityView>);
static_assert(std::is_trivially_copyable_v<GatewayControlView>);
static_assert(std::is_trivially_copyable_v<SendQuotaView>);

}  // namespace velocita::v2
