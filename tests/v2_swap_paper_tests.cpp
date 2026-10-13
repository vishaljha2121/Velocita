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
void bounded_settlement_history_never_evicts() {
    Rig r; Settlement first;
    for(std::uint64_t id=1;id<=16;++id) {
        const auto now=100+id*4;auto i=r.buy(id);i.source_time=now;i.sign_by=now+20;
        require(apply(r.s,i,Role::strategy,now)==Code::applied,"capacity reserved before send");
        require(apply(r.s,Prepare{i.effect,digest(5),digest(6),1},Role::gateway,now+1)==Code::applied,"prepare history case");
        require(apply(r.s,Attempt{i.effect,digest(5)},Role::gateway,now+2)==Code::applied,"attempt history case");
        auto x=r.fill();x.outcome=Outcome::failed_without_charge;x.base_credit={};x.quote_debit={};x.fee={};x.transaction=digest(20+id);
        require(apply(r.s,x,Role::gateway,now+3)==Code::applied,"terminal failure retained");
        if(id==1) first=x;
    }
    const auto before=r.s;
    require(apply(r.s,first,Role::gateway,200)==Code::duplicate && r.s==before,"old nonadjacent settlement is still idempotent");
    auto i=r.buy(17);i.source_time=200;i.sign_by=220;
    require(apply(r.s,i,Role::strategy,200)==Code::capacity && r.s==before,"full history stops before reserving or sending");
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
        require(saved.error==CodecError::none && saved.size==max_state_bytes,"valid state fits the exact compact snapshot budget");
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
        {"funding_and_roles",funding_and_roles},{"fractional_receipt_and_fee_accounting",fractional_receipt_and_fee_accounting},
        {"insufficient_cash_and_inventory",insufficient_cash_and_inventory},{"pending_unknown_keeps_holds",pending_unknown_keeps_holds},
        {"one_atom_bounds_conflict_without_booking",one_atom_bounds_conflict_without_booking},{"identity_and_canonical_units",identity_and_canonical_units},
        {"freshness_and_fencing",freshness_and_fencing},{"safety_floor_and_fee_caps",safety_floor_and_fee_caps},
        {"deterministic_replay_of_pending_state",deterministic_replay_of_pending_state},{"duplicate_and_contradictory_settlement",duplicate_and_contradictory_settlement},
        {"role_checks_and_finality",role_checks_and_finality},{"prepare_and_attempt_are_one_use",prepare_and_attempt_are_one_use},
        {"kill_blocks_new_attempt_not_settlement",kill_blocks_new_attempt_not_settlement},{"sell_cost_basis_and_full_close_remainder",sell_cost_basis_and_full_close_remainder},
        {"sales_do_not_replenish_purchase_allowance",sales_do_not_replenish_purchase_allowance},{"loss_stop_allows_only_sales",loss_stop_allows_only_sales},
        {"transient_liquidity_is_reserved",transient_liquidity_is_reserved},{"paper_oracle_and_reject",paper_oracle_and_reject},
        {"overflow_is_transactional",overflow_is_transactional},{"failed_transaction_cannot_hide_charges",failed_transaction_cannot_hide_charges},
        {"bounded_settlement_history_never_evicts",bounded_settlement_history_never_evicts},
        {"record_codec_has_independent_golden_and_rejects_corruption",record_codec_has_independent_golden_and_rejects_corruption},
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
