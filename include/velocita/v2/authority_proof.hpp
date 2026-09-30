#pragma once

#include "velocita/v2/authority_barrier.hpp"
#include "velocita/v2/kernel.hpp"

namespace velocita::v2 {

class ReplicatedKernel;
class GatewayLink;

// Owned local value, not an authenticated IPC encoding or a persisted grant.
struct AuthorityProofView {
    AuthorityBarrier request{};
    BoundOrderEffect binding{};
    std::int32_t issuer_id{};
    std::int64_t term{};
    std::int64_t barrier_index{};
    std::int64_t applied_index{};
    std::uint64_t state_version{};
    StrategyMode strategy_mode{StrategyMode::halted};
};

class AuthorityProof {
public:
    [[nodiscard]] const AuthorityProofView& view() const noexcept { return view_; }

private:
    friend class ReplicatedKernel;
    // Cross-process issuance: GatewayLink constructs one only from an
    // authority_granted frame that verified under the issuing voter's response
    // key, passed that voter's session fence, and answers the link's pending
    // request (see gateway_link.hpp). Nothing else outside the kernel may.
    friend class GatewayLink;
class GatewayLink;
    explicit AuthorityProof(const AuthorityProofView& view) noexcept : view_(view) {}
    AuthorityProofView view_;
};

static_assert(sizeof(AuthorityProof) <= 512);

}  // namespace velocita::v2
