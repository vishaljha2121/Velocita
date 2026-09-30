#pragma once

#include "velocita/v2/node_health.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace velocita::v2 {

// Prometheus text exposition for one voter's health page (M3a). Read-only and
// disposable: nothing here grants authority.
//
// HEALTH nullopt (missing, foreign or never-published page) renders ONLY
// `velocita_v2_up{...} 0`: an unknown voter must never look like healthy zeros.
// NOW_MONOTONIC_NS is this host's CLOCK_MONOTONIC; the page's last-turn stamp is
// from the same host clock, so their difference is the telemetry age.
[[nodiscard]] std::string render_node_metrics(const std::optional<NodeHealth>& health,
                                              std::string_view host,
                                              std::uint64_t now_monotonic_ns);

}  // namespace velocita::v2
