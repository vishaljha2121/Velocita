// True Markets behind the venue transport: quote, sign through the signer, submit.
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

enum class ApiCall : std::uint8_t {
    get_quote = 1,
    trade_prepare,
    trade_execute,
    order_status,
};

enum class ApiStatus : std::uint8_t {
    completed = 1,
    tool_error,
    rate_limited,
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

enum class ExecutionFlow : std::uint8_t {
    unset = 0,
    quote_then_execute,
    prepare_then_execute,
};

struct TmRateLimit {
    std::uint32_t capacity{};
    Tick refill_interval{};
};

inline constexpr std::size_t kMaxTmOrders = 64;
inline constexpr std::size_t kMaxTmObservations = 64;
inline constexpr std::size_t kMaxTmStatusCallsPerPoll = 2;
inline constexpr std::size_t kMaxTmDefinitiveRejections = 4;

struct TrueMarketsVenueConfig {
    VenueInstrumentSpec spec{};
    InstrumentExecutionProfile profile{};
    ExecutionFlow flow{ExecutionFlow::unset};
    Chain chain{Chain::unset};
    TmText base_asset{};
    TmText quote_asset{};
    std::uint8_t quote_decimals{};
    std::uint32_t max_fee_bps{};
    Tick quote_ttl{};
    Tick status_interval{};
    TmRateLimit order_rate{};
    TmRateLimit query_rate{};
    bool status_accepts_quote_id{};
    std::size_t definitive_rejection_count{};
    std::array<TmText, kMaxTmDefinitiveRejections> definitive_rejections{};
    CallerId caller{};
    std::uint64_t first_signing_sequence{1};
    VenueSessionId session{1};
};

[[nodiscard]] bool valid(const TrueMarketsVenueConfig& config) noexcept;

enum class TmPhase : std::uint8_t {
    none = 0,
    abandoned_before_submit,
    uncertain,
    accepted,
    settled,
    not_executed,
};

struct TmOrder {
    ClusterOrderId cluster_order_id{};
    EffectId effect_id{};
    Side side{};
    QuantityLots quantity{};
    PriceTicks limit{};
    TmPhase phase{TmPhase::none};
    bool committal_call_made{};
    TmText handle{};
    bool handle_is_order_id{};
    VenueOrderId venue_order_id{};
    QuantityLots filled{};
    bool breach{};
    Tick next_status_at{};
    std::uint64_t signature_id{};
};

struct TmCounters {
    std::array<std::uint64_t, 5> api_calls{};
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

[[nodiscard]] sim::VenueTransport truemarkets_venue_transport(TrueMarketsVenue& venue) noexcept;

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

}
