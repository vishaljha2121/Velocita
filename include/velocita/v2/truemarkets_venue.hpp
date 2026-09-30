#pragma once

#include "velocita/v2/contracts.hpp"
#include "velocita/v2/truemarkets_wire.hpp"
#include "velocita/v2/venue_signer.hpp"
#include "velocita/v2/venue_transport.hpp"
#include "velocita/v2/venue_wire.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace velocita::v2::truemarkets {

using sim::QueryOutcome;
using sim::QueryResult;
using sim::SendOutcome;
using sim::Tick;

// ADR 0019. True Markets behind the VenueTransport seam: quote (or prepare),
// inspect-and-sign, submit, status. The HTTP/MCP session, TLS, authentication
// and the signer are injected; this library contains no network code, and a
// ctest checks its object files for socket and TLS symbols.

// ---------------------------------------------------------------------------
// The injected venue API. One call per request, synchronous, with a caller-
// owned response buffer. The implementation owns sessions, JWTs, JSON-RPC id
// matching and the MCP result envelope, and must return `no_response` whenever
// it cannot prove what the server answered.

enum class ApiCall : std::uint8_t {
    get_quote = 1,
    trade_prepare,
    trade_execute,
    order_status,
};

enum class ApiStatus : std::uint8_t {
    // A complete tool result (or REST body) is in the response buffer.
    completed = 1,
    // The server answered with a tool error. Its body may be in the buffer.
    tool_error,
    // The server answered that we are being paced. It is still an answer from
    // the server, not a local refusal, so it proves nothing about execution.
    rate_limited,
    // Timeout, reset, unreadable or oversized reply, id mismatch: unknown.
    no_response,
};

struct ApiReply {
    ApiStatus status{ApiStatus::no_response};
    std::size_t length{};
};

struct TrueMarketsApi {
    ApiReply (*call_fn)(void* context, ApiCall call, std::string_view request,
                        std::span<char> response, Tick now){};
    void* context{};

    [[nodiscard]] ApiReply call(const ApiCall kind, const std::string_view request,
                                const std::span<char> response, const Tick now) const {
        return call_fn(context, kind, request, response, now);
    }
};

struct TrueMarketsClock {
    Tick (*now_fn)(void* context) noexcept {};
    void* context{};

    [[nodiscard]] Tick now() const noexcept { return now_fn(context); }
};

static_assert(std::is_trivially_copyable_v<TrueMarketsApi>);

// ---------------------------------------------------------------------------

enum class ExecutionFlow : std::uint8_t {
    unset = 0,
    // get_quote (documented non-committal) -> sign the quote's payload ->
    // submit. Whether trade_execute accepts a quote_id is UNVERIFIED.
    quote_then_execute,
    // trade_prepare -> sign -> trade_execute, the recorded MCP sequence.
    // trade_prepare may submit directly (D-26), so every uncertain prepare is
    // an unknown exposure, never a pre-send failure.
    prepare_then_execute,
};

// One token per `refill_interval`, at most `capacity`. Enforced before any
// request is encoded, so a local refusal never reaches the venue.
struct TmRateLimit {
    std::uint32_t capacity{};
    Tick refill_interval{};
};

inline constexpr std::size_t kMaxTmOrders = 64;
inline constexpr std::size_t kMaxTmObservations = 64;
inline constexpr std::size_t kMaxTmStatusCallsPerPoll = 2;
inline constexpr std::size_t kMaxTmDefinitiveRejections = 4;

struct TrueMarketsVenueConfig {
    // Must agree with `profile` under `spec_agrees_with_profile`.
    VenueInstrumentSpec spec{};
    InstrumentExecutionProfile profile{};
    ExecutionFlow flow{ExecutionFlow::unset};
    Chain chain{Chain::unset};
    TmText base_asset{};
    TmText quote_asset{};
    std::uint8_t quote_decimals{};
    // The fee the adapter will accept, in basis points of executed notional.
    // Must not exceed the kernel's maximum_fee_ppm / 100 for this account: the
    // kernel does not check fill fees (ADR 0018 finding 1).
    std::uint32_t max_fee_bps{};
    // A quote or prepared order older than this is never submitted. The quote
    // carries no recorded expiry, so the bound is ours.
    Tick quote_ttl{};
    Tick status_interval{};
    TmRateLimit order_rate{};
    TmRateLimit query_rate{};
    // UNVERIFIED venue capability: whether the status endpoint answers for a
    // quote_id when the submit reply (and its order_id) was lost.
    bool status_accepts_quote_id{};
    // Execute-reply status words that prove the order was NOT accepted. None is
    // verified, so the default is empty and every non-accepting reply is
    // ambiguous.
    std::size_t definitive_rejection_count{};
    std::array<TmText, kMaxTmDefinitiveRejections> definitive_rejections{};
    CallerId caller{};
    // The signer refuses a request sequence it has already seen, so a restarted
    // process must start above every sequence a previous one used: the binary
    // supplies this from a durable reservation, as it does request sequences.
    std::uint64_t first_signing_sequence{1};
    VenueSessionId session{1};
};

[[nodiscard]] bool valid(const TrueMarketsVenueConfig& config) noexcept;

// What the adapter has learnt about one order, in the order it learns it.
enum class TmPhase : std::uint8_t {
    none = 0,
    // Stopped before any committal call. Nothing is at the venue.
    abandoned_before_submit,
    // A committal call was made and its outcome is not established.
    uncertain,
    accepted,
    settled,
    // The venue reports a final non-execution.
    not_executed,
};

struct TmOrder {
    ClusterOrderId cluster_order_id{};
    EffectId effect_id{};
    Side side{};
    QuantityLots quantity{};
    PriceTicks limit{};
    TmPhase phase{TmPhase::none};
    // True from the moment a call that can create exposure is made; never
    // cleared. This, not `phase`, is what forbids a second submission.
    bool committal_call_made{};
    TmText handle{};
    bool handle_is_order_id{};
    VenueOrderId venue_order_id{};
    QuantityLots filled{};
    // The venue reported something the authorized FOK swap cannot produce:
    // a partial or over-fill, a price beyond the limit, a fee above the cap.
    bool breach{};
    Tick next_status_at{};
    std::uint64_t signature_id{};
};

// Counts of what the adapter did, for tests and metrics.
struct TmCounters {
    std::array<std::uint64_t, 5> api_calls{};  // indexed by ApiCall
    std::uint64_t local_rate_limited{};
    std::uint64_t signer_calls{};
    std::uint64_t signer_refusals{};
    std::uint64_t fok_kills{};
    std::uint64_t duplicate_sends_refused{};
    std::uint64_t breaches{};
};

class TrueMarketsVenue final {
public:
    TrueMarketsVenue(const TrueMarketsVenueConfig& config, TrueMarketsApi api, Signer signer,
                     TrueMarketsClock clock) noexcept;

    [[nodiscard]] bool configured() const noexcept { return configured_; }

    // accepted: the venue took the signed order; an acknowledgement follows
    // through poll, and the fill once status reports settlement.
    // business_rejected: killed before anything committal (FOK not fillable at
    // the limit, fee above cap, venue-reported issue, cancel of a swap); an
    // order_rejected report follows.
    // venue_duplicate: a committal call was already made for this effect.
    // pre_send_failure: nothing that can execute was sent.
    // rate_limited: refused locally, or by a non-committal call; nothing sent.
    // ambiguous: a committal call's outcome is unknown.
    [[nodiscard]] SendOutcome send(const OrderEffect& effect, Tick now) noexcept;

    [[nodiscard]] std::size_t poll(std::span<VenueObservation> out, Tick now) noexcept;
    void acknowledge(const VenueObservation& observation) noexcept;
    [[nodiscard]] QueryResult query(ClusterOrderId order, Tick now) noexcept;
    [[nodiscard]] VenueSessionId session() const noexcept { return config_.session; }

    [[nodiscard]] const TmOrder* find_order(ClusterOrderId order) const noexcept;
    [[nodiscard]] const TmCounters& counters() const noexcept { return counters_; }
    [[nodiscard]] std::size_t pending_observation_count() const noexcept { return queued_count_; }

private:
    struct Queued {
        VenueObservation observation{};
        bool delivered{};
    };
    struct Bucket {
        std::uint32_t tokens{};
        Tick refilled_at{};
    };

    [[nodiscard]] TmOrder* order_for(ClusterOrderId order) noexcept;
    [[nodiscard]] bool take(Bucket& bucket, const TmRateLimit& limit, std::uint32_t count,
                            Tick now) noexcept;
    [[nodiscard]] ApiReply call(ApiCall kind, std::string_view request, Tick now) noexcept;
    [[nodiscard]] SendOutcome kill(TmOrder& order, const OrderEffect& effect) noexcept;
    void queue(const VenueObservation& observation) noexcept;
    [[nodiscard]] VenueObservation report(VenueObservationKind kind, const TmOrder& order,
                                          std::string_view evidence) noexcept;
    // Ask the venue about one order and absorb what it says. False if nothing
    // was learnt (no handle, no token, no answer).
    [[nodiscard]] bool refresh_status(TmOrder& order, Tick now) noexcept;
    void absorb_settlement(TmOrder& order, const StatusReply& status,
                           std::string_view evidence) noexcept;

    TrueMarketsVenueConfig config_;
    TrueMarketsApi api_;
    Signer signer_;
    TrueMarketsClock clock_;
    bool configured_{};
    std::uint64_t rpc_id_{};
    std::uint64_t signing_sequence_{};
    std::uint64_t venue_sequence_{};
    Bucket order_bucket_{};
    Bucket query_bucket_{};
    TmCounters counters_{};
    std::size_t order_count_{};
    std::array<TmOrder, kMaxTmOrders> orders_{};
    std::size_t queued_count_{};
    std::array<Queued, kMaxTmObservations> queued_{};
    std::array<char, kMaxTmRequestBytes> request_{};
    std::array<char, kMaxTmResponseBytes> response_{};
};

// Trivially copyable handle; the venue must outlive every copy.
[[nodiscard]] sim::VenueTransport truemarkets_venue_transport(TrueMarketsVenue& venue) noexcept;

// ---------------------------------------------------------------------------
// Live arming. `--venue truemarkets` starts only when the operator armed it
// explicitly AND named a signer AND this build contains a live transport. No
// build does today: the HTTP/MCP client and signer client are not written, and
// this constant is what the gateway checks, so every live start is refused.

inline constexpr bool kLiveVenueTransportCompiled = false;

enum class LiveArmingRefusal : std::uint8_t {
    none = 0,
    not_armed,
    no_signer,
    no_live_transport,
};

struct LiveArmingInputs {
    bool armed{};
    std::string_view signer_endpoint{};
};

[[nodiscard]] LiveArmingRefusal evaluate_live_arming(
    const LiveArmingInputs& inputs,
    bool live_transport_compiled = kLiveVenueTransportCompiled) noexcept;

[[nodiscard]] std::string_view describe(LiveArmingRefusal refusal) noexcept;

}  // namespace velocita::v2::truemarkets
