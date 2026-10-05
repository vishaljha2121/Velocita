#include "velocita/v2/venue_transport.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace velocita::v2::sim {
namespace {

struct SimulatorSeam {
    SimulatedVenue* venue;
    std::vector<VenueObservation> deferred;
};

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

}

VenueBookSource simulated_venue_book(SimulatedVenue& venue) noexcept {
    return {&simulated_book_read, nullptr, &venue};
}

SendOutcome resolve_two_step_send(const TwoStepSend& steps,
                                  const OrderEffect& effect) noexcept {
    switch (steps.prepare_fn(steps.context, effect)) {
        case StepOutcome::succeeded:
            break;
        case StepOutcome::rate_limited:
            return SendOutcome::rate_limited;
        case StepOutcome::refused:
        case StepOutcome::ambiguous:
            return SendOutcome::pre_send_failure;
    }

    switch (steps.sign_fn(steps.context, effect)) {
        case StepOutcome::succeeded:
            break;
        case StepOutcome::rate_limited:
        case StepOutcome::refused:
        case StepOutcome::ambiguous:
            steps.abandon_fn(steps.context, effect);
            return SendOutcome::pre_send_failure;
    }

    switch (steps.submit_fn(steps.context, effect)) {
        case StepOutcome::succeeded:
            return SendOutcome::accepted;
        case StepOutcome::ambiguous:
            return SendOutcome::ambiguous;
        case StepOutcome::rate_limited:
            steps.abandon_fn(steps.context, effect);
            return SendOutcome::rate_limited;
        case StepOutcome::refused:
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

}
