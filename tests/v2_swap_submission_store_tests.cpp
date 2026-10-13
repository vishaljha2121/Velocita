#include "velocita/v2/swap_submission_store.hpp"
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
using Code=SwapSubmissionCode;
using Stage=SwapSubmissionStage;
void require(bool yes,const char* name){if(!yes)throw std::runtime_error(name);}
Digest256 digest(unsigned n){Digest256 d;d.bytes.fill(static_cast<std::byte>(n));return d;}
const SwapSubmissionBinding binding{digest(1),digest(2)};
CallerVerifiedSubmissionBootstrap bootstrap(){return {binding,7,digest(7),EffectId{3}};}
SwapSubmissionIdentity identity(unsigned effect=4){
    SwapSubmissionIdentity i;i.effect=EffectId{effect};i.payload=digest(8);i.execution_request=digest(9);
    i.handle[0]=std::byte{'a'};i.handle[1]=std::byte{'b'};i.handle[2]=std::byte{'c'};i.handle_size=3;
    i.handle_digest=*parse_digest256("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");return i;
}
struct Scratch {
    std::filesystem::path base;
    Scratch(){
        const auto* home=std::getenv("HOME");require(home,"fixture home exists");
        auto root=std::filesystem::path(home)/".cache/velocita-build/swap-submission-store-tests";
        std::filesystem::create_directories(root);auto name=(root/"case-XXXXXX").string();
        require(::mkdtemp(name.data())!=nullptr,"fixture private directory");base=name;
    }
    ~Scratch(){std::error_code ignored;std::filesystem::remove_all(base,ignored);}
    auto path()const{return base/"store";}
    auto file()const{return path()/"submission.bin";}
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
void checksum(std::vector<std::byte>& b){auto hash=sha256_digest(std::span(b).first(432));std::copy(hash.bytes.begin(),hash.bytes.end(),b.begin()+432);}
void seed(const Scratch& s,bool active=false){SwapSubmissionStore store(s.path(),binding);require(store.create(bootstrap())==Code::none,"fixture bootstrap");if(active)require(store.begin(identity(),8)==Code::none,"fixture active");}
void lifecycle_and_exact_bytes(){
    Scratch s;
    {
        SwapSubmissionStore store(s.path(),binding);
        require(store.create(bootstrap())==Code::none,"bootstrap durably records explicit idle state");
        require(store.latest().phase==SwapSubmissionPhase::idle && store.latest().identity.effect==EffectId{3},"bootstrap records last effect floor");
        require(store.begin(identity(),8)==Code::none,"fresh execution intent persisted before send permission");
        auto b=bytes(s.file());require(b.size()==464,"canonical record length is 464");
        require(b[0]==std::byte{'V'} && b[1]==std::byte{'S'} && b[2]==std::byte{'S'} && b[3]==std::byte{'J'} && b[4]==std::byte{1},"canonical magic and version");
        require(b[8]==std::byte{2} && b[16]==std::byte{2} && b[24]==std::byte{8} && b[32]==std::byte{4},"ordinal phase index effect canonical offsets");
        require(std::all_of(b.begin()+200,b.begin()+264,[](auto v){return v==std::byte{};}) && b[264]==std::byte{3} && b[272]==std::byte{'a'} && b[274]==std::byte{'c'},"unknown transaction and retained raw handle bytes");
        const auto expected=*parse_digest256("9bdccce9ee612b067fcf26fbede2f7b592e10509feedd76729a746f2a8e532a8");
        require(std::equal(expected.bytes.begin(),expected.bytes.end(),b.begin()+432),"independent hashlib canonical checksum");
        require(store.latest().identity==identity() && store.latest().phase==SwapSubmissionPhase::execute_intent,"published active identity matches durable request");
    }
    {
        SwapSubmissionStore store(s.path(),binding);require(store.recover()==Code::none,"restart reads active journal");
        require(store.latest().phase==SwapSubmissionPhase::execute_intent && store.latest().identity==identity(),"restart requires reconciliation with retained handle");
        SwapSubmissionTransaction tx{};tx.fill(std::byte{11});
        require(store.observe_transaction(identity(),tx)==Code::none,"transaction identity persists after response");
        auto before=bytes(s.file());
        require(store.observe_transaction(identity(),tx)==Code::none && before==bytes(s.file()),"identical transaction observation is idempotent");
        require(store.resolve({identity(),9,digest(10)})==Code::none,"committed terminal evidence resolves active submission");
    }
    {
        SwapSubmissionStore store(s.path(),binding);require(store.recover()==Code::none,"restart reads terminal journal");
        require(store.latest().phase==SwapSubmissionPhase::resolved && store.latest().committed_index==9 && store.latest().transaction[0]==std::byte{11},"terminal evidence and transaction survive restart");
        require(store.begin(identity(5),10)==Code::none && store.latest().transaction==SwapSubmissionTransaction{},"only later effect may begin after terminal evidence");
    }
}
void no_resend_and_identity_guards(){
    for(unsigned variant=0;variant<9;++variant){
        Scratch s;seed(s,true);SwapSubmissionStore store(s.path(),binding);require(store.recover()==Code::none,"fixture recovered active");auto before=bytes(s.file());
        Code code{};
        if(variant==0)code=store.begin(identity(),8);
        if(variant==1)code=store.begin(identity(5),9);
        if(variant==2){auto i=identity();i.payload=digest(88);code=store.resolve({i,9,digest(10)});}
        if(variant==3)code=store.resolve({identity(),8,digest(10)});
        if(variant==4)code=store.resolve({identity(),9,{}});
        if(variant==5){auto i=identity();i.execution_request=digest(88);code=store.resolve({i,9,digest(10)});}
        if(variant==6){auto i=identity();i.effect=EffectId{5};code=store.resolve({i,9,digest(10)});}
        if(variant==7){auto i=identity();i.handle[0]=std::byte{'z'};code=store.resolve({i,9,digest(10)});}
        if(variant==8){SwapSubmissionTransaction tx{};code=store.observe_transaction(identity(),tx);}
        require(code==Code::bad_transition && store.status()==Code::bad_transition && before==bytes(s.file()),"replay active overlap wrong identity empty evidence stale index refuse without rewrite");
        require(store.begin(identity(6),12)==Code::closed,"refusal latches store with no automatic resend");
    }
    for(unsigned variant=0;variant<6;++variant){
        Scratch s;SwapSubmissionStore store(s.path(),binding);require(store.create(bootstrap())==Code::none,"fixture idle");auto i=identity();auto index=8;
        if(variant==0)i.effect=EffectId{3};
        if(variant==1)index=7;
        if(variant==2)i.handle_digest=digest(55);
        if(variant==3)i.handle[127]=std::byte{1};
        if(variant==4)i.handle_size=129;
        if(variant==5)i.execution_request={};
        require(store.begin(i,index)!=Code::none && store.latest().phase==SwapSubmissionPhase::idle,"invalid effect index handle padding length and execution digest cannot authorize send");
    }
    {
        Scratch s;seed(s,true);SwapSubmissionStore store(s.path(),binding);require(store.recover()==Code::none,"fixture active");
        SwapSubmissionTransaction a{},b{};a.fill(std::byte{1});b.fill(std::byte{2});
        require(store.observe_transaction(identity(),a)==Code::none,"fixture known transaction");
        require(store.observe_transaction(identity(),b)==Code::bad_transition,"conflicting transaction cannot overwrite first identity");
    }
}
void recovery_corruption_and_binding(){
    {Scratch s;SwapSubmissionStore store(s.path(),binding);auto bad=bootstrap();bad.state_digest={};require(store.create(bad)==Code::invalid_input,"invalid bootstrap refuses");require(store.create(bootstrap())==Code::closed && !std::filesystem::exists(s.path()),"invalid bootstrap cannot retry implicitly");}
    {Scratch s;SwapSubmissionStore store(s.path(),binding);require(store.recover()==Code::missing && !std::filesystem::exists(s.path()),"missing journal never means unsent or bootstraps");}
    for(unsigned variant=0;variant<9;++variant){
        Scratch s;seed(s,true);auto b=bytes(s.file());
        if(variant==0)b.pop_back();
        if(variant==1)b.push_back(std::byte{});
        if(variant==2)b[104]^=std::byte{1};
        if(variant==3){b[40]=std::byte{3};checksum(b);}
        if(variant==4){b[72]=std::byte{3};checksum(b);}
        if(variant==5){put64(b,16,9);checksum(b);}
        if(variant==6){b[399]=std::byte{1};checksum(b);}
        if(variant==7){put64(b,24,0);checksum(b);}
        if(variant==8){b[272]=std::byte{'z'};checksum(b);}
        write(s.file(),b);SwapSubmissionStore store(s.path(),binding);
        require(store.recover()==Code::corrupt,"short trailing checksum binding phase padding index and handle corruption refuse");
    }
    {
        Scratch s;seed(s);auto b=bytes(s.file());put64(b,8,UINT64_MAX);checksum(b);write(s.file(),b);
        SwapSubmissionStore store(s.path(),binding);require(store.recover()==Code::none,"fixture max ordinal");
        require(store.begin(identity(),8)==Code::exhausted,"ordinal exhaustion never wraps");
    }
}
struct Fault {
    Stage stage;bool armed{};SwapSubmissionStore* store{};bool premature{};
    static bool proceed(Stage stage,void* context)noexcept{auto& f=*static_cast<Fault*>(context);if(f.armed && f.store->latest().phase!=SwapSubmissionPhase::idle)f.premature=true;return !f.armed || stage!=f.stage;}
};
void durability_failures(){
    for(auto stage:{Stage::partial_write,Stage::temporary_sync,Stage::rename,Stage::directory_sync,Stage::before_publish}){
        Scratch s;
        {Fault f{stage};SwapSubmissionStore store(s.path(),binding,{Fault::proceed,&f});f.store=&store;require(store.create(bootstrap())==Code::none,"fixture before fault");f.armed=true;
        require(store.begin(identity(),8)==Code::io_failure && !f.premature && store.latest().phase==SwapSubmissionPhase::idle,"durability failure never grants execution or publishes intent");
        require(store.begin(identity(),8)==Code::closed,"failed write cannot retry execution");}
        SwapSubmissionStore recovered(s.path(),binding);
        if(stage==Stage::directory_sync || stage==Stage::before_publish)require(recovered.recover()==Code::none && recovered.latest().phase==SwapSubmissionPhase::execute_intent,"renamed unpublished intent recovers reconcile only");
        else require(recovered.recover()==Code::temporary_present,"partial intent refuses ambiguous recovery");
    }
    {Scratch s;Fault f{Stage::parent_sync,true};SwapSubmissionStore store(s.path(),binding,{Fault::proceed,&f});f.store=&store;require(store.create(bootstrap())==Code::io_failure,"bootstrap parent sync failure refuses");}
    {Scratch s;seed(s);Fault f{Stage::recovery_sync,true};SwapSubmissionStore store(s.path(),binding,{Fault::proceed,&f});f.store=&store;require(store.recover()==Code::io_failure,"recovery sync failure refuses");}
}
struct Crash{Stage stage;bool armed{};static bool proceed(Stage stage,void* p)noexcept{auto& c=*static_cast<Crash*>(p);if(c.armed && c.stage==stage)::_exit(77);return true;}};
void abrupt_exit(){
    for(auto stage:{Stage::partial_write,Stage::temporary_sync,Stage::rename,Stage::directory_sync,Stage::before_publish}){
        Scratch s;seed(s);auto pid=::fork();require(pid>=0,"fixture fork");
        if(pid==0){Crash c{stage};SwapSubmissionStore store(s.path(),binding,{Crash::proceed,&c});if(store.recover()!=Code::none)::_exit(99);c.armed=true;(void)store.begin(identity(),8);::_exit(98);}
        int status{};require(::waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==77,"process dies at designated boundary");
        SwapSubmissionStore store(s.path(),binding);
        if(stage==Stage::directory_sync || stage==Stage::before_publish)require(store.recover()==Code::none && store.latest().phase==SwapSubmissionPhase::execute_intent,"crash after rename retains uncertain request identity");
        else require(store.recover()==Code::temporary_present,"crash before rename remains closed with evidence");
    }
}
void filesystem_guards(){
    {Scratch s;SwapSubmissionStore a(s.path(),binding),b(s.path(),binding);require(a.create(bootstrap())==Code::none,"fixture owner");require(b.recover()==Code::locked && a.status()==Code::none,"second writer cannot own journal");}
    for(unsigned variant=0;variant<5;++variant){Scratch s;seed(s);
        if(variant==0)require(::chmod(s.file().c_str(),0644)==0,"fixture public");
        if(variant==1)std::filesystem::create_hard_link(s.file(),s.base/"link");
        if(variant==2){std::filesystem::rename(s.file(),s.base/"old");std::filesystem::create_symlink(s.base/"old",s.file());}
        if(variant==3){std::filesystem::remove(s.file());require(::mkfifo(s.file().c_str(),0600)==0,"fixture fifo");}
        if(variant==4)require(::chmod(s.path().c_str(),0755)==0,"fixture public directory");
        SwapSubmissionStore store(s.path(),binding);require(store.recover()==Code::insecure,"public linked symlink fifo directory records refuse");}
    {Scratch s;SwapSubmissionStore store(s.path(),binding);require(store.create(bootstrap())==Code::none,"fixture replace");auto b=bytes(s.file());std::filesystem::rename(s.file(),s.base/"old");write(s.file(),b);require(store.begin(identity(),8)==Code::insecure,"named record replacement refuses");}
    {Scratch s;seed(s);SwapSubmissionStore store(s.path(),binding);require(store.create(bootstrap())==Code::already_exists,"existing journal cannot bootstrap over prior intent");}
}
}
int main(int argc,char** argv){
    const std::pair<const char*,void(*)()> cases[]{
        {"lifecycle_and_exact_bytes",lifecycle_and_exact_bytes},{"no_resend_and_identity_guards",no_resend_and_identity_guards},
        {"recovery_corruption_and_binding",recovery_corruption_and_binding},{"durability_failures",durability_failures},
        {"abrupt_exit",abrupt_exit},{"filesystem_guards",filesystem_guards}};
    for(const auto& [name,run]:cases)if(argc==1 || std::string(argv[1])==name){try{run();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';return 1;}}
}
