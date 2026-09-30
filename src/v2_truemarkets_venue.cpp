#include "velocita/v2/truemarkets_venue.hpp"

#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <limits>

namespace velocita::v2::truemarkets {
namespace {

constexpr std::int64_t kBasisPoints = 10'000;

// Reply words. None of the execute or status words is recorded from the venue;
// they are the fixture vocabulary and must be replaced from recorded replies.
constexpr std::array<std::string_view, 2> kAcceptedWords{"submitted", "accepted"};
constexpr std::string_view kSettledWord = "filled";
constexpr std::array<std::string_view, 3> kNotExecutedWords{"failed", "expired", "rejected"};

template <std::size_t N>
[[nodiscard]] bool one_of(const std::string_view word, const std::array<std::string_view, N>& words) noexcept {
    return std::find(words.begin(), words.end(), word) != words.end();
}

[[nodiscard]] Digest256 digest_of(const std::string_view text) noexcept {
    return sha256_digest(std::as_bytes(std::span(text.data(), text.size())));
}

// Deterministic from the venue's own identifier, so a restarted process
// derives the same id for the same order or execution.
[[nodiscard]] std::uint64_t id_from(const std::string_view domain, const std::string_view text) noexcept {
    Sha256 hash;
    hash.update(std::as_bytes(std::span(domain.data(), domain.size())));
    hash.update(std::as_bytes(std::span(text.data(), text.size())));
    const auto value = hash.finish();
    std::uint64_t id = 0;
    for (std::size_t i = 0; i < 8; ++i) id |= static_cast<std::uint64_t>(value.bytes[i]) << (8 * i);
    return id == 0 ? 1 : id;
}

[[nodiscard]] std::optional<std::int64_t> multiply(const std::int64_t a, const std::int64_t b) noexcept {
    std::int64_t out = 0;
    if (__builtin_mul_overflow(a, b, &out)) return std::nullopt;
    return out;
}

// ceil(notional * bps / 10'000), split so no intermediate exceeds int64.
[[nodiscard]] std::optional<std::int64_t> fee_cap(const std::int64_t notional, const std::uint32_t bps) noexcept {
    if (notional < 0) return std::nullopt;
    const auto whole = multiply(notional / kBasisPoints, bps);
    if (!whole) return std::nullopt;
    const auto remainder = (notional % kBasisPoints) * static_cast<std::int64_t>(bps);
    const auto rounded = remainder == 0 ? 0 : 1 + (remainder - 1) / kBasisPoints;
    std::int64_t total = 0;
    if (__builtin_add_overflow(*whole, rounded, &total)) return std::nullopt;
    return total;
}

// What one authorized FOK swap may do, from the committed effect alone.
struct Authorization {
    std::int64_t base_atoms{};
    std::int64_t limit_notional{};
    std::int64_t quote_bound{};
};

[[nodiscard]] std::optional<Authorization> authorization(const OrderEffect& effect,
                                                         const TrueMarketsVenueConfig& config) noexcept {
    const auto base = multiply(effect.quantity.atoms(), config.spec.lot_base_atoms);
    const auto per_lot = multiply(effect.price.atoms(), config.profile.quote_micros_per_tick_lot.atoms());
    const auto notional = per_lot ? multiply(*per_lot, effect.quantity.atoms()) : std::nullopt;
    if (!base || !notional || *base <= 0 || *notional <= 0) return std::nullopt;
    const auto max_fee = fee_cap(*notional, config.max_fee_bps);
    if (!max_fee) return std::nullopt;
    std::int64_t bound = 0;
    const bool overflow = effect.side == Side::buy ? __builtin_add_overflow(*notional, *max_fee, &bound)
                                                   : __builtin_sub_overflow(*notional, *max_fee, &bound);
    if (overflow) return std::nullopt;
    return Authorization{*base, *notional, bound};
}

// Executed notional (before fee) from a quote-asset movement net or gross of
// fee: a buy debits notional + fee, a sell credits notional - fee.
[[nodiscard]] std::optional<std::int64_t> executed_notional(const Side side, const std::int64_t quote_moved,
                                                            const std::int64_t fee) noexcept {
    if (quote_moved < 0 || fee < 0) return std::nullopt;
    std::int64_t notional = 0;
    const bool overflow = side == Side::buy ? __builtin_sub_overflow(quote_moved, fee, &notional)
                                            : __builtin_add_overflow(quote_moved, fee, &notional);
    if (overflow || notional <= 0) return std::nullopt;
    return notional;
}

// Within the FOK limit and the fee cap?
[[nodiscard]] bool within_authorization(const Side side, const std::int64_t notional, const std::int64_t fee,
                                        const std::int64_t limit_notional, const std::uint32_t max_fee_bps) noexcept {
    const auto cap = fee_cap(notional, max_fee_bps);
    if (!cap || fee > *cap) return false;
    return side == Side::buy ? notional <= limit_notional : notional >= limit_notional;
}

[[nodiscard]] QueryResult unavailable() noexcept {
    return {QueryOutcome::unavailable, {}, {}, {}, false, false, false, false};
}

[[nodiscard]] QueryResult absent() noexcept {
    return {QueryOutcome::not_found, {}, {}, {}, false, false, true, true};
}

}  // namespace

bool valid(const TrueMarketsVenueConfig& config) noexcept {
    return spec_agrees_with_profile(config.spec, config.profile) && config.flow != ExecutionFlow::unset &&
           config.chain != Chain::unset && !config.base_asset.empty() && !config.quote_asset.empty() &&
           config.quote_decimals <= 18 && config.spec.base_decimals <= 18 && config.max_fee_bps > 0 &&
           config.max_fee_bps <= kBasisPoints && config.quote_ttl > 0 && config.status_interval > 0 &&
           config.order_rate.capacity >= 2 && config.order_rate.refill_interval > 0 &&
           config.query_rate.capacity >= 1 && config.query_rate.refill_interval > 0 &&
           config.definitive_rejection_count <= kMaxTmDefinitiveRejections && config.caller.value() != 0 &&
           config.session.value() != 0 && config.first_signing_sequence != 0;
}

TrueMarketsVenue::TrueMarketsVenue(const TrueMarketsVenueConfig& config, const TrueMarketsApi api,
                                   const Signer signer, const TrueMarketsClock clock) noexcept
    : config_(config),
      api_(api),
      signer_(signer),
      clock_(clock),
      configured_(valid(config) && api.call_fn != nullptr && signer.sign_fn != nullptr &&
                  clock.now_fn != nullptr),
      signing_sequence_(config.first_signing_sequence - 1),
      order_bucket_{config.order_rate.capacity, 0},
      query_bucket_{config.query_rate.capacity, 0} {}

TmOrder* TrueMarketsVenue::order_for(const ClusterOrderId order) noexcept {
    for (std::size_t i = 0; i < order_count_; ++i)
        if (orders_[i].cluster_order_id == order) return &orders_[i];
    return nullptr;
}

const TmOrder* TrueMarketsVenue::find_order(const ClusterOrderId order) const noexcept {
    for (std::size_t i = 0; i < order_count_; ++i)
        if (orders_[i].cluster_order_id == order) return &orders_[i];
    return nullptr;
}

bool TrueMarketsVenue::take(Bucket& bucket, const TmRateLimit& limit, const std::uint32_t count,
                            const Tick now) noexcept {
    if (now > bucket.refilled_at) {
        const auto gained = (now - bucket.refilled_at) / limit.refill_interval;
        if (gained >= limit.capacity) {
            bucket.tokens = limit.capacity;
            bucket.refilled_at = now;
        } else if (gained != 0) {
            bucket.tokens = std::min<std::uint32_t>(limit.capacity, bucket.tokens + static_cast<std::uint32_t>(gained));
            bucket.refilled_at += gained * limit.refill_interval;
        }
    }
    if (bucket.tokens < count) return false;
    bucket.tokens -= count;
    return true;
}

ApiReply TrueMarketsVenue::call(const ApiCall kind, const std::string_view request, const Tick now) noexcept {
    ++counters_.api_calls[static_cast<std::size_t>(kind)];
    auto reply = api_.call(kind, request, response_, now);
    if (reply.length > response_.size()) reply = {ApiStatus::no_response, 0};
    return reply;
}

void TrueMarketsVenue::queue(const VenueObservation& observation) noexcept {
    // Callers reserve room before any decision that needs it.
    queued_[queued_count_++] = {observation, false};
}

VenueObservation TrueMarketsVenue::report(const VenueObservationKind kind, const TmOrder& order,
                                          const std::string_view evidence) noexcept {
    VenueObservation observation;
    observation.kind = kind;
    observation.venue_id = config_.spec.venue_id;
    observation.venue_session_id = config_.session;
    observation.venue_sequence = VenueSequence{++venue_sequence_};
    observation.cluster_order_id = order.cluster_order_id;
    observation.effect_id = order.effect_id;
    observation.venue_order_id = order.venue_order_id;
    observation.evidence_digest = digest_of(evidence);
    return observation;
}

SendOutcome TrueMarketsVenue::kill(TmOrder& order, const OrderEffect& effect) noexcept {
    ++counters_.fok_kills;
    order.phase = TmPhase::abandoned_before_submit;
    auto observation = report(VenueObservationKind::order_rejected, order, "velocita/truemarkets/local-kill");
    observation.venue_order_id = {};
    observation.effect_id = effect.effect_id;
    queue(observation);
    return SendOutcome::business_rejected;
}

SendOutcome TrueMarketsVenue::send(const OrderEffect& effect, const Tick now) noexcept {
    if (!configured_ || validate(effect) != ContractError::none || effect.venue_id != config_.spec.venue_id ||
        effect.instrument_id != config_.spec.instrument_id || queued_count_ == queued_.size()) {
        return SendOutcome::pre_send_failure;
    }
    if (effect.operation != EffectOperation::submit || effect.time_in_force != TimeInForce::fill_or_kill) {
        // Nothing rests on a swap venue, so there is nothing to cancel, and a
        // swap is fill-or-kill by nature. Refused locally; no call is made.
        TmOrder local{effect.cluster_order_id, effect.effect_id};
        queue(report(VenueObservationKind::order_rejected, local, "velocita/truemarkets/unsupported"));
        return SendOutcome::business_rejected;
    }
    auto* order = order_for(effect.cluster_order_id);
    // NO RESEND WITHOUT PROOF. Once a call that can create exposure has been
    // made for this order, only reconciliation may say what happened; a
    // second submission would be a second swap.
    if (order != nullptr && order->committal_call_made) {
        ++counters_.duplicate_sends_refused;
        return SendOutcome::venue_duplicate;
    }
    if (order != nullptr && order->effect_id != effect.effect_id) return SendOutcome::pre_send_failure;
    if (order == nullptr) {
        if (order_count_ == orders_.size()) {
            // Only a record proving nothing was sent may be reused.
            for (std::size_t i = 0; i < order_count_; ++i) {
                if (orders_[i].committal_call_made || orders_[i].phase != TmPhase::abandoned_before_submit) continue;
                orders_[i] = orders_[--order_count_];
                break;
            }
            if (order_count_ == orders_.size()) return SendOutcome::pre_send_failure;
        }
        order = &orders_[order_count_++];
        *order = {};
        order->cluster_order_id = effect.cluster_order_id;
        order->effect_id = effect.effect_id;
        order->side = effect.side;
        order->quantity = effect.quantity;
        order->limit = effect.price;
    }
    order->phase = TmPhase::abandoned_before_submit;

    const auto authorized = authorization(effect, config_);
    std::array<char, kMaxDecimalTextBytes> rendered{};
    const auto rendered_length = render_quantity(effect.quantity, config_.spec, rendered);
    const auto qty = rendered_length == 0 ? std::nullopt : tm_text({rendered.data(), rendered_length});
    if (!authorized || !qty) return SendOutcome::pre_send_failure;

    // Both calls of the send are paid for up front, so a local refusal never
    // strands a signed payload half way.
    if (!take(order_bucket_, config_.order_rate, 2, now)) {
        ++counters_.local_rate_limited;
        return SendOutcome::rate_limited;
    }

    const bool prepare = config_.flow == ExecutionFlow::prepare_then_execute;
    const TradeArguments arguments{effect.side, config_.base_asset, *qty, QtyUnit::base, config_.chain};
    const auto encoded = encode_trade_call(++rpc_id_, prepare ? TradeTool::trade_prepare : TradeTool::get_quote,
                                           arguments, request_);
    if (encoded.error != EncodeError::none) return SendOutcome::pre_send_failure;
    const auto first = call(prepare ? ApiCall::trade_prepare : ApiCall::get_quote,
                            {request_.data(), encoded.bytes_written}, now);
    const Tick received_at = clock_.now();
    const std::string_view body{response_.data(), first.length};

    // The (base-unit) reply amounts: qty in base, qty_out and fee in quote.
    TradeReply reply;
    const bool decoded =
        first.status == ApiStatus::completed &&
        decode_trade_reply(body, prepare ? ReplyShape::prepare : ReplyShape::quote,
                           {config_.spec.base_decimals, config_.quote_decimals, config_.quote_decimals},
                           reply) == DecodeError::none;
    if (prepare) {
        // trade_prepare may itself submit (D-26). Anything short of a readable
        // reply with payloads to sign is an exposure we cannot rule out, and
        // an error or pacing label proves nothing about it.
        if (!decoded || reply.payload_count == 0) {
            order->committal_call_made = true;
            order->phase = TmPhase::uncertain;
            if (decoded) {
                order->handle = reply.handle;
                order->handle_is_order_id = true;
                order->venue_order_id = VenueOrderId{id_from("velocita/truemarkets/order/v1", reply.handle.view())};
            }
            order->next_status_at = now + config_.status_interval;
            return SendOutcome::ambiguous;
        }
        order->handle_is_order_id = true;
    } else {
        // get_quote is non-committal: whatever it answered, nothing can execute.
        if (first.status == ApiStatus::rate_limited) return SendOutcome::rate_limited;
        if (!decoded || reply.payload_count == 0) return SendOutcome::pre_send_failure;
        order->handle_is_order_id = false;
    }
    order->handle = reply.handle;

    if (reply.side != effect.side || reply.base_asset != config_.base_asset ||
        reply.quote_asset != config_.quote_asset || reply.fee_asset != config_.quote_asset ||
        reply.qty_atoms != authorized->base_atoms) {
        return SendOutcome::pre_send_failure;
    }
    const auto notional = executed_notional(effect.side, reply.qty_out_atoms, reply.fee_atoms);
    if (reply.issue_count != 0 || !notional ||
        !within_authorization(effect.side, *notional, reply.fee_atoms, authorized->limit_notional,
                              config_.max_fee_bps)) {
        return kill(*order, effect);
    }

    SigningRequest request;
    request.intent = {config_.caller,        ++signing_sequence_,     effect.venue_id,
                      effect.account_id,     effect.instrument_id,    effect.effect_id,
                      effect.cluster_order_id, effect.payload_digest, effect.policy_id,
                      effect.policy_version, effect.side,             authorized->base_atoms,
                      authorized->quote_bound, received_at + config_.quote_ttl};
    request.venue_handle = order->handle.view();
    request.payload_count = reply.payload_count;
    std::copy(reply.payloads.begin(), reply.payloads.end(), request.payloads.begin());
    ++counters_.signer_calls;
    const auto signature = signer_.sign(request);
    if (signature.verdict != SignerVerdict::signed_ok || signature.signature_count != reply.payload_count) {
        ++counters_.signer_refusals;
        return SendOutcome::pre_send_failure;
    }
    order->signature_id = signature.signature_id;
    // The quote carries no recorded expiry; a stale one is never submitted.
    if (clock_.now() > received_at + config_.quote_ttl) return SendOutcome::pre_send_failure;

    const auto execute = encode_trade_execute(++rpc_id_, order->handle,
                                              std::span(signature.signatures.data(), signature.signature_count),
                                              request_);
    if (execute.error != EncodeError::none) return SendOutcome::pre_send_failure;
    order->committal_call_made = true;
    order->phase = TmPhase::uncertain;
    order->next_status_at = now + config_.status_interval;
    const auto submitted = call(ApiCall::trade_execute, {request_.data(), execute.bytes_written}, now);
    ExecuteReply accepted;
    if (submitted.status == ApiStatus::completed &&
        decode_execute_reply({response_.data(), submitted.length}, accepted) == DecodeError::none) {
        if (one_of(accepted.status.view(), kAcceptedWords)) {
            order->phase = TmPhase::accepted;
            order->handle = accepted.order_id;
            order->handle_is_order_id = true;
            order->venue_order_id = VenueOrderId{id_from("velocita/truemarkets/order/v1", order->handle.view())};
            queue(report(VenueObservationKind::order_acknowledged, *order, {response_.data(), submitted.length}));
            return SendOutcome::accepted;
        }
        for (std::size_t i = 0; i < config_.definitive_rejection_count; ++i) {
            if (accepted.status != config_.definitive_rejections[i]) continue;
            order->phase = TmPhase::not_executed;
            auto rejected = report(VenueObservationKind::order_rejected, *order, {response_.data(), submitted.length});
            rejected.venue_order_id = {};
            queue(rejected);
            return SendOutcome::business_rejected;
        }
    }
    // Timeout, pacing, a tool error or an unrecognised answer to the committal
    // call: the swap may be executing. Reconciliation asks by the id we hold.
    if (order->handle_is_order_id || config_.status_accepts_quote_id) {
        order->venue_order_id = VenueOrderId{id_from("velocita/truemarkets/order/v1", order->handle.view())};
    }
    return SendOutcome::ambiguous;
}

bool TrueMarketsVenue::refresh_status(TmOrder& order, const Tick now) noexcept {
    const bool queryable = !order.handle.empty() && (order.handle_is_order_id || config_.status_accepts_quote_id);
    if (!queryable || queued_.size() - queued_count_ < 2) return false;
    if (!take(query_bucket_, config_.query_rate, 1, now)) return false;
    std::array<char, kMaxTmRequestBytes> path{};
    const auto encoded = encode_order_status_path(order.handle, path);
    if (encoded.error != EncodeError::none) return false;
    order.next_status_at = now + config_.status_interval;
    const auto reply = call(ApiCall::order_status, {path.data(), encoded.bytes_written}, now);
    if (reply.status != ApiStatus::completed) return false;
    const std::string_view body{response_.data(), reply.length};
    StatusReply status;
    if (decode_status_reply(body, {config_.spec.base_decimals, config_.quote_decimals, config_.quote_decimals},
                            status) != DecodeError::none ||
        status.order_id != order.handle) {
        return false;
    }
    if (status.status.view() == kSettledWord && status.has_execution) {
        absorb_settlement(order, status, body);
    } else if (one_of(status.status.view(), kNotExecutedWords)) {
        order.phase = TmPhase::not_executed;
    }
    return true;
}

void TrueMarketsVenue::absorb_settlement(TmOrder& order, const StatusReply& status,
                                         const std::string_view evidence) noexcept {
    if (order.venue_order_id.value() == 0) {
        order.venue_order_id = VenueOrderId{id_from("velocita/truemarkets/order/v1", order.handle.view())};
    }
    if (order.phase == TmPhase::uncertain) {
        queue(report(VenueObservationKind::order_acknowledged, order, evidence));
    }
    order.phase = TmPhase::settled;

    const auto lot = config_.spec.lot_base_atoms;
    const bool whole = status.qty_atoms > 0 && status.qty_atoms % lot == 0;
    const auto lots = whole ? status.qty_atoms / lot : 0;
    const auto notional = executed_notional(order.side, status.qty_out_atoms, status.fee_atoms);
    const auto limit_per_lot = multiply(order.limit.atoms(), config_.profile.quote_micros_per_tick_lot.atoms());
    const auto limit_notional = limit_per_lot ? multiply(*limit_per_lot, lots) : std::nullopt;
    const auto denominator = multiply(lots, config_.profile.quote_micros_per_tick_lot.atoms());
    bool breach = !whole || lots != order.quantity.atoms() || !notional || !limit_notional ||
                  !within_authorization(order.side, *notional, status.fee_atoms, *limit_notional,
                                        config_.max_fee_bps);
    // What the venue did is booked even when it breaches the authorization:
    // the position must reflect reality. It cannot be booked when it does not
    // fit the order at all.
    if (whole && lots > 0 && lots <= order.quantity.atoms() && notional && denominator && *denominator > 0) {
        // Rounded against us: a buy's price up, a sell's down.
        const auto ticks = order.side == Side::buy ? 1 + (*notional - 1) / *denominator : *notional / *denominator;
        if (ticks > 0) {
            auto fill = report(VenueObservationKind::fill, order, evidence);
            fill.execution_id = ExecutionId{id_from("velocita/truemarkets/execution/v1", status.tx_signature.view())};
            fill.side = order.side;
            fill.price = PriceTicks::from_atoms(ticks);
            fill.quantity = QuantityLots::from_atoms(lots);
            fill.fee = FeeMicros::from_atoms(status.fee_atoms);
            queue(fill);
            order.filled = fill.quantity;
        } else {
            breach = true;
        }
    }
    if (breach) {
        order.breach = true;
        ++counters_.breaches;
    }
}

std::size_t TrueMarketsVenue::poll(const std::span<VenueObservation> out, const Tick now) noexcept {
    if (!configured_) return 0;
    std::size_t asked = 0;
    for (std::size_t i = 0; i < order_count_ && asked < kMaxTmStatusCallsPerPoll; ++i) {
        auto& order = orders_[i];
        if (!order.committal_call_made || order.next_status_at > now ||
            (order.phase != TmPhase::accepted && order.phase != TmPhase::uncertain)) {
            continue;
        }
        ++asked;
        (void)refresh_status(order, now);
    }
    std::size_t written = 0;
    for (std::size_t i = 0; i < queued_count_ && written < out.size(); ++i) {
        if (queued_[i].delivered) continue;
        queued_[i].delivered = true;
        out[written++] = queued_[i].observation;
    }
    return written;
}

void TrueMarketsVenue::acknowledge(const VenueObservation& observation) noexcept {
    const auto acknowledged = observation_fingerprint(observation);
    std::size_t kept = 0;
    for (std::size_t i = 0; i < queued_count_; ++i) {
        if (queued_[i].delivered && observation_fingerprint(queued_[i].observation) == acknowledged) continue;
        queued_[kept++] = queued_[i];
    }
    for (std::size_t i = kept; i < queued_count_; ++i) queued_[i] = {};
    queued_count_ = kept;
}

QueryResult TrueMarketsVenue::query(const ClusterOrderId id, const Tick now) noexcept {
    if (!configured_) return unavailable();
    auto* order = order_for(id);
    // True Markets takes no client order id, so an order this process has no
    // record of (a restart, a lost prepare reply) cannot be asked about.
    // Silence, never absence: absence would release a reservation that may be
    // real at the venue.
    if (order == nullptr) return unavailable();
    if (!order->committal_call_made) return absent();
    if (order->phase == TmPhase::accepted || order->phase == TmPhase::uncertain) {
        if (!refresh_status(*order, now)) return unavailable();
    }
    if (order->phase == TmPhase::not_executed) return absent();
    QueryResult result;
    result.outcome = QueryOutcome::known;
    result.venue_order_id = order->venue_order_id;
    result.window_complete = true;
    result.ordered_after_relevant_sends = true;
    if (order->phase == TmPhase::settled) {
        // An atomic swap leaves nothing to fill: the remainder is not working.
        result.filled_quantity = order->filled;
        result.inconsistent = order->breach;
        return result;
    }
    // The venue holds it and has not settled it.
    result.remaining_quantity = order->quantity;
    return result;
}

namespace {

SendOutcome transport_send(void* context, const OrderEffect& effect, const Tick now) {
    return static_cast<TrueMarketsVenue*>(context)->send(effect, now);
}

std::size_t transport_poll(void* context, const std::span<VenueObservation> out, const Tick now) {
    return static_cast<TrueMarketsVenue*>(context)->poll(out, now);
}

void transport_acknowledge(void* context, const VenueObservation& observation) {
    static_cast<TrueMarketsVenue*>(context)->acknowledge(observation);
}

QueryResult transport_query(void* context, const ClusterOrderId order, const Tick now) {
    return static_cast<TrueMarketsVenue*>(context)->query(order, now);
}

VenueSessionId transport_session(void* context) { return static_cast<TrueMarketsVenue*>(context)->session(); }

}  // namespace

sim::VenueTransport truemarkets_venue_transport(TrueMarketsVenue& venue) noexcept {
    sim::VenueTransport transport;
    transport.send_fn = &transport_send;
    transport.poll_fn = &transport_poll;
    transport.acknowledge_fn = &transport_acknowledge;
    transport.query_fn = &transport_query;
    transport.session_fn = &transport_session;
    transport.context = &venue;
    return transport;
}

LiveArmingRefusal evaluate_live_arming(const LiveArmingInputs& inputs, const bool live_transport_compiled) noexcept {
    if (!inputs.armed) return LiveArmingRefusal::not_armed;
    if (inputs.signer_endpoint.empty()) return LiveArmingRefusal::no_signer;
    if (!live_transport_compiled) return LiveArmingRefusal::no_live_transport;
    return LiveArmingRefusal::none;
}

std::string_view describe(const LiveArmingRefusal refusal) noexcept {
    switch (refusal) {
    case LiveArmingRefusal::none: return "armed";
    case LiveArmingRefusal::not_armed: return "not armed (pass --arm-live-venue)";
    case LiveArmingRefusal::no_signer: return "no signer (pass --signer-endpoint)";
    case LiveArmingRefusal::no_live_transport: return "no live venue transport is compiled into this build";
    }
    return "unknown";
}

}  // namespace velocita::v2::truemarkets
