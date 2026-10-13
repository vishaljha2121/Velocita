#include "velocita/v2/swap_account_setup.hpp"
#include "velocita/v2/swap_settlement_evidence.hpp"
#include "velocita/v2/truemarkets_wire.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
namespace velocita::v2 {
bool swap_account_user_id_valid(std::string_view user) noexcept {
    if(user.size()!=36)return false;
    bool nonzero{};
    for(std::size_t i=0;i<user.size();++i){
        if(i==8 || i==13 || i==18 || i==23){if(user[i]!='-')return false;}
        else {if(!((user[i]>='0'&&user[i]<='9')||(user[i]>='a'&&user[i]<='f')))return false;nonzero|=user[i]!='0';}
    }
    return nonzero;
}
SwapAccountSetupResult verify_swap_account_setup(std::string_view body,std::string_view user,
    const truemarkets::SolanaAccountKey& owner) noexcept {
    using namespace truemarkets;
    if(!swap_account_user_id_valid(user) || std::all_of(owner.begin(),owner.end(),[](auto b){return b==0;}))
        return {SwapAccountSetupCode::invalid_pin};
    if(validate_json_object(body)!=JsonError::none)return {};
    if(json_member_string(body,"id")!=user)return {SwapAccountSetupCode::wrong_user};
    const auto trading=json_member_raw(body,"trading");
    const auto defi=trading?json_member_raw(*trading,"defi"):std::nullopt;
    if(!defi || json_member_raw(*defi,"enabled")!="true")return {SwapAccountSetupCode::defi_disabled};
    const auto wallets=json_member_raw(body,"wallets");std::array<std::string_view,2> rows{};
    const auto count=wallets?json_array_elements(*wallets,rows):std::nullopt;
    if(!count || *count==0)return {SwapAccountSetupCode::wallet_ambiguous};
    unsigned main_count{},sol_count{};std::optional<SolanaAccountKey> observed;
    for(std::size_t i=0;i<*count;++i){
        const auto name=json_member_string(rows[i],"name");
        if(name!="main")continue;
        if(++main_count!=1 || json_member_string(rows[i],"provider")!="turnkey")return {SwapAccountSetupCode::wallet_ambiguous};
        const auto addresses=json_member_raw(rows[i],"addresses");std::array<std::string_view,2> entries{};
        const auto n=addresses?json_array_elements(*addresses,entries):std::nullopt;
        if(!n)return {SwapAccountSetupCode::wallet_ambiguous};
        for(std::size_t j=0;j<*n;++j){
            if(json_member_string(entries[j],"chain")!="solana")continue;
            if(++sol_count!=1)return {SwapAccountSetupCode::wallet_ambiguous};
            const auto text=json_member_string(entries[j],"address");
            if(text)observed=decode_solana_account_key(*text);
        }
    }
    if(main_count!=1 || sol_count!=1)return {SwapAccountSetupCode::wallet_ambiguous};
    if(!observed || *observed!=owner)return {SwapAccountSetupCode::wrong_wallet};
    return {SwapAccountSetupCode::matching_setup_only,sha256_digest(std::as_bytes(std::span(body)))};
}
}
