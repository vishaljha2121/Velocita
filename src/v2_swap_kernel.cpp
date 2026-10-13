#include "velocita/v2/swap_kernel.hpp"
#include "velocita/v2/digest.hpp"
#include <string_view>

namespace velocita::v2 {
namespace {
swap::Mode swap_mode(StrategyMode mode) noexcept {
    if (mode == StrategyMode::normal) return swap::Mode::normal;
    if (mode == StrategyMode::reduce_only ||
        mode == StrategyMode::flatten) return swap::Mode::reduce_only;
    return swap::Mode::halted;
}
bool exclusive(const TradingState& s) noexcept {
    return s.risk_account_count == 0 && s.reservation_count == 0 && s.order_count == 0 &&
        s.outbox_effect_count == 0 && s.policy_count == 0 && s.decision_count == 0 &&
        !s.active_executable_policy.present && !s.staged_executable_policy.present;
}
bool text_is(const CatalogText& text, std::string_view value) noexcept {
    return text.length == value.size() && std::string_view(text.bytes.data(), text.length) == value;
}
ApplyCode translate(swap::Code c) noexcept {
    switch (c) {
        case swap::Code::applied: return ApplyCode::applied;
        case swap::Code::duplicate: return ApplyCode::already_applied;
        case swap::Code::unauthorized: return ApplyCode::unauthorized_command;
        case swap::Code::already_initialized: return ApplyCode::risk_reconfiguration_blocked;
        case swap::Code::fenced: return ApplyCode::stale_control_generation;
        case swap::Code::stale: case swap::Code::expired: return ApplyCode::request_expired;
        case swap::Code::busy: return ApplyCode::order_slot_occupied;
        case swap::Code::stopped: return ApplyCode::decision_not_authorizable;
        case swap::Code::insufficient_funds: case swap::Code::purchase_cap: case swap::Code::unsafe_bound:
            return ApplyCode::insufficient_risk_capacity;
        case swap::Code::overflow: return ApplyCode::risk_arithmetic_overflow;
        case swap::Code::conflict: return ApplyCode::reconciliation_required;
        case swap::Code::capacity: return ApplyCode::fill_capacity_exceeded;
        default: return ApplyCode::invalid_command;
    }
}
}
Digest256 swap_record_digest(const swap::Record& record) noexcept {
    std::array<std::byte, swap::max_record_bytes> bytes{};
    const auto encoded = swap::encode(record, bytes);
    return encoded.error == swap::CodecError::none ? sha256_digest(std::span(bytes).first(encoded.size)) : Digest256{};
}
bool swap_catalog_matches(const swap::Binding& binding, const VerifiedExecutionCatalog& catalog) noexcept {
    if (binding.catalog != catalog.root() || binding.account != AccountId{2} || binding.instrument != InstrumentId{2}) return false;
    const auto* row = catalog.lookup(binding.account, VenueId{20}, binding.instrument,
                                     InstrumentSpecVersion{1}, VenueCapabilitiesVersion{1});
    return row && row->base_decimals == 9 && text_is(row->network_identity, "solana") &&
        text_is(row->base_asset_identity, "So11111111111111111111111111111111111111112") &&
        text_is(row->quote_asset_identity, "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v") &&
        text_is(row->routing_key, "truemarkets.shadow.solana.sol-usdc");
}
bool swap_state_bindings_valid(const TradingState& s) noexcept {
    if (!swap::valid_state(s.spot_swap)) return false;
    if (!s.spot_swap.initialized) return true;
    return exclusive(s) && s.spot_swap.opening.binding.catalog == s.execution_catalog_root &&
        !s.execution_catalog_root.is_zero() && s.spot_swap.kill_epoch == s.kill_epoch.value() &&
        s.spot_swap.executor_generation == s.executor_generation.value() &&
        s.spot_swap.mode == swap_mode(s.strategy_mode);
}
void synchronize_swap_controls(TradingState& s) noexcept {
    if (!s.spot_swap.initialized) return;
    s.spot_swap.mode = swap_mode(s.strategy_mode);
    s.spot_swap.kill_epoch = s.kill_epoch.value();
    s.spot_swap.executor_generation = s.executor_generation.value();
}
ApplyCode apply_spot_swap(TradingState& s, const swap::Record& r, const VerifiedExecutionCatalog* catalog) noexcept {
    if (!catalog || s.execution_catalog_root.is_zero()) return ApplyCode::catalog_not_installed;
    if (r.role != swap::required_role(r.command) || std::holds_alternative<swap::Control>(r.command))
        return ApplyCode::invalid_command;
    if (const auto* opening = std::get_if<swap::Initialize>(&r.command)) {
        if (!exclusive(s) || s.strategy_mode != StrategyMode::halted) return ApplyCode::risk_reconfiguration_blocked;
        if (!swap_catalog_matches(opening->binding, *catalog)) return ApplyCode::catalog_binding_mismatch;
        if (opening->kill_epoch != s.kill_epoch.value() || opening->executor_generation != s.executor_generation.value())
            return ApplyCode::stale_control_generation;
    }
    return translate(swap::apply(s.spot_swap, r.command, r.role, r.event_time));
}
}
