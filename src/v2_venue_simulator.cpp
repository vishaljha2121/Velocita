#include "velocita/v2/venue_simulator.hpp"

#include <algorithm>

namespace velocita::v2::sim {
namespace {

// Digest a scheduled observation so the evidence field is non-zero and varies.
// The simulator is not a venue's cryptography; this only has to be deterministic
// and distinct, since the kernel checks only that it is non-zero.
[[nodiscard]] Digest256 evidence_of(const std::uint64_t marker) noexcept {
    Digest256 value;
    for (std::size_t index = 0; index < 8; ++index) {
        value.bytes[index] = static_cast<std::byte>((marker >> (index * 8)) & 0xFF);
    }
    // Guarantee non-zero even for marker 0, which would otherwise fail
    // `validate()` for a reason that has nothing to do with the test.
    value.bytes[8] = std::byte{0x01};
    return value;
}

}  // namespace

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
    // Always consume a draw, even at zero probability. Otherwise enabling one
    // fault would shift every later draw and a "same seed, one extra fault"
    // comparison would diverge for reasons unrelated to the fault.
    const auto draw = next_random() % 1000;
    return permille != 0 && draw < permille;
}

Fault SimulatedVenue::choose_fault(const bool for_query) noexcept {
    // Fixed evaluation order so a policy enabling several faults always resolves
    // the same way. First match wins, mirroring the ordered first-match rule the
    // policy evaluator uses, so there is one precedence convention in the system
    // rather than two.
    if (for_query) {
        // A query runs over the same session a send does, so it is subject to the
        // same disconnect. Without this, `QueryOutcome::unavailable` would be
        // unreachable and the most safety-critical branch of the gateway's
        // classifier - the one that refuses to read silence as absence - would
        // never be exercised by the matrix.
        if (roll(policy_.disconnect_permille)) return Fault::disconnect;
        return roll(policy_.inconsistent_query_permille) ? Fault::inconsistent_query
                                                         : Fault::none;
    }
    if (roll(policy_.rate_limit_permille)) return Fault::rate_limit;
    if (roll(policy_.disconnect_permille)) return Fault::disconnect;
    if (roll(policy_.loss_permille)) return Fault::loss;
    if (roll(policy_.duplication_permille)) return Fault::duplication;
    if (roll(policy_.reorder_permille)) return Fault::reorder;
    if (roll(policy_.fill_after_cancel_permille)) return Fault::fill_after_cancel;
    if (roll(policy_.partial_fill_permille)) return Fault::partial_fill;
    if (roll(policy_.delay_permille)) return Fault::delay;
    return Fault::none;
}

Tick SimulatedVenue::latency(const Fault fault) noexcept {
    const auto span = policy_.maximum_latency >= policy_.minimum_latency
                          ? policy_.maximum_latency - policy_.minimum_latency + 1
                          : 1;
    auto ticks = policy_.minimum_latency + (next_random() % span);
    if (fault == Fault::delay) ticks += policy_.delay_penalty;
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
    // Reorder pulls a response earlier than its natural position by giving it a
    // lower ordinal than everything already queued. That models a venue whose
    // transport does not preserve order, without needing a second queue.
    const auto ordinal = reorder ? 0 : ++ordinal_;
    scheduled_.push_back({due, ordinal, observation});
    if (duplicate) {
        // A duplicate is byte-identical, including its sequence and evidence.
        // That is the case that matters: a duplicate carrying fresh identity
        // would be caught trivially by identity checks.
        scheduled_.push_back({due, ordinal, observation});
    }
}

SendOutcome SimulatedVenue::send(const OrderEffect& effect, const Tick now) {
    const auto fault = choose_fault(false);
    fault_log_.push_back(fault);

    if (fault == Fault::rate_limit) return SendOutcome::rate_limited;

    if (fault == Fault::disconnect) {
        // The session ends before the venue can answer. Anything in flight for
        // the old session is abandoned, and the caller cannot tell whether this
        // effect arrived: that is precisely an ambiguous outcome, never a
        // rejection.
        session_ = VenueSessionId{session_.value() + 1};
        scheduled_.clear();
        return SendOutcome::ambiguous;
    }

    if (effect.operation == EffectOperation::cancel) {
        auto* order = find(effect.cluster_order_id);
        if (order == nullptr) return SendOutcome::business_rejected;
        order->cancelled = true;
        auto cancelled = base_observation(VenueObservationKind::cancelled, *order);
        schedule(cancelled, now + latency(fault), fault == Fault::duplication,
                 fault == Fault::reorder);

        if (fault == Fault::fill_after_cancel &&
            order->filled < order->original) {
            // A fill that was already in flight lands after the cancellation is
            // confirmed. The order really did trade; the cluster must account
            // for it rather than treat the cancel as final.
            auto fill = base_observation(VenueObservationKind::fill, *order);
            fill.execution_id = ExecutionId{++execution_counter_};
            fill.side = order->side;
            fill.price = order->price;
            fill.quantity = QuantityLots::from_atoms(1);
            order->filled = QuantityLots::from_atoms(order->filled.atoms() + 1);
            schedule(fill, now + latency(fault) + 1, false, false);
        }
        return SendOutcome::accepted;
    }

    // A submit. Duplicate client identity is the venue's own idempotency, and it
    // is what makes at-least-once delivery safe under G-025.
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

    if (fault == Fault::loss) {
        // Accepted at the venue, but no response will ever arrive. The gateway
        // must eventually treat this as unknown and reconcile - never as a
        // rejection, and never as a licence to resend blindly.
        return SendOutcome::accepted;
    }

    auto& resting = resting_.back();
    auto ack = base_observation(VenueObservationKind::order_acknowledged, resting);
    schedule(ack, now + latency(fault), fault == Fault::duplication,
             fault == Fault::reorder);

    const auto total = resting.original.atoms();
    if (fault == Fault::partial_fill && total > 1) {
        // One atom at a time. Partial fills are where duplicate-fill accounting
        // actually bites: reservation arithmetic alone cannot catch a repeated
        // partial, because filled plus remaining stays within the original.
        for (std::int64_t index = 0; index < total; ++index) {
            auto fill = base_observation(VenueObservationKind::fill, resting);
            fill.execution_id = ExecutionId{++execution_counter_};
            fill.side = resting.side;
            fill.price = resting.price;
            fill.quantity = QuantityLots::from_atoms(1);
            schedule(fill, now + latency(fault) + static_cast<Tick>(index) + 1,
                     false, false);
        }
        resting.filled = resting.original;
    } else {
        auto fill = base_observation(VenueObservationKind::fill, resting);
        fill.execution_id = ExecutionId{++execution_counter_};
        fill.side = resting.side;
        fill.price = resting.price;
        fill.quantity = resting.original;
        // A duplicated FILL is the economically dangerous duplicate: booking it
        // twice doubles the position, which no reservation arithmetic downstream
        // can undo. Duplicating only the acknowledgement would have exercised the
        // identity check on the harmless case and left this one untested.
        schedule(fill, now + latency(fault) + 1, fault == Fault::duplication, false);
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
    std::size_t kept = 0;
    for (auto& entry : scheduled_) {
        if (entry.due <= now) {
            due.push_back(entry.observation);
        } else {
            scheduled_[kept++] = entry;
        }
    }
    scheduled_.resize(kept);
    return due;
}

QueryResult SimulatedVenue::query(const ClusterOrderId order, const Tick) {
    const auto fault = choose_fault(true);
    fault_log_.push_back(fault);

    if (fault == Fault::disconnect) {
        // The session ended mid-question. The venue said nothing, and nothing may
        // be concluded from that - in particular NOT that the order is absent.
        session_ = VenueSessionId{session_.value() + 1};
        scheduled_.clear();
        return {QueryOutcome::unavailable, {}, {}, {}, false, false};
    }

    auto* resting = find(order);
    if (resting == nullptr) {
        // Nothing here. Distinct from `unavailable`: this is an answer, and a
        // gateway may act on it.
        return {QueryOutcome::not_found, {}, {}, {}, false, false};
    }

    QueryResult result;
    result.outcome = QueryOutcome::known;
    result.venue_order_id = resting->venue_order_id;
    result.filled_quantity = resting->filled;
    result.remaining_quantity = QuantityLots::from_atoms(
        resting->original.atoms() - resting->filled.atoms());
    result.cancelled = resting->cancelled;

    if (fault == Fault::inconsistent_query) {
        // The venue contradicts what it already reported, in the direction that
        // actually matters: DOWNWARD. It denies fills it has already sent.
        //
        // Corrupting upward would be the benign direction - the cluster reads a
        // total ahead of its own evidence as fills still in flight, which is
        // usually true and is recoverable. Denying a fill is unclosable: no
        // future evidence reduces what the cluster has already booked. ADR 0001
        // is explicit that reconciliation "never overwrites conflicting evidence
        // to make a dashboard green", and this is the case that tests it.
        result.filled_quantity = QuantityLots{};
        result.remaining_quantity = resting->original;
        result.inconsistent = true;
    }
    return result;
}

}  // namespace velocita::v2::sim
