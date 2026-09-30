#pragma once

#include "velocita/v2/kernel.hpp"

#include <cstdint>

namespace velocita::v2 {

// A command that COMMITTED and was applied. `result` is the reducer's own
// answer, byte-identical on every voter, because every voter applied the same
// bytes to the same prior state.
struct CommittedResult {
    CommandIdentity identity{};
    ApplyResult result{};
    std::int64_t term{};
    std::int64_t index{};
};

} // namespace velocita::v2
