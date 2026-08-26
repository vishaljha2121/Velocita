#include "velocita/counter_fsm.hpp"

#include <limits>
#include <stdexcept>

namespace velocita {

std::int64_t CounterFsm::apply(const Command& command) {
    if (const auto prior = result_for(command.request_id)) {
        return *prior;
    }

    switch (command.type) {
        case CommandType::increment:
            if ((command.amount > 0 && value_ > std::numeric_limits<std::int64_t>::max() - command.amount) ||
                (command.amount < 0 && value_ < std::numeric_limits<std::int64_t>::min() - command.amount)) {
                throw std::overflow_error("counter overflow");
            }
            value_ += command.amount;
            break;
        case CommandType::get:
            break;
        default:
            throw std::runtime_error("unsupported state-machine command");
    }

    completed_.emplace(command.request_id, value_);
    return value_;
}

std::optional<std::int64_t> CounterFsm::result_for(std::uint64_t request_id) const {
    const auto it = completed_.find(request_id);
    if (it == completed_.end()) {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace velocita

