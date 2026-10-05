#include "velocita/client_recovery.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace velocita {
namespace {

std::uint64_t mix64(std::uint64_t value) noexcept {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

std::uint64_t exponential_upper_bound(const ClientRecoveryConfig& config,
                                      const std::uint32_t retry_ordinal) noexcept {
    if (retry_ordinal >= 63U ||
        config.retry_base_ns > (config.retry_cap_ns >> retry_ordinal)) {
        return config.retry_cap_ns;
    }
    return std::min(config.retry_cap_ns, config.retry_base_ns << retry_ordinal);
}

}

ClientRecoveryPolicy::ClientRecoveryPolicy(ClientRecoveryConfig config)
    : config_(config) {
    if (config_.retry_base_ns == 0) {
        throw std::invalid_argument("retry base delay must be positive");
    }
    if (config_.retry_cap_ns < config_.retry_base_ns) {
        throw std::invalid_argument("retry cap must be greater than or equal to retry base");
    }
    if (config_.unconfirmed_admission_limit == 0) {
        throw std::invalid_argument("unconfirmed admission limit must be positive");
    }
    if (config_.response_timeout_ns == 0) {
        throw std::invalid_argument("response timeout must be positive");
    }
}

std::uint64_t ClientRecoveryPolicy::response_deadline_ns(
    const std::uint64_t now_ns, const std::uint64_t command_deadline_ns) const noexcept {
    if (now_ns >= command_deadline_ns) return command_deadline_ns;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto candidate = config_.response_timeout_ns > maximum - now_ns
        ? maximum : now_ns + config_.response_timeout_ns;
    return std::min(candidate, command_deadline_ns);
}

std::uint64_t ClientRecoveryPolicy::retry_delay_ns(
    const std::uint64_t request_id, const std::uint32_t retry_ordinal) const noexcept {
    const auto upper = exponential_upper_bound(config_, retry_ordinal);
    const auto range = upper - config_.retry_base_ns;
    if (range == 0) return config_.retry_base_ns;
    const auto entropy = mix64(request_id ^
        (static_cast<std::uint64_t>(retry_ordinal) * 0xD1B54A32D192ED03ULL));
    return config_.retry_base_ns + entropy % (range + 1U);
}

std::uint64_t ClientRecoveryPolicy::next_retry_ns(
    const std::uint64_t request_id, const std::uint32_t retry_ordinal,
    const std::uint64_t now_ns, const std::uint64_t deadline_ns) const noexcept {
    if (now_ns >= deadline_ns) return deadline_ns;
    const auto delay = retry_delay_ns(request_id, retry_ordinal);
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto candidate = delay > maximum - now_ns ? maximum : now_ns + delay;
    return std::min(candidate, deadline_ns);
}

bool ClientRecoveryPolicy::may_admit(const LeaderKnowledgeState state,
                                     const std::size_t pending_commands) const noexcept {
    return state == LeaderKnowledgeState::known ||
           pending_commands < config_.unconfirmed_admission_limit;
}

bool ClientRecoveryPolicy::retry_due(const std::uint64_t now_ns,
                                     const std::uint64_t next_retry_ns,
                                     const std::uint64_t deadline_ns) noexcept {
    return now_ns < deadline_ns && now_ns >= next_retry_ns;
}

bool ClientRecoveryPolicy::deadline_reached(const std::uint64_t now_ns,
                                            const std::uint64_t deadline_ns) noexcept {
    return now_ns >= deadline_ns;
}

}
