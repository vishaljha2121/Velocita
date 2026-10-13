#include "velocita/v2/swap_authority.hpp"
#include "v2_swap_test_support.hpp"
#include "velocita/v2/node_protocol.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <vector>
#include <iostream>
#include <stdexcept>

namespace {
using namespace velocita::v2;
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
TradingState fixture(bool sell=false){
    auto s=catalog_test::state(shadow_catalog::truemarkets_two_row_catalog());
    s.kill_epoch=KillEpoch{1};s.executor_generation=ExecutorGeneration{1};
    auto opening=swap_test::opening();if(sell){opening.base=swap::BaseAtoms{8'400'000};opening.base_cost=swap::QuoteAtoms{800'000};}
    require(swap::apply(s.spot_swap,opening,swap::Role::operator_role,100)==swap::Code::applied,"fixture opening applies");
    s.strategy_mode=StrategyMode::normal;synchronize_swap_controls(s);
    auto intent=swap_test::buy(opening);if(sell){intent.side=Side::sell;intent.quote_input={};intent.max_quote_debit={};intent.min_base_receive={};intent.base_input=opening.base;intent.min_quote_receive=swap::QuoteAtoms{1'000'000};}
    require(swap::apply(s.spot_swap,intent,swap::Role::strategy,100)==swap::Code::applied,"fixture buy applies");
    require(swap::apply(s.spot_swap,swap::Prepare{EffectId{1},swap_test::digest(40),swap_test::digest(41),1},swap::Role::gateway,101)==swap::Code::applied,"fixture prepare applies");
    require(swap_state_bindings_valid(s),"prepared fixture has valid global bindings");return s;
}
AuthorityBarrier request(const TradingState&s){
    return {{std::byte{1}},1,EffectId{1},swap_signing_authority_digest(s.spot_swap.pending),KillEpoch{1},ExecutorGeneration{1},
        s.spot_swap.opening.binding.catalog,swap_test::digest(42)};
}
void exact(){auto s=fixture();auto r=request(s);
    auto oracle=s.spot_swap.pending;oracle.intent.binding.catalog=swap_test::digest(2);
    require(swap_signing_authority_digest(oracle)==*parse_digest256("359c397880e46f6c0f0129d392cfd365f2271da7eb6f09be3f4ebcd3bb6b33c8"),"independent Python signing authority digest oracle");
    require(!r.expected_effect_payload_digest.is_zero() && swap_authority_matches(s,r,r.authority_domain),"exact prepared signing scope admits authority check");
    auto changed=s.spot_swap.pending;changed.intent.min_base_receive=swap::BaseAtoms{9'000'001};
    require(swap_signing_authority_digest(changed)!=r.expected_effect_payload_digest,"authority digest binds exact economic atoms");
    changed=s.spot_swap.pending;changed.handle=swap_test::digest(99);
    require(swap_signing_authority_digest(changed)!=r.expected_effect_payload_digest,"authority digest binds venue handle");
    changed=s.spot_swap.pending;changed.payload=swap_test::digest(99);
    require(swap_signing_authority_digest(changed)!=r.expected_effect_payload_digest,"authority digest binds exact signing payload");
}
void fences(){
    for(unsigned n=0;n<9;++n){auto s=fixture();auto r=request(s);
        if(n==0)r.authority_domain=swap_test::digest(99);
        if(n==1)r.expected_kill_epoch=KillEpoch{2};
        if(n==2)r.expected_executor_generation=ExecutorGeneration{2};
        if(n==3)r.catalog_root=swap_test::digest(99);
        if(n==4)r.effect_id=EffectId{2};
        if(n==5)r.expected_effect_payload_digest=swap_test::digest(99);
        if(n==6)require(swap::apply(s.spot_swap,swap::Attempt{EffectId{1},s.spot_swap.pending.payload},swap::Role::gateway,102)==swap::Code::applied,"attempted fixture transition valid");
        if(n==7){s.strategy_mode=StrategyMode::halted;synchronize_swap_controls(s);}
        if(n==8){s.strategy_mode=StrategyMode::reduce_only;synchronize_swap_controls(s);}
        require(swap_state_bindings_valid(s),"fence fixture is otherwise a valid state");
        require(!swap_authority_matches(s,r,swap_test::digest(42)),"wrong identity controls or phase refuses signing authority");
    }
}
void current_controls_and_expiry(){
    for(unsigned n=0;n<2;++n){auto s=fixture();auto r=request(s);if(n==0){s.kill_epoch=KillEpoch{2};r.expected_kill_epoch=KillEpoch{2};}else{s.executor_generation=ExecutorGeneration{2};r.expected_executor_generation=ExecutorGeneration{2};}synchronize_swap_controls(s);
        require(swap_state_bindings_valid(s),"stale intent retains valid synchronized global controls");
        require(!swap_authority_matches(s,r,r.authority_domain),"stale pending generation refuses despite current requested controls");}
    auto s=fixture(true);auto r=request(s);s.strategy_mode=StrategyMode::reduce_only;synchronize_swap_controls(s);
    require(swap_state_bindings_valid(s) && swap_authority_matches(s,r,r.authority_domain),"valid reducing sell remains authorized in reduce only mode");
    s.strategy_mode=StrategyMode::halted;synchronize_swap_controls(s);require(swap_state_bindings_valid(s),"halted sell state remains valid");
    require(!swap_authority_matches(s,r,r.authority_domain),"halted sell refuses signing authority");
    s=fixture();r=request(s);s.spot_swap.event_time=s.spot_swap.pending.intent.sign_by-1;
    require(swap_state_bindings_valid(s) && swap_authority_matches(s,r,r.authority_domain),"authority admits one tick before signing expiry");
    ++s.spot_swap.event_time;require(swap_state_bindings_valid(s),"exact expiry retains valid pending state");
    require(!swap_authority_matches(s,r,r.authority_domain),"authority refuses exactly at signing expiry");
    s=fixture();r=request(s);s.kill_epoch=KillEpoch{2};require(!swap_state_bindings_valid(s),"unsynchronized outer control fixture is invalid");
    require(!swap_authority_matches(s,r,r.authority_domain),"unsynchronized outer kernel controls refuse authority");
}
void invalid_digests(){auto p=fixture().spot_swap.pending;p.payload={};require(swap_signing_authority_digest(p).is_zero(),"missing payload yields no signing digest");p=fixture().spot_swap.pending;p.handle={};require(swap_signing_authority_digest(p).is_zero(),"missing handle yields no signing digest");}
void request_wire(){const auto s=fixture();const auto r=request(s);const GatewayRequest request{GatewayRequestKind::swap_authority,r.incarnation,9,r};
    std::array<std::byte,kGatewayAuthorityRequestBytes> bytes{};const auto encoded=encode_gateway_request(request,bytes);GatewayRequest decoded;
    require(encoded.error==NodeProtocolError::none && encoded.bytes_written==212 && bytes[0]==std::byte{0x12} && bytes[1]==std::byte{1} && decode_gateway_request(bytes,decoded)==NodeProtocolError::none && decoded==request,"swap authority request exact wire roundtrip");
    for(std::size_t n=0;n<bytes.size();++n)require(decode_gateway_request(std::span(bytes).first(n),decoded)==NodeProtocolError::truncated,"every truncated swap authority request refuses");
    for(unsigned n=0;n<5;++n){auto bad=bytes;if(n==0)bad[0]=std::byte{0x13};if(n==1)bad[1]=std::byte{2};if(n==2)std::fill(bad.begin()+18,bad.begin()+26,std::byte{});if(n==3)bad[2]^=std::byte{1};if(n==4)bad.back()^=std::byte{1};require(decode_gateway_request(bad,decoded)!=NodeProtocolError::none,"invalid swap request kind version correlation incarnation or payload refuses");}
    auto trailing=std::vector<std::byte>(bytes.begin(),bytes.end());trailing.push_back(std::byte{});require(decode_gateway_request(trailing,decoded)==NodeProtocolError::length_mismatch,"trailing swap request byte refuses");
}
void grant_wire(){const auto s=fixture();const auto r=request(s);const SwapAuthorityProofView view{r,2,7,10,11};const ResponseCorrelation correlation{9,swap_test::digest(90)};
    std::array<std::byte,kResponseSwapAuthorityGrantedBytes> bytes{};const auto encoded=encode_swap_authority_granted(correlation,view,bytes);ResponseCorrelation decoded_correlation;SwapAuthorityProofView decoded;
    require(encoded.error==NodeProtocolError::none && encoded.bytes_written==255 && bytes[0]==std::byte{7} && decode_swap_authority_granted(bytes,decoded_correlation,decoded)==NodeProtocolError::none && decoded_correlation==correlation && decoded.request==r && decoded.issuer_id==2 && decoded.term==7 && decoded.barrier_index==10 && decoded.applied_index==11,"swap authority grant exact wire roundtrip");
    for(std::size_t n=0;n<bytes.size();++n)require(decode_swap_authority_granted(std::span(bytes).first(n),decoded_correlation,decoded)==NodeProtocolError::truncated,"every truncated swap authority grant refuses");
    for(unsigned n=0;n<7;++n){auto bad=bytes;if(n==0)bad[0]=std::byte{6};if(n==1)std::fill(bad.begin()+1,bad.begin()+9,std::byte{});if(n==2)std::fill(bad.begin()+227,bad.begin()+231,std::byte{});if(n==3)std::fill(bad.begin()+231,bad.begin()+239,std::byte{});if(n==4)std::fill(bad.begin()+239,bad.begin()+247,std::byte{});if(n==5)bad[247]=std::byte{9};if(n==6)bad[226]^=std::byte{1};require(decode_swap_authority_granted(bad,decoded_correlation,decoded)!=NodeProtocolError::none,"invalid grant kind correlation issuer term indexes or inner marker refuses");}
    auto trailing=std::vector<std::byte>(bytes.begin(),bytes.end());trailing.push_back(std::byte{});require(decode_swap_authority_granted(trailing,decoded_correlation,decoded)==NodeProtocolError::length_mismatch,"trailing swap grant byte refuses");
    auto invalid=view;invalid.applied_index=9;require(encode_swap_authority_granted(correlation,invalid,bytes).error==NodeProtocolError::invalid_value,"grant encoder rejects applied index before barrier");
}
}
int main(int argc,char** argv){const std::pair<const char*,void(*)()> cases[]={{"exact",exact},{"fences",fences},{"current_controls_and_expiry",current_controls_and_expiry},{"invalid_digests",invalid_digests},{"request_wire",request_wire},{"grant_wire",grant_wire}};for(auto [name,fn]:cases){if(argc==2 && std::string_view(argv[1])!=name)continue;try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';return 1;}}}
