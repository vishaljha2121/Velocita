// The result of a command that committed and was applied.
#pragma once

#include "velocita/v2/kernel.hpp"

#include <cstdint>

namespace velocita::v2 {

struct CommittedResult {
    CommandIdentity identity{};
    ApplyResult result{};
    std::int64_t term{};
    std::int64_t index{};
};

}
