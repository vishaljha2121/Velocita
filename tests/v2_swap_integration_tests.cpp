#include "v2_swap_test_support.hpp"
#include "velocita/v2/swap_paper_gateway.hpp"
#include "velocita/v2/kernel_codec.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace velocita::v2;
void require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
struct Rig {
    TradingState s = catalog_test::state(shadow_catalog::truemarkets_two_row_catalog());
    ApplyScratch scratch;
    std::uint64_t seq{1};
    Rig() { s.kill_epoch = KillEpoch{1}; s.executor_generation = ExecutorGeneration{1}; }
    ApplyResult send(Command c) {
        std::array<std::byte, kMaxKernelCommandBytes> bytes{};
        const auto encoded = encode_kernel_command(c, bytes);
        require(encoded.error == KernelCodecError::none, "swap command encodes canonically");
        KernelCommandCodecScratch temp; Command restored;
        require(decode_kernel_command(std::span(bytes).first(encoded.bytes_written), restored, temp).error == KernelCodecError::none,
                "swap command decodes canonically");
        return apply(s, restored, scratch, &shadow_catalog::truemarkets_two_row_catalog());
    }
    ApplyResult swap(swap::Command c, std::uint64_t caller = 9, std::uint64_t time = 100) {
        return send(swap_test::command(caller, seq++, c, time));
    }
    ApplyResult mode(StrategyMode m) {
        return send({{CallerId{9}, RequestSequence{seq++}, swap_test::digest(91)},
            SetStrategyMode{m, s.kill_epoch, s.executor_generation}});
    }
    void fund() { require(swap(swap_test::opening()).code == ApplyCode::applied, "operator funds halted swap account"); }
    void reserve() { fund(); require(mode(StrategyMode::normal).code == ApplyCode::applied, "operator enables swap risk");
        require(swap(swap_test::buy(swap_test::opening())).code == ApplyCode::applied, "operator admits explicit swap intent"); }
    void snapshot() {
        std::array<std::byte,kMaxStateSnapshotBytes> b{};
        const auto e=encode_state_snapshot(s,b); require(e.error==StateSnapshotError::none,"production snapshot accepts swap state");
        TradingState out; StateSnapshotScratch temp;
        require(decode_state_snapshot(std::span(b).first(e.bytes_written),out,temp).error==StateSnapshotError::none,"production snapshot restores swap state");
        require(out.spot_swap==s.spot_swap && out.state_version==s.state_version,"snapshot preserves exact balances and holds"); s=out;
    }
};
void funding_roles_and_exclusion() {
    Rig r; require(r.swap(swap_test::opening(),22).code==ApplyCode::unauthorized_command,"gateway cannot mint funds");
    r.fund();r.snapshot();
    require(r.swap(swap_test::buy(swap_test::opening()),22).code==ApplyCode::unauthorized_command,"gateway cannot originate a purchase");
    catalog_test::grant(r.s,23,CallerRole::strategy);
    require(r.swap(swap_test::buy(swap_test::opening()),23).code==ApplyCode::unauthorized_command,"strategy cannot bypass rule evaluation with explicit swap intent");
    for(CommandBody b : {CommandBody{ConfigureRiskLimits{}},CommandBody{PublishPolicy{}},CommandBody{StageExecutablePolicy{}}}) {
        require(apply(r.s,{{CallerId{9},RequestSequence{r.seq++},swap_test::digest(92)},b},r.scratch,
            &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::risk_reconfiguration_blocked,"legacy path cannot double fund swap balance");
    }
    auto bad=swap_test::opening();bad.binding.instrument=InstrumentId{1};Rig other;
    require(other.swap(bad).code==ApplyCode::catalog_binding_mismatch,"SOL atomic denomination cannot bind BTC row");
    Rig existing; existing.s.risk_account_count=1;
    require(existing.swap(swap_test::opening()).code==ApplyCode::risk_reconfiguration_blocked,"swap funding refuses existing legacy risk account");
}
void price_amendment_authority_and_replay() {
    Rig r;r.fund();const auto original=r.s.spot_swap.opening;
    const swap::AmendBuyPrice amendment{swap::opening_digest(original),swap_test::digest(99),original.limits.max_buy_price_per_sol,
        swap::QuoteAtoms{147'537'213},0,1,1};
    require(r.swap(amendment,22,101).code==ApplyCode::unauthorized_command,"gateway_cannot_amend_price");
    catalog_test::grant(r.s,23,CallerRole::strategy);
    require(r.swap(amendment,23,101).code==ApplyCode::unauthorized_command,"strategy_cannot_amend_price");
    auto command=swap_test::command(9,r.seq++,amendment,101);
    require(r.send(command).code==ApplyCode::applied,"owner_price_amendment_commits");
    const auto once=r.s.spot_swap;const auto version=r.s.state_version;
    require(r.send(command).code==ApplyCode::applied && r.s.spot_swap==once && r.s.state_version==version,"price_same_request_deduplicated");
    r.snapshot();require(r.s.spot_swap.opening==original && swap::effective_buy_price(r.s.spot_swap)==swap::QuoteAtoms{147'537'213},"price_snapshot_preserves_opening_and_override");
    require(r.swap(amendment,9,102).code==ApplyCode::request_expired,"price_second_request_stale");
    require(r.mode(StrategyMode::normal).code==ApplyCode::applied,"price_owner_can_resume");
    auto i=swap_test::buy(original);i.min_base_receive=swap::BaseAtoms{6'900'000};
    require(r.swap(i,9,102).code==ApplyCode::applied,"price_intent_uses_committed_override");r.snapshot();
    Rig competing;competing.fund();require(competing.mode(StrategyMode::normal).code==ApplyCode::applied,"price_unhalt_first");
    require(competing.swap(amendment,9,101).code==ApplyCode::decision_not_authorizable,"price_amendment_after_unhalt_refused");
}
void paper_restart_and_exact_receipt() {
    Rig r;r.reserve();
    const swap::PaperQuote q{swap::QuoteAtoms{108'000'000},2000};
    for(int phase=0;phase<3;++phase) {
        r.snapshot(); const auto next=plan_swap_paper_step(r.s,q,101+phase);
        require(next.valid && next.record.has_value(),"paper planner advances committed phase");
        require(r.swap(next.record->command,22,next.record->event_time).code==ApplyCode::applied,"gateway phase commits");
    }
    require(r.s.spot_swap.base.value()==9'259'259 && r.s.spot_swap.quote.value()==3'998'000 &&
        r.s.spot_swap.fees.value()==2000 && r.s.spot_swap.realized_pnl==-2000,"one dollar at 108 preserves fractional SOL and charges fee once");
    r.snapshot();require(!plan_swap_paper_step(r.s,q,105).record,"idle paper account emits no extra commands");
}
void unknown_kill_and_late_settlement() {
    Rig r;r.reserve();const swap::PaperQuote q{swap::QuoteAtoms{108'000'000},2000};
    for(int i=0;i<2;++i) {auto n=plan_swap_paper_step(r.s,q,101+i);require(n.record.has_value(),"prepare attempt available");r.swap(n.record->command,22,101+i);}
    const auto fill=plan_swap_paper_step(r.s,q,103);require(fill.record.has_value(),"settlement planned");
    require(r.swap(swap::MarkUnknown{EffectId{1}},22,103).code==ApplyCode::applied,"unknown commits");r.snapshot();
    require(!plan_swap_paper_step(r.s,q,104).valid,"unknown is never simulated again");
    require(r.mode(StrategyMode::halted).code==ApplyCode::applied,"operator halts unknown");
    require(r.mode(StrategyMode::normal).code==ApplyCode::reconciliation_required,"cannot resume unknown");
    require(r.s.spot_swap.reserved_quote.value()==1'002'000,"unknown retains fee-inclusive hold");
    require(r.swap(fill.record->command,22,105).code==ApplyCode::applied,"late settlement books while halted");r.snapshot();
    require(r.s.spot_swap.reserved_quote.value()==0,"only settlement releases unknown hold");
}
void global_controls_fence_pending() {
    for(const auto mode : {StrategyMode::halted,StrategyMode::cancel_only,StrategyMode::no_new_risk,StrategyMode::reduce_only,StrategyMode::flatten}) {
        Rig r;r.reserve();require(r.mode(mode).code==ApplyCode::applied,"global control commits");r.snapshot();
        require(r.swap(swap::Prepare{EffectId{1},swap_test::digest(7),swap_test::digest(8),1},22,101).code==ApplyCode::decision_not_authorizable,
            "non-normal modes cannot prepare a pending buy");
        require(r.s.spot_swap.reserved_quote.value()==1'002'000,"control never releases a held debit");
    }
    Rig r;r.reserve();
    require(r.send({{CallerId{9},RequestSequence{r.seq++},swap_test::digest(93)},AdvanceKillEpoch{KillEpoch{2}}}).code==ApplyCode::applied,
        "global kill advances");r.snapshot();
    require(r.s.spot_swap.kill_epoch==2 && r.s.spot_swap.mode==swap::Mode::halted,"global kill synchronizes swap fence");
    require(r.swap(swap::Control{swap::Mode::normal,3,2}).code==ApplyCode::unauthorized_command,"nested swap controls cannot bypass operator ceremony");
}
void tampering_and_configuration_change() {
    Rig r;r.reserve();auto c=swap_test::command(9,999,swap_test::buy(swap_test::opening(),2));
    std::get<swap::Intent>(std::get<ApplySpotSwap>(c.body).record.command).quote_input=swap::QuoteAtoms{2};
    const auto before=r.s.state_version;
    require(apply(r.s,c,r.scratch,&shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::invalid_command && r.s.state_version==before,"tampered payload cannot reuse identity digest");
    auto n=plan_swap_paper_step(r.s,{swap::QuoteAtoms{108'000'000},2000},101);r.swap(n.record->command,22,101);r.snapshot();
    require(!plan_swap_paper_step(r.s,{swap::QuoteAtoms{109'000'000},2000},102).valid,"restart cannot change prepared quote economics");
    auto changed_cost=swap::PaperQuote{swap::QuoteAtoms{108'000'000},2000};
    changed_cost.buy_base_deduction=swap::BaseAtoms{550840};
    require(!plan_swap_paper_step(r.s,changed_cost,102).valid,"restart_cannot_change_prepared_buy_deduction");
    changed_cost.buy_base_deduction={};changed_cost.exact_buy_fee=swap::QuoteAtoms{2000};changed_cost.use_exact_buy_fee=true;
    require(!plan_swap_paper_step(r.s,changed_cost,102).valid,"restart_cannot_change_prepared_exact_fee_mode");
    n=plan_swap_paper_step(r.s,{swap::QuoteAtoms{108'000'000},2000},102);
    require(n.record.has_value(),"original prepared quote still permits attempt");r.swap(n.record->command,22,102);r.snapshot();
    require(!plan_swap_paper_step(r.s,{swap::QuoteAtoms{109'000'000},2000},103).valid,"restart cannot change prepared quote economics");
    r.s.spot_swap.reserved_quote=swap::QuoteAtoms{1};std::array<std::byte,kMaxStateSnapshotBytes>b{};
    require(encode_state_snapshot(r.s,b).error==StateSnapshotError::invalid_state,"snapshot refuses corrupt reserved balance");
}
}
int main() {
    for(const auto& [name,fn]:std::array<std::pair<const char*,void(*)()>,6>{{
        {"price_amendment_authority_and_replay",price_amendment_authority_and_replay},
        {"global_controls_fence_pending",global_controls_fence_pending},
        {"funding_roles_and_exclusion",funding_roles_and_exclusion},{"paper_restart_and_exact_receipt",paper_restart_and_exact_receipt},
        {"unknown_kill_and_late_settlement",unknown_kill_and_late_settlement},{"tampering_and_configuration_change",tampering_and_configuration_change}}}) {
        try {fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';return 1;}
    }
}
