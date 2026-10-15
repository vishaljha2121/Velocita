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
swap::GrantStrategy grant(const Rig& r) {
    return {swap::opening_digest(r.s.spot_swap.opening),swap_test::digest(97),CallerId{21},RequestSequence{3},1,1};
}
void strategy_grant_replay_and_limits() {
    Rig r;r.fund();catalog_test::grant(r.s,21,CallerRole::strategy);
    require(r.mode(StrategyMode::normal).code==ApplyCode::applied,"grant_initial_normal");
    const auto old=r.s.spot_swap;const auto version=r.s.state_version;
    for(unsigned seq:{1,2}) require(r.send(swap_test::command(21,seq,swap_test::buy(old.opening))).code==ApplyCode::unauthorized_command,
        "grant_old_requests_still_refused");
    require(r.s.spot_swap==old && r.s.state_version==version,"grant_refusals_preserve_state");
    const auto g=grant(r);
    require(r.swap(g,9,101).code==ApplyCode::decision_not_authorizable,"grant_requires_halted");
    require(r.mode(StrategyMode::halted).code==ApplyCode::applied,"grant_halt");
    require(r.swap(g,21,101).code==ApplyCode::unauthorized_command,"grant_strategy_cannot_self_grant");
    require(r.swap(g,22,101).code==ApplyCode::unauthorized_command,"grant_gateway_cannot_grant");
    require(r.swap(g,9,101).code==ApplyCode::applied,"grant_operator_commits");r.snapshot();
    require(r.swap(g,9,102).code==ApplyCode::request_expired,"grant_is_one_time");
    require(r.mode(StrategyMode::normal).code==ApplyCode::applied,"grant_resume");
    for(unsigned seq:{1,2}) require(r.send(swap_test::command(21,seq,swap_test::buy(old.opening),102)).code==ApplyCode::unauthorized_command,
        "grant_cannot_reissue_old_sequences");
    catalog_test::grant(r.s,24,CallerRole::strategy);
    require(r.swap(swap_test::buy(old.opening),24,102).code==ApplyCode::unauthorized_command,"grant_other_strategy_refused");
    require(r.swap(swap_test::buy(old.opening),22,102).code==ApplyCode::unauthorized_command,"grant_gateway_still_refused");
    require(r.swap(old.opening,21,102).code==ApplyCode::unauthorized_command,"grant_strategy_cannot_fund");
    require(r.swap(swap::Prepare{},21,102).code==ApplyCode::unauthorized_command,"grant_strategy_cannot_forge_evidence");
    require(r.send({{CallerId{21},RequestSequence{99},swap_test::digest(98)},SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}}}).code==ApplyCode::unauthorized_control,"grant_strategy_cannot_control");
    auto bad=swap_test::buy(old.opening);bad.min_base_receive=swap::BaseAtoms{1};
    require(r.send(swap_test::command(21,3,bad,102)).code==ApplyCode::insufficient_risk_capacity,"grant_price_bound_remains");
    bad=swap_test::buy(old.opening);bad.binding.custody=swap_test::digest(98);
    require(r.send(swap_test::command(21,4,bad,102)).code==ApplyCode::invalid_command,"grant_binding_remains");
    const auto buy=swap_test::command(21,5,swap_test::buy(old.opening),102);
    require(r.send(buy).code==ApplyCode::applied,"grant_strategy_buy_admitted");r.snapshot();
    const auto once=r.s.spot_swap;
    require(r.send(buy).code==ApplyCode::applied && r.s.spot_swap==once,"grant_request_deduplicated");
    for(int n=0;n<3;++n){auto step=plan_swap_paper_step(r.s,{swap::QuoteAtoms{108'000'000},2000},103+n);
        require(step.record.has_value() && r.swap(step.record->command,22,103+n).code==ApplyCode::applied,"grant_buy_gateway_settles");}
    swap::Intent sell; sell.binding=old.opening.binding;sell.effect=EffectId{2};sell.side=Side::sell;
    sell.base_input=r.s.spot_swap.base;sell.min_quote_receive=swap::QuoteAtoms{900'000};sell.fee_cap=swap::QuoteAtoms{2000};
    sell.source_time=106;sell.sign_by=20106;sell.kill_epoch=1;sell.executor_generation=1;
    require(r.send(swap_test::command(21,6,sell,106)).code==ApplyCode::applied,"grant_strategy_sell_admitted");r.snapshot();
    for(int n=0;n<3;++n){auto step=plan_swap_paper_step(r.s,{swap::QuoteAtoms{108'000'000},2000},107+n);
        require(step.record.has_value() && r.swap(step.record->command,22,107+n).code==ApplyCode::applied,"grant_sell_gateway_settles");}
    require(r.s.spot_swap.base.value()==0 && r.s.spot_swap.quote.value()==4'995'999 && r.s.spot_swap.fees.value()==4000,
        "grant_roundtrip_exact_oracle");r.snapshot();
    require(r.mode(StrategyMode::halted).code==ApplyCode::applied,"grant_halt_after_trade");
    require(r.send(swap_test::command(21,7,swap_test::buy(old.opening,3),110)).code==ApplyCode::decision_not_authorizable,"grant_halt_still_enforced");
    require(r.send({{CallerId{9},RequestSequence{r.seq++},swap_test::digest(96)},AdvanceKillEpoch{KillEpoch{2}}}).code==ApplyCode::applied,"grant_kill_advance");
    r.snapshot();require(r.mode(StrategyMode::normal).code==ApplyCode::applied,"grant_resume_after_kill");
    bad=swap_test::buy(old.opening,3);bad.kill_epoch=2;
    require(r.send(swap_test::command(21,8,bad,111)).code==ApplyCode::unauthorized_command,"grant_epoch_change_revokes_authority");
}
void strategy_grant_legacy_snapshot_oracle() {
    Rig r;catalog_test::grant(r.s,21,CallerRole::strategy);r.fund();
    require(r.mode(StrategyMode::normal).code==ApplyCode::applied,"grant_legacy_normal");
    for(unsigned seq=0;seq<=2;++seq){
        if(seq)require(r.send(swap_test::command(21,seq,swap_test::buy(r.s.spot_swap.opening))).code==ApplyCode::unauthorized_command,"grant_legacy_refusal_code");
        std::array<std::byte,kMaxStateSnapshotBytes> bytes{};const auto encoded=encode_state_snapshot(r.s,bytes);
        require(encoded.error==StateSnapshotError::none && encoded.bytes_written==4261,"grant_legacy_snapshot_byte_count");
        const auto digest=sha256_digest(std::span(bytes).first(encoded.bytes_written));
        std::string hex;for(auto b:digest.bytes){const auto v=std::to_integer<unsigned>(b);hex+="0123456789abcdef"[v>>4];hex+="0123456789abcdef"[v&15];}
        require(hex=="1c54cf2eb7c87f4449dc69d34b851ca523dd55cd4497df5c9fd3f735c61fdb6c","grant_legacy_snapshot_digest");
    }
}
void strategy_grant_codec_oracles() {
    Rig r;r.fund();catalog_test::grant(r.s,21,CallerRole::strategy);
    for(bool amendment:{false,true}) {
        auto state=r.s.spot_swap;
        if(amendment){const swap::AmendBuyPrice a{swap::opening_digest(state.opening),swap_test::digest(99),state.opening.limits.max_buy_price_per_sol,swap::QuoteAtoms{147'537'213},0,1,1};
            require(swap::apply(state,a,swap::Role::operator_role,101)==swap::Code::applied,"grant_combined_price_setup");}
        std::array<std::byte,swap::max_state_bytes> original{},bytes{};
        const auto old=swap::encode_state(state,original);
        require(old.error==swap::CodecError::none && old.size==(amendment?3821:3756),"grant_legacy_state_size_unchanged");
        require(swap::apply(state,grant(r),swap::Role::operator_role,102)==swap::Code::applied,"grant_codec_setup");
        const auto encoded=swap::encode_state(state,bytes);swap::State restored;
        require(encoded.error==swap::CodecError::none && encoded.size==(amendment?3894:3829),"grant_extension_size_oracle");
        require(bytes[old.size]==std::byte{2} && bytes[old.size+1]==std::byte{21} && bytes[old.size+9]==std::byte{3} &&
            bytes[old.size+17]==std::byte{97} && bytes[old.size+49]==std::byte{1} && bytes[old.size+57]==std::byte{1} &&
            bytes[old.size+65]==std::byte{102},"grant_extension_offsets_oracle");
        require(swap::decode_state(std::span(bytes).first(encoded.size),restored)==swap::CodecError::none && restored==state,"grant_both_extensions_roundtrip");
        bytes[old.size]=std::byte{3};
        require(swap::decode_state(std::span(bytes).first(encoded.size),restored)==swap::CodecError::format,"grant_unknown_tag_refused");
        bytes[old.size]=std::byte{2};
        require(swap::decode_state(std::span(bytes).first(encoded.size-1),restored)==swap::CodecError::format,"grant_truncated_extension_refused");
        require(swap::decode_state(std::span(original).first(old.size),restored)==swap::CodecError::none && restored.strategy_grant==swap::StrategyGrant{},"grant_old_snapshot_defaults_absent");
    }
    std::array<std::byte,swap::max_record_bytes> record{};
    const auto encoded=swap::encode({swap::Role::operator_role,101,grant(r)},record);
    require(encoded.error==swap::CodecError::none && encoded.size==148 && record[6]==std::byte{10} && record[7]==std::byte{1} &&
        record[84]==std::byte{21} && record[92]==std::byte{3},"grant_command_wire_oracle");
}
void strategy_grant_validation() {
    for(unsigned variant=0;variant<8;++variant){Rig r;r.fund();catalog_test::grant(r.s,21,CallerRole::strategy);auto g=grant(r);
        if(variant==0)g.opening_digest={};if(variant==1)g.review_evidence={};if(variant==2)g.caller=CallerId{9};
        if(variant==3)g.caller=CallerId{22};if(variant==4)g.caller=CallerId{99};if(variant==5)g.first_sequence={};
        if(variant==6)g.kill_epoch=2;if(variant==7)g.executor_generation=2;
        const auto before=r.s.spot_swap;const auto expected=variant>=6?ApplyCode::stale_control_generation:ApplyCode::invalid_command;
        require(r.swap(g,9,101).code==expected && r.s.spot_swap==before,"grant_invalid_binding_target_or_epoch_refused");}
    Rig used;used.reserve();catalog_test::grant(used.s,21,CallerRole::strategy);
    require(used.swap(swap::ReleaseUnsent{EffectId{1},swap_test::digest(98)},22,101).code==ApplyCode::applied,"grant_used_account_release");
    require(used.mode(StrategyMode::halted).code==ApplyCode::applied,"grant_used_account_halt");
    require(used.swap(grant(used),9,102).code==ApplyCode::order_slot_occupied,"grant_requires_pristine_account");
    Rig expired;expired.fund();catalog_test::grant(expired.s,21,CallerRole::strategy);
    for(auto& cursor:expired.s.caller_cursors)if(cursor.caller_id==CallerId{21})cursor.expired_below=RequestSequence{3};
    require(expired.swap(grant(expired),9,101).code==ApplyCode::request_expired,"grant_floor_must_exceed_expired_cursor");
    Rig r;r.fund();catalog_test::grant(r.s,21,CallerRole::strategy);require(r.swap(grant(r),9,101).code==ApplyCode::applied,"grant_snapshot_setup");
    auto corrupt=r.s;corrupt.spot_swap.strategy_grant.caller=CallerId{22};std::array<std::byte,kMaxStateSnapshotBytes>b{};
    require(encode_state_snapshot(corrupt,b).error==StateSnapshotError::invalid_state,"grant_snapshot_role_corruption_refused");
    Rig executor;executor.fund();catalog_test::grant(executor.s,21,CallerRole::strategy);
    require(executor.swap(grant(executor),9,101).code==ApplyCode::applied,"grant_executor_setup");
    require(executor.send({{CallerId{9},RequestSequence{executor.seq++},swap_test::digest(95)},SetExecutorGeneration{ExecutorGeneration{2}}}).code==ApplyCode::applied,"grant_executor_advance");
    executor.snapshot();require(executor.mode(StrategyMode::normal).code==ApplyCode::applied,"grant_executor_resume");
    auto intent=swap_test::buy(executor.s.spot_swap.opening);intent.executor_generation=2;
    require(executor.send(swap_test::command(21,3,intent,102)).code==ApplyCode::unauthorized_command,"grant_executor_change_revokes_authority");
    corrupt=r.s;corrupt.spot_swap.strategy_grant.evidence={};
    require(encode_state_snapshot(corrupt,b).error==StateSnapshotError::invalid_state,"grant_snapshot_empty_evidence_refused");
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
    for(const auto& [name,fn]:std::array<std::pair<const char*,void(*)()>,10>{{
        {"strategy_grant_replay_and_limits",strategy_grant_replay_and_limits},
        {"strategy_grant_validation",strategy_grant_validation},
        {"strategy_grant_codec_oracles",strategy_grant_codec_oracles},
        {"strategy_grant_legacy_snapshot_oracle",strategy_grant_legacy_snapshot_oracle},
        {"price_amendment_authority_and_replay",price_amendment_authority_and_replay},
        {"global_controls_fence_pending",global_controls_fence_pending},
        {"funding_roles_and_exclusion",funding_roles_and_exclusion},{"paper_restart_and_exact_receipt",paper_restart_and_exact_receipt},
        {"unknown_kill_and_late_settlement",unknown_kill_and_late_settlement},{"tampering_and_configuration_change",tampering_and_configuration_change}}}) {
        try {fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';return 1;}
    }
}
