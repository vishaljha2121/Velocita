#include "velocita/v2/swap_paper_gateway.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace velocita::v2 {
namespace {
Digest256 paper_digest(const swap::Intent& intent, swap::PaperQuote quote, std::byte tag) noexcept {
    std::array<std::byte, swap::max_record_bytes + 13> bytes{};
    const auto encoded = swap::encode({swap::Role::strategy, intent.source_time, intent}, bytes);
    if (encoded.error != swap::CodecError::none) return {};
    auto at = encoded.size;
    bytes[at++] = tag;
    for (unsigned i = 0; i < 8; ++i) bytes[at++] = static_cast<std::byte>((quote.price_per_sol.value() >> (8*i)) & 255);
    for (unsigned i = 0; i < 4; ++i) bytes[at++] = static_cast<std::byte>((quote.fee_ppm >> (8*i)) & 255);
    return sha256_digest(std::span(bytes).first(at));
}
}
SwapPaperStep plan_swap_paper_step(const TradingState& state, swap::PaperQuote quote, std::uint64_t now) noexcept {
    if (!swap_state_bindings_valid(state) || !state.spot_swap.initialized ||
        !quote.price_per_sol.value() || quote.fee_ppm > 1'000'000) return {};
    const auto& s = state.spot_swap;
    const auto& p = s.pending;
    if (s.conflicted || p.phase == swap::Phase::conflict || p.phase == swap::Phase::unknown) return {};
    if (now < s.event_time) return {};
    if (p.phase == swap::Phase::none) return {true, {}};
    const auto payload = paper_digest(p.intent, quote, std::byte{1});
    swap::Command body;
    if (p.phase == swap::Phase::reserved) {
        if (now >= p.intent.sign_by || s.mode == swap::Mode::halted ||
            p.intent.kill_epoch != s.kill_epoch || p.intent.executor_generation != s.executor_generation ||
            (p.intent.side == Side::buy && s.mode != swap::Mode::normal))
            body = swap::ReleaseUnsent{p.intent.effect, paper_digest(p.intent, quote, std::byte{4})};
        else body = swap::Prepare{p.intent.effect, payload, payload, 1};
    } else {
        if (payload != p.payload || payload != p.handle) return {};
        if (p.phase == swap::Phase::prepared) body = swap::Attempt{p.intent.effect, payload};
        else if (p.phase == swap::Phase::attempted) {
            const auto fill = swap::simulate(s, quote, paper_digest(p.intent, quote, std::byte{2}),
                                             paper_digest(p.intent, quote, std::byte{3}));
            if (fill.code != swap::Code::applied) return {};
            body = fill.settlement;
        } else return {};
    }
    swap::Record record{swap::Role::gateway, now, body};
    auto probe = s;
    if (swap::apply(probe, record.command, record.role, now) != swap::Code::applied) return {};
    return {true, record};
}
SwapPaperGateway::SwapPaperGateway(GatewayLink& link, CallerId caller, std::uint64_t first,
    std::uint64_t limit, swap::PaperQuote quote, Digest256 incarnation, Tick timeout, Tick interval, SwapReconcileStore& recovery)
    : link_(link), recovery_(recovery), caller_(caller), sequence_(first), limit_(limit), quote_(quote),
      incarnation_(incarnation), timeout_(timeout), interval_(interval) {
    if (is_operator_caller(caller) || !caller.value() || !first || first >= limit ||
        !quote.price_per_sol.value() || quote.fee_ppm > 1'000'000 || incarnation.is_zero() || !timeout || !interval ||
        recovery_.status() != SwapReconcileStoreCode::none || recovery_.latest().result.index <= 0 ||
        recovery_.latest().result.binding.incarnation != incarnation_)
        throw std::invalid_argument("invalid paper swap gateway configuration");
    seen_index_ = recovery_.latest().result.index;
}
void SwapPaperGateway::step(Tick now, std::uint64_t event_time) {
    if (stopped_) return;
    if (recovery_.status() != SwapReconcileStoreCode::none) { stopped_ = true; return; }
    if (!link_.busy()) {
        if (now < next_read_) return;
        if (!link_.request_state(now, timeout_)) stopped_ = true;
        return;
    }
    const auto outcome = link_.pump(now);
    if (outcome.status == LinkStatus::pending || outcome.status == LinkStatus::idle) return;
    if (outcome.status != LinkStatus::done) { stopped_ = true; return; }
    if (outcome.request == LinkRequest::command) {
        if ((outcome.result.code != ApplyCode::applied && outcome.result.code != ApplyCode::already_applied) ||
            pending_digest_.is_zero() || recovery_.persist({recovery_.latest().result.binding, outcome.index,
                pending_digest_}) != SwapReconcileStoreCode::none) {
            stopped_ = true; return;
        }
        pending_digest_ = {};
        seen_index_ = std::max(seen_index_, outcome.index);
        return;
    }
    if (outcome.request != LinkRequest::state) { stopped_ = true; return; }
    if (outcome.reading.authority_domain != recovery_.latest().result.binding.authority_domain) {
        stopped_ = true; return;
    }
    next_read_ = now > std::numeric_limits<Tick>::max() - interval_ ? std::numeric_limits<Tick>::max() : now + interval_;
    if (outcome.reading.applied_index < seen_index_) return;
    seen_index_ = outcome.reading.applied_index;
    const auto& state = link_.state();
    if (state.spot_swap.opening.binding.incarnation != incarnation_) { stopped_ = true; return; }
    const auto next = plan_swap_paper_step(state, quote_, event_time);
    if (!next.valid) { stopped_ = true; return; }
    if (!next.record) return;
    if (sequence_ >= limit_) { stopped_ = true; return; }
    Command command{{caller_, RequestSequence{sequence_++}, swap_record_digest(*next.record)}, ApplySpotSwap{*next.record}};
    pending_digest_ = swap_record_digest(*next.record);
    if (!link_.submit(command, ProposeLane::normal, now, timeout_)) stopped_ = true;
}
}
