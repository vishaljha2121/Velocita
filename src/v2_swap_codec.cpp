#include "velocita/v2/swap_codec.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <bit>

namespace velocita::v2::swap {
namespace {
struct Writer {
    std::span<std::byte> buffer;
    std::size_t at{};
    bool good{true};
    void number(std::uint64_t v, unsigned size = 8) noexcept {
        for (unsigned n=0;n<size;++n) {
            if (at < buffer.size()) buffer[at] = static_cast<std::byte>(v >> (n*8));
            else good = false;
            ++at;
        }
    }
    void digest(const Digest256& d) noexcept { for (auto b:d.bytes) number(std::to_integer<unsigned>(b),1); }
    template<typename T> void atom(T value) noexcept { number(value.value()); }
};
struct Reader {
    std::span<const std::byte> buffer;
    std::size_t at{};
    bool good{true};
    std::uint64_t number(unsigned size=8) noexcept {
        std::uint64_t v=0;
        for (unsigned n=0;n<size;++n) {
            if (at < buffer.size()) v |= std::uint64_t{std::to_integer<unsigned>(buffer[at])} << (n*8);
            else good=false;
            ++at;
        }
        return v;
    }
    Digest256 digest() noexcept { Digest256 d; for(auto& b:d.bytes) b=static_cast<std::byte>(number(1)); return d; }
    template<typename T> T atom() noexcept { return T{number()}; }
};
void binding(Writer& w,const Binding& b) noexcept {
    w.digest(b.incarnation);w.digest(b.catalog);w.digest(b.custody);
    w.atom(b.account);w.atom(b.instrument);w.atom(b.policy);w.atom(b.policy_version);w.atom(b.risk_policy_version);
}
Binding binding(Reader& r) noexcept {
    return {r.digest(),r.digest(),r.digest(),r.atom<AccountId>(),r.atom<InstrumentId>(),r.atom<PolicyId>(),r.atom<PolicyVersion>(),r.atom<RiskPolicyVersion>()};
}
void limits(Writer& w,const Limits& l) noexcept {
    w.atom(l.max_purchase_total);w.atom(l.max_buy_debit);w.atom(l.max_fee);w.atom(l.max_realized_loss);
    w.atom(l.max_sell_input);w.atom(l.max_transient_base);w.atom(l.max_buy_price_per_sol);w.atom(l.min_sell_price_per_sol);
    w.number(l.max_source_age);w.number(l.max_sign_lifetime);
}
Limits limits(Reader& r) noexcept {
    return {r.atom<QuoteAtoms>(),r.atom<QuoteAtoms>(),r.atom<QuoteAtoms>(),r.atom<QuoteAtoms>(),
        r.atom<BaseAtoms>(),r.atom<BaseAtoms>(),r.atom<QuoteAtoms>(),r.atom<QuoteAtoms>(),r.number(),r.number()};
}
void body(Writer& w,const Initialize& c) noexcept {
    binding(w,c.binding);limits(w,c.limits);w.atom(c.base);w.atom(c.quote);w.atom(c.base_cost);
    w.digest(c.funding_evidence);w.number(c.kill_epoch);w.number(c.executor_generation);
}
void body(Writer& w,const Intent& c) noexcept {
    binding(w,c.binding);w.atom(c.effect);w.number(static_cast<unsigned>(c.side),1);
    w.atom(c.quote_input);w.atom(c.max_quote_debit);w.atom(c.min_quote_receive);w.atom(c.fee_cap);
    w.atom(c.base_input);w.atom(c.min_base_receive);w.atom(c.transient_base_hold);
    w.number(c.source_time);w.number(c.sign_by);w.number(c.kill_epoch);w.number(c.executor_generation);
}
void body(Writer& w,const Prepare& c) noexcept { w.atom(c.effect);w.digest(c.payload);w.digest(c.handle);w.number(c.payload_count); }
void body(Writer& w,const Attempt& c) noexcept { w.atom(c.effect);w.digest(c.payload); }
void body(Writer& w,const MarkUnknown& c) noexcept { w.atom(c.effect); }
void body(Writer& w,const ReleaseUnsent& c) noexcept { w.atom(c.effect);w.digest(c.evidence); }
void body(Writer& w,const Settlement& c) noexcept {
    w.atom(c.effect);w.digest(c.custody);w.digest(c.payload);w.digest(c.transaction);w.digest(c.evidence);
    w.number(static_cast<unsigned>(c.outcome),1);w.atom(c.base_debit);w.atom(c.base_credit);
    w.atom(c.quote_debit);w.atom(c.quote_credit);w.atom(c.fee);w.number(c.finalized,1);
}
void body(Writer& w,const AmendBuyPrice& c) noexcept {
    w.digest(c.opening_digest);w.digest(c.review_evidence);w.atom(c.expected_price);w.atom(c.replacement_price);
    w.number(c.expected_revision);w.number(c.kill_epoch);w.number(c.executor_generation);
}
void body(Writer& w,const Control& c) noexcept { w.number(static_cast<unsigned>(c.mode),1);w.number(c.kill_epoch);w.number(c.executor_generation); }
Command command(Reader& r,unsigned kind) noexcept {
    switch(kind) {
        case 1: return Initialize{binding(r),limits(r),r.atom<BaseAtoms>(),r.atom<QuoteAtoms>(),r.atom<QuoteAtoms>(),r.digest(),r.number(),r.number()};
        case 2: {
            Intent c;c.binding=binding(r);c.effect=r.atom<EffectId>();c.side=static_cast<Side>(r.number(1));
            c.quote_input=r.atom<QuoteAtoms>();c.max_quote_debit=r.atom<QuoteAtoms>();c.min_quote_receive=r.atom<QuoteAtoms>();c.fee_cap=r.atom<QuoteAtoms>();
            c.base_input=r.atom<BaseAtoms>();c.min_base_receive=r.atom<BaseAtoms>();c.transient_base_hold=r.atom<BaseAtoms>();
            c.source_time=r.number();c.sign_by=r.number();c.kill_epoch=r.number();c.executor_generation=r.number();return c;
        }
        case 3: return Prepare{r.atom<EffectId>(),r.digest(),r.digest(),r.number()};
        case 4: return Attempt{r.atom<EffectId>(),r.digest()};
        case 5: return MarkUnknown{r.atom<EffectId>()};
        case 6: return ReleaseUnsent{r.atom<EffectId>(),r.digest()};
        case 7: {
            Settlement c;c.effect=r.atom<EffectId>();c.custody=r.digest();c.payload=r.digest();c.transaction=r.digest();c.evidence=r.digest();
            c.outcome=static_cast<Outcome>(r.number(1));c.base_debit=r.atom<BaseAtoms>();c.base_credit=r.atom<BaseAtoms>();
            c.quote_debit=r.atom<QuoteAtoms>();c.quote_credit=r.atom<QuoteAtoms>();c.fee=r.atom<QuoteAtoms>();
            const auto b=r.number(1);if(b>1)r.good=false;c.finalized=b==1;return c;
        }
        case 8: return Control{static_cast<Mode>(r.number(1)),r.number(),r.number()};
        case 9: return AmendBuyPrice{r.digest(),r.digest(),r.atom<QuoteAtoms>(),r.atom<QuoteAtoms>(),r.number(),r.number(),r.number()};
        default:r.good=false;return Initialize{};
    }
}
}
Digest256 opening_digest(const Initialize& opening) noexcept {
    std::array<std::byte,max_record_bytes> bytes{};
    const auto encoded=encode({Role::operator_role,0,opening},bytes);
    return encoded.error==CodecError::none?sha256_digest(std::span(bytes).first(encoded.size)):Digest256{};
}
Encoded encode(const Record& record,std::span<std::byte> out) noexcept {
    if(record.role!=Role::operator_role && record.role!=Role::strategy && record.role!=Role::gateway) return {CodecError::invalid_value,0};
    Writer w{out};w.number(0x31505356,4);w.number(1,2);w.number(record.command.index()+1,1);w.number(static_cast<unsigned>(record.role),1);
    w.number(0,4);w.number(record.event_time);std::visit([&](const auto& c){body(w,c);},record.command);
    if(!w.good || w.at+32>out.size() || w.at+32>max_record_bytes) return {CodecError::short_buffer,0};
    const auto size=w.at+32;
    for(unsigned n=0;n<4;++n) out[8+n]=static_cast<std::byte>(size>>(n*8));
    w.digest(sha256_digest(out.first(w.at)));return {CodecError::none,size};
}
CodecError decode(std::span<const std::byte> bytes,Record& out) noexcept {
    if(bytes.size()<52 || bytes.size()>max_record_bytes) return CodecError::format;
    Reader r{bytes.first(bytes.size()-32)};
    if(r.number(4)!=0x31505356 || r.number(2)!=1) return CodecError::format;
    const auto kind=static_cast<unsigned>(r.number(1));const auto role=static_cast<Role>(r.number(1));
    if(r.number(4)!=bytes.size() || kind<1 || kind>9) return CodecError::format;
    if(role!=Role::operator_role && role!=Role::strategy && role!=Role::gateway) return CodecError::invalid_value;
    const auto hash=sha256_digest(bytes.first(bytes.size()-32));
    if(!std::equal(hash.bytes.begin(),hash.bytes.end(),bytes.end()-32)) return CodecError::checksum;
    Record candidate;candidate.role=role;candidate.event_time=r.number();candidate.command=command(r,kind);
    if(!r.good || r.at!=r.buffer.size()) return CodecError::format;
    out=candidate;return CodecError::none;
}
Encoded encode_state(const State& s,std::span<std::byte> out) noexcept {
    if(!valid_state(s)) return {CodecError::invalid_value,0};
    const std::size_t size=encoded_state_size(s);
    if(out.size()<size)return {CodecError::short_buffer,0};
    Writer w{out};w.number(s.initialized,1);
    if(!s.initialized)return {CodecError::none,w.at};
    w.number(s.conflicted,1);body(w,s.opening);
    w.atom(s.base);w.atom(s.reserved_base);w.atom(s.quote);w.atom(s.reserved_quote);w.atom(s.cost);w.atom(s.fees);w.atom(s.purchases);
    w.number(static_cast<std::uint64_t>(s.realized_pnl));w.number(static_cast<unsigned>(s.mode),1);
    w.number(s.kill_epoch);w.number(s.executor_generation);w.number(s.event_time);w.number(s.last_effect);
    w.number(static_cast<unsigned>(s.pending.phase),1);body(w,s.pending.intent);w.digest(s.pending.payload);w.digest(s.pending.handle);
    body(w,s.conflict_evidence);body(w,s.last_release);
    for(const auto& x:s.settlements)body(w,x);
    w.number(s.settlement_count,1);
    if (size==max_state_bytes) {
        const auto& a=s.buy_price_amendment;w.number(1,1);w.atom(a.price);w.digest(a.evidence);
        w.number(a.kill_epoch);w.number(a.executor_generation);w.number(a.event_time);
    }
    return w.good && w.at==size ? Encoded{CodecError::none,w.at} : Encoded{CodecError::format,0};
}
CodecError decode_state(std::span<const std::byte> bytes,State& out) noexcept {
    if(bytes.empty() || bytes.size()>max_state_bytes)return CodecError::format;
    Reader r{bytes};State s;const auto initialized=r.number(1);
    if(initialized>1)return CodecError::invalid_value;
    if(initialized==0) {if(bytes.size()!=1)return CodecError::format;out=s;return CodecError::none;}
    if(bytes.size()!=legacy_state_bytes && bytes.size()!=max_state_bytes)return CodecError::format;
    s.initialized=true;const auto conflicted=r.number(1);if(conflicted>1)return CodecError::invalid_value;s.conflicted=conflicted==1;
    s.opening=std::get<Initialize>(command(r,1));
    s.base=r.atom<BaseAtoms>();s.reserved_base=r.atom<BaseAtoms>();s.quote=r.atom<QuoteAtoms>();s.reserved_quote=r.atom<QuoteAtoms>();
    s.cost=r.atom<QuoteAtoms>();s.fees=r.atom<QuoteAtoms>();s.purchases=r.atom<QuoteAtoms>();
    s.realized_pnl=std::bit_cast<std::int64_t>(r.number());s.mode=static_cast<Mode>(r.number(1));
    s.kill_epoch=r.number();s.executor_generation=r.number();s.event_time=r.number();s.last_effect=r.number();
    s.pending.phase=static_cast<Phase>(r.number(1));s.pending.intent=std::get<Intent>(command(r,2));s.pending.payload=r.digest();s.pending.handle=r.digest();
    s.conflict_evidence=std::get<Settlement>(command(r,7));s.last_release=std::get<ReleaseUnsent>(command(r,6));
    for(auto& x:s.settlements)x=std::get<Settlement>(command(r,7));
    s.settlement_count=static_cast<std::uint8_t>(r.number(1));
    if (bytes.size()==max_state_bytes) {
        if(r.number(1)!=1)return CodecError::format;
        s.buy_price_amendment={r.atom<QuoteAtoms>(),r.digest(),r.number(),r.number(),r.number()};
        if(s.buy_price_amendment==BuyPriceAmendment{})return CodecError::invalid_value;
    }
    if(!r.good || r.at!=bytes.size() || !valid_state(s))return CodecError::invalid_value;
    out=s;return CodecError::none;
}
}
