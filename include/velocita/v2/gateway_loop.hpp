#pragma once

#include "velocita/v2/gateway.hpp"
#include "velocita/v2/gateway_authority.hpp"
#include "velocita/v2/gateway_link.hpp"
#include "velocita/v2/venue_transport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace velocita::v2 {

// Every tick in this file is milliseconds of the injected monotonic clock.
struct GatewayLoopConfig {
    CallerId caller{};
    // Request sequences this process may use: [first_sequence, sequence_limit).
    // The binary reserves them durably before use, so a restart never reuses one.
    std::uint64_t first_sequence{};
    std::uint64_t sequence_limit{};
    VenueCapabilityView venue{};
    // The one catalog row this gateway serves (with venue's id and capability
    // version above). Effects and orders bound to any other row are left alone.
    AccountId account{};
    InstrumentId instrument{};
    InstrumentSpecVersion instrument_spec_version{};
    GatewayTimeouts timeouts{5000, 3, 1000, 4, 2000, 60'000};
    // GatewayAuthority freshness from request start. Zero means no new risk.
    Tick freshness_bound{};
    // Control-view lease: a submit candidate needs a state read this recent.
    Tick lease_bound{2000};
    Tick state_interval{250};
    Tick request_timeout{3000};
    Tick ack_interval{1000};
    // Refilled every second; the emergency lane serves cancels only.
    SendQuotaView quota_per_second{5, 2};
    // Without it the loop never activates new risk, whatever it finds.
    bool allow_new_risk{};
};

struct GatewayClock {
    Tick (*monotonic_ms)(void*) noexcept{};
    double (*wall_seconds)(void*) noexcept{};
    void* context{};
};

enum class GatewayReject : std::uint8_t {
    business_rejected, pre_send_failure, rate_limited, dispatch_refused, venue_rejected,
    kernel_refused, reconciliation_conflict, reconciliation_exhausted, count
};
enum class GatewayAuthorityResult : std::uint8_t { granted, refused, timed_out, rejected, count };
// decision_to_authorized is not measured: the gateway sees neither the
// decision's nor the authorization's time, only committed state versions.
enum class GatewayStage : std::uint8_t {
    authorized_to_proof,  // effect first seen deliverable in a committed read -> proof accepted
    proof_to_sent,        // proof accepted -> venue send returned
    sent_to_fill,         // venue send returned -> first fill polled
    fill_to_committed,    // fill polled -> its ObserveFill committed result received
    count
};

inline constexpr std::array<double, 13> kGatewayStageBucketsMs{
    1, 2, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000};

struct GatewayStageHistogram {
    std::array<std::uint64_t, kGatewayStageBucketsMs.size()> buckets{};  // cumulative at render
    std::uint64_t count{};
    double sum_ms{};
    void observe(double ms) noexcept;
};

struct GatewayPosition {
    AccountId account_id{};
    InstrumentId instrument_id{};
    std::int64_t lots{};
};

struct GatewayMetrics {
    bool new_risk_stopped{true};
    std::uint64_t orders_sent_buy{}, orders_sent_sell{}, orders_sent_cancel{};
    std::uint64_t fills_buy{}, fills_sell{};
    std::array<std::uint64_t, static_cast<std::size_t>(GatewayReject::count)> rejects{};
    std::uint64_t unknown_outcomes{};
    std::array<std::uint64_t, static_cast<std::size_t>(GatewayAuthorityResult::count)> authority{};
    std::array<GatewayStageHistogram, static_cast<std::size_t>(GatewayStage::count)> stages{};
    std::size_t position_count{};
    std::array<GatewayPosition, kMaxRiskAccounts> positions{};
    std::int64_t fee_micros_total{};
    // Filled notional by side: price x lots x the served row's quote micros per tick-lot.
    std::int64_t volume_micros_buy{}, volume_micros_sell{};
    double last_fill_timestamp_seconds{};  // wall clock when the last fill committed; 0 = none
    double heartbeat_timestamp_seconds{};  // wall clock of the last loop step
};

// Prometheus text exposition. Every counter is present from the first render
// with every known label value, so increase() sees the first rise.
[[nodiscard]] std::string render_gateway_metrics(const GatewayMetrics& metrics);

enum class GatewayPhase : std::uint8_t { starting, running, halted };

// One gateway process's non-blocking loop: read committed state, plan, obtain
// a one-shot proof for each risk-increasing send, hand it to the venue, and
// submit what the venue reports as gateway-caller commands. Unknown outcomes,
// reconciliation conflicts and refused evidence stop new risk, and nothing in
// this loop re-activates it; only a new process (operator action) can.
class GatewayLoop {
public:
    GatewayLoop(GatewayLoopConfig config, GatewayLink& link, const VerifiedExecutionCatalog& catalog,
                sim::VenueTransport venue, GatewayClock clock);
    ~GatewayLoop();
    GatewayLoop(const GatewayLoop&) = delete;
    GatewayLoop& operator=(const GatewayLoop&) = delete;

    void step();

    [[nodiscard]] const GatewayMetrics& metrics() const noexcept { return metrics_; }
    [[nodiscard]] GatewayPhase phase() const noexcept { return phase_; }
    [[nodiscard]] bool new_risk_stopped() const noexcept;
    [[nodiscard]] bool all_transport_stopped() const noexcept;
    // Next request sequence; the binary keeps its durable reservation ahead.
    [[nodiscard]] std::uint64_t next_sequence() const noexcept { return next_sequence_; }
    void extend_sequence_limit(std::uint64_t limit) noexcept;
    [[nodiscard]] const GatewayDeliveryState& delivery() const noexcept { return delivery_; }
    [[nodiscard]] const GatewayEvidenceState& evidence() const noexcept { return *evidence_; }

private:
    enum class InFlight : std::uint8_t { none, state, authority, evidence, acknowledge };
    struct Marks {
        EffectId effect_id{};
        Tick first_deliverable{};
        Tick proof_at{};
        Tick sent_at{};
        bool fill_seen{};
    };
    struct FillMark {
        RequestSequence sequence{};
        Tick polled_at{};
    };

    void on_state(const LinkOutcome& outcome, Tick now);
    void on_authority(LinkOutcome& outcome, Tick now);
    void on_command(const LinkOutcome& outcome, Tick now);
    void poll_venue(Tick now);
    [[nodiscard]] std::optional<std::int64_t> fill_notional(const VenueObservation& fill) const noexcept;
    void choose(Tick now);
    [[nodiscard]] bool propose_evidence(Tick now);
    [[nodiscard]] bool deliver(Tick now);
    [[nodiscard]] bool reconcile(Tick now);
    [[nodiscard]] bool record(const VenueObservation& observation, Tick now);
    [[nodiscard]] bool serves(const BoundOrderEffect& binding) const noexcept;
    [[nodiscard]] std::optional<RequestSequence> take_sequence() noexcept;
    [[nodiscard]] VenueObservation synthetic(VenueObservationKind kind, ClusterOrderId order, EffectId effect,
                                             RequestSequence sequence) const;
    void stop_new_risk() noexcept;
    Marks* marks(EffectId effect, bool create) noexcept;
    static Tick transport_clock(void* self) noexcept;
    static void transport_handoff(BoundOrderEffect binding, void* self);

    GatewayLoopConfig config_;
    GatewayLink& link_;
    const VerifiedExecutionCatalog& catalog_;
    sim::VenueTransport venue_;
    GatewayClock clock_;
    GatewayPhase phase_{GatewayPhase::starting};
    std::unique_ptr<GatewayAuthority> authority_;
    Digest256 authority_domain_{};
    GatewayControlView control_{};
    std::int64_t term_{};
    GatewayDeliveryState delivery_{};
    std::unique_ptr<GatewayEvidenceState> evidence_;
    GatewayReconciliationState reconciliation_{};
    SendQuotaView quota_{};
    Tick quota_refilled_at_{};
    InFlight in_flight_{InFlight::none};
    CommandIdentity in_flight_identity_{};
    std::uint64_t in_flight_ack_{};          // sequence of the in-flight acknowledgement
    std::uint64_t in_flight_ack_through_{};  // what it acknowledges
    BoundOrderEffect prepared_{};
    Tick state_requested_at_{};
    Tick last_state_at_{};
    bool have_state_{};
    bool state_dirty_{true};
    std::uint64_t next_sequence_{};
    std::uint64_t acknowledged_through_{};
    Tick last_ack_at_{};
    std::optional<sim::SendOutcome> handoff_outcome_{};
    std::array<Marks, kMaxOutboxEffects> marks_{};
    std::array<FillMark, kMaxEvidenceObligations> fill_marks_{};
    GatewayMetrics metrics_{};
};

}  // namespace velocita::v2
