#include "velocita/v2/swap_balance.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using namespace velocita::v2;
using namespace velocita::v2::swap;
void require(bool value, const char* name) { if (!value) throw std::runtime_error(name); }
Digest256 digest(unsigned value) { Digest256 d; d.bytes[0] = static_cast<std::byte>(value); return d; }
struct Rig {
    State state;
    SwapBalanceBinding binding;
    Rig(std::uint64_t base = 31'896'934, std::uint64_t quote = 5'000'000) {
        Initialize x;
        x.binding = {digest(1), digest(2), digest(3), AccountId{2}, InstrumentId{2},
                     PolicyId{901}, PolicyVersion{1}, RiskPolicyVersion{1}};
        x.limits = {QuoteAtoms{5'000'000}, QuoteAtoms{2'000'000}, QuoteAtoms{10'000},
                    QuoteAtoms{500'000}, BaseAtoms{1'000'000'000}, BaseAtoms{},
                    QuoteAtoms{125'000'000}, QuoteAtoms{50'000'000}, 10, 20};
        x.base = BaseAtoms{base}; x.quote = QuoteAtoms{quote};
        x.funding_evidence = digest(4); x.kill_epoch = 1; x.executor_generation = 1;
        require(apply(state, x, Role::operator_role, 100) == Code::applied, "fixture funding admitted");
        binding = {x.binding, digest(99), 10'000'000};
    }
    SwapBalanceExpectation project() const { return expected_swap_balance(state, binding); }
    void reserve() {
        Intent i;
        i.binding = state.opening.binding; i.effect = EffectId{1}; i.side = Side::buy;
        i.quote_input = QuoteAtoms{1'000'000}; i.max_quote_debit = QuoteAtoms{1'002'000};
        i.fee_cap = QuoteAtoms{2'000}; i.min_base_receive = BaseAtoms{9'000'000};
        i.source_time = 100; i.sign_by = 120; i.kill_epoch = 1; i.executor_generation = 1;
        require(apply(state, i, Role::strategy, 100) == Code::applied, "fixture reservation admitted");
    }
    void prepare() {
        require(apply(state, Prepare{EffectId{1}, digest(5), digest(6), 1}, Role::gateway, 101) == Code::applied,
                "fixture preparation admitted");
    }
    void attempt() {
        require(apply(state, Attempt{EffectId{1}, digest(5)}, Role::gateway, 102) == Code::applied,
                "fixture attempt admitted");
    }
    Settlement fill() const {
        Settlement x;
        x.effect = EffectId{1}; x.custody = state.opening.binding.custody;
        x.payload = digest(5); x.transaction = digest(7); x.evidence = digest(8);
        x.outcome = Outcome::succeeded; x.base_credit = BaseAtoms{9'100'123};
        x.quote_debit = QuoteAtoms{1'002'000}; x.fee = QuoteAtoms{2'000}; x.finalized = true;
        return x;
    }
    SpotBalanceSnapshot snapshot(std::int64_t base = 31'896'934, std::int64_t quote = 5'000'000) const {
        return {binding.account_and_assets_digest, 1'000, {base, base, 0}, {quote, quote, 0}};
    }
};
void residual_and_sub_lot_atoms() {
    Rig r;
    const auto before = r.state;
    auto result = r.project();
    require(result.code == SwapBalanceCode::ready_for_comparison, "valid baseline projects");
    require(result.expected.base_lots == 3 && result.expected.base_atoms_per_lot == 10'000'000 &&
            result.expected.residual_base_atoms == 1'896'934 && result.expected.quote_atoms == 5'000'000,
            "oracle three lots plus 1896934 atoms preserves funding");
    require(result.expected.account_and_assets_digest == digest(99) &&
            result.expected.account_and_assets_digest != r.state.opening.binding.custody,
            "trusted account digest is distinct from custody digest");
    require(result.expected.unresolved_orders == 0 && r.state == before, "projection never mutates funding");
    Rig dust(1, 0); result = dust.project();
    require(result.code == SwapBalanceCode::ready_for_comparison && result.expected.base_lots == 0 &&
            result.expected.residual_base_atoms == 1 && result.expected.quote_atoms == 0,
            "one SOL atom remains inventory with zero cash");
}
void exact_settlement_projects_once() {
    Rig r; r.reserve(); r.prepare(); r.attempt();
    const auto fill = r.fill();
    require(apply(r.state, fill, Role::gateway, 103) == Code::applied, "fixture fill admitted");
    const auto result = r.project();
    require(result.code == SwapBalanceCode::ready_for_comparison && result.expected.base_lots == 4 &&
            result.expected.residual_base_atoms == 997'057 && result.expected.quote_atoms == 3'998'000,
            "oracle 31896934 plus 9100123 and 5000000 minus 1002000");
    require(apply(r.state, fill, Role::gateway, 104) == Code::duplicate, "duplicate settlement stays duplicate");
    require(r.project().expected.quote_atoms == 3'998'000, "adapter does not subtract settled fees twice");
    const auto check = compare_spot_balance(result.expected, r.snapshot(40'997'057, 3'998'000), 1'010, 10);
    require(check.verdict == SpotBalanceVerdict::matching_snapshot_only, "settled match grants no live readiness");
}
void binding_and_state_validation() {
    Rig r;
    require(expected_swap_balance(State{}, r.binding).code == SwapBalanceCode::uninitialized,
            "unfunded state has no expectation");
    r.binding.allocation.policy_version = PolicyVersion{2};
    require(r.project().code == SwapBalanceCode::invalid_binding, "changed allocation version refused");
    r.binding.allocation = r.state.opening.binding;
    r.binding.account_and_assets_digest = {};
    require(r.project().code == SwapBalanceCode::invalid_binding, "zero trusted account binding refused");
    r.binding.account_and_assets_digest = digest(99); r.binding.base_atoms_per_lot = 0;
    require(r.project().code == SwapBalanceCode::invalid_binding, "zero lot divisor refused");
    r.binding.base_atoms_per_lot = 10'000'000; r.state.quote = QuoteAtoms{5'000'001};
    const auto bad = r.project();
    require(bad.code == SwapBalanceCode::invalid_state && bad.expected.account_and_assets_digest.is_zero(),
            "unattributed one atom corruption yields no usable expectation");
}
void unresolved_and_conflicted_do_not_match() {
    Rig r; r.reserve();
    require(r.project().code == SwapBalanceCode::unresolved_activity, "reserved swap blocks baseline comparison");
    r.prepare();
    require(r.project().code == SwapBalanceCode::unresolved_activity, "prepared swap blocks baseline comparison");
    r.attempt();
    require(r.project().code == SwapBalanceCode::unresolved_activity, "attempted swap blocks baseline comparison");
    require(apply(r.state, MarkUnknown{EffectId{1}}, Role::gateway, 130) == Code::applied,
            "fixture unknown admitted");
    const auto before = r.state;
    require(r.project().code == SwapBalanceCode::unresolved_activity && r.state == before &&
            r.state.reserved_quote == QuoteAtoms{1'002'000}, "unknown retains all holds despite matching wallet");
    auto bad = r.fill(); bad.base_credit = BaseAtoms{8'999'999};
    require(apply(r.state, bad, Role::gateway, 131) == Code::conflict, "fixture conflict admitted");
    require(r.project().code == SwapBalanceCode::conflicted, "conflict cannot expose last good balance as current");
    Rig terminal; terminal.reserve(); terminal.prepare(); terminal.attempt();
    const auto fill = terminal.fill();
    require(apply(terminal.state, fill, Role::gateway, 103) == Code::applied, "terminal fixture fill admitted");
    bad = fill; bad.evidence = digest(9);
    require(apply(terminal.state, bad, Role::gateway, 104) == Code::conflict, "terminal conflict admitted");
    require(terminal.state.pending.phase == Phase::none && terminal.project().code == SwapBalanceCode::conflicted,
            "terminal conflict blocks projection without a pending phase");
}
void signed_range_boundaries() {
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    Rig r(maximum, maximum);
    auto result = r.project();
    require(result.code == SwapBalanceCode::ready_for_comparison &&
            result.expected.base_lots == 922'337'203'685 && result.expected.residual_base_atoms == 4'775'807 &&
            result.expected.quote_atoms == std::numeric_limits<std::int64_t>::max(),
            "INT64_MAX atoms decompose without multiplication overflow");
    Rig too_large(maximum + 1);
    require(too_large.project().code == SwapBalanceCode::out_of_range, "unsigned base beyond comparator range refused");
    r.binding.base_atoms_per_lot = maximum + 1;
    require(r.project().code == SwapBalanceCode::out_of_range, "unsigned lot divisor beyond comparator range refused");
}
void comparison_remains_independent() {
    Rig r;
    const auto expected = r.project().expected;
    auto observed = r.snapshot();
    require(compare_spot_balance(expected, observed, 1'010, 10).verdict == SpotBalanceVerdict::matching_snapshot_only,
            "inclusive observed age boundary only matches snapshot");
    require(compare_spot_balance(expected, observed, 1'011, 10).verdict == SpotBalanceVerdict::stale,
            "adapter does not bypass snapshot freshness");
    observed.account_and_assets_digest = r.state.opening.binding.custody;
    require(compare_spot_balance(expected, observed, 1'010, 10).verdict == SpotBalanceVerdict::identity_mismatch,
            "custody identity cannot substitute for account and assets");
    observed = r.snapshot(); --observed.base.available_atoms; observed.base.held_atoms = 1;
    require(compare_spot_balance(expected, observed, 1'010, 10).verdict == SpotBalanceVerdict::held_funds,
            "observed holds remain independently disqualifying");
    observed = r.snapshot(31'896'935);
    auto delta = compare_spot_balance(expected, observed, 1'010, 10);
    require(delta.verdict == SpotBalanceVerdict::unexplained_delta && delta.base_delta_atoms == 1,
            "one outside SOL atom remains unexplained not a fill");
    observed = r.snapshot(31'896'934, 4'999'999);
    delta = compare_spot_balance(expected, observed, 1'010, 10);
    require(delta.verdict == SpotBalanceVerdict::unexplained_delta && delta.quote_delta_atoms == -1,
            "one missing USDC atom remains unexplained");
}
}

int main(int argc, char** argv) {
    const std::pair<const char*, void (*)()> cases[] = {
        {"residual_and_sub_lot_atoms", residual_and_sub_lot_atoms},
        {"exact_settlement_projects_once", exact_settlement_projects_once},
        {"binding_and_state_validation", binding_and_state_validation},
        {"unresolved_and_conflicted_do_not_match", unresolved_and_conflicted_do_not_match},
        {"signed_range_boundaries", signed_range_boundaries},
        {"comparison_remains_independent", comparison_remains_independent}};
    unsigned ran = 0, failed = 0;
    for (const auto& [name, test] : cases) {
        if (argc == 2 && std::string_view(argv[1]) != name) continue;
        ++ran;
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    return ran && !failed ? 0 : 1;
}
