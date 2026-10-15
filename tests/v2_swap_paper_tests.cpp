#include "velocita/v2/swap_paper.hpp"
#include "velocita/v2/swap_paper_codec.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using namespace velocita::v2;
using namespace velocita::v2::swap_paper;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Digest256 digest(unsigned value) { Digest256 d; d.bytes[0] = static_cast<std::byte>(value); return d; }
Initialize opening() {
    Initialize c;
    c.binding = {digest(1), digest(2), digest(3), AccountId{2}, InstrumentId{2},
                 PolicyId{901}, PolicyVersion{1}, RiskPolicyVersion{1}};
    c.limits = {QuoteAtoms{5'000'000}, QuoteAtoms{2'000'000}, QuoteAtoms{10'000},
                QuoteAtoms{500'000}, BaseAtoms{1'000'000'000}, BaseAtoms{3'000'000},
                QuoteAtoms{125'000'000}, QuoteAtoms{50'000'000}, 10, 20};
    c.base = BaseAtoms{31'896'934}; c.quote = QuoteAtoms{5'000'000};
    c.base_cost = QuoteAtoms{3'000'000}; c.funding_evidence = digest(4);
    c.kill_epoch = 1; c.executor_generation = 1;
    return c;
}
struct Rig {
    State s;
    Rig() { require(apply(s, opening(), Role::operator_role, 100) == Code::applied, "operator baseline initializes"); }
    Intent buy(std::uint64_t id = 1) const {
        Intent i; i.binding = s.opening.binding; i.effect = EffectId{id}; i.side = Side::buy;
        i.quote_input = QuoteAtoms{1'000'000}; i.max_quote_debit = QuoteAtoms{1'002'000};
        i.fee_cap = QuoteAtoms{2'000}; i.min_base_receive = BaseAtoms{9'000'000};
        i.source_time = 100; i.sign_by = 120; i.kill_epoch = s.kill_epoch;
        i.executor_generation = s.executor_generation; return i;
    }
    Intent sell(std::uint64_t id, std::uint64_t base) const {
        auto i = buy(id); i.side = Side::sell; i.quote_input = {}; i.max_quote_debit = {};
        i.min_base_receive = {}; i.base_input = BaseAtoms{base}; i.min_quote_receive = QuoteAtoms{1};
        return i;
    }
    void start(const Intent& i) {
        require(apply(s, i, Role::strategy, 100) == Code::applied, "reserve swap");
        require(apply(s, Prepare{i.effect, digest(5), digest(6), 1}, Role::gateway, 101) == Code::applied, "prepare one payload");
        require(apply(s, Attempt{i.effect, digest(5)}, Role::gateway, 102) == Code::applied, "attempt once");
    }
    Settlement fill(std::uint64_t base = 9'100'123, std::uint64_t quote = 1'002'000) const {
        Settlement x; x.effect = s.pending.intent.effect; x.custody = s.opening.binding.custody;
        x.payload = digest(5); x.transaction = digest(7); x.evidence = digest(8);
        x.outcome = Outcome::succeeded; x.base_credit = BaseAtoms{base};
        x.quote_debit = QuoteAtoms{quote}; x.fee = QuoteAtoms{2'000}; x.finalized = true;
        return x;
    }
};

void buy_price_amendment() {
    auto o=opening();o.limits.max_purchase_total=QuoteAtoms{1'000'000};o.limits.max_buy_debit=QuoteAtoms{1'000'000};
    o.limits.max_fee=QuoteAtoms{2'000};o.limits.max_buy_price_per_sol=QuoteAtoms{146'635'156};
    State initial;require(apply(initial,o,Role::operator_role,100)==Code::applied,"price_opening");
    initial.mode=Mode::halted;
    const AmendBuyPrice amendment{opening_digest(o),digest(99),QuoteAtoms{146'635'156},QuoteAtoms{147'537'213},0,1,1};
    auto s=initial;require(apply(s,amendment,Role::operator_role,101)==Code::applied,"price_amendment_accepted");
    require(s.opening==initial.opening && s.base==initial.base && s.quote==initial.quote && s.cost==initial.cost &&
        s.mode==Mode::halted && !s.last_effect && s.buy_price_amendment.price==QuoteAtoms{147'537'213} &&
        s.buy_price_amendment.evidence==digest(99),"price_amendment_preserves_funding");
    const auto amended=s;require(apply(s,amendment,Role::operator_role,102)==Code::stale && s==amended,"price_amendment_once");
    for(auto role:{Role::strategy,Role::gateway}) {s=initial;require(apply(s,amendment,role,101)==Code::unauthorized && s==initial,"price_amendment_operator_only");}
    for(unsigned n=0;n<12;++n) {
        s=initial;auto c=amendment;Code expected=Code::invalid;
        if(n==0){s.mode=Mode::normal;expected=Code::stopped;}
        if(n==1){s.last_effect=1;expected=Code::busy;}
        if(n==2){s.reserved_quote=QuoteAtoms{1};expected=Code::busy;}
        if(n==3){s.quote=QuoteAtoms{s.quote.value()-1};expected=Code::busy;}
        if(n==4){c.kill_epoch=2;expected=Code::fenced;}
        if(n==5){c.executor_generation=2;expected=Code::fenced;}
        if(n==6)c.opening_digest=digest(88);
        if(n==7){c.expected_price=QuoteAtoms{1};expected=Code::stale;}
        if(n==8){c.expected_revision=1;expected=Code::stale;}
        if(n==9)c.review_evidence={};
        if(n==10)c.replacement_price={};
        if(n==11)c.replacement_price=QuoteAtoms{UINT64_MAX};
        const char* names[]={"price_requires_halt","price_no_prior_effect","price_no_reserved_cash","price_balances_unchanged",
            "price_kill_fence","price_executor_fence","price_opening_pin","price_expected_price","price_expected_revision",
            "price_evidence_required","price_positive_replacement","price_replacement_no_overflow"};
        const auto before=s;require(apply(s,c,Role::operator_role,101)==expected && s==before,names[n]);
    }
    s=amended;s.mode=Mode::normal;
    Intent i;i.binding=o.binding;i.effect=EffectId{1};i.side=Side::buy;i.quote_input=QuoteAtoms{998'000};i.max_quote_debit=QuoteAtoms{1'000'000};
    i.fee_cap=QuoteAtoms{2'000};i.min_base_receive=BaseAtoms{6'777'951};i.source_time=101;i.sign_by=120;i.kill_epoch=1;i.executor_generation=1;
    auto old=initial;old.mode=Mode::normal;
    require(apply(old,i,Role::strategy,101)==Code::unsafe_bound,"price_old_bound_still_refuses");
    auto lower=amendment;lower.replacement_price=QuoteAtoms{147'537'212};auto boundary=initial;
    require(apply(boundary,lower,Role::operator_role,101)==Code::applied,"price_lower_amendment_valid");boundary.mode=Mode::normal;
    require(apply(boundary,i,Role::strategy,101)==Code::unsafe_bound,"price_one_micro_boundary");
    require(apply(s,i,Role::strategy,101)==Code::applied && s.reserved_quote==QuoteAtoms{1'000'000},"price_new_bound_accepts_exact_floor");
    require(apply(s,Prepare{i.effect,digest(5),digest(6),1},Role::gateway,102)==Code::applied &&
        apply(s,Attempt{i.effect,digest(5)},Role::gateway,103)==Code::applied,"price_prepare_and_attempt");
    Settlement x;x.effect=i.effect;x.custody=o.binding.custody;x.payload=digest(5);x.transaction=digest(7);x.evidence=digest(8);
    x.outcome=Outcome::succeeded;x.base_credit=BaseAtoms{6'777'951};x.quote_debit=QuoteAtoms{1'000'000};x.fee=QuoteAtoms{2'000};x.finalized=true;
    require(apply(s,x,Role::gateway,104)==Code::applied && valid_state(s),"price_settlement_reconstructs_with_amendment");
    for(const auto& checkpoint:{initial,amended,s}) {
        std::array<std::byte,max_state_bytes> bytes{};const auto encoded=encode_state(checkpoint,bytes);State restored;
        require(encoded.error==CodecError::none && decode_state(std::span(bytes).first(encoded.size),restored)==CodecError::none && restored==checkpoint,"price_snapshot_roundtrip");
    }
    for(unsigned n=0;n<6;++n) {
        auto corrupt=amended;
        if(n==0)corrupt.buy_price_amendment.price={};
        if(n==1)corrupt.buy_price_amendment.evidence={};
        if(n==2)corrupt.buy_price_amendment.event_time=102;
        if(n==3)corrupt.buy_price_amendment.kill_epoch=2;
        if(n==4)corrupt.buy_price_amendment.executor_generation=2;
        if(n==5)corrupt.buy_price_amendment.price=o.limits.max_buy_price_per_sol;
        require(!valid_state(corrupt),"price_invalid_amendment_state_refused");
    }
    std::array<std::byte,max_state_bytes> extended{};auto encoded_state=encode_state(amended,extended);State restored_state;
    require(encoded_state.error==CodecError::none && encoded_state.size==price_amended_state_bytes,"price_extension_exact_size");
    extended[legacy_state_bytes]=std::byte{3};
    require(decode_state(std::span(extended).first(encoded_state.size),restored_state)==CodecError::format,"price_unknown_extension_tag_refused");
    extended[legacy_state_bytes]=std::byte{1};
    require(decode_state(std::span(extended).first(encoded_state.size-1),restored_state)==CodecError::format,"price_partial_extension_refused");
    std::array<std::byte,max_record_bytes> bytes{};const Record record{Role::operator_role,101,amendment};
    const auto encoded=encode(record,bytes);Record restored;
    require(encoded.error==CodecError::none && decode(std::span(bytes).first(encoded.size),restored)==CodecError::none && restored==record,"price_command_roundtrip");
}
void funding_and_roles() {
    State s; const auto before = s;
    require(apply(s, opening(), Role::strategy, 100) == Code::unauthorized && s == before, "strategy cannot fund itself");
    require(apply(s, opening(), Role::operator_role, 100) == Code::applied, "baseline admitted");
    require(s.base == BaseAtoms{31'896'934} && s.quote == QuoteAtoms{5'000'000} && s.cost == QuoteAtoms{3'000'000}, "funded exact balances");
    const auto funded = s;
    require(apply(s, opening(), Role::operator_role, 100) == Code::already_initialized && s == funded, "cannot top up by repeating baseline");
}
void fractional_receipt_and_fee_accounting() {
    Rig r; r.start(r.buy());
    require(r.s.reserved_quote == QuoteAtoms{1'002'000}, "reserve fee-inclusive debit");
    const auto x = r.fill();
    require(apply(r.s, x, Role::gateway, 103) == Code::applied, "exact receipt accepted");
    require(r.s.base == BaseAtoms{40'997'057} && r.s.quote == QuoteAtoms{3'998'000}, "fractional lot preserved exactly");
    require(r.s.cost == QuoteAtoms{4'000'000} && r.s.realized_pnl == -2'000 && r.s.fees == QuoteAtoms{2'000}, "fee realized once outside cost basis");
    require(r.s.purchases == QuoteAtoms{1'002'000} && r.s.reserved_quote == QuoteAtoms{} && r.s.pending.phase == Phase::none, "only settled debit consumes purchase budget");
}
void insufficient_cash_and_inventory() {
    Rig r; r.s.quote = QuoteAtoms{1'001'999}; const auto before = r.s;
    require(apply(r.s, r.buy(), Role::strategy, 100) == Code::insufficient_funds && r.s == before, "one atom short including fee refuses atomically");
    r.s.quote = QuoteAtoms{5'000'000}; auto i = r.sell(1, 31'896'935); i.min_quote_receive = QuoteAtoms{1'600'000};
    require(apply(r.s, i, Role::strategy, 100) == Code::insufficient_funds && r.s.reserved_base == BaseAtoms{}, "sell cannot borrow one SOL atom");
}
void pending_unknown_keeps_holds() {
    Rig r; r.start(r.buy());
    require(apply(r.s, MarkUnknown{EffectId{1}}, Role::gateway, 130) == Code::applied, "unknown outcome retained after signing deadline");
    const auto before = r.s;
    auto next = r.buy(2); next.source_time = 130; next.sign_by = 150;
    require(apply(r.s, next, Role::strategy, 130) == Code::busy && r.s == before, "unknown blocks next purchase");
    require(apply(r.s, ReleaseUnsent{EffectId{1}, digest(9)}, Role::gateway, 131) == Code::bad_transition && r.s.reserved_quote == QuoteAtoms{1'002'000}, "timeout never releases possibly sent money");
    require(apply(r.s, r.fill(), Role::gateway, 132) == Code::applied, "late finalized settlement still books");
}
void one_atom_bounds_conflict_without_booking() {
    for (unsigned which = 0; which != 4; ++which) {
        Rig r; r.start(r.buy()); auto x = r.fill();
        if (which == 0) { x.quote_debit = QuoteAtoms{1'002'001}; x.fee = QuoteAtoms{2'001}; }
        if (which == 1) x.base_credit = BaseAtoms{8'999'999};
        if (which == 2) x.base_debit = BaseAtoms{1};
        if (which == 3) x.quote_credit = QuoteAtoms{1};
        require(apply(r.s, x, Role::gateway, 103) == Code::conflict, "out-of-bound finalized evidence conflicts");
        require(r.s.conflicted && r.s.base == BaseAtoms{31'896'934} && r.s.quote == QuoteAtoms{5'000'000} && r.s.reserved_quote == QuoteAtoms{1'002'000}, "conflict retains last reconciled balances and reservation");
    }
}
void identity_and_canonical_units() {
    Rig r;
    for (unsigned which = 0; which != 6; ++which) {
        auto i = r.buy();
        if (which == 0) i.binding.catalog = digest(99);
        if (which == 1) i.binding.custody = digest(99);
        if (which == 2) i.binding.policy_version = PolicyVersion{2};
        if (which == 3) i.base_input = BaseAtoms{1};
        if (which == 4) i.min_quote_receive = QuoteAtoms{1};
        if (which == 5) i.side = static_cast<Side>(255);
        const auto before = r.s;
        require(apply(r.s, i, Role::strategy, 100) != Code::applied && r.s == before, "wrong identity or mixed input units refused without mutation");
    }
}
void freshness_and_fencing() {
    Rig r; auto i = r.buy(); i.source_time = 89;
    require(apply(r.s, i, Role::strategy, 100) == Code::stale, "old market input rejected");
    i.source_time = 101;
    require(apply(r.s, i, Role::strategy, 100) == Code::stale, "future market input rejected");
    i = r.buy(); i.sign_by = 100;
    require(apply(r.s, i, Role::strategy, 100) == Code::expired, "expired authority rejected");
    i = r.buy(); i.sign_by = 121;
    require(apply(r.s, i, Role::strategy, 100) == Code::expired, "overlong authority rejected");
    i = r.buy(); i.kill_epoch = 2;
    require(apply(r.s, i, Role::strategy, 100) == Code::fenced, "wrong kill epoch rejected");
}
void safety_floor_and_fee_caps() {
    Rig r; auto i = r.buy(); i.min_base_receive = BaseAtoms{8'015'999};
    require(apply(r.s, i, Role::strategy, 100) == Code::unsafe_bound, "minimum receive must cover all-in 125 USDC per SOL ceiling");
    i.min_base_receive = BaseAtoms{8'016'000};
    require(apply(r.s, i, Role::strategy, 100) == Code::applied, "exact rational safety boundary accepted");
    Rig q; i = q.buy(); i.fee_cap = QuoteAtoms{10'001}; i.max_quote_debit = QuoteAtoms{1'010'001};
    require(apply(q.s, i, Role::strategy, 100) == Code::unsafe_bound, "strategy cannot widen configured fee cap");
}
void deterministic_replay_of_pending_state() {
    const auto o = opening(); Rig r; auto i = r.buy();
    const std::array<Command, 5> log{o, i, Prepare{i.effect,digest(5),digest(6),1}, Attempt{i.effect,digest(5)}, MarkUnknown{i.effect}};
    const std::array<Role,5> roles{Role::operator_role,Role::strategy,Role::gateway,Role::gateway,Role::gateway};
    State uninterrupted;
    for (std::size_t k=0;k<log.size();++k) {
        require(apply(uninterrupted,log[k],roles[k],100+k)==Code::applied,"committed command applies");
        State restored;
        for (std::size_t j=0;j<=k;++j) require(apply(restored,log[j],roles[j],100+j)==Code::applied,"replay applies");
        require(restored==uninterrupted,"every replay prefix reconstructs all holds and fences");
    }
    require(uninterrupted.pending.phase==Phase::unknown && uninterrupted.reserved_quote==QuoteAtoms{1'002'000},"restarted unknown still holds funds");
}
void duplicate_and_contradictory_settlement() {
    Rig r; r.start(r.buy()); const auto x=r.fill();
    require(apply(r.s,x,Role::gateway,103)==Code::applied,"first settlement books");
    const auto before=r.s;
    require(apply(r.s,x,Role::gateway,104)==Code::duplicate && r.s==before,"duplicate cannot double book");
    auto changed=x; changed.base_credit=BaseAtoms{9'100'124};
    require(apply(r.s,changed,Role::gateway,105)==Code::conflict && r.s.conflicted && r.s.base==before.base,"contradictory duplicate freezes without changing balances");
}
void role_checks_and_finality() {
    Rig r; r.start(r.buy()); auto x=r.fill(); const auto before=r.s;
    require(apply(r.s,x,Role::strategy,103)==Code::unauthorized && r.s==before,"strategy cannot claim fills");
    x.finalized=false;
    require(apply(r.s,x,Role::gateway,103)==Code::invalid && r.s==before,"unfinalized fill retains holds");
    x=r.fill(); x.custody=digest(99);
    require(apply(r.s,x,Role::gateway,103)==Code::conflict && r.s.reserved_quote==before.reserved_quote,"wrong recipient evidence cannot book");
}
void prepare_and_attempt_are_one_use() {
    Rig r; auto i=r.buy(); require(apply(r.s,i,Role::strategy,100)==Code::applied,"reserve");
    require(apply(r.s,Prepare{i.effect,digest(5),digest(6),2},Role::gateway,101)==Code::invalid,"multiple payloads refused");
    require(apply(r.s,Prepare{i.effect,digest(5),digest(6),1},Role::gateway,101)==Code::applied,"single payload bound");
    require(apply(r.s,Attempt{i.effect,digest(99)},Role::gateway,102)==Code::invalid,"wrong bytes refused");
    require(apply(r.s,Attempt{i.effect,digest(5)},Role::gateway,102)==Code::applied,"first attempt admitted");
    require(apply(r.s,Attempt{i.effect,digest(5)},Role::gateway,103)==Code::bad_transition,"second attempt has no send authority");
}
void kill_blocks_new_attempt_not_settlement() {
    Rig r; r.start(r.buy());
    require(apply(r.s,Control{Mode::halted,2,1},Role::operator_role,103)==Code::applied,"operator kill applies");
    require(apply(r.s,r.fill(),Role::gateway,104)==Code::applied,"kill does not erase settled economic outcome");
    Rig p; const auto i=p.buy(); require(apply(p.s,i,Role::strategy,100)==Code::applied,"reserve");
    require(apply(p.s,Control{Mode::halted,2,1},Role::operator_role,101)==Code::applied,"kill before prepare");
    require(apply(p.s,Prepare{i.effect,digest(5),digest(6),1},Role::gateway,102)==Code::fenced,"old generation cannot prepare after kill");
    require(apply(p.s,ReleaseUnsent{i.effect,digest(9)},Role::gateway,103)==Code::applied,"definitely unattempted paper intent can release");
}
void sell_cost_basis_and_full_close_remainder() {
    Rig r; r.s.base=BaseAtoms{3}; r.s.cost=QuoteAtoms{10}; r.s.opening.limits.min_sell_price_per_sol=QuoteAtoms{1};
    auto i=r.sell(1,1); i.fee_cap=QuoteAtoms{1}; r.start(i);
    auto x=r.fill(); x.base_credit={}; x.base_debit=BaseAtoms{1}; x.quote_debit={}; x.quote_credit=QuoteAtoms{4}; x.fee=QuoteAtoms{1};
    require(apply(r.s,x,Role::gateway,103)==Code::applied,"sell partial atom position");
    require(r.s.cost==QuoteAtoms{7} && r.s.realized_pnl==1 && r.s.quote==QuoteAtoms{5'000'004},"floor 10/3 cost removed, net credit already excludes fee");
    i=r.sell(2,2); i.source_time=104; i.sign_by=120; i.fee_cap=QuoteAtoms{1};
    require(apply(r.s,i,Role::strategy,104)==Code::applied,"reserve close");
    require(apply(r.s,Prepare{i.effect,digest(5),digest(6),1},Role::gateway,105)==Code::applied,"prepare close");
    require(apply(r.s,Attempt{i.effect,digest(5)},Role::gateway,106)==Code::applied,"attempt close");
    x.effect=i.effect; x.transaction=digest(10); x.base_debit=BaseAtoms{2}; x.quote_credit=QuoteAtoms{8};
    require(apply(r.s,x,Role::gateway,107)==Code::applied,"settle close");
    require(r.s.base==BaseAtoms{} && r.s.cost==QuoteAtoms{} && r.s.realized_pnl==2 && r.s.fees==QuoteAtoms{2},"full close removes all cost rounding remainder");
}
void sales_do_not_replenish_purchase_allowance() {
    Rig r; r.s.purchases=QuoteAtoms{4'500'000}; auto i=r.sell(1,20'000'000); i.min_quote_receive=QuoteAtoms{1'000'000}; r.start(i);
    auto x=r.fill(); x.base_credit={}; x.base_debit=BaseAtoms{20'000'000}; x.quote_debit={}; x.quote_credit=QuoteAtoms{2'000'000};
    require(apply(r.s,x,Role::gateway,103)==Code::applied,"sell adds cash");
    require(r.s.quote==QuoteAtoms{7'000'000} && r.s.purchases==QuoteAtoms{4'500'000},"sale leaves lifetime purchase spend intact");
    auto b=r.buy(2); b.source_time=104;
    require(apply(r.s,b,Role::strategy,104)==Code::purchase_cap,"cash from sale cannot buy past cumulative cap");
}
void loss_stop_allows_only_sales() {
    Rig r; r.s.realized_pnl=-500'000;
    require(apply(r.s,r.buy(),Role::strategy,100)==Code::stopped,"loss at boundary stops buys");
    auto i=r.sell(1,20'000'000); i.min_quote_receive=QuoteAtoms{1'000'000};
    require(apply(r.s,i,Role::strategy,100)==Code::applied,"loss stop still permits funded reducing sell");
}
void transient_liquidity_is_reserved() {
    Rig r; auto i=r.buy(); i.transient_base_hold=BaseAtoms{2'039'280};
    require(apply(r.s,i,Role::strategy,100)==Code::applied && r.s.reserved_base==BaseAtoms{2'039'280},"temporary SOL liquidity is held separately");
    Rig q; q.s.base=BaseAtoms{2'039'279};
    require(apply(q.s,i,Role::strategy,100)==Code::insufficient_funds,"eventual refund cannot fund an initial shortfall");
}
void paper_oracle_and_reject() {
    Rig r; r.start(r.buy());
    auto p=simulate(r.s,{QuoteAtoms{100'000'000},2'000},digest(7),digest(8));
    require(p.code==Code::applied && p.settlement.base_credit==BaseAtoms{10'000'000} && p.settlement.quote_debit==QuoteAtoms{1'002'000},"1 USDC at 100 USD per SOL produces 0.01 SOL, fee 0.002 USDC");
    require(apply(r.s,p.settlement,Role::gateway,103)==Code::applied,"paper uses same settlement reducer");
    Rig q; q.start(q.buy()); p=simulate(q.s,{QuoteAtoms{125'000'000},2'000},digest(7),digest(8));
    require(p.code==Code::applied && p.settlement.outcome==Outcome::failed_without_charge,"slippage bound yields sponsored paper failure");
    require(apply(q.s,p.settlement,Role::gateway,103)==Code::applied && q.s.quote==QuoteAtoms{5'000'000} && q.s.purchases==QuoteAtoms{},"failure releases only after terminal evidence");
}
Intent observed_cost_buy(const Rig& r) {
    auto i=r.buy();
    i.quote_input=QuoteAtoms{998'000}; i.max_quote_debit=QuoteAtoms{1'000'000};
    i.min_base_receive=BaseAtoms{9'429'160};
    return i;
}
PaperQuote observed_cost_quote() {
    return {QuoteAtoms{100'000'000},0,BaseAtoms{550'840},QuoteAtoms{2'000},true};
}
void paper_observed_cost_oracle_and_full_close() {
    Rig r; r.s={}; auto o=opening(); o.base={}; o.base_cost={};
    require(apply(r.s,o,Role::operator_role,100)==Code::applied,"empty inventory initializes");
    r.start(observed_cost_buy(r)); const auto before=r.s;
    const auto p=simulate(r.s,observed_cost_quote(),digest(7),digest(8));
    require(p.code==Code::applied && p.settlement.outcome==Outcome::succeeded &&
            p.settlement.base_credit==BaseAtoms{9'429'160} &&
            p.settlement.quote_debit==QuoteAtoms{1'000'000} && p.settlement.fee==QuoteAtoms{2'000},
            "998000 principal buys 9980000 atoms minus 550840, exact fee makes total 1000000");
    require(r.s==before,"paper quote calculation cannot mutate reservations or account");
    require(apply(r.s,p.settlement,Role::gateway,103)==Code::applied,"net receipt books through unchanged reducer");
    require(r.s.base==BaseAtoms{9'429'160} && r.s.quote==QuoteAtoms{4'000'000} &&
            r.s.cost==QuoteAtoms{998'000} && r.s.fees==QuoteAtoms{2'000} &&
            r.s.realized_pnl==-2'000 && r.s.purchases==QuoteAtoms{1'000'000},
            "deducted inventory retains full principal cost and explicit fee is realized once");
    auto i=r.sell(2,9'429'160); i.source_time=104; i.min_quote_receive=QuoteAtoms{942'916};
    require(apply(r.s,i,Role::strategy,104)==Code::applied,"full net inventory reserved");
    require(apply(r.s,Prepare{i.effect,digest(5),digest(6),1},Role::gateway,105)==Code::applied,"full close prepared");
    require(apply(r.s,Attempt{i.effect,digest(5)},Role::gateway,106)==Code::applied,"full close attempted");
    const auto close=simulate(r.s,observed_cost_quote(),digest(9),digest(10));
    require(close.settlement.quote_credit==QuoteAtoms{942'916} && close.settlement.fee==QuoteAtoms{},
            "buy deduction and exact buy fee do not apply to a sale");
    require(apply(r.s,close.settlement,Role::gateway,107)==Code::applied,"full close settles");
    require(r.s.base==BaseAtoms{} && r.s.cost==QuoteAtoms{} && r.s.quote==QuoteAtoms{4'942'916} &&
            r.s.realized_pnl==-57'084 && r.s.fees==QuoteAtoms{2'000} && valid_state(r.s),
            "round trip realizes 55084 receipt loss plus 2000 fee exactly once");
}
void paper_observed_cost_boundaries() {
    for(unsigned which=0;which!=6;++which) {
        Rig r; auto i=observed_cost_buy(r); auto q=observed_cost_quote();
        if(which==0) i.min_base_receive=BaseAtoms{9'429'161};
        if(which==1) q.buy_base_deduction=BaseAtoms{9'980'000};
        if(which==2) q.buy_base_deduction=BaseAtoms{9'980'001};
        if(which==3) q.buy_base_deduction=BaseAtoms{std::numeric_limits<std::uint64_t>::max()};
        if(which==4) q.exact_buy_fee=QuoteAtoms{2'001};
        if(which==5) q.exact_buy_fee=QuoteAtoms{std::numeric_limits<std::uint64_t>::max()};
        r.start(i); const auto before=r.s; const auto p=simulate(r.s,q,digest(7),digest(8));
        require(p.code==Code::applied && p.settlement.outcome==Outcome::failed_without_charge,
                "net minimum, deduction underflow and exact fee cap fail without charge");
        require(r.s==before && !p.settlement.base_credit.value() && !p.settlement.base_debit.value() &&
                !p.settlement.quote_credit.value() && !p.settlement.quote_debit.value() && !p.settlement.fee.value(),
                "failed paper fills carry zero economic deltas and cannot mutate source state");
        require(apply(r.s,p.settlement,Role::gateway,103)==Code::applied && r.s.base==before.base &&
                r.s.quote==before.quote && r.s.cost==before.cost && r.s.realized_pnl==before.realized_pnl &&
                r.s.fees==before.fees && r.s.purchases==before.purchases && !r.s.reserved_quote.value() &&
                r.s.pending.phase==Phase::none && valid_state(r.s),
                "failed cost-aware fills release holds without booking cost or inventory");
    }
}
void paper_exact_fee_mode_and_unchanged_sell() {
    Rig r; r.start(observed_cost_buy(r)); auto q=observed_cost_quote(); q.fee_ppm=2'000;
    const auto before=r.s; const auto p=simulate(r.s,q,digest(7),digest(8));
    require(p.code==Code::invalid && r.s==before,"ambiguous exact and rate buy fees are refused");
    q.use_exact_buy_fee=false;
    const auto rate=simulate(r.s,q,digest(7),digest(8));
    require(rate.code==Code::applied && rate.settlement.outcome==Outcome::succeeded &&
            rate.settlement.fee==QuoteAtoms{1'996} && rate.settlement.quote_debit==QuoteAtoms{999'996} &&
            rate.settlement.base_credit==BaseAtoms{9'429'160},
            "2000 ppm of 998000 principal is 1996 and does not imply the observed exact 2000 fee");
    q=observed_cost_quote(); q.exact_buy_fee={};
    const auto zero=simulate(r.s,q,digest(7),digest(8));
    require(zero.code==Code::applied && zero.settlement.outcome==Outcome::succeeded &&
            zero.settlement.fee==QuoteAtoms{} && zero.settlement.quote_debit==QuoteAtoms{998'000},
            "explicit zero quote fee is supported without an amount sentinel");
    Rig s; auto i=s.sell(1,10'000'000); i.min_quote_receive=QuoteAtoms{998'000}; s.start(i);
    const auto normal=simulate(s.s,{QuoteAtoms{100'000'000},2'000},digest(7),digest(8));
    q.fee_ppm=2'000;
    q.buy_base_deduction=BaseAtoms{std::numeric_limits<std::uint64_t>::max()};
    q.exact_buy_fee=QuoteAtoms{std::numeric_limits<std::uint64_t>::max()};
    const auto with_buy_costs=simulate(s.s,q,digest(7),digest(8));
    require(normal.code==Code::applied && normal.settlement.outcome==Outcome::succeeded &&
            normal.settlement.quote_credit==QuoteAtoms{998'000} && normal.settlement.fee==QuoteAtoms{2'000} &&
            with_buy_costs.code==normal.code && with_buy_costs.settlement==normal.settlement,
            "sell rate fee remains unchanged by all buy-only quote fields");
}
void overflow_is_transactional() {
    Rig r; r.s.base=BaseAtoms{std::numeric_limits<std::uint64_t>::max()-1}; r.start(r.buy()); const auto before=r.s;
    require(apply(r.s,r.fill(),Role::gateway,103)==Code::conflict && r.s.base==before.base && r.s.reserved_quote==before.reserved_quote,"unrepresentable settlement freezes and retains evidence authority");
    Rig q; auto i=q.buy(); i.quote_input=QuoteAtoms{std::numeric_limits<std::uint64_t>::max()};
    const auto original=q.s;
    require(apply(q.s,i,Role::strategy,100)!=Code::applied && q.s==original,"input overflow cannot wrap into admission");
}
void failed_transaction_cannot_hide_charges() {
    Rig r; r.start(r.buy()); auto x=r.fill(); x.outcome=Outcome::failed_without_charge;
    x.base_credit={}; x.quote_debit=QuoteAtoms{1}; x.fee=QuoteAtoms{1};
    require(apply(r.s,x,Role::gateway,103)==Code::conflict && r.s.reserved_quote==QuoteAtoms{1'002'000},"unproven sponsorship cannot silently discard charged failure");
}
Settlement settle_failed(Rig& r,std::uint64_t id,std::uint64_t now) {
    auto i=r.buy(id);i.source_time=now;i.sign_by=now+20;
    require(apply(r.s,i,Role::strategy,now)==Code::applied,"window case reserved");
    require(apply(r.s,Prepare{i.effect,digest(5),digest(6),1},Role::gateway,now+1)==Code::applied,"window case prepared");
    require(apply(r.s,Attempt{i.effect,digest(5)},Role::gateway,now+2)==Code::applied,"window case attempted");
    auto x=r.fill();x.outcome=Outcome::failed_without_charge;x.base_credit={};x.quote_debit={};x.fee={};
    x.transaction=digest(0);x.transaction.bytes[1]=static_cast<std::byte>(id);x.transaction.bytes[2]=static_cast<std::byte>(id>>8);
    require(apply(r.s,x,Role::gateway,now+3)==Code::applied,"window case settled");
    return x;
}
void settlement_window_keeps_recent_and_counts_lifetime() {
    Rig r;Settlement first,recent;
    for(std::uint64_t id=1;id<=40;++id){const auto x=settle_failed(r,id,100+id*4);if(id==1)first=x;recent=x;}
    require(r.s.settlement_count==16 && r.s.settled_total==40 && r.s.last_effect==40,"window holds sixteen and counts every settlement");
    for(std::size_t n=0;n<16;++n)require(r.s.settlements[n].effect.value()==25+n,"window keeps the most recent effects in order");
    require(valid_state(r.s),"evicted history is a valid state");
    std::array<std::byte,max_state_bytes> bytes{};const auto saved=encode_state(r.s,bytes);State restored;
    require(saved.error==CodecError::none && decode_state(std::span(bytes).first(saved.size),restored)==CodecError::none && restored==r.s,
        "window and lifetime count survive the snapshot codec");
    const auto before=r.s;
    require(apply(r.s,recent,Role::gateway,300)==Code::duplicate && r.s==before,"latest settlement replay stays idempotent");
    require(apply(r.s,first,Role::gateway,300)!=Code::applied && r.s==before,"evicted settlement replay changes nothing");
    auto i=r.buy(41);i.source_time=300;i.sign_by=320;require(apply(r.s,i,Role::strategy,300)==Code::applied,"a full window never stops the next effect");
}
void settlement_window_state_is_validated() {
    Rig r;for(std::uint64_t id=1;id<=20;++id)(void)settle_failed(r,id,100+id*4);
    require(valid_state(r.s),"evicted fixture valid");
    auto fewer=r.s;fewer.settled_total=15;require(!valid_state(fewer),"lifetime count cannot be below the window");
    auto partial=r.s;partial.settlements[15]={};partial.settlement_count=15;require(!valid_state(partial),"an evicted history keeps a full window");
    auto order=r.s;std::swap(order.settlements[3],order.settlements[4]);require(!valid_state(order),"window effects increase strictly");
    auto future=r.s;future.settlements[15].effect=EffectId{21};require(!valid_state(future),"window cannot run ahead of the last effect");
    auto repeat=r.s;repeat.settlements[2].transaction=repeat.settlements[1].transaction;require(!valid_state(repeat),"one transaction settles once in the window");
    auto shape=r.s;shape.settlements[7].outcome=Outcome::succeeded;require(!valid_state(shape),"an evicted success must be buy- or sell-shaped");
    auto both=r.s;auto& mixed=both.settlements[7];mixed.outcome=Outcome::succeeded;mixed.base_credit=BaseAtoms{5};mixed.quote_debit=QuoteAtoms{3'000};
    mixed.fee=QuoteAtoms{1'000};mixed.base_debit=BaseAtoms{1};require(!valid_state(both),"an evicted success cannot both buy and sell");
    auto good=both;good.settlements[7].base_debit={};require(valid_state(good),"an evicted buy-shaped success is valid");
    auto empty=r.s;empty.base={};require(!valid_state(empty),"cost basis requires inventory after eviction");
    auto spent=r.s;spent.purchases=QuoteAtoms{spent.opening.limits.max_purchase_total.value()+1};require(!valid_state(spent),"purchases stay within the opening cap");
    Rig small;for(std::uint64_t id=1;id<=3;++id)(void)settle_failed(small,id,100+id*4);
    auto lie=small.s;lie.settled_total=4;require(!valid_state(lie),"a complete history must equal its lifetime count");
}
void strategy_orders_match_the_tm_route_shape() {
    const auto b=opening().binding;
    const auto buy=strategy_buy(b,EffectId{7},QuoteAtoms{13'200'000},BaseAtoms{110'400'000},500,510,1,1);
    require(buy && buy->side==Side::buy && buy->effect==EffectId{7} && buy->max_quote_debit==QuoteAtoms{13'200'000} &&
        buy->fee_cap==QuoteAtoms{26'400} && buy->quote_input==QuoteAtoms{13'173'600} && buy->min_base_receive==BaseAtoms{110'400'000} &&
        buy->binding==b && buy->source_time==500 && buy->sign_by==510 && buy->kill_epoch==1 && buy->executor_generation==1 &&
        !buy->base_input.value() && !buy->min_quote_receive.value() && !buy->transient_base_hold.value(),"buy is fee-inclusive whole cents");
    require(!strategy_buy(b,EffectId{7},QuoteAtoms{13'200'001},BaseAtoms{1},500,510,1,1),"buy debit must be whole cents");
    require(!strategy_buy(b,EffectId{7},QuoteAtoms{},BaseAtoms{1},500,510,1,1) && !strategy_buy(b,EffectId{7},QuoteAtoms{10'000},BaseAtoms{},500,510,1,1),
        "buy needs debit and floor");
    require(!strategy_buy(b,EffectId{},QuoteAtoms{10'000},BaseAtoms{1},500,510,1,1) && !strategy_buy(b,EffectId{7},QuoteAtoms{10'000},BaseAtoms{1},510,510,1,1),
        "buy needs an effect and a later sign-by");
    const auto sell=strategy_sell(b,EffectId{8},BaseAtoms{120'000'000},QuoteAtoms{12'437'500},QuoteAtoms{25'051},600,610,1,1);
    require(sell && sell->side==Side::sell && sell->base_input==BaseAtoms{120'000'000} && sell->min_quote_receive==QuoteAtoms{12'437'500} &&
        sell->fee_cap==QuoteAtoms{25'051} && !sell->quote_input.value() && !sell->max_quote_debit.value() && !sell->min_base_receive.value(),
        "sell carries base input, USDC floor and fee cap only");
    require(!strategy_sell(b,EffectId{8},BaseAtoms{},QuoteAtoms{1},QuoteAtoms{1},600,610,1,1) &&
        !strategy_sell(b,EffectId{8},BaseAtoms{1},QuoteAtoms{},QuoteAtoms{1},600,610,1,1) &&
        !strategy_sell(b,EffectId{8},BaseAtoms{1},QuoteAtoms{1},QuoteAtoms{},600,610,1,1),"sell needs input, floor and fee cap");
    Rig r;auto i=*strategy_buy(r.s.opening.binding,EffectId{1},QuoteAtoms{1'000'000},BaseAtoms{9'000'000},100,120,1,1);
    require(apply(r.s,i,Role::strategy,100)==Code::applied,"strategy buy is admitted by the reducer");
}
void record_codec_has_independent_golden_and_rejects_corruption() {
    Record record{Role::gateway,100,MarkUnknown{EffectId{0x0102030405060708}}};
    std::array<std::byte,max_record_bytes> bytes{};const auto encoded=encode(record,bytes);
    require(encoded.error==CodecError::none && encoded.size==60,"MarkUnknown canonical record has 60 bytes");
    constexpr std::string_view expected="56535031010005033c00000064000000000000000807060504030201ffd1d77a5c03e2e023acfe76b281607e21ec40962bf6cfb9f47986fb433c0ef1";
    constexpr char hex[]="0123456789abcdef";
    for(std::size_t n=0;n<encoded.size;++n) {
        const auto v=std::to_integer<unsigned>(bytes[n]);
        require(expected[2*n]==hex[v>>4] && expected[2*n+1]==hex[v&15],"canonical bytes match independent Python struct/hashlib oracle");
    }
    Record out;require(decode(std::span(bytes).first(encoded.size),out)==CodecError::none && out==record,"golden decoded fields match");
    const auto previous=out;
    for(std::size_t n=0;n<encoded.size;++n) {
        bytes[n]^=std::byte{1};
        require(decode(std::span(bytes).first(encoded.size),out)!=CodecError::none && out==previous,"single byte corruption rejects without destination mutation");
        bytes[n]^=std::byte{1};
    }
    for(std::size_t n=0;n<encoded.size;++n)
        require(decode(std::span(bytes).first(n),out)!=CodecError::none && out==previous,"every truncated record rejects");
    require(encode(record,std::span(bytes).first(59)).error==CodecError::short_buffer,"short output never reports a usable record");
}
void all_command_records_replay_exactly() {
    Rig rig;const auto i=rig.buy();rig.start(i);const auto x=rig.fill();
    const std::array<Record,9> records{{
        {Role::operator_role,100,opening()},{Role::strategy,100,i},
        {Role::gateway,101,Prepare{i.effect,digest(5),digest(6),1}},
        {Role::gateway,102,Attempt{i.effect,digest(5)}},{Role::gateway,103,MarkUnknown{i.effect}},
        {Role::gateway,104,x},{Role::operator_role,105,Control{Mode::halted,2,1}},
        {Role::gateway,106,ReleaseUnsent{EffectId{99},digest(9)}},
        {Role::gateway,107,x}}};
    State original,restored;
    for(const auto& record:records) {
        std::array<std::byte,max_record_bytes> bytes{};const auto packed=encode(record,bytes);
        require(packed.error==CodecError::none,"every command fits bounded record");
        Record unpacked;require(decode(std::span(bytes).first(packed.size),unpacked)==CodecError::none && unpacked==record,"all command fields survive canonical encoding");
        const auto left=apply(original,record.command,record.role,record.event_time);
        const auto right=apply(restored,unpacked.command,unpacked.role,unpacked.event_time);
        require(left==right && original==restored,"replay preserves refusals as well as economic transitions");
        std::array<std::byte,max_state_bytes> checkpoint{};
        const auto saved=encode_state(original,checkpoint);
        require(saved.error==CodecError::none && saved.size==legacy_state_bytes,"valid state fits the exact compact snapshot budget");
        State from_checkpoint;
        require(decode_state(std::span(checkpoint).first(saved.size),from_checkpoint)==CodecError::none && from_checkpoint==original,"snapshot restores all accounting and reservations");
        auto bad=original;bad.quote=QuoteAtoms{original.quote.value()+1};
        require(!valid_state(bad),"snapshot cannot invent one funded cash atom");
    }
}
void sell_fee_and_transaction_reuse_conflict() {
    Rig r;auto i=r.sell(1,20'000'000);i.min_quote_receive=QuoteAtoms{1'000'000};r.start(i);
    auto x=r.fill();x.base_credit={};x.base_debit=i.base_input;x.quote_debit={};x.quote_credit=QuoteAtoms{2'000'000};x.fee=QuoteAtoms{2'001};
    require(apply(r.s,x,Role::gateway,103)==Code::conflict && r.s.reserved_base==i.base_input,"sell fee one atom above authorization keeps inventory held");
    Rig q;q.start(q.buy());const auto first=q.fill();require(apply(q.s,first,Role::gateway,103)==Code::applied,"first transaction booked");
    auto next=q.buy(2);next.source_time=104;
    require(apply(q.s,next,Role::strategy,104)==Code::applied,"new effect reserved");
    require(apply(q.s,Prepare{next.effect,digest(5),digest(6),1},Role::gateway,105)==Code::applied,"prepare second effect");
    require(apply(q.s,Attempt{next.effect,digest(5)},Role::gateway,106)==Code::applied,"attempt second effect");
    auto repeated=first;repeated.effect=next.effect;
    require(apply(q.s,repeated,Role::gateway,107)==Code::conflict && q.s.purchases==QuoteAtoms{1'002'000},"same transaction cannot fund two effects");
}
void conservation_across_varied_prices() {
    auto o=opening();o.base={};o.base_cost={};o.quote=QuoteAtoms{10'000'000};o.limits.max_purchase_total=QuoteAtoms{10'000'000};
    State s;require(apply(s,o,Role::operator_role,100)==Code::applied,"conservation baseline");
    for(unsigned n=0;n<16;++n) {
        const auto now=101+n*4;Rig fixture;auto i=fixture.buy(n+1);i.binding=o.binding;i.source_time=now;i.sign_by=now+20;
        if(n%2) {i.side=Side::sell;i.base_input=s.base;i.quote_input={};i.max_quote_debit={};i.min_base_receive={};i.min_quote_receive=QuoteAtoms{500'000};i.fee_cap=QuoteAtoms{3'000};}
        require(apply(s,i,Role::strategy,now)==Code::applied,"varied-price intent accepted");
        require(apply(s,Prepare{i.effect,digest(5),digest(6),1},Role::gateway,now+1)==Code::applied,"varied-price prepare");
        require(apply(s,Attempt{i.effect,digest(5)},Role::gateway,now+2)==Code::applied,"varied-price attempt");
        const auto price=100'000'000+(n*7919)%10'000'000;
        const auto x=simulate(s,{QuoteAtoms{price},2'000},digest(40+n),digest(70+n));
        require(x.code==Code::applied && x.settlement.outcome==Outcome::succeeded,"varied-price paper fill");
        require(apply(s,x.settlement,Role::gateway,now+3)==Code::applied,"varied-price settlement");
        require(static_cast<std::int64_t>(s.quote.value()+s.cost.value())==10'000'000+s.realized_pnl,"funding plus realized pnl equals cash plus remaining cost basis");
        require(s.reserved_quote==QuoteAtoms{} && s.reserved_base==BaseAtoms{},"terminal fills release all holds");
    }
    require(s.base==BaseAtoms{} && s.cost==QuoteAtoms{} && s.purchases==QuoteAtoms{8'016'000},"eight varied round trips preserve total purchase debit");
}
}
int main(int argc,char** argv) {
    const std::pair<const char*,void(*)()> cases[]={
        {"buy_price_amendment",buy_price_amendment},{"funding_and_roles",funding_and_roles},{"fractional_receipt_and_fee_accounting",fractional_receipt_and_fee_accounting},
        {"insufficient_cash_and_inventory",insufficient_cash_and_inventory},{"pending_unknown_keeps_holds",pending_unknown_keeps_holds},
        {"one_atom_bounds_conflict_without_booking",one_atom_bounds_conflict_without_booking},{"identity_and_canonical_units",identity_and_canonical_units},
        {"freshness_and_fencing",freshness_and_fencing},{"safety_floor_and_fee_caps",safety_floor_and_fee_caps},
        {"deterministic_replay_of_pending_state",deterministic_replay_of_pending_state},{"duplicate_and_contradictory_settlement",duplicate_and_contradictory_settlement},
        {"role_checks_and_finality",role_checks_and_finality},{"prepare_and_attempt_are_one_use",prepare_and_attempt_are_one_use},
        {"kill_blocks_new_attempt_not_settlement",kill_blocks_new_attempt_not_settlement},{"sell_cost_basis_and_full_close_remainder",sell_cost_basis_and_full_close_remainder},
        {"sales_do_not_replenish_purchase_allowance",sales_do_not_replenish_purchase_allowance},{"loss_stop_allows_only_sales",loss_stop_allows_only_sales},
        {"transient_liquidity_is_reserved",transient_liquidity_is_reserved},{"paper_oracle_and_reject",paper_oracle_and_reject},
        {"paper_observed_cost_oracle_and_full_close",paper_observed_cost_oracle_and_full_close},
        {"paper_observed_cost_boundaries",paper_observed_cost_boundaries},
        {"paper_exact_fee_mode_and_unchanged_sell",paper_exact_fee_mode_and_unchanged_sell},
        {"overflow_is_transactional",overflow_is_transactional},{"failed_transaction_cannot_hide_charges",failed_transaction_cannot_hide_charges},
        {"settlement_window_keeps_recent_and_counts_lifetime",settlement_window_keeps_recent_and_counts_lifetime},{"settlement_window_state_is_validated",settlement_window_state_is_validated},
        {"strategy_orders_match_the_tm_route_shape",strategy_orders_match_the_tm_route_shape},{"record_codec_has_independent_golden_and_rejects_corruption",record_codec_has_independent_golden_and_rejects_corruption},
        {"all_command_records_replay_exactly",all_command_records_replay_exactly},
        {"sell_fee_and_transaction_reuse_conflict",sell_fee_and_transaction_reuse_conflict},
        {"conservation_across_varied_prices",conservation_across_varied_prices}};
    unsigned ran=0,failed=0;
    for (const auto& [name,test]:cases) {
        if(argc==2 && std::string_view(argv[1])!=name) continue;
        ++ran;
        try { test(); std::cout<<"PASS "<<name<<'\n'; }
        catch(const std::exception& e) {++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    }
    return ran && !failed ? 0 : 1;
}
