#include "velocita/v2/swap_paper_codec.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unistd.h>

namespace {
using namespace velocita::v2;
using namespace velocita::v2::swap_paper;
constexpr std::size_t max_file_bytes = 64 * max_record_bytes + 40;
Digest256 digest(unsigned v) { Digest256 d;d.bytes[0]=static_cast<std::byte>(v);return d; }
void need(bool v,const char* text) { if(!v)throw std::runtime_error(text); }
struct File {
    int fd{-1};
    File(const char* path,int flags):fd(::open(path,flags|O_NOFOLLOW,0600)) { need(fd>=0,"journal open refused"); }
    ~File(){if(fd>=0)::close(fd);}
    File(const File&)=delete;
    File& operator=(const File&)=delete;
};
void write_all(int fd,std::span<const std::byte> data) {
    while(!data.empty()) {
        const auto n=::write(fd,data.data(),data.size());
        if(n<0 && errno==EINTR)continue;
        need(n>0,"journal write failed");data=data.subspan(static_cast<std::size_t>(n));
    }
    need(::fsync(fd)==0,"journal sync failed");
}
void summary(const State& s,unsigned count) {
    std::cout<<"{\"paper_only\":true,\"records\":"<<count
        <<",\"base_atoms\":"<<s.base.value()<<",\"quote_atoms\":"<<s.quote.value()
        <<",\"cost_quote_atoms\":"<<s.cost.value()<<",\"fees_quote_atoms\":"<<s.fees.value()
        <<",\"realized_pnl_quote_atoms\":"<<s.realized_pnl
        <<",\"purchase_debits_quote_atoms\":"<<s.purchases.value()
        <<",\"reserved_quote_atoms\":"<<s.reserved_quote.value()
        <<",\"reserved_base_atoms\":"<<s.reserved_base.value()
        <<",\"pending_phase\":"<<static_cast<unsigned>(s.pending.phase)
        <<",\"settlements\":"<<static_cast<unsigned>(s.settlement_count)
        <<",\"state_bytes\":"<<sizeof(State)<<",\"command_bytes\":"<<sizeof(Command)<<"}\n";
}
struct Run {
    State state;
    Sha256 hash;
    unsigned count{};
    File& file;
    explicit Run(File& f):file(f){}
    void commit(const Command& c,Role role,std::uint64_t time) {
        State next=state;need(apply(next,c,role,time)==Code::applied,"drill command refused");
        std::array<std::byte,max_record_bytes> bytes{};
        const auto record=encode({role,time,c},bytes);need(record.error==CodecError::none,"record encode failed");
        const auto data=std::span(bytes).first(record.size);write_all(file.fd,data);hash.update(data);
        state=next;++count;
    }
    void seal() {
        std::array<std::byte,40> footer{std::byte{'E'},std::byte{'N'},std::byte{'D'},std::byte{'1'}};
        for(unsigned i=0;i<4;++i)footer[4+i]=static_cast<std::byte>(count>>(8*i));
        const auto d=hash.finish();std::copy(d.bytes.begin(),d.bytes.end(),footer.begin()+8);
        write_all(file.fd,footer);
    }
};
Initialize initial() {
    Initialize c;c.binding={digest(1),digest(2),digest(3),AccountId{2},InstrumentId{2},PolicyId{901},PolicyVersion{1},RiskPolicyVersion{1}};
    c.limits={QuoteAtoms{5'000'000},QuoteAtoms{2'000'000},QuoteAtoms{10'000},QuoteAtoms{500'000},BaseAtoms{1'000'000'000},BaseAtoms{},QuoteAtoms{125'000'000},QuoteAtoms{50'000'000},10,20};
    c.quote=QuoteAtoms{5'000'000};c.funding_evidence=digest(4);c.kill_epoch=1;c.executor_generation=1;return c;
}
Intent buy(const State& s,unsigned id,std::uint64_t now) {
    Intent i;i.binding=s.opening.binding;i.effect=EffectId{id};i.side=Side::buy;
    i.quote_input=QuoteAtoms{1'000'000};i.max_quote_debit=QuoteAtoms{1'002'000};i.fee_cap=QuoteAtoms{2'000};
    i.min_base_receive=BaseAtoms{9'000'000};i.source_time=now;i.sign_by=now+20;i.kill_epoch=1;i.executor_generation=1;return i;
}
void start(Run& r,const Intent& i,std::uint64_t now) {
    r.commit(i,Role::strategy,now);
    r.commit(Prepare{i.effect,digest(10+i.effect.value()),digest(20+i.effect.value()),1},Role::gateway,now+1);
    r.commit(Attempt{i.effect,digest(10+i.effect.value())},Role::gateway,now+2);
}
void create(const char* path) {
    File f(path,O_WRONLY|O_CREAT|O_EXCL);Run r(f);r.commit(initial(),Role::operator_role,100);
    start(r,buy(r.state,1,101),101);
    const auto first=simulate(r.state,{QuoteAtoms{108'000'000},2'000},digest(31),digest(41));
    need(first.code==Code::applied && first.settlement.outcome==Outcome::succeeded,"buy fixture failed");
    r.commit(first.settlement,Role::gateway,104);
    auto sell=buy(r.state,2,105);sell.side=Side::sell;sell.base_input=r.state.base;
    sell.quote_input={};sell.max_quote_debit={};sell.min_base_receive={};sell.min_quote_receive=QuoteAtoms{1'000'000};sell.fee_cap=QuoteAtoms{3'000};
    start(r,sell,105);
    const auto second=simulate(r.state,{QuoteAtoms{110'000'000},2'000},digest(32),digest(42));
    need(second.code==Code::applied && second.settlement.outcome==Outcome::succeeded,"sell fixture failed");
    r.commit(MarkUnknown{sell.effect},Role::gateway,108);r.commit(second.settlement,Role::gateway,109);
    start(r,buy(r.state,3,110),110);r.commit(MarkUnknown{EffectId{3}},Role::gateway,113);
    r.seal();summary(r.state,r.count);
}
std::uint32_t little32(std::span<const std::byte> b) {
    std::uint32_t v=0;for(unsigned n=0;n<4;++n)v|=std::to_integer<std::uint32_t>(b[n])<<(8*n);return v;
}
void replay(const char* path) {
    File f(path,O_RDONLY);std::array<std::byte,max_file_bytes+1> data{};std::size_t size=0;
    while(size<data.size()) {
        const auto n=::read(f.fd,data.data()+size,data.size()-size);
        if(n<0 && errno==EINTR)continue;
        need(n>=0,"journal read failed");if(n==0)break;size+=static_cast<std::size_t>(n);
    }
    need(size>=40 && size<=max_file_bytes,"journal size invalid");
    const auto bytes=std::span(data).first(size);const auto footer=bytes.last(40);
    need(footer[0]==std::byte{'E'} && footer[1]==std::byte{'N'} && footer[2]==std::byte{'D'} && footer[3]==std::byte{'1'},"unsealed or truncated journal");
    const auto digest_value=sha256_digest(bytes.first(size-40));
    need(std::equal(digest_value.bytes.begin(),digest_value.bytes.end(),footer.begin()+8),"journal digest mismatch");
    const auto expected=little32(footer.subspan(4,4));need(expected>0 && expected<=64,"journal record count invalid");
    std::size_t at=0;unsigned count=0;State state;
    while(at<size-40) {
        need(size-40-at>=12,"truncated record header");const auto length=little32(bytes.subspan(at+8,4));
        need(length>=52 && length<=max_record_bytes && length<=size-40-at,"invalid record length");
        Record record;need(decode(bytes.subspan(at,length),record)==CodecError::none,"record verification failed");
        need(apply(state,record.command,record.role,record.event_time)==Code::applied,"journal transition refused");
        ++count;at+=length;
    }
    need(count==expected,"journal record count mismatch");summary(state,count);
}
}
int main(int argc,char** argv) {
    try {
        if(argc==3 && std::string_view(argv[1])=="--create")create(argv[2]);
        else if(argc==3 && std::string_view(argv[1])=="--replay")replay(argv[2]);
        else {std::cerr<<"Usage: v2-swap-paper-drill --create NEW_FILE | --replay FILE\n";return 2;}
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
