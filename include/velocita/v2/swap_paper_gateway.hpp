#pragma once
#include "velocita/v2/gateway_link.hpp"
#include "velocita/v2/swap_kernel.hpp"
#include "velocita/v2/swap_reconcile_store.hpp"

namespace velocita::v2 {
struct SwapPaperStep {
    bool valid{};
    std::optional<swap::Record> record;
};
[[nodiscard]] SwapPaperStep plan_swap_paper_step(const TradingState&, swap::PaperQuote,
                                                std::uint64_t event_time) noexcept;
class SwapPaperGateway final {
public:
    SwapPaperGateway(GatewayLink&, CallerId, std::uint64_t first_sequence,
                     std::uint64_t sequence_limit, swap::PaperQuote, Digest256 incarnation,
                     Tick timeout, Tick state_interval, SwapReconcileStore&);
    void step(Tick now, std::uint64_t event_time);
    [[nodiscard]] bool stopped() const noexcept { return stopped_; }
    [[nodiscard]] std::uint64_t next_sequence() const noexcept { return sequence_; }
    void extend_sequence_limit(std::uint64_t limit) noexcept { if (limit > limit_) limit_ = limit; }
private:
    GatewayLink& link_;
    SwapReconcileStore& recovery_;
    Digest256 pending_digest_{};
    CallerId caller_;
    std::uint64_t sequence_, limit_;
    swap::PaperQuote quote_;
    Digest256 incarnation_;
    Tick timeout_, interval_, next_read_{};
    std::int64_t seen_index_{};
    bool stopped_{};
};
}
