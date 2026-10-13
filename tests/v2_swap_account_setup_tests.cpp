#include "velocita/v2/swap_account_setup.hpp"
#include "velocita/v2/digest.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace velocita::v2;
constexpr std::string_view user="12345678-1234-1234-1234-123456789abc";
truemarkets::SolanaAccountKey owner(){truemarkets::SolanaAccountKey k{};k.back()=1;return k;}
std::string setup(){return R"({"id":"12345678-1234-1234-1234-123456789abc","wallets":[{"name":"main","provider":"turnkey","addresses":[{"chain":"solana","address":"11111111111111111111111111111112","settlement_authority":false}]}],"trading":{"defi":{"enabled":true}},"settlement":{"defi":{"enabled":false}}})";}
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
std::string change(std::string text,std::string_view old,std::string_view value){const auto n=text.find(old);require(n!=text.npos,"fixture target exists");text.replace(n,old.size(),value);return text;}
void exact_owner(){auto s=setup();auto r=verify_swap_account_setup(s,user,owner());require(r.code==SwapAccountSetupCode::matching_setup_only && r.response_digest==sha256_digest(std::as_bytes(std::span(s))),"main SOL owner matches independently of CeFi settlement permission");}
void identity(){auto s=setup();require(verify_swap_account_setup(s,"22345678-1234-1234-1234-123456789abc",owner()).code==SwapAccountSetupCode::wrong_user,"another authenticated user refuses");auto k=owner();k.back()=2;require(verify_swap_account_setup(s,user,k).code==SwapAccountSetupCode::wrong_wallet,"another custody wallet refuses");require(verify_swap_account_setup(s,user,{}).code==SwapAccountSetupCode::invalid_pin,"zero owner refuses");require(!swap_account_user_id_valid("not-a-user") && swap_account_user_id_valid(user),"user pin has canonical UUID shape");}
void permissions(){require(verify_swap_account_setup(change(setup(),"\"enabled\":true","\"enabled\":false"),user,owner()).code==SwapAccountSetupCode::defi_disabled,"DeFi disabled refuses");require(verify_swap_account_setup(change(setup(),"\"main\"","\"strategies\""),user,owner()).code==SwapAccountSetupCode::wallet_ambiguous,"strategy wallet is not main custody");require(verify_swap_account_setup(change(setup(),"\"turnkey\"","\"other\""),user,owner()).code==SwapAccountSetupCode::wallet_ambiguous,"unsupported wallet provider refuses");}
void ambiguity(){auto s=setup();const std::string address=R"({"chain":"solana","address":"11111111111111111111111111111112"})";s=change(s,"\"addresses\":[","\"addresses\":["+address+",");require(verify_swap_account_setup(s,user,owner()).code==SwapAccountSetupCode::wallet_ambiguous,"duplicate SOL address refuses even when equal");s=change(setup(),"\"wallets\":[",R"("wallets":[{"name":"main","provider":"turnkey","addresses":[]},)");require(verify_swap_account_setup(s,user,owner()).code==SwapAccountSetupCode::wallet_ambiguous,"duplicate main wallet refuses");}
void malformed(){require(verify_swap_account_setup(change(setup(),"\"id\":", "\"id\":\"other\",\"id\":"),user,owner()).code==SwapAccountSetupCode::malformed,"duplicate JSON key refuses");require(verify_swap_account_setup(std::string(32769,' '),user,owner()).code==SwapAccountSetupCode::malformed,"oversized setup refuses");require(verify_swap_account_setup(change(setup(),"11111111111111111111111111111112","01111111111111111111111111111112"),user,owner()).code==SwapAccountSetupCode::wrong_wallet,"invalid base58 refuses");}
}
int main(int argc,char**argv){unsigned n{},failed{};for(auto [name,test]:{std::pair{"exact_owner",exact_owner},{"identity",identity},{"permissions",permissions},{"ambiguity",ambiguity},{"malformed",malformed}}){if(argc==2 && std::string_view(argv[1])!=name)continue;++n;try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}return n && !failed?0:1;}
