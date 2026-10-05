// V1: the counter state machine with request deduplication.
#pragma once

#include "velocita/wire.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace velocita {

class CounterFsm {
public:
    std::int64_t apply(const Command& command);
    [[nodiscard]] std::int64_t value() const noexcept { return value_; }
    [[nodiscard]] std::optional<std::int64_t> result_for(std::uint64_t request_id) const;

private:
    std::int64_t value_{};
    std::unordered_map<std::uint64_t, std::int64_t> completed_;
};

}
