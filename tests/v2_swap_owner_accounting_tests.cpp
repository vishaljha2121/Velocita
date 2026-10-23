#include "velocita/v2/swap_owner_accounting.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace velocita::v2;
namespace {
void require(bool yes, const char* reason) { if (!yes) throw std::runtime_error(reason); }
truemarkets::SolanaInstructionKey key(std::uint8_t n) { truemarkets::SolanaInstructionKey k{}; k[31]=n; return k; }
struct Fixture {
    std::array<DeclaredOwnedAccount,3> scope{{{key(1),OwnedAccountKind::native_wallet},
        {key(2),OwnedAccountKind::wrapped_sol},{key(3),OwnedAccountKind::usdc_token}}};
    std::array<OwnerAccountChange,3> accounts{{{key(1),{true,10'000'000,0,0},{true,19'000'000,0,0}},
        {key(2),{},{}},{key(3),{true,2'039'280,2'000'000,0,DepositControl::owner},{true,2'039'280,998'000,0,DepositControl::owner}}}};
    swap::Intent intent{};
    Fixture() { intent.side=Side::buy; intent.quote_input=swap::QuoteAtoms{1'000'000};
        intent.max_quote_debit=swap::QuoteAtoms{1'002'000}; intent.min_base_receive=swap::BaseAtoms{9'000'000}; }
    OwnerPropertyMeasurement measure() const { return measure_owner_property(scope,accounts); }
};
void rent_oracles() {
    Fixture f;
    const auto newly_sponsored=f.measure();
    require(newly_sponsored.code==OwnerAccountingCode::measured_declared_scope_only &&
        newly_sponsored.sol_change==9'000'000 && newly_sponsored.usdc_change==-1'002'000 &&
        newly_sponsored.deposit_change==0,"newly_sponsored_rent_net_zero");
    f.accounts[1].before={true,5'039'280,3'000'000,2'039'280,DepositControl::owner};
    f.accounts[0].after.lamports=22'000'000;
    auto existing=f.measure();
    require(existing.sol_change==6'960'720 && existing.deposit_change==-2'039'280,
        "existing_owner_rent_is_not_sponsored");
    require(existing.before.total_lamports==17'078'560 && existing.after.total_lamports==24'039'280,
        "closed_wrapped_property_counted_once");
    require(compare_observed_swap_bounds(f.intent,existing)==ObservedSwapBounds::receipt_shortfall,
        "wallet_gain_cannot_hide_receipt_shortfall");
    f.accounts[0].after.lamports+=2'039'280;
    require(f.measure().sol_change==9'000'000,"returning_existing_rent_is_not_profit");
}
void external_rent_control() {
    Fixture f;
    f.accounts[2].before={};
    f.accounts[2].after={true,1'488'440,1'006'989,0,DepositControl::external};
    f.accounts[0].before.lamports=40'296'934;f.accounts[0].after.lamports=31'896'934;
    auto r=f.measure();
    require(r.code==OwnerAccountingCode::measured_declared_scope_only && r.sol_change==-8'400'000 &&
        r.usdc_change==1'006'989 && r.deposit_change==0 && r.after.external_deposits==1'488'440,
        "sponsor_controlled_rent_is_not_owner_receipt");
    f.accounts[2].after.deposit_control=DepositControl::unverified;
    require(f.measure().code==OwnerAccountingCode::invalid_account,"unknown_close_control_refused");
    f=Fixture{};f.accounts[2].after.deposit_control=DepositControl::external;
    require(f.measure().sol_change==6'960'720,"existing_rent_control_transfer_reduces_owner_property");
}
void captured_buy_rent_counterexample() {
    Fixture f;f.accounts[0].after.lamports=19'191'912;
    require(f.measure().sol_change==9'191'912,"capture_nominal_output_if_deposit_newly_sponsored");
    f.accounts[1].before={true,2'039'280,0,2'039'280,DepositControl::owner};
    const auto quoted=f.measure();
    require(quoted.sol_change==7'152'632 && quoted.deposit_change==-2'039'280,
        "capture_existing_owner_deposit_reduces_quoted_net_receipt");
    require(compare_observed_swap_bounds(f.intent,quoted)==ObservedSwapBounds::receipt_shortfall,
        "capture_quote_level_output_cannot_satisfy_nine_million_net_bound");
    f.accounts[0].after.lamports=18'732'317;
    const auto nominal=f.measure();
    require(nominal.sol_change==6'693'037 && nominal.base_change_excluding_deposits==8'732'317,
        "capture_conditional_slippage_output_minus_existing_rent");
    f.intent.min_base_receive=swap::BaseAtoms{6'693'037};
    require(compare_observed_swap_bounds(f.intent,nominal)==ObservedSwapBounds::within_observed_net_bounds_only,
        "capture_relaxed_receipt_is_only_an_observed_arithmetic_case");
    f.accounts[1].before.lamports=5'039'280;f.accounts[1].before.token_atoms=3'000'000;
    f.accounts[0].after.lamports+=3'000'000;
    require(f.measure().sol_change==6'693'037,"capture_existing_wrapped_backing_is_not_new_output");
}
void bounds_oracles() {
    Fixture f;
    require(compare_observed_swap_bounds(f.intent,f.measure())==ObservedSwapBounds::within_observed_net_bounds_only,
        "exact_buy_net_boundary");
    --f.accounts[2].after.token_atoms;
    require(compare_observed_swap_bounds(f.intent,f.measure())==ObservedSwapBounds::debit_exceeded,"one_quote_atom_excess");
    f=Fixture{};--f.accounts[0].after.lamports;
    require(compare_observed_swap_bounds(f.intent,f.measure())==ObservedSwapBounds::receipt_shortfall,"one_sol_atom_short");
    f=Fixture{};f.accounts[0].after.lamports=10'000'000;
    f.accounts[2].after.lamports+=9'000'000;
    require(compare_observed_swap_bounds(f.intent,f.measure())==ObservedSwapBounds::receipt_shortfall,
        "new_deposit_cannot_replace_base_receipt");
    f=Fixture{};f.intent={};f.intent.side=Side::sell;f.intent.base_input=swap::BaseAtoms{8'400'000};
    f.intent.min_quote_receive=swap::QuoteAtoms{1'006'989};
    f.accounts[0].after.lamports=1'600'000;f.accounts[2].after.token_atoms=3'006'989;
    require(compare_observed_swap_bounds(f.intent,f.measure())==ObservedSwapBounds::within_observed_net_bounds_only,
        "exact_sell_net_boundary");
    --f.accounts[0].after.lamports;++f.accounts[2].after.lamports;
    require(compare_observed_swap_bounds(f.intent,f.measure())==ObservedSwapBounds::debit_exceeded,
        "deposit_gift_cannot_hide_base_overspend");
    --f.accounts[2].after.lamports;
    require(compare_observed_swap_bounds(f.intent,f.measure())==ObservedSwapBounds::debit_exceeded,"sell_network_fee_not_hidden");
    f.accounts[0].after.lamports=1'600'000;--f.accounts[2].after.token_atoms;
    require(compare_observed_swap_bounds(f.intent,f.measure())==ObservedSwapBounds::receipt_shortfall,"sell_quote_shortfall");
}
void current_simulation_net_receipt() {
    Fixture f;
    f.accounts[0].after.lamports=18'860'117;
    f.accounts[2].before={true,1'488'440,1'006'989,0,DepositControl::external};
    f.accounts[2].after={true,1'488'440,6'989,0,DepositControl::external};
    f.intent.quote_input=swap::QuoteAtoms{998'000};
    f.intent.max_quote_debit=swap::QuoteAtoms{1'000'000};
    f.intent.min_base_receive=swap::BaseAtoms{8'896'147};
    const auto r=f.measure();
    require(r.code==OwnerAccountingCode::measured_declared_scope_only &&
        r.usdc_change==-1'000'000 && r.sol_change==8'860'117 && r.deposit_change==0,
        "simulation_fee_included_and_net_receipt_after_sponsor_return");
    require(compare_observed_swap_bounds(f.intent,r)==ObservedSwapBounds::receipt_shortfall,
        "simulation_swap_output_cannot_replace_net_wallet_receipt");
    f.intent.min_base_receive=swap::BaseAtoms{8'860'117};
    require(compare_observed_swap_bounds(f.intent,r)==ObservedSwapBounds::within_observed_net_bounds_only,
        "simulation_exact_observed_net_receipt_boundary");
    f.intent.min_base_receive=swap::BaseAtoms{8'860'118};
    require(compare_observed_swap_bounds(f.intent,r)==ObservedSwapBounds::receipt_shortfall,
        "simulation_one_lamport_tighter_net_bound_refused");
}
void scope_and_backing() {
    Fixture f; f.accounts[1].before={true,5'039'280,0,2'039'280,DepositControl::owner};f.accounts[1].after=f.accounts[1].before;
    auto r=f.measure();
    require(r.before.wrapped_backing==3'000'000 && r.before.total_lamports==17'078'560,
        "unsynced_native_amount_does_not_erase_backing");
    f.accounts[1].before.token_atoms=3'000'001;
    require(f.measure().code==OwnerAccountingCode::invalid_account,"token_amount_cannot_exceed_native_backing");
    f=Fixture{};f.accounts[1].before.lamports=1;
    require(f.measure().code==OwnerAccountingCode::invalid_account,"absent_account_cannot_hold_property");
    f=Fixture{};f.scope[1].kind=static_cast<OwnedAccountKind>(255);
    require(f.measure().code==OwnerAccountingCode::invalid_account,"unknown_absent_account_kind_refused");
    f=Fixture{};f.accounts[1].key=key(4);
    require(f.measure().code==OwnerAccountingCode::invalid_scope,"every_declared_account_present");
    f=Fixture{};f.scope[1].key=f.scope[0].key;f.accounts[1].key=f.accounts[0].key;
    require(f.measure().code==OwnerAccountingCode::invalid_scope,"duplicate_owned_account_refused");
    f=Fixture{};f.scope[0].kind=OwnedAccountKind::usdc_token;
    require(f.measure().code==OwnerAccountingCode::invalid_scope,"exactly_one_native_wallet");
    f=Fixture{};
    require(measure_owner_property(f.scope,std::span(f.accounts).first(2)).code==OwnerAccountingCode::invalid_scope,
        "omitted_closed_account_refused");
}
void overflow_and_invalid_results() {
    Fixture f;f.accounts[0].before.lamports=std::numeric_limits<std::int64_t>::max();
    const auto result=f.measure();
    require(result.code==OwnerAccountingCode::overflow && result.before==OwnerPropertyTotal{} && result.after==OwnerPropertyTotal{},
        "overflow_does_not_return_partial_totals");
    require(compare_observed_swap_bounds(f.intent,result)==ObservedSwapBounds::unusable,"invalid_accounting_not_accepted");
    f=Fixture{};f.intent.side=static_cast<Side>(255);
    require(compare_observed_swap_bounds(f.intent,f.measure())==ObservedSwapBounds::unusable,"unknown_side_refused");
}
}
int main(int argc,char** argv) {
    int n{},failures{};
    for (auto [name,fn]:std::array<std::pair<const char*,void(*)()>,7>{{{"rent",rent_oracles},{"bounds",bounds_oracles},{"external",external_rent_control},{"capture",captured_buy_rent_counterexample},{"simulation",current_simulation_net_receipt},
            {"scope",scope_and_backing},{"overflow",overflow_and_invalid_results}}}) {
        if (argc>1 && std::string_view(argv[1])!=name) continue;
        ++n;try {fn();std::cout<<"PASS "<<name<<'\n';} catch(const std::exception& e) {++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    }
    return !n || argc>2 ? 2 : failures ? 1 : 0;
}
