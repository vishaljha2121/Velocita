#include "velocita/v2/venue_transport.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace velocita::v2::sim {
namespace {

// THE DEFERRAL BUFFER IS THE WHOLE SUBTLETY HERE.
//
// `SimulatedVenue::poll` hands back everything due at once and considers it
// delivered; the seam promises to write no more than the caller asked for. Those
// two are only compatible if the overflow is HELD rather than dropped, because
// an observation is economic evidence and losing one loses a fill.
//
// So a poll takes whatever the venue offers, gives the caller as much as fits,
// and keeps the rest to hand over first on the next call. Acknowledgement still
// goes straight through to the venue, which is what keeps redelivery-on-crash
// working: an observation sitting in this buffer is unacknowledged, so a restart
// sees it again from the venue itself rather than from here.
struct SimulatorSeam {
    SimulatedVenue* venue;
    std::vector<VenueObservation> deferred;
};

// One seam per venue instance, kept alive independently of the returned struct
// because `VenueTransport` is a trivially copyable handle and owns nothing.
std::vector<std::unique_ptr<SimulatorSeam>>& seams() {
    static std::vector<std::unique_ptr<SimulatorSeam>> instances;
    return instances;
}

SendOutcome seam_send(void* context, const OrderEffect& effect, const Tick now) {
    return static_cast<SimulatorSeam*>(context)->venue->send(effect, now);
}

std::size_t seam_poll(void* context, const std::span<VenueObservation> out,
                      const Tick now) {
    auto& seam = *static_cast<SimulatorSeam*>(context);
    auto fresh = seam.venue->poll(now);
    seam.deferred.insert(seam.deferred.end(), fresh.begin(), fresh.end());

    const auto taken = std::min(out.size(), seam.deferred.size());
    std::copy_n(seam.deferred.begin(), taken, out.begin());
    seam.deferred.erase(seam.deferred.begin(),
                        seam.deferred.begin() + static_cast<std::ptrdiff_t>(taken));
    return taken;
}

void seam_acknowledge(void* context, const VenueObservation& observation) {
    static_cast<SimulatorSeam*>(context)->venue->acknowledge_observation(observation);
}

QueryResult seam_query(void* context, const ClusterOrderId order, const Tick now) {
    return static_cast<SimulatorSeam*>(context)->venue->query(order, now);
}

VenueSessionId seam_session(void* context) {
    return static_cast<SimulatorSeam*>(context)->venue->session();
}

void simulated_book_read(void* context, VenueBook& out) {
    const auto& venue = *static_cast<const SimulatedVenue*>(context);
    out = {};
    out.available = true;
    std::int64_t base = 0;
    for (const auto& execution : venue.executions())
        base += execution.side == Side::buy ? execution.quantity.atoms() : -execution.quantity.atoms();
    out.base = QuantityLots::from_atoms(base);
    const auto& owned = venue.owned_observations();
    out.unacknowledged_reports = owned.size();
    for (const auto& entry : owned) {
        const auto order = entry.observation.cluster_order_id;
        const auto listed = std::span(out.pending_orders).first(out.pending_order_count);
        if (std::find(listed.begin(), listed.end(), order) != listed.end()) continue;
        if (out.pending_order_count == out.pending_orders.size()) {
            out.pending_orders_truncated = true;
            break;
        }
        out.pending_orders[out.pending_order_count++] = order;
    }
}

}  // namespace

VenueBookSource simulated_venue_book(SimulatedVenue& venue) noexcept {
    return {&simulated_book_read, nullptr, &venue};
}

SendOutcome resolve_two_step_send(const TwoStepSend& steps,
                                  const OrderEffect& effect) noexcept {
    // STEP ONE. Nothing exists at the venue until this succeeds.
    switch (steps.prepare_fn(steps.context, effect)) {
        case StepOutcome::succeeded:
            break;
        case StepOutcome::rate_limited:
            return SendOutcome::rate_limited;
        case StepOutcome::refused:
        case StepOutcome::ambiguous:
            // Ambiguity here is reported as "nothing was sent" because an order
            // left `initialized` cannot fill. See ADR 0009 for the documented
            // fact this rests on, and for why it is recorded as a dependency
            // rather than assumed.
            return SendOutcome::pre_send_failure;
    }

    // From here an `initialized` order may exist, so every failure path below
    // attempts to abandon it.
    switch (steps.sign_fn(steps.context, effect)) {
        case StepOutcome::succeeded:
            break;
        case StepOutcome::rate_limited:
        case StepOutcome::refused:
        case StepOutcome::ambiguous:
            // Signing is local. It cannot be ambiguous about the VENUE, so every
            // non-success means nothing was submitted.
            steps.abandon_fn(steps.context, effect);
            return SendOutcome::pre_send_failure;
    }

    switch (steps.submit_fn(steps.context, effect)) {
        case StepOutcome::succeeded:
            return SendOutcome::accepted;
        case StepOutcome::ambiguous:
            // THE ONE THAT MATTERS. The order may be resting. Do NOT abandon -
            // abandoning what may be a live order is worse than leaving it, and
            // only the gateway's reconciliation can establish which it is.
            return SendOutcome::ambiguous;
        case StepOutcome::rate_limited:
            steps.abandon_fn(steps.context, effect);
            return SendOutcome::rate_limited;
        case StepOutcome::refused:
            // A clean refusal at submission is the venue declining on its own
            // terms, which is economic information and not a transport failure.
            steps.abandon_fn(steps.context, effect);
            return SendOutcome::business_rejected;
    }
    return SendOutcome::pre_send_failure;
}

VenueTransport simulated_venue_transport(SimulatedVenue& venue) noexcept {
    seams().push_back(std::make_unique<SimulatorSeam>(SimulatorSeam{&venue, {}}));
    VenueTransport transport;
    transport.send_fn = &seam_send;
    transport.poll_fn = &seam_poll;
    transport.acknowledge_fn = &seam_acknowledge;
    transport.query_fn = &seam_query;
    transport.session_fn = &seam_session;
    transport.context = seams().back().get();
    return transport;
}

}  // namespace velocita::v2::sim
