#include "velocita/v2/paper_venue.hpp"

#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <limits>

namespace velocita::v2::paper {
namespace {

// Deterministic paper evidence, not venue proof. Byte 9 tags it as paper.
[[nodiscard]] Digest256 paper_evidence(const std::uint64_t sequence) noexcept {
    Digest256 value;
    for (std::size_t index = 0; index < 8; ++index) {
        value.bytes[index] = static_cast<std::byte>((sequence >> (index * 8)) & 0xFF);
    }
    value.bytes[8] = std::byte{0x01};
    value.bytes[9] = std::byte{0x50};
    return value;
}

[[nodiscard]] bool renderable(const OrderEffect& effect,
                              const VenueInstrumentSpec& spec) noexcept {
    std::array<char, kMaxDecimalTextBytes> text{};
    return render_price(effect.price, spec, text) != 0 &&
           render_quantity(effect.quantity, spec, text) != 0;
}

}  // namespace

bool valid(const PaperVenueConfig& config) noexcept {
    return spec_agrees_with_profile(config.spec, config.profile) &&
           config.buy_fee_bps <= kBasisPointsPerUnit &&
           config.sell_fee_bps <= kBasisPointsPerUnit &&
           config.quote_balance.atoms() >= 0 && config.base_balance.atoms() >= 0;
}

std::optional<MoneyMicros> paper_notional(const PriceTicks price,
                                          const QuantityLots quantity,
                                          const MoneyMicros quote_micros_per_tick_lot) noexcept {
    if (price.atoms() <= 0 || quantity.atoms() <= 0 ||
        quote_micros_per_tick_lot.atoms() <= 0) {
        return std::nullopt;
    }
    const auto per_lot = quote_micros_per_tick_lot.checked_multiply(price.atoms());
    if (!per_lot) return std::nullopt;
    return per_lot->checked_multiply(quantity.atoms());
}

std::optional<FeeMicros> paper_fee(const MoneyMicros notional,
                                   const std::uint32_t bps) noexcept {
    if (notional.atoms() < 0 || bps > kBasisPointsPerUnit) return std::nullopt;
    constexpr std::int64_t unit = kBasisPointsPerUnit;
    const auto rate = static_cast<std::int64_t>(bps);
    // Split so no intermediate exceeds int64: remainder * rate < 10'000 * 10'000.
    const auto whole = MoneyMicros::from_atoms(notional.atoms() / unit).checked_multiply(rate);
    if (!whole) return std::nullopt;
    const auto remainder_product = (notional.atoms() % unit) * rate;
    const auto rounded_up = remainder_product == 0 ? 0 : 1 + (remainder_product - 1) / unit;
    const auto total = whole->checked_add(MoneyMicros::from_atoms(rounded_up));
    if (!total) return std::nullopt;
    return FeeMicros::from_atoms(total->atoms());
}

PaperVenue::PaperVenue(const PaperVenueConfig& config, const PaperFaultPolicy& faults,
                       const std::uint64_t seed) noexcept
    : config_(config),
      policy_(faults),
      configured_(valid(config) && faults.split_pieces >= 1 &&
                  faults.split_pieces <= kMaxPaperSplitPieces),
      random_state_(seed),
      balances_{config.quote_balance, config.base_balance, {}} {}

std::uint64_t PaperVenue::next_random() noexcept {
    random_state_ += 0x9e3779b97f4a7c15ULL;
    auto value = random_state_;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

bool PaperVenue::roll(const std::uint16_t permille) noexcept {
    const auto draw = next_random() % 1000;
    return permille != 0 && draw < permille;
}

const PaperOrder* PaperVenue::find_order(const ClusterOrderId order) const noexcept {
    for (std::size_t index = 0; index < order_count_; ++index) {
        if (orders_[index].cluster_order_id == order) return &orders_[index];
    }
    return nullptr;
}

VenueObservation PaperVenue::report(const VenueObservationKind kind,
                                    const OrderEffect& effect) noexcept {
    VenueObservation observation;
    observation.kind = kind;
    observation.venue_id = config_.spec.venue_id;
    observation.venue_session_id = session();
    observation.venue_sequence = VenueSequence{++venue_sequence_};
    observation.cluster_order_id = effect.cluster_order_id;
    observation.effect_id = effect.effect_id;
    observation.evidence_digest = paper_evidence(venue_sequence_);
    return observation;
}

void PaperVenue::schedule(const VenueObservation& observation, const Tick due,
                          const bool duplicate, const bool withheld) noexcept {
    // Capacity is reserved by the caller before any mutation.
    scheduled_[scheduled_count_++] = {due, ++ordinal_, observation, false, withheld};
    if (duplicate) {
        // Byte-identical, one tick later, so it can arrive after the original
        // has been committed and acknowledged.
        scheduled_[scheduled_count_++] = {due + 1, ++ordinal_, observation, false, withheld};
    }
}

SendOutcome PaperVenue::refuse(const OrderEffect& effect, const PaperRejectReason reason,
                               const Tick due, const bool duplicate,
                               const bool drop_response) noexcept {
    last_reject_ = reason;
    // A killed order is not recorded: the venue holds nothing, so a later query
    // truthfully answers not_found and reconciliation releases the hold.
    if (drop_response) return SendOutcome::ambiguous;
    schedule(report(VenueObservationKind::order_rejected, effect), due, duplicate, false);
    return SendOutcome::business_rejected;
}

SendOutcome PaperVenue::send(const OrderEffect& effect, const Tick now) noexcept {
    PaperFaults faults;
    faults.delay = roll(policy_.delay_permille);
    faults.duplicate = roll(policy_.duplicate_permille);
    faults.reorder = roll(policy_.reorder_permille);
    faults.drop_request = roll(policy_.drop_request_permille);
    faults.drop_response = roll(policy_.drop_response_permille);
    faults.reject = roll(policy_.reject_permille);
    faults.split = roll(policy_.split_permille);
    last_faults_ = faults;
    last_reject_ = PaperRejectReason::none;

    if (!configured_ || validate(effect) != ContractError::none ||
        effect.venue_id != config_.spec.venue_id ||
        effect.instrument_id != config_.spec.instrument_id) {
        return SendOutcome::pre_send_failure;
    }
    if (faults.drop_request) return SendOutcome::ambiguous;

    const auto copies = faults.duplicate ? std::size_t{2} : std::size_t{1};
    const auto free_slots = scheduled_.size() - scheduled_count_;
    const Tick due = now + config_.latency + (faults.delay ? policy_.delay_ticks : 0);

    if (effect.operation != EffectOperation::submit) {
        if (free_slots < copies) return SendOutcome::pre_send_failure;
        return refuse(effect,
                      effect.operation == EffectOperation::cancel
                          ? PaperRejectReason::nothing_resting
                          : PaperRejectReason::unsupported_operation,
                      due, faults.duplicate, false);
    }
    if (find_order(effect.cluster_order_id) != nullptr) return SendOutcome::venue_duplicate;
    if (!renderable(effect, config_.spec)) return SendOutcome::pre_send_failure;

    const auto total = effect.quantity.atoms();
    const auto pieces = faults.split
                            ? std::min<std::int64_t>(policy_.split_pieces, total)
                            : std::int64_t{1};
    const auto bps = effect.side == Side::buy ? config_.buy_fee_bps : config_.sell_fee_bps;
    std::array<QuantityLots, kMaxPaperSplitPieces> piece_quantity{};
    std::array<FeeMicros, kMaxPaperSplitPieces> piece_fee{};
    FeeMicros fee{};
    for (std::int64_t index = 0; index < pieces; ++index) {
        const auto lots = total / pieces + (index + 1 == pieces ? total % pieces : 0);
        piece_quantity[index] = QuantityLots::from_atoms(lots);
        const auto notional = paper_notional(effect.price, piece_quantity[index],
                                             config_.profile.quote_micros_per_tick_lot);
        if (!notional) return SendOutcome::pre_send_failure;
        // Rounded per piece: a split never costs less than the whole.
        const auto charged = paper_fee(*notional, bps);
        if (!charged) return SendOutcome::pre_send_failure;
        const auto sum = fee.checked_add(*charged);
        if (!sum) return SendOutcome::pre_send_failure;
        piece_fee[index] = *charged;
        fee = *sum;
    }
    const auto notional = paper_notional(effect.price, effect.quantity,
                                         config_.profile.quote_micros_per_tick_lot);
    const auto fee_money = MoneyMicros::from_atoms(fee.atoms());
    const auto cost = notional ? notional->checked_add(fee_money) : std::nullopt;
    const auto proceeds = notional ? notional->checked_sub(fee_money) : std::nullopt;
    if (!cost || !proceeds) return SendOutcome::pre_send_failure;

    // Every refusal below schedules at most `copies` reports and an execution
    // at most (1 + pieces) * copies, so checking here keeps all paths pre-mutation.
    const auto needed = static_cast<std::size_t>(1 + pieces) * copies;
    if (order_count_ == orders_.size() || free_slots < needed) {
        return SendOutcome::pre_send_failure;
    }

    if (effect.time_in_force != TimeInForce::fill_or_kill) {
        return refuse(effect, PaperRejectReason::not_fill_or_kill, due, faults.duplicate,
                      faults.drop_response);
    }
    if (faults.reject) {
        return refuse(effect, PaperRejectReason::fault, due, faults.duplicate,
                      faults.drop_response);
    }
    if (effect.side == Side::buy && *cost > balances_.quote) {
        return refuse(effect, PaperRejectReason::insufficient_quote, due, faults.duplicate,
                      faults.drop_response);
    }
    if (effect.side == Side::sell && effect.quantity > balances_.base) {
        return refuse(effect, PaperRejectReason::insufficient_base, due, faults.duplicate,
                      faults.drop_response);
    }
    const auto next_quote = effect.side == Side::buy ? balances_.quote.checked_sub(*cost)
                                                     : balances_.quote.checked_add(*proceeds);
    const auto next_base = effect.side == Side::buy
                               ? balances_.base.checked_add(effect.quantity)
                               : balances_.base.checked_sub(effect.quantity);
    const auto next_fees = balances_.fees_paid.checked_add(fee);
    if (!next_quote || !next_base || !next_fees) return SendOutcome::pre_send_failure;

    const VenueOrderId venue_order{++venue_order_counter_};
    orders_[order_count_++] = {effect.cluster_order_id, effect.effect_id, venue_order,
                               effect.side, effect.price, effect.quantity, *notional, fee};
    balances_ = {*next_quote, *next_base, *next_fees};

    // A dropped response still executed. Its reports are withheld, not lost:
    // only a query releases them, because reconciliation needs identified
    // executions and a cumulative total carries none.
    const bool withheld = faults.drop_response;
    auto ack = report(VenueObservationKind::order_acknowledged, effect);
    ack.venue_order_id = venue_order;
    // Sequences follow venue order; reorder changes only delivery order.
    std::array<VenueObservation, kMaxPaperSplitPieces> fills{};
    for (std::int64_t index = 0; index < pieces; ++index) {
        auto& fill = fills[index];
        fill = report(VenueObservationKind::fill, effect);
        fill.venue_order_id = venue_order;
        fill.execution_id = ExecutionId{++execution_counter_};
        fill.side = effect.side;
        fill.price = effect.price;
        fill.quantity = piece_quantity[index];
        fill.fee = piece_fee[index];
    }
    const auto count = static_cast<Tick>(pieces);
    if (faults.reorder) {
        for (Tick index = 0; index < count; ++index) {
            schedule(fills[index], due + index, faults.duplicate, withheld);
        }
        schedule(ack, due + count, faults.duplicate, withheld);
    } else {
        schedule(ack, due, faults.duplicate, withheld);
        for (Tick index = 0; index < count; ++index) {
            schedule(fills[index], due + 1 + index, faults.duplicate, withheld);
        }
    }
    return faults.drop_response ? SendOutcome::ambiguous : SendOutcome::accepted;
}

std::size_t PaperVenue::poll(const std::span<VenueObservation> out, const Tick now) noexcept {
    std::size_t written = 0;
    while (written < out.size()) {
        std::size_t best = scheduled_count_;
        for (std::size_t index = 0; index < scheduled_count_; ++index) {
            const auto& entry = scheduled_[index];
            if (entry.delivered || entry.withheld || entry.due > now) continue;
            if (best == scheduled_count_ || entry.due < scheduled_[best].due ||
                (entry.due == scheduled_[best].due &&
                 entry.ordinal < scheduled_[best].ordinal)) {
                best = index;
            }
        }
        if (best == scheduled_count_) break;
        scheduled_[best].delivered = true;
        out[written++] = scheduled_[best].observation;
    }
    return written;
}

void PaperVenue::acknowledge(const VenueObservation& observation) noexcept {
    // Only DELIVERED copies are released. A duplicate not yet handed over must
    // survive its twin's acknowledgement, or the duplicate fault is untestable.
    const auto acknowledged = observation_fingerprint(observation);
    std::size_t kept = 0;
    for (std::size_t index = 0; index < scheduled_count_; ++index) {
        const auto& entry = scheduled_[index];
        if (entry.delivered && observation_fingerprint(entry.observation) == acknowledged) {
            continue;
        }
        scheduled_[kept++] = entry;
    }
    for (std::size_t index = kept; index < scheduled_count_; ++index) scheduled_[index] = {};
    scheduled_count_ = kept;
}

void PaperVenue::restart_delivery() noexcept {
    for (std::size_t index = 0; index < scheduled_count_; ++index) {
        scheduled_[index].delivered = false;
    }
}

QueryResult PaperVenue::query(const ClusterOrderId order, const Tick now) noexcept {
    last_faults_ = {};
    last_faults_.query_unavailable = roll(policy_.query_unavailable_permille);
    if (!configured_ || last_faults_.query_unavailable) {
        return {QueryOutcome::unavailable, {}, {}, {}, false, false, false, false};
    }
    const auto* found = find_order(order);
    if (found == nullptr) return {QueryOutcome::not_found, {}, {}, {}, false, false, true, true};

    for (std::size_t index = 0; index < scheduled_count_; ++index) {
        auto& entry = scheduled_[index];
        if (!entry.withheld || entry.observation.cluster_order_id != order) continue;
        entry.withheld = false;
        if (entry.due < now) entry.due = now;
    }
    QueryResult result;
    result.outcome = QueryOutcome::known;
    result.venue_order_id = found->venue_order_id;
    result.filled_quantity = found->quantity;
    result.remaining_quantity = {};
    return result;
}

void PaperVenue::record_committed(const sim::CommittedEvidence& evidence) noexcept {
    ledger_.committed_through = std::max(ledger_.committed_through, evidence.index);
    const auto& observation = evidence.observation;
    if (observation.kind != VenueObservationKind::fill) return;
    const auto add = [](std::int64_t& total, const std::int64_t value) noexcept {
        if (value > 0 && total <= std::numeric_limits<std::int64_t>::max() - value) total += value;
    };
    add(ledger_.fee_micros_total, observation.fee.atoms());
    add(observation.side == Side::buy ? ledger_.volume_micros_buy : ledger_.volume_micros_sell,
        evidence.notional_micros);
    ledger_.last_fill_wall_seconds = std::max(ledger_.last_fill_wall_seconds, evidence.wall_seconds);
}

void PaperVenue::read_book(sim::VenueBook& out) const noexcept {
    out = {};
    out.available = configured_;
    out.base = balances_.base;
    out.quote = balances_.quote;
    out.quote_known = true;
    out.unacknowledged_reports = scheduled_count_;
    for (std::size_t index = 0; index < scheduled_count_; ++index) {
        const auto order = scheduled_[index].observation.cluster_order_id;
        const auto listed = std::span(out.pending_orders).first(out.pending_order_count);
        if (std::find(listed.begin(), listed.end(), order) != listed.end()) continue;
        out.pending_orders[out.pending_order_count++] = order;  // 128 reports fit 128 ids
    }
    out.committed_through = ledger_.committed_through;
    out.fee_micros_total = ledger_.fee_micros_total;
    out.volume_micros_buy = ledger_.volume_micros_buy;
    out.volume_micros_sell = ledger_.volume_micros_sell;
    out.last_fill_wall_seconds = ledger_.last_fill_wall_seconds;
}

void PaperVenue::capture(PaperVenueImage& out) const noexcept {
    out.venue_id = config_.spec.venue_id;
    out.instrument_id = config_.spec.instrument_id;
    out.random_state = random_state_;
    out.venue_sequence = venue_sequence_;
    out.venue_order_counter = venue_order_counter_;
    out.execution_counter = execution_counter_;
    out.ordinal = ordinal_;
    out.balances = balances_;
    out.ledger = ledger_;
    out.order_count = order_count_;
    out.orders = orders_;
    out.report_count = scheduled_count_;
    out.reports = {};
    for (std::size_t index = 0; index < scheduled_count_; ++index) {
        const auto& entry = scheduled_[index];
        out.reports[index] = {entry.ordinal, entry.observation, entry.withheld};
    }
}

bool PaperVenue::restore(const PaperVenueImage& image) noexcept {
    const auto sided = [](const Side side) noexcept { return side == Side::buy || side == Side::sell; };
    bool sound = configured_ && image.venue_id == config_.spec.venue_id &&
                 image.instrument_id == config_.spec.instrument_id &&
                 image.order_count <= kMaxPaperOrders && image.report_count <= kMaxPaperObservations &&
                 image.balances.quote.atoms() >= 0 && image.balances.base.atoms() >= 0 &&
                 image.balances.fees_paid.atoms() >= 0 && image.ledger.committed_through >= 0 &&
                 image.ledger.fee_micros_total >= 0 && image.ledger.volume_micros_buy >= 0 &&
                 image.ledger.volume_micros_sell >= 0 && image.ledger.last_fill_wall_seconds >= 0;
    for (std::size_t index = 0; sound && index < image.order_count; ++index) {
        const auto& order = image.orders[index];
        sound = order.cluster_order_id.value() != 0 && order.venue_order_id.value() != 0 &&
                order.venue_order_id.value() <= image.venue_order_counter && sided(order.side) &&
                order.quantity.atoms() > 0 && order.fee.atoms() >= 0;
        for (std::size_t other = 0; sound && other < index; ++other)
            sound = image.orders[other].cluster_order_id != order.cluster_order_id;
    }
    for (std::size_t index = 0; sound && index < image.report_count; ++index) {
        const auto& report = image.reports[index];
        const auto kind = report.observation.kind;
        sound = report.ordinal != 0 && report.ordinal <= image.ordinal &&
                report.observation.venue_id == config_.spec.venue_id &&
                report.observation.venue_sequence.value() <= image.venue_sequence &&
                (kind == VenueObservationKind::order_acknowledged ||
                 kind == VenueObservationKind::order_rejected || kind == VenueObservationKind::fill);
    }
    if (!sound) return false;
    random_state_ = image.random_state;
    venue_sequence_ = image.venue_sequence;
    venue_order_counter_ = image.venue_order_counter;
    execution_counter_ = image.execution_counter;
    ordinal_ = image.ordinal;
    balances_ = image.balances;
    ledger_ = image.ledger;
    order_count_ = image.order_count;
    orders_ = image.orders;
    scheduled_count_ = image.report_count;
    scheduled_ = {};
    for (std::size_t index = 0; index < image.report_count; ++index) {
        const auto& report = image.reports[index];
        scheduled_[index] = {0, report.ordinal, report.observation, false, report.withheld};
    }
    last_faults_ = {};
    last_reject_ = PaperRejectReason::none;
    return true;
}

namespace {

void book_read(void* context, sim::VenueBook& out) {
    static_cast<const PaperVenue*>(context)->read_book(out);
}

void book_committed(void* context, const sim::CommittedEvidence& evidence) {
    static_cast<PaperVenue*>(context)->record_committed(evidence);
}

SendOutcome transport_send(void* context, const OrderEffect& effect, const Tick now) {
    return static_cast<PaperVenue*>(context)->send(effect, now);
}

std::size_t transport_poll(void* context, const std::span<VenueObservation> out,
                           const Tick now) {
    return static_cast<PaperVenue*>(context)->poll(out, now);
}

void transport_acknowledge(void* context, const VenueObservation& observation) {
    static_cast<PaperVenue*>(context)->acknowledge(observation);
}

QueryResult transport_query(void* context, const ClusterOrderId order, const Tick now) {
    return static_cast<PaperVenue*>(context)->query(order, now);
}

VenueSessionId transport_session(void* context) {
    return static_cast<PaperVenue*>(context)->session();
}

}  // namespace

sim::VenueTransport paper_venue_transport(PaperVenue& venue) noexcept {
    sim::VenueTransport transport;
    transport.send_fn = &transport_send;
    transport.poll_fn = &transport_poll;
    transport.acknowledge_fn = &transport_acknowledge;
    transport.query_fn = &transport_query;
    transport.session_fn = &transport_session;
    transport.context = &venue;
    return transport;
}

sim::VenueBookSource paper_venue_book(PaperVenue& venue) noexcept {
    return {&book_read, &book_committed, &venue};
}

}  // namespace velocita::v2::paper
