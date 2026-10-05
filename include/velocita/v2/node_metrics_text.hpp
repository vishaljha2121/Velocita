// Renders a voter's health page as Prometheus text.
#pragma once

#include "velocita/v2/node_health.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace velocita::v2 {

[[nodiscard]] std::string render_node_metrics(const std::optional<NodeHealth>& health,
                                              std::string_view host,
                                              std::uint64_t now_monotonic_ns);

}
