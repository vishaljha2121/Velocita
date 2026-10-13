#pragma once

#include "velocita/v2/authority_barrier.hpp"
#include "velocita/v2/swap.hpp"

namespace velocita::v2 {
struct TradingState;
class ReplicatedKernel;
class GatewayLink;
struct SwapAuthorityProofView {
    AuthorityBarrier request{};
    std::int32_t issuer_id{};
    std::int64_t term{},barrier_index{},applied_index{};
};
class SwapAuthorityProof final {
public:
    [[nodiscard]] const SwapAuthorityProofView& view()const noexcept{return view_;}
private:
    friend class ReplicatedKernel;
    friend class GatewayLink;
    explicit SwapAuthorityProof(SwapAuthorityProofView view)noexcept:view_(view){}
    SwapAuthorityProofView view_{};
};
[[nodiscard]] Digest256 swap_signing_authority_digest(const swap::Pending&)noexcept;
[[nodiscard]] bool swap_authority_matches(const TradingState&,const AuthorityBarrier&,const Digest256& authority_domain)noexcept;
static_assert(sizeof(SwapAuthorityProof)<=224);
}
