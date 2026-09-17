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

}  // namespace

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
