#include "v2_swap_test_support.hpp"
#include "velocita/v2/state_snapshot.hpp"
#include "velocita/v2/digest.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    using namespace velocita::v2;
    auto state=catalog_test::state(shadow_catalog::truemarkets_two_row_catalog());
    state.kill_epoch=KillEpoch{1};state.executor_generation=ExecutorGeneration{1};ApplyScratch scratch;
    catalog_test::grant(state,21,CallerRole::strategy);
    const auto send=[&](const Command& command){return apply(state,command,scratch,&shadow_catalog::truemarkets_two_row_catalog());};
    if(send(swap_test::command(9,1,swap_test::opening())).code!=ApplyCode::applied)throw std::runtime_error("opening");
    if(send({{CallerId{9},RequestSequence{2},swap_test::digest(91)},SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}}}).code!=ApplyCode::applied)
        throw std::runtime_error("mode");
    std::array<std::byte,kMaxStateSnapshotBytes> bytes{};
    for(unsigned seq=0;seq<=2;++seq){
        if(seq){const auto result=send(swap_test::command(21,seq,swap_test::buy(swap_test::opening())));
            if(result.code!=ApplyCode::unauthorized_command)throw std::runtime_error("old rejection changed");
            std::cout<<"code="<<static_cast<unsigned>(result.code)<<' ';}
        const auto encoded=encode_state_snapshot(state,bytes);
        if(encoded.error!=StateSnapshotError::none)throw std::runtime_error("snapshot");
        const auto digest=sha256_digest(std::span(bytes).first(encoded.bytes_written));
        std::cout<<"sequence="<<seq<<" bytes="<<encoded.bytes_written<<" sha256=";
        for(auto b:digest.bytes){const auto v=std::to_integer<unsigned>(b);std::cout<<"0123456789abcdef"[v>>4]<<"0123456789abcdef"[v&15];}
        std::cout<<'\n';
    }
}
