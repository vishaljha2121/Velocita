#include "velocita/v2/swap.hpp"

#include <limits>
#include <algorithm>

namespace velocita::v2::swap {
namespace {
__extension__ using Wide = unsigned __int128;
__extension__ using SignedWide = __int128;
constexpr std::uint64_t atom_scale = 1'000'000'000;
constexpr auto max_money = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
constexpr auto max_atoms = std::numeric_limits<std::uint64_t>::max();

bool binding_valid(const Binding& b) noexcept {
    return !b.incarnation.is_zero() && !b.catalog.is_zero() && !b.custody.is_zero() &&
           b.account.value() && b.instrument.value() && b.policy.value() &&
           b.policy_version.value() && b.risk_policy_version.value();
}
bool mode_valid(Mode m) noexcept {
    return m == Mode::normal || m == Mode::reduce_only || m == Mode::halted;
}
Wide ceil_ratio(Wide numerator, std::uint64_t denominator) noexcept {
    return numerator / denominator + (numerator % denominator != 0);
}
bool lost(const State& s) noexcept {
    return s.realized_pnl <= -static_cast<std::int64_t>(s.opening.limits.max_realized_loss.value());
}
bool fenced(const State& s) noexcept {
    return s.pending.intent.kill_epoch != s.kill_epoch ||
           s.pending.intent.executor_generation != s.executor_generation;
}
void release(State& s) noexcept {
    s.pending = {}; s.reserved_base = {}; s.reserved_quote = {};
}
constexpr std::uint32_t max_settled_total = 1'000'000;
void record(State& s, const Settlement& x) noexcept {
    if (s.settlement_count == s.settlements.size()) {
        std::move(s.settlements.begin() + 1, s.settlements.end(), s.settlements.begin());
        s.settlements.back() = x;
    } else s.settlements[s.settlement_count++] = x;
    ++s.settled_total;
}
Code conflict(State& s, const Settlement& x) noexcept {
    s.conflicted = true; s.conflict_evidence = x;
    if (s.pending.phase != Phase::none) s.pending.phase = Phase::conflict;
    return Code::conflict;
}

Code reduce(State& s, const Initialize& c, std::uint64_t) noexcept {
    if (s.initialized) return Code::already_initialized;
    const auto& l = c.limits;
    if (!binding_valid(c.binding) || c.funding_evidence.is_zero() || !c.kill_epoch || !c.executor_generation ||
        !l.max_purchase_total.value() || !l.max_buy_debit.value() || !l.max_sell_input.value() ||
        !l.max_realized_loss.value() || !l.max_buy_price_per_sol.value() || !l.min_sell_price_per_sol.value() ||
        !l.max_source_age || !l.max_sign_lifetime || l.max_fee.value() > l.max_buy_debit.value() ||
        l.max_buy_debit.value() > l.max_purchase_total.value() ||
        l.max_purchase_total.value() > max_money || l.max_realized_loss.value() > max_money ||
        l.max_buy_price_per_sol.value() > max_money || l.min_sell_price_per_sol.value() > max_money ||
        c.quote.value() > max_money || c.base_cost.value() > max_money ||
        (!c.base.value() && c.base_cost.value())) return Code::invalid;
    s.initialized = true; s.opening = c;
    s.base = c.base; s.quote = c.quote; s.cost = c.base_cost;
    s.mode = Mode::normal; s.kill_epoch = c.kill_epoch; s.executor_generation = c.executor_generation;
    return Code::applied;
}

Code reduce(State& s, const Intent& i, std::uint64_t now) noexcept {
    if (s.pending.phase != Phase::none) return Code::busy;
    if (s.settled_total >= max_settled_total) return Code::capacity;
    if (!i.effect.value() || i.effect.value() <= s.last_effect) return Code::stale;
    if (i.binding != s.opening.binding) return Code::invalid;
    if (i.kill_epoch != s.kill_epoch || i.executor_generation != s.executor_generation) return Code::fenced;
    const auto& l = s.opening.limits;
    if (i.source_time > now || now - i.source_time > l.max_source_age) return Code::stale;
    if (i.sign_by <= now || i.sign_by - now > l.max_sign_lifetime) return Code::expired;
    if (i.side != Side::buy && i.side != Side::sell) return Code::invalid;
    if (s.mode == Mode::halted || (i.side == Side::buy && (s.mode != Mode::normal || lost(s)))) return Code::stopped;
    if (i.fee_cap.value() > l.max_fee.value()) return Code::unsafe_bound;
    if (i.side == Side::buy) {
        if (!i.quote_input.value() || !i.min_base_receive.value() || i.base_input.value() || i.min_quote_receive.value()) return Code::invalid;
        const Wide debit = Wide{i.quote_input.value()} + i.fee_cap.value();
        if (debit > max_money) return Code::overflow;
        if (debit != i.max_quote_debit.value()) return Code::invalid;
        if (debit > l.max_buy_debit.value() || i.transient_base_hold.value() > l.max_transient_base.value() ||
            i.min_base_receive.value() < ceil_ratio(debit * atom_scale, effective_buy_price(s).value())) return Code::unsafe_bound;
        if (Wide{s.purchases.value()} + debit > l.max_purchase_total.value()) return Code::purchase_cap;
        if (debit > s.quote.value() || i.transient_base_hold.value() > s.base.value()) return Code::insufficient_funds;
        s.reserved_quote = i.max_quote_debit; s.reserved_base = i.transient_base_hold;
    } else {
        if (!i.base_input.value() || !i.min_quote_receive.value() || i.quote_input.value() ||
            i.max_quote_debit.value() || i.min_base_receive.value() || i.transient_base_hold.value()) return Code::invalid;
        if (i.min_quote_receive.value() > max_money) return Code::overflow;
        if (i.base_input.value() > l.max_sell_input.value() ||
            i.min_quote_receive.value() < ceil_ratio(Wide{i.base_input.value()} * l.min_sell_price_per_sol.value(), atom_scale)) return Code::unsafe_bound;
        if (i.base_input.value() > s.base.value()) return Code::insufficient_funds;
        s.reserved_base = i.base_input;
    }
    s.pending.phase = Phase::reserved; s.pending.intent = i; s.last_effect = i.effect.value();
    return Code::applied;
}

Code gate_attempt(const State& s, EffectId effect, std::uint64_t now) noexcept {
    if (s.pending.phase == Phase::none || s.pending.intent.effect != effect) return Code::wrong_effect;
    if (fenced(s)) return Code::fenced;
    if (now >= s.pending.intent.sign_by) return Code::expired;
    if (s.mode == Mode::halted || (s.pending.intent.side == Side::buy && s.mode != Mode::normal)) return Code::stopped;
    return Code::applied;
}
Code reduce(State& s, const Prepare& c, std::uint64_t now) noexcept {
    const auto code = gate_attempt(s, c.effect, now);
    if (code != Code::applied) return code;
    if (s.pending.phase != Phase::reserved) return Code::bad_transition;
    if (c.payload_count != 1 || c.payload.is_zero() || c.handle.is_zero()) return Code::invalid;
    s.pending.payload = c.payload; s.pending.handle = c.handle; s.pending.phase = Phase::prepared;
    return Code::applied;
}
Code reduce(State& s, const Attempt& c, std::uint64_t now) noexcept {
    const auto code = gate_attempt(s, c.effect, now);
    if (code != Code::applied) return code;
    if (s.pending.phase != Phase::prepared) return Code::bad_transition;
    if (c.payload != s.pending.payload) return Code::invalid;
    s.pending.phase = Phase::attempted;
    return Code::applied;
}
Code reduce(State& s, const MarkUnknown& c, std::uint64_t) noexcept {
    if (s.pending.phase == Phase::none || s.pending.intent.effect != c.effect) return Code::wrong_effect;
    if (s.pending.phase == Phase::unknown) return Code::duplicate;
    if (s.pending.phase != Phase::attempted && s.pending.phase != Phase::prepared) return Code::bad_transition;
    s.pending.phase = Phase::unknown;
    return Code::applied;
}
Code reduce(State& s, const ReleaseUnsent& c, std::uint64_t) noexcept {
    if (s.pending.phase == Phase::none && s.last_release == c && c.effect.value()) return Code::duplicate;
    if (s.pending.phase == Phase::none || s.pending.intent.effect != c.effect) return Code::wrong_effect;
    if (s.pending.phase != Phase::reserved) return Code::bad_transition;
    if (c.evidence.is_zero()) return Code::invalid;
    s.last_release = c; release(s);
    return Code::applied;
}

Code reduce(State& s, const Settlement& x, std::uint64_t) noexcept {
    for (std::size_t n = 0; n < s.settlement_count; ++n) {
        if (x.effect == s.settlements[n].effect) {
            if (x == s.settlements[n]) return Code::duplicate;
            return conflict(s, x);
        }
    }
    if (s.pending.phase == Phase::none || x.effect != s.pending.intent.effect) return Code::wrong_effect;
    if (s.pending.phase != Phase::attempted && s.pending.phase != Phase::unknown) return Code::bad_transition;
    if (!x.finalized || x.transaction.is_zero() || x.evidence.is_zero()) return Code::invalid;
    if (x.custody != s.opening.binding.custody || x.payload != s.pending.payload) return conflict(s, x);
    for (std::size_t n = 0; n < s.settlement_count; ++n)
        if (x.transaction == s.settlements[n].transaction) return conflict(s, x);
    if (x.outcome == Outcome::failed_without_charge) {
        if (x.base_debit.value() || x.base_credit.value() || x.quote_debit.value() || x.quote_credit.value() || x.fee.value()) return conflict(s, x);
        record(s, x);
        release(s);
        return Code::applied;
    }
    if (x.outcome != Outcome::succeeded) return Code::invalid;
    const auto& i = s.pending.intent;
    if (x.fee.value() > i.fee_cap.value()) return conflict(s, x);
    const Wide fees = Wide{s.fees.value()} + x.fee.value();
    if (fees > max_money) return conflict(s, x);
    if (i.side == Side::buy) {
        if (x.base_debit.value() || x.quote_credit.value() || x.base_credit.value() < i.min_base_receive.value() ||
            x.quote_debit.value() > i.max_quote_debit.value() ||
            Wide{i.quote_input.value()} + x.fee.value() != x.quote_debit.value() || x.quote_debit.value() > s.quote.value()) return conflict(s, x);
        const Wide base = Wide{s.base.value()} + x.base_credit.value();
        const Wide cost = Wide{s.cost.value()} + i.quote_input.value();
        const Wide purchases = Wide{s.purchases.value()} + x.quote_debit.value();
        const SignedWide pnl = SignedWide{s.realized_pnl} - x.fee.value();
        if (base > max_atoms || cost > max_money || purchases > s.opening.limits.max_purchase_total.value() ||
            pnl < std::numeric_limits<std::int64_t>::min()) return conflict(s, x);
        s.base = BaseAtoms{static_cast<std::uint64_t>(base)};
        s.quote = QuoteAtoms{s.quote.value() - x.quote_debit.value()};
        s.cost = QuoteAtoms{static_cast<std::uint64_t>(cost)};
        s.purchases = QuoteAtoms{static_cast<std::uint64_t>(purchases)};
        s.realized_pnl = static_cast<std::int64_t>(pnl);
    } else {
        if (x.base_credit.value() || x.quote_debit.value() || x.base_debit != i.base_input ||
            x.base_debit.value() > s.base.value() || x.quote_credit.value() < i.min_quote_receive.value() ||
            Wide{x.quote_credit.value()} + x.fee.value() > max_money) return conflict(s, x);
        const Wide quote = Wide{s.quote.value()} + x.quote_credit.value();
        const auto removed = static_cast<std::uint64_t>(Wide{s.cost.value()} * x.base_debit.value() / s.base.value());
        const SignedWide pnl = SignedWide{s.realized_pnl} + x.quote_credit.value() - removed;
        if (quote > max_money || pnl > std::numeric_limits<std::int64_t>::max() ||
            pnl < std::numeric_limits<std::int64_t>::min()) return conflict(s, x);
        s.base = BaseAtoms{s.base.value() - x.base_debit.value()};
        s.quote = QuoteAtoms{static_cast<std::uint64_t>(quote)};
        s.cost = QuoteAtoms{s.cost.value() - removed}; s.realized_pnl = static_cast<std::int64_t>(pnl);
    }
    s.fees = QuoteAtoms{static_cast<std::uint64_t>(fees)};
    record(s, x);
    release(s);
    return Code::applied;
}
Code reduce(State& s, const AmendBuyPrice& c, std::uint64_t now) noexcept {
    if (c.expected_revision || s.buy_price_amendment != BuyPriceAmendment{} ||
        c.expected_price != s.opening.limits.max_buy_price_per_sol) return Code::stale;
    if (s.mode != Mode::halted) return Code::stopped;
    if (s.pending != Pending{} || s.last_effect || s.settlement_count || s.last_release != ReleaseUnsent{} ||
        s.reserved_base.value() || s.reserved_quote.value() || s.purchases.value() || s.fees.value() || s.realized_pnl ||
        s.base != s.opening.base || s.quote != s.opening.quote || s.cost != s.opening.base_cost) return Code::busy;
    if (c.kill_epoch != s.kill_epoch || c.executor_generation != s.executor_generation) return Code::fenced;
    if (!now || c.opening_digest != opening_digest(s.opening) || c.review_evidence.is_zero() ||
        !c.replacement_price.value() || c.replacement_price.value()>max_money || c.replacement_price==c.expected_price)
        return Code::invalid;
    s.buy_price_amendment={c.replacement_price,c.review_evidence,c.kill_epoch,c.executor_generation,now};
    return Code::applied;
}

Code reduce(State& s, const Control& c, std::uint64_t) noexcept {
    if (!mode_valid(c.mode) || c.kill_epoch < s.kill_epoch || c.executor_generation < s.executor_generation) return Code::invalid;
    if (c.kill_epoch == s.kill_epoch && c.executor_generation == s.executor_generation) return Code::invalid;
    s.mode = c.mode; s.kill_epoch = c.kill_epoch; s.executor_generation = c.executor_generation;
    return Code::applied;
}

template<typename T> constexpr Role role_for() noexcept {
    if constexpr (std::is_same_v<T,Initialize> || std::is_same_v<T,Control> || std::is_same_v<T,AmendBuyPrice>) return Role::operator_role;
    if constexpr (std::is_same_v<T,Intent>) return Role::strategy;
    return Role::gateway;
}
}

QuoteAtoms effective_buy_price(const State& s) noexcept {
    return s.buy_price_amendment.price.value()?s.buy_price_amendment.price:s.opening.limits.max_buy_price_per_sol;
}

Role required_role(const Command& command) noexcept {
    return std::visit([](const auto& body) noexcept { return role_for<std::decay_t<decltype(body)>>(); }, command);
}

Code apply(State& state, const Command& command, Role role, std::uint64_t now) noexcept {
    return std::visit([&](const auto& body) noexcept {
        using T = std::decay_t<decltype(body)>;
        if (role != role_for<T>()) return Code::unauthorized;
        if constexpr (!std::is_same_v<T,Initialize>) if (!state.initialized) return Code::uninitialized;
        if (now < state.event_time) return Code::stale;
        if (state.conflicted) return Code::stopped;
        auto next = state;
        const auto code = reduce(next, body, now);
        if (code == Code::applied || code == Code::conflict) { next.event_time = now; state = next; }
        return code;
    }, command);
}

bool valid_state(const State& s) noexcept {
    if (!s.initialized) return s == State{};
    State baseline;
    if (apply(baseline, s.opening, Role::operator_role, 0) != Code::applied ||
        s.settlement_count > s.settlements.size() || s.settled_total < s.settlement_count ||
        (s.settled_total != s.settlement_count && s.settlement_count != s.settlements.size()) || !mode_valid(s.mode) ||
        s.kill_epoch < s.opening.kill_epoch || s.executor_generation < s.opening.executor_generation ||
        s.reserved_base.value() > s.base.value() || s.reserved_quote.value() > s.quote.value()) return false;
    const auto& a=s.buy_price_amendment;
    if (a != BuyPriceAmendment{}) {
        if (!a.price.value() || a.price.value()>max_money || a.price==s.opening.limits.max_buy_price_per_sol ||
            a.evidence.is_zero() || !a.event_time || a.event_time>s.event_time ||
            a.kill_epoch<s.opening.kill_epoch || a.kill_epoch>s.kill_epoch ||
            a.executor_generation<s.opening.executor_generation || a.executor_generation>s.executor_generation) return false;
        baseline.buy_price_amendment=a;
    }
    const bool complete = s.settled_total == s.settlement_count;
    std::uint64_t previous{};
    for (std::size_t n = 0; n < s.settlements.size(); ++n) {
        const auto& x = s.settlements[n];
        if (n >= s.settlement_count) { if (x != Settlement{}) return false; continue; }
        if (!x.effect.value() || x.effect.value() <= previous || !x.finalized ||
            x.custody != s.opening.binding.custody || x.payload.is_zero() || x.transaction.is_zero() || x.evidence.is_zero()) return false;
        previous = x.effect.value();
        for (std::size_t k = 0; k < n; ++k) if (x.transaction == s.settlements[k].transaction) return false;
        if (x.outcome == Outcome::failed_without_charge) {
            if (x.base_debit.value() || x.base_credit.value() || x.quote_debit.value() || x.quote_credit.value() || x.fee.value()) return false;
            if (complete) { record(baseline, x); baseline.last_effect = x.effect.value(); }
            continue;
        }
        if (x.outcome != Outcome::succeeded) return false;
        if (!complete) {
            const bool buy = x.base_credit.value() && x.quote_debit.value() > x.fee.value() && !x.base_debit.value() && !x.quote_credit.value();
            const bool sell = x.base_debit.value() && !x.base_credit.value() && !x.quote_debit.value();
            if (!buy && !sell) return false;
            continue;
        }
        Intent i; i.binding = s.opening.binding; i.effect = x.effect; i.sign_by = 1;
        i.kill_epoch = s.opening.kill_epoch; i.executor_generation = s.opening.executor_generation; i.fee_cap = x.fee;
        if (x.base_credit.value()) {
            if (x.quote_debit.value() <= x.fee.value()) return false;
            i.side = Side::buy; i.quote_input = QuoteAtoms{x.quote_debit.value() - x.fee.value()};
            i.max_quote_debit = x.quote_debit; i.min_base_receive = x.base_credit;
        } else {
            i.side = Side::sell; i.base_input = x.base_debit; i.min_quote_receive = x.quote_credit;
        }
        if (apply(baseline, i, Role::strategy, 0) != Code::applied ||
            apply(baseline, Prepare{i.effect, x.payload, x.payload, 1}, Role::gateway, 0) != Code::applied ||
            apply(baseline, Attempt{i.effect, x.payload}, Role::gateway, 0) != Code::applied ||
            apply(baseline, x, Role::gateway, 0) != Code::applied) return false;
    }
    if (!complete) {
        if (s.purchases.value() > s.opening.limits.max_purchase_total.value() || s.cost.value() > max_money ||
            s.fees.value() > max_money || s.quote.value() > max_money ||
            (!s.base.value() && s.cost.value())) return false;
        baseline.base = s.base; baseline.quote = s.quote; baseline.cost = s.cost; baseline.fees = s.fees;
        baseline.purchases = s.purchases; baseline.realized_pnl = s.realized_pnl; baseline.last_effect = previous;
        baseline.settlements = s.settlements; baseline.settlement_count = s.settlement_count; baseline.settled_total = s.settled_total;
    }
    if (s.base != baseline.base || s.quote != baseline.quote || s.cost != baseline.cost ||
        s.fees != baseline.fees || s.purchases != baseline.purchases || s.realized_pnl != baseline.realized_pnl ||
        s.last_effect < baseline.last_effect) return false;
    if (s.last_release.effect.value()) {
        if (s.last_release.evidence.is_zero() || s.last_release.effect.value() > s.last_effect) return false;
        for (std::size_t n=0;n<s.settlement_count;++n) if(s.settlements[n].effect==s.last_release.effect) return false;
    } else if (s.last_release != ReleaseUnsent{}) return false;
    if (s.conflicted != (s.conflict_evidence != Settlement{})) return false;
    if (s.pending.phase == Phase::none) return s.pending == Pending{} && !s.reserved_base.value() && !s.reserved_quote.value();
    const auto& p = s.pending;
    if (p.phase != Phase::reserved && p.phase != Phase::prepared && p.phase != Phase::attempted &&
        p.phase != Phase::unknown && p.phase != Phase::conflict) return false;
    if (p.intent.effect.value() != s.last_effect || p.intent.effect.value() <= s.last_release.effect.value() ||
        p.intent.kill_epoch > s.kill_epoch || p.intent.executor_generation > s.executor_generation ||
        p.intent.kill_epoch < s.opening.kill_epoch || p.intent.executor_generation < s.opening.executor_generation ||
        (p.phase == Phase::conflict) != s.conflicted) return false;
    baseline.kill_epoch = p.intent.kill_epoch; baseline.executor_generation = p.intent.executor_generation;
    const auto lifetime = s.opening.limits.max_sign_lifetime;
    const auto earliest = std::max(p.intent.source_time, p.intent.sign_by > lifetime ? p.intent.sign_by - lifetime : 0);
    if (earliest > s.event_time || apply(baseline, p.intent, Role::strategy, earliest) != Code::applied ||
        baseline.reserved_base != s.reserved_base || baseline.reserved_quote != s.reserved_quote) return false;
    if (p.phase == Phase::reserved) return p.payload.is_zero() && p.handle.is_zero();
    if (p.phase == Phase::conflict) return p.payload.is_zero() == p.handle.is_zero();
    return !p.payload.is_zero() && !p.handle.is_zero();
}

}
