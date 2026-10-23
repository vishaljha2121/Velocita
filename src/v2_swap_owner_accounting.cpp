#include "velocita/v2/swap_owner_accounting.hpp"

#include <limits>

namespace velocita::v2 {
BuyNetCostResult bound_buy_net_cost(std::uint64_t gross,std::uint64_t payment,
    std::uint64_t funding,swap::BaseAtoms minimum,SwapAcceptedBuyCosts policy) noexcept {
    const auto maximum=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if(!gross || !minimum.value() || gross>maximum || payment>maximum || funding>maximum ||
        minimum.value()>maximum || policy.max_output_deduction.value()>maximum)return {};
    const auto deduction=payment>funding?payment-funding:0;
    BuyNetCostResult result{BuyNetCostCode::deduction_exceeded,deduction,0};
    if(deduction>policy.max_output_deduction.value())return result;
    result.code=BuyNetCostCode::minimum_shortfall;
    if(deduction>=gross)return result;
    result.minimum_net_receipt=gross-deduction;
    if(result.minimum_net_receipt>=minimum.value())result.code=BuyNetCostCode::within_arithmetic_bounds_only;
    return result;
}
bool observed_buy_cost_within_cap(std::uint64_t gross,std::uint64_t net,SwapAcceptedBuyCosts policy) noexcept {
    const auto maximum=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    return gross && net && gross<=maximum && net<=gross &&
        policy.max_output_deduction.value()<=maximum && gross-net<=policy.max_output_deduction.value();
}
namespace {
constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
bool add(std::uint64_t& total,std::uint64_t value) noexcept {
    if (value > maximum || total > maximum-value) return false;
    total += value;
    return true;
}
OwnerAccountingCode append(OwnedAccountKind kind,const AccountProperty& p,OwnerPropertyTotal& total) noexcept {
    if (kind!=OwnedAccountKind::native_wallet && kind!=OwnedAccountKind::wrapped_sol && kind!=OwnedAccountKind::usdc_token)
        return OwnerAccountingCode::invalid_account;
    if (!p.exists) {
        if (kind==OwnedAccountKind::native_wallet || p.lamports || p.token_atoms || p.native_reserve ||
            p.deposit_control!=DepositControl::unverified)
            return OwnerAccountingCode::invalid_account;
        return OwnerAccountingCode::measured_declared_scope_only;
    }
    if (kind==OwnedAccountKind::native_wallet) {
        if (p.token_atoms || p.native_reserve) return OwnerAccountingCode::invalid_account;
        if (!add(total.wallet_lamports,p.lamports) || !add(total.total_lamports,p.lamports)) return OwnerAccountingCode::overflow;
    } else if (kind==OwnedAccountKind::wrapped_sol) {
        if (!p.native_reserve || p.native_reserve>p.lamports || p.token_atoms>p.lamports-p.native_reserve)
            return OwnerAccountingCode::invalid_account;
        if (p.deposit_control!=DepositControl::owner && p.deposit_control!=DepositControl::external)
            return OwnerAccountingCode::invalid_account;
        if (!add(total.wrapped_backing,p.lamports-p.native_reserve) || !add(total.total_lamports,p.lamports-p.native_reserve))
            return OwnerAccountingCode::overflow;
        if (p.deposit_control==DepositControl::owner) {
            if (!add(total.refundable_deposits,p.native_reserve) || !add(total.total_lamports,p.native_reserve))
                return OwnerAccountingCode::overflow;
        } else if (!add(total.external_deposits,p.native_reserve)) return OwnerAccountingCode::overflow;
    } else if (kind==OwnedAccountKind::usdc_token) {
        if (p.native_reserve || (p.deposit_control!=DepositControl::owner && p.deposit_control!=DepositControl::external))
            return OwnerAccountingCode::invalid_account;
        if (!add(total.usdc_atoms,p.token_atoms)) return OwnerAccountingCode::overflow;
        if (p.deposit_control==DepositControl::owner) {
            if (!add(total.refundable_deposits,p.lamports) || !add(total.total_lamports,p.lamports))
                return OwnerAccountingCode::overflow;
        } else if (!add(total.external_deposits,p.lamports)) return OwnerAccountingCode::overflow;
    } else return OwnerAccountingCode::invalid_account;
    return OwnerAccountingCode::measured_declared_scope_only;
}
std::int64_t change(std::uint64_t before,std::uint64_t after) noexcept {
    return static_cast<std::int64_t>(after)-static_cast<std::int64_t>(before);
}
}
OwnerPropertyMeasurement measure_owner_property(std::span<const DeclaredOwnedAccount> scope,
    std::span<const OwnerAccountChange> accounts) noexcept {
    if (scope.empty() || scope.size()>kOwnerPropertyAccounts || scope.size()!=accounts.size()) return {};
    std::size_t wallets{};
    for (std::size_t i=0;i<scope.size();++i) {
        if (scope[i].key==truemarkets::SolanaInstructionKey{} || scope[i].key!=accounts[i].key) return {};
        for (std::size_t j=0;j<i;++j) if (scope[i].key==scope[j].key) return {};
        wallets += scope[i].kind==OwnedAccountKind::native_wallet;
    }
    if (wallets!=1) return {};
    OwnerPropertyMeasurement result;
    for (std::size_t i=0;i<scope.size();++i) {
        auto code=append(scope[i].kind,accounts[i].before,result.before);
        if (code!=OwnerAccountingCode::measured_declared_scope_only) return {code};
        code=append(scope[i].kind,accounts[i].after,result.after);
        if (code!=OwnerAccountingCode::measured_declared_scope_only) return {code};
    }
    result.sol_change=change(result.before.total_lamports,result.after.total_lamports);
    result.usdc_change=change(result.before.usdc_atoms,result.after.usdc_atoms);
    result.base_change_excluding_deposits=change(result.before.wallet_lamports+result.before.wrapped_backing,
        result.after.wallet_lamports+result.after.wrapped_backing);
    result.deposit_change=change(result.before.refundable_deposits,result.after.refundable_deposits);
    result.code=OwnerAccountingCode::measured_declared_scope_only;
    return result;
}
ObservedSwapBounds compare_observed_swap_bounds(const swap::Intent& intent,const OwnerPropertyMeasurement& result) noexcept {
    if (result.code!=OwnerAccountingCode::measured_declared_scope_only) return ObservedSwapBounds::unusable;
    if (intent.side==Side::buy) {
        if (!intent.quote_input.value() || intent.quote_input.value()>intent.max_quote_debit.value() ||
            intent.max_quote_debit.value()>maximum || !intent.min_base_receive.value() ||
            intent.min_base_receive.value()>maximum) return ObservedSwapBounds::unusable;
        if (result.usdc_change>=0 || result.usdc_change < -static_cast<std::int64_t>(intent.max_quote_debit.value()))
            return ObservedSwapBounds::debit_exceeded;
        if (result.sol_change < static_cast<std::int64_t>(intent.min_base_receive.value()) ||
            result.base_change_excluding_deposits < static_cast<std::int64_t>(intent.min_base_receive.value()))
            return ObservedSwapBounds::receipt_shortfall;
    } else if (intent.side==Side::sell) {
        if (!intent.base_input.value() || intent.base_input.value()>maximum || !intent.min_quote_receive.value() ||
            intent.min_quote_receive.value()>maximum) return ObservedSwapBounds::unusable;
        if (result.sol_change>=0 || result.sol_change < -static_cast<std::int64_t>(intent.base_input.value()) ||
            result.base_change_excluding_deposits < -static_cast<std::int64_t>(intent.base_input.value()))
            return ObservedSwapBounds::debit_exceeded;
        if (result.usdc_change < static_cast<std::int64_t>(intent.min_quote_receive.value()))
            return ObservedSwapBounds::receipt_shortfall;
    } else return ObservedSwapBounds::unusable;
    return ObservedSwapBounds::within_observed_net_bounds_only;
}
}
