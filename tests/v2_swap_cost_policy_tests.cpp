#include "velocita/v2/swap_owner_accounting.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace velocita::v2;
void require(bool ok,const char* name){if(!ok)throw std::runtime_error(name);}
int main(){try{
    const SwapAcceptedBuyCosts accepted{swap::BaseAtoms{550840}};
    auto r=bound_buy_net_cost(8940092,2039280,1488440,swap::BaseAtoms{8389252},accepted);
    require(r.code==BuyNetCostCode::within_arithmetic_bounds_only && r.maximum_deduction==550840 && r.minimum_net_receipt==8389252,"hand_observed_funding_arithmetic");
    r=bound_buy_net_cost(8940092,2039280,1488440,swap::BaseAtoms{8389253},accepted);
    require(r.code==BuyNetCostCode::minimum_shortfall,"one_lamport_net_minimum_refused");
    r=bound_buy_net_cost(8940092,2039280,0,swap::BaseAtoms{8389252},accepted);
    require(r.code==BuyNetCostCode::deduction_exceeded && r.maximum_deduction==2039280,"idempotent_zero_funding_branch_exceeds_accepted_cost");
    r=bound_buy_net_cost(8940092,2039280,1488439,swap::BaseAtoms{1},accepted);
    require(r.code==BuyNetCostCode::deduction_exceeded,"one_lamport_over_cost_cap_refused");
    require(bound_buy_net_cost(100,200,300,swap::BaseAtoms{101},{}).code==BuyNetCostCode::minimum_shortfall,"surplus_funding_is_not_trade_receipt");
    require(bound_buy_net_cost(100,100,0,swap::BaseAtoms{1},{swap::BaseAtoms{100}}).code==BuyNetCostCode::minimum_shortfall,"deduction_cannot_underflow_output");
    require(bound_buy_net_cost(std::numeric_limits<std::uint64_t>::max(),0,0,swap::BaseAtoms{1},{}).code==BuyNetCostCode::invalid,"oversized_atoms_refused");
    require(observed_buy_cost_within_cap(9418072,8867232,accepted),"settled_buy_exact_cost_cap");
    require(!observed_buy_cost_within_cap(9418072,8867231,accepted),"settled_buy_one_atom_extra_cost_refused");
    require(!observed_buy_cost_within_cap(9418072,8867232,{}),"default_cost_policy_still_zero");
    require(!observed_buy_cost_within_cap(1,2,accepted),"net_gift_not_swap_credit");
    require(!observed_buy_cost_within_cap(1,0,accepted),"zero_receipt_refused");
    std::cout<<"PASS accepted_buy_cost_policy\n";
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
