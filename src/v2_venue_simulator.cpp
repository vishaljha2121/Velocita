#include "velocita/v2/venue_simulator.hpp"

#include "velocita/v2/digest.hpp"

#include <algorithm>

namespace velocita::v2::sim {
namespace {

[[nodiscard]] Digest256 evidence_of(const std::uint64_t marker) noexcept {
    Digest256 value;
    for (std::size_t index = 0; index < 8; ++index) {
        value.bytes[index] = static_cast<std::byte>((marker >> (index * 8)) & 0xFF);
    }
    value.bytes[8] = std::byte{0x01};
    return value;
}

}

SimulatedVenue::SimulatedVenue(const std::uint64_t seed, const FaultPolicy policy,
                               const VenueId venue_id) noexcept
    : seed_(seed), random_state_(seed), policy_(policy), venue_id_(venue_id) {}

std::uint64_t SimulatedVenue::next_random() noexcept {
    random_state_ += 0x9e3779b97f4a7c15ULL;
    auto value = random_state_;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

bool SimulatedVenue::roll(const std::uint16_t permille) noexcept {
    const auto draw = next_random() % 1000;
    return permille != 0 && draw < permille;
}

FaultSet SimulatedVenue::choose_faults(const bool for_query) noexcept {
    FaultSet faults;
    if (for_query) {
        faults.disconnect = roll(policy_.disconnect_permille);
        faults.inconsistent_query = roll(policy_.inconsistent_query_permille);
        faults.partial_window = roll(policy_.partial_window_permille);
        return faults;
    }
    faults.rate_limit = roll(policy_.rate_limit_permille);
    faults.disconnect = roll(policy_.disconnect_permille);
    faults.loss = roll(policy_.loss_permille);
    faults.duplication = roll(policy_.duplication_permille);
    faults.reorder = roll(policy_.reorder_permille);
    faults.fill_after_cancel = roll(policy_.fill_after_cancel_permille);
    faults.partial_fill = roll(policy_.partial_fill_permille);
    faults.delay = roll(policy_.delay_permille);
    return faults;
}

Tick SimulatedVenue::latency(const FaultSet& faults) noexcept {
    const auto span = policy_.maximum_latency >= policy_.minimum_latency
                          ? policy_.maximum_latency - policy_.minimum_latency + 1
                          : 1;
    auto ticks = policy_.minimum_latency + (next_random() % span);
    if (faults.delay) ticks += policy_.delay_penalty;
    return ticks;
}

SimulatedVenue::Resting* SimulatedVenue::find(const ClusterOrderId order) noexcept {
    for (auto& resting : resting_) {
        if (resting.cluster_order_id == order) return &resting;
    }
    return nullptr;
}

VenueObservation SimulatedVenue::base_observation(const VenueObservationKind kind,
                                                  const Resting& order) noexcept {
    VenueObservation observation;
    observation.kind = kind;
    observation.venue_id = venue_id_;
    observation.venue_session_id = session_;
    observation.venue_sequence = VenueSequence{++venue_sequence_};
    observation.cluster_order_id = order.cluster_order_id;
    observation.effect_id = order.effect_id;
    observation.venue_order_id = order.venue_order_id;
    observation.evidence_digest = evidence_of(venue_sequence_);
    return observation;
}

void SimulatedVenue::schedule(VenueObservation observation, const Tick due,
                              const bool duplicate, const bool reorder) {
    const auto ordinal = reorder ? 0 : ++ordinal_;
    scheduled_.push_back({due, ordinal, observation, false});
    if (duplicate) {
        scheduled_.push_back({due, ordinal, observation, false});
    }
}

SendOutcome SimulatedVenue::send(const OrderEffect& effect, const Tick now) {
    const auto faults = choose_faults(false);
    fault_log_.push_back(faults);

    if (faults.rate_limit) return SendOutcome::rate_limited;

    if (faults.disconnect) {
        session_ = VenueSessionId{session_.value() + 1};
        return SendOutcome::ambiguous;
    }

    if (effect.operation == EffectOperation::cancel) {
        auto* order = find(effect.cluster_order_id);
        if (order == nullptr) return SendOutcome::business_rejected;
        order->cancelled = true;
        if (!faults.loss) {
            auto cancelled =
                base_observation(VenueObservationKind::cancelled, *order);
            cancelled.effect_id = effect.effect_id;
            schedule(cancelled, now + latency(faults), faults.duplication,
                     faults.reorder);
        }

        if (faults.fill_after_cancel && order->filled < order->original) {
            auto fill = base_observation(VenueObservationKind::fill, *order);
            fill.execution_id = ExecutionId{++execution_counter_};
            fill.side = order->side;
            fill.price = order->price;
            fill.quantity = QuantityLots::from_atoms(1);
            order->filled = QuantityLots::from_atoms(order->filled.atoms() + 1);
            executions_.push_back({fill.venue_id, fill.execution_id,
                                   fill.cluster_order_id, fill.side,
                                   fill.quantity});
            schedule(fill, now + latency(faults) + 1, false, false);
        }
        return SendOutcome::accepted;
    }

    if (find(effect.cluster_order_id) != nullptr) {
        return SendOutcome::venue_duplicate;
    }

    Resting order;
    order.cluster_order_id = effect.cluster_order_id;
    order.effect_id = effect.effect_id;
    order.venue_order_id = VenueOrderId{++venue_order_counter_};
    order.side = effect.side;
    order.price = effect.price;
    order.original = effect.quantity;
    resting_.push_back(order);

    auto& resting = resting_.back();
    if (faults.loss) {
        return SendOutcome::accepted;
    }

    auto ack = base_observation(VenueObservationKind::order_acknowledged, resting);
    schedule(ack, now + latency(faults), faults.duplication, faults.reorder);

    const auto total = resting.original.atoms();
    if (faults.partial_fill && total > 1) {
        const auto pieces = total / 2;
        for (std::int64_t index = 0; index < pieces; ++index) {
            auto fill = base_observation(VenueObservationKind::fill, resting);
            fill.execution_id = ExecutionId{++execution_counter_};
            fill.side = resting.side;
            fill.price = resting.price;
            fill.quantity = QuantityLots::from_atoms(1);
            executions_.push_back({fill.venue_id, fill.execution_id,
                                   fill.cluster_order_id, fill.side,
                                   fill.quantity});
            schedule(fill, now + latency(faults) + static_cast<Tick>(index) + 1,
                     false, false);
        }
        resting.filled = QuantityLots::from_atoms(pieces);
    } else {
        auto fill = base_observation(VenueObservationKind::fill, resting);
        fill.execution_id = ExecutionId{++execution_counter_};
        fill.side = resting.side;
        fill.price = resting.price;
        fill.quantity = resting.original;

        executions_.push_back({fill.venue_id, fill.execution_id,
                               fill.cluster_order_id, fill.side,
                               fill.quantity});
        schedule(fill, now + latency(faults) + 1, faults.duplication, false);
        resting.filled = resting.original;
    }
    return SendOutcome::accepted;
}

std::vector<VenueObservation> SimulatedVenue::poll(const Tick now) {
    std::stable_sort(scheduled_.begin(), scheduled_.end(),
                     [](const ScheduledObservation& left,
                        const ScheduledObservation& right) noexcept {
                         if (left.due != right.due) return left.due < right.due;
                         return left.ordinal < right.ordinal;
                     });
    std::vector<VenueObservation> due;
    for (auto& entry : scheduled_) {
        if (entry.due <= now && !entry.delivered) {
            due.push_back(entry.observation);
            entry.delivered = true;
        }
    }
    return due;
}

void SimulatedVenue::acknowledge_observation(
    const VenueObservation& observation) noexcept {
    const auto acknowledged = observation_fingerprint(observation);
    scheduled_.erase(
        std::remove_if(scheduled_.begin(), scheduled_.end(),
                       [&](const ScheduledObservation& entry) noexcept {
                           return observation_fingerprint(entry.observation) ==
                                  acknowledged;
                       }),
        scheduled_.end());
}

void SimulatedVenue::restart_delivery() noexcept {
    for (auto& entry : scheduled_) entry.delivered = false;
}

QueryResult SimulatedVenue::query(const ClusterOrderId order, const Tick) {
    const auto faults = choose_faults(true);
    fault_log_.push_back(faults);

    if (faults.disconnect) {
        session_ = VenueSessionId{session_.value() + 1};
        return {QueryOutcome::unavailable, {}, {}, {}, false, false};
    }

    auto* resting = find(order);
    if (resting == nullptr) {
        return {QueryOutcome::not_found, {}, {}, {}, false, false};
    }

    QueryResult result;
    result.outcome = QueryOutcome::known;
    result.venue_order_id = resting->venue_order_id;
    result.filled_quantity = resting->filled;
    result.remaining_quantity = QuantityLots::from_atoms(
        resting->original.atoms() - resting->filled.atoms());
    result.cancelled = resting->cancelled;

    if (faults.inconsistent_query) {
        result.filled_quantity = QuantityLots{};
        result.remaining_quantity = resting->original;
        result.inconsistent = true;
    }

    if (faults.partial_window) {
        const auto paged = result.filled_quantity.atoms() / 2;
        result.filled_quantity = QuantityLots::from_atoms(paged);
        result.remaining_quantity =
            QuantityLots::from_atoms(resting->original.atoms() - paged);
        result.window_complete = false;
    }
    return result;
}

}
