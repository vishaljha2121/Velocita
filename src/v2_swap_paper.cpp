#include "velocita/v2/swap.hpp"
#include <limits>

namespace velocita::v2::swap {
namespace {
__extension__ using Wide = unsigned __int128;
constexpr std::uint64_t atom_scale = 1'000'000'000;
constexpr auto max_money = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
constexpr auto max_atoms = std::numeric_limits<std::uint64_t>::max();
Wide ceil_ratio(Wide n, std::uint64_t d) noexcept { return n / d + (n % d != 0); }
}
PaperFill simulate(const State& s, PaperQuote q, Digest256 transaction, Digest256 evidence) noexcept {
    PaperFill result;
    if (s.conflicted || s.pending.phase != Phase::attempted || !q.price_per_sol.value() ||
        q.price_per_sol.value() > max_money || q.fee_ppm > 1'000'000 || transaction.is_zero() || evidence.is_zero()) return result;
    if (s.pending.intent.side == Side::buy && q.use_exact_buy_fee && q.fee_ppm) return result;
    auto& x = result.settlement;
    const auto& i = s.pending.intent;
    x.effect = i.effect; x.custody = s.opening.binding.custody; x.payload = s.pending.payload;
    x.transaction = transaction; x.evidence = evidence; x.finalized = true; x.outcome = Outcome::succeeded;
    Wide fee{}, gross{}, net{}, received{};
    if (i.side == Side::buy) {
        fee = q.use_exact_buy_fee ? Wide{q.exact_buy_fee.value()} :
            ceil_ratio(Wide{i.quote_input.value()} * q.fee_ppm, 1'000'000);
        gross = Wide{i.quote_input.value()} + fee;
        received = Wide{i.quote_input.value()} * atom_scale / q.price_per_sol.value();
        const bool funded = received >= q.buy_base_deduction.value();
        net = funded ? received - q.buy_base_deduction.value() : 0;
        if (gross <= i.max_quote_debit.value() && fee <= i.fee_cap.value() && received <= max_atoms && funded && net >= i.min_base_receive.value()) {
            x.base_credit = BaseAtoms{static_cast<std::uint64_t>(net)};
            x.quote_debit = QuoteAtoms{static_cast<std::uint64_t>(gross)};
            x.fee = QuoteAtoms{static_cast<std::uint64_t>(fee)};
        } else x.outcome = Outcome::failed_without_charge;
    } else {
        gross = Wide{i.base_input.value()} * q.price_per_sol.value() / atom_scale;
        if (gross > max_money) return result;
        fee = ceil_ratio(gross * q.fee_ppm, 1'000'000); net = gross - fee;
        if (fee <= i.fee_cap.value() && net >= i.min_quote_receive.value()) {
            x.base_debit = i.base_input; x.quote_credit = QuoteAtoms{static_cast<std::uint64_t>(net)};
            x.fee = QuoteAtoms{static_cast<std::uint64_t>(fee)};
        } else x.outcome = Outcome::failed_without_charge;
    }
    result.code = Code::applied;
    return result;
}
}
