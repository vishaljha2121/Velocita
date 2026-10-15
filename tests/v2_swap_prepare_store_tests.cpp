#include "velocita/v2/swap_prepare_store.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
using namespace velocita::v2;
using Code=SwapPrepareStoreCode;
using Stage=SwapPrepareStoreStage;
void require(bool yes,const char* name){if(!yes)throw std::runtime_error(name);}
Digest256 digest(unsigned n){Digest256 d;d.bytes.fill(static_cast<std::byte>(n));return d;}
const SwapPrepareBinding binding{{digest(1),digest(2),digest(3),AccountId{2},InstrumentId{2},PolicyId{901},PolicyVersion{1},RiskPolicyVersion{1}},digest(4),digest(5)};
CallerVerifiedPrepareBootstrap bootstrap(){return {binding,7,digest(7),EffectId{3}};}
SwapPrepareOperation operation(unsigned effect=4){
    constexpr std::string_view body=R"({"base_asset":"So11111111111111111111111111111111111111112","chain":"solana","side":"buy","type":"market","qty":"1.000000","qty_unit":"quote"})";
    return {EffectId{effect},digest(8),sha256_digest(std::as_bytes(std::span(body.data(),body.size()))),{Side::buy,1'000'000}};
}
truemarkets::SwapRestPrepared payload(){
    truemarkets::SwapRestPrepared p;p.handle=*truemarkets::tm_text("order-1");
    constexpr std::string_view text="{\"activity\":\"synthetic\"}\n";
    std::copy(text.begin(),text.end(),p.payload.begin());p.payload_size=text.size();
    p.payload_digest=*parse_digest256("3a02efff5acd5e623cd99f11791246c191fb38fd52ab29fa4f2bac1fc66f7256");
    p.quoted_input=1'000'000;p.quoted_output=9'191'912;p.quoted_fee=2'000;return p;
}
CallerVerifiedPreparePredecessor predecessor(){
    swap::Settlement x;x.effect=EffectId{4};x.custody=binding.allocation.custody;x.payload=payload().payload_digest;
    x.transaction=digest(70);x.evidence=digest(71);x.finalized=true;x.outcome=swap::Outcome::succeeded;
    return {x,10,digest(10)};
}
struct Scratch {
    std::filesystem::path base;
    Scratch(){
        const auto* home=std::getenv("HOME");require(home,"fixture home exists");
        auto root=std::filesystem::path(home)/".cache/velocita-build/swap-prepare-store-tests";
        std::filesystem::create_directories(root);auto name=(root/"case-XXXXXX").string();
        require(::mkdtemp(name.data())!=nullptr,"fixture private directory");base=name;
    }
    ~Scratch(){std::error_code ignored;std::filesystem::remove_all(base,ignored);}
    auto path()const{return base/"store";}
    auto file()const{return path()/"prepare.bin";}
};
std::vector<std::byte> bytes(const std::filesystem::path& path){
    std::ifstream in(path,std::ios::binary);std::vector<std::byte> out;char c;
    while(in.get(c))out.push_back(static_cast<std::byte>(c));return out;
}
void write(const std::filesystem::path& path,const std::vector<std::byte>& b){
    std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()));out.close();
    require(::chmod(path.c_str(),0600)==0,"fixture private file");
}
void put64(std::vector<std::byte>& b,std::size_t offset,std::uint64_t v){for(unsigned n=0;n<8;++n)b[offset+n]=static_cast<std::byte>(v>>(8*n));}
void checksum(std::vector<std::byte>& b){auto hash=sha256_digest(std::span(b).first(8736));std::copy(hash.bytes.begin(),hash.bytes.end(),b.begin()+8736);}
void seed(const Scratch& s,bool active=false,bool complete=false){SwapPrepareStore store(s.path(),binding);require(store.create(bootstrap())==Code::none,"fixture bootstrap");if(active)require(store.begin(operation(),8,digest(9))==Code::none,"fixture active");if(complete)require(store.complete(payload(),digest(10))==Code::none,"fixture prepared");}
void owned_payload_and_recovery(){
    Scratch s;
    {
        SwapPrepareStore store(s.path(),binding);require(store.create(bootstrap())==Code::none,"explicit prepare bootstrap durably retained");
        require(store.begin(operation(),8,digest(9))==Code::none,"prepare intent persists before POST permission");
        auto b=bytes(s.file());require(b.size()==8768 && b[0]==std::byte{'V'} && b[1]==std::byte{'P'} && b[2]==std::byte{'R'} && b[3]==std::byte{'P'},"canonical prepare length and magic");
        require(b[8]==std::byte{2} && b[16]==std::byte{2} && b[24]==std::byte{8} && b[32]==std::byte{4},"canonical prepare ordinal phase index effect offsets");
        const auto expected=*parse_digest256("f999284bfbe90c522a83a7b621786dbae041c7c701ccb4c818798a052cb5d6fd");
        require(std::equal(expected.bytes.begin(),expected.bytes.end(),b.begin()+8736),"independent Python canonical prepare checksum oracle");
        auto p=payload();require(store.complete(p,digest(10))==Code::none,"exact decoded payload durably retained before publication");
        p.payload.fill('x');require(store.latest().prepared.payload_view()==payload().payload_view(),"store owns original payload independently of HTTP buffer");
    }
    SwapPrepareStore recovered(s.path(),binding);require(recovered.recover()==Code::none && recovered.latest().phase==SwapPreparePhase::prepared &&
        recovered.latest().prepared.payload_view()==payload().payload_view() && recovered.latest().prepared.handle.view()=="order-1" &&
        recovered.latest().prepared.payload_digest==payload().payload_digest && recovered.latest().prepared.quoted_fee==2'000,
        "recovery retains exact original payload handle hash and quote fields");
}
void one_shot_and_prior_settlement(){
    for(unsigned mode=0;mode<8;++mode){
        Scratch s;seed(s,true,true);SwapPrepareStore store(s.path(),binding);require(store.recover()==Code::none,"fixture prepared restart");
        auto proof=predecessor();const auto* ptr=&proof;auto next=operation(5);
        if(mode==0)ptr=nullptr;
        if(mode==1)proof.settlement.effect=EffectId{99};
        if(mode==2)proof.settlement.payload=digest(99);
        if(mode==3)proof.settlement.custody=digest(99);
        if(mode==4)proof.settlement.finalized=false;
        if(mode==5)proof.committed_index=9;
        if(mode==6)proof.state_digest=digest(99);
        if(mode==7)next.effect=EffectId{4};
        require(store.begin(next,10,digest(10),ptr)==Code::bad_transition && store.latest().phase==SwapPreparePhase::prepared,
            "new prepare requires higher effect and exact finalized predecessor at authenticated index");
        require(store.begin(operation(6),11,digest(11),&proof)==Code::closed,"prepare refusal never retries implicitly");
    }
    {
        Scratch s;seed(s,true,true);SwapPrepareStore store(s.path(),binding);require(store.recover()==Code::none,"fixture old prepared");auto proof=predecessor();
        require(store.begin(operation(5),10,digest(10),&proof)==Code::none && store.latest().phase==SwapPreparePhase::create_intent &&
            store.latest().operation.effect==EffectId{5} && store.latest().prepared.payload_size==0,"proved predecessor permits one later prepare and clears old payload");
    }
    {
        Scratch s;seed(s,true);SwapPrepareStore store(s.path(),binding);require(store.recover()==Code::none,"fixture unknown create");
        require(store.begin(operation(5),10,digest(10))==Code::bad_transition,"unknown create cannot be replaced even with caller predecessor claim");
    }
}
void unsent_create_release(){
    {
        Scratch s;seed(s,true);SwapPrepareStore store(s.path(),binding);require(store.recover()==Code::none,"fixture unsent create");
        const auto created=store.latest().checksum;
        require(store.release(8,digest(12))==Code::none && store.latest().phase==SwapPreparePhase::released &&
            store.latest().response_digest==created && store.latest().operation.effect==EffectId{4} &&
            store.latest().committed_index==8 && store.latest().state_digest==digest(12) && store.latest().prepared.payload_size==0,
            "unsent create is released with its own create record as evidence");
        auto b=bytes(s.file());require(b[16]==std::byte{4},"released phase is four on disk");
    }
    {
        Scratch s;seed(s,true);
        {SwapPrepareStore store(s.path(),binding);require(store.recover()==Code::none && store.release(9,digest(12))==Code::none,"fixture released");}
        {SwapPrepareStore store(s.path(),binding);require(store.recover()==Code::none && store.latest().phase==SwapPreparePhase::released,"released record recovers");
        require(store.begin(operation(4),10,digest(13))==Code::bad_transition,"released effect is never prepared again");}
        {SwapPrepareStore again(s.path(),binding);require(again.recover()==Code::none,"fixture released restart");
        auto proof=predecessor();require(again.begin(operation(5),10,digest(13),&proof)==Code::bad_transition,"released effect has no settlement predecessor");}
        SwapPrepareStore later(s.path(),binding);require(later.recover()==Code::none,"fixture released restart again");
        require(later.begin(operation(5),10,digest(13))==Code::none && later.latest().phase==SwapPreparePhase::create_intent &&
            later.latest().operation.effect==EffectId{5},"released effect permits one later create");
    }
    for(unsigned mode=0;mode<5;++mode){
        Scratch s;seed(s,mode!=0,mode==1);SwapPrepareStore store(s.path(),binding);require(store.recover()==Code::none,"fixture release refusal");
        if(mode==3)require(store.release(9,digest(12))==Code::none,"fixture already released");
        const auto index=mode==2?7:9;const auto state=mode==4?Digest256{}:digest(12);
        if(mode==4 || mode==2){require(store.release(index,state)!=Code::none,"stale or unproven release read refused");continue;}
        require(store.release(10,digest(14))==Code::bad_transition,"only an unsent create may be released");
    }
}
void payload_and_request_guards(){
    for(unsigned mode=0;mode<6;++mode){Scratch s;seed(s,true);SwapPrepareStore store(s.path(),binding);require(store.recover()==Code::none,"fixture active");auto p=payload();
        if(mode==0)p.payload_digest=digest(99);
        if(mode==1)p.quoted_input=2'000'000;
        if(mode==2)p.handle=*truemarkets::tm_text("../other");
        if(mode==3)p.payload_size=8193;
        if(mode==4)p.payload.back()='x';
        if(mode==5)p.quoted_output=0;
        require(store.complete(p,digest(10))==Code::invalid_input && store.latest().phase==SwapPreparePhase::create_intent,"malformed payload quote handle length padding or digest cannot publish prepared state");}
    {Scratch s;SwapPrepareStore store(s.path(),binding);require(store.create(bootstrap())==Code::none,"fixture idle");auto op=operation();op.request_digest=digest(99);
        require(store.begin(op,8,digest(9))==Code::invalid_input,"create request digest must match canonical exact request");}
    {Scratch s;SwapPrepareStore store(s.path(),binding);require(store.create(bootstrap())==Code::none,"fixture idle");auto op=operation();
        constexpr std::string_view legacy=R"({"asset_id":"495e07ac-fa3e-4179-85b7-b1e8cece3dc4","side":"buy","type":"market","qty":"1.000000","qty_unit":"quote"})";
        op.request_digest=sha256_digest(std::as_bytes(std::span(legacy.data(),legacy.size())));
        require(store.begin(op,8,digest(9))==Code::invalid_input && store.latest().phase==SwapPreparePhase::idle,
            "legacy request digest cannot authorize a different create body");}
}
void corrupt_and_missing_recovery(){
    {Scratch s;SwapPrepareStore store(s.path(),binding);require(store.recover()==Code::missing && !std::filesystem::exists(s.path()),"missing prepare journal never proves no POST");}
    for(unsigned mode=0;mode<7;++mode){Scratch s;seed(s,true,true);auto b=bytes(s.file());
        if(mode==0)b.pop_back();
        if(mode==1)b.push_back(std::byte{});
        if(mode==2)b[304]^=std::byte{1};
        if(mode==3){b[104]^=std::byte{1};checksum(b);}
        if(mode==4){b[208]^=std::byte{1};checksum(b);}
        if(mode==5){b[544]^=std::byte{1};checksum(b);}
        if(mode==6){put64(b,16,99);checksum(b);}
        write(s.file(),b);SwapPrepareStore store(s.path(),binding);require(store.recover()==Code::corrupt,"torn trailing checksum custody mapping payload and phase corruption refuse");}
}
struct Fault{Stage stage;bool armed{};SwapPrepareStore* store{};bool premature{};
    static bool proceed(Stage stage,void* p)noexcept{auto& f=*static_cast<Fault*>(p);if(f.armed && f.store->latest().phase!=SwapPreparePhase::create_intent)f.premature=true;return !f.armed || stage!=f.stage;}};
void durability_and_single_writer(){
    for(auto stage:{Stage::partial_write,Stage::temporary_sync,Stage::rename,Stage::directory_sync,Stage::before_publish}){
        Scratch s;
        {Fault f{stage};SwapPrepareStore store(s.path(),binding,{Fault::proceed,&f});f.store=&store;
        require(store.create(bootstrap())==Code::none && store.begin(operation(),8,digest(9))==Code::none,"fixture before prepare persistence fault");f.armed=true;
        require(store.complete(payload(),digest(10))==Code::io_failure && !f.premature && store.latest().phase==SwapPreparePhase::create_intent,"failed durability never publishes usable prepared bytes");}
        SwapPrepareStore recovered(s.path(),binding);
        if(stage==Stage::directory_sync || stage==Stage::before_publish)require(recovered.recover()==Code::none && recovered.latest().phase==SwapPreparePhase::prepared,"post rename payload recovery preserves exact bytes without POST");
        else require(recovered.recover()==Code::temporary_present,"partial prepare response recovery stays closed");
    }
    {Scratch s;SwapPrepareStore a(s.path(),binding),b(s.path(),binding);require(a.create(bootstrap())==Code::none,"fixture writer");require(b.recover()==Code::locked,"second prepare writer refuses");}
    {Scratch s;seed(s);require(::chmod(s.file().c_str(),0644)==0,"fixture public record");SwapPrepareStore store(s.path(),binding);require(store.recover()==Code::insecure,"public prepare payload file refuses");}
}
struct Crash{Stage stage;bool armed{};static bool proceed(Stage stage,void* p)noexcept{auto& c=*static_cast<Crash*>(p);if(c.armed && c.stage==stage)::_exit(77);return true;}};
void abrupt_exit_boundaries(){
    for(bool completing:{false,true})for(auto stage:{Stage::partial_write,Stage::temporary_sync,Stage::rename,Stage::directory_sync,Stage::before_publish}){
        Scratch s;seed(s,completing);const auto pid=::fork();require(pid>=0,"fixture prepare fork");
        if(pid==0){Crash c{stage};SwapPrepareStore store(s.path(),binding,{Crash::proceed,&c});if(store.recover()!=Code::none)::_exit(99);c.armed=true;
            if(completing)(void)store.complete(payload(),digest(10));else(void)store.begin(operation(),8,digest(9));::_exit(98);}
        int status{};require(::waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==77,"process exits at designated prepare persistence boundary");
        SwapPrepareStore recovered(s.path(),binding);
        if(stage==Stage::directory_sync || stage==Stage::before_publish)require(recovered.recover()==Code::none &&
            recovered.latest().phase==(completing?SwapPreparePhase::prepared:SwapPreparePhase::create_intent),"renamed request or payload survives abrupt exit without resend");
        else require(recovered.recover()==Code::temporary_present,"torn request or payload remains closed after abrupt exit");
    }
}

}
int main(int argc,char** argv){
    const std::pair<const char*,void(*)()> cases[]{
        {"owned_payload_and_recovery",owned_payload_and_recovery},{"unsent_create_release",unsent_create_release},{"one_shot_and_prior_settlement",one_shot_and_prior_settlement},
        {"payload_and_request_guards",payload_and_request_guards},{"corrupt_and_missing_recovery",corrupt_and_missing_recovery},
        {"durability_and_single_writer",durability_and_single_writer},{"abrupt_exit_boundaries",abrupt_exit_boundaries}};
    unsigned ran{},failed{};for(const auto& [name,run]:cases)if(argc==1 || std::string(argv[1])==name){++ran;try{run();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return ran && !failed?0:1;
}
