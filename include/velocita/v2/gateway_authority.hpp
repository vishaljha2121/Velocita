// The gateway's one-shot, request-correlated permission to send an order.
#pragma once

#include "velocita/v2/authority_proof.hpp"
#include "velocita/v2/gateway.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace velocita::v2 {

namespace detail {
[[nodiscard]] constexpr std::optional<std::uint64_t> next_authority_sequence(
    const std::uint64_t current) noexcept {
    if (current == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    return current + 1;
}
}

class GatewayAuthority final {
public:
    GatewayAuthority(std::array<std::byte, 16> incarnation,
                     Digest256 authority_domain,
                     Tick freshness_bound = 0) noexcept;

    GatewayAuthority(const GatewayAuthority&) = delete;
    GatewayAuthority& operator=(const GatewayAuthority&) = delete;
    GatewayAuthority(GatewayAuthority&&) = delete;
    GatewayAuthority& operator=(GatewayAuthority&&) = delete;

    [[nodiscard]] std::optional<AuthorityBarrier> begin_request(
        const BoundOrderEffect& binding, const GatewayControlView& control,
        Tick now) noexcept;
    [[nodiscard]] bool accept(const AuthorityProof& proof, Tick now) noexcept;

    [[nodiscard]] bool consume(const BoundOrderEffect& binding,
                               const GatewayControlView& current,
                               Tick now) noexcept;

    [[nodiscard]] bool permits_committed_cancel(
        const BoundOrderEffect& binding,
        const GatewayControlView& current) const noexcept;

    void observe_fence(const GatewayControlView& control,
                       std::int64_t term) noexcept;
    void abandon_request() noexcept;

    void stop_new_risk() noexcept;
    void stop_all_transport() noexcept;
    void activate_after_reconciliation() noexcept;

    [[nodiscard]] bool new_risk_stopped() const noexcept {
        return stop_new_risk_;
    }
    [[nodiscard]] bool all_transport_stopped() const noexcept {
        return stop_all_transport_;
    }

private:
    struct Pending {
        AuthorityBarrier request{};
        BoundOrderEffect binding{};
        GatewayControlView control{};
        Tick started_at{};
    };

    [[nodiscard]] bool observe_time(Tick now) noexcept;
    void drop_permission() noexcept;

    std::array<std::byte, 16> incarnation_{};
    Digest256 authority_domain_{};
    Tick freshness_bound_{};
    Tick last_now_{};
    std::uint64_t next_sequence_{};
    std::optional<Pending> pending_{};
    std::optional<AuthorityProofView> accepted_{};
    Tick accepted_started_at_{};
    ExecutorGeneration generation_highwater_{};
    KillEpoch kill_highwater_{};
    StrategyMode observed_mode_{StrategyMode::halted};
    std::int64_t term_highwater_{};
    std::int64_t index_highwater_{};
    std::int64_t applied_highwater_{};
    bool have_time_{};
    bool have_control_{};
    bool stop_new_risk_{true};
    bool stop_all_transport_{true};
};

static_assert(sizeof(GatewayAuthority) <= 1536,
              "gateway authority exceeded its bounded two-slot state");

}
