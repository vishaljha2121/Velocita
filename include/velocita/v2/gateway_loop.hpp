// The gateway's main loop: reads committed state, delivers orders, reports venue evidence.
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
#include <span>
#include <string>

namespace velocita::v2 {

struct GatewayLoopConfig {
    CallerId caller{};
    std::uint64_t first_sequence{};
    std::uint64_t sequence_limit{};
    VenueCapabilityView venue{};
    AccountId account{};
    InstrumentId instrument{};
    InstrumentSpecVersion instrument_spec_version{};
    GatewayTimeouts timeouts{5000, 3, 1000, 4, 2000, 60'000};
    Tick freshness_bound{};
    Tick lease_bound{2000};
    Tick state_interval{250};
    Tick request_timeout{3000};
    Tick ack_interval{1000};
    SendQuotaView quota_per_second{5, 2};
    bool allow_new_risk{};
    bool require_loss_limit{};
    Tick rearm_quiet{30'000};
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
enum class GatewayStage : std::uint8_t {
    authorized_to_proof,
    proof_to_sent,
    sent_to_fill,
    fill_to_committed,
    count
};

inline constexpr std::array<double, 13> kGatewayStageBucketsMs{
    1, 2, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000};

struct GatewayStageHistogram {
    std::array<std::uint64_t, kGatewayStageBucketsMs.size()> buckets{};
    std::uint64_t count{};
    double sum_ms{};
    void observe(double ms) noexcept;
};

enum class GatewayStopCause : std::uint8_t {
    fence,
    unknown_outcome,
    conflict,
    refused_evidence,
    control_change,
    reconciliation_mismatch,
    reconciliation_exhausted,
    capacity,
    corrupt_state,
    wrong_cluster,
    authority,
    reducing_mode,
    count
};

[[nodiscard]] constexpr bool rearmable(const GatewayStopCause cause) noexcept {
    return cause == GatewayStopCause::fence || cause == GatewayStopCause::reducing_mode;
}

enum class BookVerdict : std::uint8_t { match, not_comparable, mismatch, unavailable, count };
enum class BookBreak : std::uint8_t { none, position, unknown_order };

struct BookCut {
    std::int64_t applied_index{};
    std::int64_t seen_index{};
    std::size_t evidence_pending{};
    std::size_t open_orders{};
};

struct BookComparison {
    BookVerdict verdict{BookVerdict::unavailable};
    BookBreak kind{BookBreak::none};
    std::int64_t kernel_lots{};
    std::int64_t venue_lots{};
    ClusterOrderId order{};

    constexpr bool operator==(const BookComparison&) const noexcept = default;
};

[[nodiscard]] BookComparison compare_venue_book(const TradingState& state, AccountId account, InstrumentId instrument,
                                                const sim::VenueBook& venue, const BookCut& cut) noexcept;

enum class GatewayEventKind : std::uint8_t { stopped, armed, rearmed, reconciliation_mismatch };

struct GatewayEvent {
    GatewayEventKind kind{};
    GatewayStopCause cause{GatewayStopCause::count};
    std::int64_t term{};
    std::int64_t applied_index{};
    std::int64_t kernel_lots{};
    std::int64_t venue_lots{};
    ClusterOrderId order{};
    ApplyCode refused_code{ApplyCode::applied};
    VenueObservationKind refused_kind{};
};

inline constexpr std::size_t kMaxGatewayEvents = 32;

[[nodiscard]] const char* describe(GatewayStopCause cause) noexcept;
[[nodiscard]] const char* describe(BookVerdict verdict) noexcept;

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
    std::int64_t volume_micros_buy{}, volume_micros_sell{};
    double last_fill_timestamp_seconds{};
    double heartbeat_timestamp_seconds{};
    std::array<std::uint64_t, static_cast<std::size_t>(GatewayStopCause::count)> stops{};
    std::uint64_t rearms{};
    std::uint64_t stale_state_reads{};
    std::array<bool, static_cast<std::size_t>(GatewayStopCause::count)> stop_active{};
    bool awaiting_arm{};
    bool not_configured{};
    bool no_loss_limit{};
    BookVerdict reconciliation{BookVerdict::unavailable};
    bool reconciliation_mismatch{};
    std::int64_t reconciliation_kernel_lots{}, reconciliation_venue_lots{};
    double reconciliation_last_match_seconds{};
    bool venue_book_known{};
    std::int64_t venue_base_lots{};
    std::int64_t venue_quote_micros{};
    std::uint64_t events_dropped{};
};

[[nodiscard]] std::string render_gateway_metrics(const GatewayMetrics& metrics);

enum class GatewayPhase : std::uint8_t { starting, running, halted };

class GatewayLoop {
public:
    GatewayLoop(GatewayLoopConfig config, GatewayLink& link, const VerifiedExecutionCatalog& catalog,
                sim::VenueTransport venue, GatewayClock clock, sim::VenueBookSource book = {});
    ~GatewayLoop();
    GatewayLoop(const GatewayLoop&) = delete;
    GatewayLoop& operator=(const GatewayLoop&) = delete;

    void step();

    [[nodiscard]] const GatewayMetrics& metrics() const noexcept { return metrics_; }
    [[nodiscard]] GatewayPhase phase() const noexcept { return phase_; }
    [[nodiscard]] bool new_risk_stopped() const noexcept;
    [[nodiscard]] bool all_transport_stopped() const noexcept;
    [[nodiscard]] std::uint64_t next_sequence() const noexcept { return next_sequence_; }
    void extend_sequence_limit(std::uint64_t limit) noexcept;
    [[nodiscard]] const GatewayDeliveryState& delivery() const noexcept { return delivery_; }
    [[nodiscard]] const GatewayEvidenceState& evidence() const noexcept { return *evidence_; }
    [[nodiscard]] std::size_t drain_events(std::span<GatewayEvent> out) noexcept;

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
    [[nodiscard]] std::optional<RequestSequence> take_acknowledgement_sequence() noexcept;
    [[nodiscard]] VenueObservation synthetic(VenueObservationKind kind, ClusterOrderId order, EffectId effect,
                                             RequestSequence sequence) const;
    void stop(GatewayStopCause cause) noexcept;
    void stop_for_refused(const VenueObservation& observation, ApplyCode code) noexcept;
    void note_authority(bool was_stopped) noexcept;
    void gate(const TradingState& state, const StateReading& reading, Tick now);
    [[nodiscard]] std::size_t open_orders(const TradingState& state) const noexcept;
    [[nodiscard]] bool loss_limit_set(const TradingState& state) const noexcept;
    void push(const GatewayEvent& event) noexcept;
    Marks* marks(EffectId effect, bool create) noexcept;
    static Tick transport_clock(void* self) noexcept;
    static void transport_handoff(BoundOrderEffect binding, void* self);

    GatewayLoopConfig config_;
    GatewayLink& link_;
    const VerifiedExecutionCatalog& catalog_;
    sim::VenueTransport venue_;
    GatewayClock clock_;
    sim::VenueBookSource book_;
    GatewayPhase phase_{GatewayPhase::starting};
    std::unique_ptr<GatewayAuthority> authority_;
    Digest256 authority_domain_{};
    GatewayControlView control_{};
    std::int64_t term_{};
    std::int32_t leader_{};
    std::int64_t seen_index_{};
    std::uint32_t latched_{};
    bool fence_pending_{};
    Tick last_fence_at_{};
    std::array<GatewayEvent, kMaxGatewayEvents> events_{};
    std::size_t event_count_{};
    GatewayDeliveryState delivery_{};
    std::unique_ptr<GatewayEvidenceState> evidence_;
    GatewayReconciliationState reconciliation_{};
    SendQuotaView quota_{};
    Tick quota_refilled_at_{};
    InFlight in_flight_{InFlight::none};
    CommandIdentity in_flight_identity_{};
    std::uint64_t in_flight_ack_{};
    std::uint64_t in_flight_ack_through_{};
    BoundOrderEffect prepared_{};
    Tick state_requested_at_{};
    Tick last_state_at_{};
    bool have_state_{};
    bool state_dirty_{true};
    bool state_stale_{};
    std::uint64_t next_sequence_{};
    std::uint64_t acknowledged_through_{};
    std::uint64_t last_evidence_sequence_{};
    Tick last_ack_at_{};
    std::optional<sim::SendOutcome> handoff_outcome_{};
    std::array<Marks, kMaxOutboxEffects> marks_{};
    std::array<FillMark, kMaxEvidenceObligations> fill_marks_{};
    GatewayMetrics metrics_{};
};

}
