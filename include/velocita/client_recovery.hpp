#pragma once

#include "velocita/client_routing.hpp"

#include <cstddef>
#include <cstdint>

namespace velocita {

struct ClientRecoveryConfig {
    std::uint64_t retry_base_ns{5'000'000};
    std::uint64_t retry_cap_ns{250'000'000};
    std::size_t unconfirmed_admission_limit{1};
    std::uint64_t response_timeout_ns{250'000'000};
};

class ClientRecoveryPolicy {
public:
    explicit ClientRecoveryPolicy(ClientRecoveryConfig config = {});

    [[nodiscard]] std::uint64_t retry_delay_ns(std::uint64_t request_id,
                                                std::uint32_t retry_ordinal) const noexcept;
    [[nodiscard]] std::uint64_t next_retry_ns(std::uint64_t request_id,
                                               std::uint32_t retry_ordinal,
                                               std::uint64_t now_ns,
                                               std::uint64_t deadline_ns) const noexcept;
    [[nodiscard]] std::uint64_t response_deadline_ns(
        std::uint64_t now_ns, std::uint64_t command_deadline_ns) const noexcept;
    [[nodiscard]] bool may_admit(LeaderKnowledgeState state,
                                 std::size_t pending_commands) const noexcept;

    [[nodiscard]] static bool retry_due(std::uint64_t now_ns,
                                        std::uint64_t next_retry_ns,
                                        std::uint64_t deadline_ns) noexcept;
    [[nodiscard]] static bool deadline_reached(std::uint64_t now_ns,
                                               std::uint64_t deadline_ns) noexcept;
    [[nodiscard]] const ClientRecoveryConfig& config() const noexcept { return config_; }

private:
    ClientRecoveryConfig config_;
};

}  // namespace velocita
